# Architecture

> The authoritative design map and invariants live in
> [`../ARCHITECTURE.md`](../.agents/ARCHITECTURE.md).

ABIX turns the ABI into an explicit, machine-readable object and keeps a single
source of ABI truth across the toolchain and the runtime.

## Layers

```text
┌──────────────────────────────────────────────────────┐
│                  Language Ecosystem                   │
│       C      C++      Rust      Zig      ...          │
└─────────────────────────┬────────────────────────────┘
                          ▼
┌──────────────────────────────────────────────────────┐
│                         AMC                           │
│                 ABI Toolchain / Driver                │
└─────────────────────────┬────────────────────────────┘
                          ▼
┌──────────────────────────────────────────────────────┐
│                       ABIX IR                         │
│              ABI Semantic Representation              │
└───────────────┬───────────────────────┬──────────────┘
                ▼                       ▼
        ┌──────────────┐       ┌──────────────────┐
        │    .abix     │       │  ABIX Runtime    │
        │ ABI Artifact │       │  Native Binding  │
        └──────┬───────┘       └────────┬─────────┘
               ▼                        ▼
      CI / Package / Tools         Native Binary
```

* **ABIX IR** — the language-independent ABI model: types, fields, functions,
  parameters, symbols, hashes, compatibility and mapping records.
* **`.abix`** — the serialized, canonical artifact. See [`abix.md`](abix.md).
* **AMC** — extracts ABI from a language AST, projects/compares modules and
  generates native code. See [`amc.md`](amc.md).
* **ABIX Runtime** — consumes the ABI: registry, binding, dispatch, adaptation.
  See [`runtime.md`](runtime.md).

## One source of ABI truth

> The runtime may use a **projection** of the ABI model, but must not
> independently redefine ABI semantics.

The same model drives every consumer. The toolchain is split into libraries so
that each consumer links only what it needs while sharing exactly one parser:

| Library | Responsibility |
|---------|----------------|
| `libabix-format` | `.abix` v4 read/write, hashing, canonical form, ELF reader |
| `libabix-abi` | `TypeID`/`LayoutHash` comparison, compatibility diff |
| `libabix-metadata` | Metadata Region serialize/parse/materialize, context, query |
| `libabix-tools` | Lua contract generation, symbol store |
| `libabix-runtime` | header-only registry / binding layer |

`amc-core` is an aggregate over these for the executables.

## Identity

ABIX separates four identities that are frequently conflated:

| ID | Meaning | Use |
|----|---------|-----|
| **TypeID** | semantic type identity | "is this the same type?" |
| **LayoutHash** | physical layout identity | "is the memory layout compatible?" |
| **BuildID** | binary build identity | locate the matching artifact |
| **MetadataID** | metadata content identity | dedup / integrity |

Lookup chain: `Binary → BuildID → .abix → MetadataID → TypeID → LayoutHash`.
See [`compatibility.md`](compatibility.md).

## Metadata modes

ABIX metadata has three consumption modes, from richest to most compact:

```text
.abix (full artifact)
   │  projection
   ▼
Metadata Region (embedded, pointer-free, mmap-able)
   │  materialization
   ▼
Runtime Descriptor (pointer-rich, hot path)
```

* The **Region** is offset-based and relocation-free; it can be shipped as a
  file, embedded in an ELF section, or mmap'd by an offline parser.
* The **Runtime Descriptor** is a one-time materialization of the Region at
  initialization; after that the hot path is close to a static ABI.

See [`metadata_modes.md`](metadata_modes.md).

## Execution model

ABIX is **not** a VM, RPC framework or universal object runtime. For a
compatible native function, ABIX establishes the relationship and then executes
through the native ABI:

```text
discover → verify → identify → bind → adapt → native call
```

## Repository layout

```text
ABIX
├── abix/      ABI model + runtime (header-only registry)
├── amc/        AMC toolchain: core/, cpp/ frontend+backend, dump/, mcp/
├── test/       runtime + unit tests (Catch2)
├── bench/      benchmarks
├── aue/        experimental Lua boundary layer + conformance runner
├── tools/      helper scripts (MCP demo, token cost, LLDB command)
└── docs/       specification and design
```
