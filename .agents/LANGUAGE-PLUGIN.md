# Language Plugin Development

ABIX is designed to be language-independent: AMC is a toolchain, not a
language-specific compiler. This document is the contract for adding a language.

Read [`ABI-SPEC.md`](ABI-SPEC.md) first. A plugin produces ABIX IR; it does not
own ABI semantics.

## 1. Goal

Given a language's source, produce ABIX IR describing the ABI of the selected
public surface — independent of the host language's implementation details.

## 2. Plugin Boundary

```text
Language AST
     │
     ▼
Language Plugin        (frontend)
     │
     ▼
ABIX IR  ──►  .abix / projection / analysis
```

The C++ implementation of this boundary is `amc-cpp` (a provider speaking a
JSON-lines IPC protocol) driven by `amc`. A new language may be implemented as a
separate provider executable; it does not need to live inside `amc`.

## 3. Responsibilities

A plugin **should**:

* parse the language's constructs;
* resolve types to concrete ABI types;
* extract ABI-relevant semantics (size, alignment, offsets, calling convention,
  parameter/return types, symbol naming);
* normalize language-specific constructs into the ABIX model;
* declare the target (arch / os / ABI / compiler / calling convention).

A plugin **must not**:

* redefine `TypeID` or `LayoutHash` semantics;
* redefine compatibility rules;
* create a second ABI IR;
* depend on runtime internals;
* leak language-specific concepts into the core model without an ABI meaning.

## 4. Supported ABI Concepts

The core model currently covers: primitive, enumeration, record, pointer, array,
function, namespace, alias; fields with offsets/flags; functions with signatures,
parameters, calling convention and flags; symbols; hashes; compatibility and
map records.

See [`ABI-SPEC.md`](ABI-SPEC.md) §3–§8.

## 5. Type Mapping

| Language construct | ABIX representation |
|--------------------|---------------------|
| fixed-width integer / float | primitive (size, align) |
| enum | enumeration (underlying integer type) |
| struct / class / union | record + fields |
| pointer / reference | pointer |
| fixed-size array | array (`array_count`) |
| function pointer | function |
| namespace / module | namespace type |
| `typedef` / `using` | alias (with an `underlying` field) |

The mapping must describe the **ABI**, not the source spelling: two source types
with the same ABI must produce the same identity.

## 6. Generics / Templates

* A template **primary** is recorded as a type with the
  `type_template_primary` flag; it is metadata for dependency closure, not a
  concrete native type.
* A concrete **specialization** with a stable instantiated layout is recorded as
  its own type.
* Do not expose dependent AST nodes; if a construct cannot be resolved to a
  concrete ABI, record it as opaque rather than guessing.

## 7. Namespaces / Modules

* Namespaces become `namespace_type` records; they are part of the diagnostic
  naming, not of compatibility.
* A symbol's fully qualified name uses the target language's separator mapped to
  a stable canonical form (`Owner::name` for C++).
* The runtime never resolves by name; names exist for tools and humans.

## 8. ABI Attributes

Attributes that change the ABI — calling convention, visibility, packing,
alignment, `extern "C"`, export/import — must be captured explicitly. An
attribute that does not change the ABI may be ignored.

## 9. Code Generation

Code generation is a **backend** concern and should be language-paired:

> Given source in language L, generate native code/headers for language L, not a
> generic C shim.

`amc generate -l <language>` selects a backend. A new backend should consume ABIX
IR (not the language AST) and must not change ABI semantics.

## 10. Testing Requirements

A plugin is not complete without:

* **extraction tests** — known source → expected ABI records;
* **serialization round-trip** — write/read `.abix` preserves the model;
* **compatibility tests** — intentional ABI changes are detected;
* **determinism** — identical inputs produce byte-identical artifacts;
* **cross-check** — at least one consumer (`amc query` / runtime projection)
  works against the produced artifact.

The C++ reference coverage lives in `amc/tests/`.

## 11. Example Plugin

```text
1. Implement parsing + type resolution for the language.
2. Emit ABIX IR records (types, fields, functions, parameters, symbols).
3. Assign TypeID / LayoutHash via the shared hashing rules (do not invent).
4. Declare the target.
5. Wire the provider into `amc` (or ship a standalone producer + `.abic.toml`).
6. Add extraction + round-trip + compatibility tests.
7. Document the mapping in docs/ (and `_zh`).
```

## 12. Version Compatibility

* A plugin targets a `.abix` format version and the ABIX IR revision.
* When ABIX IR changes, plugins must be updated in the same change window; the
  format version guards readers against mismatched artifacts.
* Plugins must tolerate unknown optional sections (see [`ABI-SPEC.md`](ABI-SPEC.md) §17).
