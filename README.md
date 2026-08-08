<div align="center">

# ABIX

## A cross-DLL SKL_ABIX-safe function calling library with signature verification, versioning, hot-reload, and lookup acceleration.

![License](https://img.shields.io/badge/License-Apache_2.0-blue)
![Language](https://img.shields.io/badge/Language-C/C++-red)

English | [中文](README_ZH.md)
</div>

## Overview

ABIX (SKL_ABIX Interface) is a lightweight C++ library that enables robust, type-safe function calls across DLL/shared-library boundaries. It addresses the fundamental fragility of `GetProcAddress`/`dlsym` by introducing a stable function table, compile-time signature hashing, and optional versioning — ensuring that DLL consumers and providers never silently mismatch.

Key design goals:

- **SKL_ABIX-stable tables** — Plain-POD `entry` and `table` structs with a fixed magic number and format version, safe across compiler versions, CRT variants, and calling conventions.
- **Compile-time signature hashing** — Every function signature is hashed at compile time via FNV-1a; a mismatch is detected at resolve time, never at call time.
- **Version evolution** — Multiple versions of the same function name can coexist in a single table, enabling forward-compatible API evolution.
- **Hot-reload** — Integer handle IDs remain stable across unload/reload cycles, enabling zero-downtime DLL upgrades.
- **Lookup acceleration** — Three lookup policies (Linear, StaticHot, AdaptiveHot) adapt to different access patterns, with adaptive hot-cache learning from runtime call frequencies.

## Features

- **Stable Function Table** — `SKL_ABIX_DEFINE_TABLE(...)` macro generates a POD export table with `abi_get_table()` entry point
- **Signature Safety** — `fn_sig<T>` produces a unique compile-time hash for each function type, including calling convention information
- **Type-safe Smart Pointers** — `unique_dll_ptr`, `ref_dll_ptr`, `shared_dll_ptr`, `weak_dll_ptr`, `view_dll_ptr` for managing DLL-allocated resources with correct DLL-side deallocation
- **Cross-boundary Callbacks** — `function_dll<R(Args...)>` is an 8-byte closure that captures lambdas and invokes them across DLL boundaries
- **Version Tokens** — `SKL_ABIX_VERSION("1.0")` enables multiple implementations of the same named function to coexist
- **Calling Convention Awareness** — `dll_func_cc<C, Sig>` and `dll_func<Sig, C>` templates support `__cdecl`, `__stdcall`, `__fastcall`, and `__vectorcall`
- **Dynamic Reflection Integration** — Built on the Reflection library, supporting runtime type queries and POD field access via `make_pod_type_info` / `make_offset_field`
- **Lookup Policy** — Three strategies for function table lookup, with adaptive hot-cache that automatically promotes frequently-called entries

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

> [!NOTE]
> - [API.md](doc/api.md)

## Registration Macros

### Export Table Definition

| Macro | Description |
|-------|-------------|
| `SKL_ABIX_DEFINE_TABLE(...)` | Define the export table with a list of entries |
| `SKL_ABIX_ENTRY(name, func)` | Register a function with default calling convention (Cdecl) and version 0 |
| `SKL_ABIX_ENTRY_CC(name, func, cctype)` | Register a function with a specific calling convention |
| `SKL_ABIX_ENTRY_FULL(name, func, cctype, ver, flg)` | Register a function with full parameters (calling convention, version, flags) |
| `SKL_ABIX_VERSION("1.0")` | Create a version token for versioned function entries |

### Type Tag Registration

| Macro | Description |
|-------|-------------|
| `SKL_ABIX_TYPE_TAG(T, tag)` | Register a custom hash tag for user type `T` (delegates to `STATIC_TYPE_TAG`) |

### Calling Convention Pickers

| Macro | Description |
|-------|-------------|
| `SKL_ABIX_CCPICK(Cdecl)` | Select `Cdecl` calling convention |
| `SKL_ABIX_CCPICK(Stdcall)` | Select `Stdcall` calling convention |

## Core Types

### `entry` — Function Table Entry

```cpp
struct entry {
    const char *name;       // Function name
    sig_t sig;              // Signature hash (compile-time FNV-1a)
    version_t version;      // Version token (0 = unversioned)
    uintptr_t fnptr;        // Function pointer
    name_hash_t name_hash;  // Name hash (FNV-1a 32-bit)
    uint32_t flags;         // Flags (SKL_ABIX_ENTRY_HOT = 0x1)
};
```

### `table` — Export Table

```cpp
struct table {
    uint32_t count;          // Number of entries
    uint32_t magic;          // SKL_ABIX_TABLE_MAGIC (0xAB1E7A81)
    uint32_t format_version; // SKL_ABIX_TABLE_FORMAT_VERSION (1)
    uint32_t reserved;       // Reserved for future
    const entry *entries;    // Pointer to entry array
};
```

### `dll_object` — DLL Module

| Method | Returns | Description |
|--------|---------|-------------|
| `load(path)` | `bool` | Load a DLL/SO and validate its export table |
| `unload()` | `bool` | Unload if no live handles (ref-count = 0) |
| `force_unload()` | `void` | Unload regardless of ref-count |
| `reload(path)` | `bool` | Unload and re-load a new DLL |
| `is_loaded()` | `bool` | Whether the module is loaded |
| `get_table()` | `const table*` | Get the export table pointer |
| `add_ref()` | `void` | Increment reference count |
| `release_ref()` | `void` | Decrement reference count |
| `ref_count()` | `uint32_t` | Current reference count |

### `call_error` — Error Codes

| Value | Description |
|-------|-------------|
| `none` | No error |
| `not_loaded` | DLL is not loaded |
| `not_found` | Function name not found in table |
| `sig_mismatch` | Signature hash mismatch |
| `version_mismatch` | Version token mismatch |
| `stale_handle` | Cannot unload while handles exist |
| `table_changed` | Table changed after resolve |
| `invalid` | Invalid handle |
| `load_failed` | DLL load failed |

## Smart Pointers for DLL Resources

| Type | Semantics | Description |
|------|-----------|-------------|
| `unique_dll_ptr<T>` | Exclusive ownership | Single owner, calls DLL-side deleter on destruction |
| `ref_dll_ptr<T>` | Reference counting (non-atomic) | Single-thread shared ownership |
| `shared_dll_ptr<T>` | Atomic reference counting | Thread-safe shared ownership |
| `weak_dll_ptr<T>` | Weak reference | Non-owning observer for `shared_dll_ptr` |
| `view_dll_ptr<T>` | View reference | Non-owning observer for `ref_dll_ptr` |
| `fn_deleter<T>` | Custom deleter | Wraps a DLL destroy function for `std::unique_ptr` |

## Lookup Policies

ABIX supports three lookup policies that can be selected at compile time via the `dll_func` template parameter:

| Policy | Description | Best For |
|--------|-------------|----------|
| `Linear` | Full table linear scan (default) | Small tables, cold start |
| `StaticHot` | Pre-registered hot-cache | Known hot entries fixed at compile time |
| `AdaptiveHot` | Self-learning hot-cache | Dynamic workloads with shifting hot spots |

The `AdaptiveHot` policy automatically samples call frequencies and promotes entries that exceed a configurable threshold to the hot cache.

## Directory Structure

```
ABIX/
├── abix/                      # Core library headers
│   ├── abix.hpp               # Main entry header (includes all)
│   ├── config.h               # Platform detection, macros, AbiLookupPolicy
│   ├── type.h                 # Core types: entry, table, type aliases
│   ├── register.h             # SKL_ABIX_DEFINE_TABLE, SKL_ABIX_ENTRY macros
│   ├── obj_dll.h              # dll_object: DLL load/unload/ref-count
│   ├── fn_dll.h               # dll_func / dll_func_cc: typed function handles
│   ├── fn_sig.h               # fn_sig<T>: compile-time signature hashing
│   ├── type_sig.h             # type_sig<T>: compile-time type hashing
│   ├── search.h               # find_index, lookup_linear: table search
│   ├── cache.h                # static_hot_cache, adaptive_hot_cache: lookup acceleration
│   ├── function.h             # function_dll: 8-byte cross-boundary closure
│   ├── dll.h                  # Aggregator: obj_dll + fn_dll
│   ├── dll_ptr.h              # Aggregator: all smart pointer types
│   ├── refl.h                 # Dynamic reflection integration helpers
│   └── dll_ptr/               # Smart pointer implementations
│       ├── unique_ptr.h       # unique_dll_ptr<T>, fn_deleter<T>
│       ├── ref_ptr.h          # ref_dll_ptr<T>, view_dll_ptr<T>
│       ├── shared_ptr.h       # shared_dll_ptr<T>
│       ├── weak_ptr.h         # weak_dll_ptr<T>
│       └── view_ptr.h         # view_dll_ptr<T>
├── dlls/                      # Example/test DLL implementations
│   ├── plugin_types.h         # Shared type tags for cross-DLL types
│   ├── math_dll.cpp           # Basic math functions
│   ├── version_dll.cpp        # Versioned log function (v1.0/v2.0)
│   ├── sigcheck_dll.cpp       # Signature mismatch test
│   ├── resource_dll.cpp       # Resource lifecycle (create/destroy)
│   ├── callback_dll.cpp       # Cross-boundary callback test
│   ├── reload_dll_a.cpp       # Hot-reload variant A
│   ├── reload_dll_b.cpp       # Hot-reload variant B
│   ├── edge_dll.cpp           # Edge case test
│   ├── edge_stdcall_dll.cpp   # __stdcall calling convention test
│   ├── hotcache_dll.cpp       # Lookup policy benchmark DLL
│   ├── closed_dll.cpp         # Closed-source simulation
│   └── closed_dll_v2.cpp      # Closed-source simulation v2
├── tools/                     # Build and code generation scripts
│   ├── build.py               # Main build script
│   ├── build_variants.py      # Cross-compiler variant build
│   ├── build_msvc_variants.ps1 # MSVC variant build
│   └── gen_hotcache_dll.py    # hotcache_dll.cpp code generator
├── main.cpp                   # Test suite (Catch2)
└── CMakeLists.txt             # Build configuration
```

## Supported Platforms & Toolchains

| Platform | Compiler | Minimum Version | Status |
|----------|----------|-----------------|--------|
| Windows  | MSVC     | VS 2022 (17.0+) | ✓ |
| Windows  | MinGW-w64 (GCC) | 13.0+ | ✓ |
| Windows  | Clang-cl | 17.0+ | ✓ |
| Linux    | GCC      | 13.0+ | ✓ |
| Linux    | Clang    | 17.0+ | ✓ |

**Requirements:** C++17 or later (C++20 recommended for `consteval` support). The Reflection library is included as a git submodule.

## Testing

Tests use [Catch2](https://github.com/catchorg/Catch2), driven by `main.cpp` covering:

| Test | Tags | Coverage |
|------|------|----------|
| Basic math linear scan | `[basic]` | Load DLL, resolve functions via linear scan, call through integer handles |
| Cross-compiler variant | `[cross]` | Same function name yields consistent results across g++/clang/MSVC builds |
| Signature hash check | `[typesafe]` | Signature mismatch rejected at lookup, no silent type conversion |
| Unique resource takeover | `[resource]` | `unique_dll_ptr`/`unique_ptr` invoke DLL release function on destruction |
| ref_dll_ptr refcount | `[resource]` | Non-atomic ref-count shared resource, released on last destruction |
| ABI function callback | `[callback]` | `function_dll` captures lambda and invokes across boundary |
| Version evolution | `[version]` | v1.0/v2.0 version tokens for same log interface coexist |
| Lookup policy benchmark | `[perf]` | Linear/StaticHot/AdaptiveHot strategies with real-world distributions |
| Hot-reload | `[reload]` | Handle ID unchanged after unload A / load B, return value updates |
| Edge handling | `[edge]` | Not found, call after unload, ref-count reject, calling convention mismatch |

```bash
# Build and run tests
cd tools
python build.py                    # GCC/Clang (Ninja or MinGW Makefiles)
python build.py --with-msvc        # Also build MSVC cross-compiler variants
python build.py --run-only         # Run tests only, skip build
```

## Future Plans

- **AMC Integration** — Combine with the planned Meta Object Compiler to auto-generate `SKL_ABIX_DEFINE_TABLE` entries from C++ attributes
- **Serialization Support** — Extend `type_sig` and type tags to support serialization of complex types across DLL boundaries
- **Network Transport** — Enable remote function calls through the same stable table format

## License

Apache License, Version 2.0. See [LICENSE](https://www.apache.org/licenses/LICENSE-2.0) for the full text.

---

Copyright 2026 [Sukanle](https://github.com/Sukanle)