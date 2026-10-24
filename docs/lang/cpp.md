# C++

<p align="center">
  <a href="cpp_zh.md">中文</a> · English
</p>

<details>
<summary>Contents</summary>

- [Role](#role)
- [Frontend](#frontend)
- [Generated projection](#generated-projection)
- [Interface layer](#interface-layer)
- [Example](#example)
- [Limits and notes](#limits-and-notes)

</details>

C++ is ABIX's first host language and the reference provider. It uses Clang/LLVM to extract ABI facts from the AST and generates a C++17 projection with compile-time layout checks.

## Role

- `amc-cpp` is both a frontend and a backend provider (`amc --describe-language cpp` reports `frontend backend`).
- The frontend parses the configured translation units with Clang; AMC records types, fields, functions, symbols, layout, target facts, ownership and error contracts into the artifact.
- The backend emits `amc_generated.hpp` and `amc_abi_check.hpp`.
- C++ is also the implementation language of ABIX itself and of the AMC core.

## Frontend

`.abic.toml` selects `language = "cpp"` and lists the files and exported symbols:

```toml
[[import]]
language = "cpp"
flags = ["-std=c++17"]
files = ["demo_api.hpp"]
symbols = ["DemoVec2", "demo_add", "demo_div"]
```

The frontend and AMC handle templates, `std::` types, attributes, `[[interface]]` / `[[implements]]`, `[[owned]]` / `[[contract]]`, `[[instantiate]]`, `[[view]]`, `[[expose]]` and the error boundary. See [`abic.md`](../abix/abic.md).

## Generated projection

```bash
amc generate build/package.abix -l cpp -o gen/
```

`amc_generated.hpp` contains layout-compatible C++ types and typedefs, ABI constants, ownership and error-boundary helpers, and metadata-region accessors. For an interface contract it also emits `k<Name>Interface`, `k<Name>Version`, the `<Name>_<method>_fn` typedefs, a `Traits` struct with a typed zero-based `Slots` table and expected signatures, and, when every method takes the object handle first, an `UniqueInterfaceProxy`-derived proxy.

`amc_abi_check.hpp` is the compile-time layout check. Include it after the native declarations it names:

```cpp
#include "demo_api.hpp"
#include "amc_abi_check.hpp"
```

It asserts `sizeof`, `alignof`, per-field `offsetof` and per-field width. Bit-fields and implicitly named types are skipped because `offsetof` / `sizeof` are not defined for them.

## Interface layer

The C++ interface layer is header-only and lives in [`SKL/ABIX/Interface/`](../../ABIX/Interface/):

- `Contract.h`: `InterfaceId`, `method_signature`, `interface_id`, `InterfaceMethod` and `InterfaceImplementation`.
- `Proxy.h`: `UniqueInterfaceProxy<Traits>`; a generated or hand-written proxy gives `shape.area()`.
- `Registry.h` / `Ref.h`: in-process registration and one-time resolution.
- `Module.h` / `ModuleRegistry.h` / `Lease.h` / `Discovery.h`: dynamic module manifests and leases.
- `Owner.h`: cross-module ownership.
- `Declaration.h`: declaration sugar (`SKL_ABIX_INTERFACE_DECLARE`) and version packing helpers.

A provider keeps its classes, virtual functions and layout. The adapter is the only bridge, and the interface never feeds `TypeID` / `LayoutHash` / `ABIHash`.

## Example

Build the interop demo and generate its C++ projection:

```bash
cmake -B build/Release -DCMAKE_BUILD_TYPE=Release -G Ninja -S .
cmake --build build/Release --parallel

./build/Release/bin/amc build -c AMC/tests/interop/interop_demo.abic.toml -B build/demo
./build/Release/bin/amc generate build/demo/interop_demo.abix -l cpp -o build/demo/gen
```

The generated header can then be included by a C++ consumer; `amc_abi_check.hpp` fails the build if the provider's layout drifted.

## Limits and notes

- C++ STL types must not cross the boundary raw; declare a `[[view]]`, `[[instantiate]]` or `[[expose]]` mapping, or use an ABIX-STD contract.
- The recorded ABI is target-specific. Use the same target, compiler ABI and calling convention on both sides.
- The interface layer does not require inheritance or virtual functions; a plain `struct` works.
- The generated C++ header targets C++17.
- The Clang frontend requires LLVM / Clang development libraries; the Rust and Zig providers do not.
