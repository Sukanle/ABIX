# Roadmap

> **Roadmap items are not implementation requirements unless explicitly marked
> as current work.** Do not implement a future item just because it appears
> here. See [`AGENTS.md`](AGENTS.md) §13 (scope discipline).

## Current — ABIX 1.0 (Foundation, self-hosting and toolchain)

Established and maintained:

* ABI inspection, diff, compatibility classification and adapter generation;
* three metadata modes (Debug / Release / RelWithDebInfo) and ELF / Mach-O binary inspection;
* an explicit ABI semantic model and the `.abix` canonical artifact;
* self-hosting: ABIX describes and verifies its own public ABI;
* a stable bootstrap ABI separated from internal implementation;
* the C++ implementation and its Clang-based frontend;
* AMC toolchain: build / inspect / query / diff / verify / generate / metadata / adapter;
* ABI metadata: Metadata Region, `.abix.meta`, symbol server, offline scanning;
* ABI verification and structured diagnostics;
* one shared parser across CLI, MCP, LLDB and the runtime (`libabix-*`);
* ELF section reader (`amc_elf.h`) for offline binary inspection;
* ABI adapter generation (`amc adapter`) — field-level mapping between ABI versions;
* versioned TypeID registration — same TypeID across module ABI versions;
* multi-module MCP knowledge base and field-width assertion;
* LLDB C++ plugin (`libabix_lldb.so`) and Python helper;
* ELF metadata section test script (`tools/test_amc_elf-pe.py`).

## Next — ABIX 2.0 (Cross-language)

The next stage is **cross-language support**: ABIX IR becomes the common ABI
representation that other language frontends feed into and project out of.

### Main line — language plugins and cross-language bindings

* language plugin API and producer/consumer protocol
  ([`LANGUAGE-PLUGIN.md`](LANGUAGE-PLUGIN.md));
* a second language prototype beyond C++ — **Rust**, then **Zig**;
* cross-language type projection (for example C ABI scalars projected to
  Rust `core::ffi::c_*` aliases) built on the ABI-normalised primitive
  identity;
* cross-language bindings and a C++ ↔ Rust interoperability demo.

### P0 — stabilize and validate

* specification stabilization ([`ABI-SPEC.md`](ABI-SPEC.md));
* AMC usability: diagnostics, error messages, editor/CI output;
* ABI diff and compatibility reporting;
* ABI regression tests for real-world break cases;
* documentation and examples;
* external validation and adoption.

### P1 — broaden the toolchain

* build-system / package-manager integration;
* deeper debugger integration (for example LLDB go-to-definition via Source
  Origin) — LLDB C++ plugin delivered, Python helper delivered.

### P2 — ecosystem

* package ecosystem;
* broader debugger / editor integration;
* AI-agent integration surface (MCP tools, token-cost studies).

## AI-native ABI toolchain

ABIX is also developed as an AI-native ABI toolchain: an explicit ABI model is
exactly the durable, machine-readable interface AI coding agents need. See
[`docs/development/roadmap.md`](../docs/development/roadmap.md) for the detailed plan.

Delivered so far: `amc context --format llm`, unified `abix.error/1` JSON
errors, `amc verify`, the Metadata Region, `amc query`, the `amc-mcp` server,
multi-module ABI knowledge base, field-width assertion, and ELF binary
inspection via `amc metadata --from-elf`.

## Research / experimental

* runtime materialization of the metadata image;
* ABI adaptation / shimming — adapter generation delivered, runtime dispatch pending;
* dynamic-language boundary layers with differential semantic verification
  (`aue/`).

## Explicitly out of scope

ABIX is not a VM, an RPC framework, a universal object model, a debug-info
format or a C-ABI wrapper. See [What ABIX Is Not](../README.md#what-abix-is-not).
