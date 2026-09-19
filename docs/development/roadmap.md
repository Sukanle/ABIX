# Roadmap

<p align="center">
  English · <a href="roadmap_zh.md">中文</a>
</p>

<details>

<summary>Contents</summary>

- [Version Roadmap](#version-roadmap)
- [Direction](#direction)
- [Near-term](#near-term)
- [AI-native ABI toolchain](#ai-native-abi-toolchain)
- [Research / experimental](#research--experimental)
- [Release History](#release-history)
- [Explicitly out of scope](#explicitly-out-of-scope)

</details>

> The authoritative version is [`ROADMAP.md`](../../.agents/ROADMAP.md). This
> file is the English companion to the Chinese edition.

The roadmap prioritises interoperability and real-world usage over adding
runtime features.

## Version Roadmap

ABIX releases are project stages, not feature lists. Each major version marks a
phase, so the version number itself carries the product story.

```text
ABIX 1.0 — Foundation    the ABI foundation exists
ABIX 2.0 — Toolchain     ABIX is usable in real engineering
ABIX 3.0 — Ecosystem     others build on top of ABIX
```

### ABIX 1.0 — Foundation (released)

* ABI specification, metadata format, core runtime.
* Bootstrap and self-hosting: ABIX describes and verifies its own public ABI.
* ABI stability: the external contract stays explicit while internals evolve.

### ABIX 2.0 — Toolchain (current)

* Complete core ABI workflow: inspection, diff, compatibility classification.
* ABI adapter generation and the metadata modes (Debug / Release /
  RelWithDebInfo).
* ELF / Mach-O binary inspection.
* Build-system and CI integration, runtime stability, benchmark baseline.

### ABIX 3.0 — Ecosystem (future)

Target areas, not hard commitments:

* Cross-language bindings (Rust, Zig).
* AI / Agent integration (MCP, TROI).
* LSP, plugin ecosystem, package management, application areas.

### Version independence

The ABIX specification version, the AMC toolchain version, and application
versions are independent dimensions. Toolchain features (Rust, AI, Agent, LSP)
never require a format version change by themselves. See
[`ABI-SPEC.md`](../../.agents/ABI-SPEC.md) §14.1.

## Direction

```mermaid
graph LR
    A[ABIX Core] --> B[AMC]
    B --> C[ABI-aware Toolchain]
    C --> D[Native ABI Ecosystem]
    B --> E[C / C++]
    B --> F[Rust]
    B --> G[Zig]
    B --> H[other language frontends]
```

## Near-term

* **Specification** — strengthen the ABIX IR and `.abix` spec, keeping the model
  language-agnostic.
* **AMC usability** — clearer diagnostics, better error messages, editor/ and
  CI-friendly output.
* **Language / toolchain integration** — expand frontends beyond C++; ABIX IR
  stays the common representation.
* **ABI compatibility analysis** — diff, compatibility classification and
  automatic adapter generation.
* **Documentation and examples** — quick-start material and real case studies.
* **External validation** — adopt ABIX in real projects and feed back results.

## AI-native ABI toolchain

One direction for ABIX is an AI-native ABI toolchain: an explicit ABI model is
the durable, machine-readable interface AI coding agents need.

Current and planned work:

* `amc context --format llm` — compact, token-friendly ABI context.
* Unified structured JSON errors across all tools.
* `amc verify` — contract-vs-implementation consistency checking.
* ABIX Metadata Region — versioned, mappable metadata image.
* `amc query` and the `amc-mcp` server — agent-facing ABI queries.
* Aue (experimental) — Lua boundary layer with L0/L1 diff consistency.

## Research / experimental

* Runtime materialization of the metadata image.
* ABI adaptation / shimming.
* Dynamic-language boundary layers with differential semantic verification.

## Release History

ABIX uses standard SemVer tags from `v1.0.0` onward. The project stage is named
in the release title (for example, `ABIX 2.0.0 — Toolchain`).

* `v1.0.0` — Foundation
* `v2.0.0` — Toolchain

## Explicitly out of scope

ABIX is not a VM, an RPC framework, a universal object model, a debug-info
format or a C-ABI wrapper. See [What ABIX Is Not](../../README.md#what-abix-is-not).
