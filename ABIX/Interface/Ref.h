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
#ifndef SKL_ABIX_INTERFACE_REF_H
#define SKL_ABIX_INTERFACE_REF_H

#include "ABIX/Interface/Contract.h"
#include "ABIX/Interface/Registry.h"

namespace skl::abix::poly {

// A resolved interface handle: one provider object plus the binding resolved at
// query time. resolve() performs exactly one registry lookup and copies the
// bound adapter pointers into a slot-indexed cache; every call afterwards is a
// plain native indirect call — no per-call lookup, name comparison or type
// inspection. The runtime never becomes a generic dispatch loop.
//
// The provider's object must outlive the handle and must be the object the
// adapters were generated for (the handle creator owns the correct pointer and
// any `this` adjustment).
template<size_t MaxMethods = 32>
class InterfaceRef {
public:
    InterfaceRef() noexcept = default;

    // Resolve (concrete_type, interface_id, version) once. `object` is the
    // provider's object handle; the adapters receive it as their first
    // argument. `version == 0` selects the newest registered version.
    template<size_t RegistryCapacity>
    InterfaceStatus resolve(const InterfaceRegistry<RegistryCapacity> &registry, void *object,
        model::TypeId concrete_type, InterfaceId interface_id, uint32_t version = 0) noexcept {
        reset();
        if (object == nullptr) return InterfaceStatus::invalid_table;
        const auto *impl = registry.find(concrete_type, interface_id, version);
        if (impl == nullptr) {
            uint32_t newest = 0;
            if (registry.available(concrete_type, interface_id, newest)) return InterfaceStatus::version_mismatch;
            return InterfaceStatus::interface_not_found;
        }
        return resolve_methods(impl->interface_id, impl->version, impl->methods, impl->method_count, object);
    }

    // Resolve directly from a provider-owned adapter table. This is the path a
    // dynamically discovered module uses: the table comes from its manifest,
    // and the caller holds a module lease for its lifetime.
    InterfaceStatus resolve_methods(InterfaceId interface_id, uint32_t version, const InterfaceMethod *methods,
        uint32_t method_count, void *object) noexcept {
        reset();
        if (object == nullptr || (method_count != 0 && methods == nullptr)) return InterfaceStatus::invalid_table;
        uint32_t highest = 0;
        for (uint32_t i = 0; i < method_count; ++i) {
            const uint32_t slot = methods[i].slot;
            if (slot >= MaxMethods) return InterfaceStatus::method_capacity_exceeded;
            if (slot + 1 > highest) highest = slot + 1;
        }
        for (uint32_t i = 0; i < method_count; ++i) {
            _entries[methods[i].slot] = methods[i].entry;
            _signatures[methods[i].slot] = methods[i].signature;
        }
        _object = object;
        _interface_id = interface_id;
        _version = version;
        _method_count = highest;
        _resolved = true;
        return InterfaceStatus::ok;
    }

    // Cached adapter for a slot, or nullptr when the slot is unbound. The
    // returned function takes the object handle as its first argument; pass
    // object() for it. Runs no registry lookup.
    template<typename Fn>
    Fn method(uint32_t slot) const noexcept {
        static_assert(sizeof(Fn) <= sizeof(uintptr_t), "function pointer wider than uintptr_t");
        if (slot >= _method_count || _entries[slot] == 0) return nullptr;
        Fn fn = nullptr;
        memcpy(&fn, &_entries[slot], sizeof(Fn));
        return fn;
    }

    bool bound(uint32_t slot) const noexcept { return slot < _method_count && _entries[slot] != 0; }
    bool valid() const noexcept { return _resolved && _object != nullptr; }
    void *object() const noexcept { return _object; }
    InterfaceId interface_id() const noexcept { return _interface_id; }
    uint32_t version() const noexcept { return _version; }
    // Number of slots spanned by the implementation (highest bound slot + 1).
    uint32_t method_count() const noexcept { return _method_count; }

    // The method ABI signature recorded at `slot`, or a zero hash when the slot
    // is unbound. Callers compare it against the interface contract they expect
    // before trusting a cached entry point.
    InterfaceMethodHash signature(uint32_t slot) const noexcept {
        return slot < _method_count ? _signatures[slot] : InterfaceMethodHash{};
    }

    // Verify that `slot` is bound and that its recorded signature matches the
    // expected one (a zero `expected` means "do not check the signature").
    // Returns `method_not_found` for an unbound slot and `signature_mismatch`
    // when the bound method's signature differs, so a call through the assumed
    // function type is refused rather than performed unsafely.
    InterfaceStatus verify(uint32_t slot, InterfaceMethodHash expected) const noexcept {
        if (slot >= _method_count || _entries[slot] == 0) return InterfaceStatus::method_not_found;
        if (!(expected == InterfaceMethodHash{}) && !(_signatures[slot] == expected))
            return InterfaceStatus::signature_mismatch;
        return InterfaceStatus::ok;
    }

    // Fetch a slot's entry point only after verifying its signature. `out` is
    // set to nullptr on failure.
    template<typename Fn>
    InterfaceStatus method(Fn &out, uint32_t slot, InterfaceMethodHash expected) const noexcept {
        const InterfaceStatus status = verify(slot, expected);
        out = status == InterfaceStatus::ok ? method<Fn>(slot) : nullptr;
        return status;
    }

private:
    void reset() noexcept {
        _object = nullptr;
        _interface_id = InterfaceId{};
        _version = 0;
        _method_count = 0;
        _resolved = false;
        for (size_t i = 0; i < (MaxMethods == 0 ? 1 : MaxMethods); ++i) {
            _entries[i] = 0;
            _signatures[i] = InterfaceMethodHash{};
        }
    }

    void *_object = nullptr;
    InterfaceId _interface_id{};
    uint32_t _version = 0;
    uint32_t _method_count = 0;
    bool _resolved = false;
    uintptr_t _entries[MaxMethods == 0 ? 1 : MaxMethods]{};
    InterfaceMethodHash _signatures[MaxMethods == 0 ? 1 : MaxMethods]{};
};

// Query convenience: resolve an interface handle for one object. Returns the
// handle by value; check `valid()` or the status.
template<size_t MaxMethods = 32, size_t RegistryCapacity>
inline InterfaceRef<MaxMethods> query_interface(const InterfaceRegistry<RegistryCapacity> &registry, void *object,
    model::TypeId concrete_type, InterfaceId interface_id, uint32_t version = 0) noexcept {
    InterfaceRef<MaxMethods> ref;
    (void)ref.resolve(registry, object, concrete_type, interface_id, version);
    return ref;
}

}   // namespace skl::abix::poly

#endif   // SKL_ABIX_INTERFACE_REF_H
