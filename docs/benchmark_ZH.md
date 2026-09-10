# ABIX 基准测试与性能分析

> 文档说明
>> 数据来源：本报告基于 bench/ 目录下的 Google Benchmark 测试程序运行结果生成。
>> 编写方式：测试数据由脚本汇总，文本分析及排版借助 AI 辅助完成，以降低手动整理的人为误差。
>> 维护策略：由于硬件环境及编译器版本差异，绝对数值可能发生变化。本报告旨在展示性能趋势与横向对比（如 HashIndex vs 线性扫描），不作为绝对的性能承诺。 如需复现或验证，请直接运行项目根目录下的 CMake Benchmark 目标（源码已全部提供）
>
> 本文记录基准测试的范围、方法和可以由当前代码复核的结论。文中的吞吐量不是跨机器的承诺，也不应脱离测试平台、编译器和负载模型单独引用。

## 1. 结论摘要

- ABIX 的主要目标是 **read-mostly** 场景：多个 reader 执行短读临界区，少数 writer 执行 `retire()` 和 `synchronize()`。
- `bench_all` 当前包含原子操作、函数调用、查找、插件 reload、EBR fast path、grace period、false sharing 以及 EBR workload 等多组测试。
- 对 EBR 扩展性，`BM_EBR_SyncPhase` 和 `BM_EBR_Workload/role_based` 比单一 microbench 更有参考价值；前者隔离同步成本，后者模拟真实 reader/writer 角色。
- AMC 提取、`.abix` 序列化、兼容性分析和生成 metadata 的注册属于构建/控制平面操作，不包含在本文件的 DLL 调用或 EBR 热路径测量中；本文不对这些操作作延迟承诺。
- `synthetic_mixed` 是固定工作量测试，适合比较线程数的扩展性；`role_based` 的 writer 操作量会随线程数变化，适合模拟实际角色分工，但不适合直接当作固定工作量 scaling 曲线。
- 本次 Linux 实测成功编译并运行了 `bench_all`，固定工作量校验通过；但 CPU scaling 开启，短跑结果噪声很大，因此没有把 B8/B16 的本次差异写成性能结论。

## 2. 测试对象

### EBR / Micro-RCU

| 层次 | 测试 | 用途 |
|---|---|---|
| fast path | `BM_EBR_EnterExit`、`BM_EBR_ProtectedLoad` | reader 进入/退出和受保护读取的基本开销 |
| 同步 | `BM_EBR_SyncPhase` | 多线程 `synchronize()` 的竞争与扩展性 |
| 回收 | `BM_EBR_GracePhase`、`BM_EBR_ReclaimBatch` | grace period 和批量回收成本 |
| 角色负载 | `BM_EBR_Workload/synthetic_mixed` | 所有线程执行同一完整 schedule，固定总工作量 |
| 角色负载 | `BM_EBR_Workload/role_based` | N 个 reader + 1 个 writer，接近目标使用方式 |
| 微架构 | false sharing / atomic contention | 判断共享写、原子 RMW 和 cache-line 隔离是否可能成为瓶颈 |

### ABIX 其他路径

`bench_all` 还覆盖调用方式、线性查找与 HashIndex 交叉点、ABI resolve、reload 和 DLL 场景。这些测试的指标不同，不能与 EBR 吞吐量混成一张"总排名"表。

## 3. 工作负载语义

`workload_runner` 定义了 `read_heavy`、`balanced` 和 `write_heavy` 三类 schedule。当前实现的校验输出确认：

```text
synthetic_mixed: 每个线程数均为 10,000,000 total ops
read_heavy:      9,000,000 reads + 900,000 retires + 100,000 syncs
```

`role_based` 中只有 thread 0 执行 retire/sync，其他线程只执行 read。因此线程数增加时，retire 和 sync 数量下降是设计行为，不是 benchmark 失败。需要比较固定工作量时使用 `synthetic_mixed`；需要模拟真实读写角色时使用 `role_based`。

## 4. 当前实测记录

### 环境

```text
日期:       2026-09-08
系统:       Linux 7.2.2-1-cachyos
CPU:        Intel Core i7-14700K，20 logical CPUs / 20 physical cores
NUMA:       1 node
编译器:     GNU 16.2.1
构建类型:   Release
Google Benchmark: 已发现并链接
默认配置:   SKL_ABIX_RCU_EPOCH_BATCH=8
```

`lscpu` 显示本环境只暴露 20 个 CPU，不能据此复述 Windows/P-core/E-core 测试环境。Google Benchmark 还报告 CPU scaling 已开启，所以频率变化会增加实时测量噪声。

### 执行命令

```bash
cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release --target bench_all --parallel 4

build/Release/bin/bench_all \
  --benchmark_filter='BM_EBR_Workload/role_based/(read_heavy|balanced|write_heavy)/20T' \
  --benchmark_min_time=1s \
  --benchmark_repetitions=10 \
  --benchmark_report_aggregates_only=true
```

本次检查实际使用了相同的 `bench_all` 可执行文件完成编译和短跑；固定工作量检查输出为 `All counts match — synthetic mixed is truly fixed-work`。短跑只用于确认路径可运行，不作为发布级数值基线。

### 调频策略复测

后续复测时，系统 governor 查询结果为 `performance`。由于当前执行环境不能交互输入 `sudo` 密码，无法在本次会话中确认切换命令是由本会话完成的；该状态只作为运行时观测记录。

使用新入口运行 `ebr_profile_bench` 后，20T `BM_EBR_SyncPhase` 的 3 次短跑结果为：中位数约 `2.50 us`，CV 约 `4.95%`。这说明加入 `MaybeReenterWithoutASLR` 后程序可以正常运行，但样本数和运行时长仍不足以形成发布级基线。

### B8/B16 对照状态

额外配置了 `SKL_ABIX_RCU_EPOCH_BATCH=16` 并运行了 20T role-based 对照。两组短跑的变异系数约为 29%～44%，且受到 CPU scaling 影响，结果不稳定。因此当前证据只能支持：

> B8 和 B16 都能正常构建和运行；本次运行不足以判定谁更快，也不足以支持固定的百分比收益。

原文中诸如"B16 提升 24%"和 `thr16/thr32` 的具体数字，如果没有附带原始输出、提交版本、编译器、运行参数和重复统计，应视为历史记录而不是当前可复核结论。

## 5. 性能分析方法

建议按以下顺序分析，而不是先从单个异常数字推导原因：

1. 先确认 Release 构建、工作量计数和 benchmark filter 正确。
2. 使用 `SyncPhase` 判断同步路径是否随线程数退化。
3. 使用 `synthetic_mixed` 比较固定总工作量的扩展性。
4. 使用 `role_based` 判断目标 read-mostly 场景下的实际吞吐量。
5. 对 B8、B16 和 threshold 做邻域扫描，而不是只测 8、16、32 这些整齐数字。
6. 至少重复 10 次，记录 median、p90、标准差和 CV；CV 较高时只报告"结果不确定"。

需要重点关注的潜在瓶颈包括：

```text
reader enter/exit
    -> 共享 epoch 读取
synchronize
    -> writer 竞争
    -> epoch 原子 RMW
    -> reader scan / grace period
    -> retire 发布与回收
```

`alignas`、epoch batching 和 publish batching 只能在对应路径的测量结果支持时保留。全局 padding 可能降低 cache 利用率，批处理则可能增加回收延迟；它们都不是无条件优化。

## 6. 配置与建议

| 参数 | 作用 |
|---|---|
| `SKL_ABIX_RCU_CACHE_LINE_SIZE` | 竞争字段的 cache-line 隔离大小 |
| `SKL_ABIX_RCU_EPOCH_BATCH` | 多少次同步调用后推进一次 epoch |
| `SKL_ABIX_RCU_BATCH_PUBLISH` | retire 对象发布到全局列表前的批量大小 |

当前默认值为 epoch batch 8。它应被视为保守默认值，而不是对所有 CPU 和负载都最优。修改默认值前，应在目标平台上以 Release 构建、固定 CPU 亲和性、稳定频率和足够重复次数重新测量。

## 7. 局限性

- 当前结果只说明 ABIX 在给定实现和给定负载下的行为，不能推广为通用 RCU 或通用并发容器结论。
- Linux、Windows、macOS，以及不同 CPU 拓扑的绝对吞吐量不可直接排名。
- CPU scaling、线程迁移、Turbo、温度、NUMA、ASLR 和后台任务都会影响短 benchmark。
- 没有原始输出和完整环境信息的历史数字无法独立复核。
- 性能测试不能替代正确性测试；修改回收和同步逻辑后应先运行完整测试套件。

## 8. 完整基准执行记录

### 执行方式

本次在 governor 为 `performance` 的条件下，以 Release 构建逐个运行了 `build/Release/bin` 中的 benchmark 程序：

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

必须从 `build/Release/bin` 启动并设置 `LD_LIBRARY_PATH=.`。benchmark 使用 `dll_path()` 生成裸文件名，Linux 的 `dlopen("hotcache_dll.so")` 不会自动搜索当前目录。

`bench_all` 也已运行，但当前 unity build 使用 `ebr_profile_bench` 的 `main`，默认只注册 atomic、false-sharing 和 EBR fast-path 等部分测试；不能把它单独视为完整 benchmark 入口。

### 代表性结果

以下为 2026-09-08 本机结果，取 Google Benchmark 的 median；重复次数为 3，仅用于记录当前实现状态：

| 类别 | 测试 | 结果 |
|---|---|---:|
| EBR fast path | `BM_EBR_EnterExit` | 0.491 ns |
| EBR fast path | `BM_EBR_ProtectedLoad` | 0.530 ns |
| EBR 同步 | `BM_EBR_SyncPhase/20T` | 3.23 us wall / 1.46 us CPU |
| EBR role-based | read-heavy / 20T | 12.34 G/s |
| EBR role-based | balanced / 20T | 11.70 G/s |
| EBR role-based | write-heavy / 20T | 11.92 G/s |
| reload | logical | 3.00 us |
| reload | real DLL | 2.96 us |
| call | `BM_ABIX_Call` | 3.91 ns |
| lookup crossover | uniform, linear, 16K | 1.59 us |
| lookup crossover | uniform, hash index, 16K | 13.2 ns |
| false sharing | packed, 16T | 128 ns |
| false sharing | padded, 16T | 3.87 ns |

结果支持以下有限结论：共享写竞争在 packed false-sharing 测试中随线程数增加，而 padding 测试保持在约 3.4～4.0 ns；HashIndex 在 16K uniform lookup 中明显低于线性扫描；EBR role-based 20T 吞吐量约为 12 G/s 量级。以上不是跨平台承诺，且 role-based write-heavy 的 CV 约 14.7%，应谨慎引用。

### 启动方式错误的复现

第一次从项目根目录直接运行时，`lookup_bench` 默认执行以及以下四个过滤项均以退出码 139（段错误）结束：

```text
BM_FindIndex_Only
BM_Resolve_Linear
BM_Resolve_Linear_80_20
BM_Resolve_Linear_Random
```

ASAN 报告崩溃在 `bench/lookup_bench.cpp:26` 的 `g_image->index`，因为 `dll_object::load()` 失败后 `g_image` 为空。设置 `LD_LIBRARY_PATH=.` 后上述测试全部通过：`BM_FindIndex_Only` 约 33.6 ns，`BM_Resolve_Linear` 约 232 ns，`BM_Resolve_WithEBR` 约 34.3 ns。问题是启动环境和 benchmark 未检查 load 返回值，不是 lookup 算法本身的段错误。

## 9. 参考

- [ABIX README](../README_ZH.md)
- [Google Benchmark User Guide](https://github.com/google/benchmark)
- [User-Level Implementations of Read-Copy Update](https://doi.org/10.1109/TPDS.2011.159)
