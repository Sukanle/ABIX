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
 *
 * ABIX Interface layer: the C++ proxy API.
 *
 * An interface proxy is a lightweight handle that binds an interface contract to
 * one provider object and caches the resolved adapters. It reads like a C++
 * `Shape*`, but it is not a base class: the concrete type is never modified and
 * no compiler vtable is used. `UniqueInterfaceProxy<Traits>` holds the shared
 * machinery (binding, status, object access, cached calls); a per-interface
 * class adds only its methods, and `Traits` supplies the contract's identity,
 * version, expected method signatures and typed slot table.
 *
 *   constexpr auto i32 = model::canonical_type_id("prim:i32");
 *   struct ShapeTraits {
 *       static constexpr mics::util::array signatures = {
 *             method_signature("area", i32, mics::util::array{}),
 *             ...
 *        };
 *       static constexpr uint32_t version = 1;
 *       static constexpr InterfaceId id = interface_id("Shape", version, signatures);
 *       alignas(8) struct Slots {
 *             uint32_t area;
 *             ...
 *        };
 *       // Slots are zero-based.
 *       alignas(8) static constexpr Slots slots =
 *           mics::util::make_iota<Slots, signatures.size(), 0>();
 *   };
 *   class Shape : public UniqueInterfaceProxy<ShapeTraits> {
 *       using Base = UniqueInterfaceProxy<ShapeTraits>;
 *   public:
 *       using Base::Base;
 *       int32_t area() const noexcept { return this->template call<int(*)(const void*)>(ShapeTraits::slots.area); }
 *       InterfaceStatus area(int32_t& out) const noexcept {
 *           return this->template call_checked<int(*)(const void*)>(out, ShapeTraits::slots.area);
 *       }
 *   };
 *
 * `UniqueInterfaceProxy` stores the adapter entries inline (one cache per object).
 * `SharedInterfaceProxy<Traits>` is the compact alternative: it stores the
 * object handle plus a pointer to one `InterfaceSlotTable<slot_capacity>`
 * shared by every object of the same implementation.
 */
#ifndef SKL_ABIX_INTERFACE_PROXY_H
#define SKL_ABIX_INTERFACE_PROXY_H

#include <stddef.h>
#include <stdint.h>

#include <utility>

#include "SKL/ABIX/Interface/Ref.h"

namespace skl::abix::poly {

// Shared, interface-agnostic proxy machinery. Traits must provide:
//   static constexpr InterfaceId id;                  // the contract identity
//   static constexpr uint32_t version;                // the contract version
//   static constexpr mics::util::array signatures;   // expected per slot
// and a typed `Slots` table (`Traits::slots.<method>`) whose fields name the
// slots used by the proxy's methods. `signatures.size()` is the slot capacity:
// one signature is required for every slot, including sparse zero placeholders.
template<typename Traits>
class UniqueInterfaceProxy {
public:
    static constexpr size_t slot_capacity = Traits::signatures.size();

    UniqueInterfaceProxy() noexcept = default;

    // Bind through an in-process registry. The status is retained; use
    // `status()` or `valid()` to observe it (bind never throws or aborts).
    template<size_t RegistryCapacity>
    UniqueInterfaceProxy(const InterfaceRegistry<RegistryCapacity> &registry, void *object, model::TypeId concrete_type,
        uint32_t version = Traits::version) noexcept {
        (void)bind(registry, object, concrete_type, version);
    }

    template<size_t RegistryCapacity>
    InterfaceStatus bind(const InterfaceRegistry<RegistryCapacity> &registry, void *object, model::TypeId concrete_type,
        uint32_t version = Traits::version) noexcept {
        _status = _ref.resolve(registry, object, concrete_type, Traits::id, version, Traits::signatures);
        return _status;
    }

    // Bind directly to a provider-owned adapter table (a discovered module's
    // manifest). The caller must keep the module loaded (hold its lease) for as
    // long as the proxy is used.
    InterfaceStatus bind_methods(InterfaceId interface_id, uint32_t version, const InterfaceMethod *methods,
        uint32_t method_count, void *object) noexcept {
        if (interface_id != Traits::id) {
            _ref.clear();
            return _status = InterfaceStatus::interface_not_found;
        }
        if (version != Traits::version) {
            _ref.clear();
            return _status = InterfaceStatus::version_mismatch;
        }
        _status = _ref.resolve_methods(interface_id, version, methods, method_count, object, Traits::signatures);
        return _status;
    }

    InterfaceStatus status() const noexcept { return _status; }
    bool valid() const noexcept { return _status == InterfaceStatus::ok && _ref.valid(); }

    void *object() const noexcept { return _ref.object(); }
    InterfaceId interface_id() const noexcept { return _ref.valid() ? Traits::id : InterfaceId{}; }
    uint32_t version() const noexcept { return _ref.valid() ? Traits::version : 0; }
    uint32_t method_count() const noexcept { return _ref.method_count(); }

    // Verify that `slot` is bound. The contract signature was checked when the
    // proxy was bound; provider signatures are not retained per proxy.
    InterfaceStatus verify(uint32_t slot) const noexcept {
        if (slot >= slot_capacity || !_ref.bound(slot)) return InterfaceStatus::method_not_found;
        return InterfaceStatus::ok;
    }

protected:
    // Cached entry point for a slot (nullptr when unbound). No lookup, no check.
    template<typename Fn>
    Fn method(uint32_t slot) const noexcept {
        return _ref.template method<Fn>(slot);
    }

    // Call a slot's adapter, passing the object handle as its first argument.
    // Precondition: the proxy is valid and the slot is bound.
    template<typename Fn, typename... Args>
    decltype(auto) call(uint32_t slot, Args &&...args) const noexcept {
        const Fn fn = _ref.template method<Fn>(slot);
        return fn(_ref.object(), std::forward<Args>(args)...);
    }

    // Call a slot's adapter with an explicit status: an unbound slot reports
    // `method_not_found` instead of performing an unsafe call, so a legitimate
    // result can never be confused with a failure.
    template<typename Fn, typename R, typename... Args>
    InterfaceStatus call_checked(R &out, uint32_t slot, Args &&...args) const noexcept {
        const Fn fn = _ref.template method<Fn>(slot);
        if (fn == nullptr) return InterfaceStatus::method_not_found;
        out = fn(_ref.object(), std::forward<Args>(args)...);
        return InterfaceStatus::ok;
    }

private:
    InterfaceSlotCache<slot_capacity> _ref;
    InterfaceStatus _status = InterfaceStatus::interface_not_found;
};

// Build a shared adapter table from the registry. The caller owns the table and
// must keep it alive for every SharedInterfaceProxy that references it.
template<typename Traits, size_t RegistryCapacity>
InterfaceStatus make_shared_slot_table(InterfaceSlotTable<Traits::signatures.size()> &table,
    const InterfaceRegistry<RegistryCapacity> &registry, model::TypeId concrete_type,
    uint32_t version = Traits::version) noexcept {
    const auto *impl = registry.find(concrete_type, Traits::id, version);
    if (impl == nullptr) {
        table.clear();
        uint32_t newest = 0;
        if (registry.available(concrete_type, Traits::id, newest)) return InterfaceStatus::version_mismatch;
        return InterfaceStatus::interface_not_found;
    }
    return table.build(impl->methods, impl->method_count, Traits::signatures);
}

template<typename Traits>
InterfaceStatus make_shared_slot_table(InterfaceSlotTable<Traits::signatures.size()> &table,
    const InterfaceMethod *methods, uint32_t method_count) noexcept {
    return table.build(methods, method_count, Traits::signatures);
}

// Shared-storage proxy: many instances can point at one InterfaceSlotTable.
// The table is built once per concrete implementation; each proxy adds only the
// object handle. This is the vtable-like layout:
//
//   proxy -> shared slot table -> adapter
//
// The table must outlive every proxy that references it.
template<typename Traits>
class SharedInterfaceProxy {
public:
    static constexpr size_t slot_capacity = Traits::signatures.size();
    using Table = InterfaceSlotTable<slot_capacity>;

    SharedInterfaceProxy() noexcept = default;

    SharedInterfaceProxy(const Table &table, void *object) noexcept { (void)bind(table, object); }

    template<size_t RegistryCapacity>
    SharedInterfaceProxy(const InterfaceRegistry<RegistryCapacity> &registry, void *object, model::TypeId concrete_type,
        Table &table, uint32_t version = Traits::version) noexcept {
        (void)bind(registry, object, concrete_type, table, version);
    }

    InterfaceStatus bind(const Table &table, void *object) noexcept {
        _table = &table;
        _object = object;
        _status = (object != nullptr && table.valid()) ? InterfaceStatus::ok : InterfaceStatus::invalid_table;
        return _status;
    }

    template<size_t RegistryCapacity>
    InterfaceStatus bind(const InterfaceRegistry<RegistryCapacity> &registry, void *object, model::TypeId concrete_type,
        Table &table, uint32_t version = Traits::version) noexcept {
        _table = nullptr;
        _object = nullptr;
        _status = make_shared_slot_table<Traits>(table, registry, concrete_type, version);
        if (_status != InterfaceStatus::ok) return _status;
        return bind(table, object);
    }

    InterfaceStatus bind_methods(
        Table &table, const InterfaceMethod *methods, uint32_t method_count, void *object) noexcept {
        _table = nullptr;
        _object = nullptr;
        _status = make_shared_slot_table<Traits>(table, methods, method_count);
        if (_status != InterfaceStatus::ok) return _status;
        return bind(table, object);
    }

    InterfaceStatus status() const noexcept { return _status; }
    bool valid() const noexcept {
        return _status == InterfaceStatus::ok && _object != nullptr && _table != nullptr && _table->valid();
    }

    void *object() const noexcept { return _object; }
    InterfaceId interface_id() const noexcept { return valid() ? Traits::id : InterfaceId{}; }
    uint32_t version() const noexcept { return valid() ? Traits::version : 0; }
    uint32_t method_count() const noexcept { return _table != nullptr ? _table->method_count() : 0; }

    InterfaceStatus verify(uint32_t slot) const noexcept {
        return _table != nullptr && _table->bound(slot) ? InterfaceStatus::ok : InterfaceStatus::method_not_found;
    }

protected:
    template<typename Fn>
    Fn method(uint32_t slot) const noexcept {
        return _table != nullptr ? _table->template method<Fn>(slot) : nullptr;
    }

    template<typename Fn, typename... Args>
    decltype(auto) call(uint32_t slot, Args &&...args) const noexcept {
        const Fn fn = method<Fn>(slot);
        return fn(_object, std::forward<Args>(args)...);
    }

    template<typename Fn, typename R, typename... Args>
    InterfaceStatus call_checked(R &out, uint32_t slot, Args &&...args) const noexcept {
        const Fn fn = method<Fn>(slot);
        if (fn == nullptr) return InterfaceStatus::method_not_found;
        out = fn(_object, std::forward<Args>(args)...);
        return InterfaceStatus::ok;
    }

private:
    void *_object = nullptr;
    const Table *_table = nullptr;
    InterfaceStatus _status = InterfaceStatus::interface_not_found;
};

}   // namespace skl::abix::poly

#endif   // SKL_ABIX_INTERFACE_PROXY_H
