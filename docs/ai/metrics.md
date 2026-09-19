# ABIX Metrics: Quantitative Evaluation Framework

<p align="center">
  <a href="metrics_zh.md">中文</a> · English
</p>

<details>

<summary>Contents</summary>

- [Principles](#principles)
- [Measurement Methodology](#measurement-methodology)
- [Metric Tiers](#metric-tiers)
- [AI Efficiency](#ai-efficiency)
  - [TROI](#troi)
  - [Token Reduction Ratio (TRR)](#token-reduction-ratio-trr)
- [ABI Reliability](#abi-reliability)
  - [ABI Coverage](#abi-coverage)
  - [Breaking Change Detection Rate](#breaking-change-detection-rate)
  - [Unsafe Acceptance Rate](#unsafe-acceptance-rate)
  - [ABI Escape Rate](#abi-escape-rate)
  - [ABI Detection Latency](#abi-detection-latency)
  - [Impact Recall and Precision](#impact-recall-and-precision)
- [Toolchain Efficiency](#toolchain-efficiency)
  - [Inspection Speedup](#inspection-speedup)
  - [Integration Effort Reduction](#integration-effort-reduction)
  - [Binary Reuse Rate](#binary-reuse-rate)
  - [Migration Automation Rate](#migration-automation-rate)
  - [Metadata Overhead](#metadata-overhead)
- [Agent Automation](#agent-automation)
  - [Verified Task Success Rate](#verified-task-success-rate)
  - [Human Intervention Rate](#human-intervention-rate)
- [Business Evaluation](#business-evaluation)
  - [Annual Cost Avoidance](#annual-cost-avoidance)
  - [ROI](#roi)
  - [Payback Period](#payback-period)
  - [ABI Risk Reduction](#abi-risk-reduction)
- [ABIX Performance Profile](#abix-performance-profile)
- [Relation to Other Documents](#relation-to-other-documents)

</details>

> This document defines the official ABIX quantitative metrics, their formulas,
> and how to report them. It is the **definition** document; actual measurements
> live in [`benchmark.md`](../benchmark/benchmark.md) and [`troi.md`](troi.md).

---

## Principles

ABIX metrics follow three rules:

1. **No composite score.** Each metric is a standalone, reproducible number.
   ABIX does not publish a weighted "ABIX Score" because different metrics
   measure different dimensions and cannot be meaningfully added.

2. **Before/After, not absolute.** Every metric compares a baseline (without
   ABIX) against an ABIX-enabled workflow. Absolute numbers without context are
   not meaningful.

3. **Reproducible protocol.** Every reported number must specify: hardware,
   compiler, project, dataset, task definition, sample size, and statistical
   method (mean, median, P95).

---

## Measurement Methodology

All ABIX benchmarks must report:

| Field | Description |
|-------|-------------|
| Hardware | CPU model, core count, RAM, storage type |
| OS / Kernel | Distribution and kernel version |
| Compiler | Toolchain and version (e.g. Clang 22.0) |
| ABIX version | Git commit or release tag |
| Project / Dataset | Source of the test artifacts |
| Task definition | Exact steps performed |
| Sample size | Number of runs per measurement |
| Statistical method | Mean, median, P95, standard deviation |
| Date | When the measurement was taken |

**Do not report only mean.** Report at minimum median and P95. Example:

```text
ABI inspection latency

Baseline
  median: 420 ms
  p95:    610 ms

ABIX
  median: 31 ms
  p95:    44 ms

Speedup
  median: 13.5×
  p95:    13.9×
```

---

## Metric Tiers

Not all metrics are available at the same project maturity.

### Tier 1: Measurable now (ABIX 1.0+)

| Metric | Category |
|--------|----------|
| TROI | AI |
| Token Reduction Ratio | AI |
| ABI Coverage | ABI |
| Breaking Change Detection Rate | ABI |
| Unsafe Acceptance Rate | ABI |
| Inspection Speedup | Toolchain |
| Metadata Overhead | Toolchain |
| ABI Detection Latency | ABI |

### Tier 2: Measurable after real-world adoption

| Metric | Category |
|--------|----------|
| Integration Effort Reduction | Toolchain |
| Migration Automation Rate | Toolchain |
| Binary Reuse Rate | Toolchain |
| ABI Escape Rate | ABI |
| Impact Recall / Precision | ABI |
| Verified Task Success Rate | Agent |
| Human Intervention Rate | Agent |

### Tier 3: Measurable during enterprise PoC

| Metric | Category |
|--------|----------|
| Annual Cost Avoidance | Business |
| ROI | Business |
| Payback Period | Business |
| ABI Risk Reduction | Business |

Tier 3 metrics must be computed by the adopting organization using their own
historical data. ABIX provides the formulas and measurement methodology, not
the absolute numbers.

---

## AI Efficiency

### TROI

**Definition:** Token Return on Investment — AI-agent work accomplished per
token when structured ABI knowledge is available through AMC/MCP, relative to a
traditional flow.

See [`troi.md`](troi.md) for the full definition, formula, and measurement
method.

### Token Reduction Ratio (TRR)

TRR measures raw token savings independent of task value.

$$
\text{TRR} = 1 - \frac{T_{\text{ABIX}}}{T_{\text{baseline}}}
$$

| Symbol | Meaning |
|--------|---------|
| $T_{\text{baseline}}$ | Tokens consumed without ABIX |
| $T_{\text{ABIX}}$ | Tokens consumed with ABIX |

**Example:**

```text
baseline = 100k tokens
ABIX     = 20k tokens
TRR      = 80%
```

**Relation to TROI:** TRR answers "how many fewer tokens"; TROI answers "how
much more work those savings enable." Both should be reported.

---

## ABI Reliability

### ABI Coverage

**Definition:** The proportion of public ABI entities that ABIX successfully
describes.

$$
C_{\text{ABI}} = \frac{N_{\text{covered}}}{N_{\text{public}}} \times 100\%
$$

| Symbol | Meaning |
|--------|---------|
| $N_{\text{public}}$ | Exported ABI entities (types, functions, variables) |
| $N_{\text{covered}}$ | Entities with valid ABIX metadata |

**Example:**

```text
5000 public ABI entities
4800 covered
C_ABI = 96%
```

**Tools:** `amc dump`, `amc inspect`, `amc metadata --from-elf`, compiler
frontend.

### Breaking Change Detection Rate

**Definition:** The proportion of actual ABI breaking changes that ABIX
correctly identifies.

$$
\text{BDR} = \frac{N_{\text{detected}}}{N_{\text{actual breaking}}}
$$

| Symbol | Meaning |
|--------|---------|
| $N_{\text{actual breaking}}$ | Known breaking changes in the test set |
| $N_{\text{detected}}$ | Breaking changes correctly flagged by ABIX |

**Example:**

```text
100 actual breaking changes
97 detected
BDR = 97%
```

**Tools:** `amc diff`, `amc verify`.

### Unsafe Acceptance Rate

**Definition:** The proportion of incompatible ABI changes that ABIX
incorrectly accepts as compatible. This is the most critical safety metric.

$$
\text{UAR} = \frac{N_{\text{incompatible accepted}}}{N_{\text{actual incompatible}}} \times 100\%
$$

| Symbol | Meaning |
|--------|---------|
| $N_{\text{actual incompatible}}$ | Known incompatible changes |
| $N_{\text{incompatible accepted}}$ | Incompatible changes ABIX failed to reject |

**Target:** $\text{UAR} \rightarrow 0$

**Example:**

```text
1000 incompatible cases
0 unsafe acceptances
UAR = 0.0%
```

**Why this matters more than accuracy:** ABIX's primary risk is not false
positives (safe changes flagged as breaking) but **false negatives** (breaking
changes accepted as safe). Enterprise customers care about: "Will you tell me
this DLL is safe when it will actually crash?"

### ABI Escape Rate

**Definition:** The proportion of ABI incidents discovered after reaching
production.

$$
\text{AER} = \frac{N_{\text{post-release}}}{N_{\text{all incidents}}} \times 100\%
$$

| Symbol | Meaning |
|--------|---------|
| $N_{\text{all incidents}}$ | Total ABI issues discovered (any stage) |
| $N_{\text{post-release}}$ | ABI issues found in production |

**Example:**

```text
Before ABIX:
  30 ABI issues total, 3 reached production
  AER = 10%

After ABIX:
  30 ABI issues total, 1 reached production
  AER = 3.3%

Reduction: 67%
```

This metric aligns with DORA's change fail rate and deployment rework rate
indicators for software delivery stability.

### ABI Detection Latency

**Definition:** Time between when an ABI change is introduced and when ABIX
detects it.

$$
\text{DL}_{\text{ABI}} = t_{\text{detected}} - t_{\text{introduced}}
$$

**Speedup factor:**

$$
\text{Speedup}_{\text{detection}} = \frac{\text{DL}_{\text{baseline}}}{\text{DL}_{\text{ABIIX}}}
$$

**Example:**

```text
Developer commit:     10:00
ABIX CI detection:    10:02
DL_ABI = 2 min

Traditional flow:
  commit → build → release → user feedback = 3 days
  DL_baseline = 4320 min

Speedup = 2160×
```

### Impact Recall and Precision

**Definition:** Accuracy of ABIX's impact analysis when a breaking change is
detected.

**Impact Recall (IR):**

$$
\text{IR} = \frac{N_{\text{correctly identified affected}}}{N_{\text{actually affected}}}
$$

**Impact Precision (IP):**

$$
\text{IP} = \frac{N_{\text{correctly identified affected}}}{N_{\text{reported affected}}}
$$

**Example:**

```text
Breaking change in Widget struct
Actually affected: Plugin A, Plugin B, Rust binding, SDK C (4 components)
ABIX reported: Plugin A, Plugin B, Rust binding (3 components)
All 3 correct

IR = 3/4 = 75%
IP = 3/3 = 100%
```

These metrics are intended for future enterprise-grade ABI Impact Analysis
benchmarks.

---

## Toolchain Efficiency

### Inspection Speedup

**Definition:** Ratio of baseline inspection time to ABIX inspection time.

$$
\text{IS} = \frac{T_{\text{baseline}}}{T_{\text{ABIX}}}
$$

**Example:**

```text
ELF/DWARF inspection:  500 ms
ABIX inspection:        30 ms
IS = 16.7×
```

Applies to: `amc inspect`, `amc query`, `amc metadata --from-elf`, Metadata
Region mmap, `.abix` parsing.

### Integration Effort Reduction

**Definition:** Reduction in engineering hours for cross-language integration.

$$
\text{IER} = 1 - \frac{H_{\text{ABIX}}}{H_{\text{baseline}}}
$$

| Symbol | Meaning |
|--------|---------|
| $H_{\text{baseline}}$ | Hours without ABIX |
| $H_{\text{ABIX}}$ | Hours with ABIX |

**Example:**

```text
C++ → Rust binding:
  baseline: 10 hours
  ABIX:      3 hours
  IER = 70%
```

### Binary Reuse Rate

**Definition:** Proportion of builds that can reuse an existing binary instead
of recompiling.

$$
\text{BRR} = \frac{N_{\text{reused}}}{N_{\text{eligible builds}}} \times 100\%
$$

**Example:**

```text
1000 builds
750 reused existing binary
BRR = 75%
```

Relevant for ABIX Package Manager / Binary Registry scenarios.

### Migration Automation Rate

**Definition:** Proportion of ABI breaking changes that can be automatically
migrated via adapter generation.

$$
\text{MAR} = \frac{N_{\text{automatically migrated}}}{N_{\text{migratable}}} \times 100\%
$$

**Example:**

```text
100 breaking changes
70 auto-migrated via adapter
MAR = 70%
```

Applies to: `amc generate`, `amc adapter`, ABI migration workflows.

### Metadata Overhead

**Per-type overhead:**

$$
O_{\text{type}} = \frac{S_{\text{ABIX}}}{N_{\text{types}}}
$$

**Binary overhead ratio:**

$$
O_{\text{binary}} = \frac{S_{\text{ABIX}}}{S_{\text{binary}}} \times 100\%
$$

**Example:**

```text
61 types, 3.4 KB metadata
O_type = 56 B/type

1.2 MB binary, 14 KB ABIX
O_binary = 1.17%
```

See [`metadata_modes.md`](../abix/metadata_modes.md) for Debug/Release/RelWithDebInfo
overhead details.

---

## Agent Automation

### Verified Task Success Rate

**Definition:** Proportion of tasks completed AND verified through
ABI/build/test checks.

$$
\text{VTSR} = \frac{N_{\text{verified successful}}}{N_{\text{tasks}}} \times 100\%
$$

**Critical constraint:** "Agent says done" does NOT count as success. Success
requires:

```text
ABIX verification  ✓
Build passes       ✓
Tests pass         ✓
```

**Example:**

```text
100 ABI migration tasks
83 completed and passed all verification
VTSR = 83%
```

### Human Intervention Rate

**Definition:** Proportion of tasks requiring human intervention.

$$
\text{HIR} = \frac{N_{\text{tasks requiring intervention}}}{N_{\text{tasks}}} \times 100\%
$$

**Automation Rate:**

$$
\text{Automation Rate} = 1 - \text{HIR}
$$

**Example:**

```text
100 tasks
35 required human intervention
HIR = 35%
Automation Rate = 65%
```

---

## Business Evaluation

All Tier 3 metrics must be computed by the adopting organization using their
own data. ABIX provides the formulas only.

### Annual Cost Avoidance

$$
B_{\text{annual}} = B_{\text{labor}} + B_{\text{build}} + B_{\text{incident}} + B_{\text{infra}}
$$

| Component | Formula |
|-----------|---------|
| Labor savings | $\Delta H \times C_{\text{hour}}$ |
| Build savings | $\Delta T_{\text{build}} \times C_{\text{compute/time}}$ |
| Incident avoidance | $N_{\text{avoided}} \times C_{\text{incident}}$ |
| Infrastructure savings | CI, artifact storage, registry, dev machines |

### ROI

$$
\text{ROI} = \frac{B_{\text{annual}} - C_{\text{annual}}}{C_{\text{annual}}} \times 100\%
$$

$C_{\text{annual}}$ includes: adoption, engineering integration, training,
infrastructure, maintenance, enterprise support.

### Payback Period

$$
\text{PBP} = \frac{C_{\text{initial}}}{B_{\text{monthly}} - C_{\text{monthly}}}
$$

**Example:**

```text
Implementation cost: $50k
Monthly net savings:  $10k
PBP = 5 months
```

### ABI Risk Reduction

$$
\text{ARR} = 1 - \frac{R_{\text{ABIX}}}{R_{\text{baseline}}}
$$

Where $R$ is the ABI incident rate:

$$
R = \frac{N_{\text{ABI incidents}}}{N_{\text{releases}}}
$$

**Example:**

```text
baseline: 20 incidents / 1000 releases
ABIX:      3 incidents / 1000 releases
ARR = 85%
```

---

## ABIX Performance Profile

Instead of a composite score, ABIX publishes a **performance profile** — a set
of客观 numbers that together describe the system's value:

```text
ABIX Performance Profile
──────────────────────────────────

Metadata
    per-type overhead       56 B/type
    binary overhead         1.17%

Inspection
    speedup                 14.2× faster

ABI reliability
    coverage                97.8%
    breaking detection      99.1%
    unsafe acceptance       0.0%

Toolchain
    integration effort      -64%
    migration automation     71%
    binary reuse             76%

Agent
    verified task success    91%
    human intervention       35%

AI efficiency
    token reduction          82%
    TROI                     5.6×
```

All numbers come from reproducible benchmarks on explicitly documented
hardware and datasets.

---

## Relation to Other Documents

| Document | Scope |
|----------|-------|
| [`metrics.md`](metrics.md) | **This document.** Definitions, formulas, methodology. |
| [`benchmark.md`](../benchmark/benchmark.md) | Actual measurements on specific hardware. |
| [`troi.md`](troi.md) | Deep dive into TROI / token-efficiency for AI workflows. |
| [`metadata_modes.md`](../abix/metadata_modes.md) | Per-type overhead details (Debug / Release / RelWithDebInfo). |
