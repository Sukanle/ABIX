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
#ifndef SKL_ABIX_INTERFACE_LEASE_ABI_H
#define SKL_ABIX_INTERFACE_LEASE_ABI_H

// Cross-language lease C ABI.
//
// ABIX defines the C types and the function-table contract; the Host (for
// example AMC) owns the registry and provides the implementation. There is no
// process-wide ABIX registry and no exported lease symbol: the function table
// is passed from the Host to Rust/Zig consumers.
//
// See docs/abix/lease.md for the normative contract.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct abix_lease_registry abix_lease_registry;
typedef uint64_t abix_module_instance;
typedef uint64_t abix_lease;

#define ABIX_LEASE_INVALID UINT64_C(0)
#define ABIX_LEASE_API_VERSION UINT32_C(1)

typedef enum abix_status {
    ABIX_SUCCESS = 0,
    ABIX_ERROR_INVALID = 1,
    ABIX_ERROR_NOT_FOUND = 2,
    ABIX_ERROR_STALE_HANDLE = 3,
    ABIX_ERROR_BUSY = 4,
    ABIX_ERROR_UNLOADING = 5
} abix_status;

// Host-owned function table. `registry_create` / `registry_destroy` are Host
// operations; Rust/Zig consumers only use `acquire` / `release`.
typedef struct abix_lease_api {
    uint32_t abi_version;
    uint32_t struct_size;
    void *host_context;

    abix_status (*registry_create)(void *host_context, abix_lease_registry **out_registry);
    abix_status (*registry_destroy)(void *host_context, abix_lease_registry *registry);
    abix_status (*acquire)(
        void *host_context, abix_lease_registry *registry, abix_module_instance module, abix_lease *out_lease);
    void (*release)(void *host_context, abix_lease_registry *registry, abix_lease lease);
} abix_lease_api;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#  include "SKL/ABIX/Error/ErrorCode.h"

namespace skl::abix {

// Map the C status to the stable ABIX error identity. Unknown values are
// preserved numerically as a call-domain failure; they never become success.
[[nodiscard]] constexpr ErrorCode to_error_code(abix_status status) noexcept {
    switch (status) {
        case ABIX_SUCCESS:            return success();
        case ABIX_ERROR_INVALID:      return ErrorCode{call_domain, 7, 0};     // CallError::invalid
        case ABIX_ERROR_NOT_FOUND:    return ErrorCode{module_domain, 7, 0};   // ModuleStatus::not_registered
        case ABIX_ERROR_STALE_HANDLE: return ErrorCode{call_domain, 5, 0};     // CallError::stale_handle
        case ABIX_ERROR_BUSY:         return ErrorCode{module_domain, 8, 0};   // ModuleStatus::busy
        case ABIX_ERROR_UNLOADING:    return ErrorCode{call_domain, 9, 0};     // CallError::unloading
    }
    return ErrorCode{call_domain, static_cast<int32_t>(status), 0};
}

[[nodiscard]] constexpr bool is_success(abix_status status) noexcept { return status == ABIX_SUCCESS; }

static_assert(ABIX_LEASE_API_VERSION == 1, "the initial lease API version is 1");
static_assert(ABIX_SUCCESS == 0, "abix_status values are a stable C ABI");
static_assert(ABIX_ERROR_UNLOADING == 5, "abix_status values are a stable C ABI");
static_assert(is_success(to_error_code(ABIX_SUCCESS)), "ABIX_SUCCESS maps to canonical success");
static_assert(!is_success(to_error_code(ABIX_ERROR_BUSY)), "ABIX_ERROR_BUSY maps to failure");

}   // namespace skl::abix

#endif   // __cplusplus

#endif   // SKL_ABIX_INTERFACE_LEASE_ABI_H
