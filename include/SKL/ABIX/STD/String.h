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
#ifndef SKL_ABIX_STD_STRING_H
#define SKL_ABIX_STD_STRING_H

#include <stddef.h>
#include <stdint.h>

#include "SKL/ABIX/STD/Layout.h"

// ABIX-STD string view: a borrowed, read-only view of encoded text.
//
// The contract is `string-view` (see ABI-SPEC.md §18). It is not a wrapper for
// any language's string type: the same contract is satisfied by a C++ view, a
// Rust `&str` or a Zig slice once encoding, length unit, layout and borrowing
// are proven. The `encoding` field is a runtime value; the build stage checks
// its presence and type, and runtime verification can check its value.
//
// v0.4.0 makes the trailing padding explicit as `reserved`. It must be zero on
// write and is rejected when non-zero on read.

#ifdef __cplusplus
namespace abix_std {

// Encoding codes stored in `abix_str::encoding`.
enum class Encoding : uint32_t {
    utf8 = 0,
    utf16 = 1,
    utf32 = 2,
};

template<class CodeUnit>
struct abix_str {
    const CodeUnit *data;
    uint64_t len;
    uint32_t encoding;
    uint32_t reserved;   // must be 0
};

template<class CodeUnit>
constexpr std::size_t abix_str_len_offset() noexcept {
    return abix_std::detail::align_up(sizeof(CodeUnit *), alignof(uint64_t));
}

template<class CodeUnit>
constexpr std::size_t abix_str_encoding_offset() noexcept {
    return abix_std::detail::align_up(abix_str_len_offset<CodeUnit>() + sizeof(uint64_t), alignof(uint32_t));
}

template<class CodeUnit>
constexpr std::size_t abix_str_reserved_offset() noexcept {
    return abix_str_encoding_offset<CodeUnit>() + sizeof(uint32_t);
}

template<class CodeUnit>
constexpr std::size_t abix_str_alignment() noexcept {
    return abix_std::detail::max_align(
        abix_std::detail::max_align(alignof(CodeUnit *), alignof(uint64_t)), alignof(uint32_t));
}

template<class CodeUnit>
constexpr std::size_t abix_str_size() noexcept {
    return abix_std::detail::align_up(
        abix_str_reserved_offset<CodeUnit>() + sizeof(uint32_t), abix_str_alignment<CodeUnit>());
}

}   // namespace abix_std

extern "C" {
#endif

// Concrete ABIX-STD instantiations. UTF-8 code units are C `char`.
typedef abix_std::abix_str<char> abix_str_utf8;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
static_assert(offsetof(abix_std::abix_str<char>, data) == 0, "abix_str layout changed");
static_assert(
    offsetof(abix_std::abix_str<char>, len) == abix_std::abix_str_len_offset<char>(), "abix_str layout changed");
static_assert(offsetof(abix_std::abix_str<char>, encoding) == abix_std::abix_str_encoding_offset<char>(),
    "abix_str layout changed");
static_assert(offsetof(abix_std::abix_str<char>, reserved) == abix_std::abix_str_reserved_offset<char>(),
    "abix_str layout changed");
static_assert(sizeof(abix_std::abix_str<char>) == abix_std::abix_str_size<char>(), "abix_str layout changed");
static_assert(alignof(abix_std::abix_str<char>) == abix_std::abix_str_alignment<char>(), "abix_str layout changed");
#endif

#endif
