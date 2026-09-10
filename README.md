<div align="center">

# ABIX

## A cross-DLL SKL_ABIX-safe function calling library with signature verification, versioning, hot-reload, and automatic lookup acceleration.

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
- **RCU Non-Blocking Unload** — Global `rcu_domain` (Epoch-Based Reclamation), `dll_object` delegates to a global domain via `enter_read()`/`exit_read()` with compiler-builtin atomics, allowing safe concurrent DLL unload without blocking active callers.
- **Lookup acceleration** — Automatic lookup strategy: linear scan for small tables (< 64 entries), HashIndex for large tables (≥ 64 entries), with zero ABI format changes.

## Features

- **Stable Function Table** — `SKL_ABIX_DEFINE_TABLE(...)` macro generates a POD export table with `abi_get_table()` entry point
- **Signature Safety** — `fn_sig<T>` produces a unique compile-time hash for each function type, including calling convention information
- **Type-safe Smart Pointers** — `unique_dll_ptr`, `ref_dll_ptr`, `shared_dll_ptr`, `weak_dll_ptr`, `view_dll_ptr` for managing DLL-allocated resources with correct DLL-side deallocation
- **Cross-boundary Callbacks** — `function_dll<R(Args...)>` is an 8-byte closure that captures lambdas and invokes them across DLL boundaries
- **Version Tokens** — `SKL_ABIX_VERSION("1.0")` enables multiple implementations of the same named function to coexist
- **Calling Convention Awareness** — `dll_func_cc<C, Sig>` and `dll_func<Sig, C>` templates support `__cdecl`, `__stdcall`, `__fastcall`, and `__vectorcall`
- **RCU Non-Blocking Unload** — Thread-safe DLL unloading via global `rcu_domain` read-side critical sections (`enter_read()`/`exit_read()`) and compiler-builtin atomics (`_Interlocked*`/`__atomic_*`), zero `std::atomic` ABI risk
- **RCU Timeout Policies** — Three strategies for when the EBR grace period exceeds `ABIX_RCU_TIMEOUT_MS`: Safe (zombie + leak), ForceUnload (bypass EBR), and ForceLeak (detach + leak, opt-in via macro)
- **Timeout Check Modes** — Three zero/low-CPU check modes: Lazy (check on entry), Tick (host-driven), and OS Timer (kernel-level wait)
- **Pluggable Logging** — Compile-time removable logging with C-callback sink (`ABIX_LOG_*` macros), per-level disable, and ABI-safe `set_log_sink()` for production log platforms
- **Dynamic Reflection Integration** — Built on the Reflection library, supporting runtime type queries and POD field access via `make_pod_type_info` / `make_offset_field`
- **Lookup Strategy** — Automatic: linear scan for small tables, HashIndex for large tables. Built at load time, zero ABI format changes.

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
| `enter_read()` | `const table*` | Enter RCU read-side critical section (delegates to global `rcu_domain`); returns validated table pointer, or `nullptr` if not loaded/zombie |
| `exit_read()` | `void` | Exit RCU read-side critical section (delegates to global `rcu_domain`) |
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

When `ABIX_RCU_TIMEOUT_ENABLE` is on (default) and the EBR grace period exceeds `ABIX_RCU_TIMEOUT_MS` (default: 5000ms), one of three strategies is applied:

| Strategy | Behavior | Availability | Default |
|----------|----------|-------------|---------|
| `Safe` | Mark zombie, abandon unload, DLL leaks but **never crashes** | Always | Default |
| `ForceUnload` | Bypass EBR, force `FreeLibrary`/`dlclose` — active callers **will crash** | Always | — |
| `ForceLeak` | Detach module, don't unload DLL, old objects safely leak | Requires `#define ABIX_ENABLE_FORCE_LEAK_POLICY` | — |

**Zombie State:** Under Safe/ForceLeak, the `dll_object` becomes a zombie:
- `is_loaded()` returns `false`
- `enter_read()` returns `nullptr` (sets `call_error::unloading`)
- `load()` force-unloads the zombie and reloads fresh

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

## Lookup Strategy

ABIX automatically selects the optimal lookup strategy based on table size:

| Table Size | Strategy | Description |
|------------|----------|-------------|
| < 64 entries | **Linear** | Full table linear scan, zero overhead, ~15 ns |
| ≥ 64 entries | **HashIndex** | Open-addressing hash index, ~13-17 ns, O(1) lookup |

**Design:**
- **HashIndex** is built once at DLL load time — no runtime initialization races
- Uses open addressing with linear probing and power-of-two capacity
- Load factor ~50% (capacity = `next_pow2(count * 2)`)
- **Zero ABI format changes**: `table` and `entry` structs remain unchanged; `hash_index` is runtime-only metadata
- HashIndex stores only `{name_hash, entry_index}` — never copies `entry` data

**Performance:**
| Entries | Linear | HashIndex | Speedup |
|---------|--------|-----------|---------|
| 16 | 14.8 ns | 13.2 ns | 1.1× |
| 64 | 21.8 ns | 13.4 ns | 1.6× |
| 256 | 190 ns | 13.9 ns | 13.7× |
| 1024 | 743 ns | 14.8 ns | 50.2× |
| 4096 | 1220 ns | 15.6 ns | 78.2× |
| 16384 | 2451 ns | 17.0 ns | 144.2× |

> **Key insight**: Don't optimize small tables — optimize large tables. Small tables are already fast enough (~15 ns). Large tables benefit from HashIndex by 2-3 orders of magnitude.

## Directory Structure

```
ABIX/
├── abix/                      # Core library headers
│   ├── abix.hpp               # Main entry header (includes all)
│   ├── config.h               # Platform detection, macros
│   ├── type.h                 # Core types: entry, table, type aliases
│   ├── register.h             # SKL_ABIX_DEFINE_TABLE, SKL_ABIX_ENTRY macros
│   ├── obj_dll.h              # dll_object: DLL load/unload/ref-count, RCU read/write sides, timeout policies
│   ├── rcu_domain.h           # rcu_domain: global EBR domain, enter/exit/retire/synchronize
│   ├── fn_dll.h               # dll_func / dll_func_cc: typed function handles
│   ├── fn_sig.h               # fn_sig<T>: compile-time signature hashing
│   ├── type_sig.h             # type_sig<T>: compile-time type hashing
│   ├── search.h               # find_index, hash_index, find_linear, find_hash: table search with auto-strategy
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
│   ├── hotcache_dll.cpp       # HashIndex lookup benchmark DLL
│   ├── closed_dll.cpp         # Closed-source simulation
│   └── closed_dll_v2.cpp      # Closed-source simulation v2
├── tools/                     # Build and code generation scripts
│   ├── build.py               # Main build script
│   ├── build_variants.py      # Cross-compiler variant build
│   ├── build_msvc_variants.ps1 # MSVC variant build
│   └── gen_hotcache_dll.py    # hotcache_dll.cpp code generator (large-table benchmark)
├── bench/                     # Performance benchmarks (Google Benchmark)
│   ├── CMakeLists.txt
│   ├── ebr/                   # EBR benchmarks (enter/exit, sync, mixed workloads, topology)
│   ├── workloads/             # Workload runner (threshold sweep, workload mixes)
│   └── abix/                  # ABIX integration benchmarks (resolve, cross-strategy lookup)
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
| **Performance** | **8, bench/** | `[perf]`, `[bench]` | **Atomic / EBR / Lookup (Linear + HashIndex) / Call / Concurrency / Scalability** full-matrix performance benchmarks (Google Benchmark) |

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

## Performance Benchmarks (Google Benchmark)

* Test environment:
  - `CPU`: Intel Core i7-14700K@3.4 GHz
  - `Memory`: 32GBx2 DDR5 6000MHz
  - `OS`: Windows 11 25H2
  - `HT`: Disabled
* Full output: `bench_all.exe`.

### 1. Atomic Baseline

| Benchmark | Time | Description |
|-----------|------|-------------|
| `BM_Raw_Load_U32` | 0.095 ns | Plain load (baseline) |
| `BM_StdAtomic_Load_U32` | 0.190 ns | `std::atomic` load (acquire) |
| `BM_ABIX_Atomic_Load_U32` | 0.190 ns | ABIX wrapper load (acquire) |
| `BM_Raw_Store_U32` | 0.096 ns | Plain store (baseline) |
| `BM_StdAtomic_Store_U32` | 0.192 ns | `std::atomic` store (release) |
| `BM_ABIX_Atomic_Store_U32` | 0.192 ns | ABIX wrapper store (release) |
| `BM_Raw_Inc_U32` | 1.40 ns | Plain inc (baseline) |
| `BM_StdAtomic_Inc_U32` | 3.44 ns | `std::atomic` fetch_add |
| `BM_ABIX_Atomic_Inc_U32` | 3.42 ns | ABIX wrapper inc |

#### Memory Ordering Comparison (U32)

| Benchmark | Time | Description |
|-----------|------|-------------|
| `BM_Atomic_Load_U32_Acquire` | 0.192 ns | acquire semantics |
| `BM_Atomic_Load_U32_Relaxed` | 0.190 ns | relaxed semantics |
| `BM_Atomic_Store_U32_Release` | 0.191 ns | release semantics |
| `BM_Atomic_Store_U32_Relaxed` | 0.188 ns | relaxed semantics |

#### RMW Operations (U32)

| Benchmark | Time | Description |
|-----------|------|-------------|
| `BM_Atomic_Inc_U32` | 3.41 ns | fetch_add |
| `BM_Atomic_Dec_U32` | 3.41 ns | fetch_sub |
| `BM_Atomic_CAS_U32` | 5.20 ns | compare_exchange |

#### 64-bit & Pointers

| Benchmark | Time | Description |
|-----------|------|-------------|
| `BM_Atomic_Load_U64_Acquire` | 0.189 ns | 64-bit load acquire |
| `BM_Atomic_Store_U64_Release` | 0.192 ns | 64-bit store release |
| `BM_Atomic_Inc_AcqRel_U64` | 3.41 ns | 64-bit fetch_add |
| `BM_Atomic_Load_Pointer_Acquire` | 0.189 ns | pointer load acquire |
| `BM_Atomic_Store_Pointer_Release` | 0.190 ns | pointer store release |

> **Conclusion**: ABIX atomic load/store is fully aligned with `std::atomic` (0.190 ns), zero wrapper overhead. RMW (inc/dec/CAS) uses `Interlocked*` intrinsics, matching `std::atomic` performance. acquire/relaxed/release memory orders show negligible performance differences. CAS is ~50% slower than inc (5.20 vs 3.41 ns). 64-bit and pointer operations perform identically to 32-bit.

### 2. Atomic Multi-Threaded Contention

| Benchmark | Threads | ns/op |
|-----------|---------|-------|
| `BM_Atomic_Inc_Contention` | 1 | 3.41 ns |
| `BM_Atomic_Inc_Contention` | 2 | 12.2 ns |
| `BM_Atomic_Inc_Contention` | 4 | 24.8 ns |
| `BM_Atomic_Inc_Contention` | 8 | 56.8 ns |
| `BM_Atomic_Inc_Contention` | 16 | 118 ns |
| `BM_Atomic_Load_Contention` | 1 | 0.192 ns |
| `BM_Atomic_Load_Contention` | 2 | 0.191 ns |
| `BM_Atomic_Load_Contention` | 4 | 0.190 ns |
| `BM_Atomic_Load_Contention` | 8 | 0.207 ns |
| `BM_Atomic_Load_Contention` | 16 | 0.277 ns |

> **Conclusion**: RMW contention degrades linearly with thread count (~+7 ns per additional thread), limited by cache-line bouncing. Pure load is virtually unaffected by contention (0.19 → 0.28 ns @ 16 threads), as loads do not trigger cache invalidations.

### 3. EBR Reader Fast Path (Single-Threaded)

| Benchmark | Time | Description |
|-----------|------|-------------|
| `BM_Atomic_LoadPointer_Raw` | 0.188 ns | Baseline raw pointer load |
| `BM_EBR_EnterExit` | 0.575 ns | `enter()` + `exit()` (global `rcu_domain`) |
| `BM_EBR_ProtectedLoad` | 0.553 ns | enter + load + exit |

> **Conclusion**: EBR reader fast path is ~0.55 ns (roughly 3 atomic loads). `enter()` records the global epoch, `exit()` writes only the thread-local cache-line.

### 4. Lookup Strategy (hotcache_dll: 20,000 entries)

| Benchmark | Time | Description |
|-----------|------|-------------|
| `BM_FindIndex_Only` | 220 ns | Index lookup only (baseline) |
| `BM_Resolve_Linear` | 220 ns | Linear scan |
| `BM_Resolve_HashIndex` | **14.0 ns** | HashIndex lookup |
| `BM_Resolve_WithEBR` | 221 ns | Linear scan with EBR protection |
| `BM_Resolve_Linear_Random` | 22.8 ns | Random access + linear |

> **Conclusion**: HashIndex achieves ~**16×** speedup (220 ns → 14.0 ns) for large tables. Linear scan is kept for small tables (< 64 entries) where the overhead of HashIndex is not justified.

### 5. Call Overhead

#### Layer A: Optimized Real-World Cost (inline allowed)

| Benchmark | Time | Description |
|-----------|------|-------------|
| `BM_DirectCall` | 0.095 ns | Direct C++ call |
| `BM_FunctionPointerCall` | 0.094 ns | Function pointer call |
| `BM_StdFunctionCall` | 0.753 ns | `std::function` call (inlineable target) |
| `BM_MediumFunction_Direct` | 0.095 ns | Medium function direct call |
| `BM_MediumFunction_FnPtr` | 0.095 ns | Medium function pointer call |

#### Layer B: Forced Dispatch Overhead (NOINLINE)

| Benchmark | Time | Description |
|-----------|------|-------------|
| `BM_DirectCall_NoInline` | 0.096 ns | Direct call to NOINLINE target |
| `BM_FunctionPointerCall_NoInline` | 0.095 ns | Function pointer + NOINLINE target |
| `BM_StdFunctionCall_NoInline` | 0.993 ns | `std::function` (global target, prevents devirtualization) |
| `BM_MediumFunction_Direct_NoInline` | 0.095 ns | Medium function NOINLINE direct call |
| `BM_MediumFunction_FnPtr_NoInline` | 0.096 ns | Medium function NOINLINE fn ptr |

#### ABIX Calls (inherently cross-DLL boundary, NOINLINE)

| Benchmark | Time | Description |
|-----------|------|-------------|
| `BM_ABIX_Call` | 6.66 ns | Full ABIX call (RCU + lookup) |
| `BM_ABIX_TinyFunction` | 6.62 ns | Tiny function (1 instruction) |
| `BM_ABIX_SmallFunction` | 7.77 ns | Small function (10 instructions) |
| `BM_ABIX_Call_Raw` | 1.14 ns | Lock-free raw call (no RCU) |

> **Conclusion**: Full ABIX call is ~6.66 ns, raw call is only 1.14 ns — the ~5.5 ns gap comes from RCU enter/exit + handle resolution. `std::function` with devirtualization prevented is 0.99 ns, nearly identical to a function pointer (0.095 ns), indicating `std::function`'s type-erased wrapper overhead is ~0.9 ns.
>
> **ABIX's call overhead primarily comes from handle resolution and EBR lifetime protection required for hot-reload safety, not from the DLL function call itself. This overhead is largely independent of the specific function instance and depends mainly on whether the call goes through ABIX's hot-reloadable handle.**

### 6. EBR Writer (Synchronize & Reclaim)

| Benchmark | Time | Description |
|-----------|------|-------------|
| `BM_EBR_SyncPhase` | 8.74 ns | `synchronize()` epoch advance (no readers) |
| `BM_EBR_TryCollect_Empty` | 5.33 ns | Empty `try_collect()` attempt |
| `BM_EBR_GracePhase` | 5.2 μs | retire + synchronize (small objects) |

### 7. EBR Reader Scalability (Multi-Threaded `enter()`/`exit()`)

| Threads | ns/op | Scalability |
|---------|-------|-------------|
| 1 | 0.478 ns | Baseline |
| 2 | 0.475 ns | Perfect |
| 4 | 0.497 ns | Perfect |
| 8 | 0.510 ns | Perfect |
| 16 | 0.919 ns | Slight degradation |
| 20 | 0.858 ns | Core saturation |
| 32 | 1.22 ns | Oversubscription |

> **Conclusion**: 1→8 threads scale almost perfectly. `enter()`/`exit()` only write thread-local cache-lines, zero contention. 16+ threads begin to see `_global_epoch` cache-line contention.

### 8. EBR Writer Scalability (`synchronize()` latency vs readers)

| R/W | 0 Readers | 1 Reader | 4 Readers | 8 Readers | 16 Readers |
|-----|-----------|----------|-----------|-----------|------------|
| 1 Writer | 695 ns | 863 ns | 1.1 μs | 1.8 μs | 3.5 μs |
| 2 Writers | 880 ns | — | 1.3 μs | 2.2 μs | 5.7 μs |
| 4 Writers | 1.2 μs | — | 1.5 μs | 2.3 μs | 3.8 μs |

> **Conclusion**: `synchronize()` latency grows linearly with reader count (waiting for all readers to exit the previous epoch). Multi-writer `_writer_lock` contention begins to appear at 4W.

### 9. EBR Read/Write Ratio (Latency Distribution)

| R/W | reader_avg | reader_p50 | reader_p99 | writer_avg | writer_p99 |
|-----|-----------|-----------|-----------|-----------|------------|
| 1/1 | 28 ns | 1 ns | 64 ns | 2.0 μs | 4.1 μs |
| 4/1 | 28 ns | 1 ns | 64 ns | 2.4 μs | 4.1 μs |
| 8/1 | 31 ns | 1 ns | 64 ns | 3.5 μs | 4.1 μs |
| 16/1 | 39 ns | 1 ns | 128 ns | 8.9 μs | 8.2 μs |
| 32/1 | 70 ns | 1 ns | 64 ns | 30.0 μs | 8.2 μs |
| 4/2 | 101 ns | 64 ns | 256 ns | 8.6 μs | 16.4 μs |
| 8/2 | 113 ns | 64 ns | 256 ns | 12.6 μs | 32.8 μs |
| 16/2 | 127 ns | 64 ns | 256 ns | 26.7 μs | 65.5 μs |
| 4/4 | 191 ns | 128 ns | 256 ns | 25.9 μs | 65.5 μs |
| 8/4 | 208 ns | 128 ns | 512 ns | 33.8 μs | 131.1 μs |
| 16/4 | 218 ns | 128 ns | 512 ns | 264.5 μs | 8.4 ms |

> **Conclusion**: Reader p50-p99 remain extremely low (1-128 ns), even at 32R/1W. Writer p99 reaches 8.4 ms at 16R/4W, exposing writer starvation—multi-writer contention + multi-reader blocking significantly degrades tail latency.

### 10. EBR Read/Write Ops (Throughput)

| R/W | reader_ops | writer_ops |
|-----|-----------|------------|
| 1/1 | 109.2M | 87.3k |
| 4/1 | 30.2M | 88.3k |
| 8/1 | 21.0M | 58.3k |
| 16/1 | 33.1M | 57.3k |
| 32/1 | 61.2M | 90.2k |
| 4/2 | 17.5M | 92.7k |
| 8/2 | 18.7M | 78.1k |
| 16/2 | 21.2M | 38.8k |
| 4/4 | 13.0M | 68.5k |
| 8/4 | 10.0M | 42.6k |
| 16/4 | 37.2M | 82.0k |

> **Conclusion**: Reader throughput ranges from 10M-109M ops/s, limited by thread scheduling. Writer throughput is stable at 38k-92k ops/s regardless of R/W ratio—synchronize overhead dominates.

### 11. EBR Grace Period (Long Reader Test)

| Reader Duration | `synchronize()` Latency | Notes |
|----------------|-----------------|------|
| 10 ns | 5.2 μs | Fixed overhead |
| 100 ns | 5.2 μs | Fixed overhead |
| 1 μs | 6.0 μs | Starts tracking |
| 10 μs | **10.3 μs** | Tracks exactly |
| 100 μs | **100.4 μs** | Tracks exactly |
| 1 ms | **1000.4 μs** | Tracks exactly |

> **Conclusion**: Above 10 μs, `synchronize()` latency = reader duration. Confirms the grace period is correctly gated by the slowest reader. Short readers (< 1 μs) are dominated by fixed overhead.

### 12. EBR Retire Batch

| Batch | Total | ns/object |
|-------|-------|-----------|
| 1 | 5.1 μs | 5100 |
| 10 | 5.5 μs | 510 |
| 100 | 8.3 μs | 63 |
| 1000 | 35.1 μs | 16.3 |
| 10000 | 298.8 μs | **10.9** |

> **Conclusion**: Batch reclaim dramatically reduces per-object cost (5100 → 10.9 ns/object). The `BATCH_PUBLISH_SIZE = 64` local accumulation strategy operates in the optimal range.

### 13. EBR Mixed Workload (Protected Load + Intermittent `synchronize()`)

| R/W | reader_avg | reader_p99 | writer_avg | writer_p99 |
|-----|-----------|-----------|-----------|------------|
| 1/100 | 25 ns | 64 ns | 5.1 μs | 4.1 μs |
| 4/100 | 25 ns | 64 ns | 5.6 μs | 8.2 μs |
| 8/100 | 26 ns | 64 ns | 7.2 μs | 8.2 μs |
| 16/100 | 35 ns | 64 ns | 12.0 μs | 8.2 μs |
| 4/10 | 24 ns | 64 ns | 5.9 μs | 4.1 μs |
| 8/10 | 26 ns | 64 ns | 7.4 μs | 8.2 μs |
| 16/10 | 36 ns | 64 ns | 12.3 μs | 16.4 μs |

> **Conclusion**: Simulates real-world ABIX scenarios (heavy readers + intermittent writers). Reader p99 remains stable at 64 ns; occasional writer synchronize does not affect reader tail latency. `writer_interval=100` represents a 99.9% reader / 0.1% writer ratio.

## Industry Comparison

> [!WARNING]
> Data sources: ABIX benchmarks + public benchmarks & industry documentation.
> Please submit Pull Request if any errors are found.

### Key Metrics Overview

| Approach | Core Cost | Latency | Notes |
|----------|----------|---------|-------|
| **Raw function pointer (baseline)** | Direct call | ~0.095 ns | Compiler can inline |
| **ABIX (cross-DLL call)** | Table lookup + indirect call | ~6.7 ns | Full safety checks included |
| **ABIX (hash-index lookup)** | Table lookup + type safety | ~14 ns | Full safety checks included |
| **GetProcAddress / dlsym** | PE/ELF export table traversal + string hash | ~27 μs (27,000 ns) | Per-lookup cost |
| **GetProcAddress (cached)** | Function pointer call only | ~0.1 μs (100 ns) | No type safety |
| **std::function invocation** | Type erasure + indirect call | 1.6 ~ 2.8 ns | ~1.6 ns with SBO hit |
| **std::function construction** | SBO or heap allocation | 2.3 ~ 42 ns | 19.6 ns for heap alloc (exceeds SBO) |
| **C++ virtual function call** | vtable lookup + indirect jump | 0.55 ~ 2.1 ns | ~0.23 ns when devirtualized |
| **COM QueryInterface** | Runtime interface query + ref counting | Significantly higher than vfunc | Called on every interface switch |
| **Qt signal-slot (same thread)** | Meta-object lookup + slot invocation | ~42.7 ns | Qt 6.5.1 benchmark |
| **Qt signal-slot (cross-thread)** | Event queue + arg serialization | ~128 ns | Significant cross-thread overhead |
| **Unreal Engine (Blueprint Tick)** | Script context + reflection call | ~100 - 200 ns | Empty Blueprint Tick |
| **Unity (managed→native callback)** | Managed/native domain switch | Significantly higher than native | Known bottleneck at scale |

### Deep Dive

#### 1. vs GetProcAddress / dlsym: 3 Orders of Magnitude Faster

`GetProcAddress` traverses the DLL export table and performs string comparison on every call, costing up to **27 μs**. While caching the function pointer reduces subsequent calls to near-zero overhead, this requires manual cache maintenance by the developer and completely abandons type safety.

ABIX's lookup mechanism (HashIndex ~14 ns) reduces lookup overhead by **1,900×** while providing compile-time type safety — something manual `GetProcAddress` caching can never achieve.

#### 2. vs std::function: Lighter, Safer

`std::function` invocation at 1.6~2.8 ns appears faster than ABIX's 6.7 ns. However:

- `std::function` **cannot be safely passed across DLL boundaries** (cross-CRT heap issues)
- `std::function` object size is **32 bytes** (vs `function_dll`'s **8 bytes**)
- `std::function` construction may trigger heap allocation (19.6 ns when exceeding SBO)

ABIX's `function_dll` is designed with a fixed **8-byte** size and **zero heap allocation**, safely crossing DLL boundaries — a scenario `std::function` simply cannot handle.

#### 3. vs Qt Signal-Slot: 6× Faster

Qt same-thread signal-slot costs **42.7 ns**, which is **6×** slower than ABIX's full cross-DLL call (6.7 ns). Qt's overhead primarily comes from meta-object system connection lookup and argument marshaling — costs that ABIX bypasses entirely through compile-time hashing.

#### 4. vs Game Engines: Orders of Magnitude Advantage

Unreal's empty Blueprint Tick costs ~**100-200 ns**, and Unity's managed→native callback switch is a known bottleneck at scale. ABIX's **6.7 ns** call overhead means: within the same frame budget, ABIX can support **15-30×** the call volume of native game engine callbacks.

#### 5. COM QueryInterface: The Cost of Runtime Type Safety

COM's `QueryInterface` requires a runtime query on every interface switch, with overhead significantly higher than virtual function calls. ABIX's type safety is resolved at compile time; at runtime, only a single ~14 ns hash table lookup is needed — no repeated `QueryInterface` calls on the hot path.

## Future Plans

- **AMC Integration** — Combine with the planned Meta Object Compiler to auto-generate `SKL_ABIX_DEFINE_TABLE` entries from C++ attributes
- **Serialization Support** — Extend `type_sig` and type tags to support serialization of complex types across DLL boundaries
- **Network Transport** — Enable remote function calls through the same stable table format

### ABIX Runtime Bootstrap (Long-term)

The current RCU/EBR implementation is header-only with inline reader fast paths, keeping `enter()`/`exit()` at ~2–3 ns. This serves as the **reference/golden implementation**. The long-term plan is to bootstrap ABIX's own runtime:

```
Header-only RCU (reference implementation)
       │
       ▼
ABI Metadata + AMC / ABI Generator
       │
       ▼
Bootstrap ABI (auto-generated glue)
       │
       ▼
libabix_rcu (replaceable Runtime)
       │
       ▼
ABIX builds its own Runtime
```

The end state separates concerns cleanly:

```
                 Public ABIX Header
                         │
             ┌───────────┴───────────┐
             ▼                       ▼
       Header backend          Runtime backend
             │                       │
        inline reader            ABI Runtime
             │                       │
             └───────────┬───────────┘
                         ▼
                    Same semantic ABI
```

This allows the Runtime to freely evolve through layout strategies (padded, dense, NUMA, hierarchical) while `rcu_domain`, `rcu_guard`, and the ABI contract remain stable. The header-only implementation acts as the golden reference, and ABIX's own ABI system will eventually generate the Runtime glue — the library bootstraps itself.

## License

Apache License, Version 2.0. See [LICENSE](https://www.apache.org/licenses/LICENSE-2.0) for the full text.

---

Copyright 2026 [Sukanle](https://github.com/Sukanle)