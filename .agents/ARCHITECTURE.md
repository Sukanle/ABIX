# Architecture

This document is the **design map** for ABIX. It states *why the system is
shaped this way* and which invariants must not be broken. It intentionally does
not track file-level implementation detail, which ages quickly.

For ABI semantics see [`ABI-SPEC.md`](ABI-SPEC.md). For extension see
[`LANGUAGE-PLUGIN.md`](LANGUAGE-PLUGIN.md). Deeper implementation knowledge lives
in [`docs/`](../docs/).

## 1. Design Goals

* Make the native ABI an explicit, machine-readable, verifiable object.
* Keep ABI semantics independent of any single language implementation.
* Keep compatible native execution on the native fast path.
* Give the ecosystem one machine-readable source of ABI truth.
* Make ABI evolution explicit and safe (identity, layout, compatibility).

## 2. Layer Model

```text
Language Ecosystem (C / C++ / Rust / Zig / ...)
        │
        ▼
Language Frontend / Plugin        maps one language AST
        │
        ▼
AMC                               extract / project / compare / generate
        │
        ▼
ABIX IR                           language-independent ABI semantics
        │
        ├──────────────► .abix / Metadata Region     serialized artifact
        └──────────────► ABIX Runtime                 binding / execution
```

Each layer has one responsibility. Data flows downward; nothing below
redefines a layer above it.

## 3. Ownership of Responsibilities

| Layer | Owns |
|-------|------|
| Language frontend / plugin | parsing, type resolution, extracting language ABI semantics, normalizing language constructs |
| AMC | driving frontends, projection, comparison, verification, code generation, distribution |
| ABIX IR | the ABI model: identity, layout, functions, symbols, compatibility, mapping |
| `.abix` / Metadata Region | canonical serialization of the model |
| ABIX Runtime | consuming ABI: registry, binding, adaptation, native execution |

## 4. Architectural Invariants

### 4.1 One ABI Truth

There is exactly one authoritative ABI semantic model. The runtime may project
or cache it, but must not redefine it.

### 4.2 Native Execution

ABIX establishes the ABI relationship dynamically and then executes through the
native ABI. It does not interpret compatible calls.

### 4.3 No Universal Object Model

ABIX describes existing native types and binary contracts. It does not define a
universal object hierarchy, ownership runtime, GC, or VM.

### 4.4 Language Independence

ABIX IR is language-independent. Language-specific behaviour belongs in a
frontend/plugin, not in the core model.

### 4.5 Pointer-Free Serialization

Serialized metadata uses offsets and indices, never absolute pointers, so it is
relocation-free and survives ASLR, PIE, mmap and cross-process transport.

### 4.6 Names Are Not Identity

`TypeID` and `LayoutHash` define identity and layout. Names are diagnostic only
and must never drive compatibility.

### 4.7 Deterministic Artifacts

Given the same inputs, `amc build` / `amc generate` produce identical artifacts.

## 5. Dependency Direction

```text
docs / spec ──► implementation
plugins ──► ABIX IR ──► AMC ──► runtime
                     └──► .abix
```

* Plugins depend on ABIX IR, never on runtime internals.
* The runtime depends on the ABI model, never on a language AST.
* Tooling splits into `libabix-format` / `libabix-abi` / `libabix-metadata` /
  `libabix-tools` (plus header-only `libabix-runtime`) so every consumer shares
  one parser.

## 6. Bootstrap Architecture

ABIX is self-hosting for its public ABI: it describes its own public ABI with
its own model, then uses that description to build and verify later versions.
The stable bootstrap ABI is separated from internal implementation. See
[`docs/self-hosting.md`](../docs/self-hosting.md).

## 7. Runtime Architecture

```text
.abix (full artifact)
   │ projection
   ▼
Metadata Region (embedded, pointer-free, mmap-able)
   │ materialization
   ▼
Runtime Descriptor (pointer-rich, hot path)
```

Dynamic cost is concentrated at initialisation. After materialization the hot
path is close to a static ABI. See [`docs/runtime.md`](../docs/runtime.md) and
[`docs/metadata_modes.md`](../docs/metadata_modes.md).

## 7.1 Versioned TypeID Registration

The `RuntimeRegistry` supports versioned registration: the same `TypeID` may
coexist under distinct module ABI versions with different layouts. Only a
repeated `TypeID` *within the same version* is a layout conflict (Boundary #1).

```cpp
// Two versions of the same module may register the same TypeID.
registry.register_module(v1_module, /*version=*/1);
registry.register_module(v2_module, /*version=*/2);

// find_type() selects by exact (TypeID, version) pair.
const auto *entry = registry.find_type(my_type_id, /*version=*/2);

// find_by_id() returns the newest registered version.
const auto *latest = registry.find_by_id(my_type_id);
```

The version is parsed from the module's `version` string: leading decimal
digits map to an integer (`"1"` → 1, `"1.0"` → 1, `"2"` → 2); no digits → 0
(experimental). The adapter layer selects the version at dispatch time.

## 7.2 ABI Adapter

The ABI adapter generates field-level mapping code between two ABIX modules:

```text
amc diff v1.abix v2.abix  →  compatibility report (maps[])
amc adapter v1.abix v2.abix  →  C++ header with field-level copy
```

The generated adapter applies `copy_field` / `add_default` / `skip_field` /
`convert_int` / `convert_float` mappings from raw source memory to raw target
memory. It works without the C++ projections being present.

Typed mode (`amc adapter --typed`) generates `abix::adapter<Source, Target>`
specializations for each compatible type pair. The primary template returns
`false` so callers can fall back gracefully.

See [`docs/amc.md`](../docs/amc.md) `amc adapter` section and
[`amc/core/amc_adapter.h`](../amc/core/amc_adapter.h).

## 8. Extension Points

* **New language** → implement a frontend/plugin ([`LANGUAGE-PLUGIN.md`](LANGUAGE-PLUGIN.md)).
* **New artifact consumer** → link `libabix-*`, do not write a new parser.
* **New tool** → build on `amc/core` query/verify APIs; keep JSON schemas stable.
* **New runtime capability** → extend the runtime projection, not the ABI model,
  unless it is a genuine ABI concept.
* **ABI adaptation** → use `amc adapter` to generate field-level mapping code
  between ABI versions; the runtime adapter dispatches via `abix::adapter<S,T>`.

## 9. Forbidden Architectural Patterns

Do not:

* make the runtime registry a second ABI authority;
* put C++ template / language semantics into ABIX IR;
* introduce runtime dispatch for directly callable functions;
* make `.abix` or the Metadata Region depend on raw pointers;
* require the runtime to understand source-language ASTs;
* let names participate in ABI identity or compatibility;
* hand-edit generated artifacts instead of regenerating them;
* duplicate a metadata parser in a new consumer;
* change ABI semantics to simplify an implementation;
* hardcode adapter mappings instead of deriving them from compatibility reports;

If a task appears to require one of these, stop and explain the conflict
before changing the constraint ([`AGENTS.md`](AGENTS.md) §13).
