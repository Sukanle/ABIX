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
#ifndef SKL_ABIX_UTIL_ATOMIC_MSVC_H
#define SKL_ABIX_UTIL_ATOMIC_MSVC_H

// MSVC backend for ABIX/Util/Atomic.h, built on the `_Interlocked*` compiler
// intrinsics (native MSVC; clang-cl takes the GCC/Clang backend instead).
//
// `_Interlocked*` operations are full memory barriers (roughly sequentially
// consistent). Later mapping the weaker acquire/release/relaxed operations onto
// them is correct - a stronger order satisfies a weaker one - but it is not a
// cheaper code path; the names below express intent, not a performance claim.
// The interlocked operations also require the object to be naturally aligned.

#include <cstddef>
#include <intrin.h>
#include <type_traits>

namespace skl::abix::util::atomic_detail {

namespace detail {

template<typename T>
inline T compare_exchange_value(T *p, T exchange, T comparand) noexcept {
    if constexpr (std::is_pointer<T>::value) {
        void *previous = _InterlockedCompareExchangePointer(reinterpret_cast<void *volatile *>(p),
            reinterpret_cast<void *>(exchange), reinterpret_cast<void *>(comparand));
        return reinterpret_cast<T>(previous);
    } else if constexpr (sizeof(T) == 1) {
        return static_cast<T>(_InterlockedCompareExchange8(
            reinterpret_cast<char volatile *>(p), static_cast<char>(exchange), static_cast<char>(comparand)));
    } else if constexpr (sizeof(T) == 2) {
        return static_cast<T>(_InterlockedCompareExchange16(
            reinterpret_cast<short volatile *>(p), static_cast<short>(exchange), static_cast<short>(comparand)));
    } else if constexpr (sizeof(T) == 4) {
        return static_cast<T>(_InterlockedCompareExchange(
            reinterpret_cast<long volatile *>(p), static_cast<long>(exchange), static_cast<long>(comparand)));
    } else if constexpr (sizeof(T) == 8) {
        return static_cast<T>(_InterlockedCompareExchange64(reinterpret_cast<long long volatile *>(p),
            static_cast<long long>(exchange), static_cast<long long>(comparand)));
    } else {
        static_assert(sizeof(T) <= 8, "ABIX: atomic type wider than 8 bytes is not supported");
    }
}

template<typename T>
inline T exchange_value(T *p, T value) noexcept {
    if constexpr (std::is_pointer<T>::value) {
        return reinterpret_cast<T>(
            _InterlockedExchangePointer(reinterpret_cast<void *volatile *>(p), reinterpret_cast<void *>(value)));
    } else if constexpr (sizeof(T) == 1) {
        return static_cast<T>(_InterlockedExchange8(reinterpret_cast<char volatile *>(p), static_cast<char>(value)));
    } else if constexpr (sizeof(T) == 2) {
        return static_cast<T>(_InterlockedExchange16(reinterpret_cast<short volatile *>(p), static_cast<short>(value)));
    } else if constexpr (sizeof(T) == 4) {
        return static_cast<T>(_InterlockedExchange(reinterpret_cast<long volatile *>(p), static_cast<long>(value)));
    } else if constexpr (sizeof(T) == 8) {
        return static_cast<T>(
            _InterlockedExchange64(reinterpret_cast<long long volatile *>(p), static_cast<long long>(value)));
    } else {
        static_assert(sizeof(T) <= 8, "ABIX: atomic type wider than 8 bytes is not supported");
    }
}

// Return the value *after* the addition (matching `__atomic_add_fetch`).
template<typename T>
inline T add_fetch(T *p, T delta) noexcept {
    if constexpr (sizeof(T) == 1) {
        return static_cast<T>(
            _InterlockedExchangeAdd8(reinterpret_cast<char volatile *>(p), static_cast<char>(delta)) + delta);
    } else if constexpr (sizeof(T) == 2) {
        return static_cast<T>(
            _InterlockedExchangeAdd16(reinterpret_cast<short volatile *>(p), static_cast<short>(delta)) + delta);
    } else if constexpr (sizeof(T) == 4) {
        return static_cast<T>(
            _InterlockedExchangeAdd(reinterpret_cast<long volatile *>(p), static_cast<long>(delta)) + delta);
    } else if constexpr (sizeof(T) == 8) {
        return static_cast<T>(
            _InterlockedExchangeAdd64(reinterpret_cast<long long volatile *>(p), static_cast<long long>(delta))
            + delta);
    } else {
        static_assert(sizeof(T) <= 8, "ABIX: atomic integer wider than 8 bytes is not supported");
    }
}

}   // namespace detail

template<typename T>
inline T load_acquire(const T *p) noexcept {
    // A compare-exchange whose exchange equals its comparand is an atomic load.
    return detail::compare_exchange_value(const_cast<T *>(p), T{}, T{});
}

template<typename T>
inline T load_relaxed(const T *p) noexcept {
    return load_acquire(p);
}

template<typename T, typename V>
inline void store_release(T *p, V v) noexcept {
    (void)detail::exchange_value(p, static_cast<T>(v));
}

template<typename T, typename V>
inline void store_relaxed(T *p, V v) noexcept {
    store_release(p, v);
}

template<typename T>
inline T inc_relaxed(T *p) noexcept {
    return detail::add_fetch(p, T{1});
}

template<typename T>
inline T dec_relaxed(T *p) noexcept {
    return detail::add_fetch(p, static_cast<T>(-1));
}

template<typename T>
inline T inc_acq_rel(T *p) noexcept {
    return detail::add_fetch(p, T{1});
}

template<typename T, typename V>
inline T exchange_acq_rel(T *p, V v) noexcept {
    return detail::exchange_value(p, static_cast<T>(v));
}

template<typename T>
inline bool compare_exchange(T *p, T &expected, T desired) noexcept {
    const T previous = detail::compare_exchange_value(p, desired, expected);
    if (previous == expected) return true;
    expected = previous;
    return false;
}

// On the MSVC targets the `_Interlocked*` widths map to native instructions, so
// integer (<= 8 bytes) and pointer operations are always lock-free.
template<typename T>
inline constexpr bool is_always_lock_free =
    std::is_pointer<T>::value
    || (std::is_integral<T>::value && (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8));

}   // namespace skl::abix::util::atomic_detail

#endif   // SKL_ABIX_UTIL_ATOMIC_MSVC_H
