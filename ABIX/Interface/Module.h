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
#ifndef SKL_ABIX_INTERFACE_MODULE_H
#define SKL_ABIX_INTERFACE_MODULE_H

#include <stddef.h>
#include <stdint.h>

#include "ABIX/Interface/Contract.h"
#include "ABIX/Util/Config.h"

// ABIX Interface layer — dynamic discovery.
//
// A provider (typically a shared library) exposes one fixed entry point that
// returns a stable, self-describing manifest of the interfaces it implements.
// The platform's export table is only used to find that entry; it is never
// parsed for interface meaning, and no C++ mangled name is ever decoded into
// an interface. Discovery, validation and registration then happen against the
// manifest below.
//
// The manifest is an ABI contract: its layout is fixed and versioned. A
// consumer must treat any pointer inside it as guest-owned and validate
// versions, counts and ids before use; it must never dereference an arbitrary
// pointer just because the manifest claims one.

namespace skl::abix::poly {

// Module instance identity. Unique per load: the same file loaded twice, or
// unloaded and reloaded, gets a new id, so a stale binding can never be
// mistaken for a live one. 0 is invalid.
using ModuleInstanceId = uint64_t;
constexpr ModuleInstanceId invalid_module_instance = 0;

// Version of the manifest ABI itself. A consumer that does not know a
// manifest version must refuse it rather than guess.
constexpr uint32_t abix_manifest_version = 1;

// The fixed export a provider exposes. The platform export table is only
// consulted for this symbol.
constexpr const char *abix_module_query_symbol = "abix_module_query_v1";

// Sanity bounds so a malformed manifest cannot drive an unbounded loop. They
// are deliberately generous; the real limit is the provider's design.
constexpr uint32_t abix_max_manifest_interfaces = 4'096;
constexpr uint32_t abix_max_manifest_methods = 65'536;

// One interface implementation advertised by a module. `methods` points at a
// provider-owned adapter table; `entry` values in it must be non-null for
// bound slots. `interface_version` distinguishes compatible contract revisions.
struct InterfaceManifestEntry {
    InterfaceId interface_id;         //  16
    uint32_t interface_version;       //   4
    uint32_t flags;                   //   4
    const InterfaceMethod *methods;   //  8
    uint32_t method_count;            //   4
    uint32_t reserved;                //   4
};
static_assert(sizeof(InterfaceManifestEntry) == 40, "InterfaceManifestEntry wire layout changed");

// The module manifest returned by `abix_module_query_v1`. `extensions` is an
// opaque, optional, forward-compatible region: a consumer that does not know
// it skips it, but must still validate its declared boundary.
struct ModuleManifest {
    uint32_t manifest_version;     // must be abix_manifest_version
    uint32_t module_abi_version;   // provider's own ABI version
    uint32_t flags;
    uint32_t interface_count;
    const InterfaceManifestEntry *interfaces;
    uint32_t extension_count;
    uint32_t reserved;
    const void *extensions;
};
static_assert(sizeof(ModuleManifest) == 40, "ModuleManifest wire layout changed");

// The signature of the fixed query entry. A provider defines it with
// SKL_ABIX_DEFINE_MODULE_QUERY.
using ModuleQueryFn = const ModuleManifest *(*)(void);

#define SKL_ABIX_DEFINE_MODULE_QUERY(manifest_expression)                                                \
    extern "C" SKL_ABIX_DLL_EXPORT const ::skl::abix::poly::ModuleManifest *abix_module_query_v1(void) { \
        return (manifest_expression);                                                                    \
    }

enum class ManifestStatus : uint32_t {
    ok = 0,
    null_manifest,
    unsupported_version,   // manifest_version is not known
    invalid_interface,     // an entry has no id, or a bad method table
    invalid_binding,       // a bound method has no adapter entry, or duplicate slot
    too_many_interfaces,
    too_many_methods,
};

constexpr const char *to_string(ManifestStatus status) noexcept {
    switch (status) {
        case ManifestStatus::ok:                  return "ok";
        case ManifestStatus::null_manifest:       return "null_manifest";
        case ManifestStatus::unsupported_version: return "unsupported_version";
        case ManifestStatus::invalid_interface:   return "invalid_interface";
        case ManifestStatus::invalid_binding:     return "invalid_binding";
        case ManifestStatus::too_many_interfaces: return "too_many_interfaces";
        case ManifestStatus::too_many_methods:    return "too_many_methods";
    }
    return "unknown";
}

// Validate a manifest's structure and declared boundaries. It cannot prove
// that a pointer is safe in general; it rejects what can be rejected without
// dereferencing arbitrary memory:
//   * null manifest, unknown manifest_version;
//   * interface_count / method_count above the sanity bounds;
//   * an entry with a zero interface_id or a null method table when it claims
//     methods;
//   * a method table with a duplicate slot.
// A bound method with a zero `entry` is allowed (an unbound slot).
inline ManifestStatus validate_manifest(const ModuleManifest *manifest) noexcept {
    if (manifest == nullptr) return ManifestStatus::null_manifest;
    if (manifest->manifest_version != abix_manifest_version) return ManifestStatus::unsupported_version;
    if (manifest->interface_count > abix_max_manifest_interfaces) return ManifestStatus::too_many_interfaces;
    if (manifest->interface_count != 0 && manifest->interfaces == nullptr) return ManifestStatus::invalid_interface;
    for (uint32_t i = 0; i < manifest->interface_count; ++i) {
        const InterfaceManifestEntry &entry = manifest->interfaces[i];
        if (entry.interface_id == InterfaceId{}) return ManifestStatus::invalid_interface;
        if (entry.method_count > abix_max_manifest_methods) return ManifestStatus::too_many_methods;
        if (entry.method_count != 0 && entry.methods == nullptr) return ManifestStatus::invalid_interface;
        for (uint32_t m = 0; m < entry.method_count; ++m) {
            const uint32_t slot = entry.methods[m].slot;
            for (uint32_t previous = 0; previous < m; ++previous)
                if (entry.methods[previous].slot == slot) return ManifestStatus::invalid_binding;
        }
    }
    return ManifestStatus::ok;
}

}   // namespace skl::abix::poly

#endif   // SKL_ABIX_INTERFACE_MODULE_H
