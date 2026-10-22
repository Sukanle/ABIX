# ABIX vs Mainstream Plugin / Runtime Frameworks

<p align="center">
  <a href="competitors_zh.md">中文</a> · English
</p>

> This chapter compares ABIX with the runtime and binding frameworks people usually reach for when a host application loads third-party native code — **COM, Qt, Unity, Unreal** — plus the adjacent options (WebAssembly Component Model, FlatBuffers/gRPC, P/Invoke). It is written from the measurement side: what each one does _per call_, what that costs, and when it is the better choice.
>
> The tooling landscape (libabigail, Conan, vcpkg, CMake/Bazel, bindgen/SWIG) is a different axis and lives in [`docs/development/competitors.md`](../development/competitors.md).

<details>

<summary>Contents</summary>

- [1. How to read this](#1-how-to-read-this)
- [2. The cost classes ABIX is measured against](#2-the-cost-classes-abix-is-measured-against)
- [3. Head to head](#3-head-to-head)
    - [3.1 COM](#31-com)
    - [3.2 Qt](#32-qt)
    - [3.3 Unity](#33-unity)
    - [3.4 Unreal Engine](#34-unreal-engine)
    - [3.5 Adjacent options](#35-adjacent-options)
- [4. Where the performance gap actually is](#4-where-the-performance-gap-actually-is)
- [5. Scenario matrix](#5-scenario-matrix)
- [6. Where ABIX is not the answer](#6-where-abix-is-not-the-answer)
- [7. References](#7-references)

</details>

## 1. How to read this

Two kinds of statement appear below, and they are not interchangeable:

| Kind              | Meaning                                                                                                                                                                                                                  |
| ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Measured**      | A number produced by this repository's benchmark suite on this machine; see [`benchmark.md`](benchmark.md) §5, and the per-family rerun in `memory/windows-benchmark.md` §8. Reproduce with `python tools/run_bench.py`. |
| **Architectural** | A statement about what a framework _does_ (indirection, marshalling, reflection, GC, lifetime bookkeeping), **not measured here**. No framework below was installed or benchmarked in this repository.                   |

So: ABIX's cost is a measurement; the competitors' costs are mechanism descriptions with an order-of-magnitude judgement, and the comparison is made in **cost classes**, not by subtracting unlike numbers.

Currency: Windows 11, i7-14700K (20 physical cores, HT off, Defender real-time off), MSYS2 UCRT64 g++ 16.2.0, `Release` (`-O3 -DNDEBUG`).

## 2. The cost classes ABIX is measured against

These are the measured rungs a "call into a plugin" can sit on. Any framework's hot path is built out of some combination of them.

| Class                                                                     | ABIX benchmark                                      |               ns/call |
| ------------------------------------------------------------------------- | --------------------------------------------------- | --------------------: |
| Direct call (inlined)                                                     | `BM_DirectCall`                                     |                 0.093 |
| Call through a function pointer                                           | `BM_FunctionPointerCall`                            |                 0.092 |
| `std::function`                                                           | `BM_StdFunctionCall`                                |                 0.757 |
| ABIX Interface, path cached                                               | `BM_Interface_CachedCall`                           |                 0.946 |
| ABIX Interface, table probed per call                                     | `BM_Interface_ResolvePerCall`                       |                 1.680 |
| ABIX call, RCU bracket + direct call (no revalidation)                    | `BM_ABIX_Call_Raw`                                  |                 1.551 |
| ABIX read-side bracket alone                                              | `BM_Domain_EnterExit_Instance` / `BM_EBR_EnterExit` |         0.539 / 0.527 |
| **ABIX `Function::operator()` (name + signature revalidated every call)** | `BM_ABIX_Call`                                      |             **2.763** |
| name hash (6 / 16 chars)                                                  | `BM_Hash_Name_6ch` / `_16ch`                        |         1.959 / 5.218 |
| symbol lookup via hash index / linear scan (16K entries, uniform)         | `abix_lookup_cross_bench`                           |     14.753 / 1569.475 |
| resolve through the index (16K, RCU bracket on)                           | `abix_resolve_bench rcu_on`                         |                 6.473 |
| same-path hot reload                                                      | `BM_Reload_SamePath`                                |               3,545.6 |
| a/b alternating reload                                                    | `BM_Reload_AlternatingPaths`                        |              18,492.0 |
| `LoadLibrary` alone / `FreeLibrary` alone                                 | `BM_Reload_LoadOnly` / `FreeOnly`                   |    36,829.7 / 6,194.2 |
| retire + synchronize (the RCU part of an unload)                          | `BM_Reload_RetireSync`                              |                  48.0 |
| EBR `synchronize()` on an empty domain                                    | `BM_EBR_Synchronize`                                |                 6.390 |
| EBR sync phase, 1T → 20T                                                  | `BM_EBR_SyncPhase`                                  |         6.4 → 2,831.3 |
| reclaim a batch of 1 → 10,000 retired nodes                               | `BM_EBR_ReclaimBatch`                               |        77.0 → 322,323 |
| grace period with 10 → 1M pending nodes                                   | `BM_EBR_GracePeriod`                                |       100.4 → 125,000 |
| atomic load / store / inc                                                 | `BM_Atomic_*`                                       | 0.183 / 0.187 / 3.390 |

Two facts fall out immediately, and they frame everything else:

1. **The indirect-call floor is the same everywhere.** A pointer call is 0.09 ns; ABIX's own Interface fast path (0.95 ns) and a `std::function` (0.76 ns) are in the same class. No framework wins or loses on "how fast is an indirect call".
2. **ABIX's whole premium is 2.67 ns/call** — 2.763 ns for the fully revalidated `Function::operator()` versus 0.093 ns for a direct call, and 1.21 ns versus its own `Call_Raw`. That premium is exactly what buys hot reload: the handle re-checks the cached image, name hash and signature on every call, so a reloaded module can never make it jump into the retired image.

## 3. Head to head

For each framework: what happens per call, what it is good at, what it costs, and when to pick it.

### 3.1 COM

**Mechanism.** A binary standard, not a library: interfaces are vtable layouts, lifetime is `AddRef`/`Release`, discovery is `QueryInterface` with an IID, and activation goes through the registry. Language-neutral at the ABI level, and the only model here that is _designed_ to be consumed from unrelated languages.

|                               |                                                                                                                                                                                                                                                                                                         |
| ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Per-call work (architectural) | one vtable indirection; no name/signature check (the IID is the check, and it happens once, at `QueryInterface`)                                                                                                                                                                                        |
| ABIX analogue (measured)      | `BM_Interface_CachedCall` 0.946 ns — same "cached entry, then indirect call" shape. A `Release()` is an interlocked decrement, the same class as `BM_Atomic_Inc_U32` 3.390 ns                                                                                                                           |
| Strengths                     | ubiquitous on Windows; language-neutral; stable, documented binary contract; apartment/marshalling model for cross-thread and cross-process                                                                                                                                                             |
| Weaknesses                    | no hot reload and no ABI evolution: an interface is frozen, so adding a method means a new IID and recompilation of every consumer; refcount lifetime is manual and leaks/unload bugs are the caller's; registry activation; marshalling overhead when apartments/processes differ                      |
| ABIX gap                      | ABIX has no language neutrality (a C ABI table, consumed from C++), no registry, no apartment model. But it has what COM deliberately lacks: `retire`-based lifetime (no refcount to get wrong), metadata-verified cross-compiler ABI, and reload that keeps live handles calling into the _new_ module |

**Pick COM** when the boundary must be consumed from other languages/VMs on Windows and the interface will not change; **pick ABIX** when the plugin itself gets replaced under a running host.

### 3.2 Qt

**Mechanism.** A C++ application framework with its own object model: `moc` generates meta-object tables, signals/slots dispatch through `QMetaObject::activate` by index, `Q_PROPERTY`/`Q_INVOKABLE` give reflection, and `QPluginLoader` loads plugins whose `QObject` ABI must match the Qt build.

|                               |                                                                                                                                                                                                                                                                                               |
| ----------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Per-call work (architectural) | index-based dispatch into a meta-object table plus QObject bookkeeping; `QMetaType` conversion when the call goes through the meta-object system                                                                                                                                              |
| ABIX analogue (measured)      | ABIX's table probe is `abix_lookup_cross_bench` (14.8 ns at 16K, 6.3 ns zipf) — the same "find the entry, then call it" step, and the only part the two models share                                                                                                                          |
| Strengths                     | a whole application framework (widgets, event loop, networking, model/view); cross-platform source portability; mature tooling, designer, translations; plugin loading with a stable _within-version_ ABI                                                                                     |
| Weaknesses                    | **not an ABI layer**: Qt's binary compatibility is per Qt major version and per compiler/flags, and plugins must be rebuilt when Qt is upgraded; signals/slots are a source-level mechanism, not a cross-compiler contract; no hot reload; heavy runtime footprint for a pure plugin boundary |
| ABIX gap                      | ABIX is a boundary, Qt is an application. Qt gives you the app; ABIX gives you the ability to swap a module inside one                                                                                                                                                                        |

**Pick Qt** for the application and its UI; use ABIX (or plain C ABI) for the part that must survive a module swap.

### 3.3 Unity

**Mechanism.** A managed runtime (Mono or IL2CPP) with C# as the authoring language; native code is reached through `[DllImport]`/P-Invoke, which marshals arguments and transitions between managed and native execution.

|                               |                                                                                                                                                                                                                                                                                                        |
| ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Per-call work (architectural) | managed ↔ native transition, optional argument/string marshalling, plus managed bookkeeping (GC, `MonoBehaviour` reflection/serialization). IL2CPP removes the interpreter but not the marshalling or the GC                                                                                           |
| ABIX analogue (measured)      | none — ABIX has no managed boundary. Its closest analogue is `BM_ABIX_Call` 2.763 ns, which is the _entire_ cost of a fully validated call                                                                                                                                                             |
| Strengths                     | fast iteration on gameplay code; huge asset/editor ecosystem; cross-platform builds; GC removes most manual lifetime bugs; Burst/Jobs for data-parallel work                                                                                                                                           |
| Weaknesses                    | crossing into native code is the expensive direction (marshalling + transition, orders of magnitude above a vtable call); GC pauses; the ABI you expose to the C# side is the runtime's, not C++'s; native hot reload is not provided (script reload reloads the domain or requires an IL2CPP rebuild) |
| ABIX gap                      | ABIX does not run managed code and offers no marshalling. In exchange, a call costs nanoseconds rather than a transition, and unload safety is explicit (`unload()` refuses while handles are alive) rather than delegated to a GC                                                                     |

**Pick Unity** when the product _is_ a managed game/simulation; **pick ABIX** when a native host must load, call and replace native modules with no managed boundary in between.

### 3.4 Unreal Engine

**Mechanism.** C++ with generated glue: UHT produces reflection data for `UCLASS`/`UFUNCTION`/`UPROPERTY`, `UObject` instances are GC-managed, Blueprint calls go through that reflection layer, and iteration uses Live Coding / Hot Reload rather than a plugin ABI.

|                               |                                                                                                                                                                                                                                                                                                                                                       |
| ----------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Per-call work (architectural) | a native-to-native `UFunction` call is close to a plain C++ call; anything reached from Blueprint or through reflection adds a reflection dispatch and parameter marshalling                                                                                                                                                                          |
| ABIX analogue (measured)      | the native-to-native end: `BM_DirectCall` 0.093 ns / `BM_ABIX_Call` 2.763 ns. The reflection end has no ABIX analogue                                                                                                                                                                                                                                 |
| Strengths                     | a complete engine (renderer, physics, networking, editor); reflection enables the editor to drive C++ types generically; Live Coding gives short iteration loops without a full restart                                                                                                                                                               |
| Weaknesses                    | `UObject` layout and module ABI are **not stable across engine versions** — plugins are rebuilt with the engine; Live Coding patches code but does not change layouts, and "hot reload" in UE has well-known limitations; the reflection/GC layer is mandatory for the object model, i.e. you pay for it even when you only wanted a function pointer |
| ABIX gap                      | ABIX's boundary is versioned by _metadata content_ and verified at load/verify time, and its handles survive a module swap; UE's boundary is "same engine build"                                                                                                                                                                                      |

**Pick Unreal** when you are building inside it; **pick ABIX** when the plugin must be replaceable in a shipping host without restarting, and when you want a verifiable ABI rather than a matching build.

### 3.5 Adjacent options

| Option                            | Model                                                                    | Relative to ABIX                                                                                                                                                        |
| --------------------------------- | ------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **WebAssembly Component Model**   | sandboxed module with an interface-typed boundary and generated bindings | stronger isolation and language neutrality; pays a boundary/serialization cost and needs a wasm runtime; ABIX is in-process and zero-copy                               |
| **FlatBuffers / Protobuf / gRPC** | serialized messages over a buffer, pipe or socket                        | language-neutral and decouples versions; per-message encode/decode cost is far above a function call — a different problem (data interchange, not function binding)     |
| **P/Invoke, ctypes, CFFI, JNI**   | one language's foreign-function mechanism                                | the same "call native code" goal, one binding at a time, with marshalling; ABIX adds metadata verification, a name+sig table, hash-index lookup and reload-safe handles |
| **libffi / vtable hand-rolling**  | build the boundary yourself                                              | maximum control, no metadata, no verification, no reload story; ABIX's per-call premium (2.67 ns) is what you would otherwise have to write                             |

## 4. Where the performance gap actually is

1. **Per-call indirection: no meaningful gap.** Pointer call 0.09 ns, cached Interface 0.95 ns, `std::function` 0.76 ns. Anyone claiming a hot-path win at this level is measuring their own bookkeeping, not the call.
2. **Per-call validation: ABIX pays, deliberately.** +2.67 ns over a bare call. Nothing in COM/Qt pays this per call (they validate once, or never), and nothing in COM/Qt can therefore survive a module swap the way ABIX does.
3. **Managed/VM boundaries: the real order-of-magnitude step.** P/Invoke, Blueprint reflection and marshalling are transitions, not calls — tens to hundreds of nanoseconds at best, microseconds once reflection or serialization is involved, against ABIX's 2.76 ns. This is where a managed-engine plugin differs from ABIX by one to three orders of magnitude, and it is a deliberate trade for GC and authoring speed, not a defect.
4. **Load/unload is OS-bound, plus a small RCU tax.** `LoadLibrary` 36.8 µs, `FreeLibrary` 6.2 µs, and the RCU part of an unload (retire + synchronize) is **48 ns** — under 1 % of the loader cost. Every framework here pays the loader cost; ABIX's grace-period safety is not what makes unloading expensive.
5. **Hot reload is where the models separate.** ABIX: **3.5 µs** same path, **18.5 µs** a/b alternating. Qt: rebuild plugins per Qt version. COM: new IID, rebuild consumers. UE Live Coding: patches, no layout change, seconds-scale loops. Unity: domain reload or IL2CPP rebuild.
6. **Concurrency bookkeeping is the scaling cost, and it is visible.** `synchronize()` is 6.4 ns on an empty domain but `BM_EBR_SyncPhase` is 2,831 ns at 20 threads (≈ 440×): EBR must wait for every active epoch, so a high-frequency, many-thread writer side is ABIX's weakest axis. `ReclaimBatch` (77 ns → 322 µs for 1 → 10,000 nodes) shows the same for reclamation work. A reader registry entry is never removed either — a thread that ever entered stays in the scan list with `epoch == 0`, and every grace-period poll walks all of them: ~3.4 ns per registered node, i.e. 12 ns at 10 threads but **34 µs per poll at 10,000**, inside a spin loop.
7. **Lookup is O(1) by design.** At 16K entries: hash index **14.8 ns** vs linear scan **1,569 ns** (106×) uniform, and 6.3 vs 36.7 ns (5.8×) zipf. Above `SKL_ABIX_HASH_THRESHOLD` the index is built at load time (77.8 ns at 64 entries → 49.5 µs at 16K), which is why a 16K-entry module's load is dominated by index construction rather than by the loader.

## 5. Scenario matrix

| Scenario                                              | ABIX                                 | COM                  | Qt           | Unity                    | Unreal                          |
| ----------------------------------------------------- | ------------------------------------ | -------------------- | ------------ | ------------------------ | ------------------------------- |
| Native plugin replaceable _while the host runs_       | **best fit**                         | no                   | no           | managed scripts only     | partial (Live Coding)           |
| Cross-compiler / cross-toolchain native boundary      | **best fit** (metadata verify)       | good (frozen IID)    | per Qt build | n/a                      | per engine build                |
| Language-neutral consumers                            | no (C ABI table)                     | **best fit**         | C++ only     | C# first                 | C++/Blueprint                   |
| Strict per-call latency (read-mostly, short sections) | **best fit**                         | good                 | good         | poor across the boundary | good native, poor via Blueprint |
| Many-thread, high-frequency writer side               | weak (EBR sync scales)               | n/a                  | n/a          | n/a                      | n/a                             |
| Rich application surface (UI, event loop, assets)     | none                                 | none                 | **best fit** | **best fit**             | **best fit**                    |
| Sandboxed / out-of-process isolation                  | no                                   | partial (apartments) | no           | no                       | no                              |
| ABI evolution with CI verification                    | **best fit** (`.abix`, `amc verify`) | manual               | manual       | n/a                      | manual                          |
| Managed-code ergonomics, GC                           | no                                   | no                   | no           | **best fit**             | partial                         |

## 6. Where ABIX is not the answer

Stated plainly, because a comparison that only lists wins is useless:

- **It is a boundary, not a platform.** No UI, event loop, asset pipeline, networking or scripting. If you need those, you need Qt/Unity/Unreal and ABIX is at most a small part of the picture.
- **C++-only, no marshalling.** Types must be ABI-stable (integers, pointers, plain structs). No IDL, no automatic complex-type conversion, no language-neutral object model.
- **No fairness guarantee for the advancement right.** A writer that keeps losing the race retries without a bound (F0). The plan for a cancellable FIFO exists but is deliberately not implemented — 100+ isolated-process runs found no sustained starvation.
- **Unprotected escape hatches exist and are documented as such:** `Function::raw()`, `Object::resolve()` and closures carry no read-side protection. A callee must not let an exception escape a call (the bracket is released after the call returns).
- **`ForceLeak` and per-entry `reload_until()`/`unload_until()` are not implemented.** A timeout defers a retired image (Safe) rather than leaking it.
- **The reader registry only grows**, and the writer side scales with thread count (see §4.6). A thread that ever called `enter()` keeps a node for the process lifetime, and every grace-period poll walks all of them: ~3.4 ns per registered node, so 12 ns at 10 threads but **34 µs per poll at 10,000**, with one never-returned allocation per thread. Bounding it needs a fixed-capacity slot array ([roadmap, Research](../development/roadmap.md#research--experimental)); `stats().registered_threads` makes the growth observable today.
- **No out-of-process or sandboxed mode.** Everything is in-process; a hostile plugin is not contained.

## 7. References

- [`benchmark.md`](benchmark.md) — the ABIX numbers quoted above, with method and environment.
- `memory/windows-benchmark.md` §8/§10 — per-family rerun (one process per family, `repetitions=5`) and the two benchmark corrections.
- [`docs/development/competitors.md`](../development/competitors.md) — the tooling landscape (libabigail, ABI Compliance Checker, Conan, vcpkg, Wasm CM, bindgen/SWIG, gRPC, CMake/Bazel) and ABIX's positioning against it.
- [`docs/abix/api.md`](../abix/api.md) — the actual contract: `Domain`, `Deadline`, `Function::operator()`, `Object` and `ReclaimOutcome`.
- `tools/run_bench.py` — reproduce any row in §2 with `python tools/run_bench.py <family> --no-extra --repetitions 5`.
