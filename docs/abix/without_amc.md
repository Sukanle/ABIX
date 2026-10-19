# Using ABIX without AMC

<p align="center">
  <a href="without_amc_zh.md">中文</a> · English
</p>

ABIX and AMC have different jobs:

- **ABIX** (`ABIX/`) is a header-only runtime layer. It contains no `.cpp` files and never includes `AMC/...`. `libabix-runtime` is a CMake `INTERFACE` target that only adds the include root.
- **AMC** is a build-time toolchain (Clang/LLVM + `toml++`) that extracts C++ semantics, computes canonical ABI identities, writes `.abix` artifacts and generates thunks, adapters and C++ projections.

A C++-only project can therefore depend on ABIX alone. AMC is never a runtime dependency — even when it is used, the produced binaries do not link it — and a hand-written C++ contract does not need it at build time either.

The trade-off is that the contract becomes manual: you write the adapter tables and `Traits`, and you supply the TypeIDs instead of letting Clang extract them.

## What works with ABIX alone

| Capability                               | Header                                                                           | AMC required?                                                                   |
| ---------------------------------------- | -------------------------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| Interface identity and method signatures | `ABIX/Interface/Contract.h`                                                      | No — `interface_id`, `method_signature` and `canonical_type_id` are `constexpr` |
| Hand-written `Traits` and proxies        | `ABIX/Interface/Proxy.h`                                                         | No — `InterfaceProxy<Traits>` is header-only                                    |
| In-process register / resolve / call     | `Registry.h`, `Ref.h`, `Proxy.h`                                                 | No                                                                              |
| Dynamic-library discovery and leases     | `Module.h`, `ModuleRegistry.h`, `Discovery.h`, `Lease.h`                         | No — write the `ModuleManifest` by hand and use `SKL_ABIX_DEFINE_MODULE_QUERY`  |
| Owner ABI                                | `Owner.h`                                                                        | No                                                                              |
| Runtime table / descriptors              | `ABIX/Runtime/Registry.h`, `ABIX/Metadata/Descriptor.h`, `SKL_ABIX_DEFINE_TABLE` | No — the structures are public and can be written by hand                       |
| ABIX's own type/layout digests           | `ABIX/Model/ABI.h`, `ABIX/Model/Compat.h`                                        | No — `canonical_type_id`, `canonical_layout_hash`, `abi_signature_hash`         |

A minimal C++-only declaration:

```cpp
#include "ABIX/Interface/Interface.h"

using namespace skl::abix;
using namespace skl::abix::poly;

constexpr model::TypeId i32 = model::canonical_type_id("prim:i32");

struct ShapeTraits {
    static constexpr util::StaticArray signatures = {method_signature("area", i32, nullptr, 0)};
    static constexpr InterfaceId id = interface_id("Shape", 1, signatures);
    static constexpr uint32_t version = 1;
    static constexpr size_t slots = 1;
    static constexpr uint32_t area_slot = 0;
};
class Shape : public InterfaceProxy<ShapeTraits> {
    using Base = InterfaceProxy<ShapeTraits>;

public:
    using Base::Base;
    int area() const noexcept { return this->template call<int (*)(const void *)>(ShapeTraits::area_slot); }
};

// Provider side: adapters receive the object handle first.
constexpr model::TypeId square_id{0x51, 0x52};   // the concrete type's TypeID
int square_area(const void *self);
const InterfaceMethod square_methods[] = {
    {ShapeTraits::signatures[0], interface_entry(&square_area), 0, 0},
};
InterfaceRegistry<4> registry;
registry.add({ShapeTraits::id, square_id, square_methods, 1, 1, 0});
```

No AMC-generated file is involved. For a shared library, expose the same information through a hand-written `ModuleManifest` and `SKL_ABIX_DEFINE_MODULE_QUERY`; consumers use `discover_module` and `ModuleRegistry` as usual.

## STL types: allowed inside one C++ toolchain, forbidden at a foreign boundary

The Interface layer never inspects a parameter's type: `interface_entry` stores a function pointer and `call<Fn>` invokes it. An adapter can therefore use C++ STL types directly when the entire contract stays inside one C++ toolchain — the case where Interface is simply a vtable replacement.

This is safe only when **all** of the following hold:

- provider and consumer are C++ and share the compiler and standard-library ABI (`_GLIBCXX_USE_CXX11_ABI`, the MSVC runtime, debug/release CRT, iterator debug level, exception/RTTI settings);
- the same C++ standard and allocator are used;
- the contract is not a plugin boundary that may be built by a different toolchain, and not a cross-language boundary;
- both sides accept the STL type's lifetime rules (a `std::string` passed by value or `const&` is valid for the call; a returned one is owned by the caller).

```cpp
struct Square {
    std::string name;
    // ...
};
using NameFn = const std::string &(*)(const void *);
const std::string &square_name(const void *self) {
    return static_cast<const Square *>(self)->name;
}
// registration and `proxy.call<NameFn>(slot)` work as usual.
```

The signature is still a contract tag, not a layout check: use the same TypeID for `std::string` on both sides (for example `canonical_type_id("std::string")`, or your own fixed value). ABIX does not verify the STL layout, so the same-toolchain assumption is what makes the call well-defined. Write the `Traits` and proxy by hand (or let AMC generate them), and pass the STL parameter's TypeID to `method_signature`.

**Do not use STL types across a plugin, foreign-language or foreign-toolchain boundary.** A different `std::string` / `std::vector` ABI, allocator, exception model or RTTI setting makes the call undefined, and an object created in one module must not be destroyed in another with a different allocator. Use the ABIX-STD boundary types (`StringView`, `SequenceView<T>`, `Opaque<T>`) or explicit marshalling instead — the same rule as `[[adapt]]`: a native STL parameter needs a declared boundary contract.

## What AMC provides

Without AMC you lose the automatic parts:

- `[[interface]]` / `[[implements]]` / `[[expose]]` / `[[adapt]]` driven generation;
- Clang AST extraction of types, fields, layouts and function signatures;
- `.abix` artifacts, the Metadata Region, `TypeTraits`, layout validation, compatibility reports and generated adapters;
- Rust / Zig / Lua projections;
- the C++-spelling → canonical-name mapping (`prim:i32`, pointer spellings, template aliases), which you would have to implement yourself with `if constexpr`, traits or hand-written constants.

## One engine, distinct identities

All identity and digest functions share `util::canonical_hash` (the same engine AMC calls `hash_text`). They differ in canonical input and domain, not in the algorithm:

| API                            | Semantic                                                  | Matches AMC artifacts?                             |
| ------------------------------ | --------------------------------------------------------- | -------------------------------------------------- |
| `model::canonical_type_id`     | type identity                                             | yes — `TypeID`                                     |
| `poly::method_signature`       | method identity                                           | yes — `Function.signature`                         |
| `poly::interface_id`           | interface identity                                        | yes — `InterfaceRecord.id`                         |
| `model::canonical_layout_hash` | Canonical Layout Encoding v1 (cross-tool layout contract) | yes — `LayoutHash`                                 |
| `model::abi_signature_hash`    | call ABI (return/params + calling convention/qualifiers)  | not `method_signature`; method identity ≠ call ABI |

Do not collapse these into one value. The same type can keep its `TypeID` while its layout changes across targets; a method's logical identity does not prove that two calling conventions are interchangeable.

`canonical_layout_hash` implements the versioned Canonical Layout Encoding v1 from `ABI-SPEC.md` §11.2: the encoding is name-independent, target-ABI-aware and byte-identical between AMC and ABIX. Every artifact carries the `layout_encoding` section and a reader recomputes each `LayoutHash`; there is no legacy layout rule.

`model::type_hash` and `model::signature_hash` were removed: use `canonical_type_id` for type identity and `abi_signature_hash` for the call ABI. The legacy `util::fnv1a` / `Value` / `Descriptor` / `ValueSet` family was also removed; it was a second engine and was not used for identity.

## Three ways to build

1. **Hand-written only.** Depend on ABIX headers; write `Traits` and adapter tables by hand. Lightest dependency, most boilerplate.
2. **Generate once, vendor the output.** Run AMC in CI (or once locally), commit the generated `Traits`, thunks and `.abix`, and let day-to-day builds depend only on ABIX. Developers do not need Clang/LLVM, but the ABI snapshot can drift and must be regenerated when the source contract changes.
3. **AMC as a build-time dependency** (the default path). Most automated; AMC runs on the build machine or in CI and never ships in the runtime.

Use (1) or (2) when the goal is "A C++ project that does not install AMC" ; use (3) when you also need cross-language interop, layout validation and generated adapters.

## Related

- [`interface.md`](interface.md) — the Interface contract, proxies and `signatures`.
- [`abic.md`](abic.md) — `[[interface]]` / `[[implements]]` build declarations.
- [`std.md`](std.md) — ABIX-STD boundary types such as `StringView`.
