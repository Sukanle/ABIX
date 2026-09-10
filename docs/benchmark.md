# ABIX Benchmark & Performance Analysis

> Documentation Notes
> Data Source: This report is generated based on the results of Google Benchmark tests located in the bench/ directory.
>> Authoring Method: Test data is aggregated via scripts; text analysis and formatting are assisted by AI to reduce human error in manual compilation.
>> Maintenance Policy: Absolute performance numbers may vary due to differences in hardware environments and compiler versions. This report is intended to demonstrate performance trends and comparative analysis (e.g., HashIndex vs. Linear Scan), and does not constitute an absolute performance guarantee. To reproduce or verify the results, please run the CMake Benchmark targets directly from the project root (source code is fully provided).
>
> It records the benchmark scope, method, and conclusions that can be checked against the current source tree. Throughput values are platform-specific observations, not cross-machine guarantees.

## 1. Executive Summary

- ABIX targets **read-mostly** workloads: many readers execute short read-side critical sections while a small number of writers perform `retire()` and `synchronize()`.
- `bench_all` covers atomics, calls, lookup, plugin reload, EBR fast paths, grace periods, false sharing, and EBR workloads.
- `BM_EBR_SyncPhase` isolates synchronization cost. `BM_EBR_Workload/role_based` is the more representative N-readers/one-writer model.
- AMC extraction, `.abix` serialization, compatibility analysis, and generated metadata registration are build/control-plane operations. They are not included in these DLL-call or EBR hot-path measurements, and this document makes no latency claim for them.
- `synthetic_mixed` keeps total work fixed across thread counts and is suitable for scalability comparisons. `role_based` changes the writer operation count as readers are added; it models role separation, but is not a fixed-work scaling curve.
- The current Linux run successfully built and executed `bench_all`, and the fixed-work validation passed. CPU frequency scaling was enabled and short-run variance was high, so the current B8/B16 run does not justify a performance winner or a fixed percentage claim.

## 2. What Is Measured

| Layer | Benchmarks | Purpose |
|---|---|---|
| Fast path | `BM_EBR_EnterExit`, `BM_EBR_ProtectedLoad` | Basic reader entry/exit and protected-load cost |
| Synchronization | `BM_EBR_SyncPhase` | Contention and scaling of `synchronize()` |
| Reclamation | `BM_EBR_GracePhase`, `BM_EBR_ReclaimBatch` | Grace-period and reclamation costs |
| Role workload | `BM_EBR_Workload/synthetic_mixed` | Same complete schedule on every thread; fixed total work |
| Role workload | `BM_EBR_Workload/role_based` | N readers plus one writer; target usage model |
| Microarchitecture | False-sharing and atomic-contention tests | Evidence for shared writes, atomic RMW, and cache-line isolation |

`bench_all` also contains call overhead, linear-vs-HashIndex lookup crossover, ABI resolve, reload, and DLL benchmarks. Their metrics are different and should not be merged into one overall ranking.

## 3. Workload Semantics

`workload_runner` defines `read_heavy`, `balanced`, and `write_heavy` schedules. The current validation confirms that `synthetic_mixed` uses 10,000,000 total operations for every tested thread count. The schedule includes 9,000,000 reads, 900,000 retires, and 100,000 synchronizations.

In `role_based`, only thread 0 performs retire/sync work and the other threads perform reads. A decreasing retire/sync count as the thread count increases is therefore expected behavior, not a benchmark failure. Use `synthetic_mixed` for fixed-work scaling and `role_based` for the real role split.

## 4. Current Measurement Record

```text
Date:        2026-09-08
System:      Linux 7.2.2-1-cachyos
CPU:         Intel Core i7-14700K, 20 logical / 20 physical CPUs
NUMA:        1 node
Compiler:    GNU 16.2.1
Build:       Release
Benchmark:   Google Benchmark found and linked
Default:     SKL_ABIX_RCU_EPOCH_BATCH=8
```

This environment exposes 20 CPUs and does not provide the Windows P-core/E-core setup described by older notes. Google Benchmark also reports that CPU scaling is enabled, so frequency changes add noise to real-time measurements.

### Reproduction Command

```bash
cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release --target bench_all --parallel 4

build/Release/bin/bench_all \
  --benchmark_filter='BM_EBR_Workload/role_based/(read_heavy|balanced|write_heavy)/20T' \
  --benchmark_min_time=1s \
  --benchmark_repetitions=10 \
  --benchmark_report_aggregates_only=true
```

The current check did build and execute the same `bench_all` target. Its validation printed `All counts match — synthetic mixed is truly fixed-work`. The short run confirms that the path works; it is not a release-grade numerical baseline.

### Governor Follow-up

During the follow-up run, the governor query reported `performance`. The current execution environment could not interactively provide the `sudo` password, so this session cannot establish that the switch command itself changed the state; the value is recorded only as an observed runtime condition.

Using the updated `ebr_profile_bench` entry point, the 20-thread `BM_EBR_SyncPhase` short run produced a median of approximately `2.50 us` and a CV of approximately `4.95%` across three repetitions. This confirms that the new `MaybeReenterWithoutASLR` entry-point change runs correctly, but it is not enough for a release-grade baseline.

### B8/B16 Status

An additional build with `SKL_ABIX_RCU_EPOCH_BATCH=16` was compiled and run against the 20-thread role-based workloads. The two short runs had coefficients of variation of roughly 29% to 44%, with CPU scaling enabled. The defensible conclusion is therefore:

> B8 and B16 both build and run; this run is insufficient to decide which is faster or to claim a fixed percentage improvement.

Older statements such as "B16 improves throughput by 24%" or exact `thr16`/`thr32` results should be treated as historical notes unless their raw output, source revision, compiler, command line, and repeated-run statistics are available.

## 5. Analysis Method

1. Confirm a Release build, correct operation counts, and the intended benchmark filter.
2. Use `SyncPhase` to identify synchronization degradation with thread count.
3. Use `synthetic_mixed` for fixed-work scalability.
4. Use `role_based` for target read-mostly throughput.
5. Sweep neighboring values around B8/B16 and threshold values instead of testing only round numbers.
6. Repeat at least 10 times and record median, p90, standard deviation, and CV. When CV is high, report the result as inconclusive.

The potential contention chain is:

```text
reader enter/exit
    -> shared epoch load
synchronize
    -> writer contention
    -> epoch atomic RMW
    -> reader scan / grace period
    -> retire publication and reclamation
```

`alignas`, epoch batching, and publish batching should be retained only when measurements support the corresponding change. Global padding can reduce cache utilization, while batching can increase reclamation delay; neither is a universal optimization.

## 6. Configuration Guidance

| Parameter | Purpose |
|---|---|
| `SKL_ABIX_RCU_CACHE_LINE_SIZE` | Cache-line isolation size for contended fields |
| `SKL_ABIX_RCU_EPOCH_BATCH` | Synchronization calls per epoch advancement |
| `SKL_ABIX_RCU_BATCH_PUBLISH` | Retired objects accumulated before global publication |

The current default epoch batch is 8. Treat it as a conservative default, not as an optimum for every CPU and workload. Before changing it, rerun a Release benchmark on the target platform with stable CPU affinity, frequency, and enough repetitions.

## 7. Limitations

- These results describe the current implementation under the stated workloads; they are not general claims about RCU or concurrent containers.
- Absolute throughput across Linux, Windows, macOS, and different CPU topologies is not directly comparable.
- Frequency scaling, migration, Turbo, temperature, NUMA, ASLR, and background activity affect short benchmarks.
- Historical numbers without raw output and complete environment metadata cannot be independently reproduced.
- Performance tests do not replace correctness tests. Run the full test suite after changing reclamation or synchronization code.

## 8. Full Benchmark Run

### Execution

With the governor observed as `performance`, the Release build ran each benchmark executable in `build/Release/bin`:

```bash
for bench in atomic_bench call_bench reload_bench falseSharing_bench \
  lookup_bench abix_resolve_bench abix_lookup_cross_bench \
  stress_bench ebr_bench ebr_profile_bench; do
  (cd build/Release/bin && LD_LIBRARY_PATH=. ./$bench \
    --benchmark_min_time=0.1s \
    --benchmark_repetitions=3 \
    --benchmark_report_aggregates_only=true)
done
```

The benchmarks must be launched from `build/Release/bin` with `LD_LIBRARY_PATH=.`. `dll_path()` produces a bare filename, and Linux `dlopen("hotcache_dll.so")` does not automatically search the current directory.

`bench_all` was also run. Its current unity build uses the `ebr_profile_bench` `main`, so it registers only part of the source suites by default. It must not be treated as the only complete benchmark entry point.

### Representative Results

The following are median values from 2026-09-08 on this machine. Each benchmark used three repetitions; these values record the current implementation and are not release baselines.

| Category | Benchmark | Result |
|---|---|---:|
| EBR fast path | `BM_EBR_EnterExit` | 0.491 ns |
| EBR fast path | `BM_EBR_ProtectedLoad` | 0.530 ns |
| EBR synchronization | `BM_EBR_SyncPhase/20T` | 3.23 us wall / 1.46 us CPU |
| EBR role-based | read-heavy / 20T | 12.34 G/s |
| EBR role-based | balanced / 20T | 11.70 G/s |
| EBR role-based | write-heavy / 20T | 11.92 G/s |
| Reload | logical | 3.00 us |
| Reload | real DLL | 2.96 us |
| Calls | `BM_ABIX_Call` | 3.91 ns |
| Lookup crossover | uniform, linear, 16K | 1.59 us |
| Lookup crossover | uniform, hash index, 16K | 13.2 ns |
| False sharing | packed, 16T | 128 ns |
| False sharing | padded, 16T | 3.87 ns |

The limited conclusions are that shared-write contention grows with thread count in the packed false-sharing test while the padded variant stays around 3.4-4.0 ns; HashIndex is substantially faster than linear scanning for the 16K uniform lookup; and 20-thread role-based EBR throughput is around 12 G/s. These are platform-specific observations, and the write-heavy role-based CV was about 14.7%.

### Incorrect Invocation Reproduction

When first run directly from the project root, the default `lookup_bench` run and these four individual filters exited with code 139 (segmentation fault):

```text
BM_FindIndex_Only
BM_Resolve_Linear
BM_Resolve_Linear_80_20
BM_Resolve_Linear_Random
```

ASAN located the crash at `g_image->index` in `bench/lookup_bench.cpp:26`: `dll_object::load()` failed, leaving `g_image` null. With `LD_LIBRARY_PATH=.` all of these tests complete: `BM_FindIndex_Only` is about 33.6 ns, `BM_Resolve_Linear` about 232 ns, and `BM_Resolve_WithEBR` about 34.3 ns. The issue is the launch environment combined with an unchecked load result, not a lookup algorithm crash.

## 9. References

- [ABIX README](../README.md)
- [Google Benchmark User Guide](https://github.com/google/benchmark)
- [User-Level Implementations of Read-Copy Update](https://doi.org/10.1109/TPDS.2011.159)
