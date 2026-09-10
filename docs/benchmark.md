# Benchmark & Performance

This document covers the full performance engineering process for ABIX's Micro-RCU:
from bottleneck identification through optimization to final validation and limitations.

---

## 1. Benchmark Scope

### What We Measure

```
Micro-RCU
├── Reader Fast Path        — enter() / exit() / protected load
├── synchronize()           — epoch advancement + grace period
├── WriterLock              — multi-writer contention
├── EpochAdvance            — global_epoch atomic RMW
└── Reader Scan             — thread list traversal

完整 workload
├── Synthetic Mixed         — all threads execute full schedule
└── Role-Based              — N readers + 1 writer (real-world model)

参数分析
├── Epoch Batch             — SKL_ABIX_RCU_EPOCH_BATCH
├── Publish Batch           — SKL_ABIX_RCU_BATCH_PUBLISH
└── Workload Threshold      — retire batching threshold
```

### Priority

**Role-Based is the most important performance metric for ABIX.**

```
Role-Based
    >
Synthetic Mixed
    >
SyncPhase
    >
Microbench
```

Role-Based directly models real-world ABIX usage: many concurrent readers performing `enter()`/`exit()` and one writer thread performing `retire()` + `synchronize()`. All other benchmarks provide supporting evidence for design decisions.

---

## 2. Test Environment

### Intel Mainstream High-Performance Platform

```
Intel Core i7-14700K
64 GB DDR5-6000
Windows 11
SMT disabled
P/E heterogeneous cores (20 P-cores used)
```

### Apple Silicon Platform

```
Apple M4
macOS
```

M4 is used primarily to simulate low-power/edge device behavior; 14700K is used to simulate mainstream high-performance devices.

> [!IMPORTANT]
> Do not directly compare absolute throughput numbers across different CPU architectures for simple ranking purposes. Different platforms have different core counts, frequency characteristics, and memory subsystems.

---

## 3. Baseline: Identifying the Bottleneck

### 3.1 ThreadState — Shared vs Per-Thread Write

Per-thread write to a thread-local cache line is fast:

```
per-thread write ≈ 1.7–3.6 ns
```

But shared cache-line writes degrade rapidly with thread count. At 10 threads:

```
shared write ≈ 308 ns
```

This establishes the fundamental problem:

> **Shared cache-line writes are the bottleneck, not ordinary reads.**

### 3.2 WriterLock

The WriterLock contention curve closely matches the shared ThreadState write curve. This confirms:

> **WriterLock contention is primarily caused by cache-line bouncing.**

### 3.3 EpochAdvance

Further measurement reveals:

```
EpochAdvance > WriterLock
```

The additional cost comes primarily from:

```cpp
_global_epoch.fetch_add(...);
```

This is a shared atomic RMW operation — every `synchronize()` call must increment the global epoch counter, and every increment triggers a cache-line ownership transfer.

### 3.4 The Performance Chain

The complete bottleneck chain is:

```
synchronize()
    ↓
WriterLock            (cache-line bouncing)
    ↓
publish               (retire list manipulation)
    ↓
global_epoch RMW      (shared atomic fetch_add)
    ↓
shared cache-line bouncing
```

This is the most important performance analysis chain in the entire benchmark document. Every subsequent optimization targets one or more links in this chain.

---

## 4. Cache-Line Layout Optimization

Three configurations were tested:

| Configuration | Description |
|---------------|-------------|
| `no_alignas` | No explicit cache-line alignment |
| `epoch_alignas` | Only `_epoch` is `alignas(64)` |
| `all_alignas` | All members are `alignas(64)` |

### 4.1 `alignas(64) _global_epoch` — Worth Keeping

20T SyncPhase improvement:

```
≈ -43%
```

Role-Based workloads also show measurable benefit. The `_epoch` struct is the most contended cache line in the entire system — every reader's `enter()` loads from it, every `synchronize()` writes to it.

### 4.2 Full `alignas` — Not Worth It

Applying `alignas(64)` to every member:

```
sizeof: 56 B → 256 B
```

And low-thread-count performance decreases noticeably. The larger memory footprint and reduced cache utilization outweigh the isolation benefits for fields that are not under heavy contention.

### 4.3 Final Strategy

> **Only isolate fields that have been proven to experience shared-write contention. Do not apply global cache-line padding.**

---

## 5. Epoch Advancement Batching

### Principle

Without batching (B1):

```
synchronize → advance epoch
synchronize → advance epoch
synchronize → advance epoch
```

With batching (e.g., B16):

```
synchronize
synchronize
...
synchronize (16 calls)
    ↓
advance epoch (only once)
```

The goal is to reduce the frequency of `_global_epoch` shared atomic RMW operations. Each `fetch_add` on `_global_epoch` triggers a cache-line ownership transfer across all cores. By batching, only 1/N of `synchronize()` calls actually perform the expensive RMW.

### Configurations Tested

```
B1  — no batching (advance every synchronize)
B8  — batch 8 synchronize calls per epoch advance
B16 — batch 16 synchronize calls per epoch advance
```

---

## 6. B8 vs B16 — Detailed Comparison

### 6.1 SyncPhase

B8 and B16 are essentially at the same level — there is no significant scalability regression. Both provide substantial improvement over B1.

### 6.2 Synthetic Mixed

B8 and B16 trade blows — neither is a consistent absolute winner across all thread counts and workload profiles.

### 6.3 Role-Based

This is the primary basis for decision-making.

**20T — the most important thread count for 14700K (all P-cores):**

| Workload    | Threads |            B8 |           B16 | Delta |
| ----------- | ------: | ------------: | ------------: | ----: |
| read-heavy  |     20T |      9.44 G/s | **11.67 G/s** |  +24% |
| balanced    |     20T |     11.66 G/s | **11.90 G/s** |   +2% |
| write-heavy |     20T | **13.94 G/s** |     13.53 G/s |   −3% |

> **Larger epoch batch sizes can further reduce shared epoch update overhead in high-concurrency read-mostly workloads.**

The write-heavy case shows a slight regression with B16, which is expected: when writes are frequent, delaying epoch advancement can cause retire lists to accumulate, increasing the per-collect cost.

---

## 7. WorkloadThreshold and Anomalous Noise

### Initial Observation

During initial testing, B16 showed a significant throughput drop at `threshold=16` and `threshold=32`. This raised concerns about periodic coupling between batch size and retire threshold.

### Fine-Grained Sweep

To investigate, a fine-grained sweep was performed:

```
14, 15, 16, 17, 18
30, 31, 32, 33
```

### Corrected Results

```
thr16: +12%
thr32: +10%
```

The original `thr16/32` collapse could not be reproduced. It was not a structural performance problem with B16.

> **This is an important experimental methodology lesson: a single anomalous data point at a round number (16, 32) should trigger a fine-grained neighborhood sweep before concluding it is a systematic effect.**

---

## 8. Benchmark Noise

Two distinct techniques are used to handle noise:

### Fine-Grained Sweep

Used to **discover/exclude structural patterns**. For example:

```
15 → normal
16 → crash?
17 → normal
```

If the pattern is genuine (e.g., batch/threshold periodic coupling), it will appear at predictable intervals. A single-point anomaly is almost certainly noise.

### Repeated Runs

Used to **reduce the impact of single-run system noise on conclusions**. For formal benchmarks, record:

```
median
p90 / p95
variance / CV
```

Not just a single run result.

### Known Noise Sources (Windows / 14700K)

- P/E core scheduling
- Thread migration
- Turbo Boost state transitions
- Thermal state
- CPU frequency scaling
- Cache state (cold vs warm)

These are not defects in the benchmark — they are real-world platform characteristics. The goal is to distinguish them from genuine performance patterns.

---

## 9. Cross-Platform Parameter Interpretation

`SKL_ABIX_RCU_EPOCH_BATCH` is **not** a CPU architecture constant.

Do **not** use simple mappings like:

```cpp
// WRONG
#if defined(__x86_64__)
#  define SKL_ABIX_RCU_EPOCH_BATCH 16
#elif defined(__aarch64__)
#  define SKL_ABIX_RCU_EPOCH_BATCH 8
#endif
```

Batch size is affected by:

```
CPU microarchitecture
+
Cache coherence protocol
+
Core topology
+
Workload characteristics
```

The same batch size may perform differently on different platforms even with the same ISA. Always benchmark on the target platform.

---

## 10. Compile-Time Configuration

The final configuration interface in [rcu_domain.h](../abix/rcu_domain.h):

```cpp
#ifndef SKL_ABIX_RCU_EPOCH_BATCH
#  define SKL_ABIX_RCU_EPOCH_BATCH 8
#endif

#ifndef SKL_ABIX_CACHE_LINE_SIZE
#  define SKL_ABIX_CACHE_LINE_SIZE 64
#endif

#ifndef SKL_ABIX_RCU_BATCH_PUBLISH
#  define SKL_ABIX_RCU_BATCH_PUBLISH 64
#endif
```

| Macro | Purpose |
|-------|---------|
| `SKL_ABIX_CACHE_LINE_SIZE` | Cache-line isolation for contended fields |
| `SKL_ABIX_RCU_EPOCH_BATCH` | Epoch advancement batching — how many `synchronize()` calls before advancing `_global_epoch` |
| `SKL_ABIX_RCU_BATCH_PUBLISH` | Retire batch size — how many retired objects to accumulate before publishing to the global retire list |

---

## 11. Recommended Configuration

### Default

```cpp
SKL_ABIX_CACHE_LINE_SIZE   = 64
SKL_ABIX_RCU_EPOCH_BATCH   = 8
SKL_ABIX_RCU_BATCH_PUBLISH = 64
```

**Rationale:** B8 is the more conservative default. It provides most of the batching benefit without excessively delaying epoch progression. B16 can be tested as an optional tuning parameter.

### Optional Tuning

For workloads with:

```
high concurrency
+
read-mostly
+
frequent synchronize
```

Consider testing:

```
B8
B16
```

On the current 14700K platform, Role-Based results show B16 is competitive, especially for read-heavy workloads at high thread counts.

**Always benchmark on the target platform before changing the default.**

---

## 12. Scope & Limitations

Benchmark results only demonstrate ABIX's behavior under its target workload. Do not generalize these conclusions.

### Hash Container

```
Read-only / rarely modified
→ Not a general-purpose concurrent hash map
```

### Micro-RCU

```
Many readers
+
Few writers
+
Short read-side critical sections
→ Not a general-purpose RCU implementation
```

### Epoch Batching

```
High-frequency synchronize
+
Shared epoch write contention
→ ABIX-specific optimization
```

All of the above are **ABIX-specific optimizations** for ABIX's own read-mostly workload pattern. They are not claims about how RCU or concurrent containers should be designed in general.

---

## References

- [Intel Core i7-14700K Specification](https://www.intel.com/content/www/us/en/products/sku/236778/intel-core-i7-processor-14700k-33m-cache-up-to-5-60-ghz/specifications.html)
- [M. Desnoyers et al., "User-Level Implementations of Read-Copy Update"](https://doi.org/10.1109/TPDS.2011.159)
- [Paul E. McKenney, "Is Parallel Programming Hard, And, If So, What Can You Do About It?"](https://kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook.html)