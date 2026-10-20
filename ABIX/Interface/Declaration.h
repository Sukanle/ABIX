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
 * ABIX Interface layer: optional declaration sugar for signatures and proxies.
 *
 * `SKL_ABIX_INTERFACE_DECLARE(Type, list, version)` expands one method list into
 * `TypeTraits` (signatures / id / Slots) and a `Type` proxy derived from
 * `InterfaceProxy<TypeTraits>`. Runtime registration stays explicit: register
 * `TypeTraits::id` and an adapter table with `InterfaceRegistry`, or pass the
 * table directly to `bind_methods()`.
 *
 * Each list entry is:
 *
 *   X(method, cxx_return_type, return_type_id, {parameter_type_ids...})
 *
 * e.g.
 *   constexpr model::TypeId i32 = model::canonical_type_id("prim:i32");
 *
 *   #define SHAPE_METHODS(X)       \
 *       X(area, int32_t, i32, {}) \
 *       X(resize, int32_t, i32, {i32, i32})
 *
 *   SKL_ABIX_INTERFACE_DECLARE(Shape, SHAPE_METHODS, 1);
 *
 * `return_type_id` and each parameter are `model::TypeId`s from
 * `model::canonical_type_id`; `method_signature` hashes those, not their names.
 * List order is slot order and also determines the interface id.
 *
 * For each method the proxy generates:
 *
 *   ret_t method(Args&&...) const noexcept;                  // direct call
 *   InterfaceStatus method(ret_t& out, Args&&...) const;    // checked slot
 *
 * Argument types are deduced from the call site (`std::decay_t<Args>...`), so
 * pass values whose native types match the adapter. The checked overload
 * requires a non-void return type.
 *
 * The macro cannot express overloads, reference/record/array parameters, `void`
 * returns or complex ABI signatures; AMC (which has the Clang AST) or a
 * hand-written Traits/proxy is the right tool there. Invoke the macro inside
 * the target namespace with an unqualified type name: the stringized name is
 * the interface identity.
 *
 * `SKL_ABIX_INTERFACE_TRAITS` and `SKL_ABIX_INTERFACE_DATA` are the two halves;
 * `SKL_ABIX_INTERFACE_DECLARE` combines them. The generated struct definition
 * does not include a trailing semicolon, so the invocation needs one.
 */
#ifndef SKL_ABIX_INTERFACE_DECLARATION_H
#define SKL_ABIX_INTERFACE_DECLARATION_H

#include <type_traits>

#include "ABIX/Interface/Proxy.h"   // IWYU pragma: keep

// Pack a semantic version into one unsigned integer:
//   major * 10'000'000 + minor * 100'000 + patch
// `major` and `minor` are reduced to two decimal digits, `patch` to five.
#define SKL_ABIX_VERSION_NUMBER(major, minor, patch) \
    ((((major) % 100U) * 10'000'000U) + (((minor) % 100U) * 100'000U) + ((patch) % 100'000U))

// Unpack a value produced by SKL_ABIX_VERSION_NUMBER into three unsigned int
// variables. The caller supplies the trailing semicolon:
//
//   SKL_ABIX_VERSION_EXTRACT(version, major, minor, patch);
//
#define SKL_ABIX_VERSION_EXTRACT(version, major, minor, patch) \
    major = ((version) / 10'000'000U);                         \
    minor = (((version) / 100'000U) % 100U);                   \
    patch = ((version) % 100'000U)

#define SKL_ABIX_INTERFACE_METHOD(fn, ret_t, reg_ret_t, ...) \
    skl::abix::poly::method_signature(#fn, reg_ret_t, skl::mics::utils::array __VA_ARGS__),

#define SKL_ABIX_INTERFACE_FORWARD(fn, ret_t, ...)                                             \
    template<typename... Args>                                                                 \
    auto fn(Args &&...args) const noexcept -> ret_t {                                          \
        return this->template call<ret_t (*)(const void *, std::decay_t<Args>...)>(            \
            Traits::slots.fn, static_cast<Args &&>(args)...);                                  \
    }                                                                                          \
    template<typename... Args>                                                                 \
    auto fn(ret_t &out, Args &&...args) const noexcept -> ::skl::abix::poly::InterfaceStatus { \
        return this->template call_checked<ret_t (*)(const void *, std::decay_t<Args>...)>(    \
            out, Traits::slots.fn, static_cast<Args &&>(args)...);                             \
    }

#define SKL_ABIX_INTERFACE_SLOT(fn, ...) uint32_t fn;

#define SKL_ABIX_INTERFACE_SLOTS(list) \
    struct alignas(8) Slots {          \
        list(SKL_ABIX_INTERFACE_SLOT)  \
    };                                 \
    alignas(8) static constexpr Slots slots = skl::mics::utils::make_iota<Slots, signatures.size(), 0>();

#define SKL_ABIX_INTERFACE_TRAITS(type, list, ver)                                                   \
    class type;                                                                                      \
    struct type##Traits {                                                                            \
        using Data = type;                                                                           \
        static constexpr auto signatures = skl::mics::utils::array{list(SKL_ABIX_INTERFACE_METHOD)}; \
        static constexpr auto version = ver;                                                         \
        static constexpr auto id = skl::abix::poly::interface_id(#type, version, signatures);        \
        SKL_ABIX_INTERFACE_SLOTS(list)                                                               \
    };

#define SKL_ABIX_INTERFACE_DATA(type, list)                              \
    struct type : public skl::abix::poly::InterfaceProxy<type##Traits> { \
        using InterfaceProxy<type##Traits>::InterfaceProxy;              \
        using Traits = type##Traits;                                     \
        list(SKL_ABIX_INTERFACE_FORWARD)                                 \
    }

#define SKL_ABIX_INTERFACE_DECLARE(type, list, ver) \
    SKL_ABIX_INTERFACE_TRAITS(type, list, ver)      \
    SKL_ABIX_INTERFACE_DATA(type, list)

#endif   // SKL_ABIX_INTERFACE_DECLARATION_H
