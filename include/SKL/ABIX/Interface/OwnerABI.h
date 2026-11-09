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
#ifndef SKL_ABIX_INTERFACE_OWNER_ABI_H
#define SKL_ABIX_INTERFACE_OWNER_ABI_H

// Canonical owner handle C ABI.
//
// The owner handle is the boundary type shared by ABIX, AMC and every language
// adapter. It is deliberately a plain C struct: the creator owns the object and
// the destroy entry, the consumer never infers a deallocator, and
// `module_instance` is the hook for the ABIX lease C ABI.
//
// Modes: borrowed = 0 (the borrower must not release), owned = 1 (exclusive
// ownership, released through `destroy`), copied = 2 (independent copy, released
// through its own `destroy`). A shared/weak owner is out of scope.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ABIX_OWNER_ABI_VERSION UINT32_C(1)

typedef enum abix_owner_mode {
    ABIX_OWNER_BORROWED = 0,
    ABIX_OWNER_OWNED = 1,
    ABIX_OWNER_COPIED = 2
} abix_owner_mode;

typedef void (*abix_owner_destroy_fn)(void *object);

typedef struct abix_owner_handle {
    void *object;                    // opaque object identity, 0 when invalid
    uint32_t mode;                   // abix_owner_mode
    uint32_t flags;                  // must be 0 in v1
    abix_owner_destroy_fn destroy;   // creator's release entry, required for owned/copied
    uint64_t module_instance;        // creator module instance; 0 = unavailable
} abix_owner_handle;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
static_assert(offsetof(abix_owner_handle, object) == 0, "abix_owner_handle layout changed");
static_assert(offsetof(abix_owner_handle, mode) == sizeof(void *), "abix_owner_handle layout changed");
static_assert(offsetof(abix_owner_handle, flags) == sizeof(void *) + 4, "abix_owner_handle layout changed");
static_assert(alignof(abix_owner_handle) >= alignof(void *), "abix_owner_handle alignment changed");
static_assert(sizeof(abix_owner_handle) >= sizeof(void *) + 8 + sizeof(void *), "abix_owner_handle size changed");

#  if UINTPTR_MAX == UINT64_MAX
static_assert(offsetof(abix_owner_handle, destroy) == 16, "abix_owner_handle layout changed");
static_assert(offsetof(abix_owner_handle, module_instance) == 24, "abix_owner_handle layout changed");
static_assert(sizeof(abix_owner_handle) == 32, "abix_owner_handle layout changed");
static_assert(alignof(abix_owner_handle) == 8, "abix_owner_handle layout changed");
#  endif
#else
_Static_assert(offsetof(abix_owner_handle, object) == 0, "abix_owner_handle layout changed");
_Static_assert(offsetof(abix_owner_handle, mode) == sizeof(void *), "abix_owner_handle layout changed");
_Static_assert(offsetof(abix_owner_handle, flags) == sizeof(void *) + 4, "abix_owner_handle layout changed");
#endif

#endif   // SKL_ABIX_INTERFACE_OWNER_ABI_H
