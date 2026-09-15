# ABIX Documentation

This directory holds the specification and design material. The
[README](../README.md) is the project entry point; this index is the map.

Authoritative project-level documents live at the repository root:

| Document | Answers |
|----------|---------|
| [ABI-SPEC.md](../.agents/ABI-SPEC.md) | what a legal ABIX ABI is |
| [ARCHITECTURE.md](../.agents/ARCHITECTURE.md) | how the system is shaped, and invariants |
| [LANGUAGE-PLUGIN.md](../.agents/LANGUAGE-PLUGIN.md) | how to add a language |
| [AGENTS.md](../.agents/AGENTS.md) | how an AI agent should work here |
| [ROADMAP.md](../.agents/ROADMAP.md) | what is in scope now |
| [CONTRIBUTING.md](../CONTRIBUTING.md) | contribution workflow |

## Start here

| Document | Contents |
|----------|----------|
| [getting-started.md](getting-started.md) | build + first walkthrough |
| [abix.md](abix.md) | the canonical `.abix` artifact format |
| [architecture.md](architecture.md) | deeper, implementation-oriented architecture |

## Core concepts

| Document | Contents |
|----------|----------|
| [compatibility.md](compatibility.md) | TypeID / LayoutHash / BuildID / MetadataID |
| [abic.md](abic.md) | `.abic.toml` configuration reference |
| [metadata_modes.md](metadata_modes.md) | full artifact / Metadata Region / runtime descriptor |
| [self-hosting.md](self-hosting.md) | bootstrap ABI and the self-description loop |

## Runtime and API

| Document | Contents |
|----------|----------|
| [runtime.md](runtime.md) | runtime responsibilities, registry, execution model |
| [api.md](api.md) | full C++ API reference |
| [benchmark.md](benchmark.md) | performance measurements |

## Toolchain

| Document | Contents |
|----------|----------|
| [amc.md](amc.md) | the AMC toolchain and CLI |
| [MCP.md](MCP.md) | MCP server and the ABIX tool catalogue |

## Project

| Document | Contents |
|----------|----------|
| [design-notes.md](design-notes.md) | design principles and project history |
| [bootstrap.md](bootstrap.md) | bootstrap model and milestones |

## Chinese editions

Most documents have a `_zh` edition (for example
[abix_zh.md](abix_zh.md), [amc_zh.md](amc_zh.md),
[MCP_zh.md](MCP_zh.md),
[getting-started_zh.md](getting-started_zh.md),
[roadmap_zh.md](roadmap_zh.md)).
