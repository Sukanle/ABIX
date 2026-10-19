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
#ifndef SKL_ABIX_INTERFACE_MODULE_REGISTRY_H
#define SKL_ABIX_INTERFACE_MODULE_REGISTRY_H

#include <stddef.h>

#include "ABIX/Interface/Lease.h"
#include "ABIX/Interface/Module.h"
#include "ABIX/Interface/Ref.h"

namespace skl::abix::poly {

// One interface implementation discovered in a loaded module. `control` is the
// lifetime token: acquire a lease before calling, so the module cannot unload
// underneath the call.
struct ModuleInterface {
    ModuleInstanceId module = invalid_module_instance;
    InterfaceId interface_id{};
    uint32_t version = 0;
    const InterfaceMethod *methods = nullptr;
    uint32_t method_count = 0;
    ModuleControl *control = nullptr;
};

enum class ModuleStatus : uint32_t {
    ok = 0,
    already_registered,            // the same control is already registered
    invalid_manifest,              // structural or version error in the manifest
    invalid_state,                 // the control is not in the loading state
    capacity_exceeded,             // no free module slot
    interface_capacity_exceeded,   // no room for the module's interfaces
    duplicate_interface,           // the manifest declares the same id+version twice
    not_registered,                // unregistering an unknown instance
    busy,                          // unload refused: leases are still active
};

constexpr const char *to_string(ModuleStatus status) noexcept {
    switch (status) {
        case ModuleStatus::ok:                          return "ok";
        case ModuleStatus::already_registered:          return "already_registered";
        case ModuleStatus::invalid_manifest:            return "invalid_manifest";
        case ModuleStatus::invalid_state:               return "invalid_state";
        case ModuleStatus::capacity_exceeded:           return "capacity_exceeded";
        case ModuleStatus::interface_capacity_exceeded: return "interface_capacity_exceeded";
        case ModuleStatus::duplicate_interface:         return "duplicate_interface";
        case ModuleStatus::not_registered:              return "not_registered";
        case ModuleStatus::busy:                        return "busy";
    }
    return "unknown";
}

// Registry of dynamically discovered module interfaces.
//
//   * several modules may implement the same interface; queries return all of
//     them and never overwrite one with another;
//   * a version is exact when requested, and "any" (returning the newest) when
//     0 is requested;
//   * unregistering begins an unload and only succeeds once every lease is
//     released; otherwise it reports `busy` and the module stays draining
//     (no new leases) so the caller can retry after in-flight calls finish.
//
// The registry does not own the manifest or the adapters: both are static
// storage inside the provider, valid while the module is loaded.
//
// Threading: the registry is a bind-time structure and is **not** internally
// synchronized. Concurrent register/unregister/lookup must be serialized by the
// caller (lookups return pointers into an array that compacts on unregister).
// The lifecycle half is safe to drive from many threads: leases may be acquired
// and released concurrently with `unregister_module`.
template<size_t ModuleCapacity = 16, size_t InterfaceCapacity = 128>
class ModuleRegistry {
public:
    ModuleStatus register_module(ModuleControl &control, const ModuleManifest &manifest) noexcept {
        const ManifestStatus manifest_status = validate_manifest(&manifest);
        if (manifest_status != ManifestStatus::ok) return ModuleStatus::invalid_manifest;
        // Re-registering the same control is idempotent.
        for (const auto &slot : _modules)
            if (slot.used && slot.control == &control) return ModuleStatus::already_registered;
        if (manifest.interface_count != 0) {
            // A module must not advertise the same (id, version) twice.
            for (uint32_t i = 0; i < manifest.interface_count; ++i)
                for (uint32_t j = i + 1; j < manifest.interface_count; ++j)
                    if (manifest.interfaces[i].interface_id == manifest.interfaces[j].interface_id
                        && manifest.interfaces[i].interface_version == manifest.interfaces[j].interface_version)
                        return ModuleStatus::duplicate_interface;
        }
        if (_module_count == ModuleCapacity) return ModuleStatus::capacity_exceeded;
        if (_interface_count + manifest.interface_count > InterfaceCapacity)
            return ModuleStatus::interface_capacity_exceeded;

        // Slots are not compacted on unregister, so append to the first free slot
        // rather than to index `_module_count` (which can overwrite a live slot
        // whenever a lower-indexed slot was freed).
        size_t slot_index = 0;
        while (slot_index < ModuleCapacity && _modules[slot_index].used)
            ++slot_index;
        if (slot_index == ModuleCapacity) return ModuleStatus::capacity_exceeded;

        if (!activate_module(control)) return ModuleStatus::invalid_state;

        control.id = next_module_instance_id();
        _modules[slot_index].control = &control;
        _modules[slot_index].begin = static_cast<uint32_t>(_interface_count);
        _modules[slot_index].count = manifest.interface_count;
        _modules[slot_index].used = true;
        ++_module_count;
        for (uint32_t i = 0; i < manifest.interface_count; ++i) {
            const InterfaceManifestEntry &entry = manifest.interfaces[i];
            ModuleInterface binding;
            binding.module = control.id;
            binding.interface_id = entry.interface_id;
            binding.version = entry.interface_version;
            binding.methods = entry.methods;
            binding.method_count = entry.method_count;
            binding.control = &control;
            _interfaces[_interface_count++] = binding;
        }
        return ModuleStatus::ok;
    }

    // Begin unloading and remove the module once it is idle. `busy` means the
    // module is now draining and the caller must retry after releasing leases.
    ModuleStatus unregister_module(ModuleInstanceId id) noexcept {
        ModuleSlot *slot = find_slot(id);
        if (slot == nullptr) return ModuleStatus::not_registered;
        (void)begin_unload(*slot->control);
        if (!wait_for_idle(*slot->control)) return ModuleStatus::busy;

        const uint32_t begin = slot->begin;
        const uint32_t count = slot->count;
        for (size_t i = begin; i + count < _interface_count; ++i)
            _interfaces[i] = _interfaces[i + count];
        _interface_count -= count;
        for (auto &other : _modules) {
            if (!other.used) continue;
            if (other.begin > begin) other.begin -= count;
        }
        mark_unloaded(*slot->control);
        slot->used = false;
        slot->control = nullptr;
        slot->begin = 0;
        slot->count = 0;
        --_module_count;
        return ModuleStatus::ok;
    }

    bool registered(ModuleInstanceId id) const noexcept { return find_slot(id) != nullptr; }
    size_t module_count() const noexcept { return _module_count; }
    size_t interface_count() const noexcept { return _interface_count; }

    const ModuleInterface *interface_at(size_t index) const noexcept {
        return index < _interface_count ? &_interfaces[index] : nullptr;
    }

    // Every implementation of (id, version), in registration order. `version`
    // 0 matches any version. Returns the number written (up to max).
    size_t find_interface(InterfaceId id, uint32_t version, const ModuleInterface **out, size_t max) const noexcept {
        size_t written = 0;
        for (size_t i = 0; i < _interface_count && written < max; ++i) {
            if (_interfaces[i].interface_id != id) continue;
            if (version != 0 && _interfaces[i].version != version) continue;
            out[written++] = &_interfaces[i];
        }
        return written;
    }

    // The newest implementation of (id, version); `version` 0 matches any.
    const ModuleInterface *find_first(InterfaceId id, uint32_t version = 0) const noexcept {
        const ModuleInterface *best = nullptr;
        for (size_t i = 0; i < _interface_count; ++i) {
            if (_interfaces[i].interface_id != id) continue;
            if (version != 0 && _interfaces[i].version != version) continue;
            if (best == nullptr || _interfaces[i].version >= best->version) best = &_interfaces[i];
        }
        return best;
    }

private:
    struct ModuleSlot {
        ModuleControl *control = nullptr;
        uint32_t begin = 0;
        uint32_t count = 0;
        bool used = false;
    };

    const ModuleSlot *find_slot(ModuleInstanceId id) const noexcept {
        if (id == invalid_module_instance) return nullptr;
        for (const auto &slot : _modules)
            if (slot.used && slot.control->id == id) return &slot;
        return nullptr;
    }

    ModuleSlot *find_slot(ModuleInstanceId id) noexcept {
        return const_cast<ModuleSlot *>(static_cast<const ModuleRegistry *>(this)->find_slot(id));
    }

    ModuleSlot _modules[ModuleCapacity == 0 ? 1 : ModuleCapacity]{};
    ModuleInterface _interfaces[InterfaceCapacity == 0 ? 1 : InterfaceCapacity]{};
    size_t _module_count = 0;
    size_t _interface_count = 0;
};

// A resolved module interface binding plus the lease that keeps its module
// loaded. Calling through `ref` is only valid while this object is alive.
template<size_t MaxMethods = 32>
struct BoundInterface {
    InterfaceRef<MaxMethods> ref;
    ModuleLease lease;

    bool valid() const noexcept { return ref.valid() && lease.valid(); }
};

// Resolve a discovered module interface against a provider object and hold a
// lease. Returns an invalid binding (no lease, unresolved ref) when the module
// is no longer active.
template<size_t MaxMethods = 32>
inline BoundInterface<MaxMethods> bind_interface(const ModuleInterface &binding, void *object) noexcept {
    BoundInterface<MaxMethods> result;
    if (binding.control == nullptr) return result;
    result.lease = acquire_lease(*binding.control);
    if (!result.lease.valid()) return result;
    (void)result.ref.resolve_methods(
        binding.interface_id, binding.version, binding.methods, binding.method_count, object);
    return result;
}

}   // namespace skl::abix::poly

#endif   // SKL_ABIX_INTERFACE_MODULE_REGISTRY_H
