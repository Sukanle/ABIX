# ABI-SPEC.md — ABIX ABI Specification

> This is the normative description of what a legal ABIX ABI is.
> When implementation behaviour conflicts with this document, the
> implementation is considered incorrect unless the specification is
> intentionally being changed (see [Precedence](#0-precedence)).

Byte-level encoding lives in [`docs/abix.md`](../docs/abix/abix.md); the build
configuration in [`docs/abic.md`](../docs/abix/abic.md).

## 0. Precedence

```text
ABI-SPEC.md  >  ARCHITECTURE.md  >  LANGUAGE-PLUGIN.md  >  docs/  >  implementation
```

The implementation is evidence of the current state, not the definition of
intended ABI semantics. A change to any rule below is an **ABI semantic change**
and must update this document, [`ARCHITECTURE.md`](ARCHITECTURE.md) and tests.

## 1. Scope

This specification defines:

- the ABI semantic model (types, layouts, functions, symbols, modules);
- the identities used to compare and locate ABI facts;
- the compatibility rules;
- the canonical representation and its encoding rules;
- versioning and extension rules.

It does not define a language, an object model, an execution engine, or a
debug-information format.

## 2. Terminology

| Term          | Meaning                                                                                               |
| ------------- | ----------------------------------------------------------------------------------------------------- |
| **Type**      | a native type as seen at the ABI: primitive, enum, record, pointer, array, function, namespace, alias |
| **Field**     | a named, offset-addressable member of a record                                                        |
| **Layout**    | size, alignment and field placement of a type                                                         |
| **Function**  | a callable ABI entity with a signature and calling convention                                         |
| **Parameter** | an ordered function input                                                                             |
| **Symbol**    | a named entry point mapping to a type, field or function                                              |
| **Module**    | a set of ABI facts for one artifact                                                                   |
| **Artifact**  | the serialized form of a module (`.abix`)                                                             |

## 3. ABI Model

A module is a set of records:

```text
Module
 ├── identity        package, target, compiler, calling convention
 ├── types[]         + fields[]
 ├── functions[]     + parameters[]
 ├── symbols[]
 ├── hashes[]        TypeID / LayoutHash / SignatureHash / ABIHash
 ├── compatibility[] (comparison results)
 └── maps[]          (field-level mapping between two layouts)
```

## 4. Type Identity (TypeID)

- `TypeID` is a 128-bit value identifying a type semantically.
- A `TypeID` is **name-independent at comparison time**: two modules may spell a
  type differently and still agree, and a name is never used to decide
  compatibility.
- `TypeID` must be stable across builds, machines and translation units for the
  same semantic type.
- A zero `TypeID` is invalid.
- Two distinct types must not share a `TypeID` within one module.

The derivation of a `TypeID` depends on how much of the type's semantics is
determined by its ABI:

- **Scalar / primitive types** are identified by an **ABI descriptor**:
  kind, size, alignment, and — where they change the ABI — signedness
  (integers, and the implementation-defined `char` / `wchar_t`) or
  floating-point format (IEEE binary16/32/64/128, x87 80-bit, PowerPC
  double-double, ...). The source spelling is excluded, so `int` / `int32_t`,
  `long` / `long long` (LP64), and a 64-bit C `double` / Rust `f64` on the same
  target share one `TypeID`. `bool`, and each character type — `char`,
  `signed char`, `unsigned char`, `char8_t`, `char16_t`, `char32_t`,
  `wchar_t` — is a distinct ABI kind; plain `char` and `wchar_t` carry their
  target-defined signedness, so a signed and an unsigned `char` never merge.
- **Nominal types** (`record`, `enumeration`, `alias`, `namespace_type`) are
  identified by their canonical qualified name. Two distinct nominal types must
  not be merged merely because their layouts happen to match; physical
  equivalence is expressed through `LayoutHash` (§5) instead.

A primitive and a nominal type may still be ABI-compatible; that is decided by
`LayoutHash` and the compatibility rules (§12), never by the spelling.

## 5. Layout Identity (LayoutHash)

- `LayoutHash` is a 128-bit value identifying the **physical layout** of a type.
- It is derived from the type's size, alignment and its fields' identity,
  offsets and order.
- Two types with the same `TypeID` and the same `LayoutHash` are layout
  compatible.
- Same `TypeID`, different `LayoutHash` is an **ABI conflict**.
- `LayoutHash` must be **independent of the source spelling**: two ABI-equal
  types declared under different names must hash to the same layout. In
  particular, a primitive's layout is a function of its ABI descriptor (§4), not
  of its name, so a C `double` and a Rust `f64` of the same target share a
  `LayoutHash`.
- A concrete type with size or alignment zero is invalid.

## 6. Function ABI

A function record carries:

- `owner_type` (zero for a free/namespace function);
- `signature` (`SignatureHash`);
- `return_type` (`TypeID`);
- ordered `parameters[]` with their `TypeID`s;
- `calling_convention`;
- flags.

A function is identified at the ABI by its signature, not its name. Parameter
order is significant. A function whose return type is zero is invalid.

### 6.1 Error boundary

A function may carry an optional **error-boundary contract** describing how it
reports success/failure and where the result value is carried: the status source
(the return value or a parameter), the status value that means success, and the
parameter holding the result. It lets a consumer turn a bare C status code into a
typed result (`Result<T, E>`).

The contract is a **semantic contract, not a layout fact**: it does not change
`TypeID`, `LayoutHash` or `ABIHash`, and a module without it is fully valid
(every function is then "no declared error boundary"). It never permits a
language-native unwinding mechanism (C++ exception, Rust panic) to cross the ABI
boundary; the producing side converts it to the declared status.

## 7. Symbol Identity

- A `Symbol` maps a name to a type, field or function by index.
- Symbols are a **name → ABI fact** index for humans and tools.
- Symbols are not ABI identity and must not participate in compatibility
  decisions.

## 8. Calling Convention

- The calling convention is part of the function ABI and must match for two
  functions to be interchangeable.
- `0` means unspecified/default; concrete frontends assign the language's
  convention code.
- A calling-convention mismatch is incompatible, even when layouts match.

## 9. Ownership

Ownership is a **contract about how an ABI interaction is used**, not a fixed
property of a type: the same type may be borrowed by one function and owned by
another. It is declared, never inferred.

A module may carry an optional ownership contract:

- **release policies** — a value type and the entry points that create and
  release it;
- **per-interaction modes** — for a parameter or return value, one of
  `borrowed` (non-owning view), `owned` (receiver releases through the declared
  release entry point) or `copy` (independent data from an explicit copy), plus
  a reference to the release entry point for `owned` returns.

Rules:

- Ownership is a contract, not a layout fact: it does **not** participate in
  `TypeID`, `LayoutHash` or `ABIHash`, and it never changes the meaning of an
  existing record.
- An absent contract means `unspecified`; a reader must not infer `borrowed` or
  `owned` from the absence of a record.
- An `owned` interaction must reference an available release entry point; a
  contract without one is rejected rather than assumed safe.
- A module without ownership records is fully valid.

Because it is optional and additive, ownership differences are not (yet) part of
the compatibility classification; they are reported through the contract query,
not folded into a single "ABI compatible" answer.

### 9.1 ABIX-STD

**ABIX-STD** is the standard cross-language ABI primitive layer. It is not a
replacement STL: it defines a small set of ABI types and contracts that
different languages can consume without each re-inventing a library interface.
The ownership contract above is the ABIX-STD ownership vocabulary.

The three data-passing semantics are:

- **borrow** — a non-owning view; the receiver must not release the data, and it
  is valid only while the producer keeps it alive;
- **owned** — the receiver takes ownership and releases through the producer's
  declared release entry point;
- **copy** — independent data produced by an explicit copy.

The first ABIX-STD primitive is a **borrowed span** (`abix_span_i32`): a plain
two-field POD `{ const int32_t *data; uint64_t len; }` whose layout is identical
in every language. ABIX records concrete instantiations (one span type per
element type), not generic parameters; generic instantiation is a frontend
concern (see [`LANGUAGE-PLUGIN.md`](LANGUAGE-PLUGIN.md)).

ABIX-STD types are ordinary ABIX types: their `TypeID`/`LayoutHash` come from the
shared rules, and the borrow/owned/copy semantics are carried by the ownership
contract, never by the layout.

ABIX-STD is organized in three layers, and standardizes **boundary protocols, not
container implementations**:

1. **ABIX Core** records ABI facts (layout, pointers, length, alignment, calling
   convention, type identity) and does not embed any language's containers.
2. **ABIX-STD contracts** define a few composable interoperability contracts.
3. **Language adapters and AMC** map each language's types to a contract and
   generate thin adapters, copies or conversions at the boundary.

Contracts are matched by **capability**, not by container name: a type is mapped
by whether it can prove the required capability (contiguous storage, a release
entry point, safe copying), never by a name-based table such as
`vector ↔ Vec ↔ ArrayList`. Capability must be proven by a trusted frontend,
an explicit declaration, or verified metadata -- never inferred from a name.

| Priority | Contracts                                                        |
| -------- | ---------------------------------------------------------------- |
| P0       | sequence view, string view, opaque handle, ownership, error boundary |
| P1       | optional, result, callback, generic range                        |
| P2       | map, set, deque, complex graphs, language-specific containers    |

P2 types are not given a unified implementation: they are exposed through
`Opaque` handles with accessors, capability adapters, or explicit copies. A
`std::vector<T>` is not a container contract of its own: AMC reads `data()` and
`size()` through accessors and lowers it to a `SequenceView<T>` when the element
type, layout, borrowing lifetime and invalidation conditions allow; otherwise it
uses `Opaque` or an explicit copy. Nothing is claimed to be zero-copy merely
because a name or an API looks similar.

## 10. Target

A module records `arch`, `os`, `target_abi`, `compiler`, the module-level
`calling_convention`, and the optional platform facts `pointer_bits` and
`endianness` (0 means unknown). ABI facts are only comparable when their targets
are compatible; tooling must report target differences rather than silently
comparing them.

A module describes **one** target, and target differences are resolved by
generating one artifact per target (a cross-compiled `.abix`), rather than by
putting unresolved, target-independent types into a single portable artifact.
The core model therefore stores the concrete ABI fact (width, alignment,
floating-point format) for that target. Source-language identity — how a
language spelled the type (`c_long`, `i32`) — is not an ABIX core type; it may be
retained as optional provenance that never participates in identity. See
[`docs/development/type-model.md`](../docs/development/type-model.md).

### 10.1 Runtime target verification

A region may carry a target description so a consumer can verify, at load time,
that the artifact's platform matches the host or a required target. Verification
distinguishes three outcomes and **never reports an unknown as compatible**:

- `compatible` — every checked fact matched;
- `incompatible` — a checked fact disagreed (including a pointer-typed record
  that is not pointer-sized);
- `unverifiable` — evidence is missing (no target description, or the region
  leaves a checked fact unknown).

Static verification (comparing a target against the produced artifact at build
or CI time) and runtime verification (platform identity and metadata consistency
at load) are separate concerns; facts that cannot be independently verified at
runtime are reported as unverifiable rather than assumed.

Verification is reported **per dimension**, so a tool can say precisely what
matched, what disagreed, and what could not be checked from the region alone:

| Dimension           | Checks from the region                                    |
| ------------------- | --------------------------------------------------------- |
| `platform`          | arch / os / abi / pointer width / endianness vs expected  |
| `layout`            | pointer width consistency; fields inside their owner type |
| `calling_convention`| each function declares a convention matching the required |

Facts that need the compiled binary or the target toolchain — real field offsets
in an object file, aggregate parameter classification, hidden return pointers —
are **not** claimed by region verification; they belong to the build stage and
come back `unverifiable` here. Region-internal `layout` checks prove the metadata
is self-consistent, not that it matches a particular binary.

## 11. Canonical Representation

- The canonical form is deterministic: the same inputs produce byte-identical
  output.
- Hashing domains and canonical encoding are part of this specification;
  changing them invalidates every existing artifact.
- Primitive `TypeID` values are derived from ABI descriptors (§4) and are
  deliberately spelling-independent. Introducing the descriptor form changes
  the `TypeID` of every primitive, so all artifacts produced before this
  revision are invalid and must be regenerated.
- `ABIHash` is the module-level artifact identity, computed over the canonical
  content (identity, types, functions, maps). It excludes diagnostic-only data.
- `SignatureHash` identifies a function signature.
- **Source origin is not ABI identity**: the optional debug `sources` section is
  excluded from `ABIHash`.

## 12. Compat Rules

Comparing a source ABI to a target ABI classifies each type. A source type is
paired with a target type by canonical name for nominal types, falling back to
`TypeID` for primitives (so ABI-equal scalars line up across spellings).

### Compatible

- `identical` — same `TypeID`, same `LayoutHash`.
- `layout_compatible` — same effective layout.
- `map_compatible` — layout differs, but a field mapping exists and is complete.

### Conditionally compatible

- Mapping-based compatibility is only as safe as the generated map; consumers
  must apply the map rather than reinterpret memory.

### Incompatible

- Same `TypeID`, different `LayoutHash`.
- Size/alignment change, field removal, field type change, parameter or return
  type change, calling-convention change.
- A type required by the source is absent from the target.

Verification (`amc verify`) is stricter than classification: **any** difference,
including _added_ types or functions, is drift, because widening the ABI surface
must be an explicit decision.

## 13. Serialization

- Serialized metadata is **pointer-free**: cross references use offsets and
  indices, never absolute addresses.
- The Metadata Region is self-describing: a manifest precedes the records and
  records each section's offset and size.
- A Metadata Region is integrity-checked by recomputing `MetadataID` over
  `desc + hash + names`.
- Artifacts must be mmap-able and parse-able offline without loading the program.

### 13.1 Extension directory

Optional contract tables (ownership, error boundary, target description, and
future contracts) are discovered through a trailing **extension directory** in
the `desc` section, after the fixed record tables:

```text
[ header  ]  version, count, flags, reserved
[ entries ]  kind, version, count, byte_offset, byte_size, reserved
[ records ]  payloads, addressed by their entry
```

- A region with **no extensions** has `desc_size` equal to the fixed record
  size; its bytes are identical to a region produced before the directory
  existed.
- `directory.version` describes the directory encoding only; each entry carries
  its own `version`, so contracts evolve independently.
- `kind` names the contract type, not the record shape; `byte_size` alone fixes
  record boundaries. A reader may skip an unknown `kind` only when the directory
  is well formed and the boundaries check out.
- An unknown directory version, a duplicate `kind`, or an out-of-bounds entry is
  rejected rather than guessed at.

Known extension kinds: `1` ownership, `2` error boundary, `3` target
description, `4` ABIX-STD lowering. Adding a kind does not require a format
version change.

## 14. Versioning

- The `.abix` format carries a format version; readers reject unsupported
  versions rather than guessing.
- The Metadata Region manifest carries major/minor version and a hash algorithm
  id/version.
- `TypeID`/`LayoutHash` are independent of the container version.

### 14.1 Version Independence

ABIX distinguishes between the version of the `.abix` representation, the
semantic ABI specification, and the AMC toolchain.

These version dimensions serve different purposes and MUST NOT be implicitly
coupled.

| Version                          | Scope                  | Purpose                                                                                                   |
| -------------------------------- | ---------------------- | --------------------------------------------------------------------------------------------------------- |
| Format Version                   | `.abix` representation | Defines how ABIX data is encoded and interpreted at the format level                                      |
| Specification Revision / Profile | ABI semantic model     | Defines ABI semantics, compatibility rules, and required interpretation                                   |
| AMC Toolchain Version            | AMC implementation     | Defines available CLI tools, generators, integrations, MCP/Agent capabilities, and other tooling features |

#### Format Version

The Format Version governs the compatibility contract of the `.abix`
representation.

A Format Version MAY change when a change to the ABIX representation cannot be
handled by the existing compatibility rules or extension mechanism.

The addition of an optional extension MUST NOT, by itself, require a Format
Version change.

A Format Version change does not imply that the AMC toolchain itself has
introduced new functionality.

#### Specification Revision / Profile

The ABI specification defines the semantics of the information represented by
ABIX, including type identity, layout semantics, compatibility rules, and
other normative behavior.

A semantic change MAY require a new specification revision or profile even when
the underlying `.abix` binary representation remains unchanged.

Implementations SHOULD explicitly identify the specification revision or
profile required to interpret normative ABI semantics.

#### AMC Toolchain Version

AMC versions describe the functionality of the AMC implementation.

Toolchain features such as:

- Rust or Zig binding generation
- LSP integration
- MCP tools
- Agent workflows
- package management
- additional inspection or generation capabilities

do not require a Format Version change unless they introduce or depend on an
incompatible change to the ABIX representation or its normative semantics.

For example:

```text
ABIX Format 2.x
    │
    ├── AMC 2.x
    └── AMC 3.x
          ├── Rust bindings
          ├── MCP
          └── Agent
```

AMC 3.x MAY consume ABIX files produced by AMC 2.x when the required Format
Version and Specification Revision/Profile are supported.

Compat is defined by the reader's supported versions and capabilities;
it MUST NOT be assumed to be bidirectional.

## 15. Extension Rules

- New optional sections may be added after the last known section id; readers
  must skip unknown optional sections.
- A new required section, or a change to an existing record layout, is a format
  version change.
- Extensions must not change the meaning of existing records.
- Optional contract tables inside the Metadata Region use the extension
  directory (§13.1); adding a contract kind does not require a format version
  change.

## 16. Reserved Fields

- Reserved fields must be written as zero and ignored on read.
- A reader must not rely on reserved bits retaining a value across versions.

## 17. Forward / Backward Compat

- **Backward**: a newer reader must read older artifacts by skipping unknown
  optional data and honouring reserved-field rules.
- **Forward**: an older reader must reject required data it does not understand,
  and must not treat an unknown optional section as an error.
- A name-stripped artifact remains valid: names and source origins are
  diagnostic and are excluded from `ABIHash`; `TypeID`/`LayoutHash` are
  unaffected.

## 18. Generic Instantiation

ABIX does not define a universal generic system, and it does not unify C++
templates, Rust generics and Zig `comptime`. It records **concrete ABI
entities**. Instantiation is a pipeline, not a single type transform:

```text
source generic
    -> frontend instantiation + constraint check   (the source language owns this)
    -> concrete type + target ABI facts
    -> ABIX-STD contract match                       (direct map / thin adapter / Opaque / reject)
    -> .abix + generated bindings and symbols
```

Responsibilities are separated:

- the **language frontend** performs instantiation, enforces the language's
  constraints, and reports the concrete layout and calling ABI (aggregate
  parameter passing, hidden return pointers, etc. cannot be derived from
  `sizeof`/`alignof` alone);
- **AMC** coordinates the request, selects the target, matches the concrete type
  against an ABIX-STD contract, and generates bindings or adapters;
- **ABIX Core / ABIX-STD** record the resulting facts and contracts; they never
  implement a language's template semantics.

Two stages are distinct:

- **Instantiation** replaces generic parameters with concrete arguments to
  obtain a real type (`abix_span<int32_t>`).
- **ABI lowering** decides whether that type can be used directly at the boundary
  (matches an ABIX-STD contract) or needs a thin adapter. A successful
  instantiation does **not** by itself prove contract compatibility: a concrete
  `std::span<int>` is not automatically an ABIX-STD `SequenceView<i32>`; AMC must
  prove representation, alignment, calling ABI, element representation and
  lifetime compatibility, or generate an adapter.

Lowering against a contract has three outcomes:

| Outcome         | Meaning                                                             |
| --------------- | ------------------------------------------------------------------ |
| `direct`        | the concrete type is already the boundary entity                    |
| `needs_adapter` | compatible shape, but a thin adapter to the canonical type is generated |
| `unsupported`   | cannot be lowered; the instantiation is rejected (no silent ABI)    |

The first contract is `sequence-view`: a record of exactly two fields — a data
pointer and an unsigned length. Matching is **not shape alone**; each dimension is
checked independently:

| Dimension    | Must confirm                                          | Otherwise            |
| ------------ | ----------------------------------------------------- | -------------------- |
| data field   | a pointer (or a contract-permitted equivalent)        | `unsupported`        |
| element type | the pointee `TypeID` matches the declared element     | `unsupported`        |
| mutability   | a mutable contract requires a non-const pointer       | `unsupported`        |
| length field | an unsigned integer of known width                    | `unsupported`        |
| layout       | size/align/offsets/representation are known           | adapter or reject    |
| target ABI   | pointer/length/aggregate passing is known             | not `direct`         |

A pointer records its pointee (`PointeeRef`, an optional section) so element
matching uses ABI identity, not a spelling: `const float *` does not match
`SequenceView<i32>` merely because it has two fields. When a pointer has no
recorded pointee, the matcher falls back to a normalized ABI spelling.

The second contract is `string-view`: a record of `{ data, length, encoding }`.
The code unit is matched by the data pointer's pointee `TypeID`; the length and
encoding fields must be unsigned integers. It is read-only and borrowed by
default (a mutable contract requires a non-const pointer). The `encoding` value
is a **runtime** property: the build stage checks the field's presence and type,
and runtime verification can check its value; the build does not assume a
particular encoding merely because the shape matches.

Identity has two layers that must not be conflated:

- a **type reference** links type-graph nodes inside one `.abix`; the metadata
  region may additionally address a type by its table index, which is a
  file-local reference and MUST NOT be compared across files;
- a **stable type identity** is the canonical `TypeID` used to compare types
  across builds and modules. ABIX `TypeID`s are content hashes (a name or an ABI
  descriptor), so they are stable; a region index is not.

Mutability is a **separate qualifier**, not part of the base element identity:
the pointer's pointee records whether the data is const, and the contract matcher
checks access rights independently.

A matching element `TypeID` is necessary but **not sufficient** for `direct`: the
matcher also checks the element's layout/size/alignment, the data-pointer and
length representation, mutability, and the target calling ABI. Type and layout
are reported as distinct reasons (`element_mismatch` vs `layout`) so `amc diff`
can separate "element type changed" from "layout changed".

A pointer-then-length record at the canonical offsets (pointer at 0, length at
pointer width, size `2 * pointer_width`) is `direct`; the same two fields in the
reversed order are `needs_adapter` and are lowered to the canonical view;
anything else is rejected. `direct` means the mapping has been **proved**, not
optimistically assumed — a `const float *` + length record does not match
`SequenceView<i32>` merely because it has two fields.

The result also records a **reason** (for example `element_mismatch`,
`field_count`, `layout`) so tools can explain the decision rather than only
reporting "incompatible".

The lowering result is recorded in the `.abix` (an optional section) and is
readable at run time; a target backend uses it to emit the adapter.

A container whose layout is not a sequence view (for example a `std::vector<T>`
with three pointers) is **not** a contract of its own. AMC lowers it to a
`SequenceView<T>` through declared **accessor functions** (`data()`/`size()`),
or uses `Opaque`/a copy when the layout, element type, borrow lifetime or
invalidation conditions cannot be proven. The container's internal layout is
never exposed; the generated adapter calls the accessors.

**Lowering is a conversion decision** derived from concrete ABI facts and the
target contract. It is **not** part of type identity and does **not** by itself
prove ABI compatibility:

- instantiation answers *what concrete entity the source language produced*;
- the ABI description answers *its actual binary representation and call rules*;
- lowering answers *how that entity satisfies a target contract*;
- runtime verification answers *whether the loaded artifact meets the declared
  conditions*.

Two types with different layouts (`abix_view_rev_i32` and `abix_span_i32`) may
satisfy the same `SequenceView<i32>` contract through an adapter; they are not
the same concrete ABI type. A lowering record lets a reader discover that an
adapter is required, but `status = needs_adapter` alone must never trigger a
runtime conversion: the adapter must be really generated, linked, and checked at
build time.

Identity is layered and MUST NOT be conflated:

| Identity                   | Describes                                            |
| -------------------------- | ---------------------------------------------------- |
| Instantiation request      | template + canonical arguments + target + frontend   |
| Concrete ABI identity      | layout + calling ABI + target (layout/`LayoutHash`)  |
| Export symbol name         | a link/diagnostic name; not an identity              |

Ownership and other contracts are validated separately from layout identity.

Not every instantiation is exported. Instantiating a type, recording it in
`.abix`, and producing an export symbol are independent steps: an instantiation
used only inside a module does not need a symbol, and a type may be recorded
without an exported wrapper. Only entities that actually cross a module boundary
need generated symbols or call adapters. In practice the C++ frontend records a
typedef alias of an instantiation (for example `abix_span_i32`) as the concrete
ABI entity when a selected function signature references it, so a contract type
does not have to appear in the export symbol list.

Instantiation requests fall into three classes, handled differently:

- **A — pure type parameters** (`span<T>`, `vector<T>`): support primitive,
  pointer and known-ABIX combinations first, checking element representation and
  constraints.
- **B — value parameters / conditional instantiation** (`array<T, N>`, `const`
  generic parameters): the frontend evaluates the value first and records its
  effect on layout.
- **C — language constraints and specialization** (C++ concepts, template
  specialization, Rust trait bounds, Zig `comptime`): decided entirely by the
  source frontend; AMC does not re-implement these rules.

An instantiation request's identity (for caching) includes the template, the
canonical type/value arguments, the target configuration and the frontend
configuration; it may also depend on macros, constraints, specialization choice
and compile options, so it is not a function of the template name and arguments
alone. A cache hit does not by itself prove that an existing artifact is still
ABI-compatible with the current target.

When a concrete type matches no ABIX-STD contract, AMC does not force an ABI: it
requires an explicit mapping, uses an `Opaque` handle, or rejects the
instantiation.
