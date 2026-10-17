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
#ifndef SKL_ABIX_UTIL_ATOMIC_H
#define SKL_ABIX_UTIL_ATOMIC_H

// ABIX's atomic primitives: stateless free functions over caller-owned storage,
// so ABIX keeps a plain integer/pointer layout across DLL and CRT boundaries
// instead of sharing a `std::atomic<T>` object whose layout is compiler-defined.
//
// This header defines the public contract (type constraints, names, memory
// order) and delegates the implementation to a per-compiler backend:
//
//   * AtomicGCCClang.h - the `__atomic` builtins (GCC, Clang, clang-cl)
//   * AtomicMSVC.h     - the `_Interlocked` intrinsics (native MSVC)
//
// Backend selection is by compiler capability, not by the target OS: clang-cl
// defines both `__clang__` and `_MSC_VER` and uses the `__atomic` backend.

#include <cstddef>
#include <cstdint>
#include <type_traits>

#if defined(__clang__) || defined(__GNUC__)
#  include "ABIX/Util/AtomicGCCClang.h"
#elif defined(_MSC_VER)
#  include "ABIX/Util/AtomicMSVC.h"
#else
#  error "ABIX has no atomic backend for this compiler (expected GCC/Clang __atomic or MSVC intrinsics)"
#endif

namespace skl::abix::util {

// ---------------------------------------------------------------------------
// Type constraints
//
// These are deliberately narrower than "any arithmetic type". The set of types
// that can be loaded/stored/exchanged atomically, that support atomic
// read-modify-write, and that have a well-defined atomic compare-exchange are
// not the same, so they are modelled separately. Floating point is not part of
// any of them: its atomic support (and, for compare-exchange, its equality
// semantics) varies by compiler and target, and these primitives are
// integer/pointer oriented.
// ---------------------------------------------------------------------------

// Integral (excluding bool) or object pointer: accepted by load/store/exchange.
template<typename T>
struct is_atomic_scalar
    : std::integral_constant<bool, std::is_pointer<T>::value
                                       || (std::is_integral<T>::value
                                           && !std::is_same<std::remove_cv_t<T>, bool>::value)> {};

// Integral (excluding bool): accepted by atomic increment/decrement. Pointers
// are intentionally excluded - their step semantics differ across targets.
template<typename T>
struct is_atomic_integer
    : std::integral_constant<bool, std::is_integral<T>::value
                                       && !std::is_same<std::remove_cv_t<T>, bool>::value> {};

// Accepted by atomic compare-exchange (the scalar set).
template<typename T>
struct is_atomic_cas : is_atomic_scalar<T> {};

template<typename T>
inline constexpr bool is_atomic_scalar_v = is_atomic_scalar<T>::value;
template<typename T>
inline constexpr bool is_atomic_integer_v = is_atomic_integer<T>::value;
template<typename T>
inline constexpr bool is_atomic_cas_v = is_atomic_cas<T>::value;

// Compile-time lock-free capability reported by the active backend. `false`
// does not mean the operation is unavailable - only that the backend may lower
// it to a runtime-library call on this target.
template<typename T>
inline constexpr bool is_always_lock_free = atomic_detail::is_always_lock_free<T>;

// ---------------------------------------------------------------------------
// Load / store
// ---------------------------------------------------------------------------

template<typename T, std::enable_if_t<is_atomic_scalar_v<T>, int> = 0>
inline T load_acquire(const T *p) noexcept {
    return atomic_detail::load_acquire(p);
}

template<typename T, std::enable_if_t<is_atomic_scalar_v<T>, int> = 0>
inline T load_relaxed(const T *p) noexcept {
    return atomic_detail::load_relaxed(p);
}

template<typename T, typename V, std::enable_if_t<is_atomic_scalar_v<T>, int> = 0,
    std::enable_if_t<std::is_convertible<V, T>::value, int> = 0>
inline void store_release(T *p, V v) noexcept {
    atomic_detail::store_release(p, v);
}

template<typename T, typename V, std::enable_if_t<is_atomic_scalar_v<T>, int> = 0,
    std::enable_if_t<std::is_convertible<V, T>::value, int> = 0>
inline void store_relaxed(T *p, V v) noexcept {
    atomic_detail::store_relaxed(p, v);
}

// ---------------------------------------------------------------------------
// Read-modify-write
//
// `inc_*` and `dec_*` return the value *after* the update (they lower to
// `__atomic_add_fetch` / `sub_fetch`), not the previous value.
// ---------------------------------------------------------------------------

template<typename T, std::enable_if_t<is_atomic_integer_v<T>, int> = 0>
inline T inc_relaxed(T *p) noexcept {
    return atomic_detail::inc_relaxed(p);
}

template<typename T, std::enable_if_t<is_atomic_integer_v<T>, int> = 0>
inline T dec_relaxed(T *p) noexcept {
    return atomic_detail::dec_relaxed(p);
}

template<typename T, std::enable_if_t<is_atomic_integer_v<T>, int> = 0>
inline T inc_acq_rel(T *p) noexcept {
    return atomic_detail::inc_acq_rel(p);
}

template<typename T, typename V, std::enable_if_t<is_atomic_scalar_v<T>, int> = 0,
    std::enable_if_t<std::is_convertible<V, T>::value, int> = 0>
inline T exchange_acq_rel(T *p, V v) noexcept {
    return atomic_detail::exchange_acq_rel(p, v);
}

// ---------------------------------------------------------------------------
// Compare-exchange
// ---------------------------------------------------------------------------

// Strong compare-exchange (never spuriously fails) with relaxed success/failure
// ordering. On failure `expected` is updated with the value observed in `*p`,
// so a retry loop can reuse it without a second load.
template<typename T, std::enable_if_t<is_atomic_cas_v<T>, int> = 0>
inline bool compare_exchange(T *p, T &expected, T desired) noexcept {
    return atomic_detail::compare_exchange(p, expected, desired);
}

// Convenience form: reports only whether the exchange succeeded. The value
// observed on failure is discarded - use compare_exchange() when a retry needs
// it.
template<typename T, std::enable_if_t<is_atomic_cas_v<T>, int> = 0>
inline bool cas_relaxed(T *p, T expected, T desired) noexcept {
    return atomic_detail::compare_exchange(p, expected, desired);
}

}   // namespace skl::abix::util

#endif   // SKL_ABIX_UTIL_ATOMIC_H
