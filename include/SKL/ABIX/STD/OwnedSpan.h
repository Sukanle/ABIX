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
#ifndef SKL_ABIX_STD_OWNED_SPAN_H
#define SKL_ABIX_STD_OWNED_SPAN_H

#include "SKL/ABIX/Interface/OwnerABI.h"
#include "SKL/ABIX/STD/Span.h"

// An owned span is a borrowed view plus the owner that keeps its backing
// allocation alive. `view` can be passed to any `abix_span<T>` parameter without
// a copy; release the allocation exactly once through
// `owner.destroy(owner.object)`.
//
// The concrete instance, not the template, is the ABI entity:
// `abix_owned_span_i32` is the exported name for a span of int32_t.

#ifdef __cplusplus
namespace abix_std {

template<class T>
struct abix_owned_span {
    abix_span<T> view;
    abix_owner_handle owner;
};

}   // namespace abix_std

extern "C" {
#endif

typedef abix_std::abix_owned_span<int32_t> abix_owned_span_i32;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
static_assert(offsetof(abix_std::abix_owned_span<int32_t>, view) == 0, "abix_owned_span layout changed");
static_assert(
    offsetof(abix_std::abix_owned_span<int32_t>, owner) == sizeof(abix_span_i32), "abix_owned_span layout changed");
static_assert(sizeof(abix_std::abix_owned_span<int32_t>) == sizeof(abix_span_i32) + sizeof(abix_owner_handle),
    "abix_owned_span layout changed");
static_assert(
    alignof(abix_std::abix_owned_span<int32_t>) >= alignof(abix_owner_handle), "abix_owned_span alignment changed");
#endif

#endif   // SKL_ABIX_STD_OWNED_SPAN_H
