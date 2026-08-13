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
- **RCU Non-Blocking Unload** — `dll_object` uses RCU (Read-Copy-Update) epoch-based reclamation with compiler-builtin atomics, allowing safe concurrent DLL unload without blocking active callers.
- **Lookup acceleration** — Three lookup policies (Linear, StaticHot, AdaptiveHot) adapt to different access patterns, with adaptive hot-cache learning from runtime call frequencies.

## Features

- **Stable Function Table** — `SKL_ABIX_DEFINE_TABLE(...)` macro generates a POD export table with `abi_get_table()` entry point
- **Signature Safety** — `fn_sig<T>` produces a unique compile-time hash for each function type, including calling convention information
- **Type-safe Smart Pointers** — `unique_dll_ptr`, `ref_dll_ptr`, `shared_dll_ptr`, `weak_dll_ptr`, `view_dll_ptr` for managing DLL-allocated resources with correct DLL-side deallocation
- **Cross-boundary Callbacks** — `function_dll<R(Args...)>` is an 8-byte closure that captures lambdas and invokes them across DLL boundaries
- **Version Tokens** — `SKL_ABIX_VERSION("1.0")` enables multiple implementations of the same named function to coexist
- **Calling Convention Awareness** — `dll_func_cc<C, Sig>` and `dll_func<Sig, C>` templates support `__cdecl`, `__stdcall`, `__fastcall`, and `__vectorcall`
- **RCU Non-Blocking Unload** — Thread-safe DLL unloading via RCU read-side critical sections (`try_enter_read`/`exit_read`) and compiler-builtin atomics (`_Interlocked*`/`__atomic_*`), zero `std::atomic` ABI risk
- **RCU Timeout Policies** — Three strategies for when the RCU grace period exceeds `ABIX_RCU_TIMEOUT_MS`: Safe (zombie + leak), ForceUnload (bypass RCU), and ForceLeak (detach + leak, opt-in via macro)
- **Timeout Check Modes** — Three zero/low-CPU check modes: Lazy (check on entry), Tick (host-driven), and OS Timer (kernel-level wait)
- **Pluggable Logging** — Compile-time removable logging with C-callback sink (`ABIX_LOG_*` macros), per-level disable, and ABI-safe `set_log_sink()` for production log platforms
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
| `unload()` | `bool` | Mark unloading → wait for readers → unload if no live handles (ref-count = 0) |
| `force_unload()` | `void` | Unload regardless of ref-count (bypasses RCU, caller must ensure safety) |
| `reload(path)` | `bool` | Unload and re-load a new DLL |
| `is_loaded()` | `bool` | Whether the module is loaded and not in the unloading state |
| `get_table()` | `const table*` | Get the export table pointer |
| `add_ref()` | `void` | Increment reference count |
| `release_ref()` | `void` | Decrement reference count |
| `ref_count()` | `uint32_t` | Current reference count |
| `try_enter_read()` | `bool` | Enter RCU read-side critical section (double-checked); returns `false` if unloading or zombie |
| `exit_read()` | `void` | Exit RCU read-side critical section |
| `begin_rcu_unload()` | `bool` | Mark unloading → wait for readers → unload or apply timeout policy; returns `true` on success |
| `set_timeout_policy(p)` | `void` | Set the RCU timeout policy (`Safe` / `ForceUnload` / `ForceLeak`) |
| `timeout_policy()` | `RCUTimeoutPolicy` | Get the current RCU timeout policy |

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
| `unloading` | DLL is currently unloading |

## Smart Pointers for DLL Resources

| Type | Semantics | Description |
|------|-----------|-------------|
| `unique_dll_ptr<T>` | Exclusive ownership | Single owner, calls DLL-side deleter on destruction |
| `ref_dll_ptr<T>` | Reference counting (non-atomic) | Single-thread shared ownership |
| `shared_dll_ptr<T>` | Atomic reference counting | Thread-safe shared ownership |
| `weak_dll_ptr<T>` | Weak reference | Non-owning observer for `shared_dll_ptr` |
| `view_dll_ptr<T>` | View reference | Non-owning observer for `ref_dll_ptr` |
| `fn_deleter<T>` | Custom deleter | Wraps a DLL destroy function for `std::unique_ptr` |

## Logging

ABIX provides a pluggable, compile-time removable logging system with zero ABI risk.

### Log Levels

| Level | Macro | Description |
|-------|-------|-------------|
| `Debug` | `ABIX_LOG_DEBUG(...)` | Verbose diagnostic information |
| `Info` | `ABIX_LOG_INFO(...)` | General operational messages |
| `Warning` | `ABIX_LOG_WARNING(...)` | Recoverable issues, degraded behavior |
| `Error` | `ABIX_LOG_ERROR(...)` | Critical failures, unrecoverable errors |

### Runtime Redirection

```cpp
void my_sink(skl::abix::LogLevel level, const char *message) {
    // Forward to spdlog, fmt, ELK, Splunk, etc.
    spdlog::log(static_cast<spdlog::level::level_enum>(level), message);
}
skl::abix::set_log_sink(my_sink);
```

### Compile-time Control

```cpp
#define ABIX_DISABLE_LOGGING               // Zero-overhead: all log code removed
#define ABIX_DISABLE_LOG_LEVEL_DEBUG      // Disable Debug-level only
#define ABIX_DISABLE_LOG_LEVEL_INFO       // Disable Info-level only
```

## RCU Timeout Policies

When `ABIX_RCU_TIMEOUT_ENABLE` is on (default) and the RCU grace period exceeds `ABIX_RCU_TIMEOUT_MS` (default: 5000ms), one of three strategies is applied:

| Strategy | Behavior | Availability | Default |
|----------|----------|-------------|---------|
| `Safe` | Mark zombie, abandon unload, DLL leaks but **never crashes** | Always | Default |
| `ForceUnload` | Bypass RCU, force `FreeLibrary`/`dlclose` — active callers **will crash** | Always | — |
| `ForceLeak` | Detach module, don't unload DLL, old objects safely leak | Requires `#define ABIX_ENABLE_FORCE_LEAK_POLICY` | — |

**Zombie State:** Under Safe/ForceLeak, the `dll_object` becomes a zombie:
- `is_loaded()` returns `false`
- `try_enter_read()` returns `false` (sets `call_error::unloading`)
- `load()` force-unloads the zombie and reloads fresh

## Timeout Check: Dual-Fuel (Time + Frames)

ABIX treats **wall-clock time** and **frame count** as two orthogonal fuel sources. Both are always available at runtime — no compile-time mode switch required.

- **Time fuel**: `get_tick_ms()` always returns the system wall-clock (no host cooperation needed).
- **Frame fuel**: `abix::tick(timestamp)` injects frame counts from the host loop (optional, zero overhead if unused).
- **Deadline check**: `wait_for_readers()` checks both `_timeout_ms` AND `_timeout_frames` every ~1M spin iterations. Whichever deadline arrives first triggers the timeout.

This design lets the host "refuel" both sources simultaneously without forcing a binary choice. The host's scheduling system is never replaced; ABIX only provides precise deadline judgment at the critical point.

## Configuration Quick Reference

```cpp
// ==================== 1. Logging ====================
// #define ABIX_DISABLE_LOGGING
// #define ABIX_DISABLE_LOG_LEVEL_DEBUG

// ==================== 2. Timeout Policy ====================
#define ABIX_RCU_TIMEOUT_ENABLE     1
#define ABIX_RCU_TIMEOUT_MS         5000   // Compile-time fallback default
#define ABIX_RCU_TIMEOUT_FRAMES_DEFAULT 0  // Compile-time fallback default (0 = disabled)
// #define ABIX_ENABLE_FORCE_LEAK_POLICY

// ==================== 3. Lazy Starvation Guard ====================
#define ABIX_LAZY_STARVATION_GUARD  ABIX_LAZY_STARVATION_GUARD_TICK  // 0=Off | 1=Tick (default) | 2=Idle
// #define ABIX_ENABLE_IDLE_BACKGROUND_THREAD   // Required for Level 2

// Runtime configuration:
// dll_object lib(RCUTimeoutConfig{5000, 300});  // 5s or 300 frames, whichever first
// lib.set_timeout_policy(RCUTimeoutPolicy::ForceUnload);
```

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
│   ├── obj_dll.h              # dll_object: DLL load/unload/ref-count, RCU read/write sides, timeout policies
│   ├── fn_dll.h               # dll_func / dll_func_cc: typed function handles
│   ├── fn_sig.h               # fn_sig<T>: compile-time signature hashing
│   ├── type_sig.h             # type_sig<T>: compile-time type hashing
│   ├── search.h               # find_index, lookup_linear: table search
│   ├── cache.h                # static_hot_cache, adaptive_hot_cache: lookup acceleration
│   ├── log.h                  # Logging: pluggable C-callback sink, per-level compile-time disable
│   ├── rcu_config.h           # RCUTimeoutConfig: runtime timeout settings (ms + frames)
│   ├── rcu_timeout.h          # RCU timeout: tick source, OS timer, starvation guard, platform abstraction
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

Tests use [Catch2](https://github.com/catchorg/Catch2), driven by `main.cpp`. **All 31 test cases pass**, covering core functionality, edge cases, resource management, cross-compiler compatibility, closed-source contracts, timeout policies, and performance benchmarks.

### Test Categories

| Category | Tests | Tags | Coverage |
|----------|-------|------|----------|
| **Basic** | 1, 3, 6, 7, 9 | `[basic]`, `[typesafe]`, `[callback]`, `[version]`, `[reload]` | Linear scan, signature hash, `function_dll` callback, version token coexistence, hot-reload |
| **Resource** | 4, 5, 11, 12 | `[resource]` | `unique_dll_ptr`/`ref_dll_ptr`/Socket/string cross-boundary lifecycle |
| **Cross-Compiler/CRT** | 2, 13, 17 | `[cross]` | GCC/Clang/MSVC interop; MinGW host + MSVC DLL with no heap conflict |
| **Closed-Source** | 15, 16, 18 | `[closed]` | Private field hiding, dual offset validation, breaking version change interception |
| **Log & Config** | 19, 20, 21, 22, 23, 25, 27, 28 | `[log]`, `[config]`, `[tick]` | Log redirection, buffer truncation, `RCUTimeoutConfig`, policy switching, `tick()` injection |
| **RCU Timeout & Zombie** | 24, 26, 29, 30, 31 | `[rcu]`, `[zombie]`, `[policy]`, `[timeout]`, `[tick]` | Safe/ForceUnload/ForceLeak timeout triggers, zombie recovery, frame-driven timeout |
| **Reflection** | 14 | `[refl]` | Static/dynamic reflection (FP/Any/Registry/TypeInfo/StaticRefl) integration |
| **Edge Cases** | 10 | `[edge]` | Not found, call after unload, ref-count reject, calling convention mismatch |
| **Performance** | **8** | `[perf]` | **Linear/StaticHot/AdaptiveHot** with 100% hot, cold start, hot drift, 80/20 distributions |

> **Performance notes**: Test 8 runs under Release optimization, uses `volatile` anti-optimization, and auto-validates speedup ratios. Example results (from logs):
> - **100% hot**: Linear 245.1ms, Static 42.6ms, Adaptive 42.1ms → **5.8× speedup**
> - **80/20 distribution**: Linear 229.8ms, Static 66.0ms, Adaptive 66.0ms → **3.5× speedup**
> - **Hot drift**: Adaptive 68.7ms vs Static 100.4ms → Adaptive is 46% faster

### Build & Run

```bash
# Build all tests (default GCC/Clang + Ninja or MinGW Makefiles)
cd tools
python build.py

# Also build MSVC cross-compiler variants (for Test 2/13/17)
python build.py --with-msvc

# Run existing tests only (skip rebuild)
python build.py --run-only
```

### Expected Results & Log Interpretation

Running `python build.py --run-only` produces a summary like:

```
All tests passed (31 assertions in 31 test cases)
```

Each test case outputs detailed steps prefixed with `[log]`, for example:

- **Test 8**: Prints per-strategy timings and speedup ratios, auto-checks thresholds (hardware variance may emit `WARN` instead of failing).
- **Test 15~18**: Prints closed-source contract hash and offset validation results; if a breaking change is intercepted, explicitly outputs `[PASS]` with hash mismatch details.
- **Test 24/26/29/30**: Simulates RCU timeout, prints zombie creation and `load()` recovery, verifying all three Safe/ForceUnload/ForceLeak timeout trigger paths.
- **Test 31**: Frame-driven timeout, pushing `tick()` from a separate thread and verifying the frame-count deadline triggers.

All tests have no external network dependencies. DLL files reside in `plugins/` or `variants/` directories. If certain cross-compiler variants are missing, the corresponding tests skip automatically and emit `WARN` (without failing the overall run).

### Debugging Tips

- Performance data is larger under Debug builds; use Release builds for realistic performance numbers.
- Cross-compiler tests require running `tools/build_msvc_variants.py` first to generate MSVC variant DLLs, otherwise those tests are skipped.
- If a test fails, the log clearly identifies the failure location (`REQUIRE` expression and line number); check `build/test.log` for diagnosis.

## Future Plans

- **AMC Integration** — Combine with the planned Meta Object Compiler to auto-generate `SKL_ABIX_DEFINE_TABLE` entries from C++ attributes
- **Serialization Support** — Extend `type_sig` and type tags to support serialization of complex types across DLL boundaries
- **Network Transport** — Enable remote function calls through the same stable table format

## License

Apache License, Version 2.0. See [LICENSE](https://www.apache.org/licenses/LICENSE-2.0) for the full text.

---

Copyright 2026 [Sukanle](https://github.com/Sukanle)