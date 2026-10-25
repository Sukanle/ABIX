# Roadmap

> **Roadmap items are not implementation requirements unless explicitly marked
> as current work.** Do not implement a future item just because it appears
> here. See [`AGENTS.md`](AGENTS.md) §13 (scope discipline).

## Foundation — ABIX 0.1.0 (released, self-hosting and toolchain)

**Platform support:** Linux and macOS are supported today. Windows is **not
available yet** — the PE/CRT paths, cross-DLL behaviour and platform-consistency
tests are still being validated.

Established and maintained:

- ABI inspection, diff, compatibility classification and adapter generation;
- three metadata modes (Debug / Release / RelWithDebInfo) and ELF / Mach-O binary inspection;
- an explicit ABI semantic model and the `.abix` canonical artifact;
- self-hosting: ABIX describes and verifies its own public ABI;
- a stable bootstrap ABI separated from internal implementation;
- the C++ implementation and its Clang-based frontend;
- AMC toolchain: build / inspect / query / diff / verify / generate / metadata / adapter;
- ABI metadata: Metadata Region, `.abix.meta`, symbol server, offline scanning;
- ABI verification and structured diagnostics;
- one shared parser across CLI, MCP, LLDB and the runtime (`libabix-*`);
- ELF section reader (`amc_elf.h`) for offline binary inspection;
- ABI adapter generation (`amc adapter`) — field-level mapping between ABI versions;
- versioned TypeID registration — same TypeID across module ABI versions;
- multi-module MCP knowledge base and field-width assertion;
- LLDB C++ plugin (`libabix_lldb.so`) and Python helper;
- ELF metadata section test suite (`tools/run_test.py --elf-pe`).

## Current — ABIX 0.2.0 (Cross-language, released)

The current stage is **cross-language support**: ABIX IR becomes the common ABI
representation that other language frontends feed into and project out of.

### Main line — language plugins and cross-language bindings

- language plugin API and producer/consumer protocol
  ([`LANGUAGE-PLUGIN.md`](LANGUAGE-PLUGIN.md));
- a second language prototype beyond C++ — **Rust**, then **Zig**;
- cross-language type projection (for example C ABI scalars projected to
  Rust `core::ffi::c_*` aliases) built on the ABI-normalised primitive
  identity;
- cross-language bindings and a C++ ↔ Rust interoperability demo.

Delivered so far: an initial `amc-rust` provider (`frontend` + `backend`,
JSON-lines IPC) — a source-level `#[repr(C)]` / `extern "C"` extractor and a
Rust `repr(C)` projection built on the ABI-normalised primitive identity
(`core::ffi::c_*`). A C++ fixture and an ABI-equal Rust fixture already produce
the same `TypeID` / `LayoutHash` and report `compatible=true`.

Also delivered: a minimal `amc-zig` **backend-only** provider that consumes the
same `.abix` and projects it to Zig (`extern struct` / `extern fn` / `[*c]`
typed pointers / `comptime` layout assertions). It validates that ABIX IR is
reusable: the same artifact drives both the Rust and the Zig projection, and
`AMC/tests/interop/` builds one native library and runs both consumers against
it. The C++ → ABIX → Rust closed loop (with ownership, error-boundary and target
contracts in an optional metadata-region extension directory) is complete.

### P0 — stabilize and validate

- specification stabilization ([`ABI-SPEC.md`](ABI-SPEC.md));
- AMC usability: diagnostics, error messages, editor/CI output;
- ABI diff and compatibility reporting;
- ABI regression tests for real-world break cases;
- documentation and examples;
- external validation and adoption.

## Patch — ABIX 0.2.1 (Maintenance)

Maintenance release: fix existing behavior and add tests/documentation; no
public ABI or error-protocol change.

- `last_error()` is thread-local; its per-thread / per-DSO scope is documented;
- existing error-status enums have explicit numeric values and stable
  `to_string()` spellings;
- added thread-isolation, cross-language error-propagation and `[[error]]`
  negative tests;
- Zig error-set/error-union projection remains a documented limitation.

See [`CHANGELOG.md`](../CHANGELOG.md).

## Post-0.2.0 — toolchain and ecosystem follow-ups

These are follow-up goals after the 0.2.0 cross-language milestone. They are not part of the 0.2.0 release gate and are tracked with the 0.3.0 work.

### P1 — broaden the toolchain

- build-system / package-manager integration;
- deeper debugger integration (for example LLDB go-to-definition via Source
  Origin) — LLDB C++ plugin delivered, Python helper delivered.

### P1 feat — AMC Interface Devirtualization (v0.3.0)

ABIX Interface keeps its explicit versioned contract and runtime dispatch as the
correctness baseline. This feature is a compile-time AMC optimization: use the
Clang AST and ABIX metadata to prove the adapter/target reachable from a call
site, then emit a direct adapter or native call when safe and fall back to the
existing dispatch otherwise. It is not a new runtime mechanism and it does not
change `.abix` contract semantics.

Plan and analysis: [`memory/amc-interface.md`](../memory/amc-interface.md).

- **P0** — reproducible baseline (direct / true vtable / ABIX unique+shared /
  `pro::proxy`) and static `KnownTarget` direct-adapter calls.
- **P1** — one target-determinism analysis for unique and shared proxies,
  cross-function tracking and conservative alias fallback.
- **P2** — guarded speculative devirtualization that preserves rebinding,
  module unload, lifetime and concurrency semantics.
- **P3** — whole-program / LTO target sets, with closed and extensible provider
  sets kept distinct.

Acceptance: a `KnownTarget` call site has no ABIX slot indirect dispatch and
unchanged behavior; unknown/rebindable sites keep the dynamic path; diagnostics
explain why a site was not optimized. See the plan for the full constraints and
benchmark matrix.

### P2 — ecosystem

- package ecosystem;
- broader debugger / editor integration;
- **under discussion**: the AI-agent integration surface (advanced MCP tools,
  workflows, token-cost studies) — decide whether these are premature; the
  existing low-level `amc-mcp` queries remain supported.

## AI-native ABI toolchain

ABIX is also developed as an AI-native ABI toolchain: an explicit ABI model is
exactly the durable, machine-readable interface AI coding agents need. See
[`docs/development/roadmap.md`](../docs/development/roadmap.md) for the detailed plan.

Delivered so far: `amc context --format llm`, unified `abix.error/1` JSON
errors, `amc verify`, the Metadata Region, `amc query`, the `amc-mcp` server,
multi-module ABI knowledge base, field-width assertion, and ELF binary
inspection via `amc metadata --from-elf`.

## Research / experimental

- runtime materialization of the metadata image;
- ABI adaptation / shimming — adapter generation delivered, runtime dispatch pending;
- dynamic-language boundary layers with differential semantic verification
  (`Aue/`).

## Explicitly out of scope

ABIX is not a VM, an RPC framework, a universal object model, a debug-info
format or a C-ABI wrapper. See [What ABIX Is Not](../README.md#what-abix-is-not).
