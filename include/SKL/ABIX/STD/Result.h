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
#ifndef SKL_ABIX_STD_RESULT_H
#define SKL_ABIX_STD_RESULT_H

#include <stddef.h>
#include <stdint.h>

#include "SKL/ABIX/STD/Layout.h"

// ABIX-STD result: a transport status plus a value. `status` is not the
// 16-byte ErrorCode; the [[error]] contract selects the success value and the
// error identity mapping. The reserved word must be zero on write and is
// rejected when non-zero on read.
#ifdef __cplusplus
namespace abix_std {

template<class T>
struct abix_result {
    int32_t status;       // transport status
    uint32_t reserved;    // must be 0
    alignas(T) T value;   // valid when status is the contract's success value
};

template<class T>
constexpr std::size_t abix_result_value_offset() noexcept {
    return abix_std::detail::align_up(8, alignof(T));
}

template<class T>
constexpr std::size_t abix_result_alignment() noexcept {
    return abix_std::detail::max_align(4, alignof(T));
}

template<class T>
constexpr std::size_t abix_result_size() noexcept {
    return abix_std::detail::align_up(abix_result_value_offset<T>() + sizeof(T), abix_result_alignment<T>());
}

}   // namespace abix_std

extern "C" {
#endif

typedef abix_std::abix_result<int32_t> abix_result_i32;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
static_assert(offsetof(abix_std::abix_result<int32_t>, status) == 0, "abix_result layout changed");
static_assert(offsetof(abix_std::abix_result<int32_t>, reserved) == 4, "abix_result layout changed");
static_assert(offsetof(abix_std::abix_result<int32_t>, value) == 8, "abix_result layout changed");
static_assert(sizeof(abix_std::abix_result<int32_t>) == 12, "abix_result layout changed");
static_assert(alignof(abix_std::abix_result<int32_t>) == 4, "abix_result layout changed");

static_assert(offsetof(abix_std::abix_result<int64_t>, value) == abix_std::abix_result_value_offset<int64_t>(),
    "abix_result<int64_t> layout changed");
static_assert(sizeof(abix_std::abix_result<int64_t>) == abix_std::abix_result_size<int64_t>(),
    "abix_result<int64_t> layout changed");
static_assert(offsetof(abix_std::abix_result<void *>, value) == abix_std::abix_result_value_offset<void *>(),
    "abix_result<T*> layout changed");
static_assert(
    sizeof(abix_std::abix_result<void *>) == abix_std::abix_result_size<void *>(), "abix_result<T*> layout changed");
#endif

#endif
