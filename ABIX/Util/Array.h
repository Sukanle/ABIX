/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */
#ifndef SKL_ABIX_UTIL_ARRAY_H
#define SKL_ABIX_UTIL_ARRAY_H

#include <stddef.h>

namespace skl::abix::util {

// A fixed-size, zero-allocation array usable in constant expressions.
//
// The deduction guide lets a braced list name its own type, so a caller can
// pass one inline where a pointer + count pair would otherwise force a named
// array:
//
//   StaticArray{type_a, type_b}   // StaticArray<model::TypeId, 2>
//
// This is deliberately a plain aggregate over a C array: it has no allocator,
// no bounds-checked growth and no dependency on the standard library.
template<typename T, size_t Count>
struct StaticArray {
    T values[Count == 0 ? 1 : Count];

    constexpr const T *data() const noexcept { return values; }
    constexpr T *data() noexcept { return values; }
    static constexpr size_t size() noexcept { return Count; }
    constexpr const T &operator[](size_t index) const noexcept { return values[index]; }
    constexpr T &operator[](size_t index) noexcept { return values[index]; }
    constexpr const T *begin() const noexcept { return values; }
    constexpr const T *end() const noexcept { return values + Count; }
};

template<typename T, typename... Rest>
StaticArray(T, Rest...) -> StaticArray<T, 1 + sizeof...(Rest)>;

}   // namespace skl::abix::util

#endif   // SKL_ABIX_UTIL_ARRAY_H
