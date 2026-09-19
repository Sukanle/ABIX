# ABIX 指标体系：定量评估框架

<p align="center">
  English · <a href="metrics_zh.md">中文</a>
</p>

<details>

<summary>目录</summary>

- [原则](#原则)
- [测量方法](#测量方法)
- [指标分层](#指标分层)
- [AI 效率](#ai-效率)
  - [TROI](#troi)
  - [Token 缩减率 (TRR)](#token-缩减率-trr)
- [ABI 可靠性](#abi-可靠性)
  - [ABI 覆盖率](#abi-覆盖率)
  - [Breaking Change 检出率](#breaking-change-检出率)
  - [不安全接受率](#不安全接受率)
  - [ABI 逃逸率](#abi-逃逸率)
  - [ABI 检出延迟](#abi-检出延迟)
  - [影响召回率与精确率](#影响召回率与精确率)
- [工具链效率](#工具链效率)
  - [检查加速比](#检查加速比)
  - [集成工作量缩减](#集成工作量缩减)
  - [二进制复用率](#二进制复用率)
  - [迁移自动化率](#迁移自动化率)
  - [元数据开销](#元数据开销)
- [Agent 自动化](#agent-自动化)
  - [验证任务成功率](#验证任务成功率)
  - [人工干预率](#人工干预率)
- [商业评估](#商业评估)
  - [年度成本避免](#年度成本避免)
  - [ROI](#roi)
  - [回收期](#回收期)
  - [ABI 风险降低](#abi-风险降低)
- [ABIX 性能画像](#abix-性能画像)
- [与其他文档的关系](#与其他文档的关系)

</details>

> 本文档定义 ABIX 官方量化指标、公式和报告方法。这是**定义**文档；
> 实际测量数据见 [`benchmark.md`](../benchmark/benchmark_zh.md) 和 [`troi.md`](troi_zh.md)。

---

## 原则

ABIX 指标遵循三条规则：

1. **不制造综合分数。** 每个指标都是独立的、可复现的数字。ABIX 不发布加权的"ABIX 评分"，因为不同指标衡量不同维度，不能有意义地相加。

2. **Before/After，而非绝对值。** 每个指标对比基线（不使用 ABIX）与启用 ABIX 的工作流。脱离上下文的绝对数字没有意义。

3. **可复现协议。** 每个报告的数字必须注明：硬件、编译器、项目、数据集、任务定义、样本量和统计方法（均值、中位数、P95）。

---

## 测量方法

所有 ABIX 基准测试必须报告：

| 字段 | 描述 |
|------|------|
| 硬件 | CPU 型号、核心数、内存、存储类型 |
| OS / 内核 | 发行版和内核版本 |
| 编译器 | 工具链和版本（如 Clang 22.0） |
| ABIX 版本 | Git commit 或 release tag |
| 项目 / 数据集 | 测试产物的来源 |
| 任务定义 | 执行的精确步骤 |
| 样本量 | 每次测量的运行次数 |
| 统计方法 | 均值、中位数、P95、标准差 |
| 日期 | 测量执行时间 |

**不要只报告均值。** 至少报告中位数和 P95。示例：

```text
ABI 检查延迟

基线
  中位数: 420 ms
  P95:    610 ms

ABIX
  中位数: 31 ms
  P95:    44 ms

加速比
  中位数: 13.5×
  P95:    13.9×
```

---

## 指标分层

并非所有指标都在同一项目成熟度下可用。

### 第一层：当前可测量（ABIX 1.0+）

| 指标 | 类别 |
|------|------|
| TROI | AI |
| Token 缩减率 | AI |
| ABI 覆盖率 | ABI |
| Breaking Change 检出率 | ABI |
| 不安全接受率 | ABI |
| 检查加速比 | 工具链 |
| 元数据开销 | 工具链 |
| ABI 检出延迟 | ABI |

### 第二层：真实采用后可测量

| 指标 | 类别 |
|------|------|
| 集成工作量缩减 | 工具链 |
| 迁移自动化率 | 工具链 |
| 二进制复用率 | 工具链 |
| ABI 逃逸率 | ABI |
| 影响召回率 / 精确率 | ABI |
| 验证任务成功率 | Agent |
| 人工干预率 | Agent |

### 第三层：企业 PoC 时测量

| 指标 | 类别 |
|------|------|
| 年度成本避免 | 商业 |
| ROI | 商业 |
| 回收期 | 商业 |
| ABI 风险降低 | 商业 |

第三层指标必须由采用组织使用自己的历史数据计算。ABIX 提供公式和测量方法，不提供绝对数字。

---

## AI 效率

### TROI

**定义：** Token 投资回报率 — 通过 AMC/MCP 获取结构化 ABI 知识后，AI Agent 每 token 完成的工作量，相对于传统工作流。

完整定义、公式和测量方法见 [`troi.md`](troi_zh.md)。

### Token 缩减率 (TRR)

TRR 衡量原始 token 节省，与任务价值无关。

$$
\text{TRR} = 1 - \frac{T_{\text{ABIX}}}{T_{\text{baseline}}}
$$

| 符号 | 含义 |
|------|------|
| $T_{\text{baseline}}$ | 不使用 ABIX 时消耗的 token |
| $T_{\text{ABIX}}$ | 使用 ABIX 时消耗的 token |

**示例：**

```text
基线 = 100k tokens
ABIX = 20k tokens
TRR  = 80%
```

**与 TROI 的关系：** TRR 回答"少了多少 token"；TROI 回答"这些节省带来了多少额外工作"。两者都应报告。

---

## ABI 可靠性

### ABI 覆盖率

**定义：** ABIX 成功描述的公共 ABI 实体比例。

$$
C_{\text{ABI}} = \frac{N_{\text{covered}}}{N_{\text{public}}} \times 100\%
$$

| 符号 | 含义 |
|------|------|
| $N_{\text{public}}$ | 导出的 ABI 实体（类型、函数、变量） |
| $N_{\text{covered}}$ | 拥有有效 ABIX 元数据的实体 |

**示例：**

```text
5000 个公共 ABI 实体
4800 个已覆盖
C_ABI = 96%
```

**工具：** `amc dump`、`amc inspect`、`amc metadata --from-elf`、编译器前端。

### Breaking Change 检出率

**定义：** ABIX 正确识别的 ABI breaking change 比例。

$$
\text{BDR} = \frac{N_{\text{detected}}}{N_{\text{actual breaking}}}
$$

| 符号 | 含义 |
|------|------|
| $N_{\text{actual breaking}}$ | 测试集中的已知 breaking change |
| $N_{\text{detected}}$ | ABIX 正确标记的 breaking change |

**示例：**

```text
100 个实际 breaking change
97 个被检出
BDR = 97%
```

**工具：** `amc diff`、`amc verify`。

### 不安全接受率

**定义：** ABIX 错误接受为兼容的不兼容 ABI change 比例。这是最关键的安全指标。

$$
\text{UAR} = \frac{N_{\text{incompatible accepted}}}{N_{\text{actual incompatible}}} \times 100\%
$$

| 符号 | 含义 |
|------|------|
| $N_{\text{actual incompatible}}$ | 已知不兼容 change |
| $N_{\text{incompatible accepted}}$ | ABIX 未能拒绝的不兼容 change |

**目标：** $\text{UAR} \rightarrow 0$

**示例：**

```text
1000 个不兼容 case
0 个不安全接受
UAR = 0.0%
```

**为什么这比准确率更重要：** ABIX 的最大风险不是假阳性（安全 change 被标记为 breaking），而是**假阴性**（breaking change 被接受为安全）。企业客户真正关心的是："你有没有可能告诉我这个 DLL 安全，但实际上会崩？"

### ABI 逃逸率

**定义：** 进入生产环境后才被发现的 ABI 问题比例。

$$
\text{AER} = \frac{N_{\text{post-release}}}{N_{\text{all incidents}}} \times 100\%
$$

| 符号 | 含义 |
|------|------|
| $N_{\text{all incidents}}$ | 发现的 ABI 问题总数（任何阶段） |
| $N_{\text{post-release}}$ | 在生产环境中发现的 ABI 问题 |

**示例：**

```text
引入 ABIX 前：
  30 个 ABI 问题，3 个进入生产
  AER = 10%

引入 ABIX 后：
  30 个 ABI 问题，1 个进入生产
  AER = 3.3%

降低：67%
```

该指标与 DORA 的 change fail rate 和 deployment rework rate 对齐。

### ABI 检出延迟

**定义：** ABI change 引入到 ABIX 检出之间的时间。

$$
\text{DL}_{\text{ABI}} = t_{\text{detected}} - t_{\text{introduced}}
$$

**加速因子：**

$$
\text{Speedup}_{\text{detection}} = \frac{\text{DL}_{\text{baseline}}}{\text{DL}_{\text{ABIIX}}}
$$

**示例：**

```text
开发者 commit:     10:00
ABIX CI 检出:      10:02
DL_ABI = 2 min

传统流程：
  commit → build → release → 用户反馈 = 3 天
  DL_baseline = 4320 min

加速比 = 2160×
```

### 影响召回率与精确率

**定义：** ABIX 在检测到 breaking change 时，影响分析的准确性。

**影响召回率 (IR)：**

$$
\text{IR} = \frac{N_{\text{correctly identified affected}}}{N_{\text{actually affected}}}
$$

**影响精确率 (IP)：**

$$
\text{IP} = \frac{N_{\text{correctly identified affected}}}{N_{\text{reported affected}}}
$$

**示例：**

```text
Widget struct 的 breaking change
实际受影响：Plugin A、Plugin B、Rust binding、SDK C（4 个组件）
ABIX 报告：Plugin A、Plugin B、Rust binding（3 个组件）
全部正确

IR = 3/4 = 75%
IP = 3/3 = 100%
```

这些指标适用于未来的企业级 ABI 影响分析基准测试。

---

## 工具链效率

### 检查加速比

**定义：** 基线检查时间与 ABIX 检查时间的比值。

$$
\text{IS} = \frac{T_{\text{baseline}}}{T_{\text{ABIX}}}
$$

**示例：**

```text
ELF/DWARF 检查: 500 ms
ABIX 检查:       30 ms
IS = 16.7×
```

适用于：`amc inspect`、`amc query`、`amc metadata --from-elf`、Metadata Region mmap、`.abix` 解析。

### 集成工作量缩减

**定义：** 跨语言集成的工程小时数缩减。

$$
\text{IER} = 1 - \frac{H_{\text{ABIX}}}{H_{\text{baseline}}}
$$

| 符号 | 含义 |
|------|------|
| $H_{\text{baseline}}$ | 不使用 ABIX 的小时数 |
| $H_{\text{ABIX}}$ | 使用 ABIX 的小时数 |

**示例：**

```text
C++ → Rust binding：
  基线: 10 小时
  ABIX:  3 小时
  IER = 70%
```

### 二进制复用率

**定义：** 可复用现有二进制而非重新编译的构建比例。

$$
\text{BRR} = \frac{N_{\text{reused}}}{N_{\text{eligible builds}}} \times 100\%
$$

**示例：**

```text
1000 次构建
750 次复用现有二进制
BRR = 75%
```

适用于 ABIX 包管理器 / 二进制注册表场景。

### 迁移自动化率

**定义：** 可通过 adapter 生成自动迁移的 ABI breaking change 比例。

$$
\text{MAR} = \frac{N_{\text{automatically migrated}}}{N_{\text{migratable}}} \times 100\%
$$

**示例：**

```text
100 个 breaking change
70 个通过 adapter 自动迁移
MAR = 70%
```

适用于：`amc generate`、`amc adapter`、ABI 迁移工作流。

### 元数据开销

**每类型开销：**

$$
O_{\text{type}} = \frac{S_{\text{ABIX}}}{N_{\text{types}}}
$$

**二进制开销比：**

$$
O_{\text{binary}} = \frac{S_{\text{ABIX}}}{S_{\text{binary}}} \times 100\%
$$

**示例：**

```text
61 个类型，3.4 KB 元数据
O_type = 56 B/type

1.2 MB 二进制，14 KB ABIX
O_binary = 1.17%
```

Debug/Release/RelWithDebInfo 开销详情见 [`metadata_modes_zh.md`](../abix/metadata_modes_zh.md)。

---

## Agent 自动化

### 验证任务成功率

**定义：** 完成并通过 ABI/构建/测试验证的任务比例。

$$
\text{VTSR} = \frac{N_{\text{verified successful}}}{N_{\text{tasks}}} \times 100\%
$$

**关键约束：** "Agent 说完成了"不算成功。成功必须满足：

```text
ABIX 验证    ✓
构建通过     ✓
测试通过     ✓
```

**示例：**

```text
100 个 ABI 迁移任务
83 个完成并通过所有验证
VTSR = 83%
```

### 人工干预率

**定义：** 需要人工干预的任务比例。

$$
\text{HIR} = \frac{N_{\text{tasks requiring intervention}}}{N_{\text{tasks}}} \times 100\%
$$

**自动化率：**

$$
\text{Automation Rate} = 1 - \text{HIR}
$$

**示例：**

```text
100 个任务
35 个需要人工干预
HIR = 35%
自动化率 = 65%
```

---

## 商业评估

所有第三层指标必须由采用组织使用自己的数据计算。ABIX 仅提供公式。

### 年度成本避免

$$
B_{\text{annual}} = B_{\text{labor}} + B_{\text{build}} + B_{\text{incident}} + B_{\text{infra}}
$$

| 组件 | 公式 |
|------|------|
| 人力节约 | $\Delta H \times C_{\text{hour}}$ |
| 构建节约 | $\Delta T_{\text{build}} \times C_{\text{compute/time}}$ |
| 事件避免 | $N_{\text{avoided}} \times C_{\text{incident}}$ |
| 基础设施节约 | CI、产物存储、注册表、开发机 |

### ROI

$$
\text{ROI} = \frac{B_{\text{annual}} - C_{\text{annual}}}{C_{\text{annual}}} \times 100\%
$$

$C_{\text{annual}}$ 包括：采用、工程集成、培训、基础设施、维护、企业支持。

### 回收期

$$
\text{PBP} = \frac{C_{\text{initial}}}{B_{\text{monthly}} - C_{\text{monthly}}}
$$

**示例：**

```text
实施成本: $50k
每月净节约: $10k
PBP = 5 个月
```

### ABI 风险降低

$$
\text{ARR} = 1 - \frac{R_{\text{ABIX}}}{R_{\text{baseline}}}
$$

其中 $R$ 是 ABI 事件率：

$$
R = \frac{N_{\text{ABI incidents}}}{N_{\text{releases}}}
$$

**示例：**

```text
基线: 20 事件 / 1000 次发布
ABIX:  3 事件 / 1000 次发布
ARR = 85%
```

---

## ABIX 性能画像

ABIX 不发布综合分数，而是发布**性能画像** — 一组客观数字共同描述系统价值：

```text
ABIX 性能画像
──────────────────────────────────

元数据
    每类型开销           56 B/type
    二进制开销           1.17%

检查
    加速比               14.2× faster

ABI 可靠性
    覆盖率              97.8%
    breaking 检出率      99.1%
    不安全接受率          0.0%

工具链
    集成工作量           -64%
    迁移自动化率          71%
    二进制复用率          76%

Agent
    验证任务成功率        91%
    人工干预率            35%

AI 效率
    Token 缩减率         82%
    TROI                 5.6×
```

所有数字来自可复现的基准测试，硬件和数据集均有明确文档。

---

## 与其他文档的关系

| 文档 | 范围 |
|------|------|
| [`metrics.md`](metrics.md) | **本文档。** 定义、公式、方法。 |
| [`benchmark.md`](../benchmark/benchmark_zh.md) | 特定硬件上的实际测量数据。 |
| [`troi.md`](troi_zh.md) | AI 工作流 TROI / token 效率深度分析。 |
| [`metadata_modes_zh.md`](../abix/metadata_modes_zh.md) | 每类型开销详情（Debug / Release / RelWithDebInfo）。 |
