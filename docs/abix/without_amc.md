# Using ABIX without AMC

<p align="center">
  <a href="without_amc_zh.md">中文</a> · English
</p>

ABIX and AMC have different jobs:

- **ABIX** (`ABIX/`) is a header-only runtime layer. It contains no `.cpp` files
  and never includes `AMC/...`. `libabix-runtime` is a CMake `INTERFACE` target
  that only adds the include root.
- **AMC** is a build-time toolchain (Clang/LLVM + `toml++`) that extracts C++
  semantics, computes canonical ABI identities, writes `.abix` artifacts and
  generates thunks, adapters and C++ projections.

A C++-only project can therefore depend on ABIX alone. AMC is never a runtime
dependency — even when it is used, the produced binaries do not link it — and a
hand-written C++ contract does not need it at build time either.

The trade-off is that the contract becomes manual: you write the adapter tables
and `Traits`, and you supply the TypeIDs instead of letting Clang extract them.

## What works with ABIX alone

| Capability | Header | AMC required? |
|---|---|---|
| Interface identity and method signatures | `ABIX/Interface/Contract.h` | No — `interface_id`, `method_signature` and `canonical_type_id` are `constexpr` |
| Hand-written `Traits` and proxies | `ABIX/Interface/Declaration.h` | No — `ABIX_DECLARE_INTERFACE` / `ABIX_DECLARE_INTERFACE_AUTO` |
| In-process register / resolve / call | `Registry.h`, `Ref.h`, `Proxy.h` | No |
| Dynamic-library discovery and leases | `Module.h`, `ModuleRegistry.h`, `Discovery.h`, `Lease.h` | No — write the `ModuleManifest` by hand and use `SKL_ABIX_DEFINE_MODULE_QUERY` |
| Owner ABI | `Owner.h` | No |
| Runtime table / descriptors | `ABIX/Runtime/Registry.h`, `ABIX/Metadata/Descriptor.h`, `SKL_ABIX_DEFINE_TABLE` | No — the structures are public and can be written by hand |
| ABIX's own type/layout digests | `ABIX/Model/ABI.h`, `ABIX/Model/Compat.h` | No — `canonical_type_id`, `structural_layout_hash`, `abi_signature_hash` |

A minimal C++-only declaration:

```cpp
#include "ABIX/Interface/Declaration.h"
#include "ABIX/Interface/Interface.h"

using namespace skl::abix::poly;

constexpr model::TypeId i32 = model::canonical_type_id("prim:i32");

#define SHAPE_METHODS(X) X(area, i32, int32_t, 0)
ABIX_DECLARE_INTERFACE_AUTO(Shape, 1, SHAPE_METHODS)

// Provider side: adapters receive the object handle first.
constexpr model::TypeId square_id{0x51, 0x52};   // the concrete type's TypeID
int square_area(const void *self);
const InterfaceMethod square_methods[] = {
    {ShapeTraits::signatures[0], interface_entry(&square_area), 0, 0},
};
InterfaceRegistry<4> registry;
registry.add({ShapeTraits::id, square_id, square_methods, 1, 1, 0});
```

No AMC-generated file is involved. For a shared library, expose the same
information through a hand-written `ModuleManifest` and
`SKL_ABIX_DEFINE_MODULE_QUERY`; consumers use `discover_module` and
`ModuleRegistry` as usual.

## What AMC provides

Without AMC you lose the automatic parts:

- `[[interface]]` / `[[implements]]` / `[[expose]]` / `[[adapt]]` driven
  generation;
- Clang AST extraction of types, fields, layouts and function signatures;
- `.abix` artifacts, the Metadata Region, `TypeTraits`, layout validation,
  compatibility reports and generated adapters;
- Rust / Zig / Lua projections;
- the C++-spelling → canonical-name mapping (`prim:i32`, pointer spellings,
  template aliases), which you would have to implement yourself with
  `if constexpr`, traits or hand-written constants.

## One engine, distinct identities

All identity and digest functions share `util::canonical_hash` (the same engine
AMC calls `hash_text`). They differ in canonical input and domain, not in the
algorithm:

| API | Semantic | Matches AMC artifacts? |
|---|---|---|
| `model::canonical_type_id` | type identity | yes — `TypeID` |
| `poly::method_signature` | method identity | yes — `Function.signature` |
| `poly::interface_id` | interface identity | yes — `InterfaceRecord.id` |
| `model::canonical_layout_hash` | Canonical Layout Encoding v1 (cross-tool layout contract) | yes — `LayoutHash` |
| `model::structural_layout_hash` | ABIX-local structural digest | no — use `canonical_layout_hash` for the artifact contract |
| `model::abi_signature_hash` | call ABI (return/params + calling convention/qualifiers) | not `method_signature`; method identity ≠ call ABI |

Do not collapse these into one value. The same type can keep its `TypeID` while its layout changes across targets; a method's logical identity does not prove that two calling conventions are interchangeable.

`canonical_layout_hash` implements the versioned Canonical Layout Encoding v1 from `ABI-SPEC.md` §11.2: the encoding is name-independent, target-ABI-aware and byte-identical between AMC and ABIX. New artifacts select it with the optional `layout_encoding` section; artifacts without that section keep the legacy rule. `structural_layout_hash` remains an ABIX-local digest and is not the artifact `LayoutHash`.

`model::type_hash` and `model::signature_hash` were removed: use
`canonical_type_id` for type identity and `abi_signature_hash` for the call ABI.
The legacy `util::fnv1a` / `Value` / `Descriptor` / `ValueSet` family was also
removed; it was a second engine and was not used for identity.

## Three ways to build

1. **Hand-written only.** Depend on ABIX headers; write `Traits` and adapter
   tables by hand. Lightest dependency, most boilerplate.
2. **Generate once, vendor the output.** Run AMC in CI (or once locally), commit
   the generated `Traits`, thunks and `.abix`, and let day-to-day builds depend
   only on ABIX. Developers do not need Clang/LLVM, but the ABI snapshot can
   drift and must be regenerated when the source contract changes.
3. **AMC as a build-time dependency** (the default path). Most automated; AMC
   runs on the build machine or in CI and never ships in the runtime.

Use (1) or (2) when the goal is “a C++ project that does not install AMC”; use
(3) when you also need cross-language interop, layout validation and generated
adapters.

## Related

- [`interface.md`](interface.md) — the Interface contract, proxies and
  `signatures`.
- [`abic.md`](abic.md) — `[[interface]]` / `[[implements]]` build declarations.
- [`std.md`](std.md) — ABIX-STD boundary types such as `StringView`.
