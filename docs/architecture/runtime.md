# Runtime Overview

<p align="center">
  <a href="runtime_zh.md">中文</a> · English
</p>

<details>

<summary>Contents</summary>

- [Responsibilities](#responsibilities)
- [Registry](#registry)
- [Three metadata modes](#three-metadata-modes)
- [DLL function table](#dll-function-table)
- [DLL Loading Sequence](#dll-loading-sequence)
- [Function Invocation Sequence](#function-invocation-sequence)
- [Quick Start](#quick-start)
- [API reference](#api-reference)
- [Performance](#performance)

</details>

The ABIX runtime consumes ABI metadata and keeps the execution path native. It
does not interpret compatible calls.

## Responsibilities

```mermaid
graph LR
    A[discover] --> B[verify] --> C[identify] --> D[bind] --> E[adapt] --> F["native call"]
```

The runtime may participate in all of these, but after binding, a compatible
call goes through the native ABI with no per-call ABI machinery.

## Registry

`RuntimeRegistry` holds the ABI facts for all loaded modules and validates them
at load time:

* canonical `TypeDesc` / `TypeLayout` records are the one ABI truth;
* a shared `TypeID` must carry the same `LayoutHash` within one module version
  (compatible duplicates are deduplicated; conflicts are rejected), while a
  newer version may carry a different layout as a separate entry;
* lookups are by `TypeID` (`find_by_id` for the newest version,
  `find_type(id, version)` for a pinned one, `type_of<T>()`); name lookup is
  intentionally diagnostic-only and returns `nullptr` in the compact build.

## Three metadata modes

```mermaid
graph TD
    A[".abix (full artifact, names included)"] -->|projection| B["Metadata Region (embedded, pointer-free, mmap-able)"]
    B -->|materialization| C["Runtime Descriptor (pointer-rich, hot path)"]
```

* The toolchain emits the Region into the `.abix.metadata` section of a
  generated header; an offline tool can scan a binary without loading it.
* `MaterializedModule` projects a Region back into a pointer-rich
  `ModuleDescriptor`, so a registry can be driven entirely by the metadata
  image — no compile-time descriptor arrays required.

See [`metadata_modes.md`](../abix/metadata_modes.md) for the design.

## DLL function table

The stable public call ABI is the DLL export table: a flat, versioned table of
function entries. Metadata is an additional, explicit layer on top of it.

## DLL Loading Sequence

```mermaid
sequenceDiagram
    participant App
    participant Loader
    participant DLL
    participant ABIX

    App->>Loader: load(path)
    Loader->>DLL: LoadLibrary
    DLL-->>Loader: module handle
    Loader->>ABIX: locate metadata
    ABIX-->>Loader: ABI information
    Loader-->>App: loaded module
```

The loader locates the ABIX Metadata Region embedded in the binary, materializes
it into a `ModuleDescriptor`, and registers it in the `RuntimeRegistry`. After
this point all type lookups are resolved from the registry, not re-parsed from
the section.

## Function Invocation Sequence

```mermaid
sequenceDiagram
    participant User
    participant dll_func
    participant FunctionTable
    participant NativeFunction

    User->>dll_func: operator()(args...)
    dll_func->>FunctionTable: read function pointer
    FunctionTable-->>dll_func: function pointer
    dll_func->>NativeFunction: direct call(args...)
    NativeFunction-->>User: result
```

After binding, the call goes through a plain native function pointer. There is
no per-call type lookup, argument marshalling, or dynamic dispatch — the runtime
is not involved in the hot path.

## Quick Start

### DLL Side (Provider)

```cpp
#include "abix/abix.hpp"

extern "C" int add(int a, int b) { return a + b; }
extern "C" double multiply(double a, double b) { return a * b; }

SKL_ABIX_DEFINE_TABLE(
    SKL_ABIX_ENTRY("add", add),
    SKL_ABIX_ENTRY("multiply", multiply),
)
```

### Host Side (Consumer)

```cpp
#include "abix/abix.hpp"

using namespace skl::abix;

dll_object lib;
lib.load("math_dll.dll");

auto add = dll_func<int(int, int)>(lib, "add");
if (add.valid()) {
    int result = add(2, 3);  // 5
}

auto mul = dll_func<double(double, double)>(lib, "multiply");
if (mul.valid()) {
    double result = mul(1.5, 4.0);  // 6.0
}
```

See [`api.md`](../abix/api.md) for the runtime API.

## API reference

The full runtime API — `entry`, `table`, `dll_object`, `call_error`, smart
pointers for DLL resources, logging, RCU timeout policies, typed function
handles and signature hashing — is documented in
[`api.md`](../abix/api.md).

## Performance

Runtime binding and metadata projection happen at initialization. Boundary
overhead and lookup benchmarks are documented in [`benchmark.md`](../benchmark/benchmark.md).
