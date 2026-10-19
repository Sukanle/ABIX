# ABIX Interface — non-intrusive polymorphism

<p align="center">
  <a href="interface_zh.md">中文</a> · English
</p>

The ABIX Interface layer defines the **cross-language interface contract** and
the **method dispatch** mechanism. It is a layer of ABIX, not of any compiler's
object model: an interface is an independent contract, and a provider type is
never modified to implement it.

It lives in `ABIX/Interface/` (namespace `skl::abix::poly`) and is exposed by
`ABIX/ABIX.h`.

## Why an interface ABI, not a vtable ABI

A C++ virtual call is not just a function pointer fetched from an object: it may
involve multiple inheritance, `this` adjustment, thunks, RTTI and
compiler-specific vtable layouts. Rust trait objects, Zig function tables and
other languages have yet another representation. Even when two languages both
say "vtable", their entry order, pointer adjustment and calling rules differ.

ABIX therefore does **not** standardise any private vtable. It defines an
interface above the vtable:

- **Interface identity** — a stable content hash over the interface name,
  version and ordered method signatures. Not a class name, not a vtable address.
- **Method identity** — `(interface_id, slot)`, with the method signature
  verified independently. A matching name or shape alone never makes two
  interfaces compatible.
- **Implementation binding** — how one concrete type reaches a method, through a
  generated adapter that receives the provider's object handle as its first
  argument. The binding names the adapter by signature, never a vtable slot.

These are **contracts, not layout facts**: they never change `TypeID` /
`LayoutHash` / `ABIHash`. Interfaces can also be carried in the metadata region
(extension kinds 7–11) so a consumer discovers them at run time.

## Compared with C++ virtual functions

The same task — two shapes each computing an area, then one polymorphic call over
a set of shapes — written two ways. The left column is a native C++ virtual
function; the right column is ABIX Interface. Both programs print `area=9` and
`area=12`, and both compile and run as shown.

<table>
<tr>
<th align="left">C++ virtual functions (intrusive)</th>
<th align="left">ABIX Interface (non-intrusive)</th>
</tr>
<tr>
<td valign="top">
<pre><code class="language-cpp">#include &lt;cstdio&gt;
struct Shape {
    virtual ~Shape() = default;
    virtual int area() const = 0;
};
struct Square : Shape {
    explicit Square(int s) : side(s) {}
    int area() const override { return side * side; }
    int side;
};
struct Circle : Shape {
    explicit Circle(int r) : radius(r) {}
    int area() const override { return 3 * radius * radius; }
    int radius;
};
int total(const Shape &amp;s) { return s.area(); }
int main() {
    Square sq{3};
    Circle ci{2};
    const Shape *shapes[] = {&amp;sq, &amp;ci};
    for (const Shape *s : shapes) std::printf("area=%d\n", total(*s));
    return 0;
}</code></pre>
</td>
<td valign="top">
<pre><code class="language-cpp">#include &lt;cstdio&gt;
#include "ABIX/Interface/Interface.h"
using namespace skl::abix::poly;
using skl::abix::model::TypeId;
constexpr InterfaceId i_area{0x91, 0x00};
constexpr TypeId square_id{0x11, 0x00};
constexpr TypeId circle_id{0x12, 0x00};
using AreaFn = int (*)(const void *);
struct Square { int side; };
struct Circle { int radius; };
int square_area(const void *self) { int a = static_cast&lt;const Square *&gt;(self)-&gt;side; return a * a; }
int circle_area(const void *self) { int r = static_cast&lt;const Circle *&gt;(self)-&gt;radius; return 3 * r * r; }
int main() {
    const InterfaceMethod square_methods[] = {{{0x01, 0x00}, interface_entry(&amp;square_area), 0, 0}};
    const InterfaceMethod circle_methods[] = {{{0x01, 0x00}, interface_entry(&amp;circle_area), 0, 0}};
    InterfaceRegistry&lt;4&gt; registry;
    registry.add({i_area, square_id, square_methods, 1, 1, 0});
    registry.add({i_area, circle_id, circle_methods, 1, 1, 0});
    Square sq{3};
    Circle ci{2};
    struct Case { void *object; TypeId type; };
    const Case cases[] = {{&amp;sq, square_id}, {&amp;ci, circle_id}};
    for (const Case &amp;k : cases) {
        InterfaceRef&lt;2&gt; ref;
        ref.resolve(registry, k.object, k.type, i_area, 1);
        const auto area = ref.method&lt;AreaFn&gt;(0);
        std::printf("area=%d\n", area(ref.object()));
    }
    return 0;
}</code></pre>
</td>
</tr>
</table>

The difference is not whether polymorphism is possible, but **what carries it**:

- The left encodes polymorphism in the **type system**: `Shape` is a base class,
  `Square`/`Circle` must inherit and `override`, and `area()` dispatches through
  the object's vptr into a compiler-private vtable.
- The right encodes polymorphism in an **interface contract**: `i_area` is the
  interface identity, methods are addressed by a stable `slot` (0 here), and
  `entry` is the adapter; `Square`/`Circle` are plain `struct`s with no
  inheritance and no virtual function. Dispatch is the native entry fetched by
  `ref.method<AreaFn>(0)`.

### Side by side

| Dimension | C++ virtual functions | ABIX Interface |
|-----------|-----------------------|----------------|
| What carries the abstraction | Base class + pure virtual (the type system) | Interface identity `InterfaceId` + method `slot` (a contract) |
| Intrusion on the concrete type | Must inherit, must `override` | None; a plain C++ `struct` / Rust / Zig type all work |
| Dispatch basis | vptr in the object → a compiler-private vtable slot | An explicitly resolved adapter entry `(interface_id, slot)` |
| `this` adjustment / multiple inheritance | Compiler-generated thunk hidden in the vtable | The provider's adapter, with the object handle as its first argument |
| Resolve timing | Implicitly fetches the vptr on every call | `resolve()` once, then zero lookups |
| Heterogeneous collection | Via a base pointer `const Shape*` | `{ void* object, adapter }`, one uniform signature |
| Cross-language | Not portable (layout/order differ) | One `interface_id` implemented by several languages |
| Failure mode | UB (wrong call, dangling vptr) | `InterfaceStatus`: `interface_not_found` / `version_mismatch` / `method_not_found` |
| ABI stability | Adding a virtual or changing inheritance is an ABI break | An interface is a contract; it never feeds `TypeID` / `ABIHash` |

### Which to use

- **Plain C++, one compile-time world, no cross-boundary need**: virtual
  functions are enough, with zero extra ceremony.
- **Across DLLs / across languages / "program to the interface, not the type"**:
  use Interface. A consumer can mention only `InterfaceId` (see the cross-module
  path in [Example 1](#examples)) and keep working when the provider changes its
  concrete type.
- **They coexist**: ABIX does not ask you to give up C++ semantics — a provider
  keeps using virtual functions internally and binds one concrete type's methods
  to interface slots at the boundary only (AMC's `[[interface]]` /
  `[[implements]]` declarations). The interface can be declared in a header like a
  C++ abstract class: `[[interface]]` defines the contract, `[[implements]]`
  establishes the binding, and `amc generate -l cpp` emits
  `k<Name>Interface` / `k<Name>Version` / `enum class <Name>Slot` /
  `<Name>_<method>_fn`, so a consumer can wrap it as
  `Shape s = Shape::bind(...); s.area();`.

## Quick start

Three steps: **register** (record "this concrete type implements this interface"
in the registry) → **resolve** (look the binding up once and cache the adapter
entries) → **call** (a plain native indirect call). The complete program below
needs no shared library.

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

`resolve()` is the only lookup — `registry.lookup_count()` stays `1`; calls after
it run no lookup, name comparison or type inspection. The full API is in the
[API reference](#api-reference), and more scenarios are in [Examples](#examples).

## Binding-time resolution, native call

The runtime facility is deliberately a *binding-time* one. `InterfaceRegistry`
holds "this concrete type implements this interface" records. `InterfaceRef`
resolves once and caches the adapter pointers by slot; every call afterwards is
a plain native indirect call. The runtime never becomes a generic dispatch loop.

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

`resolve()` distinguishes `interface_not_found`, `version_mismatch`,
`method_not_found` and `method_capacity_exceeded` instead of guessing.

## Dynamic discovery across a module boundary

A provider (a shared library) exposes one fixed entry, `abix_module_query_v1`,
returning a stable `ModuleManifest` that lists the interfaces it implements.
The platform export table is only consulted to find that entry: no mangled name
is decoded and no interface meaning is inferred from a symbol.

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

A consumer validates the manifest (`validate_manifest`), registers it, and
queries. Every module instance has a fresh `ModuleInstanceId`; a module may not
unload while a binding holds a lease.

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
- re-registering the same instance is idempotent; a module that declares the
  same `(interface_id, version)` twice is rejected;
- unload begins a drain (no new leases) and completes only after every lease is
  released, so a cached binding is never a dangling function pointer;
- an unknown manifest version, or a malformed shape, fails clearly rather than
  being guessed at.

## Owner ABI

Interface ABI says how to call an object; the owner ABI says who owns it and who
releases it. Only three modes are formalised — **borrowed**, **owned**,
**copied** — and release always returns to the creator's allocation domain: a
consumer never `free()`s or `delete`s the pointer, and never infers a
deallocator from the address or the layout.

```cpp
#include "ABIX/Interface/Owner.h"

// The provider returns a POD handle; the destroy entry lives in the module.
ABIX_OWNER handle = make_owned(new Renderer, &destroy_renderer, control.id);

// The consumer adopts it, holding a module lease for the object's lifetime.
skl::abix::owner::ref object = skl::abix::owner::ref::adopt(handle, &control);
// ... use object.object() through an interface binding ...
// release calls destroy_renderer exactly once, then drops the lease.
```

An owner handle keeps its creating module loaded until released, so a
long-lived object can never outlive the module that must destroy it. Moving the
handle transfers ownership; releasing twice is a no-op.

## Examples

Each example below is complete apart from the shared header it names; the
illustrative provider-object construction is elided.

### Example 1 — discover and call across a module

The provider and the consumer share one contract-constant header (the interface
identity and version must match):

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

The provider builds to `libcanvas.so` and exposes only the fixed entry and its
own object handle — never the object layout:

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

The point: `discover_module` consults the export table for the single symbol
`abix_module_query_v1`; while a binding holds a lease the module cannot unload,
so the cached `width` pointer can never dangle.

### Example 2 — several implementations of one interface, and versions

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

A requested version is exact and `0` selects the newest; a duplicate
`(id, version)` within one module is rejected (`duplicate_interface`), different
modules may coexist, and queries return all of them without overwriting.

### Example 3 — the owner ABI: create and release across a module

The provider returns an owner handle from its own entry; the destroy entry lives
inside that module:

```cpp
// owner_plugin.cpp (excerpt)
skl::abix::owner::handle owner_counter_create(int base) {
    auto *counter = new Counter{base};
    return skl::abix::owner::make_owned(counter, &destroy_counter, /*module_instance*/ 0);
}
```

The consumer adopts the handle and holds a lease, destroying exactly once when it
goes out of scope (continues Example 1's `lib` / `control` / `binding`):

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

The symbols below come from `ABIX/ABIX.h`. Everything lives in namespace
`skl::abix::poly` except the owner ABI (`skl::abix::owner`). To pull in the whole
layer on its own, `#include "ABIX/Interface/Interface.h"`.

| Header | Contents |
|------|------|
| `ABIX/Interface/Contract.h` | Interface identity, `InterfaceMethod` / `InterfaceImplementation`, `InterfaceStatus`, `interface_entry` |
| `ABIX/Interface/Registry.h` | `InterfaceRegistry` — binding-time registration and resolution |
| `ABIX/Interface/Ref.h` | `InterfaceRef` — resolve once, call natively through a slot cache |
| `ABIX/Interface/Module.h` | The fixed entry `abix_module_query_v1`, `ModuleManifest`, `validate_manifest` |
| `ABIX/Interface/Lease.h` | The `ModuleControl` state machine and the RAII `ModuleLease` |
| `ABIX/Interface/ModuleRegistry.h` | `ModuleRegistry` — register and query discovered module interfaces |
| `ABIX/Interface/Discovery.h` | `discover_module` — load → validate → register |
| `ABIX/Interface/Owner.h` | Owner ABI — borrow/own/copy semantics for `handle` / `ref` |
| `ABIX/Interface/Proxy.h` | C++ proxy API — `InterfaceProxy<Traits>`, `bind`/`call`, signature checks |
| `ABIX/Interface/Declaration.h` | optional declaration macros — `ABIX_DECLARE_INTERFACE` (hand-written signature) and `ABIX_DECLARE_INTERFACE_AUTO` (computed by `method_signature`) |

### `Contract.h` — interface identity and methods

| Symbol | Type | Description |
|------|------|------|
| `InterfaceId` | `Hash128` | Interface identity: the content hash over the interface name, version and ordered method signatures. Not a class name, not a vtable address; derive it with `interface_id(...)` |
| `InterfaceMethodHash` | `model::SignatureHash` | One method's ABI signature hash |
| `interface_id(name, version, signatures, count)` | `InterfaceId` | Compile-time interface identity from a canonical name, version and slot-ordered signatures; reproduces `AMC::interface_identity`, so neither side needs a hand-written constant |
| `InterfaceMethodKey` | `{signature, slot}` | A signature paired with its slot, for the `interface_id` array overload to sort before hashing |
| `method_signature(name, return_type, parameters, count)` | `InterfaceMethodHash` | Compile-time method signature from the method name and the return/parameter TypeIDs; reproduces `AMC::signature_hash` (the `[[interface]]` path; parameter names are not hashed). Get TypeIDs from `model::canonical_type_id` (`ABI.h`) |
| `interface_entry<Fn>(fn)` | `uintptr_t` | Copies a function pointer into the opaque `entry` slot; generated registrations and callers spell it the same way |
| `to_string(status)` | `const char*` | Readable name for an `InterfaceStatus` |

**Cross-tool identity contract.** `InterfaceId` is defined identically by
`AMC::interface_identity` and `ABIX::poly::interface_id`:

```
hash_text("ABIX-INTERFACE:" + canonical_name + ":" + version + slot-ordered signatures,
          domain = 0x494E544552464143)
```

The domain tag is the 8-byte ASCII `"INTERFAC"` — a big-endian truncation of
`"INTERFACE"` (the 9th byte, `E`, does not fit a `uint64_t`) — i.e. bytes
`49 4E 54 45 52 46 41 43`. It is **mixed into the hash's initial state as an
integer** (`lo ^= domain`, `hi += domain`), **not** used as a byte prefix. There
is therefore no endianness choice to make; another language must use the same
integer and the same mixing, not `reinterpret_cast` the integer's memory.

The seeds are shared with ABIX's own `fnv1a`: `lo` starts from the standard
**FNV-1a 64 offset basis** `0xCBF29CE484222325`, `hi` from FNV prime
`0x100000001B3` + domain, and the `hi` lane multiplier is XXH64's `PRIME64_2`.
`amc-core-test` pins these constants with fixed vectors and checks that both
sides agree.

`InterfaceMethod` — one method's adapter entry:

| Field | Type | Description |
|------|------|------|
| `signature` | `InterfaceMethodHash` | Method ABI signature hash recorded by AMC |
| `entry` | `uintptr_t` | Adapter entry point, called with the object handle as its first argument; `0` means the slot is unbound |
| `slot` | `uint32_t` | Stable dispatch slot |
| `flags` | `uint32_t` | Reserved |

`InterfaceImplementation` — one concrete type's implementation of an interface:

| Field | Type | Description |
|------|------|------|
| `interface_id` | `InterfaceId` | |
| `concrete_type` | `model::TypeId` | The concrete type implementing the interface |
| `methods` | `const InterfaceMethod*` | Provider-owned method table |
| `method_count` | `uint32_t` | Number of entries |
| `version` | `uint32_t` | Interface version |
| `flags` | `uint32_t` | Reserved |

`InterfaceStatus`: `ok`, `invalid_table`, `duplicate_implementation`,
`capacity_exceeded`, `interface_not_found`, `version_mismatch`,
`method_not_found`, `method_capacity_exceeded`.

### `Registry.h` — binding-time registry

`InterfaceRegistry<Capacity = 64>`: fixed capacity, allocation-free. Adding and
resolving are setup work, so a linear scan is fine — it never runs on the call
path. `add()` **registers**, `find()` **resolves**.

| Method | Return | Description |
|------|--------|------|
| `add(impl)` | `InterfaceStatus` | Register one implementation. A repeated `(concrete_type, interface_id, version)` returns `duplicate_implementation` (a conflict, not a silent overwrite); a full registry returns `capacity_exceeded`; an empty id or a zero method table with a nonzero count returns `invalid_table` |
| `find(concrete_type, interface_id, version = 0)` | `const InterfaceImplementation*` | `version == 0` selects the newest registered version, otherwise an exact match; `nullptr` when absent |
| `find_newest(concrete_type, interface_id)` | `const InterfaceImplementation*` | Newest version, or `nullptr` |
| `available(concrete_type, interface_id, newest&)` | `bool` | Whether any version exists; `newest` receives the newest version — lets a caller distinguish "unknown interface" from "wrong version" without a second full scan |
| `size()` | `size_t` | Number of registered implementations |
| `at(index)` | `const InterfaceImplementation*` | Indexed access; `nullptr` out of range |
| `lookup_count()` | `size_t` | Diagnostic: how many `find()` calls were served; proves resolve-then-call performs exactly one lookup |

### `Ref.h` — resolved handle

`InterfaceRef<MaxMethods = 32>`: one resolve copies the bound adapter pointers
into a slot-indexed cache; every call afterwards is a plain native indirect call
— no per-call lookup, name comparison or type inspection.

| Method | Return | Description |
|------|--------|------|
| `resolve<RegCap>(registry, object, concrete_type, interface_id, version = 0)` | `InterfaceStatus` | Exactly one registry lookup; `object == nullptr` returns `invalid_table`; on failure distinguishes `version_mismatch` (interface exists, version does not) from `interface_not_found` |
| `resolve_methods(interface_id, version, methods, method_count, object)` | `InterfaceStatus` | Resolve directly from a provider-owned table — the path a dynamically discovered module uses (table from its manifest, caller holds the lease); a slot above `MaxMethods` returns `method_capacity_exceeded` |
| `method<Fn>(slot)` | `Fn` | The cached entry point, or `nullptr` when the slot is unbound or out of range. `Fn` takes the object handle as its first argument; pass `object()` |
| `bound(slot)` | `bool` | Whether the slot is bound |
| `valid()` | `bool` | Resolved and the object is non-null |
| `object()` | `void*` | The provider's object handle (the first argument of a call) |
| `interface_id()` / `version()` | | Resolved interface identity / version |
| `method_count()` | `uint32_t` | Slots spanned by the implementation (highest bound slot + 1) |

`query_interface<MaxMethods = 32, RegCap>(registry, object, concrete_type,
interface_id, version = 0)` — convenience that returns an `InterfaceRef` by value;
check `valid()`.

The provider's object must outlive the handle and must be the object the adapters
were generated for (the handle creator owns the correct pointer and any `this`
adjustment).

### `Module.h` — the discovery manifest

| Symbol | Type | Description |
|------|------|------|
| `ModuleInstanceId` | `uint64_t` | Unique per load; the same file loaded twice, or reloaded, gets a new id; `0` is invalid |
| `invalid_module_instance` | constant | |
| `abix_manifest_version` | `uint32_t` | Version of the manifest ABI itself; a consumer that does not know a version must refuse it rather than guess |
| `abix_module_query_symbol` | `const char*` | The fixed entry name `"abix_module_query_v1"` |
| `abix_max_manifest_interfaces` / `abix_max_manifest_methods` | constants | Sanity bounds so a malformed manifest cannot drive an unbounded loop |
| `ModuleQueryFn` | `const ModuleManifest* (*)(void)` | The fixed entry signature |
| `SKL_ABIX_DEFINE_MODULE_QUERY(manifest)` | macro | Defines the `extern "C"` `abix_module_query_v1` |
| `ManifestStatus` | enum | Result of `validate_manifest`; `to_string` gives the name |
| `validate_manifest(const ModuleManifest*)` | `ManifestStatus` | Validates a manifest |

`InterfaceManifestEntry` (fixed 40-byte wire layout):

| Field | Type | Description |
|------|------|------|
| `interface_id` | `InterfaceId` | |
| `interface_version` | `uint32_t` | Distinguishes compatible contract revisions |
| `flags` | `uint32_t` | Reserved |
| `methods` | `const InterfaceMethod*` | Provider-owned adapter table |
| `method_count` | `uint32_t` | Number of entries |
| `reserved` | `uint32_t` | |

`ModuleManifest` (40 bytes): `manifest_version`, `module_abi_version`, `flags`,
`interface_count`, `interfaces`, `extension_count`, `reserved`, `extensions`.
`extensions` is an opaque, optional, forward-compatible region: a consumer that
does not know it skips it, but must still validate its declared boundary.

`ManifestStatus`: `ok`, `null_manifest`, `unsupported_version`,
`invalid_interface`, `invalid_binding`, `too_many_interfaces`, `too_many_methods`.

`validate_manifest` rejects, **without dereferencing arbitrary memory**: a null
manifest, an unknown `manifest_version`, an `interface_count` / `method_count`
above the bounds, a zero `interface_id`, a null method table when it claims
methods, or a method table with a duplicate slot. A bound slot with
`entry == 0` is allowed (an unbound slot). It cannot prove a pointer is safe in
general.

### `Lease.h` — module lifecycle and leases

The lifecycle is a small state machine with a lease count:

```text
loading ──activate──▶ active ──begin_unload──▶ draining ──idle──▶ unloaded
```

| Symbol | Type | Description |
|------|------|------|
| `ModuleState` | enum | `loading` / `active` / `draining` / `unloaded`; `to_string` gives the name |
| `ModuleControl` | struct | The loader-owned lifecycle token: `id`, `name`, `state`, `leases`, plus the platform `module_handle` and the `pin`/`unpin` hooks. Kept outside the registry so the loader can tie it to the platform module handle |
| `next_module_instance_id()` | `ModuleInstanceId` | Monotonic; never reused |
| `module_state(control)` | `ModuleState` | Reads the current state |
| `activate_module(control)` | `bool` | `loading → active` (CAS); `false` when not loading |
| `try_acquire_lease(control)` | `bool` | Acquires only while `active`; the state is re-checked after the increment so a concurrent `begin_unload` cannot be missed. Takes an OS-level reference (`pin`) so the module stays mapped while the lease is held |
| `release_lease(control)` | `void` | Releases one lease |
| `begin_unload(control)` | `bool` | Stops admitting new leases (enters `draining`); `true` if already draining/unloaded, `false` if never activated |
| `wait_for_idle(control, max_spins = 1000000)` | `bool` | Waits until every lease is released; bounded so a leaked lease cannot hang a caller forever |
| `mark_unloaded(control)` | `void` | Sets `unloaded` |
| `ModuleLease` | RAII class | Movable, non-copyable; `valid()`, `release()`; the destructor releases; releasing an unheld lease is a no-op |
| `acquire_lease(control)` | `ModuleLease` | An invalid lease (never a partial one) when the module is not `active` |

A lease is held for the duration of an active call or a live object handle. This
is what makes a cached method binding safe: the binding stays valid exactly as
long as the caller holds a lease.

**OS-level pinning.** When the loader binds the platform identity
(`Discovery.h` → `bind_module_platform`), the first lease calls `pin` and the
last calls `unpin`: on Windows `pin` resolves the module base address with
`GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)` and `unpin` calls
`FreeLibrary`; on POSIX `pin` re-opens the module by its load path
(`dlopen(path)`, which increments the reference count without re-running
initializers) and `unpin` calls `dlclose`. A held lease therefore **directly
blocks** `FreeLibrary`/`dlclose` at the OS level, not only through the state
machine — an owner that unloads while a lease is held cannot map the module out.
The reference is taken once across all leases and needs the loader-owned
`module_path`, so the loader must keep the `Object` (and its path) alive while
leases are held.

### `ModuleRegistry.h` — dynamically discovered module interfaces

`ModuleInterface` — one discovered module interface: `module`, `interface_id`,
`version`, `methods`, `method_count`, `control` (the lifetime token a caller must
lease before calling).

`ModuleStatus`: `ok`, `already_registered`, `invalid_manifest`, `invalid_state`,
`capacity_exceeded`, `interface_capacity_exceeded`, `duplicate_interface`,
`not_registered`, `busy`.

`ModuleRegistry<ModuleCapacity = 16, InterfaceCapacity = 128>`:

| Method | Return | Description |
|------|--------|------|
| `register_module(control, manifest)` | `ModuleStatus` | Validates the manifest, rejects a duplicate `(id, version)` within the module, activates and registers; re-registering the same control is idempotent (`already_registered`); a `control` not in the loading state returns `invalid_state` |
| `unregister_module(id)` | `ModuleStatus` | Begins unloading: `begin_unload`, then removes only once every lease is released; otherwise the module is draining (no new leases) and the caller retries after in-flight calls — reported as `busy` |
| `registered(id)` | `bool` | |
| `module_count()` / `interface_count()` | `size_t` | |
| `interface_at(index)` | `const ModuleInterface*` | Indexed access |
| `find_interface(id, version, out, max)` | `size_t` | Writes **all** implementations in registration order (`version == 0` matches any); returns the count written (up to `max`) |
| `find_first(id, version = 0)` | `const ModuleInterface*` | The newest implementation, or `nullptr` |

Several modules may implement one interface; queries return all of them and never
overwrite one with another; a version is exact when requested and "newest" when
`0` is requested.

`BoundInterface<MaxMethods = 32>` — a resolved binding plus the lease that keeps
its module loaded; `valid()` requires both `ref.valid()` and `lease.valid()`.
Calling through `ref` is only valid while this object is alive.
`bind_interface<MaxMethods = 32>(binding, object)` — acquires the lease and
resolves; returns an invalid binding (no lease, unresolved ref) when the module
is no longer active.

### `Discovery.h` — load and discover

| Symbol | Type | Description |
|------|------|------|
| `DiscoveryResult` | struct | `manifest` (`ManifestStatus`), `registration` (`ModuleStatus`), `queried` (the manifest pointer), `ok()` |
| `discover_module<ModuleCapacity = 16, InterfaceCapacity = 128>(module, control, registry)` | `DiscoveryResult` | Resolves `abix_module_query_v1` from a loaded module, validates it with `validate_manifest`, then `register_module` |

The platform export table is consulted for **that one symbol only**: no other
export is decoded and no interface meaning is inferred from a symbol. A missing
entry, an unknown manifest version, a malformed manifest or a duplicate
declaration all fail explicitly. On success `control` becomes `active`.

### `Owner.h` — the owner ABI

Namespace `skl::abix::owner`. Interface ABI says how to call an object; the
owner ABI says who owns it, who releases it, and where the releasing code lives.

| Symbol | Type | Description |
|------|------|------|
| `abix_owner_abi_version` | `uint32_t` | Owner ABI version |
| `mode` | enum | `borrowed` / `owned` / `copied`; `to_string` gives the name |
| `destroy_fn` | `void (*)(void*)` | The creator's destroy entry, with a fixed C ABI |
| `handle` | struct (32 bytes) | POD handle: `object`, `mode`, `flags`, `destroy`, `module_instance` |
| `valid(handle)` | `bool` | `object != nullptr` |
| `make_borrowed(object)` | `handle` | Non-owning reference |
| `make_owned(object, destroy, module_instance = invalid)` | `handle` | Exclusive ownership, released through `destroy` |
| `make_copied(object, destroy, module_instance = invalid)` | `handle` | Independent copy, released through `destroy` |

`handle` fields: `object` (opaque to the consumer), `mode`, `flags`, `destroy`
(required for owned, optional for copied), `module_instance` (diagnostic: which
load created it).

`ref` — the runtime owner handle: the creator's POD `handle` plus a module lease
that keeps the destroying module loaded for as long as the object lives.

| Method | Return | Description |
|------|--------|------|
| `adopt(handle, control = nullptr)` | `ref` | Adopts a creator-returned handle; for an owned/copied handle pass the creating module's `control` to hold the lease; a borrowed handle may omit it |
| `borrow(object, control = nullptr)` | `ref` | Convenience: `adopt(make_borrowed(object), control)` |
| `valid()` | `bool` | Object is non-null |
| `object()` | `void*` | The object pointer |
| `ownership()` | `mode` | Ownership mode |
| `owns()` | `bool` | `owned` or `copied` |
| `leased()` | `bool` | Whether a module lease is held |
| `raw()` | `const handle&` | The underlying POD handle |
| `release()` | `void` | Releases exactly once: for owned/copied it calls the creator's `destroy`, then drops the module lease; a borrowed handle is not destroyed. The handle is cleared, so releasing twice is a no-op |

A failed creation returns an invalid handle (`object == nullptr`) rather than a
partial one; `destroy` returns `void` — a release cannot fail, so any failure
must be reported at creation time. Moving the handle transfers ownership.

### `Proxy.h` / `Declaration.h` — the C++ proxy API

An interface proxy is a lightweight handle over "an object handle plus resolved
adapters". It reads like a C++ `Shape*`, but it is not a base class — the
concrete type is never modified and no compiler vtable is used.
`InterfaceProxy<Traits>` holds the shared machinery; a per-interface class adds
only its methods, and `Traits` supplies the interface difference:

| `Traits` member | Description |
|------|------|
| `id` | The `InterfaceId` (computed by AMC) |
| `version` | The interface version |
| `slots` | The slot **capacity** (>= highest slot + 1), which sizes the `InterfaceRef` cache — not a magic number |
| `signatures[]` (optional) | The expected signature per slot; when present, `bind()` verifies every slot (`0` means "do not check this slot") |

`InterfaceProxy<Traits>`:

| Method | Return | Description |
|------|--------|------|
| `bind(registry, object, concrete_type, version = Traits::version)` | `InterfaceStatus` | Binds and (when `signatures` exists) verifies; failure is **explicit** and also retained in the proxy |
| `bind_methods(interface_id, version, methods, count, object)` | `InterfaceStatus` | Binds directly to a provider-owned table (dynamic discovery); the caller keeps the module lease |
| `status()` / `valid()` | | Binding status / usable (`ok` and resolved) |
| `object()` / `interface_id()` / `version()` / `method_count()` | | Object handle and contract facts |
| `verify(slot)` | `InterfaceStatus` | Verify one slot: `method_not_found` if unbound, `signature_mismatch` on a wrong signature |
| `call<Fn>(slot, args...)` (protected) | the `Fn` result | Directly call a cached entry (the object handle is supplied first); precondition: `valid()` and the slot is bound |
| `call_checked<Fn>(out, slot, args...)` (protected) | `InterfaceStatus` | Status and result are separate: an unbound slot is `method_not_found`, so a legitimate result is never read as a failure |

`Declaration.h` (optional sugar) offers two declaration macros that differ only
in how a method's signature hash is obtained. Both remove the
boilerplate with an X-macro and **never let the preprocessor parse a signature**.
The identity is not written by hand either: both compute it with
`interface_id("Shape", version, methods)`, matching AMC exactly.

`ABIX_DECLARE_INTERFACE` takes the signature as a `{lo, hi}` constant:

```cpp
#define SHAPE_METHODS(X)                 \
    X(area, 0x1234, 0x5678, int32_t, 0)  \
    X(size, 0x9abc, 0xdef0, int32_t, 1)

ABIX_DECLARE_INTERFACE(Shape, 1, SHAPE_METHODS)
```

`ABIX_DECLARE_INTERFACE_AUTO` computes it with `method_signature(...)` from AMC
TypeIDs. Each entry is `X(method, return_type_id, parameters, parameter_count,
return_type, slot)`, and the proxy forwards the method arguments:

```cpp
constexpr model::TypeId i32 = model::canonical_type_id("prim:i32");
constexpr util::StaticArray resize_params{i32, i32};

#define SHAPE_AUTO(X)                             \
    X(area,   i32, nullptr,      0, int32_t, 0)   \
    X(resize, i32, resize_params, 2, int32_t, 1)

ABIX_DECLARE_INTERFACE_AUTO(Shape, 1, SHAPE_AUTO)

// shape.area()             object handle first, no arguments
// shape.resize(3, 4)       forwards two arguments
// shape.resize(out, 3, 4)  explicit-status overload
```

The parameter list must be a name already in scope (the preprocessor cannot
build `{i32, i32}`); it may be a raw `model::TypeId[]` or a `util::StaticArray`.

A hand-written `Traits` can compute the signatures and the identity too (the
helpers reproduce `AMC::signature_hash` / `AMC::interface_identity`;
`amc-core-test` checks them against AMC):

```cpp
struct ShapeTraits {
    // AMC's int32_t TypeID is an ABI key, not the C++ spelling.
    static constexpr model::TypeId i32 = model::canonical_type_id("prim:i32");
    // The method signature is generated too; the return/parameter TypeIDs must
    // match the provider.
    static constexpr InterfaceMethodHash sigs[] = {
        method_signature("area", i32, nullptr, 0),
        method_signature("size", i32, nullptr, 0),
    };
    static constexpr InterfaceId id = interface_id("Shape", 1, sigs, 2);
    static constexpr uint32_t version = 1;
    static constexpr size_t slots = 2;
    static constexpr uint32_t area_slot = 0;
    static constexpr uint32_t size_slot = 1;
};
```

`method_signature` / `canonical_type_id` only solve the last hop: their inputs —
the method name and the return/parameter TypeIDs — are still AMC-derived. A
primitive TypeID is an ABI key (`int32_t` → `prim:i32`, which varies with the
target's width and signedness) that the preprocessor cannot infer from a C++
spelling. Interfaces with parameters, overloads or complex signatures should
still have AMC generate the signature table; the helpers are for simple contracts
where both sides agree.

It emits `ShapeTraits` (`id`/`version`/`slots`/`signatures`/`*_slot`) and a `Shape`
proxy whose methods are object-handle-first pass-throughs, plus a form returning
`InterfaceStatus`. Methods with extra parameters, overloads and complex signatures
are better generated by AMC (Clang has the AST); the macro is a convenience, not
the only mechanism.

#### Registering `signatures` — TypeIDs for basic and custom types

`signatures` is indexed by slot, so write the entries in slot order. Each entry
is compared with the provider's `InterfaceMethod.signature`, and `interface_id`
hashes the same values; a mismatch is reported as `signature_mismatch`, never
called silently.

`method_signature(name, return_type, parameters, count)` reproduces
`AMC::signature_hash`:

```
name + decimal(return_type.lo) + decimal(return_type.hi)
     + for each parameter: decimal(type.lo) + decimal(type.hi)
```

with domain `0x5349474E41545552` (`"SIGNATUR"`). Parameter names are not hashed.
`name` and `parameters` are those of the **contract function** AMC recorded, not
an arbitrary label. If that function takes the object handle first (the usual
adapter convention), the handle is parameter 0 and belongs in `parameters`; a
free function has no handle. AMC already emits the exact `signatures[]` in the
generated header — copy it when in doubt.

The parameter list can be passed in three equivalent, allocation-free forms:

```cpp
constexpr TypeId params[] = {i32, i32};
method_signature("resize", v, params, 2);                 // pointer + count
method_signature("resize", v, util::StaticArray{i32, i32});     // inline fixed array
method_signature("resize", v, i32, i32);                  // variadic
```

`util::StaticArray` is the minimal fixed-size type in `ABIX/Util/Array.h`; its
deduction guide makes `util::StaticArray{i32, i32}` name its own type. All three forms
hash the same bytes and allocate nothing.

A signature mentions **TypeIDs**, not C++ types. For name-based identities,
`model::canonical_type_id(canonical_name)` reproduces AMC's value:

| C++ type | canonical name | Notes |
|---|---|---|
| `int` / `int32_t` | `prim:i32` | integer keys are ABI-derived **bit** widths; ABI-equal spellings collapse |
| `uint64_t` | `prim:u64` | |
| `double` | `prim:fieee64` | float keys keep the format (`ieee`, `bfloat`, `x87`, `ppc`) |
| `bool` | `prim:bool` | |
| `void` | `prim:void` | return position only |
| `char` | `prim:char_s` / `prim:char_u` | signedness is target-defined |
| `cv::Point` | `cv::Point` | qualified record name |
| `std::errc` | `std::errc` | enum; `[[expose]]` rejects raw `std::*` parameters (see below) |
| `std::string` | `abix_str_utf8` | the `[[adapt]]` target; `std::string` itself is rejected |
| `std::vector<int32_t>` | `abix_span_i32` | via `[[adapt]]` / `[[instantiate]]` |
| `T*` / `const T*` | Clang's pointer spelling | not stable to hand-write; read it from `amc query` / `amc-dump` |

**Custom value records — `cv::Point`.** A plain value record is representable;
its TypeID is the qualified name:

```cpp
constexpr model::TypeId void_id = model::canonical_type_id("prim:void");
constexpr model::TypeId point   = model::canonical_type_id("cv::Point");
constexpr model::TypeId move_params[] = {point};
method_signature("move", void_id, move_params, 1);   // move(cv::Point)
```

If the contract passes `cv::Point*`, use the pointer's TypeID, not
`cv::Point`'s; get that value from `amc query` / `amc-dump` rather than guessing Clang's
spelling.

**Strings — `std::string`.** An STL type must not cross the boundary raw.
Declare the mapping:

```toml
[[adapt]]
native = "std::string"
target = "abix_str_utf8"
```

The thunk then takes `abix_str_utf8` (data + length + encoding), and the
signature uses that boundary TypeID:

```cpp
constexpr model::TypeId i32  = model::canonical_type_id("prim:i32");
constexpr model::TypeId text = model::canonical_type_id("abix_str_utf8");
constexpr model::TypeId open_params[] = {text};
method_signature("label", text, nullptr, 0);       // returns a borrowed view
method_signature("open", i32, open_params, 1);     // open(abix_str_utf8) -> status
```

The adapter is generated and linked; it converts the boundary value to
`std::string` / `std::string_view`. Only parameters are adapted. A returned view
follows the borrow/own/copy ownership contract; a value the consumer keeps must
be copied or owned.

**Enums — `std::errc`.** An enum has a TypeID
(`canonical_type_id("std::errc")`), but AMC's `[[expose]]` representability check
rejects a raw `std::*` parameter. For an error status, pass the underlying integer
(`prim:i32`) and describe it with `[[error]]` (`status`, `success`, `value`), or
return an `abix_result_*` record; the error boundary carries `Result<T, E>`
without exposing the enum layout. A hand-written provider/consumer pair may use
the enum TypeID directly if both sides agree, but it will not match an
AMC-generated `[[expose]]` thunk.

**Complete example.** `draw()`, `move(cv::Point)`,
`label() -> abix_str_utf8`, and `open(abix_str_utf8) -> int32_t` (a `std::errc`
code):

```cpp
constexpr model::TypeId i32   = model::canonical_type_id("prim:i32");
constexpr model::TypeId v     = model::canonical_type_id("prim:void");
constexpr model::TypeId point = model::canonical_type_id("cv::Point");
constexpr model::TypeId text  = model::canonical_type_id("abix_str_utf8");
constexpr model::TypeId move_params[] = {point};
constexpr model::TypeId open_params[] = {text};

static constexpr InterfaceMethodHash sigs[] = {
    method_signature("draw",  v,    nullptr,     0),
    method_signature("move",  v,    move_params, 1),
    method_signature("label", text, nullptr,     0),
    method_signature("open",  i32,  open_params, 1),
};
static constexpr InterfaceId id = interface_id("Canvas", 1, sigs, 4);
```

If the provider is AMC-generated and its contract functions take the object
handle first, prepend the handle TypeID to each parameter list (or simply copy
the generated `signatures[]`).

**Getting the exact value.** For pointers, typedefs, template aliases and nested
records, do not guess the canonical spelling. Read it from the artifact:

```sh
amc query module.abix --type cv::Point     # id=0X... name=cv::Point
amc-dump module.abix                       # full section dump
```

`amc-core-test` pins `method_signature` against `AMC::signature_hash` and
`canonical_type_id` against `AMC::hash_text`, so the helpers cannot drift from
the generator.

`amc generate -l cpp` emits the equivalent `Traits` for a `[[interface]]`, and —
when **every** method takes the object handle first — an `InterfaceProxy`-derived
class as well (otherwise only `Traits`, with a note explaining why).

#### Three ways to do it

The same task — measure a shape's area and call it polymorphically — written three
ways, all printing `area=25`.

Left: a C++ virtual function; middle: ABIX manual registration (a hand-written adapter + `InterfaceRegistry` + `InterfaceRef`); right: an ABIX proxy (`Traits` + `InterfaceProxy`).

<table>
<tr>
<th align="left">C++ virtual function</th>
<th align="left">ABIX: manual registration</th>
<th align="left">ABIX: proxy (Proxy.h)</th>
</tr>
<tr>
<td valign="top">
<pre><code class="language-cpp">struct Shape {
    virtual ~Shape() = default;
    virtual int area() const = 0;
};
struct Square : Shape {
    explicit Square(int s) : side(s) {}
    int area() const override { return side * side; }
    int side;
};
int main() {
    Square sq{5};
    const Shape &amp;s = sq;                    // polymorphic
    std::printf("area=%d\n", s.area());     // 25
}</code></pre>
</td>
<td valign="top">
<pre><code class="language-cpp">struct Square { int side; };
int square_area(const void *self) {
    int s = static_cast&lt;const Square *&gt;(self)-&gt;side;
    return s * s;
}
using AreaFn = int (*)(const void *);
int main() {
    const InterfaceMethod m[] = {
        {{0x1111, 0x2222}, interface_entry(&amp;square_area), 0, 0}};
    InterfaceRegistry&lt;4&gt; registry;
    registry.add({kShapeInterface, square_id, m, 1, 1, 0});
    Square sq{5};
    InterfaceRef&lt;4&gt; ref;
    ref.resolve(registry, &amp;sq, square_id, kShapeInterface, 1);
    std::printf("area=%d\n", ref.method&lt;AreaFn&gt;(0)(ref.object()));
}</code></pre>
</td>
<td valign="top">
<pre><code class="language-cpp">// Traits: written by hand, or generated by ABIX_DECLARE_INTERFACE / amc generate -l cpp
struct ShapeTraits {
    static constexpr InterfaceId id = kShapeInterface;
    static constexpr uint32_t version = 1;
    static constexpr size_t slots = 1;
    static constexpr uint32_t area_slot = 0;
};
class Shape : public InterfaceProxy&lt;ShapeTraits&gt; {
    using Base = InterfaceProxy&lt;ShapeTraits&gt;;
public:
    using Base::Base;
    int area() const noexcept {
        return this-&gt;template call&lt;int (*)(const void *)&gt;(ShapeTraits::area_slot);
    }
    InterfaceStatus area(int &amp;out) const noexcept {
        return this-&gt;template call_checked&lt;int (*)(const void *)&gt;(out, ShapeTraits::area_slot);
    }
};
int main() {
    /* registration as in the middle column */
    Shape shape(registry, &amp;sq, square_id);
    std::printf("area=%d\n", shape.valid() ? shape.area() : -1);
}</code></pre>
</td>
</tr>
</table>

Comparing the three:

- **C++ virtual function** encodes polymorphism in the **type system**: the
  implementation must inherit `Shape`, and dispatch goes through a
  compiler-generated vtable.
- **ABIX manual registration** is the lowest-level explicit path (write an
  adapter → register → resolve → call), with no extra abstraction; the adapter's
  first parameter is the object handle.
- **ABIX proxy** packages binding / status / calls into one type: the concrete
  type stays a plain `struct` (no inheritance, no virtual function), and after a
  single bind `shape.area()` is a plain native indirect call. The `Traits` can be
  written by hand, or generated by `ABIX_DECLARE_INTERFACE` or
  `amc generate -l cpp`.

## What it is not

- Not a shared object model, and not an object lifetime runtime.
- Not a per-call lookup, name comparison or type inspection.
- Not a promise to understand an arbitrary compiler's private object layout: if
  the provider supplies no adapter, ABIX cannot safely invent one.

## Related

- [`ABI-SPEC.md`](../../.agents/ABI-SPEC.md) — contracts, the extension directory and the Interface contract (identity / slot stability / signature checks).
- [`std.md`](std.md) — ABIX-STD primitives and the borrow/owned/copy contract.
- [`abic.md`](abic.md) — `[[interface]]` / `[[implements]]` build declarations.
- [`without_amc.md`](without_amc.md) — using the header-only runtime without the AMC toolchain.
