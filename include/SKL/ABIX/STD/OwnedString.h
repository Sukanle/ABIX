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
#ifndef SKL_ABIX_STD_OWNED_STRING_H
#define SKL_ABIX_STD_OWNED_STRING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#  include "SKL/ABIX/STD/String.h"
#else
// The string-view template is a C++ source construct. The C ABI only needs the
// concrete UTF-8 view, so an owned-string consumer can include this header from C.
typedef struct abix_str_utf8 {
    const char *data;
    uint64_t len;
    uint32_t encoding;
    uint32_t reserved;
} abix_str_utf8;
#endif

#include "SKL/ABIX/Interface/OwnerABI.h"

// An owned string is a borrowed view plus the owner that keeps its backing
// allocation alive. The consumer can pass `view` to any `abix_str` parameter
// without a copy, and releases the allocation exactly once through
// `owner.destroy(owner.object)`.
#ifdef __cplusplus
extern "C" {
#endif

typedef struct abix_owned_str {
    abix_str_utf8 view;
    abix_owner_handle owner;
} abix_owned_str;

typedef abix_owned_str abix_owned_str_utf8;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
static_assert(offsetof(abix_owned_str, view) == 0, "abix_owned_str layout changed");
static_assert(offsetof(abix_owned_str, owner) == sizeof(abix_str_utf8), "abix_owned_str layout changed");
static_assert(
    sizeof(abix_owned_str) == sizeof(abix_str_utf8) + sizeof(abix_owner_handle), "abix_owned_str layout changed");
static_assert(alignof(abix_owned_str) >= alignof(abix_owner_handle), "abix_owned_str alignment changed");
#else
_Static_assert(offsetof(abix_owned_str, view) == 0, "abix_owned_str layout changed");
_Static_assert(offsetof(abix_owned_str, owner) == sizeof(abix_str_utf8), "abix_owned_str layout changed");
#endif

#endif   // SKL_ABIX_STD_OWNED_STRING_H
