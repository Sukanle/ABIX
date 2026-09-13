# Roadmap

> **Roadmap items are not implementation requirements unless explicitly marked
> as current work.** Do not implement a future item just because it appears
> here. See [`AGENTS.md`](AGENTS.md) §13 (scope discipline).

## Current — ABIX 1.0

Established and maintained:

* an explicit ABI semantic model and the `.abix` canonical artifact;
* self-hosting: ABIX describes and verifies its own public ABI;
* a stable bootstrap ABI separated from internal implementation;
* the C++ implementation and its Clang-based frontend;
* AMC toolchain: build / inspect / query / diff / verify / generate / metadata;
* ABI metadata: Metadata Region, `.abix.meta`, symbol server, offline scanning;
* ABI verification and structured diagnostics;
* one shared parser across CLI, MCP, LLDB and the runtime (`libabix-*`).

## Next

### P0 — stabilize and validate

* specification stabilization ([`ABI-SPEC.md`](ABI-SPEC.md));
* AMC usability: diagnostics, error messages, editor/CI output;
* ABI diff and compatibility reporting;
* ABI regression tests for real-world break cases;
* documentation and examples;
* external validation and adoption.

### P1 — broaden the toolchain

* language plugin API and a second language prototype
  ([`LANGUAGE-PLUGIN.md`](LANGUAGE-PLUGIN.md));
* build-system / package-manager integration;
* deeper debugger integration (for example LLDB go-to-definition via Source
  Origin).

### P2 — ecosystem

* package ecosystem;
* broader debugger / editor integration;
* AI-agent integration surface (MCP tools, token-cost studies).

## AI-native ABI toolchain

ABIX is also developed as an AI-native ABI toolchain: an explicit ABI model is
exactly the durable, machine-readable interface AI coding agents need. See
[`docs/plan.md`](../docs/plan.md) for the detailed plan.

Delivered so far: `amc context --format llm`, unified `abix.error/1` JSON
errors, `amc verify`, the Metadata Region, `amc query`, and the `amc-mcp`
server.

## Research / experimental

* runtime materialization of the metadata image;
* ABI adaptation / shimming;
* dynamic-language boundary layers with differential semantic verification
  (`aue/`).

## Explicitly out of scope

ABIX is not a VM, an RPC framework, a universal object model, a debug-info
format or a C-ABI wrapper. See [What ABIX Is Not](../README.md#what-abix-is-not).
