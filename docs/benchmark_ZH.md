# 性能基准与优化

本文档涵盖 ABIX Micro-RCU 的完整性能工程过程：
从瓶颈定位到优化，再到最终验证与局限性分析。

---

## 1. 基准测试范围

### 测试内容

```
Micro-RCU
├── Reader Fast Path        — enter() / exit() / protected load
├── synchronize()           — epoch 推进 + grace period
├── WriterLock              — 多 writer 竞争
├── EpochAdvance            — global_epoch 原子 RMW
└── Reader Scan             — 线程列表遍历

完整 workload
├── Synthetic Mixed         — 所有线程执行完整调度
└── Role-Based              — N 个 reader + 1 个 writer（真实场景建模）

参数分析
├── Epoch Batch             — SKL_ABIX_RCU_EPOCH_BATCH
├── Publish Batch           — SKL_ABIX_RCU_BATCH_PUBLISH
└── Workload Threshold      — retire 批量回收阈值
```

### 优先级

**Role-Based 是 ABIX 最重要的性能指标。**

```
Role-Based
    >
Synthetic Mixed
    >
SyncPhase
    >
Microbench
```

Role-Based 直接建模了 ABIX 的真实使用场景：大量并发 reader 执行 `enter()`/`exit()`，一个 writer 线程执行 `retire()` + `synchronize()`。其他所有基准测试为设计决策提供辅助证据。

---

## 2. 测试环境

### Intel 主流高性能平台

```
Intel Core i7-14700K
64 GB DDR5-6000
Windows 11
SMT 关闭
P/E 异构核心（使用 20 个 P-core）
```

### Apple Silicon 平台

```
Apple M4
macOS
```

M4 主要用于模拟低功耗/边缘设备行为；14700K 主要用于模拟主流高性能设备。

> [!IMPORTANT]
> 不要直接比较不同 CPU 架构的绝对吞吐量来进行简单排名。不同平台有不同的核心数量、频率特性和内存子系统。

---

## 3. 基线：定位瓶颈

### 3.1 ThreadState — 共享写 vs 线程本地写

线程本地 cache-line 的写入非常快：

```
per-thread write ≈ 1.7–3.6 ns
```

但共享 cache-line 写入随线程数急剧恶化。在 10 个线程时：

```
shared write ≈ 308 ns
```

这确立了根本问题：

> **共享 cache-line 写入是瓶颈，而非普通读取。**

### 3.2 WriterLock

WriterLock 的竞争曲线与共享 ThreadState 写入曲线高度一致。这证实了：

> **WriterLock 竞争主要源于 cache-line bouncing。**

### 3.3 EpochAdvance

进一步测量发现：

```
EpochAdvance > WriterLock
```

额外成本主要来自：

```cpp
_global_epoch.fetch_add(...);
```

这是一个共享原子 RMW 操作——每次 `synchronize()` 调用都必须递增全局 epoch 计数器，而每次递增都触发一次 cache-line 所有权转移。

### 3.4 性能链

完整的瓶颈链为：

```
synchronize()
    ↓
WriterLock            (cache-line bouncing)
    ↓
publish                (retire 列表操作)
    ↓
global_epoch RMW       (共享原子 fetch_add)
    ↓
共享 cache-line bouncing
```

这是整个 benchmark 文档中最重要的性能分析链。后续的每一项优化都针对这条链中的一个或多个环节。

---

## 4. Cache-Line 布局优化

测试了三组配置：

| 配置 | 说明 |
|------|------|
| `no_alignas` | 无显式 cache-line 对齐 |
| `epoch_alignas` | 仅 `_epoch` 为 `alignas(64)` |
| `all_alignas` | 所有成员均为 `alignas(64)` |

### 4.1 `alignas(64) _global_epoch` — 值得保留

20T SyncPhase 改进：

```
≈ -43%
```

Role-Based 工作负载也有明显收益。`_epoch` 结构体是整个系统中竞争最激烈的 cache-line——每个 reader 的 `enter()` 从中读取，每次 `synchronize()` 向其写入。

### 4.2 全部 `alignas` — 不值得

对所有成员应用 `alignas(64)`：

```
sizeof: 56 B → 256 B
```

低线程数性能明显下降。更大的内存占用和降低的 cache 利用率超过了为非竞争字段隔离带来的收益。

### 4.3 最终策略

> **只隔离已证明存在共享写竞争的字段，不进行全局 cache-line padding。**

---

## 5. Epoch 推进批处理

### 原理

无批处理（B1）：

```
synchronize → advance epoch
synchronize → advance epoch
synchronize → advance epoch
```

有批处理（例如 B16）：

```
synchronize
synchronize
...
synchronize（16 次调用）
    ↓
advance epoch（仅一次）
```

目标是降低 `_global_epoch` 共享原子 RMW 操作的频率。每次对 `_global_epoch` 的 `fetch_add` 都会触发跨所有核心的 cache-line 所有权转移。通过批处理，只有 1/N 的 `synchronize()` 调用实际执行昂贵的 RMW。

### 测试的配置

```
B1  — 无批处理（每次 synchronize 都推进 epoch）
B8  — 每 8 次 synchronize 调用推进一次 epoch
B16 — 每 16 次 synchronize 调用推进一次 epoch
```

---

## 6. B8 vs B16 — 详细对比

### 6.1 SyncPhase

B8 和 B16 基本处于同一水平——没有明显的可扩展性退化。两者均比 B1 有显著改进。

### 6.2 Synthetic Mixed

B8 和 B16 互有胜负——在所有线程数和工作负载配置下，没有一致的绝对赢家。

### 6.3 Role-Based

这是决策的主要依据。

**20T — 14700K 最重要的线程数（全部 P-core）：**

| Workload    | Threads |            B8 |           B16 | Delta |
| ----------- | ------: | ------------: | ------------: | ----: |
| read-heavy  |     20T |      9.44 G/s | **11.67 G/s** |  +24% |
| balanced    |     20T |     11.66 G/s | **11.90 G/s** |   +2% |
| write-heavy |     20T | **13.94 G/s** |     13.53 G/s |   −3% |

> **更大的 epoch batch 在高并发 read-mostly 工作负载下可以进一步降低共享 epoch 更新开销。**

write-heavy 场景下 B16 有轻微回归，这在意料之中：当写入频繁时，延迟 epoch 推进可能导致 retire 列表累积，增加每次回收的成本。

---

## 7. WorkloadThreshold 与异常噪声

### 初始观察

在初始测试中，B16 在 `threshold=16` 和 `threshold=32` 时出现了显著的吞吐量下降。这引发了对 batch size 与 retire threshold 之间周期性耦合的担忧。

### 细粒度扫描

为调查此问题，进行了细粒度扫描：

```
14, 15, 16, 17, 18
30, 31, 32, 33
```

### 修正后的结果

```
thr16: +12%
thr32: +10%
```

原先的 `thr16/32` 暴跌无法复现。这不是 B16 的结构性性能问题。

> **这是一个重要的实验方法论教训：在整数倍数字（16, 32）出现单个异常数据点时，应在断定其为系统性效应之前进行细粒度邻域扫描。**

---

## 8. Benchmark 噪声

使用两种不同的技术来处理噪声：

### 细粒度扫描

用于**发现/排除结构性模式**。例如：

```
15 → 正常
16 → 崩溃？
17 → 正常
```

如果模式是真实的（例如 batch/threshold 周期性耦合），它会在可预测的间隔出现。单点异常几乎可以确定是噪声。

### 重复运行

用于**降低单次系统噪声对结论的影响**。对于正式 benchmark，记录：

```
中位数
p90 / p95
方差 / CV
```

而不仅仅是单次运行结果。

### 已知噪声来源（Windows / 14700K）

- P/E 核心调度
- 线程迁移
- Turbo Boost 状态切换
- 热状态
- CPU 频率缩放
- Cache 状态（冷 vs 热）

这些不是 benchmark 的缺陷——它们是真实的平台特性。目标是将其与真正的性能模式区分开来。

---

## 9. 跨平台参数解释

`SKL_ABIX_RCU_EPOCH_BATCH` **不是** CPU 架构常量。

**不要**使用简单的映射，例如：

```cpp
// 错误做法
#if defined(__x86_64__)
#  define SKL_ABIX_RCU_EPOCH_BATCH 16
#elif defined(__aarch64__)
#  define SKL_ABIX_RCU_EPOCH_BATCH 8
#endif
```

Batch size 受以下因素影响：

```
CPU 微架构
+
Cache 一致性协议
+
核心拓扑
+
工作负载特征
```

相同的 batch size 在不同平台上可能表现不同，即使 ISA 相同。务必在目标平台上进行 benchmark。

---

## 10. 编译期配置

[rcu_domain.h](../abix/rcu_domain.h) 中的最终配置接口：

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

| 宏 | 作用 |
|----|------|
| `SKL_ABIX_CACHE_LINE_SIZE` | 竞争字段的 cache-line 隔离 |
| `SKL_ABIX_RCU_EPOCH_BATCH` | Epoch 推进批处理——多少次 `synchronize()` 调用后才推进 `_global_epoch` |
| `SKL_ABIX_RCU_BATCH_PUBLISH` | Retire 批量大小——在发布到全局 retire 列表之前累积多少个 retire 对象 |

---

## 11. 推荐配置

### 默认配置

```cpp
SKL_ABIX_CACHE_LINE_SIZE   = 64
SKL_ABIX_RCU_EPOCH_BATCH   = 8
SKL_ABIX_RCU_BATCH_PUBLISH = 64
```

**理由：** B8 是更保守的默认配置。它提供了大部分批处理收益，同时不会过度延迟 epoch 推进。B16 可作为可选调优参数进行测试。

### 可选调优

对于满足以下条件的工作负载：

```
高并发
+
read-mostly
+
频繁 synchronize
```

可以考虑测试：

```
B8
B16
```

在当前 14700K 平台上，Role-Based 结果显示 B16 具有竞争力，尤其是在高线程数的 read-heavy 工作负载下。

**在更改默认值之前，务必在目标平台上进行 benchmark。**

---

## 12. 范围与局限性

Benchmark 结果仅展示 ABIX 在其目标工作负载下的行为。不要将这些结论推广。

### Hash Container

```
只读 / 极少修改
→ 不是通用并发 hash map
```

### Micro-RCU

```
大量 reader
+
少量 writer
+
短读临界区
→ 不是通用 RCU 实现
```

### Epoch Batching

```
高频 synchronize
+
共享 epoch 写竞争
→ ABIX 特化优化
```

以上所有均为针对 ABIX 自身 read-mostly 工作负载模式的 **ABIX 特化优化**。它们并非关于 RCU 或并发容器应如何设计的通用主张。

---

## 参考文献

- [Intel Core i7-14700K 规格](https://www.intel.com/content/www/us/en/products/sku/236778/intel-core-i7-processor-14700k-33m-cache-up-to-5-60-ghz/specifications.html)
- [M. Desnoyers et al., "User-Level Implementations of Read-Copy Update"](https://doi.org/10.1109/TPDS.2011.159)
- [Paul E. McKenney, "Is Parallel Programming Hard, And, If So, What Can You Do About It?"](https://kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook.html)