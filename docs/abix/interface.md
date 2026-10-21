# ABIX Interface — non-intrusive polymorphism

<p align="center">
  <a href="interface_zh.md">中文</a> · English
</p>

The ABIX Interface layer defines the **cross-language interface contract** and the **method dispatch** mechanism. It is a layer of ABIX, not of any compiler's object model: an interface is an independent contract, and a provider type is never modified to implement it.

It lives in `ABIX/Interface/` (namespace `skl::abix::poly`) and is exposed by `ABIX/ABIX.h`.

## Why an interface ABI, not a vtable ABI

A C++ virtual call is not just a function pointer fetched from an object: it may involve multiple inheritance, `this` adjustment, thunks, RTTI and compiler-specific vtable layouts. Rust trait objects, Zig function tables and other languages have yet another representation. Even when two languages both say "vtable", their entry order, pointer adjustment and calling rules differ.

ABIX therefore does **not** standardise any private vtable. It defines an interface above the vtable:

- **Interface identity** — a stable content hash over the interface name, version and ordered method signatures. Not a class name, not a vtable address.
- **Method identity** — `(interface_id, slot)`, with the method signature verified independently. A matching name or shape alone never makes two interfaces compatible.
- **Implementation binding** — how one concrete type reaches a method, through a generated adapter that receives the provider's object handle as its first argument. The binding names the adapter by signature, never a vtable slot.

These are **contracts, not layout facts**: they never change `TypeID` / `LayoutHash` / `ABIHash`. Interfaces can also be carried in the metadata region (extension kinds 7–11) so a consumer discovers them at run time.

## Compared with C++ virtual functions

The same task — three shapes each computing an area, then one polymorphic call over a set of shapes — written two ways. The left column is a native C++ virtual function; the right column is ABIX Interface. Both programs print `area=9`, `area=12` and `area=10`, and both compile and run as shown.

<table>
<tr>
<th align="left">C++ virtual functions (intrusive)</th>
<th align="left">ABIX Interface (non-intrusive)</th>
</tr>
<tr>
<td valign="top" markdown="1">

```cpp
#include <cstdio>

struct Shape {
    virtual ~Shape() = default;
    virtual int area() const = 0;
};
struct Square : Shape {
    explicit Square(int s) : side(s) {}
    int area() const override { return side * side; }
    int side;
};
struct Rectangle : Shape {
    Rectangle(int w, int h) : width(w), height(h) {}
    int area() const override { return width * height; }
    int width, height;
};
struct Triangle : Shape {
    Triangle(int b, int h) : base(b), height(h) {}
    int area() const override { return base * height / 2; }
    int base, height;
};
int total(const Shape &s) { return s.area(); }
int main() {
    Square square{3};
    Rectangle rectangle{2, 6};
    Triangle triangle{4, 5};
    const Shape *shapes[] = {&square, &rectangle, &triangle};
    for (const Shape *shape : shapes) std::printf("area=%d\n", total(*shape));
    return 0;
}
```

</td>
<td valign="top" markdown="1">

```cpp
#include <cstdio>
#include "ABIX/Interface/Interface.h"

using namespace skl::abix::poly;
using skl::abix::model::TypeId;

constexpr TypeId i32 = skl::abix::model::canonical_type_id("prim:i32");
constexpr auto area_sig = method_signature("area", i32, nullptr, 0);

// Interface class: Traits describes the contract; Shape only adds methods.
struct ShapeTraits {
    static constexpr skl::mics::utils::array signatures = {area_sig};
    static constexpr InterfaceId id = interface_id("Shape", 1, signatures);
    static constexpr uint32_t version = 1;
    struct alignas(8) Slots {
        uint32_t area;
    };
    alignas(8) static constexpr Slots slots = skl::mics::utils::make_iota<Slots, signatures.size(), 0>();
};
class Shape : public UniqueInterfaceProxy<ShapeTraits> {
    using Base = UniqueInterfaceProxy<ShapeTraits>;

public:
    using Base::Base;
    int area() const noexcept { return this->template call<int (*)(const void *)>(ShapeTraits::slots.area); }
};

// Three implementations: plain structs, no inheritance, no virtual functions.
struct Square {
    int side;
};
struct Rectangle {
    int width;
    int height;
};
struct Triangle {
    int base;
    int height;
};

int square_area(const void *self) {
    const auto *s = static_cast<const Square *>(self);
    return s->side * s->side;
}
int rectangle_area(const void *self) {
    const auto *r = static_cast<const Rectangle *>(self);
    return r->width * r->height;
}
int triangle_area(const void *self) {
    const auto *t = static_cast<const Triangle *>(self);
    return t->base * t->height / 2;
}

constexpr TypeId square_id{0x11, 0x00};
constexpr TypeId rectangle_id{0x12, 0x00};
constexpr TypeId triangle_id{0x13, 0x00};

InterfaceStatus register_shape(InterfaceRegistry<4> &registry, TypeId type, const InterfaceMethod *methods) {
    return registry.add({ShapeTraits::id, type, methods, 1, ShapeTraits::version, 0});
}

int main() {
    const InterfaceMethod square_methods[] = {{area_sig, interface_entry(&square_area), 0, 0}};
    const InterfaceMethod rectangle_methods[] = {{area_sig, interface_entry(&rectangle_area), 0, 0}};
    const InterfaceMethod triangle_methods[] = {{area_sig, interface_entry(&triangle_area), 0, 0}};

    InterfaceRegistry<4> registry;
    if (register_shape(registry, square_id, square_methods) != InterfaceStatus::ok) return 1;
    if (register_shape(registry, rectangle_id, rectangle_methods) != InterfaceStatus::ok) return 1;
    if (register_shape(registry, triangle_id, triangle_methods) != InterfaceStatus::ok) return 1;

    Square square{3};
    Rectangle rectangle{2, 6};
    Triangle triangle{4, 5};
    struct Case {
        void *object;
        TypeId type;
    };
    const Case cases[] = {{&square, square_id}, {&rectangle, rectangle_id}, {&triangle, triangle_id}};
    for (const Case &c : cases) {
        Shape shape(registry, c.object, c.type);
        if (!shape.valid()) return 1;
        std::printf("area=%d\n", shape.area());
    }
    return 0;
}
```

</td>
</tr>
</table>

The difference is not whether polymorphism is possible, but **what carries it**:

- The left encodes polymorphism in the **type system**: `Shape` is a base class, `Square`/`Rectangle`/`Triangle` must inherit and `override`, and `area()` dispatches through the object's vptr into a compiler-private vtable.
- The right encodes polymorphism in an **interface contract**: `ShapeTraits` describes the identity, version and slots, and the `Shape` proxy caches the resolved adapter entries; `Square`/`Rectangle`/`Triangle` are plain `struct`s with no inheritance and no virtual function. After binding, `shape.area()` is a native indirect call.

### Side by side

| Dimension                                | C++ virtual functions                                    | ABIX Interface                                                                     |
| ---------------------------------------- | -------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| What carries the abstraction             | Base class + pure virtual (the type system)              | Interface identity `InterfaceId` + method `slot` (a contract)                      |
| Intrusion on the concrete type           | Must inherit, must `override`                            | None; a plain C++ `struct` / Rust / Zig type all work                              |
| Dispatch basis                           | vptr in the object → a compiler-private vtable slot      | An explicitly resolved adapter entry `(interface_id, slot)`                        |
| `this` adjustment / multiple inheritance | Compiler-generated thunk hidden in the vtable            | The provider's adapter, with the object handle as its first argument               |
| Resolve timing                           | Implicitly fetches the vptr on every call                | `resolve()` once, then zero lookups                                                |
| Heterogeneous collection                 | Via a base pointer `const Shape*`                        | `{ void* object, adapter }`, one uniform signature                                 |
| Cross-language                           | Not portable (layout/order differ)                       | One `interface_id` implemented by several languages                                |
| Failure mode                             | UB (wrong call, dangling vptr)                           | `InterfaceStatus`: `interface_not_found` / `version_mismatch` / `method_not_found` |
| ABI stability                            | Adding a virtual or changing inheritance is an ABI break | An interface is a contract; it never feeds `TypeID` / `ABIHash`                    |

### Which to use

- **Plain C++, one compile-time world, no cross-boundary need**: virtual functions are enough, with zero extra ceremony.
- **Across DLLs / across languages / "program to the interface, not the type"**: use Interface. A consumer can mention only `InterfaceId` (see the cross-module path in [Example 2](#examples)) and keep working when the provider changes its concrete type.
- **They coexist**: ABIX does not ask you to give up C++ semantics — a provider keeps using virtual functions internally and binds one concrete type's methods to interface slots at the boundary only (AMC's `[[interface]]` / `[[implements]]` declarations). The interface can be declared in a header like a C++ abstract class: `[[interface]]` defines the contract, `[[implements]]` establishes the binding, and `amc generate -l cpp` emits `k<Name>Interface` / `k<Name>Version` / `<Name>_<method>_fn` / a `Traits` struct with a typed `Slots` table, so a consumer can wrap it as `Shape s = Shape::bind(...); s.area();`.

## ABIX Interface vs ngcpp/proxy

[ngcpp/proxy](https://github.com/ngcpp/proxy) (formerly Microsoft `proxy`) and ABIX Interface solve the same broad problem — runtime polymorphism without forcing the implementation type into a class hierarchy — but they put the abstraction at different boundaries.

ABIX Interface models an **independent, versioned, cross-language contract**: an `InterfaceId`, ordered method signatures, slot-indexed adapters and an explicit bind step. ngcpp/proxy models a **C++20 facade**: a pointer-like `pro::proxy<Facade>` whose facade declares conventions built from dispatches, and whose implementation is anything that satisfies those expressions at compile time.

Both examples below use their library's macro form and both return `area == 12`.

<table>
<tr>
<th align="left">ABIX Interface: contract macro + member adapter</th>
<th align="left">ngcpp/proxy: facade + dispatch macro</th>
</tr>
<tr>
<td valign="top" markdown="1">

```cpp
#include "ABIX/Interface/Declaration.h"

using namespace skl::abix::poly;
constexpr auto i32 =
    skl::abix::model::canonical_type_id("prim:i32");

#define SHAPE_METHODS(X) X(area, int32_t, i32, {})
SKL_ABIX_INTERFACE_DECLARE(UNIQUE, Shape, SHAPE_METHODS, 1);

struct Box {
    int32_t width;
    int32_t height;
    int32_t area() const noexcept {
        return width * height;
    }
};

constexpr skl::abix::model::TypeId box_id{0x51, 0x01};

int main() {
    const InterfaceMethod methods[] = {
        SKL_ABIX_INTERFACE_MEMBER_ENTRY(
            ShapeTraits, area, &Box::area),
    };

    InterfaceRegistry<4> registry;
    registry.add({ShapeTraits::id, box_id, methods,
        1, ShapeTraits::version, 0});

    Box box{3, 4};
    Shape shape(registry, &box, box_id);
    return shape.valid() && shape.area() == 12 ? 0 : 1;
}
```

The macro generates the `Traits`, `signatures`, zero-based `Slots` and the `Shape` proxy. `SKL_ABIX_INTERFACE_MEMBER_ENTRY` turns `Box::area` into an adapter entry; the registry binding supplies the concrete `TypeId`.

</td>
<td valign="top" markdown="1">

```cpp
#include <proxy/proxy.h>

PRO_DEF_MEM_DISPATCH(MemArea, area);

struct Shape : pro::facade_builder
    ::add_convention<MemArea, int() const noexcept>
    ::build {};

struct Box {
    int width;
    int height;
    int area() const noexcept {
        return width * height;
    }
};

int main() {
    Box box{3, 4};
    pro::proxy<Shape> shape = &box;
    return shape->area() == 12 ? 0 : 1;
}
```

`PRO_DEF_MEM_DISPATCH` generates the dispatch for the `area` member expression. `Shape` is a facade assembled from `facade_builder`; assigning `&box` performs the compile-time conformance check, and `shape->area()` invokes the erased member.

</td>
</tr>
</table>

The macro roles are different: ABIX's `SKL_ABIX_INTERFACE_DECLARE` generates the contract data (`id`, `signatures`, `Slots`) and the proxy type, while `PRO_DEF_MEM_DISPATCH` generates only the dispatch for one member expression and `facade_builder` composes the facade. In ABIX the provider must register a method table; in ngcpp/proxy constructing the proxy from the pointer is the conformance check.

### Where they differ

| Dimension                  | ABIX Interface                                                                           | ngcpp/proxy                                                                                      |
| -------------------------- | ---------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| Abstraction unit           | `InterfaceId` + version + ordered method signatures + typed slots                        | Facade type built from conventions, dispatches and overloads                                     |
| Provider conformance       | Explicit `InterfaceRegistry` registration and a provider-owned adapter table             | Implicit at compile time: the pointer type must satisfy the facade                               |
| Binding                    | Runtime lookup once (`bind` / `bind_methods`), then cached slot entries                  | No registry or resolve step; the proxy is constructed from a pointer                             |
| Dispatch                   | Slot-indexed native indirect call through a cached adapter                               | Facade-generated dispatch/accessor over a type-erased pointer                                    |
| Versioning                 | Explicit contract version; exact/newest selection; `version_mismatch`                    | Source-level facade definition; no runtime version negotiation                                   |
| Cross-language / cross-DLL | Stable C ABI, `abix_module_query_v1`, leases, cross-language consumers                   | C++ only; facade and implementation share the same C++ ABI/toolchain                             |
| Ownership                  | Borrowed object handle; module `lease`; separate Owner ABI for owned handles             | Raw pointer (non-owning), `unique_ptr` / `shared_ptr`, or in-place `make_proxy`                  |
| Failure model              | `InterfaceStatus` (`interface_not_found`, `version_mismatch`, `signature_mismatch`, ...) | Compile-time constraints; optional `rtti` / `bad_proxy_cast`; weak dispatch can be configured    |
| Overloads                  | Separate slots/signatures; explicit member-pointer cast for overloaded members           | Native overload resolution across convention overloads                                           |
| Lifetime                   | Provider object must outlive the proxy; a module lease keeps a discovered adapter mapped | Encoded by the pointer type: raw is non-owning, smart pointers own, `make_proxy` may be in-place |
| Requirements               | C++17, header-only interface layer                                                       | C++20, header-only facade/pointer library                                                        |

### Which to use

- Choose **ABIX Interface** when the boundary is a DLL/module or a foreign language, when the contract needs an explicit identity and version, when method signatures must be verified before a call, or when the provider object already exists and must stay untouched.
- Choose **ngcpp/proxy** when the whole program is C++20, when you want pointer-like ownership variants (`raw`, `unique_ptr`, `shared_ptr`, in-place values), native overload/operator conventions, and compile-time facade checking without a registry.
- They compose: use ABIX at the cross-boundary contract layer, and use a `pro::proxy` internally for C++ value/ownership polymorphism. ABIX supplies the ABI and versioned contract; ngcpp/proxy supplies a rich C++-only pointer abstraction.

### Performance and footprint

Measurements below use one method, `int32_t area() const noexcept`, a non-owning proxy, and the same box object for every mechanism. ABIX was built as `C++17`; ngcpp/proxy as `C++20`.

The native virtual baseline is compiled with `-fno-devirtualize`. This matters: GCC 16 at `-O3` can speculatively devirtualise even through a `noinline` opaque accessor, emitting a vptr guard followed by an inlined `Derived::area`. That measures a guarded direct call (about 0.27 ns), not a generic vtable dispatch. The true indirect vtable path is about 0.75 ns.

**Call latency** (x86-64, GCC 16.2.1, `-O3 -DNDEBUG`, Google Benchmark, ~2 s, CPU scaling enabled — compare trends, not absolute nanoseconds):

| Mechanism                                    | ns/call | vs true native virtual |
| -------------------------------------------- | ------: | ---------------------: |
| native direct call                           |    0.19 |                  0.25x |
| native virtual call (true indirect)          |    0.75 |                   1.0x |
| native virtual call (compiler devirtualised) |    0.27 |                  0.36x |
| ABIX direct adapter                          |    0.19 |                  0.25x |
| ABIX `InterfaceRef` cached entry             |    0.75 |                   1.0x |
| ABIX `UniqueInterfaceProxy`                  |    0.75 |                   1.0x |
| ABIX `SharedInterfaceProxy`                  |    0.75 |                   1.0x |
| ngcpp/proxy `pro::proxy`                     |    0.75 |                   1.0x |

The non-intrusive proxies match a true vtable dispatch in this microbenchmark. ABIX's unique and shared layouts and ngcpp/proxy all converge at roughly the same cost; the indirect call dominates, and the extra table load in shared mode is not separately visible. The remaining gap is to **direct/devirtualised** calls: when the compiler can prove the dynamic type, it can eliminate the dispatch entirely. ABIX/proxy cannot do that because the implementation is selected at run time (or erased at compile time). This is the price of not owning the object model (registration, type erasure, cross-DLL/cross-language contracts); it is not a per-call registry lookup.

**Footprint** (64-bit; `N` = number of interface methods):

| Mechanism                   |             Per object | Per implementation / type       | 5-method object | 6-method object |
| --------------------------- | ---------------------: | ------------------------------- | --------------: | --------------: |
| native vtable               | 8-byte `vptr` + fields | vtable ≈ `8N` + RTTI/header     |      8 + fields |      8 + fields |
| ngcpp/proxy `pro::proxy`    |                  24–32 | static dispatch/accessor tables |           24–32 |           24–32 |
| ABIX `UniqueInterfaceProxy` |              `24 + 8N` | none                            |              64 |              72 |
| ABIX `SharedInterfaceProxy` |                 **24** | `InterfaceSlotTable`: `8N + 8`  |         24 + 48 |         24 + 56 |
| ABIX `InterfaceRef`         |             `32 + 24N` | none                            |             152 |             176 |
| ABIX `InterfaceRegistry<8>` |                      — | 464                             |       bind-time |       bind-time |

`pro::proxy_view` is 16–24 bytes. The ABIX registry and shared table are setup/implementation storage, not per-object storage. For many objects, `SharedInterfaceProxy` is the memory-density choice: one 24-byte handle per object plus one slot table per implementation. For one/few objects, `UniqueInterfaceProxy` is self-contained and avoids the table lifetime contract; it is also required when different objects of the same type need different adapter tables.

```cpp
SKL_ABIX_INTERFACE_DECLARE_BOTH(Shape, SHAPE_METHODS, 1);

// few objects: inline entries
Shape shape(registry, &box, box_id);

// many objects: one table per implementation
InterfaceSlotTable<ShapeTraits::signatures.size()> table;
make_shared_slot_table<ShapeTraits>(table, registry, box_id);

Box a{3, 4}, b{5, 6};
SharedShape first(table, &a);   // 24 bytes
SharedShape second(table, &b);  // 24 bytes, same table
```

## Quick start

Three steps: **register** (record "this concrete type implements this interface" in the registry) → **resolve** (look the binding up once and cache the adapter entries) → **call** (a plain native indirect call). The complete program below needs no shared library.

```cpp
#include <cstdio>

#include "ABIX/Interface/Interface.h"

using namespace skl::abix::poly;
using skl::abix::model::TypeId;

// (1) Provider type: no vtable, never modified. The adapter receives the object
// handle as its first argument.
struct Vec2 { int x, y; };
int vec2_axis(const void *self, int axis) {
    const auto *v = static_cast<const Vec2 *>(self);
    return axis == 0 ? v->x : v->y;
}
using AxisFn = int (*)(const void *, int);

int main() {
    constexpr TypeId      vec2_id{0x11, 0x12};   // type identity recorded by AMC
    constexpr InterfaceId ivec   {0x31, 0x32};   // interface identity computed by AMC

    // (2) Register once: methods bind by a stable slot (slot 0 -> vec2_axis).
    const InterfaceMethod methods[] = {
        {{0xa1, 0x0}, interface_entry(&vec2_axis), /*slot*/ 0, /*flags*/ 0},
    };
    InterfaceRegistry<8> registry;
    if (registry.add({ivec, vec2_id, methods, 1, /*version*/ 1, 0}) != InterfaceStatus::ok) return 1;

    // (3) Resolve once; every call afterwards is a plain native indirect call.
    Vec2 v{2, 3};
    InterfaceRef<8> ref;
    if (ref.resolve(registry, &v, vec2_id, ivec, 1) != InterfaceStatus::ok) return 1;

    const auto axis = ref.method<AxisFn>(0);
    std::printf("x=%d y=%d\n", axis(ref.object(), 0), axis(ref.object(), 1));   // x=2 y=3
}
```

`resolve()` is the only lookup — `registry.lookup_count()` stays `1`; calls after it run no lookup, name comparison or type inspection. The full API is in the [API reference](#api-reference), and more scenarios are in [Examples](#examples).

## Binding-time resolution, native call

The runtime facility is deliberately a _binding-time_ one. `InterfaceRegistry` holds "this concrete type implements this interface" records. `InterfaceRef` resolves once and caches the adapter pointers by slot; every call afterwards is a plain native indirect call. The runtime never becomes a generic dispatch loop.

```cpp
#include "ABIX/Interface/Interface.h"

using namespace skl::abix::poly;

// Provider type: no vtable, not modified.
struct Vec2 { int x, y; };
int vec2_axis(const void *self, int axis) { /* ... */ }

// Adapters, registered once.
const InterfaceMethod methods[] = {
    {{0xa1, 0x0}, interface_entry(&vec2_axis), /*slot*/ 0, /*flags*/ 0},
};
InterfaceRegistry<8> registry;
registry.add({ivec, vec2_id, methods, 1, /*version*/ 1, 0});

// Query once, then call natively.
InterfaceRef<8> ref;
ref.resolve(registry, &object, vec2_id, ivec, 1);
const auto axis = ref.method<int (*)(const void *, int)>(0);
int x = axis(ref.object(), 0);
```

`resolve()` distinguishes `interface_not_found`, `version_mismatch`, `method_not_found` and `method_capacity_exceeded` instead of guessing.

## Dynamic discovery across a module boundary

A provider (a shared library) exposes one fixed entry, `abix_module_query_v1`, returning a stable `ModuleManifest` that lists the interfaces it implements. The platform export table is only consulted to find that entry: no mangled name is decoded and no interface meaning is inferred from a symbol.

```cpp
const InterfaceMethod methods[] = {
    {{0xa1, 0x0}, interface_entry(&vec2_axis), 0, 0},
};
const InterfaceManifestEntry interfaces[] = {
    {ivec, /*version*/ 1, 0, methods, 1, 0},
};
const ModuleManifest manifest = {
    abix_manifest_version, /*module_abi*/ 1, 0, 1, interfaces, 0, 0, nullptr,
};
SKL_ABIX_DEFINE_MODULE_QUERY(&manifest)
```

A consumer validates the manifest (`validate_manifest`), registers it, and queries. Every module instance has a fresh `ModuleInstanceId`; a module may not unload while a binding holds a lease.

```cpp
ModuleControl control;
control.name = "renderer";
ModuleRegistry registry;
registry.register_module(control, manifest);

const auto *binding = registry.find_first(ivec, 1);
auto bound = bind_interface(*binding, &object);   // acquires a module lease
const auto axis = bound.ref.method<AxisFn>(0);
int x = axis(bound.ref.object(), 0);

registry.unregister_module(control.id);           // `busy` until the lease releases
```

Rules:

- several modules may implement one interface; queries return all of them;
- a version is exact when requested and "newest" when `0` is requested;
- re-registering the same instance is idempotent; a module that declares the same `(interface_id, version)` twice is rejected;
- unload begins a drain (no new leases) and completes only after every lease is released, so a cached binding is never a dangling function pointer;
- an unknown manifest version, or a malformed shape, fails clearly rather than being guessed at.

## Owner ABI

Interface ABI says how to call an object; the owner ABI says who owns it and who releases it. Only three modes are formalised — **borrowed**, **owned**, **copied** — and release always returns to the creator's allocation domain: a consumer never `free()`s or `delete`s the pointer, and never infers a deallocator from the address or the layout.

```cpp
#include "ABIX/Interface/Owner.h"

// The provider returns a POD handle; the destroy entry lives in the module.
ABIX_OWNER handle = make_owned(new Renderer, &destroy_renderer, control.id);

// The consumer adopts it, holding a module lease for the object's lifetime.
skl::abix::owner::ref object = skl::abix::owner::ref::adopt(handle, &control);
// ... use object.object() through an interface binding ...
// release calls destroy_renderer exactly once, then drops the lease.
```

An owner handle keeps its creating module loaded until released, so a long-lived object can never outlive the module that must destroy it. Moving the handle transfers ownership; releasing twice is a no-op.

## Examples

Example 1 is a complete in-process program (one interface, three implementation classes, registration, resolution and calls); Examples 2–4 cover cross-module discovery, version coexistence and the Owner ABI.

### Example 1 — end to end: an interface class and three implementations

The contract lives in a shared header: `Traits` carries the interface identity, version, typed slot table and expected signatures, and the proxy class only adds the methods.

```cpp
// shape.h — the shared contract
#pragma once
#include "ABIX/Interface/Interface.h"

namespace demo {

using skl::abix::model::TypeId;
using skl::abix::poly::InterfaceId;
using skl::abix::poly::interface_id;
using skl::abix::poly::method_signature;

inline constexpr TypeId i32 = skl::abix::model::canonical_type_id("prim:i32");

struct ShapeTraits {
    static constexpr skl::mics::utils::array signatures = {
        method_signature("area", i32, nullptr, 0),
        method_signature("perimeter", i32, nullptr, 0),
    };
    static constexpr InterfaceId id = interface_id("Shape", 1, signatures);
    static constexpr uint32_t version = 1;
    struct alignas(8) Slots {
        uint32_t area;
        uint32_t perimeter;
    };
    alignas(8) static constexpr Slots slots = skl::mics::utils::make_iota<Slots, signatures.size(), 0>();
};

class Shape : public skl::abix::poly::UniqueInterfaceProxy<ShapeTraits> {
    using Base = skl::abix::poly::UniqueInterfaceProxy<ShapeTraits>;

public:
    using Base::Base;
    int area() const noexcept { return this->template call<int (*)(const void *)>(ShapeTraits::slots.area); }
    int perimeter() const noexcept {
        return this->template call<int (*)(const void *)>(ShapeTraits::slots.perimeter);
    }
};

}  // namespace demo
```

On the provider side, three plain implementations; each method has one adapter whose first parameter is the object handle, and each implementation has one method table and one registration.

```cpp
// shapes.cpp
// Build: c++ -std=c++17 -I<abix_root> shapes.cpp -o shapes
#include <cstdio>

#include "shape.h"

using demo::ShapeTraits;
using skl::abix::model::TypeId;
using skl::abix::poly::InterfaceMethod;
using skl::abix::poly::InterfaceRegistry;
using skl::abix::poly::InterfaceStatus;
using skl::abix::poly::interface_entry;

// Three implementations: plain structs, no base class, no virtual functions,
// no ABIX members.
struct Square {
    int side;
};
struct Rectangle {
    int width;
    int height;
};
struct Triangle {
    int base;
    int height;
    int left;
    int right;
};

// One adapter per method: the first parameter is the type-erased object handle.
int square_area(const void *self) {
    const auto *s = static_cast<const Square *>(self);
    return s->side * s->side;
}
int square_perimeter(const void *self) {
    return 4 * static_cast<const Square *>(self)->side;
}
int rectangle_area(const void *self) {
    const auto *r = static_cast<const Rectangle *>(self);
    return r->width * r->height;
}
int rectangle_perimeter(const void *self) {
    const auto *r = static_cast<const Rectangle *>(self);
    return 2 * (r->width + r->height);
}
int triangle_area(const void *self) {
    const auto *t = static_cast<const Triangle *>(self);
    return t->base * t->height / 2;
}
int triangle_perimeter(const void *self) {
    const auto *t = static_cast<const Triangle *>(self);
    return t->base + t->left + t->right;
}

// One concrete-type identity per implementation; AMC records these in a real
// project.
constexpr TypeId square_id{0x51, 0x01};
constexpr TypeId rectangle_id{0x51, 0x02};
constexpr TypeId triangle_id{0x51, 0x03};

// One method table per implementation: signature and slot come straight from
// the shared contract.
const InterfaceMethod square_methods[] = {
    {ShapeTraits::signatures[ShapeTraits::slots.area], interface_entry(&square_area), ShapeTraits::slots.area, 0},
    {ShapeTraits::signatures[ShapeTraits::slots.perimeter], interface_entry(&square_perimeter),
        ShapeTraits::slots.perimeter, 0},
};
const InterfaceMethod rectangle_methods[] = {
    {ShapeTraits::signatures[ShapeTraits::slots.area], interface_entry(&rectangle_area), ShapeTraits::slots.area, 0},
    {ShapeTraits::signatures[ShapeTraits::slots.perimeter], interface_entry(&rectangle_perimeter),
        ShapeTraits::slots.perimeter, 0},
};
const InterfaceMethod triangle_methods[] = {
    {ShapeTraits::signatures[ShapeTraits::slots.area], interface_entry(&triangle_area), ShapeTraits::slots.area, 0},
    {ShapeTraits::signatures[ShapeTraits::slots.perimeter], interface_entry(&triangle_perimeter),
        ShapeTraits::slots.perimeter, 0},
};

InterfaceStatus register_shape(
    InterfaceRegistry<8> &registry, TypeId type, const InterfaceMethod *methods, uint32_t count) {
    return registry.add({ShapeTraits::id, type, methods, count, ShapeTraits::version, 0});
}

int main() {
    // Register: one record per "implementation class -> Shape" binding.
    InterfaceRegistry<8> registry;
    if (register_shape(registry, square_id, square_methods, 2) != InterfaceStatus::ok) return 1;
    if (register_shape(registry, rectangle_id, rectangle_methods, 2) != InterfaceStatus::ok) return 1;
    if (register_shape(registry, triangle_id, triangle_methods, 2) != InterfaceStatus::ok) return 1;

    Square square{5};
    Rectangle rectangle{4, 6};
    Triangle triangle{6, 4, 5, 5};

    struct Instance {
        void *object;
        TypeId type;
        const char *name;
    };
    const Instance instances[] = {
        {&square, square_id, "square"},
        {&rectangle, rectangle_id, "rectangle"},
        {&triangle, triangle_id, "triangle"},
    };

    // One call path serves all three implementations: bind once, then plain
    // native indirect calls.
    for (const auto &instance : instances) {
        demo::Shape shape;
        if (shape.bind(registry, instance.object, instance.type) != InterfaceStatus::ok) return 1;
        std::printf("%-9s area=%d perimeter=%d\n", instance.name, shape.area(), shape.perimeter());
    }
    return 0;
}
```

Output:

```
square    area=25 perimeter=20
rectangle area=24 perimeter=20
triangle  area=12 perimeter=16
```

Takeaways:

- `Square` / `Rectangle` / `Triangle` never inherit from `Shape` and are never modified; they stay plain aggregates.
- `signatures` makes `bind()` verify every slot: a method table `signature` that disagrees with the contract reports `signature_mismatch` instead of calling.
- Each `bind()` resolves once; the `area()` / `perimeter()` calls in the loop are plain native indirect calls.
- The caller only holds `demo::Shape` and never names a concrete type; adding an implementation means adding one method table and one registration.

### Example 2 — discover and call across a module

The provider and the consumer share one contract-constant header (the interface identity and version must match):

```cpp
// canvas_interface.h
#pragma once
#include "ABIX/Interface/Interface.h"

namespace canvas {
inline constexpr skl::abix::poly::InterfaceId kInterface{0x51, 0x00};
inline constexpr uint32_t kVersion = 1;
using WidthFn = int (*)(const void *, int);
}  // namespace canvas
```

The provider builds to `libcanvas.so` and exposes only the fixed entry and its own object handle — never the object layout:

```cpp
// canvas_plugin.cpp
// build: c++ -std=c++17 -shared -fPIC -I<abix_root> -o libcanvas.so canvas_plugin.cpp
#include "ABIX/Interface/Interface.h"
#include "canvas_interface.h"

namespace canvas {

struct Canvas { int width; };
Canvas g_canvas{1920};

int canvas_width(const void *self, int) { return static_cast<const Canvas *>(self)->width; }

const skl::abix::poly::InterfaceMethod kMethods[] = {
    {{0x52, 0x00}, skl::abix::poly::interface_entry(&canvas_width), /*slot*/ 0, 0},
};
const skl::abix::poly::InterfaceManifestEntry kInterfaces[] = {
    {kInterface, kVersion, 0, kMethods, 1, 0},
};
const skl::abix::poly::ModuleManifest kManifest = {
    skl::abix::poly::abix_manifest_version, 1, 0, 1, kInterfaces, 0, 0, nullptr,
};

}  // namespace canvas

// The fixed entry: the consumer consults the export table for this alone.
SKL_ABIX_DEFINE_MODULE_QUERY(&canvas::kManifest)

// The provider's object handle, through an ordinary exported symbol.
extern "C" SKL_ABIX_DLL_EXPORT const void *canvas_instance(void) { return &canvas::g_canvas; }
```

The consumer loads, discovers, binds once, calls, then unloads:

```cpp
// canvas_host.cpp
// build: c++ -std=c++17 -I<abix_root> canvas_host.cpp -ldl -o canvas_host
#include <cstdio>

#include "ABIX/DLL/Object.h"
#include "ABIX/Interface/Interface.h"
#include "canvas_interface.h"

using namespace skl::abix::poly;

int main() {
    skl::abix::dll::Object lib;
    if (!lib.load("./libcanvas.so")) return 1;

    ModuleRegistry<> registry;
    ModuleControl control;   // the loader-owned lifecycle token
    control.name = "canvas";
    if (!discover_module(lib, control, registry).ok()) return 1;   // validate + register

    const auto *binding = registry.find_first(canvas::kInterface, canvas::kVersion);
    if (binding == nullptr) return 1;

    // The provider's object handle comes from its own exported symbol; the
    // interface table never exposes the layout.
    using InstanceFn = const void *(*)(void);
    auto instance = reinterpret_cast<InstanceFn>(lib.resolve("canvas_instance"));
    void *object = const_cast<void *>(instance());

    {
        auto bound = bind_interface(*binding, object);   // acquires the module lease
        if (!bound.valid()) return 1;
        const auto width = bound.ref.method<canvas::WidthFn>(0);
        std::printf("width=%d\n", width(bound.ref.object(), 0));   // width=1920
    }   // bound destructs: releases the lease

    registry.unregister_module(control.id);   // the lease is gone: unload completes
    lib.unload();
    return 0;
}
```

The point: `discover_module` consults the export table for the single symbol `abix_module_query_v1`; while a binding holds a lease the module cannot unload, so the cached `width` pointer can never dangle.

### Example 3 — several implementations of one interface, and versions

Without a shared library, register several providers' `ModuleManifest`s directly:

```cpp
#include "ABIX/Interface/Interface.h"

using namespace skl::abix::poly;

// manifest_v1 / manifest_v2 are each provider's manifest, both implementing
// kInterface; each manifest owns its own method table (construction elided).
ModuleRegistry<> registry;
ModuleControl a;
ModuleControl b;
a.name = "a";
b.name = "b";
registry.register_module(a, manifest_v1);
registry.register_module(b, manifest_v2);

// Every implementation, in registration order; version 0 matches any.
const ModuleInterface *all[4];
const size_t count = registry.find_interface(kInterface, 0, all, 4);   // count == 2

// Select by version: exact, or newest when 0 is requested.
const auto *v1 = registry.find_first(kInterface, 1);        // version 1
const auto *newest = registry.find_first(kInterface, 0);    // newest, i.e. version 2
```

A requested version is exact and `0` selects the newest; a duplicate `(id, version)` within one module is rejected (`duplicate_interface`), different modules may coexist, and queries return all of them without overwriting.

### Example 4 — the owner ABI: create and release across a module

The provider returns an owner handle from its own entry; the destroy entry lives inside that module:

```cpp
// owner_plugin.cpp (excerpt)
skl::abix::owner::handle owner_counter_create(int base) {
    auto *counter = new Counter{base};
    return skl::abix::owner::make_owned(counter, &destroy_counter, /*module_instance*/ 0);
}
```

The consumer adopts the handle and holds a lease, destroying exactly once when it goes out of scope (continues Example 2's `lib` / `control` / `binding`):

```cpp
#include <cstdio>

#include "ABIX/Interface/Owner.h"

using namespace skl::abix::poly;
using AddFn = int (*)(const void *, int);

using CreateFn = skl::abix::owner::handle (*)(int);
auto create = reinterpret_cast<CreateFn>(lib.resolve("owner_counter_create"));
const skl::abix::owner::handle raw = create(40);
if (!skl::abix::owner::valid(raw)) return 1;

{
    // Adopt + lease: while the object lives, the module that must destroy it
    // cannot unload.
    const skl::abix::owner::ref object = skl::abix::owner::ref::adopt(raw, &control);
    if (registry.unregister_module(control.id) == ModuleStatus::busy) { /* expected */ }

    auto bound = bind_interface(*binding, object.object());
    const auto add = bound.ref.method<AddFn>(0);
    std::printf("add(2)=%d\n", add(bound.ref.object(), 2));   // add(2)=42
}   // object destructs: calls the creator's destroy, then drops the lease

registry.unregister_module(control.id);   // now it can unload
```

## API reference

The symbols below come from `ABIX/ABIX.h`. Everything lives in namespace `skl::abix::poly` except the owner ABI (`skl::abix::owner`). To pull in the whole layer on its own, `#include "ABIX/Interface/Interface.h"`.

| Header                            | Contents                                                                                                                 |
| --------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| `ABIX/Interface/Contract.h`       | Interface identity, `InterfaceMethod` / `InterfaceImplementation`, `InterfaceStatus`, `interface_entry`                  |
| `ABIX/Interface/Registry.h`       | `InterfaceRegistry` — binding-time registration and resolution                                                           |
| `ABIX/Interface/Ref.h`            | `InterfaceRef` — resolve once, call natively through a slot cache                                                        |
| `ABIX/Interface/Module.h`         | The fixed entry `abix_module_query_v1`, `ModuleManifest`, `validate_manifest`                                            |
| `ABIX/Interface/Lease.h`          | The `ModuleControl` state machine and the RAII `ModuleLease`                                                             |
| `ABIX/Interface/ModuleRegistry.h` | `ModuleRegistry` — register and query discovered module interfaces                                                       |
| `ABIX/Interface/Discovery.h`      | `discover_module` — load → validate → register                                                                           |
| `ABIX/Interface/Owner.h`          | Owner ABI — borrow/own/copy semantics for `handle` / `ref`                                                               |
| `ABIX/Interface/Proxy.h`          | C++ proxy API — `UniqueInterfaceProxy<Traits>`, `bind`/`call`, signature checks                                          |
| `ABIX/Interface/Declaration.h`    | declaration sugar — `SKL_ABIX_INTERFACE_DECLARE`; version helpers `SKL_ABIX_VERSION_NUMBER` / `SKL_ABIX_VERSION_EXTRACT` |

### `Contract.h` — interface identity and methods

| Symbol                                                   | Type                   | Description                                                                                                                                                                                                                                              |
| -------------------------------------------------------- | ---------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `InterfaceId`                                            | `Hash128`              | Interface identity: the content hash over the interface name, version and ordered method signatures. Not a class name, not a vtable address; derive it with `interface_id(...)`                                                                          |
| `InterfaceMethodHash`                                    | `model::SignatureHash` | One method's ABI signature hash                                                                                                                                                                                                                          |
| `interface_id(name, version, signatures)`                | `InterfaceId`          | Compile-time interface identity from a canonical name, version and slot-ordered signatures; reproduces `AMC::interface_identity`, so neither side needs a hand-written constant. Accepts a `mics::utils::array` or a variadic `InterfaceMethodHash` list |
| `InterfaceMethodKey`                                     | `{signature, slot}`    | A signature paired with its slot, for the `interface_id` `mics::utils::array` overload to sort before hashing                                                                                                                                            |
| `method_signature(name, return_type, parameters, count)` | `InterfaceMethodHash`  | Compile-time method signature from the method name and the return/parameter TypeIDs; reproduces `AMC::signature_hash` (the `[[interface]]` path; parameter names are not hashed). Get TypeIDs from `model::canonical_type_id` (`ABI.h`)                  |
| `interface_entry<Fn>(fn)`                                | `uintptr_t`            | Copies a function pointer into the opaque `entry` slot; generated registrations and callers spell it the same way                                                                                                                                        |
| `to_string(status)`                                      | `const char*`          | Readable name for an `InterfaceStatus`                                                                                                                                                                                                                   |

**Cross-tool identity contract.** `InterfaceId` is defined identically by `AMC::interface_identity` and `ABIX::poly::interface_id`:

```
hash_text("ABIX-INTERFACE:" + canonical_name + ":" + version + slot-ordered signatures,
          domain = 0x494E544552464143)
```

The domain tag is the 8-byte ASCII `"INTERFAC"` — a big-endian truncation of `"INTERFACE"` (the 9th byte, `E`, does not fit a `uint64_t`) — i.e. bytes `49 4E 54 45 52 46 41 43`. It is **mixed into the hash's initial state as an integer** (`lo ^= domain`, `hi += domain`), **not** used as a byte prefix. There is therefore no endianness choice to make; another language must use the same integer and the same mixing, not `reinterpret_cast` the integer's memory.

The seeds are shared with ABIX's own `fnv1a`: `lo` starts from the standard **FNV-1a 64 offset basis** `0xCBF29CE484222325`, `hi` from FNV prime `0x100000001B3` + domain, and the `hi` lane multiplier is XXH64's `PRIME64_2`. `amc-core-test` pins these constants with fixed vectors and checks that both sides agree.

`InterfaceMethod` — one method's adapter entry:

| Field       | Type                  | Description                                                                                             |
| ----------- | --------------------- | ------------------------------------------------------------------------------------------------------- |
| `signature` | `InterfaceMethodHash` | Method ABI signature hash recorded by AMC                                                               |
| `entry`     | `uintptr_t`           | Adapter entry point, called with the object handle as its first argument; `0` means the slot is unbound |
| `slot`      | `uint32_t`            | Stable dispatch slot                                                                                    |
| `flags`     | `uint32_t`            | Reserved                                                                                                |

`InterfaceImplementation` — one concrete type's implementation of an interface:

| Field           | Type                     | Description                                  |
| --------------- | ------------------------ | -------------------------------------------- |
| `interface_id`  | `InterfaceId`            |                                              |
| `concrete_type` | `model::TypeId`          | The concrete type implementing the interface |
| `methods`       | `const InterfaceMethod*` | Provider-owned method table                  |
| `method_count`  | `uint32_t`               | Number of entries                            |
| `version`       | `uint32_t`               | Interface version                            |
| `flags`         | `uint32_t`               | Reserved                                     |

`InterfaceStatus`: `ok`, `invalid_table`, `duplicate_implementation`, `capacity_exceeded`, `interface_not_found`, `version_mismatch`, `method_not_found`, `method_capacity_exceeded`.

### `Registry.h` — binding-time registry

`InterfaceRegistry<Capacity = 64>`: fixed capacity, allocation-free. Adding and resolving are setup work, so a linear scan is fine — it never runs on the call path. `add()` **registers**, `find()` **resolves**.

On a 64-bit target `InterfaceImplementation` is 56 bytes: two 128-bit IDs, the method pointer, `method_count`, `version`, the reserved `flags` and padding. `InterfaceRegistry<8>` is therefore 464 bytes (`8 * 56 + count/lookups`). This is bind-time memory; one shared registry is usually preferable to one per controller. A custom compact record can drop the unused `flags` field (48 bytes per entry) or use a smaller capacity, but that changes the public record layout.

`InterfaceRegistry` is **not internally synchronized**. `add()` must be serialized by the caller and must not race a `find()`. Once the registry is populated and no longer mutated, concurrent `find()` / `find_newest()` / `available()` calls are safe: they only read the entry array, and the diagnostic lookup counter is a relaxed atomic. The atomic counter does not make concurrent `add()` safe; for runtime provider registration use `ModuleRegistry` (which adds module lifetime/lease management) and still serialize its registration calls.

| Method                                            | Return                           | Description                                                                                                                                                                                                                                                                            |
| ------------------------------------------------- | -------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `add(impl)`                                       | `InterfaceStatus`                | Register one implementation. A repeated `(concrete_type, interface_id, version)` returns `duplicate_implementation` (a conflict, not a silent overwrite); a full registry returns `capacity_exceeded`; an empty id or a zero method table with a nonzero count returns `invalid_table` |
| `find(concrete_type, interface_id, version = 0)`  | `const InterfaceImplementation*` | `version == 0` selects the newest registered version, otherwise an exact match; `nullptr` when absent                                                                                                                                                                                  |
| `find_newest(concrete_type, interface_id)`        | `const InterfaceImplementation*` | Newest version, or `nullptr`                                                                                                                                                                                                                                                           |
| `available(concrete_type, interface_id, newest&)` | `bool`                           | Whether any version exists; `newest` receives the newest version — lets a caller distinguish "unknown interface" from "wrong version" without a second full scan                                                                                                                       |
| `size()`                                          | `size_t`                         | Number of registered implementations                                                                                                                                                                                                                                                   |
| `at(index)`                                       | `const InterfaceImplementation*` | Indexed access; `nullptr` out of range                                                                                                                                                                                                                                                 |
| `lookup_count()`                                  | `size_t`                         | Diagnostic: how many `find()` calls were served; proves resolve-then-call performs exactly one lookup                                                                                                                                                                                  |

### `Ref.h` — resolved handle

`InterfaceRef<MaxMethods = 32>`: one resolve copies the bound adapter pointers into a slot-indexed cache; every call afterwards is a plain native indirect call — no per-call lookup, name comparison or type inspection.

| Method                                                                        | Return            | Description                                                                                                                                                                                                  |
| ----------------------------------------------------------------------------- | ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `resolve<RegCap>(registry, object, concrete_type, interface_id, version = 0)` | `InterfaceStatus` | Exactly one registry lookup; `object == nullptr` returns `invalid_table`; on failure distinguishes `version_mismatch` (interface exists, version does not) from `interface_not_found`                        |
| `resolve_methods(interface_id, version, methods, method_count, object)`       | `InterfaceStatus` | Resolve directly from a provider-owned table — the path a dynamically discovered module uses (table from its manifest, caller holds the lease); a slot above `MaxMethods` returns `method_capacity_exceeded` |
| `method<Fn>(slot)`                                                            | `Fn`              | The cached entry point, or `nullptr` when the slot is unbound or out of range. `Fn` takes the object handle as its first argument; pass `object()`                                                           |
| `bound(slot)`                                                                 | `bool`            | Whether the slot is bound                                                                                                                                                                                    |
| `valid()`                                                                     | `bool`            | Resolved and the object is non-null                                                                                                                                                                          |
| `object()`                                                                    | `void*`           | The provider's object handle (the first argument of a call)                                                                                                                                                  |
| `interface_id()` / `version()`                                                |                   | Resolved interface identity / version                                                                                                                                                                        |
| `method_count()`                                                              | `uint32_t`        | Slots spanned by the implementation (highest bound slot + 1)                                                                                                                                                 |

`query_interface<MaxMethods = 32, RegCap>(registry, object, concrete_type, interface_id, version = 0)` — convenience that returns an `InterfaceRef` by value; check `valid()`.

`InterfaceSlotCache<MaxMethods>` is the compact storage used by `UniqueInterfaceProxy`. It keeps only the object handle, the highest bound slot and the adapter entries; expected signatures are verified during resolve and then discarded. Its size is roughly `16 + 8 * MaxMethods` bytes instead of `InterfaceRef`'s `32 + 24 * MaxMethods`. Use `InterfaceRef` when you need `interface_id()` / `version()` / `signature()` diagnostics or a standalone raw handle; use a proxy for the per-object path.

`InterfaceSlotTable<MaxMethods>` is the shareable form of that cache: build it once with `build()` / `make_shared_slot_table<Traits>()` and pass it to any number of `SharedInterfaceProxy` objects. The table itself is roughly `8 * MaxMethods + 8` bytes.

The provider's object must outlive the handle and must be the object the adapters were generated for (the handle creator owns the correct pointer and any `this` adjustment).

### `Module.h` — the discovery manifest

| Symbol                                                       | Type                              | Description                                                                                                  |
| ------------------------------------------------------------ | --------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| `ModuleInstanceId`                                           | `uint64_t`                        | Unique per load; the same file loaded twice, or reloaded, gets a new id; `0` is invalid                      |
| `invalid_module_instance`                                    | constant                          |                                                                                                              |
| `abix_manifest_version`                                      | `uint32_t`                        | Version of the manifest ABI itself; a consumer that does not know a version must refuse it rather than guess |
| `abix_module_query_symbol`                                   | `const char*`                     | The fixed entry name `"abix_module_query_v1"`                                                                |
| `abix_max_manifest_interfaces` / `abix_max_manifest_methods` | constants                         | Sanity bounds so a malformed manifest cannot drive an unbounded loop                                         |
| `ModuleQueryFn`                                              | `const ModuleManifest* (*)(void)` | The fixed entry signature                                                                                    |
| `SKL_ABIX_DEFINE_MODULE_QUERY(manifest)`                     | macro                             | Defines the `extern "C"` `abix_module_query_v1`                                                              |
| `ManifestStatus`                                             | enum                              | Result of `validate_manifest`; `to_string` gives the name                                                    |
| `validate_manifest(const ModuleManifest*)`                   | `ManifestStatus`                  | Validates a manifest                                                                                         |

`InterfaceManifestEntry` (fixed 40-byte wire layout):

| Field               | Type                     | Description                                 |
| ------------------- | ------------------------ | ------------------------------------------- |
| `interface_id`      | `InterfaceId`            |                                             |
| `interface_version` | `uint32_t`               | Distinguishes compatible contract revisions |
| `flags`             | `uint32_t`               | Reserved                                    |
| `methods`           | `const InterfaceMethod*` | Provider-owned adapter table                |
| `method_count`      | `uint32_t`               | Number of entries                           |
| `reserved`          | `uint32_t`               |                                             |

`ModuleManifest` (40 bytes): `manifest_version`, `module_abi_version`, `flags`, `interface_count`, `interfaces`, `extension_count`, `reserved`, `extensions`. `extensions` is an opaque, optional, forward-compatible region: a consumer that does not know it skips it, but must still validate its declared boundary.

`ManifestStatus`: `ok`, `null_manifest`, `unsupported_version`, `invalid_interface`, `invalid_binding`, `too_many_interfaces`, `too_many_methods`.

`validate_manifest` rejects, **without dereferencing arbitrary memory**: a null manifest, an unknown `manifest_version`, an `interface_count` / `method_count` above the bounds, a zero `interface_id`, a null method table when it claims methods, or a method table with a duplicate slot. A bound slot with `entry == 0` is allowed (an unbound slot). It cannot prove a pointer is safe in general.

### `Lease.h` — module lifecycle and leases

The lifecycle is a small state machine with a lease count:

```text
loading ──activate──▶ active ──begin_unload──▶ draining ──idle──▶ unloaded
```

| Symbol                                        | Type               | Description                                                                                                                                                                                                        |
| --------------------------------------------- | ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `ModuleState`                                 | enum               | `loading` / `active` / `draining` / `unloaded`; `to_string` gives the name                                                                                                                                         |
| `ModuleControl`                               | struct             | The loader-owned lifecycle token: `id`, `name`, `state`, `leases`, plus the platform `module_handle` and the `pin`/`unpin` hooks. Kept outside the registry so the loader can tie it to the platform module handle |
| `next_module_instance_id()`                   | `ModuleInstanceId` | Monotonic; never reused                                                                                                                                                                                            |
| `module_state(control)`                       | `ModuleState`      | Reads the current state                                                                                                                                                                                            |
| `activate_module(control)`                    | `bool`             | `loading → active` (CAS); `false` when not loading                                                                                                                                                                 |
| `try_acquire_lease(control)`                  | `bool`             | Acquires only while `active`; the state is re-checked after the increment so a concurrent `begin_unload` cannot be missed. Takes an OS-level reference (`pin`) so the module stays mapped while the lease is held  |
| `release_lease(control)`                      | `void`             | Releases one lease                                                                                                                                                                                                 |
| `begin_unload(control)`                       | `bool`             | Stops admitting new leases (enters `draining`); `true` if already draining/unloaded, `false` if never activated                                                                                                    |
| `wait_for_idle(control, max_spins = 1000000)` | `bool`             | Waits until every lease is released; bounded so a leaked lease cannot hang a caller forever                                                                                                                        |
| `mark_unloaded(control)`                      | `void`             | Sets `unloaded`                                                                                                                                                                                                    |
| `ModuleLease`                                 | RAII class         | Movable, non-copyable; `valid()`, `release()`; the destructor releases; releasing an unheld lease is a no-op                                                                                                       |
| `acquire_lease(control)`                      | `ModuleLease`      | An invalid lease (never a partial one) when the module is not `active`                                                                                                                                             |

A lease is held for the duration of an active call or a live object handle. This is what makes a cached method binding safe: the binding stays valid exactly as long as the caller holds a lease.

**OS-level pinning.** When the loader binds the platform identity (`Discovery.h` → `bind_module_platform`), the first lease calls `pin` and the last calls `unpin`: on Windows `pin` resolves the module base address with `GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)` and `unpin` calls `FreeLibrary`; on POSIX `pin` re-opens the module by its load path (`dlopen(path)`, which increments the reference count without re-running initializers) and `unpin` calls `dlclose`. A held lease therefore **directly blocks** `FreeLibrary`/`dlclose` at the OS level, not only through the state machine — an owner that unloads while a lease is held cannot map the module out. The reference is taken once across all leases and needs the loader-owned `module_path`, so the loader must keep the `Object` (and its path) alive while leases are held.

### `ModuleRegistry.h` — dynamically discovered module interfaces

`ModuleInterface` — one discovered module interface: `module`, `interface_id`, `version`, `methods`, `method_count`, `control` (the lifetime token a caller must lease before calling).

`ModuleStatus`: `ok`, `already_registered`, `invalid_manifest`, `invalid_state`, `capacity_exceeded`, `interface_capacity_exceeded`, `duplicate_interface`, `not_registered`, `busy`.

`ModuleRegistry<ModuleCapacity = 16, InterfaceCapacity = 128>`:

| Method                                  | Return                   | Description                                                                                                                                                                                                                                |
| --------------------------------------- | ------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `register_module(control, manifest)`    | `ModuleStatus`           | Validates the manifest, rejects a duplicate `(id, version)` within the module, activates and registers; re-registering the same control is idempotent (`already_registered`); a `control` not in the loading state returns `invalid_state` |
| `unregister_module(id)`                 | `ModuleStatus`           | Begins unloading: `begin_unload`, then removes only once every lease is released; otherwise the module is draining (no new leases) and the caller retries after in-flight calls — reported as `busy`                                       |
| `registered(id)`                        | `bool`                   |                                                                                                                                                                                                                                            |
| `module_count()` / `interface_count()`  | `size_t`                 |                                                                                                                                                                                                                                            |
| `interface_at(index)`                   | `const ModuleInterface*` | Indexed access                                                                                                                                                                                                                             |
| `find_interface(id, version, out, max)` | `size_t`                 | Writes **all** implementations in registration order (`version == 0` matches any); returns the count written (up to `max`)                                                                                                                 |
| `find_first(id, version = 0)`           | `const ModuleInterface*` | The newest implementation, or `nullptr`                                                                                                                                                                                                    |

Several modules may implement one interface; queries return all of them and never overwrite one with another; a version is exact when requested and "newest" when `0` is requested.

`BoundInterface<MaxMethods = 32>` — a resolved binding plus the lease that keeps its module loaded; `valid()` requires both `ref.valid()` and `lease.valid()`. Calling through `ref` is only valid while this object is alive. `bind_interface<MaxMethods = 32>(binding, object)` — acquires the lease and resolves; returns an invalid binding (no lease, unresolved ref) when the module is no longer active.

### `Discovery.h` — load and discover

| Symbol                                                                                     | Type              | Description                                                                                                         |
| ------------------------------------------------------------------------------------------ | ----------------- | ------------------------------------------------------------------------------------------------------------------- |
| `DiscoveryResult`                                                                          | struct            | `manifest` (`ManifestStatus`), `registration` (`ModuleStatus`), `queried` (the manifest pointer), `ok()`            |
| `discover_module<ModuleCapacity = 16, InterfaceCapacity = 128>(module, control, registry)` | `DiscoveryResult` | Resolves `abix_module_query_v1` from a loaded module, validates it with `validate_manifest`, then `register_module` |

The platform export table is consulted for **that one symbol only**: no other export is decoded and no interface meaning is inferred from a symbol. A missing entry, an unknown manifest version, a malformed manifest or a duplicate declaration all fail explicitly. On success `control` becomes `active`.

### `Owner.h` — the owner ABI

Namespace `skl::abix::owner`. Interface ABI says how to call an object; the owner ABI says who owns it, who releases it, and where the releasing code lives.

| Symbol                                                    | Type              | Description                                                         |
| --------------------------------------------------------- | ----------------- | ------------------------------------------------------------------- |
| `abix_owner_abi_version`                                  | `uint32_t`        | Owner ABI version                                                   |
| `mode`                                                    | enum              | `borrowed` / `owned` / `copied`; `to_string` gives the name         |
| `destroy_fn`                                              | `void (*)(void*)` | The creator's destroy entry, with a fixed C ABI                     |
| `handle`                                                  | struct (32 bytes) | POD handle: `object`, `mode`, `flags`, `destroy`, `module_instance` |
| `valid(handle)`                                           | `bool`            | `object != nullptr`                                                 |
| `make_borrowed(object)`                                   | `handle`          | Non-owning reference                                                |
| `make_owned(object, destroy, module_instance = invalid)`  | `handle`          | Exclusive ownership, released through `destroy`                     |
| `make_copied(object, destroy, module_instance = invalid)` | `handle`          | Independent copy, released through `destroy`                        |

`handle` fields: `object` (opaque to the consumer), `mode`, `flags`, `destroy` (required for owned, optional for copied), `module_instance` (diagnostic: which load created it).

`ref` — the runtime owner handle: the creator's POD `handle` plus a module lease that keeps the destroying module loaded for as long as the object lives.

| Method                              | Return          | Description                                                                                                                                                                                     |
| ----------------------------------- | --------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `adopt(handle, control = nullptr)`  | `ref`           | Adopts a creator-returned handle; for an owned/copied handle pass the creating module's `control` to hold the lease; a borrowed handle may omit it                                              |
| `borrow(object, control = nullptr)` | `ref`           | Convenience: `adopt(make_borrowed(object), control)`                                                                                                                                            |
| `valid()`                           | `bool`          | Object is non-null                                                                                                                                                                              |
| `object()`                          | `void*`         | The object pointer                                                                                                                                                                              |
| `ownership()`                       | `mode`          | Ownership mode                                                                                                                                                                                  |
| `owns()`                            | `bool`          | `owned` or `copied`                                                                                                                                                                             |
| `leased()`                          | `bool`          | Whether a module lease is held                                                                                                                                                                  |
| `raw()`                             | `const handle&` | The underlying POD handle                                                                                                                                                                       |
| `release()`                         | `void`          | Releases exactly once: for owned/copied it calls the creator's `destroy`, then drops the module lease; a borrowed handle is not destroyed. The handle is cleared, so releasing twice is a no-op |

A failed creation returns an invalid handle (`object == nullptr`) rather than a partial one; `destroy` returns `void` — a release cannot fail, so any failure must be reported at creation time. Moving the handle transfers ownership.

### `Proxy.h` — the C++ proxy API

An interface proxy is a lightweight handle over "an object handle plus resolved adapters". It reads like a C++ `Shape*`, but it is not a base class — the concrete type is never modified and no compiler vtable is used. `UniqueInterfaceProxy<Traits>` holds the shared machinery; a per-interface class adds only its methods, and `Traits` supplies the interface difference:

| `Traits` member | Description                                                                                                                                           |
| --------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| `id`            | The `InterfaceId` (computed by AMC)                                                                                                                   |
| `version`       | The interface version                                                                                                                                 |
| `signatures`    | The `mics::utils::array` of expected signatures, one per slot; its size is also the proxy's slot capacity. A zero hash means "do not check this slot" |
| `slots`         | A typed zero-based slot table: `Traits::slots.<method>` is the slot value used by the proxy's `call(...)`                                             |

`UniqueInterfaceProxy<Traits>`:

| Method                                                             | Return            | Description                                                                                                                                       |
| ------------------------------------------------------------------ | ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| `bind(registry, object, concrete_type, version = Traits::version)` | `InterfaceStatus` | Binds, verifies provider signatures against `Traits::signatures`, then discards the signatures; failure is **explicit** and retained in the proxy |
| `bind_methods(interface_id, version, methods, count, object)`      | `InterfaceStatus` | Binds directly to a provider-owned table (dynamic discovery); the caller keeps the module lease and the id/version must match the `Traits`        |
| `status()` / `valid()`                                             |                   | Binding status / usable (`ok` and resolved)                                                                                                       |
| `object()` / `interface_id()` / `version()` / `method_count()`     |                   | Object handle and contract facts (invalid proxy reports an empty id / zero version)                                                               |
| `verify(slot)`                                                     | `InterfaceStatus` | After binding, checks that the slot is bound; the signature comparison already happened in `bind()`                                               |
| `call<Fn>(slot, args...)` (protected)                              | the `Fn` result   | Directly call a cached entry (the object handle is supplied first); precondition: `valid()` and the slot is bound                                 |
| `call_checked<Fn>(out, slot, args...)` (protected)                 | `InterfaceStatus` | Status and result are separate: an unbound slot is `method_not_found`, so a legitimate result is never read as a failure                          |

The proxy stores only the compact `InterfaceSlotCache`: object handle, highest bound slot and one pointer per slot. Provider signatures, interface id and version are not duplicated per proxy; they remain in `Traits` and are checked during binding.

#### Exclusive vs shared storage

There are two proxy layouts:

| Layout | Per-object size | Extra storage | Best for |
|--------|----------------:|---------------|----------|
| `UniqueInterfaceProxy<Traits>` | `24 + 8 * N` | none | One/few objects; per-object method tables; no shared-table lifetime |
| `SharedInterfaceProxy<Traits>` | `24` | `InterfaceSlotTable`: `8 * N + 8` per implementation | Many objects of the same implementation; a table built once and shared read-only |

**Performance is not the deciding factor.** Both layouts measure at about 0.75 ns for the method call in the latency benchmark above; the extra table load in shared mode is hidden by the indirect call. Choose based on ownership of the adapter table, binding flexibility and total memory.

##### Unique advantages

- **Self-contained**: the proxy stores its own slot entries. There is no extra object to keep alive and no risk of a dangling `InterfaceSlotTable`.
- **Per-object binding**: each proxy can use a different provider method table, even for the same `(concrete_type, interface_id, version)`. This matters for multiple module instances, hot reload, or provider instances with different adapter tables.
- **Simplest setup**: `Shape shape(registry, &object, concrete_id)` is the whole binding; no table build step or table lifetime contract.
- **Smallest total for a single object**: the shared layout adds the `8 * N + 8` table, so one object is usually smaller with unique storage.

##### Shared advantages

- **24-byte objects regardless of method count**: the table grows with `N`, not with the number of objects.
- **One table per implementation**: adapter entries can be placed in static/read-only storage and reused by any number of objects.
- **Better memory density for collections**: controllers, devices, nodes and other many-instance objects benefit most.

For `K` objects and `N` methods:

```text
Unique total = K * (24 + 8N)
Shared total = K * 24 + (8N + 8)
```

Shared wins as soon as the table is amortized. For `N >= 2`, that is already true at `K = 2`; for `N = 1`, it ties at `K = 2` and wins from `K = 3`. Unique is the better choice only for a single object, a per-instance adapter table, or when the table's lifetime cannot be managed safely.

`SharedInterfaceProxy` still verifies signatures when the shared table is built, but the table keeps only adapter entries. Build it once and keep it alive for every proxy that points at it; after `build()` it is read-only and safe to share.

Use `SKL_ABIX_INTERFACE_DECLARE_BOTH` to generate both proxy classes (`Shape` and `SharedShape`), or `SKL_ABIX_INTERFACE_DECLARE_SHARED` for only the shared class:

```cpp
SKL_ABIX_INTERFACE_DECLARE_BOTH(Shape, SHAPE_METHODS, 1);

// One table per (concrete type, interface, version).
InterfaceSlotTable<ShapeTraits::signatures.size()> table;
if (make_shared_slot_table<ShapeTraits>(table, registry, box_id) != InterfaceStatus::ok)
    return 1;

Box first{3, 4};
Box second{5, 6};

Shape exclusive(registry, &first, box_id);   // self-contained entries
SharedShape shared_a(table, &first);         // object + shared table
SharedShape shared_b(table, &second);        // same table, different object
```

`SharedShape` is 24 bytes regardless of method count; `InterfaceSlotTable<N>` is roughly `8 * N + 8` bytes per implementation. Rebuild the table only when the provider's adapter table changes (and keep the module lease alive if it comes from a discovered module).

A hand-written `Traits` can compute the signatures and the identity too (the helpers reproduce `AMC::signature_hash` / `AMC::interface_identity`; `amc-core-test` checks them against AMC):

```cpp
struct ShapeTraits {
    // AMC's int32_t TypeID is an ABI key, not the C++ spelling.
    static constexpr model::TypeId i32 = model::canonical_type_id("prim:i32");
    // The method signature is generated too; the return/parameter TypeIDs must
    // match the provider.
    static constexpr skl::mics::utils::array sigs = {
        method_signature("area", i32, nullptr, 0),
        method_signature("size", i32, nullptr, 0),
    };
    static constexpr InterfaceId id = interface_id("Shape", 1, sigs);
    static constexpr uint32_t version = 1;
    struct alignas(8) Slots {
        uint32_t area;
        uint32_t size;
    };
    alignas(8) static constexpr Slots slots = skl::mics::utils::make_iota<Slots, sigs.size(), 0>();
};
```

`method_signature` / `canonical_type_id` only solve the last hop: their inputs — the method name and the return/parameter TypeIDs — are still AMC-derived. A primitive TypeID is an ABI key (`int32_t` → `prim:i32`, which varies with the target's width and signedness) that the preprocessor cannot infer from a C++ spelling. Interfaces with parameters, overloads or complex signatures should still have AMC generate the signature table; the helpers are for simple contracts where both sides agree.

#### Registering `signatures` — TypeIDs for basic and custom types

`signatures` is indexed by slot, so write the entries in slot order. Each entry is compared with the provider's `InterfaceMethod.signature`, and `interface_id` hashes the same values; a mismatch is reported as `signature_mismatch`, never called silently.

`method_signature(name, return_type, parameters, count)` reproduces `AMC::signature_hash`:

```
name + decimal(return_type.lo) + decimal(return_type.hi)
     + for each parameter: decimal(type.lo) + decimal(type.hi)
```

with domain `0x5349474E41545552` (`"SIGNATUR"`). Parameter names are not hashed. `name` and `parameters` are those of the **contract function** AMC recorded, not an arbitrary label. If that function takes the object handle first (the usual adapter convention), the handle is parameter 0 and belongs in `parameters`; a free function has no handle. AMC already emits the exact `signatures` array in the generated header — copy it when in doubt.

The parameter list can be passed in three equivalent, allocation-free forms:

```cpp
constexpr TypeId params[] = {i32, i32};
method_signature("resize", v, params, 2);                 // pointer + count
method_signature("resize", v, mics::utils::array{i32, i32});     // inline fixed array
method_signature("resize", v, i32, i32);                  // variadic
```

`mics::utils::array` is the minimal fixed-size type in `mics/utils/array.h`; its deduction guide makes `mics::utils::array{i32, i32}` name its own type. All three forms hash the same bytes and allocate nothing.

A signature mentions **TypeIDs**, not C++ types. For name-based identities, `model::canonical_type_id(canonical_name)` reproduces AMC's value:

| C++ type               | canonical name                | Notes                                                                     |
| ---------------------- | ----------------------------- | ------------------------------------------------------------------------- |
| `int` / `int32_t`      | `prim:i32`                    | integer keys are ABI-derived **bit** widths; ABI-equal spellings collapse |
| `uint64_t`             | `prim:u64`                    |                                                                           |
| `double`               | `prim:fieee64`                | float keys keep the format (`ieee`, `bfloat`, `x87`, `ppc`)               |
| `bool`                 | `prim:bool`                   |                                                                           |
| `void`                 | `prim:void`                   | return position only                                                      |
| `char`                 | `prim:char_s` / `prim:char_u` | signedness is target-defined                                              |
| `cv::Point`            | `cv::Point`                   | qualified record name                                                     |
| `std::errc`            | `std::errc`                   | enum; `[[expose]]` rejects raw `std::*` parameters (see below)            |
| `std::string`          | `abix_str_utf8`               | the `[[adapt]]` target; `std::string` itself is rejected                  |
| `std::vector<int32_t>` | `abix_span_i32`               | via `[[adapt]]` / `[[instantiate]]`                                       |
| `T*` / `const T*`      | Clang's pointer spelling      | not stable to hand-write; read it from `amc query` / `amc-dump`           |

**Custom value records — `cv::Point`.** A plain value record is representable; its TypeID is the qualified name:

```cpp
constexpr model::TypeId void_id = model::canonical_type_id("prim:void");
constexpr model::TypeId point   = model::canonical_type_id("cv::Point");
constexpr model::TypeId move_params[] = {point};
method_signature("move", void_id, move_params, 1);   // move(cv::Point)
```

If the contract passes `cv::Point*`, use the pointer's TypeID, not `cv::Point`'s; get that value from `amc query` / `amc-dump` rather than guessing Clang's spelling.

**Strings — `std::string`.** An STL type must not cross the boundary raw. Declare the mapping:

```toml
[[adapt]]
native = "std::string"
target = "abix_str_utf8"
```

The thunk then takes `abix_str_utf8` (data + length + encoding), and the signature uses that boundary TypeID:

```cpp
constexpr model::TypeId i32  = model::canonical_type_id("prim:i32");
constexpr model::TypeId text = model::canonical_type_id("abix_str_utf8");
constexpr model::TypeId open_params[] = {text};
method_signature("label", text, nullptr, 0);       // returns a borrowed view
method_signature("open", i32, open_params, 1);     // open(abix_str_utf8) -> status
```

The adapter is generated and linked; it converts the boundary value to `std::string` / `std::string_view`. Only parameters are adapted. A returned view follows the borrow/own/copy ownership contract; a value the consumer keeps must be copied or owned.

**Enums — `std::errc`.** An enum has a TypeID (`canonical_type_id("std::errc")`), but AMC's `[[expose]]` representability check rejects a raw `std::*` parameter. For an error status, pass the underlying integer (`prim:i32`) and describe it with `[[error]]` (`status`, `success`, `value`), or return an `abix_result_*` record; the error boundary carries `Result<T, E>` without exposing the enum layout. A hand-written provider/consumer pair may use the enum TypeID directly if both sides agree, but it will not match an AMC-generated `[[expose]]` thunk.

**Complete example.** `draw()`, `move(cv::Point)`, `label() -> abix_str_utf8`, and `open(abix_str_utf8) -> int32_t` (a `std::errc` code):

```cpp
constexpr model::TypeId i32   = model::canonical_type_id("prim:i32");
constexpr model::TypeId v     = model::canonical_type_id("prim:void");
constexpr model::TypeId point = model::canonical_type_id("cv::Point");
constexpr model::TypeId text  = model::canonical_type_id("abix_str_utf8");
constexpr model::TypeId move_params[] = {point};
constexpr model::TypeId open_params[] = {text};

static constexpr mics::utils::array sigs = {
    method_signature("draw",  v,    nullptr,     0),
    method_signature("move",  v,    move_params, 1),
    method_signature("label", text, nullptr,     0),
    method_signature("open",  i32,  open_params, 1),
};
static constexpr InterfaceId id = interface_id("Canvas", 1, sigs);
```

If the provider is AMC-generated and its contract functions take the object handle first, prepend the handle TypeID to each parameter list (or simply copy the generated `signatures` array).

**Getting the exact value.** For pointers, typedefs, template aliases and nested records, do not guess the canonical spelling. Read it from the artifact:

```sh
amc query module.abix --type cv::Point     # id=0X... name=cv::Point
amc-dump module.abix                       # full section dump
```

`amc-core-test` pins `method_signature` against `AMC::signature_hash` and `canonical_type_id` against `AMC::hash_text`, so the helpers cannot drift from the generator.

`amc generate -l cpp` emits the equivalent `Traits` for a `[[interface]]`, and — when **every** method takes the object handle first — an `UniqueInterfaceProxy`-derived class as well (otherwise only `Traits`, with a note explaining why).

#### Three ways to do it

The same task — measure three shapes' areas and call them polymorphically — written three ways, all printing `area=25`, `area=24` and `area=12`.

Left: C++ virtual functions; middle: ABIX manual registration (a hand-written adapter + `InterfaceRegistry` + `InterfaceRef`); right: an ABIX proxy (`Traits` + `UniqueInterfaceProxy`).

<table>
<tr>
<th align="left">C++ virtual functions</th>
<th align="left">ABIX: manual registration</th>
<th align="left">ABIX: proxy (Proxy.h)</th>
</tr>
<tr>
<td valign="top" markdown="1">

```cpp
#include <cstdio>

struct Shape {
    virtual ~Shape() = default;
    virtual int area() const = 0;
};
struct Square : Shape {
    explicit Square(int s) : side(s) {}
    int area() const override { return side * side; }
    int side;
};
struct Rectangle : Shape {
    Rectangle(int w, int h) : width(w), height(h) {}
    int area() const override { return width * height; }
    int width, height;
};
struct Triangle : Shape {
    Triangle(int b, int h) : base(b), height(h) {}
    int area() const override { return base * height / 2; }
    int base, height;
};
int main() {
    Square square{5};
    Rectangle rectangle{4, 6};
    Triangle triangle{6, 4};
    const Shape *shapes[] = {&square, &rectangle, &triangle};
    for (const Shape *shape : shapes) std::printf("area=%d\n", shape->area());
    return 0;
}
```

</td>
<td valign="top" markdown="1">

```cpp
#include <cstdio>
#include "ABIX/Interface/Interface.h"

using namespace skl::abix::poly;
using skl::abix::model::TypeId;

constexpr TypeId i32 = skl::abix::model::canonical_type_id("prim:i32");
constexpr auto area_sig = method_signature("area", i32, nullptr, 0);
constexpr InterfaceId kShape = interface_id("Shape", 1, area_sig);
constexpr TypeId square_id{0x11, 0x00};
constexpr TypeId rectangle_id{0x12, 0x00};
constexpr TypeId triangle_id{0x13, 0x00};
using AreaFn = int (*)(const void *);

struct Square {
    int side;
};
struct Rectangle {
    int width;
    int height;
};
struct Triangle {
    int base;
    int height;
};

int square_area(const void *self) {
    const auto *s = static_cast<const Square *>(self);
    return s->side * s->side;
}
int rectangle_area(const void *self) {
    const auto *r = static_cast<const Rectangle *>(self);
    return r->width * r->height;
}
int triangle_area(const void *self) {
    const auto *t = static_cast<const Triangle *>(self);
    return t->base * t->height / 2;
}

int main() {
    const InterfaceMethod square_methods[] = {{area_sig, interface_entry(&square_area), 0, 0}};
    const InterfaceMethod rectangle_methods[] = {{area_sig, interface_entry(&rectangle_area), 0, 0}};
    const InterfaceMethod triangle_methods[] = {{area_sig, interface_entry(&triangle_area), 0, 0}};

    InterfaceRegistry<4> registry;
    if (registry.add({kShape, square_id, square_methods, 1, 1, 0}) != InterfaceStatus::ok) return 1;
    if (registry.add({kShape, rectangle_id, rectangle_methods, 1, 1, 0}) != InterfaceStatus::ok) return 1;
    if (registry.add({kShape, triangle_id, triangle_methods, 1, 1, 0}) != InterfaceStatus::ok) return 1;

    Square square{5};
    Rectangle rectangle{4, 6};
    Triangle triangle{6, 4};
    struct Case {
        void *object;
        TypeId type;
    };
    const Case cases[] = {{&square, square_id}, {&rectangle, rectangle_id}, {&triangle, triangle_id}};
    for (const Case &c : cases) {
        InterfaceRef<2> ref;
        if (ref.resolve(registry, c.object, c.type, kShape, 1) != InterfaceStatus::ok) return 1;
        const auto area = ref.method<AreaFn>(0);
        std::printf("area=%d\n", area(ref.object()));
    }
    return 0;
}
```

</td>
<td valign="top" markdown="1">

```cpp
// contract, adapters, method tables and registration as in the middle column
struct ShapeTraits {
    static constexpr skl::mics::utils::array signatures = {area_sig};
    static constexpr InterfaceId id = interface_id("Shape", 1, signatures);
    static constexpr uint32_t version = 1;
    struct alignas(8) Slots {
        uint32_t area;
    };
    alignas(8) static constexpr Slots slots = skl::mics::utils::make_iota<Slots, signatures.size(), 0>();
};
class Shape : public UniqueInterfaceProxy<ShapeTraits> {
    using Base = UniqueInterfaceProxy<ShapeTraits>;

public:
    using Base::Base;
    int area() const noexcept { return this->template call<int (*)(const void *)>(ShapeTraits::slots.area); }
    InterfaceStatus area(int &out) const noexcept {
        return this->template call_checked<int (*)(const void *)>(out, ShapeTraits::slots.area);
    }
};
int main() {
    // registry / square / rectangle / triangle as in the middle column
    struct Case {
        void *object;
        TypeId type;
    };
    const Case cases[] = {{&square, square_id}, {&rectangle, rectangle_id}, {&triangle, triangle_id}};
    for (const Case &c : cases) {
        Shape shape(registry, c.object, c.type);
        std::printf("area=%d\n", shape.valid() ? shape.area() : -1);
    }
    return 0;
}
```

</td>
</tr>
</table>

Comparing the three:

- **C++ virtual function** encodes polymorphism in the **type system**: the implementation must inherit `Shape`, and dispatch goes through a compiler-generated vtable.
- **ABIX manual registration** is the lowest-level explicit path (write an adapter → register → resolve → call), with no extra abstraction; the adapter's first parameter is the object handle.
- **ABIX proxy** packages binding / status / calls into one type: the concrete type stays a plain `struct` (no inheritance, no virtual function), and after a single bind `shape.area()` is a plain native indirect call. The `Traits` can be written by hand, or generated by `amc generate -l cpp`.

### `Declaration.h` — macro declarations and registration

`Declaration.h` is optional macro sugar. It does not add a runtime mechanism; it generates the pieces that would otherwise be written by hand:

- the `InterfaceMethodHash` signature table `signatures`
- the interface identity `id`
- the zero-based slot table `Slots` / `slots`
- the `Traits` struct
- the `UniqueInterfaceProxy<Traits>`-derived proxy class and its typed forwarding methods

The generated `Traits::id` / `signatures` / `slots` are registered and bound through the ordinary `InterfaceRegistry` (or `ModuleRegistry` + `bind_methods`), exactly like a hand-written `Traits`. The macro only removes declaration boilerplate; it does not perform runtime registration.

> The macro sees the method name, the C++ return type, the return TypeID and the parameter TypeIDs, but not the C++ parameter types. It is intended for simple value-parameter contracts. Complex signatures, overloads, reference/record parameters and `void` returns should still be generated by `amc generate -l cpp` or written as a hand-written proxy.

#### 1. The method list

Define an `X` macro that takes exactly one argument:

```cpp
constexpr skl::abix::model::TypeId i32 =
    skl::abix::model::canonical_type_id("prim:i32");
constexpr skl::abix::model::TypeId v =
    skl::abix::model::canonical_type_id("prim:void");

#define SHAPE_METHODS(X)       \
    X(area, int32_t, i32, {}) \
    X(resize, int32_t, i32, {i32, i32})
```

Each entry has the form:

```text
X(method_name, cxx_return_type, return_type_id, {parameter_type_ids...})
```

| Field             | Meaning                                                                                              |
| ----------------- | ---------------------------------------------------------------------------------------------------- |
| `method_name`     | Generated proxy method name and the field name in `Slots`                                            |
| `cxx_return_type` | C++ return type of the proxy method, e.g. `int32_t`                                                  |
| `return_type_id`  | `model::TypeId` used by `method_signature`; must match the provider / AMC                            |
| `{...}`           | Parameter TypeIDs used in the signature; `{}` means no parameters. The order must match the contract |

Notes:

- The **position in the list** determines the slot: the first entry is slot 0, the second is slot 1, and so on.
- Elements in `{...}` are `model::TypeId`s, not C++ type names. When the provider is AMC-generated, copy the generated `signatures` / TypeID spelling.
- The list is expanded several times by `TRAITS`, `SLOTS` and `DATA`, so it must contain only declaration entries, never runtime side effects.

#### 2. One-line Traits and proxy

```cpp
#include "ABIX/Interface/Declaration.h"

SKL_ABIX_INTERFACE_DECLARE(UNIQUE, Shape, SHAPE_METHODS, 1);
```

`SKL_ABIX_INTERFACE_DECLARE(storage, type, list, version, ...)` is equivalent to:

```cpp
SKL_ABIX_INTERFACE_TRAITS(type, list, version, __VA_ARGS__);
SKL_ABIX_INTERFACE_##storage##_DATA(type, list, __VA_ARGS__);
```

`storage` is `UNIQUE` or `SHARED`. `UNIQUE` generates the inline `type` proxy; `SHARED` generates `Shared##type`. `SKL_ABIX_INTERFACE_DECLARE_SHARED` and `SKL_ABIX_INTERFACE_DECLARE_BOTH` are convenience spellings; `_BOTH` emits both the inline proxy and the shared proxy.

The optional fourth and later arguments are **type attributes / export attributes** inserted verbatim before the generated `typeTraits` and `type` names:

```cpp
SKL_ABIX_INTERFACE_DECLARE(UNIQUE, Shape, SHAPE_METHODS, 1, SHAPE_API);
// expands to:
// struct SHAPE_API ShapeTraits { ... };
// struct SHAPE_API Shape : public UniqueInterfaceProxy<ShapeTraits> { ... };
```

Without attributes, the original form is unchanged:

```cpp
SKL_ABIX_INTERFACE_DECLARE(UNIQUE, Shape, SHAPE_METHODS, 1);
```

Its expansion is, in essence:

```cpp
struct Shape;

struct ShapeTraits {
    static constexpr auto signatures = skl::mics::utils::array{
        skl::abix::poly::method_signature("area", i32, skl::mics::utils::array{}),
        skl::abix::poly::method_signature("resize", i32, skl::mics::utils::array{i32, i32}),
    };
    static constexpr auto version = 1;
    static constexpr auto id = skl::abix::poly::interface_id("Shape", version, signatures);

    struct alignas(8) Slots {
        uint32_t area;
        uint32_t resize;
    };
    alignas(8) static constexpr Slots slots =
        skl::mics::utils::make_iota<Slots, signatures.size(), 0>();
};

struct Shape : public skl::abix::poly::UniqueInterfaceProxy<ShapeTraits> {
    using UniqueInterfaceProxy<ShapeTraits>::UniqueInterfaceProxy;
    using Traits = ShapeTraits;

    // two overloads per method, see below
};
```

Important details:

- `id` is computed as `interface_id(#type, version, signatures)`. `#type` is the stringized macro argument, so invoke the macro inside the target namespace with the **unqualified** name `Shape`. Writing `SKL_ABIX_INTERFACE_DECLARE(UNIQUE, ns::Shape, ...)` makes the interface name `"ns::Shape"`, which is usually not what AMC records.
- The order of `signatures` is the slot order: `slots.area == 0`, `slots.resize == 1`.
- Renaming a method, reordering the list or changing the version changes `signatures` / `id` and is an ABI break. Do not reorder the list for cosmetic reasons.
- `SKL_ABIX_INTERFACE_DECLARE(...)` needs a trailing semicolon: the generated struct definition does not carry one.

**Windows DLL import/export attributes**

The final variadic parameter of `SKL_ABIX_INTERFACE_DECLARE`, `SKL_ABIX_INTERFACE_TRAITS` and `SKL_ABIX_INTERFACE_UNIQUE_DATA` / `SHARED_DATA` exists for platform attributes such as Windows DLL decorations. It is inserted verbatim before the generated type name, so it accepts `__declspec(dllimport)`, `__declspec(dllexport)` or a project-owned API macro:

```cpp
// shape_api.h
#if defined(_WIN32)
#  ifdef SHAPE_BUILD_DLL
#    define SHAPE_API __declspec(dllexport)
#  else
#    define SHAPE_API __declspec(dllimport)
#  endif
#else
#  define SHAPE_API
#endif

#define SHAPE_METHODS(X)       \
    X(area, int32_t, i32, {}) \
    X(resize, int32_t, i32, {i32, i32})

SKL_ABIX_INTERFACE_DECLARE(UNIQUE, Shape, SHAPE_METHODS, 1, SHAPE_API);
```

- A provider building the DLL defines `SHAPE_BUILD_DLL`, producing `dllexport`.
- A consumer does not define it and gets `dllimport`.
- On non-Windows platforms `SHAPE_API` is empty and the expansion is unchanged.

When using the two half macros separately, pass the attribute to both:

```cpp
SKL_ABIX_INTERFACE_TRAITS(Shape, SHAPE_METHODS, 1, SHAPE_API);
SKL_ABIX_INTERFACE_UNIQUE_DATA(Shape, SHAPE_METHODS, SHAPE_API);
```

Be clear about the scope: ABIX Interface is header-only. `ShapeTraits` / `Shape` are normally compile-time types with inline forwarding methods, and **do not need DLL import/export decoration in the normal case**. Use the attribute only when the generated type is genuinely part of a Windows DLL boundary and you accept MSVC's rules for imported classes with inline/static members.

`SKL_ABIX_DLL_EXPORT` (from `ABIX/Util/Config.h`) is a different concern: it marks provider-side fixed C entries such as:

```cpp
extern "C" SKL_ABIX_DLL_EXPORT const void *shape_instance(void);
```

It exports symbols found by name (`abi_get_table`, `abix_module_query_v1`); the interface macro attribute decorates the generated `Traits` / proxy type.

#### 3. Generated proxy methods

For every entry, `SKL_ABIX_INTERFACE_FORWARD` generates two template overloads:

```cpp
// 1) Direct result: the proxy is valid and the slot is bound
template<typename... Args>
auto resize(Args&&... args) const noexcept -> int32_t;

// 2) Explicit status: an unbound slot returns method_not_found; success writes out
template<typename... Args>
auto resize(int32_t& out, Args&&... args) const noexcept
    -> skl::abix::poly::InterfaceStatus;
```

Usage:

```cpp
Shape shape = /* after bind */;

const int32_t area = shape.area();

int32_t area_out = 0;
if (shape.area(area_out) == skl::abix::poly::InterfaceStatus::ok) {
    // area_out is valid
}

const int32_t width = 800;
const int32_t height = 600;
const int32_t resized = shape.resize(width, height);

int32_t resized_out = 0;
if (shape.resize(resized_out, width, height) == skl::abix::poly::InterfaceStatus::ok) {
    // resized_out is valid
}
```

Semantics:

- The forwarding form calls `UniqueInterfaceProxy::call` directly and does not check whether the slot is bound. It is the hot path after `proxy.valid()` has been established, and costs the same as a hand-written `call<Fn>(Traits::slots.fn, ...)`.
- The checked form calls `UniqueInterfaceProxy::call_checked`: an unbound slot returns `InterfaceStatus::method_not_found` instead of calling a null pointer; success returns `ok` and writes `out`. Signature verification already happened during `bind()` / `bind_methods()`; it is not repeated per call.
- `ret_t` cannot be `void`: the checked overload needs `ret_t& out`. Methods with no return value should use an AMC-generated proxy or a hand-written proxy.
- The proxy method's parameter types are deduced from the **call-site arguments** via `std::decay_t<Args>...`; the macro does not know the C++ parameter types in the contract. Pass arguments whose types match the adapter's native signature (for example, `int32_t` variables rather than `double` or a different-width integer). A mismatch is not diagnosed by the macro, but constructs the wrong function-pointer type.

#### 3.1 Adapters from existing member functions

If the provider already has a plain C++ class and you do not want to hand-write an `R fn(const void*, ...)` adapter for every method, use:

```cpp
SKL_ABIX_INTERFACE_MEMBER_ENTRY(Traits, slot_name, member_pointer_expression)
```

It emits one `InterfaceMethod` entry whose adapter is generated by `member_adapter<Member>::invoke`. For example:

```cpp
struct Box {
    int32_t width;
    int32_t height;

    int32_t area() const noexcept { return width * height; }

    int32_t resize(int32_t w, int32_t h) noexcept {
        width = w;
        height = h;
        return width * height;
    }
};
```

The contract is still described by `Traits`; the slot name comes from `Traits::slots`:

```cpp
const InterfaceMethod methods[] = {
    SKL_ABIX_INTERFACE_MEMBER_ENTRY(ShapeTraits, area, &Box::area),
    SKL_ABIX_INTERFACE_MEMBER_ENTRY(ShapeTraits, resize, &Box::resize),
};

InterfaceRegistry<4> registry;
if (registry.add({ShapeTraits::id, box_id, methods, 2, ShapeTraits::version, 0})
    != InterfaceStatus::ok)
    return 1;

Box box{3, 4};
Shape shape(registry, &box, box_id);
if (!shape.valid()) return 2;

const int32_t area = shape.area();                 // Box::area()
const int32_t resized = shape.resize(800, 600);   // Box::resize()
```

The table and the registration record also have shorter spellings:

```cpp
#define BOX_METHODS(X)                   \
    X(ShapeTraits, area, &Box::area)     \
    X(ShapeTraits, resize, &Box::resize)

SKL_ABIX_INTERFACE_MEMBER_TABLE(boxMethods, BOX_METHODS);

constexpr TypeId BoxType = box_id;
registry.add(SKL_ABIX_INTERFACE_IMPLEMENTATION(Shape, Box, box, 0));
```

`SKL_ABIX_INTERFACE_MEMBER_TABLE(name, list)` expands the list into a named
`mics::utils::array<InterfaceMethod, N>`. `SKL_ABIX_INTERFACE_IMPLEMENTATION`
takes token prefixes and appends `Traits`, `Type` and `Methods`, so the example
reads `ShapeTraits::id`, `BoxType`, `boxMethods` and `ShapeTraits::version`.

The generated adapter has the shape:

```cpp
int32_t invoke(const void *self, int32_t width, int32_t height);
```

Rules and limitations:

- Supports `const` and non-`const` member functions, including their `noexcept` forms. A single `noexcept(Noex)` specialization avoids Clang's ambiguous `const` versus `const noexcept` partial ordering, so members do not need their `noexcept` removed.
- Supports static member functions as well as instance members. A static member has no implicit object, so the generated adapter accepts the interface handle and ignores it:

  ```cpp
  struct StaticBox { static int32_t area() noexcept; };
  SKL_ABIX_INTERFACE_MEMBER_ENTRY(ShapeTraits, area, &StaticBox::area)
  ```

  If the static function itself already takes the object handle as its first parameter, use `interface_entry(&StaticBox::area)` directly instead.

- A non-`const` member receives a `const void*` handle and removes const internally; a `const` member keeps `const T*`.
- The adapter always uses `const void*` as its first parameter, matching the proxy generated by `SKL_ABIX_INTERFACE_DECLARE`. If a hand-written proxy expects `void*`, write a wrapper.
- It does not handle `volatile`, ref-qualified members (`&` / `&&`) or overload sets. For an overload, pass an explicit member-pointer cast wrapped in parentheses:

    ```cpp
    SKL_ABIX_INTERFACE_MEMBER_ENTRY(
        ShapeTraits, resize,
        (static_cast<int32_t (Box::*)(int32_t, int32_t)>(&Box::resize)))
    ```

- It only generates the adapter entry; it does not produce `Traits`, method signatures or the `id`. Those come from `SKL_ABIX_INTERFACE_DECLARE` / AMC.
- `slot_name` is the interface slot field; it may differ from the member name. `member_pointer_expression` selects the member actually called.

#### 4. Registration and binding: complete example

The macro only declares. The provider still supplies adapters and registers the generated `id` / `signatures` / `slots` with `InterfaceRegistry`:

```cpp
#include "ABIX/Interface/Declaration.h"

namespace demo {

using namespace skl::abix::poly;
using skl::abix::model::TypeId;

constexpr TypeId i32 = skl::abix::model::canonical_type_id("prim:i32");

#define SHAPE_METHODS(X)       \
    X(area, int32_t, i32, {}) \
    X(resize, int32_t, i32, {i32, i32})

SKL_ABIX_INTERFACE_DECLARE(UNIQUE, Shape, SHAPE_METHODS, 1);

// Provider: a plain aggregate, no ABIX members and no Shape base.
struct Box {
    int32_t width;
    int32_t height;
};

// The adapter's first parameter is the object handle.
int32_t box_area(const void *self) {
    const auto *box = static_cast<const Box *>(self);
    return box->width * box->height;
}

int32_t box_resize(void *self, int32_t width, int32_t height) {
    auto *box = static_cast<Box *>(self);
    box->width = width;
    box->height = height;
    return box->width * box->height;
}

constexpr TypeId box_id{0x51, 0x01};

int run() {
    // Slot values and signatures come from the generated Traits.
    const InterfaceMethod methods[] = {
        {ShapeTraits::signatures[ShapeTraits::slots.area],
            interface_entry(&box_area), ShapeTraits::slots.area, 0},
        {ShapeTraits::signatures[ShapeTraits::slots.resize],
            interface_entry(&box_resize), ShapeTraits::slots.resize, 0},
    };

    InterfaceRegistry<4> registry;
    if (registry.add({ShapeTraits::id, box_id, methods, 2, ShapeTraits::version, 0})
        != InterfaceStatus::ok)
        return 1;

    Box box{3, 4};
    Shape shape(registry, &box, box_id);
    if (!shape.valid()) return 2;

    // Hot path: a cached native indirect call.
    const int32_t area = shape.area();

    // Checked path: an unbound slot returns method_not_found.
    int32_t checked = 0;
    if (shape.area(checked) != InterfaceStatus::ok) return 3;

    const int32_t width = 8;
    const int32_t height = 6;
    const int32_t resized = shape.resize(width, height);

    int32_t checked_resize = 0;
    if (shape.resize(checked_resize, width, height) != InterfaceStatus::ok) return 4;

    return area == 12 && checked == 12 && resized == 48 && checked_resize == 48 ? 0 : 5;
}

}   // namespace demo
```

For a dynamically discovered module whose adapter table comes from a manifest, bypass `InterfaceRegistry`:

```cpp
Shape shape;
if (shape.bind_methods(ShapeTraits::id, ShapeTraits::version, methods, 2, &box)
    != InterfaceStatus::ok)
    return 1;
```

`bind_methods` uses the provider-owned method table directly; hold the module lease for as long as the cached function pointers are used, or they will dangle after unload.

#### 5. Versions and interface identity

The third argument of `SKL_ABIX_INTERFACE_DECLARE` / `SKL_ABIX_INTERFACE_TRAITS` is `version`. It feeds:

- `Traits::version`
- the identity calculation `interface_id(#type, version, signatures)`
- version matching when the binding is registered

A plain integer is fine for small iterations. For semantic versions use:

```cpp
constexpr unsigned int version = SKL_ABIX_VERSION_NUMBER(1, 2, 3);
// version == 10'200'003

uint32_t major = 0, minor = 0, patch = 0;
SKL_ABIX_VERSION_EXTRACT(version, major, minor, patch);   // caller supplies ';'
// major == 1, minor == 2, patch == 3
```

Packing rule:

```text
major * 10'000'000 + minor * 100'000 + patch
```

`major` / `minor` are reduced to two decimal digits and `patch` to five. `SKL_ABIX_VERSION_EXTRACT` is the inverse and needs three assignable variables; the caller supplies the trailing semicolon.

Identity rules:

- Same name, different version → different `id`.
- Same version, different method list → different `id`.
- Reordering methods → different signature order → different `id`, and the slot meanings change, so it is an ABI break.
- If another TU generates the same contract with `amc generate -l cpp`, its `kNameInterface` should equal the macro's `interface_id("Name", version, signatures)`. A `static_assert` can pin this down.

#### 6. Step-by-step use: `TRAITS` and `DATA`

The two halves can be used separately:

```cpp
SKL_ABIX_INTERFACE_TRAITS(Shape, SHAPE_METHODS, 1);
```

This emits only `ShapeTraits` (plus a forward declaration of `class Shape;`), not the `Shape` proxy. Useful when:

- only registration is needed, not a C++ proxy;
- a custom proxy exposes only part of the contract;
- the contract constants live in a public header and the proxy in another.

```cpp
SKL_ABIX_INTERFACE_TRAITS(Shape, SHAPE_METHODS, 1);
SKL_ABIX_INTERFACE_UNIQUE_DATA(Shape, SHAPE_METHODS);
```

`SKL_ABIX_INTERFACE_UNIQUE_DATA(type, list)` generates only the inline proxy class and requires `type##Traits` to exist. `SKL_ABIX_INTERFACE_SHARED_DATA(type, list)` generates the shared `Shared##type` class from the same Traits. `SKL_ABIX_INTERFACE_DECLARE` / `_SHARED` / `_BOTH` combine the appropriate steps.

#### 7. Internal expansion macros

These are normally not used directly, but they explain expansion errors:

| Macro                                                       | Effect                                                                                                              |
| ----------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| `SKL_ABIX_INTERFACE_METHOD(fn, ret_t, reg_ret_t, ...)`      | Emits one `method_signature(#fn, reg_ret_t, mics::utils::array __VA_ARGS__),` entry                                 |
| `SKL_ABIX_INTERFACE_SLOT(fn, ...)`                          | Emits `uint32_t fn;` inside `Slots`                                                                                 |
| `SKL_ABIX_INTERFACE_SLOTS(list)`                            | Emits `Slots` and `slots = make_iota<Slots, signatures.size(), 0>()`                                                |
| `SKL_ABIX_INTERFACE_FORWARD(fn, ret_t, ...)`                | Emits the direct-return and `InterfaceStatus` checked overloads                                                     |
| `SKL_ABIX_INTERFACE_MEMBER_ENTRY(Traits, slot, member)`     | Emits one `InterfaceMethod` entry adapted from an existing member function via `member_adapter<member>::invoke`     |
| `SKL_ABIX_INTERFACE_MEMBER_TABLE(name, list)`               | Emits a named `mics::utils::array<InterfaceMethod, N>` from a list of `MEMBER_ENTRY` entries                        |
| `SKL_ABIX_INTERFACE_IMPLEMENTATION(traits, type, methods, flags)` | Emits an `InterfaceImplementation` initializer from `traits##Traits`, `type##Type`, `methods##Methods` and `flags` |
| `SKL_ABIX_INTERFACE_TRAITS(type, list, ver, ...)`           | Emits `typeTraits` (`signatures`, `version`, `id`, `slots`); `...` is placed before `typeTraits` as type attributes |
| `SKL_ABIX_INTERFACE_UNIQUE_DATA(type, list, ...)`           | Emits the `UniqueInterfaceProxy<typeTraits>`-derived `type` proxy; `...` is placed before `type`                    |
| `SKL_ABIX_INTERFACE_SHARED_DATA(type, list, ...)`           | Emits the `SharedInterfaceProxy<typeTraits>`-derived `Shared##type` proxy; `...` is placed before `Shared##type`    |
| `SKL_ABIX_INTERFACE_DECLARE(storage, type, list, ver, ...)` | `TRAITS` + `SKL_ABIX_INTERFACE_<storage>_DATA`; `storage` is `UNIQUE` or `SHARED`                                   |
| `SKL_ABIX_INTERFACE_DECLARE_UNIQUE(type, list, ver, ...)`   | `TRAITS` + `UNIQUE_DATA`                                                                                            |
| `SKL_ABIX_INTERFACE_DECLARE_SHARED(type, list, ver, ...)`   | `TRAITS` + `SHARED_DATA`                                                                                            |
| `SKL_ABIX_INTERFACE_DECLARE_BOTH(type, list, ver, ...)`     | `TRAITS` + `UNIQUE_DATA` + `SHARED_DATA`                                                                            |

Common expansion errors:

- Missing semicolon after `SKL_ABIX_INTERFACE_DECLARE` / `SKL_ABIX_INTERFACE_UNIQUE_DATA` / `SHARED_DATA`.
- Using C++ type names (`int32_t`) instead of `model::TypeId` variables (`i32`) in the parameter list.
- Template arguments containing commas splitting the macro arguments; parenthesize or switch to AMC.
- A method name colliding with an existing macro/type; `fn` becomes both the member name and the `Slots` field name.

#### 8. Limitations and when to use AMC

The macro is a good fit for:

- simple contracts with value parameters and no overloads;
- header-only consumers that do not want to run `amc generate`;
- quick test fixtures and examples.

The macro is not suitable for:

- `void` returns (the checked overload needs `ret_t&`);
- reference, array, function-pointer or record parameters (`std::decay_t` changes the type);
- overloaded methods (same-name entries generate duplicate members);
- complex ABI signatures that must match the compiler AST exactly;
- providers that need an automatically generated manifest.

For those, use `amc generate -l cpp`, or write `Traits` and the proxy by hand following [`Proxy.h`](../../ABIX/Interface/Proxy.h). AMC has the Clang AST and therefore knows the real C++ parameter types; its signatures and adapter typedefs are not limited by macro text.

## What it is not

- Not a shared object model, and not an object lifetime runtime.
- Not a per-call lookup, name comparison or type inspection.
- Not a promise to understand an arbitrary compiler's private object layout: if the provider supplies no adapter, ABIX cannot safely invent one.

## Related

- [`ABI-SPEC.md`](../../.agents/ABI-SPEC.md) — contracts, the extension directory and the Interface contract (identity / slot stability / signature checks).
- [`std.md`](std.md) — ABIX-STD primitives and the borrow/owned/copy contract.
- [`abic.md`](abic.md) — `[[interface]]` / `[[implements]]` build declarations.
- [`without_amc.md`](without_amc.md) — using the header-only runtime without the AMC toolchain.
- [ngcpp/proxy](https://github.com/ngcpp/proxy) — C++20 facade/pointer runtime-polymorphism library (compared above).
