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

## What it is not

- Not a shared object model, and not an object lifetime runtime.
- Not a per-call lookup, name comparison or type inspection.
- Not a promise to understand an arbitrary compiler's private object layout: if
  the provider supplies no adapter, ABIX cannot safely invent one.

## Related

- `../../.agents/ABI-SPEC.md` §9.1 / §13.1 — contracts and the extension directory.
- [`std.md`](std.md) — ABIX-STD primitives and the borrow/owned/copy contract.
- `docs/abix/abic.md` — `[[interface]]` / `[[implements]]` build declarations.
