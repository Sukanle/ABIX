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
 * no compiler vtable is used. `InterfaceProxy<Traits>` holds the shared
 * machinery (binding, status, object access, cached calls); a per-interface
 * class adds only its methods, and `Traits` supplies the contract's identity,
 * version, slot count and (optionally) the expected method signatures.
 *
 *   struct ShapeTraits {
 *       static constexpr InterfaceId id{0x91, 0x00};   // computed by AMC
 *       static constexpr uint32_t version = 1;
 *       static constexpr size_t slots = 1;             // slot capacity
 *       static constexpr uint32_t area_slot = 0;
 *   };
 *   class Shape : public InterfaceProxy<ShapeTraits> {
 *       using Base = InterfaceProxy<ShapeTraits>;
 *   public:
 *       using Base::Base;
 *       int32_t area() const noexcept { return call<ShapeTraits::area_fn>(ShapeTraits::area_slot); }
 *       InterfaceStatus area(int32_t& out) const noexcept {
 *           return call_checked<ShapeTraits::area_fn>(out, ShapeTraits::area_slot);
 *       }
 *   };
 */
#ifndef SKL_ABIX_INTERFACE_PROXY_H
#define SKL_ABIX_INTERFACE_PROXY_H

#include <stddef.h>
#include <stdint.h>

#include <type_traits>
#include <utility>

#include "ABIX/Interface/Ref.h"

namespace skl::abix::poly {

// `Traits::signatures` is optional: when present, `bind()` verifies every slot's
// recorded method signature against it before the proxy becomes valid.
template<typename Traits, typename = void>
struct has_method_signatures : std::false_type {};
template<typename Traits>
struct has_method_signatures<Traits, std::void_t<decltype(Traits::signatures)>> : std::true_type {};

// Shared, interface-agnostic proxy machinery. Traits must provide:
//   static constexpr InterfaceId id;      // the contract identity
//   static constexpr uint32_t version;    // the contract version
//   static constexpr size_t slots;        // slot capacity (>= highest slot + 1)
// and may provide:
//   static constexpr InterfaceMethodHash signatures[slots];  // expected per slot
template<typename Traits>
class InterfaceProxy {
public:
    InterfaceProxy() noexcept = default;

    // Bind through an in-process registry. The status is retained; use
    // `status()` or `valid()` to observe it (bind never throws or aborts).
    template<size_t RegistryCapacity>
    InterfaceProxy(const InterfaceRegistry<RegistryCapacity> &registry, void *object, model::TypeId concrete_type,
        uint32_t version = Traits::version) noexcept {
        (void)bind(registry, object, concrete_type, version);
    }

    template<size_t RegistryCapacity>
    InterfaceStatus bind(const InterfaceRegistry<RegistryCapacity> &registry, void *object, model::TypeId concrete_type,
        uint32_t version = Traits::version) noexcept {
        _status = _ref.resolve(registry, object, concrete_type, Traits::id, version);
        return finish();
    }

    // Bind directly to a provider-owned adapter table (a discovered module's
    // manifest). The caller must keep the module loaded (hold its lease) for as
    // long as the proxy is used.
    InterfaceStatus bind_methods(InterfaceId interface_id, uint32_t version, const InterfaceMethod *methods,
        uint32_t method_count, void *object) noexcept {
        _status = _ref.resolve_methods(interface_id, version, methods, method_count, object);
        return finish();
    }

    InterfaceStatus status() const noexcept { return _status; }
    bool valid() const noexcept { return _status == InterfaceStatus::ok && _ref.valid(); }

    void *object() const noexcept { return _ref.object(); }
    InterfaceId interface_id() const noexcept { return _ref.interface_id(); }
    uint32_t version() const noexcept { return _ref.version(); }
    uint32_t method_count() const noexcept { return _ref.method_count(); }

    // Verify one slot against the contract's expected signature (when Traits
    // provides one); without a declared signature this only checks that the slot
    // is bound.
    InterfaceStatus verify(uint32_t slot) const noexcept {
        if constexpr (has_method_signatures<Traits>::value)
            return slot < Traits::slots ? _ref.verify(slot, Traits::signatures[slot])
                                        : InterfaceStatus::method_not_found;
        return _ref.verify(slot, InterfaceMethodHash{});
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
        return fn(_ref.object(), static_cast<Args &&>(args)...);
    }

    // Call a slot's adapter with an explicit status: an unbound slot reports
    // `method_not_found` instead of performing an unsafe call, so a legitimate
    // result can never be confused with a failure.
    template<typename Fn, typename R, typename... Args>
    InterfaceStatus call_checked(R &out, uint32_t slot, Args &&...args) const noexcept {
        const Fn fn = _ref.template method<Fn>(slot);
        if (fn == nullptr) return InterfaceStatus::method_not_found;
        out = fn(_ref.object(), static_cast<Args &&>(args)...);
        return InterfaceStatus::ok;
    }

private:
    // After a successful resolve, check every declared signature. A zero expected
    // hash means "do not check this slot".
    InterfaceStatus finish() noexcept {
        if (_status != InterfaceStatus::ok) return _status;
        if constexpr (has_method_signatures<Traits>::value) {
            for (uint32_t slot = 0; slot < Traits::slots; ++slot) {
                if (Traits::signatures[slot] == InterfaceMethodHash{}) continue;
                const InterfaceStatus status = _ref.verify(slot, Traits::signatures[slot]);
                if (status != InterfaceStatus::ok) return _status = status;
            }
        }
        return _status;
    }

    InterfaceRef<Traits::slots> _ref;
    InterfaceStatus _status = InterfaceStatus::interface_not_found;
};

}   // namespace skl::abix::poly

#endif   // SKL_ABIX_INTERFACE_PROXY_H
