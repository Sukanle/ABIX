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
 * `SKL_ABIX_INTERFACE_DECLARE(storage, Type, list, version)` expands one method
 * list into `TypeTraits` (signatures / id / Slots) and a proxy. `storage` is
 * `UNIQUE` (inline slot entries, class `Type`) or `SHARED` (class `SharedType`,
 * with a shared `InterfaceSlotTable`). Runtime registration stays explicit:
 * register `TypeTraits::id` and an adapter table with `InterfaceRegistry`, or
 * pass the table directly to `bind_methods()`.
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
 *   SKL_ABIX_INTERFACE_DECLARE(UNIQUE, Shape, SHAPE_METHODS, 1);
 *   SKL_ABIX_INTERFACE_DECLARE(SHARED, Shape, SHAPE_METHODS, 1);
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
 * `SKL_ABIX_INTERFACE_TRAITS` plus `SKL_ABIX_INTERFACE_UNIQUE_DATA` or
 * `_SHARED_DATA` are the two halves; `DECLARE`, `DECLARE_UNIQUE`,
 * `DECLARE_SHARED` and `DECLARE_BOTH` combine them. The generated struct
 * definition does not include a trailing semicolon, so the invocation needs one.
 *
 * Two proxy layouts are available:
 *
 *   SKL_ABIX_INTERFACE_DECLARE(UNIQUE, ...)  // inline slot entries (type)
 *   SKL_ABIX_INTERFACE_DECLARE(SHARED, ...)  // shared table (Shared##type)
 *   SKL_ABIX_INTERFACE_DECLARE_BOTH(...)     // both classes
 *
 * A shared proxy points at one InterfaceSlotTable<slot_capacity>; build it with
 * make_shared_slot_table<Traits>(table, registry, concrete_type) and keep it
 * alive for every proxy that uses it. Shared proxies are 24 bytes regardless of
 * method count; inline proxies are roughly 24 + 8 * method_count.
 *
 * The layouts perform the same in the call microbenchmark. Choose unique for a
 * self-contained handle, per-object adapter tables or a single/few objects;
 * choose shared for many objects of one implementation, where the table is
 * amortized and can live in read-only storage.
 *
 * The final variadic argument is an optional type attribute inserted before the
 * generated type names. It exists for platform DLL decorations, for example:
 *
 *   #if defined(_WIN32)
 *   #  ifdef SHAPE_BUILD_DLL
 *   #    define SHAPE_API __declspec(dllexport)
 *   #  else
 *   #    define SHAPE_API __declspec(dllimport)
 *   #  endif
 *   #else
 *   #  define SHAPE_API
 *   #endif
 *
 *   SKL_ABIX_INTERFACE_DECLARE(UNIQUE, Shape, SHAPE_METHODS, 1, SHAPE_API);
 *
 * ABIX Interface is header-only, so the attribute is usually unnecessary; use
 * it only when the generated Traits/proxy type is genuinely part of a DLL
 * boundary. Pass the same attribute to both half macros when using them
 * separately.
 *
 * Existing provider classes can supply adapter entries without hand-written
 * thunks:
 *
 *   struct Box {
 *       int32_t area() const noexcept;
 *       int32_t resize(int32_t w, int32_t h) noexcept;
 *   };
 *
 *   const InterfaceMethod methods[] = {
 *       SKL_ABIX_INTERFACE_MEMBER_ENTRY(ShapeTraits, area, &Box::area),
 *       SKL_ABIX_INTERFACE_MEMBER_ENTRY(ShapeTraits, resize, &Box::resize),
 *   };
 *
 * `member_adapter<Member>::invoke` takes the const object handle first and
 * forwards the member's own parameters. It supports const/non-const, noexcept
 * and static members; a static member ignores the handle. The exception
 * specification is a single `bool Noex` parameter so Clang does not see `const`
 * and `const noexcept` as ambiguous partial specializations. Volatile,
 * ref-qualified and overloaded members need an explicit member-pointer
 * expression or a hand-written wrapper.
 */
#ifndef SKL_ABIX_INTERFACE_DECLARATION_H
#define SKL_ABIX_INTERFACE_DECLARATION_H

#include <iterator>
#include <type_traits>
#include <utility>

#include "SKL/ABIX/Interface/Proxy.h"   // IWYU pragma: keep

namespace skl::abix::poly {

// Adapters for existing C++ member functions. The generated function has the
// interface adapter shape: the object handle first, then the member's own
// parameters. A non-const member receives a const handle and removes const
// internally; a const member keeps it. This matches the `const void*` first
// parameter used by the declaration macros' generated proxies.
template<auto Member>
struct member_adapter;

// A single `noexcept(Noex)` parameter covers both throwing and non-throwing
// members. Separate `const` and `const noexcept` specializations are ambiguous
// under Clang's partial-ordering rules.
template<typename R, typename C, typename... Args, bool Noex, R (C::*Member)(Args...) noexcept(Noex)>
struct member_adapter<Member> {
    using function_type = R (*)(const void *, Args...);

    static R invoke(const void *self, Args... args) {
        auto *object = const_cast<C *>(static_cast<const C *>(self));
        return (object->*Member)(std::forward<Args>(args)...);
    }
};

template<typename R, typename C, typename... Args, bool Noex, R (C::*Member)(Args...) const noexcept(Noex)>
struct member_adapter<Member> {
    using function_type = R (*)(const void *, Args...);

    static R invoke(const void *self, Args... args) {
        return (static_cast<const C *>(self)->*Member)(std::forward<Args>(args)...);
    }
};

// Static member functions (and ordinary function pointers) have no implicit
// object. The interface adapter still accepts the object handle for a uniform
// calling convention, then ignores it and forwards the contract parameters.
// If the function itself already expects the handle, use interface_entry()
// directly instead of this adapter.
template<typename R, typename... Args, bool Noex, R (*Member)(Args...) noexcept(Noex)>
struct member_adapter<Member> {
    using function_type = R (*)(const void *, Args...);

    static R invoke(const void *self, Args... args) {
        (void)self;
        return Member(std::forward<Args>(args)...);
    }
};

}   // namespace skl::abix::poly

// Build one InterfaceMethod entry from an existing member function. `slot_name`
// is the field name in Traits::slots; `member` is a member-pointer expression:
//
//   const InterfaceMethod methods[] = {
//       SKL_ABIX_INTERFACE_MEMBER_ENTRY(ShapeTraits, area, &Box::area),
//       SKL_ABIX_INTERFACE_MEMBER_ENTRY(ShapeTraits, resize, &Box::resize),
//   };
//
// For overloaded members, pass an explicit cast expression. The expression may
// need surrounding parentheses when its type contains top-level commas.
#define SKL_ABIX_INTERFACE_MEMBER_ENTRY(Traits, slot_name, member)                              \
    InterfaceMethod{Traits::signatures[Traits::slots.slot_name],                                \
        ::skl::abix::poly::interface_entry(&::skl::abix::poly::member_adapter<member>::invoke), \
        Traits::slots.slot_name, 0}

/* Build a named InterfaceMethod table from a member-entry list. The first
 * argument names the interface, so `Shape` produces `ShapeMethods`. Each list
 * entry invokes its argument with the arguments MEMBER_ENTRY expects:
 *
 *   #define BOX_METHODS(X)                   \
 *       X(ShapeTraits, area, &Box::area)     \
 *       X(ShapeTraits, resize, &Box::resize)
 *
 *   SKL_ABIX_INTERFACE_MEMBER_TABLE(Shape, BOX_METHODS);
 *
 * The table is a `mics::util::array<InterfaceMethod, N>` with static storage
 * duration, so its `data()` / `size()` can feed an InterfaceImplementation.
 */
#define SKL_ABIX_INTERFACE_MEMBER_ENTRY_ITEM(...) SKL_ABIX_INTERFACE_MEMBER_ENTRY(__VA_ARGS__),
#define SKL_ABIX_INTERFACE_MEMBER_TABLE(interface, list) \
    const auto interface##Methods = skl::mics::util::array{list(SKL_ABIX_INTERFACE_MEMBER_ENTRY_ITEM)};

// Build one InterfaceImplementation record from a member table. `interface`
// names `interface##Traits` and `interface##Methods`; `type` is the concrete
// type's model::TypeId expression:
//
//   registry.add(SKL_ABIX_INTERFACE_IMPLEMENTATION(Shape, box_id, 0));
//
// `flags` is the reserved InterfaceImplementation field.
#define SKL_ABIX_INTERFACE_IMPLEMENTATION(interface, type, flags)                               \
    {interface##Traits::id, type, std::data(interface##Methods), std::size(interface##Methods), \
        interface##Traits::version, flags}

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
    skl::abix::poly::method_signature(#fn, reg_ret_t, skl::mics::util::array __VA_ARGS__),

#define SKL_ABIX_INTERFACE_FORWARD(fn, ret_t, ...)                                             \
    template<typename... Args>                                                                 \
    auto fn(Args &&...args) const noexcept -> ret_t {                                          \
        return this->template call<ret_t (*)(const void *, std::decay_t<Args>...)>(            \
            Traits::slots.fn, std::forward<Args>(args)...);                                    \
    }                                                                                          \
    template<typename... Args>                                                                 \
    auto fn(ret_t &out, Args &&...args) const noexcept -> ::skl::abix::poly::InterfaceStatus { \
        return this->template call_checked<ret_t (*)(const void *, std::decay_t<Args>...)>(    \
            out, Traits::slots.fn, std::forward<Args>(args)...);                               \
    }

#define SKL_ABIX_INTERFACE_SLOT(fn, ...) uint32_t fn;

#define SKL_ABIX_INTERFACE_SLOTS(list) \
    struct alignas(8) Slots {          \
        list(SKL_ABIX_INTERFACE_SLOT)  \
    };                                 \
    alignas(8) static constexpr Slots slots = skl::mics::util::make_iota<Slots, signatures.size(), 0>();

#define SKL_ABIX_INTERFACE_TRAITS(type, list, ver, ...)                                             \
    struct type;                                                                                    \
    struct __VA_ARGS__ type##Traits {                                                               \
        static constexpr auto signatures = skl::mics::util::array{list(SKL_ABIX_INTERFACE_METHOD)}; \
        static constexpr auto version = ver;                                                        \
        static constexpr auto id = skl::abix::poly::interface_id(#type, version, signatures);       \
        SKL_ABIX_INTERFACE_SLOTS(list)                                                              \
    };

#define SKL_ABIX_INTERFACE_UNIQUE_DATA(type, list, ...)                                    \
    struct __VA_ARGS__ type : public skl::abix::poly::UniqueInterfaceProxy<type##Traits> { \
        using UniqueInterfaceProxy<type##Traits>::UniqueInterfaceProxy;                    \
        using Traits = type##Traits;                                                       \
        list(SKL_ABIX_INTERFACE_FORWARD)                                                   \
    }

// Backward-compatible spelling for the inline/exclusive data macro.
#define SKL_ABIX_INTERFACE_DATA(type, list, ...) SKL_ABIX_INTERFACE_UNIQUE_DATA(type, list, __VA_ARGS__)

#define SKL_ABIX_INTERFACE_SHARED_DATA(type, list, ...)                                            \
    struct __VA_ARGS__ Shared##type : public skl::abix::poly::SharedInterfaceProxy<type##Traits> { \
        using SharedInterfaceProxy<type##Traits>::SharedInterfaceProxy;                            \
        using Traits = type##Traits;                                                               \
        list(SKL_ABIX_INTERFACE_FORWARD)                                                           \
    }

#define SKL_ABIX_INTERFACE_UNIQUE_DECLARE(type, list, ver, ...) \
    SKL_ABIX_INTERFACE_TRAITS(type, list, ver, __VA_ARGS__)     \
    SKL_ABIX_INTERFACE_UNIQUE_DATA(type, list, __VA_ARGS__)

#define SKL_ABIX_INTERFACE_DECLARE_UNIQUE(type, list, ver, ...) \
    SKL_ABIX_INTERFACE_UNIQUE_DECLARE(type, list, ver, __VA_ARGS__)

#define SKL_ABIX_INTERFACE_DECLARE_SHARED(type, list, ver, ...) \
    SKL_ABIX_INTERFACE_TRAITS(type, list, ver, __VA_ARGS__)     \
    SKL_ABIX_INTERFACE_SHARED_DATA(type, list, __VA_ARGS__)

#define SKL_ABIX_INTERFACE_DECLARE_BOTH(type, list, ver, ...) \
    SKL_ABIX_INTERFACE_TRAITS(type, list, ver, __VA_ARGS__)   \
    SKL_ABIX_INTERFACE_UNIQUE_DATA(type, list, __VA_ARGS__);  \
    SKL_ABIX_INTERFACE_SHARED_DATA(type, list, __VA_ARGS__)

// storage is UNIQUE or SHARED.
#define SKL_ABIX_INTERFACE_DECLARE(storage, type, list, ver, ...) \
    SKL_ABIX_INTERFACE_TRAITS(type, list, ver, __VA_ARGS__)       \
    SKL_ABIX_INTERFACE_##storage##_DATA(type, list, __VA_ARGS__)

#endif   // SKL_ABIX_INTERFACE_DECLARATION_H
