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
#ifndef SKL_ABIX_STD_LAYOUT_H
#define SKL_ABIX_STD_LAYOUT_H

#include <stddef.h>

#ifdef __cplusplus
#  include <cstddef>

namespace abix_std {
namespace detail {

// Layout arithmetic is shared by the explicit ABIX-STD layouts so the header
// assertions and the conformance tests use the same formula.
constexpr std::size_t align_up(std::size_t value, std::size_t align) noexcept {
    return align == 0 ? value : (value + align - 1) / align * align;
}

constexpr std::size_t max_align(std::size_t lhs, std::size_t rhs) noexcept { return lhs > rhs ? lhs : rhs; }

}   // namespace detail
}   // namespace abix_std
#endif

#endif
