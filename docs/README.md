# ABIX Documentation

<p align="center">
  <a href="README.md">English</a> · <a href="README_zh.md">中文</a>
</p>

> The [project README](README.md) is the entry point. This file navigates the `docs/` tree.

> **Platform support:** Linux, macOS and Windows are supported; see [`../CONTRIBUTING.md`](../CONTRIBUTING.md#windows-msys2-ucrt64) for the Windows (MSYS2 UCRT64) setup.

```mermaid
graph TD
    R[Documentation] --> GS[Getting Started]
    R --> AX[ABIX Core]
    R --> AM[AMC Toolchain]
    R --> L[Languages]
    R --> AR[Architecture]
    R --> AI[AI &amp; Agent]
    R --> DV[Development]
    R --> BM[Benchmark]
    R --> EC[Ecosystem]
```

## Choose your path

| I want to … | Go to |
|-------------|-------|
| **install and try ABIX** | [Getting Started](getting-started/) |
| **understand the ABI model** | [ABIX](abix/) |
| **use the AMC toolchain** | [AMC](amc/) |
| **work across languages** | [Languages](lang/) |
| **learn internal architecture** | [Architecture](architecture/) |
| **integrate AI / Agent workflows** | [AI](ai/) |
| **see benchmarks and metrics** | [Benchmark](benchmark/) |
| **contribute or understand design** | [Development](development/) |
| **adopt ABIX in a project** | [Ecosystem](ecosystem/) |

---

## Authoritative documents

These live at the repository root and are the sources of truth.

| Document | Answers |
|----------|---------|
| [ABI-SPEC.md](../.agents/ABI-SPEC.md) | what a legal ABIX ABI is |
| [ARCHITECTURE.md](../.agents/ARCHITECTURE.md) | how the system is shaped, and invariants |
| [LANGUAGE-PLUGIN.md](../.agents/LANGUAGE-PLUGIN.md) | how to add a language |
| [AGENTS.md](../.agents/AGENTS.md) | how an AI agent should work here |
| [ROADMAP.md](../.agents/ROADMAP.md) | what is in scope now |
| [CONTRIBUTING.md](../CONTRIBUTING.md) | contribution workflow |

---

## Getting Started

Start here if you are new to ABIX.

| Document | 中文 | Description |
|----------|------|-------------|
| [getting-started.md](getting-started/getting-started.md) | [getting-started_zh.md](getting-started/getting-started_zh.md) | build + first walkthrough |
| [bootstrap.md](getting-started/bootstrap.md) | [bootstrap_zh.md](getting-started/bootstrap_zh.md) | bootstrap model and milestones |
| [self-hosting.md](getting-started/self-hosting.md) | [self-hosting_zh.md](getting-started/self-hosting_zh.md) | bootstrap ABI and the self-description loop |

## ABIX

Core ABI model, format, and API documentation.

| Document | 中文 | Description |
|----------|------|-------------|
| [abix.md](abix/abix.md) | [abix_zh.md](abix/abix_zh.md) | the canonical `.abix` artifact format |
| [abic.md](abix/abic.md) | [abic_zh.md](abix/abic_zh.md) | `.abic.toml` configuration reference |
| [api.md](abix/api.md) | [api_zh.md](abix/api_zh.md) | full C++ API reference |
| [interface.md](abix/interface.md) | [interface_zh.md](abix/interface_zh.md) | the Interface layer: contracts, proxies, discovery, Owner ABI |
| [std.md](abix/std.md) | [std_zh.md](abix/std_zh.md) | ABIX-STD cross-language ABI primitives |
| [metadata_modes.md](abix/metadata_modes.md) | [metadata_modes_zh.md](abix/metadata_modes_zh.md) | three metadata modes: Debug / Release / RelWithDebInfo |

## Languages

Cross-language projections and per-language guides.

| Document | 中文 | Description |
|----------|------|-------------|
| [README.md](lang/README.md) | [README_zh.md](lang/README_zh.md) | why ABIX differs from bind-style FFI; the projection model |
| [cpp.md](lang/cpp.md) | [cpp_zh.md](lang/cpp_zh.md) | C++ frontend/backend, generated layout checks, Interface layer |
| [rust.md](lang/rust.md) | [rust_zh.md](lang/rust_zh.md) | Rust provider, `repr(C)` projection, ownership/`Result` wrappers |
| [zig.md](lang/zig.md) | [zig_zh.md](lang/zig_zh.md) | Zig backend-only projection, `extern` / `comptime` checks |

## AMC

Toolchain documentation.

| Document | 中文 | Description |
|----------|------|-------------|
| [amc.md](amc/amc.md) | [amc_zh.md](amc/amc_zh.md) | the AMC toolchain and CLI |

## Architecture

How ABIX and AMC work internally.

| Document | 中文 | Description |
|----------|------|-------------|
| [architecture.md](architecture/architecture.md) | [architecture_zh.md](architecture/architecture_zh.md) | system overview, design map, invariants |
| [runtime.md](architecture/runtime.md) | [runtime_zh.md](architecture/runtime_zh.md) | runtime responsibilities, registry, execution model |
| [compatibility.md](architecture/compatibility.md) | [compatibility_zh.md](architecture/compatibility_zh.md) | TypeID / LayoutHash / BuildID / MetadataID |
| [elf-inspection.md](architecture/elf-inspection.md) | [elf-inspection_zh.md](architecture/elf-inspection_zh.md) | inspecting ABIX metadata sections in ELF / Mach-O binaries |
| [pe-inspection.md](architecture/pe-inspection.md) | [pe-inspection_zh.md](architecture/pe-inspection_zh.md) | designing the ABIX private header for Windows PE images |
| [binary-image.md](architecture/binary-image.md) | [binary-image_zh.md](architecture/binary-image_zh.md) | the BinaryImage capability layer over ELF / Mach-O / PE |

## AI

AI and Agent integration.

| Document | 中文 | Description |
|----------|------|-------------|
| [ai.md](ai/ai.md) | [ai_zh.md](ai/ai_zh.md) | ABIX in AI Vibe Coding — theoretical foundation |
| [MCP.md](ai/MCP.md) | [MCP_zh.md](ai/MCP_zh.md) | MCP server and the ABIX tool catalogue |
| [troi.md](ai/troi.md) | [troi_zh.md](ai/troi_zh.md) | TROI token-efficiency metric for AMC/MCP agent workflows |
| [metrics.md](ai/metrics.md) | [metrics_zh.md](ai/metrics_zh.md) | quantitative evaluation framework and metric definitions |

## Benchmark

Quantitative evaluation.

| Document | 中文 | Description |
|----------|------|-------------|
| [benchmark.md](benchmark/benchmark.md) | [benchmark_zh.md](benchmark/benchmark_zh.md) | performance measurements |

## Development

Project design, comparison, and roadmap.

| Document | 中文 | Description |
|----------|------|-------------|
| [design-notes.md](development/design-notes.md) | [design-notes_zh.md](development/design-notes_zh.md) | design principles and project history |
| [logging.md](development/logging.md) | [logging_zh.md](development/logging_zh.md) | diagnostic callback contract, error/log boundary, and cross-DLL ownership |
| [type-model.md](development/type-model.md) | [type-model_zh.md](development/type-model_zh.md) | ABI facts vs source-language types, and the per-target generation model |
| [competitors.md](development/competitors.md) | [competitors_zh.md](development/competitors_zh.md) | competitive landscape analysis |
| [roadmap.md](development/roadmap.md) | [roadmap_zh.md](development/roadmap_zh.md) | scope and sequencing |
| [testing.md](development/testing.md) | [testing_zh.md](development/testing_zh.md) | the complete test flow, per-tier purpose and expected results |

## Ecosystem

Adoption, applications, and sustainability.

| Document | 中文 | Description |
|----------|------|-------------|
| [ecosystem.md](ecosystem/ecosystem.md) | [ecosystem_zh.md](ecosystem/ecosystem_zh.md) | ecosystem, adoption, and application directions |
| [sustainability.md](ecosystem/sustainability.md) | [sustainability_zh.md](ecosystem/sustainability_zh.md) | project sustainability and community funding |
