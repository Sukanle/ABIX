/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef SKL_ABIX_ATOMIC_H
#define SKL_ABIX_ATOMIC_H

#include <type_traits>

#include "config.h"
#ifdef SKL_ABIX_WINDOWS
#  include <intrin.h>
#endif

SKL_ABIX_NAMESPACE_BEGIN

namespace atomic {

template<typename T>
struct is_atomic : std::integral_constant<bool, std::is_arithmetic<T>::value || std::is_pointer<T>::value> {};
template<typename T>
using is_atomic_cas = std::is_arithmetic<T>;

template<typename T, typename = typename std::enable_if<is_atomic<T>::value>::type>
inline T load_acquire(const T *p) noexcept {
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}
template<typename T, typename = typename std::enable_if<is_atomic<T>::value>::type>
inline T load_relaxed(const T *p) noexcept {
    return __atomic_load_n(p, __ATOMIC_RELAXED);
}
template<typename T, typename V, typename = typename std::enable_if<is_atomic<T>::value>::type,
    typename = typename std::enable_if<std::is_convertible<V, T>::value>::type>
inline void store_release(T *p, V v) noexcept {
    __atomic_store_n(p, static_cast<T>(v), __ATOMIC_RELEASE);
}
template<typename T, typename V, typename = typename std::enable_if<is_atomic<T>::value>::type,
    typename = typename std::enable_if<std::is_convertible<V, T>::value>::type>
inline void store_relaxed(T *p, V v) noexcept {
    __atomic_store_n(p, static_cast<T>(v), __ATOMIC_RELAXED);
}
template<typename T, typename = typename std::enable_if<is_atomic<T>::value>::type>
inline T inc_relaxed(T *p) noexcept {
    return __atomic_add_fetch(p, 1, __ATOMIC_RELAXED);
}
template<typename T, typename = typename std::enable_if<is_atomic<T>::value>::type>
inline T dec_relaxed(T *p) noexcept {
    return __atomic_sub_fetch(p, 1, __ATOMIC_RELAXED);
}
template<typename T, typename = typename std::enable_if<is_atomic<T>::value>::type>
inline T inc_acq_rel(T *p) noexcept {
    return __atomic_add_fetch(p, 1, __ATOMIC_ACQ_REL);
}
template<typename T, typename V, typename = typename std::enable_if<is_atomic<T>::value>::type,
    typename = typename std::enable_if<std::is_convertible<V, T>::value>::type>
inline T exchange_acq_rel(T *p, V v) noexcept {
    return __atomic_exchange_n(p, static_cast<T>(v), __ATOMIC_ACQ_REL);
}
template<typename T, typename = typename std::enable_if<is_atomic<T>::value>::type>
inline bool cas_relaxed(T *p, T expected, T desired) noexcept {
    return __atomic_compare_exchange_n(p, &expected, desired, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}

}   // namespace atomic

SKL_ABIX_NAMESPACE_END
#endif   // SKL_ABIX_ATOMIC_H