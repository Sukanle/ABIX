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
#ifndef SKL_ABIX_STD_CALLBACK_H
#define SKL_ABIX_STD_CALLBACK_H

#include <stddef.h>
#include <stdint.h>

// ABIX-STD callback (P1): a function entry point plus an opaque context. The
// lifetime, unregister and thread constraints are part of the caller's
// contract; ABIX only describes the layout.
#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t (*abix_callback_fn)(void *context);

typedef struct abix_callback {
    abix_callback_fn invoke;
    void *context;
} abix_callback;

// Owned callback: the producer owns `context` and releases it exactly once
// after the last invocation. `release` is required for an owned callback;
// `flags` and `reserved` are zero in v1. The release entry must not unwind.
typedef void (*abix_callback_release_fn)(void *context);

typedef struct abix_callback_owned {
    abix_callback_fn invoke;
    void *context;
    abix_callback_release_fn release;
    uint32_t flags;
    uint32_t reserved;
} abix_callback_owned;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
static_assert(offsetof(abix_callback, invoke) == 0, "abix_callback layout changed");
static_assert(offsetof(abix_callback, context) == sizeof(void *), "abix_callback layout changed");
static_assert(offsetof(abix_callback_owned, invoke) == 0, "abix_callback_owned layout changed");
static_assert(offsetof(abix_callback_owned, context) == sizeof(void *), "abix_callback_owned layout changed");
static_assert(offsetof(abix_callback_owned, release) == 2 * sizeof(void *), "abix_callback_owned layout changed");
static_assert(offsetof(abix_callback_owned, flags) == 3 * sizeof(void *), "abix_callback_owned layout changed");
static_assert(offsetof(abix_callback_owned, reserved) == 3 * sizeof(void *) + 4, "abix_callback_owned layout changed");
static_assert(sizeof(abix_callback_owned) == 3 * sizeof(void *) + 8, "abix_callback_owned layout changed");
#endif

#endif
