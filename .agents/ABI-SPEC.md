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

* the ABI semantic model (types, layouts, functions, symbols, modules);
* the identities used to compare and locate ABI facts;
* the compatibility rules;
* the canonical representation and its encoding rules;
* versioning and extension rules.

It does not define a language, an object model, an execution engine, or a
debug-information format.

## 2. Terminology

| Term | Meaning |
|------|---------|
| **Type** | a native type as seen at the ABI: primitive, enum, record, pointer, array, function, namespace, alias |
| **Field** | a named, offset-addressable member of a record |
| **Layout** | size, alignment and field placement of a type |
| **Function** | a callable ABI entity with a signature and calling convention |
| **Parameter** | an ordered function input |
| **Symbol** | a named entry point mapping to a type, field or function |
| **Module** | a set of ABI facts for one artifact |
| **Artifact** | the serialized form of a module (`.abix`) |

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

* `TypeID` is a 128-bit value identifying a type semantically.
* A `TypeID` is **name-independent at comparison time**: two modules may spell a
  type differently and still agree, and a name is never used to decide
  compatibility.
* `TypeID` must be stable across builds, machines and translation units for the
  same semantic type.
* A zero `TypeID` is invalid.
* Two distinct types must not share a `TypeID` within one module.

The derivation of a `TypeID` depends on how much of the type's semantics is
determined by its ABI:

* **Scalar / primitive types** are identified by an **ABI descriptor**:
  kind, size, alignment, and — where they change the ABI — signedness
  (integers, and the implementation-defined `char` / `wchar_t`) or
  floating-point format (IEEE binary16/32/64/128, x87 80-bit, PowerPC
  double-double, ...). The source spelling is excluded, so `int` / `int32_t`,
  `long` / `long long` (LP64), and a 64-bit C `double` / Rust `f64` on the same
  target share one `TypeID`. `bool`, and each character type — `char`,
  `signed char`, `unsigned char`, `char8_t`, `char16_t`, `char32_t`,
  `wchar_t` — is a distinct ABI kind; plain `char` and `wchar_t` carry their
  target-defined signedness, so a signed and an unsigned `char` never merge.
* **Nominal types** (`record`, `enumeration`, `alias`, `namespace_type`) are
  identified by their canonical qualified name. Two distinct nominal types must
  not be merged merely because their layouts happen to match; physical
  equivalence is expressed through `LayoutHash` (§5) instead.

A primitive and a nominal type may still be ABI-compatible; that is decided by
`LayoutHash` and the compatibility rules (§12), never by the spelling.

## 5. Layout Identity (LayoutHash)

* `LayoutHash` is a 128-bit value identifying the **physical layout** of a type.
* It is derived from the type's size, alignment and its fields' identity,
  offsets and order.
* Two types with the same `TypeID` and the same `LayoutHash` are layout
  compatible.
* Same `TypeID`, different `LayoutHash` is an **ABI conflict**.
* `LayoutHash` must be **independent of the source spelling**: two ABI-equal
  types declared under different names must hash to the same layout. In
  particular, a primitive's layout is a function of its ABI descriptor (§4), not
  of its name, so a C `double` and a Rust `f64` of the same target share a
  `LayoutHash`.
* A concrete type with size or alignment zero is invalid.

## 6. Function ABI

A function record carries:

* `owner_type` (zero for a free/namespace function);
* `signature` (`SignatureHash`);
* `return_type` (`TypeID`);
* ordered `parameters[]` with their `TypeID`s;
* `calling_convention`;
* flags.

A function is identified at the ABI by its signature, not its name. Parameter
order is significant. A function whose return type is zero is invalid.

## 7. Symbol Identity

* A `Symbol` maps a name to a type, field or function by index.
* Symbols are a **name → ABI fact** index for humans and tools.
* Symbols are not ABI identity and must not participate in compatibility
  decisions.

## 8. Calling Convention

* The calling convention is part of the function ABI and must match for two
  functions to be interchangeable.
* `0` means unspecified/default; concrete frontends assign the language's
  convention code.
* A calling-convention mismatch is incompatible, even when layouts match.

## 9. Ownership

Ownership semantics are **not yet part of the ABIX ABI model**. Where a language
expresses ownership (for example C++ RAII), a frontend may encode it as flags,
but it must not be assumed by the core model. Reserved for a future revision;
until then, ownership differences are not detected by compatibility checks.

## 10. Target

A module records `arch`, `os`, `target_abi`, `compiler` and the module-level
`calling_convention`. ABI facts are only comparable when their targets are
compatible; tooling must report target differences rather than silently
comparing them.

A module describes **one** target, and target differences are resolved by
generating one artifact per target (a cross-compiled `.abix`), rather than by
putting unresolved, target-independent types into a single portable artifact.
The core model therefore stores the concrete ABI fact (width, alignment,
floating-point format) for that target. Source-language identity — how a
language spelled the type (`c_long`, `i32`) — is not an ABIX core type; it may be
retained as optional provenance that never participates in identity. See
[`docs/development/type-model.md`](../docs/development/type-model.md).

## 11. Canonical Representation

* The canonical form is deterministic: the same inputs produce byte-identical
  output.
* Hashing domains and canonical encoding are part of this specification;
  changing them invalidates every existing artifact.
* Primitive `TypeID` values are derived from ABI descriptors (§4) and are
  deliberately spelling-independent. Introducing the descriptor form changes
  the `TypeID` of every primitive, so all artifacts produced before this
  revision are invalid and must be regenerated.
* `ABIHash` is the module-level artifact identity, computed over the canonical
  content (identity, types, functions, maps). It excludes diagnostic-only data.
* `SignatureHash` identifies a function signature.
* **Source origin is not ABI identity**: the optional debug `sources` section is
  excluded from `ABIHash`.

## 12. Compatibility Rules

Comparing a source ABI to a target ABI classifies each type. A source type is
paired with a target type by canonical name for nominal types, falling back to
`TypeID` for primitives (so ABI-equal scalars line up across spellings).

### Compatible

* `identical` — same `TypeID`, same `LayoutHash`.
* `layout_compatible` — same effective layout.
* `map_compatible` — layout differs, but a field mapping exists and is complete.

### Conditionally compatible

* Mapping-based compatibility is only as safe as the generated map; consumers
  must apply the map rather than reinterpret memory.

### Incompatible

* Same `TypeID`, different `LayoutHash`.
* Size/alignment change, field removal, field type change, parameter or return
  type change, calling-convention change.
* A type required by the source is absent from the target.

Verification (`amc verify`) is stricter than classification: **any** difference,
including *added* types or functions, is drift, because widening the ABI surface
must be an explicit decision.

## 13. Serialization

* Serialized metadata is **pointer-free**: cross references use offsets and
  indices, never absolute addresses.
* The Metadata Region is self-describing: a manifest precedes the records and
  records each section's offset and size.
* A Metadata Region is integrity-checked by recomputing `MetadataID` over
  `desc + hash + names`.
* Artifacts must be mmap-able and parse-able offline without loading the program.

## 14. Versioning

* The `.abix` format carries a format version; readers reject unsupported
  versions rather than guessing.
* The Metadata Region manifest carries major/minor version and a hash algorithm
  id/version.
* `TypeID`/`LayoutHash` are independent of the container version.

### 14.1 Version Independence

ABIX distinguishes between the version of the `.abix` representation, the
semantic ABI specification, and the AMC toolchain.

These version dimensions serve different purposes and MUST NOT be implicitly
coupled.

| Version | Scope | Purpose |
|---------|-------|---------|
| Format Version | `.abix` representation | Defines how ABIX data is encoded and interpreted at the format level |
| Specification Revision / Profile | ABI semantic model | Defines ABI semantics, compatibility rules, and required interpretation |
| AMC Toolchain Version | AMC implementation | Defines available CLI tools, generators, integrations, MCP/Agent capabilities, and other tooling features |

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

* Rust or Zig binding generation
* LSP integration
* MCP tools
* Agent workflows
* package management
* additional inspection or generation capabilities

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

Compatibility is defined by the reader's supported versions and capabilities;
it MUST NOT be assumed to be bidirectional.

## 15. Extension Rules

* New optional sections may be added after the last known section id; readers
  must skip unknown optional sections.
* A new required section, or a change to an existing record layout, is a format
  version change.
* Extensions must not change the meaning of existing records.

## 16. Reserved Fields

* Reserved fields must be written as zero and ignored on read.
* A reader must not rely on reserved bits retaining a value across versions.

## 17. Forward / Backward Compatibility

* **Backward**: a newer reader must read older artifacts by skipping unknown
  optional data and honouring reserved-field rules.
* **Forward**: an older reader must reject required data it does not understand,
  and must not treat an unknown optional section as an error.
* A name-stripped artifact remains valid: names and source origins are
  diagnostic and are excluded from `ABIHash`; `TypeID`/`LayoutHash` are
  unaffected.
