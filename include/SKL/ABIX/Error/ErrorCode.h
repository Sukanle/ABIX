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
#ifndef SKL_ABIX_ERROR_ERROR_CODE_H
#define SKL_ABIX_ERROR_ERROR_CODE_H

#include "SKL/ABIX/Error/Domain.h"
#include <cstddef>
#include <stdint.h>
#include <type_traits>

namespace skl::abix {

// A stable, fixed-layout error identity for boundaries that need to store,
// compare or transport a failure across a DSO or language boundary.
//
// It is deliberately not an exception object and owns no text, pointer,
// allocator or module handle, so a saved value stays valid after the producing
// module unloads. It describes identity only: the `[[error]]` contract decides
// where the status sits and where a result payload travels.
struct ErrorCode final {
    uint64_t domain;
    int32_t value;
    uint32_t reserved;   // v1: must be 0; not part of error identity
};

// Canonical success: no domain and no value.
[[nodiscard]] constexpr ErrorCode success() noexcept { return ErrorCode{invalid_domain, 0, 0}; }

// Success is defined by the value alone. A zero value with a non-zero domain is
// still a success, but it is not canonical and should be normalized before it
// crosses a serialization or ABI boundary.
[[nodiscard]] constexpr bool is_success(ErrorCode code) noexcept { return code.value == 0; }

// A canonical failure names a real domain and carries a zero reserved field.
// A canonical success is all-zero. Anything else is comparable and storable but
// must be normalized before it crosses a boundary.
[[nodiscard]] constexpr bool is_canonical(ErrorCode code) noexcept {
    if (code.reserved != 0) return false;
    return code.value == 0 ? code.domain == invalid_domain : code.domain != invalid_domain;
}

static_assert(sizeof(ErrorCode) == 16, "ErrorCode is a 16-byte fixed-layout record");
static_assert(alignof(ErrorCode) == 8, "ErrorCode is 8-byte aligned");
static_assert(offsetof(ErrorCode, domain) == 0, "ErrorCode domain offset changed");
static_assert(offsetof(ErrorCode, value) == 8, "ErrorCode value offset changed");
static_assert(offsetof(ErrorCode, reserved) == 12, "ErrorCode reserved offset changed");
static_assert(std::is_standard_layout<ErrorCode>::value, "ErrorCode must stay standard-layout");
static_assert(std::is_trivial<ErrorCode>::value, "ErrorCode must stay trivial");
static_assert(std::is_trivially_copyable<ErrorCode>::value, "ErrorCode must stay trivially copyable");
static_assert(std::is_aggregate<ErrorCode>::value, "ErrorCode must stay an aggregate");

static_assert(is_success(success()), "canonical success must be a success");
static_assert(is_canonical(success()), "canonical success must be canonical");
static_assert(!is_canonical(ErrorCode{call_domain, 0, 0}), "a success with a domain is not canonical");
static_assert(is_canonical(ErrorCode{call_domain, 1, 0}), "a named failure is canonical");
static_assert(!is_canonical(ErrorCode{invalid_domain, 1, 0}), "a failure without a domain is not canonical");
static_assert(!is_canonical(ErrorCode{call_domain, 1, 1}), "a non-zero reserved field is not canonical");

}   // namespace skl::abix

#endif   // SKL_ABIX_ERROR_ERROR_CODE_H
