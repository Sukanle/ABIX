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
#ifndef SKL_ABIX_ERROR_DOMAIN_H
#define SKL_ABIX_ERROR_DOMAIN_H

#include <stdint.h>

namespace skl::abix {

// Error domains use the same 64-bit layout as EPCD, but ABIX keeps its own
// definition: the runtime must not include or link EPCD.
//
//   63..60  owner kind (1 = SKL ecosystem)
//   59..48  module id
//   47..0   local domain id
//
// A released DomainId is permanent. Allocating a new one never reuses the
// identity of a removed domain.
using DomainId = uint64_t;

inline constexpr DomainId invalid_domain = 0;

inline constexpr DomainId domain_owner_mask = 0xF000000000000000ULL;
inline constexpr DomainId domain_module_mask = 0x0FFF000000000000ULL;
inline constexpr DomainId domain_local_mask = 0x0000FFFFFFFFFFFFULL;

inline constexpr unsigned domain_owner_shift = 60;
inline constexpr unsigned domain_module_shift = 48;

inline constexpr DomainId domain_module_max = 0x0FFF;
inline constexpr DomainId owner_skl = 1;
inline constexpr DomainId skl_domain_mask = owner_skl << domain_owner_shift;

// Build a domain id inside the SKL mask. Only the mask layout is shared with
// EPCD; the module id and local id are allocated by the owning module.
[[nodiscard]] constexpr DomainId make_skl_domain(uint16_t module_id, uint64_t local_id) noexcept {
    return skl_domain_mask
         | ((static_cast<DomainId>(module_id) & domain_module_max) << domain_module_shift)
         | (local_id & domain_local_mask);
}

[[nodiscard]] constexpr bool is_valid_domain(DomainId id) noexcept { return id != invalid_domain; }

[[nodiscard]] constexpr DomainId domain_owner(DomainId id) noexcept {
    return (id & domain_owner_mask) >> domain_owner_shift;
}

[[nodiscard]] constexpr DomainId domain_module(DomainId id) noexcept {
    return (id & domain_module_mask) >> domain_module_shift;
}

[[nodiscard]] constexpr DomainId domain_local(DomainId id) noexcept { return id & domain_local_mask; }

[[nodiscard]] constexpr bool is_skl_domain(DomainId id) noexcept {
    return is_valid_domain(id) && domain_owner(id) == owner_skl;
}

// ABIX module allocation. WCAS already owns module 0x001; the SKL allocation
// table is documented in memory/error-abi.md and must not be changed once a
// value has been released.
inline constexpr uint16_t abix_module_id = 0x002;

inline constexpr DomainId call_domain = make_skl_domain(abix_module_id, 1);
inline constexpr DomainId interface_domain = make_skl_domain(abix_module_id, 2);
inline constexpr DomainId module_domain = make_skl_domain(abix_module_id, 3);
inline constexpr DomainId manifest_domain = make_skl_domain(abix_module_id, 4);
inline constexpr DomainId lifecycle_domain = make_skl_domain(abix_module_id, 5);

static_assert(call_domain == 0x1002000000000001ULL, "ABIX call domain id changed");
static_assert(interface_domain == 0x1002000000000002ULL, "ABIX interface domain id changed");
static_assert(module_domain == 0x1002000000000003ULL, "ABIX module domain id changed");
static_assert(manifest_domain == 0x1002000000000004ULL, "ABIX manifest domain id changed");
static_assert(lifecycle_domain == 0x1002000000000005ULL, "ABIX lifecycle domain id changed");
static_assert(domain_module(call_domain) == abix_module_id, "ABIX module id is encoded in every ABIX domain");
static_assert(domain_local(lifecycle_domain) == 5, "ABIX local domain ids are contiguous");
static_assert(is_skl_domain(call_domain), "ABIX domains live inside the SKL owner mask");

}   // namespace skl::abix

#endif   // SKL_ABIX_ERROR_DOMAIN_H
