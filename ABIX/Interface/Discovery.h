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

namespace detail {

// Take/remove one OS-level reference to the module so a held lease blocks
// FreeLibrary/dlclose at the OS level. Windows resolves the base address with
// GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS (which increments the reference count);
// POSIX re-opens the object by path (which increments the count without
// re-running initializers). A single reference is held across all leases: it is
// taken when the first lease is acquired and dropped when the last is released.
inline bool pin_module(ModuleControl &control) noexcept {
#ifdef SKL_ABIX_WINDOWS
    if (control.module_handle == nullptr) return false;
    HMODULE pinned = nullptr;
    return GetModuleHandleExA(
               GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCSTR>(control.module_handle), &pinned)
        != 0;
#else
    if (control.module_path == nullptr) return false;
    control.pin_state = dlopen(control.module_path, RTLD_NOW | RTLD_LOCAL);
    return control.pin_state != nullptr;
#endif
}

inline void unpin_module(ModuleControl &control) noexcept {
#ifdef SKL_ABIX_WINDOWS
    if (control.module_handle != nullptr) FreeLibrary(reinterpret_cast<HMODULE>(control.module_handle));
#else
    if (control.pin_state != nullptr) {
        dlclose(control.pin_state);
        control.pin_state = nullptr;
    }
#endif
}

}   // namespace detail

// Bind the loaded module's platform identity to the lifecycle control so leases
// can hold an OS-level reference. The loader (this discovery step) is the only
// place that knows the handle and path, which is why ModuleControl stays out of
// the registry.
inline void bind_module_platform(ModuleControl &control, const dll::Object &module) noexcept {
    control.module_handle = module.module();
    control.module_path = module.module_path();
    control.pin = &detail::pin_module;
    control.unpin = &detail::unpin_module;
}

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
    bind_module_platform(control, module);
    result.registration = registry.register_module(control, *result.queried);
    return result;
}

}   // namespace skl::abix::poly

#endif   // SKL_ABIX_INTERFACE_DISCOVERY_H
