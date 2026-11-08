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
#ifndef SKL_ABIX_STD_OPTIONAL_H
#define SKL_ABIX_STD_OPTIONAL_H

#include <stddef.h>
#include <stdint.h>

#include "SKL/ABIX/STD/Layout.h"

// ABIX-STD optional: a value that may be absent. v0.4.0 makes the layout
// explicit: a 32-bit discriminant, a required-zero reserved word and a payload
// aligned for T. Only 0 (None) and 1 (Some) are valid; a consumer must reject
// any other discriminant or a non-zero reserved word and must not read `value`
// when the discriminant is None.
#ifdef __cplusplus
namespace abix_std {

template<class T>
struct abix_optional {
    uint32_t discriminant;   // 0 = None, 1 = Some
    uint32_t reserved;       // must be 0
    alignas(T) T value;      // valid only when discriminant == 1
};

template<class T>
constexpr std::size_t abix_optional_value_offset() noexcept {
    return abix_std::detail::align_up(8, alignof(T));
}

template<class T>
constexpr std::size_t abix_optional_alignment() noexcept {
    return abix_std::detail::max_align(4, alignof(T));
}

template<class T>
constexpr std::size_t abix_optional_size() noexcept {
    return abix_std::detail::align_up(abix_optional_value_offset<T>() + sizeof(T), abix_optional_alignment<T>());
}

}   // namespace abix_std

extern "C" {
#endif

typedef abix_std::abix_optional<int32_t> abix_optional_i32;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
static_assert(offsetof(abix_std::abix_optional<int32_t>, discriminant) == 0, "abix_optional layout changed");
static_assert(offsetof(abix_std::abix_optional<int32_t>, reserved) == 4, "abix_optional layout changed");
static_assert(offsetof(abix_std::abix_optional<int32_t>, value) == 8, "abix_optional layout changed");
static_assert(sizeof(abix_std::abix_optional<int32_t>) == 12, "abix_optional layout changed");
static_assert(alignof(abix_std::abix_optional<int32_t>) == 4, "abix_optional layout changed");

static_assert(offsetof(abix_std::abix_optional<int64_t>, value) == abix_std::abix_optional_value_offset<int64_t>(),
    "abix_optional<int64_t> layout changed");
static_assert(sizeof(abix_std::abix_optional<int64_t>) == abix_std::abix_optional_size<int64_t>(),
    "abix_optional<int64_t> layout changed");
static_assert(offsetof(abix_std::abix_optional<void *>, value) == abix_std::abix_optional_value_offset<void *>(),
    "abix_optional<T*> layout changed");
static_assert(sizeof(abix_std::abix_optional<void *>) == abix_std::abix_optional_size<void *>(),
    "abix_optional<T*> layout changed");
#endif

#endif
