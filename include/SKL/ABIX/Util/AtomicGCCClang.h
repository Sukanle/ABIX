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
#ifndef SKL_ABIX_UTIL_ATOMIC_GCC_CLANG_H
#define SKL_ABIX_UTIL_ATOMIC_GCC_CLANG_H

// GCC / Clang backend for SKL/ABIX/Util/Atomic.h, built on the compiler `__atomic`
// builtins. When the target has a native instruction for the type and operation
// the compiler emits it directly; otherwise the builtin may lower to a runtime
// library call, so the memory-order names document intent rather than a
// guaranteed lock-free path (see skl::abix::util::is_always_lock_free).

#include <stddef.h>

namespace skl::abix::util::atomic_detail {

// GCC's -Wstringop-overflow cannot bound the destination of `__atomic_store_n`
// when the pointer comes from an opaque allocation, and reports a false
// positive ("writing N bytes into a region of size 0 ... likely at address
// zero") at the inlined builtin below. The stores are well-formed by contract,
// so silence that one warning for this header only. Clang has no such warning.
#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif

template<typename T>
inline T load_acquire(const T *p) noexcept {
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

template<typename T>
inline T load_relaxed(const T *p) noexcept {
    return __atomic_load_n(p, __ATOMIC_RELAXED);
}

template<typename T, typename V>
inline void store_release(T *p, V v) noexcept {
    __atomic_store_n(p, static_cast<T>(v), __ATOMIC_RELEASE);
}

template<typename T, typename V>
inline void store_relaxed(T *p, V v) noexcept {
    __atomic_store_n(p, static_cast<T>(v), __ATOMIC_RELAXED);
}

// Return the value *after* the update (`__atomic_add_fetch`/`sub_fetch`).
template<typename T>
inline T inc_relaxed(T *p) noexcept {
    return __atomic_add_fetch(p, T{1}, __ATOMIC_RELAXED);
}

template<typename T>
inline T dec_relaxed(T *p) noexcept {
    return __atomic_sub_fetch(p, T{1}, __ATOMIC_RELAXED);
}

template<typename T>
inline T inc_acq_rel(T *p) noexcept {
    return __atomic_add_fetch(p, T{1}, __ATOMIC_ACQ_REL);
}

template<typename T, typename V>
inline T exchange_acq_rel(T *p, V v) noexcept {
    return __atomic_exchange_n(p, static_cast<T>(v), __ATOMIC_ACQ_REL);
}

// Strong compare-exchange, relaxed success/failure ordering. On failure
// `expected` is updated with the value observed in `*p` (the builtin does this
// by itself through the `expected` pointer).
template<typename T>
inline bool compare_exchange(T *p, T &expected, T desired) noexcept {
    return __atomic_compare_exchange_n(p, &expected, desired, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}

template<typename T>
inline constexpr bool is_always_lock_free = __atomic_always_lock_free(sizeof(T), nullptr);

#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic pop
#endif

}   // namespace skl::abix::util::atomic_detail

#endif   // SKL_ABIX_UTIL_ATOMIC_GCC_CLANG_H
