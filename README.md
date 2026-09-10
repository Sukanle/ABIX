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
- **ABI metadata runtime** — Versioned `.abix` v4 artifacts, generated static descriptors, and a bounded `RuntimeRegistry` provide native type metadata without changing the DLL function-table ABI.
- **AMC pipeline** — `amc` extracts selected C++ ABI layouts, validates/inspects artifacts, generates C++ projections, and produces compatibility/Map IR reports.

> [!IMPORTANT]
> ABIX's Hash Container, Micro-RCU, RCU batching, and other performance optimizations are **specialized for ABIX's own read-mostly workloads**. They are not general-purpose concurrent containers or a general-purpose RCU implementation.

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
- **Dynamic mics Integration** — Built on the mics library, supporting runtime type queries and POD field access via `make_pod_type_info` / `make_offset_field`
- **Lookup Strategy** — Automatic: linear scan for small tables, HashIndex for large tables. Built at load time, zero ABI format changes.
- **Generated Runtime Metadata** — `RuntimeRegistry::register_module()` validates a generated `ModuleDescriptor`; opt-in generated `TypeTraits<T>` enables `type_of<T>()`.
- **ABI Metadata Compiler (AMC)** — C++ frontend, C++17 projection backend, compatibility analysis, `MapPrivate<A, B>` generation, and JSON-lines provider IPC.

## ABI Metadata and AMC

The stable DLL function table remains the public call ABI.  Metadata is an
additional, explicit layer: AMC reads a selected C++ surface and writes an
`.abix` v4 artifact containing type/layout, field, function, symbol, hash,
compatibility, and map records.  It can then generate a C++17 descriptor
projection for registration in the runtime registry.

```sh
amc build -c package.abic.toml -B build
amc validate build/build/package.abix
amc generate build/build/package.abix -l cpp -o package_metadata.hpp
amc diff v1.abix v2.abix -o compatibility.abix
```

Generated native traits are deliberately opt-in: define
`AMC_GENERATED_DECLARE_NATIVE_TYPE_TRAITS` before including the generated
header, then register its `amc_generated::amc_module` descriptor before
calling `RuntimeRegistry::type_of<T>()`.  See [the API reference](docs/api.md)
and [the `.abix` format notes](docs/abix.md).

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
> - [API.md](docs/api.md)

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
│   ├── refl.h                 # Dynamic mics integration helpers
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

**Requirements:** C++17 or later (C++20 recommended for `consteval` support). The mics library is included as a git submodule.

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
| **mics** | 14 | `[refl]` | Static/dynamic mics (FP/Any/Registry/TypeInfo/StaticRefl) integration |
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

## Performance

ABIX is optimized for read-mostly workloads with many concurrent readers and relatively few writers. The embedded Micro-RCU uses cache-line-aware state placement and batched epoch advancement to reduce shared cache-line contention.

### Role-Based Workload (B8 vs B16 Epoch Batch)

Role-Based is the most important performance metric for ABIX — it models real-world usage with N reader threads and 1 writer thread. The table below compares `SKL_ABIX_RCU_EPOCH_BATCH = 8` (default) vs `16` on Intel Core i7-14700K (20 P-cores, SMT disabled):

| Workload    | Threads |            B8 |           B16 |
| ----------- | ------: | ------------: | ------------: |
| read-heavy  |     20T |      9.44 G/s | **11.67 G/s** |
| balanced    |     20T |     11.66 G/s | **11.90 G/s** |
| write-heavy |     20T | **13.94 G/s** |     13.53 G/s |

Batching reduces high-concurrency overhead in ABIX's target read-mostly workloads. See [Benchmark & Performance](docs/benchmark.md) for complete methodology, hardware configurations, optimization analysis, and full results.

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
| **Unreal Engine (Blueprint Tick)** | Script context + mics call | ~100 - 200 ns | Empty Blueprint Tick |
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

- **AMC wrapper expansion** — Generate native wrapper classes, explicit conversion adapters, and hot-reload projections; metadata does not auto-generate exported DLL function-table entries today.
- **Serialization Support** — Extend `type_sig` and type tags to support serialization of complex types across DLL boundaries
- **Network Transport** — Enable remote function calls through the same stable table format

### ABIX Runtime Bootstrap Status

ABIX Runtime metadata is self-described today: a clean build can use
`abix/self.abic.toml` to regenerate metadata for the model, registry, map, and
RCU/EBR types, register it, and query native types with `type_of<T>()`.
The hand-written Bootstrap Kernel remains the trusted bootstrap component.

AMC also describes its own core IR through `amc/self.abic.toml`; its generated
metadata projection is compiled and consumed by `RuntimeRegistry`. This is a
metadata self-hosting check, not compiler-source self-hosting: `amc-cpp` still
uses Clang/LLVM for C++ semantic extraction and is not built from its generated
descriptor.

The remaining runtime evolution goal is to make the runtime implementation
replaceable behind the same semantics:

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

This allows the Runtime to freely evolve through layout strategies (padded, dense, NUMA, hierarchical) while `rcu_domain`, `rcu_guard`, and the ABI contract remain stable.

## License

Apache License, Version 2.0. See [LICENSE](https://www.apache.org/licenses/LICENSE-2.0) for the full text.

---

Copyright 2026 [Sukanle](https://github.com/Sukanle)
