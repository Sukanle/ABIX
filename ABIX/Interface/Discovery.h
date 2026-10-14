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
#ifndef SKL_ABIX_INTERFACE_DISCOVERY_H
#define SKL_ABIX_INTERFACE_DISCOVERY_H

#include "ABIX/DLL/Object.h"
#include "ABIX/Interface/ModuleRegistry.h"

namespace skl::abix::poly {

// Discovery ties the loader to the registry: after a module is loaded, ask it
// for its manifest through the fixed entry, validate it, and register what it
// implements. The platform export table is only consulted for that one symbol;
// no interface meaning is inferred from any other export.

struct DiscoveryResult {
    ManifestStatus manifest = ManifestStatus::null_manifest;
    ModuleStatus registration = ModuleStatus::invalid_manifest;
    const ModuleManifest *queried = nullptr;

    bool ok() const noexcept { return manifest == ManifestStatus::ok && registration == ModuleStatus::ok; }
};

// Resolve `abix_module_query_v1` from a loaded module, validate it, and register
// its interfaces. `control` is the lifecycle control for this load; it becomes
// active on success. A missing entry, an unknown manifest version, a malformed
// manifest or a duplicate declaration all fail explicitly.
template<size_t ModuleCapacity = 16, size_t InterfaceCapacity = 128>
inline DiscoveryResult discover_module(const dll::Object &module, ModuleControl &control,
    ModuleRegistry<ModuleCapacity, InterfaceCapacity> &registry) noexcept {
    DiscoveryResult result;
    using QueryFn = const ModuleManifest *(*)(void);
    auto query = reinterpret_cast<QueryFn>(module.resolve(abix_module_query_symbol));
    if (query == nullptr) {
        result.manifest = ManifestStatus::null_manifest;
        result.registration = ModuleStatus::invalid_manifest;
        return result;
    }
    result.queried = query();
    result.manifest = validate_manifest(result.queried);
    if (result.manifest != ManifestStatus::ok) {
        result.registration = ModuleStatus::invalid_manifest;
        return result;
    }
    result.registration = registry.register_module(control, *result.queried);
    return result;
}

}   // namespace skl::abix::poly

#endif   // SKL_ABIX_INTERFACE_DISCOVERY_H
