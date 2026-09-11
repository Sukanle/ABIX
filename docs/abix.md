# .abix — Canonical ABI Artifact

## Positioning

`.abix` is the core artifact of the ABIX system — a **language-agnostic, compiler-agnostic, C++ ABI-agnostic normalized ABI metadata container**.

```
.abic = Intent / Policy    (configuration/intent)
.abix = Fact / Artifact    (facts/artifact)
```

`.abix` can be understood as:

- **ABI Object File** — analogous to `.o` / `.obj`, but describes ABI rather than machine code
- **ABI IR** — analogous to LLVM IR, but describes ABI layout/compatibility/mapping rather than control flow
- **ABI Bytecode** — can be directly consumed by Runtime via `mmap`, or lowered by AMC to compile-time code

---

## Core Responsibilities

| Responsibility | Description |
|------|------|
| Record ABI facts | Real state of types, layouts, fields, functions, symbols |
| Record ABI IR | Intermediate representation of Map operations (language-agnostic) |
| Record compatibility | LayoutHash / SignatureHash / Compatibility |
| Support mmap | Binary format, zero deserialization, direct access |
| Support projection | Can be projected by AMC to C++ / Rust / Zig and other target code |

---

## Format: Binary

`.abix` is intended for **Runtime / AMC / Compiler**, therefore it uses a binary format.

### Design Principles

| Principle | Description |
|------|------|
| Fixed-width | `u8` / `u16` / `u32` / `u64` / `i8` / `i16` / ..., never uses `sizeof(long)` / `sizeof(void*)` |
| No raw pointers | Uses `TypeId` / `Offset` / `Index` for references, not pointers |
| Offset-based | All tables are accessed via offsets/indices |
| Little endian | Uniform byte order |
| Compact | Optimized for cache locality / compactness / sequential access / random access |
| mmap-friendly | Can directly `mmap("foo.abix")` then access Header → Section → Record |
| No redundancy | Does not store information deducible from other tables |

### "No deserialization" ≠ "Zero runtime cost"

After `mmap`, there may still be: page faults / cache misses / bounds checks / hash lookups / pointer chasing. Therefore, `.abix` optimizes its layout for access patterns.

---

## Overall Structure

```
.abix
├── Header
├── Section Directory
├── ABI Identity
├── String Table
├── Type Table
├── Layout Table
├── Field Table
├── Function Table
├── Parameter Table
├── Symbol Table
├── Compatibility Table
├── Map Table
├── Map Operation Table
├── Dependency Table
├── Hash Cache (optional)
└── Extensions
```

**Streamlining notes** (compared to initial design):

| Removed/Changed | Reason |
|-----------|------|
| `Target` section | Merged into `ABI Identity`, unified responsibility as "compilation environment of this artifact" |
| `Hash Table` | Current AMC v4 required section, stores HashDescriptor and canonical hash records |
| `Compatibility Table` `source/target_layout_hash` | Derived from source/target TypeId → Type Table → Layout Table |
| Type Table `field_begin` / `field_count` | Obtained from `layout_index` via Layout Table |
| Symbol Table `target_hash` | Changed to `(target_kind, target_index)` typed index |
| Parameter Table `position` | Derived from array order |
| Field Table `size` | Default derived from Layout corresponding to field TypeId (only retained for special cases like arrays/opaque) |

---

## Section Details

### Header

```
Offset  Size  Field
0x00    u32   magic              = 0x58494241  ("ABIX")
0x04    u16   format_version     = 3
0x06    u16   hash_algorithm     = 0 (AMC Core development Hash128)
                                      | 1 (xxh3) | 2 (blake3)
0x08    u32   flags
0x0C    u32   section_count      ← Number of Section Directory entries
0x10    u32   section_dir_offset ← Section Directory offset
```

**Hash algorithm is explicitly recorded in the Header**, rather than being implicit in the implementation. This is a key design for `.abix` as a stable ABI artifact.

A Section Directory replaces fixed `*_offset` fields, avoiding Header layout changes when adding new sections.

### Current AMC v4 Implementation Profile

The current development version of `amc-core` implements and only reads `format_version = 4`. Earlier v1/v2/v3 artifacts are actively deprecated during devel and no compatibility reads are provided. v4 uses the Header and Section Directory described in this section. The current required section IDs are: `1 Strings`, `2 Identity`, `13 Target`, `3 Types`, `4 Fields`, `5 Functions`, `6 Parameters`, `7 Symbols`, `8 HashDescriptor`, `9 HashTable`.

All implemented string references use `(offset, length)`; Identity records also carry package name and package version as such references. Readers will reject incorrect directory ranges, incorrect required entry sizes, out-of-bounds string references, invalid TypeKind/SymbolKind, and artifacts that violate Core layout/type reference invariants. The Hash Table is implemented as part of the current required profile. Compatibility, Map IR, Dependency, and optional sections remain for future expansion.

Directory entries use `{section_id, offset, byte_length, count, entry_size, flags}`. `flags.required` requires the reader to understand that section; unknown optional sections can be safely skipped. Compatibility, Map, and MapOperation are currently optional sections. Hash Cache sections remain a future extension.

Runtime integration uses the fixed-layout descriptors from `abix/runtime_descriptor.h`. MICS Runtime's `TypeId` and ABIX `model::TypeId` are both full Hash128; cross-namespace conversion must go through explicit `abix/mics_bridge.h` functions; truncating ABI identity to a single `uint64_t` is forbidden.

`abix/runtime_registry.h` provides a `RuntimeRegistry<Capacity>` bridge. It receives the generated `ModuleDescriptor`, performs module-level integrity checks, duplicate TypeId checks, and field/function type reference checks before writing; failure leaves no partial registration results. On success, it retains both runtime descriptor pointers and canonical `MetadataRegistry` entries, queryable by either Hash128 or name.

### Section Directory

```
Offset  Size  Field             (per entry)
0x00    u32   section_id        = 1 (string_table) | 2 (abi_identity) | 3 (type_table)
                            | 4 (field_table) | 5 (function_table) | 6 (parameter_table)
                            | 7 (symbol_table) | 8 (map_table) | 9 (map_op_table)
                            | 10 (dependency_table) | 11 (hash_cache) | ...
0x04    u32   offset            ← Section offset within the file
0x08    u32   byte_length       ← Total byte length of the section
0x0C    u32   count             ← Number of records in this section
0x10    u32   entry_size        ← Fixed record size; 0 for variable-length sections
0x14    u32   flags             ← bit 0: required
```

### ABI Identity

Merges the original `Target` section to uniformly describe the compilation environment of this artifact:

```
Offset  Size  Field
0x00    u32   arch               = 0 (x86_64) | 1 (aarch64) | 2 (riscv64) | ...
0x04    u32   os                 = 0 (linux) | 1 (windows) | 2 (macos) | ...
0x08    u32   compiler           = 0 (gcc) | 1 (clang) | 2 (msvc) | ...
0x0C    u32   calling_convention = 0 (sysv_abi) | 1 (ms_abi) | 2 (aapcs) | ...
0x10    u32   abi_flags
0x14    u64   artifact_identity_hash_lo  ← Currently package identity hash; Canonical ABIHash to be implemented later
0x1C    u64   artifact_identity_hash_hi
0x24    u32   package_name_offset
0x28    u32   package_name_length
0x2C    u32   package_version_offset
0x30    u32   package_version_length
```

The future Canonical ABIHash will be defined as `hash(all ABI Identity fields + all Type/Layout/Function/Symbol)`, using Hash128. The current v2 profile stores the full canonical ABIHash in this field.

### String Table

```
Offset  Size  Field
0x00    u32   count
0x04    u32   total_bytes
0x08    u8[]  data               ← Contiguous strings, \0-separated
```

All names reference substrings in the String Table via `(offset, length)`, avoiding duplicate storage.

### Type Table

```
Offset  Size  Field             (per record)
0x00    u64   type_hash_lo      ← TypeId low 64 bits
0x08    u64   type_hash_hi      ← TypeId high 64 bits
0x10    u64   layout_hash_lo    ← LayoutHash low 64 bits
0x18    u64   layout_hash_hi    ← LayoutHash high 64 bits
0x20    u32   name_offset       ← String Table offset
0x24    u32   name_length
0x28    u32   kind
0x2C    u32   flags
0x30    u32   size
0x34    u32   align
0x38    u32   field_begin
0x3C    u32   field_count
0x40    u32   array_count
```

The current AMC v2 profile Type record is 68 bytes, carrying LayoutHash, layout size, and field range directly. A standalone Layout Table and multi-inheritance Base Table are reserved for future schema expansion.

The current AMC devel profile adds a Hash128 `owner_type` before Field records for member-level projection, making Field records 48 bytes; similarly adds Hash128 `owner_type` before Function records, making Function records 72 bytes. Free functions use a zero-value owner.

**Multi-inheritance support**: `base_type_index` becomes `base_begin` + `base_count`, pointing to a Base Table (or a base class index array inlined at the end of the Type Table).

### Layout Table

```
Offset  Size  Field             (per record)
0x00    u64   layout_hash_lo    ← LayoutHash low 64 bits
0x08    u64   layout_hash_hi    ← LayoutHash high 64 bits
0x10    u32   size              ← sizeof
0x14    u32   alignment         ← alignof
0x18    u32   field_begin       ← Starting index into Field Table
0x1C    u32   field_count
0x20    u32   padding           ← Number of internal padding bytes
0x24    u32   vtable_offset     ← vtable offset (UINT32_MAX = no vtable)
0x28    u64   base_layout_hash_lo ← Primary base class LayoutHash low 64 bits (0 = no base class)
0x30    u64   base_layout_hash_hi ← Primary base class LayoutHash high 64 bits
```

Field range information **is stored only once in the Layout Table**; Type Table references it via `layout_index`.

### Field Table

```
Offset  Size  Field             (per record)
0x00    u32   name_offset
0x04    u32   name_length
0x08    u64   type_hash_lo      ← Field type TypeId low 64 bits
0x10    u64   type_hash_hi      ← Field type TypeId high 64 bits
0x18    u32   offset            ← offsetof
0x1C    u32   flags             ← static / mutable / bitfield / array / opaque / ...
0x20    u32   bitfield_width    ← Bitfield width (0 = non-bitfield)
0x24    u32   bitfield_offset   ← Bitfield offset
0x28    u32   array_count       ← Array element count (0 = non-array)
0x2C    u32   explicit_size     ← Only valid when flags contain opaque/array, otherwise 0
```

**Streamlined**: Removed `size` field. Regular field size is derived from TypeId → Layout Table. Only special cases like `opaque` / `array` store size via `explicit_size`.

AMC's `owner_type` enables `Foo::field` selection without relying on member name guessing.

### Function Table

```
Offset  Size  Field             (per record)
0x00    u32   name_offset
0x04    u32   name_length
0x08    u64   signature_hash_lo ← SignatureHash low 64 bits
0x10    u64   signature_hash_hi ← SignatureHash high 64 bits
0x18    u64   return_type_hash_lo ← Return type TypeId low 64 bits
0x20    u64   return_type_hash_hi ← Return type TypeId high 64 bits
0x28    u32   param_begin       ← Starting index into Parameter Table
0x2C    u32   param_count
0x30    u32   calling_convention
0x34    u32   flags             ← static / virtual / const / noexcept / ...
0x38    u32   vtable_index      ← vtable index (UINT32_MAX = non-virtual)
```

The current AMC profile adds a Hash128 `owner_type` before the above fields. A zero value indicates a namespace/free function; selecting `Foo` retains all members; selecting `Foo::method` retains only the specified method.

The AMC-M8 frontend currently stores namespaces and typedefs/aliases as standalone Type records (kind `namespace` / `alias`). Field flags use `field_bitfield`, `field_base`, visibility bitfields, and bit offset/width bitfields to record bitfield, inheritance, and access levels; concrete template specializations are recorded by their instantiated type; dependent template bodies are not emitted as layoutable ABI outputs. Function flags record access levels; `calling_convention` uses stable AMC numbering (C, stdcall, fastcall, thiscall, aarch64 SVE).

### Parameter Table

```
Offset  Size  Field             (per record)
0x00    u32   name_offset
0x04    u32   name_length
0x08    u64   type_hash_lo      ← Parameter type TypeId low 64 bits
0x10    u64   type_hash_hi      ← Parameter type TypeId high 64 bits
0x18    u32   flags             ← in / out / inout / default / ...
```

**Streamlined**: Removed `position`; parameter position is derived from array order.

### Symbol Table

```
Offset  Size  Field             (per record)
0x00    u32   name_offset       ← Linker symbol name
0x04    u32   name_length
0x08    u32   mangled_offset    ← Mangled name offset
0x0C    u32   mangled_length
0x10    u32   target_kind       = 0 (type) | 1 (field) | 2 (function)
0x14    u32   target_index      ← Index into the corresponding table
0x18    u32   visibility        = 0 (public) | 1 (protected) | 2 (private)
0x1C    u32   binding           = 0 (local) | 1 (global) | 2 (weak)
```

**Streamlined**: `target_hash` changed to `(target_kind, target_index)` typed index. Storing both hash and index is redundant — index is faster and collision-free.

### Compatibility Table

```
Offset  Size  Field             (per record)
0x00    u64   source_type_hash_lo
0x08    u64   source_type_hash_hi
0x10    u64   target_type_hash_lo
0x18    u64   target_type_hash_hi
0x20    u32   compatibility     = 0 (identical) | 1 (layout_compatible)
                            | 2 (map_compatible) | 3 (incompatible)
0x24    u32   map_index         ← Index into Map Table (UINT32_MAX = no mapping)
0x28    u32   flags
```

**Streamlined**: Removed `source_layout_hash` / `target_layout_hash`. LayoutHash is derived from source/target TypeId → Type Table → Layout Table, no redundant storage needed.

### Map Table

```
Offset  Size  Field             (per record)
0x00    u64   source_type_hash_lo
0x08    u64   source_type_hash_hi
0x10    u64   target_type_hash_lo
0x18    u64   target_type_hash_hi
0x20    u32   source_name_offset
0x24    u32   source_name_length
0x28    u32   target_name_offset
0x2C    u32   target_name_length
0x30    u32   operation_begin   ← Starting index into Map Operation Table
0x34    u32   operation_count
0x38    u32   flags             ← bidirectional / lossy / ...
```

The current AMC v2 Map record is 60 bytes. Compatibility reports can be stored across two artifacts, so Map records store stable names for both source and target, avoiding the requirement that the source TypeId must exist in the target artifact's Type Table.

### Map Operation Table (ABI IR)

`.abix` stores **Map IR** (language-agnostic opcodes), not C++ code:

| Opcode | Name | source_idx | target_idx | auxiliary | Description |
|--------|------|:----------:|:----------:|:---------:|------|
| 0x00 | `COPY_FIELD` | ✓ | ✓ | — | Copy field by offset |
| 0x01 | `CONVERT_INT` | ✓ | ✓ | ✓ | Integer type conversion, aux = target type TypeId |
| 0x02 | `CONVERT_FLOAT` | ✓ | ✓ | ✓ | Float type conversion, aux = target type TypeId |
| 0x03 | `DEFAULT_FIELD` | — | ✓ | ✓ | Fill with default value, aux = default value encoding |
| 0x04 | `RENAME_FIELD` | ✓ | ✓ | — | Field rename |
| 0x05 | `PTR_REINTERPRET` | ✓ | ✓ | ✓ | Pointer reinterpretation, aux = target TypeId |
| 0x06 | `CALL_CONVERTER` | ✓ | ✓ | ✓ | Call converter, aux = converter function SignatureHash |
| 0x07 | `SKIP_FIELD` | ✓ | — | — | Skip field (source has it, target doesn't) |
| 0x08 | `ADD_DEFAULT` | — | ✓ | ✓ | Add default field, aux = default value encoding |
| 0x09 | `NESTED_MAP` | ✓ | ✓ | ✓ | Recursive mapping, aux = nested Map Table index |

**Opcode-specific payload**: Different opcodes actually require different fields. Invalid fields are set to `UINT32_MAX` (sentinel), rather than wasting space storing useless data.

Each Map Operation encoding:

```
Offset  Size  Field
0x00    u8    opcode
0x01    u8    flags             ← Reserved
0x02    u16   reserved
0x04    u32   source_field_index ← UINT32_MAX = not used
0x08    u32   target_field_index ← UINT32_MAX = not used
0x0C    u64   auxiliary_lo      ← Auxiliary data low 64 bits
0x14    u64   auxiliary_hi      ← Auxiliary data high 64 bits
```

The Runtime must verify source/target Layout, field indices, and `offset + copy_size <= layout.size` before executing a Map; partial writes must not be performed on verification failure. The current core executor supports `COPY_FIELD`, `RENAME_FIELD`, `DEFAULT_FIELD`, `ADD_DEFAULT`, and `SKIP_FIELD`; `CONVERT_INT`, `CONVERT_FLOAT`, and `CALL_CONVERTER` are executed via registered noexcept Converter callbacks. RuntimeKey or any standalone hash cannot replace these bounds and type checks.

### Dependency Table

```
Offset  Size  Field             (per record)
0x00    u64   dependency_hash_lo ← ABIHash of the dependency .abix low 64 bits
0x08    u64   dependency_hash_hi ← ABIHash of the dependency .abix high 64 bits
0x10    u32   name_offset
0x14    u32   name_length
0x18    u32   version_min
0x1C    u32   version_max
```

### Hash Cache (optional)

```
Offset  Size  Field             (per record)
0x00    u64   hash_lo           ← TypeId / SignatureHash low 64 bits
0x08    u64   hash_hi           ← TypeId / SignatureHash high 64 bits
0x10    u32   table_kind        = 0 (type) | 1 (function) | 2 (symbol)
0x14    u32   index             ← Index into the corresponding table
```

**Non-canonical data**. This is a rebuildable index of the main tables, used for fast Runtime lookup. It can be built on demand by the Runtime, or pre-generated as an optional cache section in .abix.

### Extensions

Reserved area for future extension. Section Directory `section_id = 255` points to this area.

---

## Type / Layout / Function / Symbol Separation

This is an important principle of `.abix` design:

| Concept | Question Answered | Example |
|------|-----------|------|
| **Type** | What type is this? | `Foo` |
| **Layout** | What does it look like at the ABI level? | `size=32, align=8, field[a@0, b@8, c@24]` |
| **Function** | What is the logical function signature? | `Foo* foo_create(i32, u64)` |
| **Symbol** | Which linker symbol does it correspond to? | `_Z11foo_createim` |

**Type ≠ Layout**: The same Type can have different Layouts on different platforms.

**Function ≠ Symbol**: The same Function can have different mangled Symbols with different compilers.

This avoids mixing C++/Rust/compiler-specific implementations into the `.abix` core model.

---

## Hash System

### Hash Width and Compatibility Layer

```cpp
struct Hash128 {
    uint64_t lo;
    uint64_t hi;
};
```

`.abix` canonical metadata uses Hash128; existing ABIX DLL tables and MICS runtime interfaces continue to use 64-bit hashes to maintain existing binary compatibility:

| Hash Type | Width | Description |
|----------|------|------|
| `TypeId` | Hash128 | Canonical type identity in `.abix` |
| `LayoutHash` | Hash128 | Full layout description |
| `SignatureHash` | Hash128 | Canonical function signature in `.abix`; existing DLL function tables still use Hash64 |
| `ABIHash` | Hash128 | Entire ABI artifact |

The `type_hash` / `sig_t` in existing runtime interfaces remain `uint64_t`. The first-generation `.abix` uses Hash128 for TypeId, LayoutHash, SignatureHash and ABIHash; the algorithm can be replaced with XXH3 / BLAKE3 / SHA-256 truncated, **without changing the `.abix` model**. The hash algorithm is identified by the `hash_algorithm` field in the Header.

Implementations must use the canonical definition of the algorithm. The dual-lane FNV value in the current C++ reference code is for testing and transition only, using a separate algorithm ID; it must not be labeled as standard FNV1A-128.

Hashes are only used for fast identity location, not as proof of content correctness. When a loader finds the same ID, it should continue to verify canonical name, layout, or signature to avoid silent errors from collisions.

### HashDescriptor and Hash Domain

The current AMC v2 uses a fixed HashDescriptor section (ID `8`, 16 bytes):

```text
u16 algorithm              = 0 (AMC Hash128)
u16 algorithm_version      = 1
u32 canonical_version      = 1
u32 flags
u32 reserved
```

The Hash Table section (ID `9`) uses 32-byte records:

```text
u32 hash_kind               = 0 artifact | 1 type_id | 2 layout | 3 signature
u32 target_kind             = 0 module | 1 type | 2 function
u32 target_index
u32 flags
u64 hash_lo
u64 hash_hi
```

The artifact hash in Identity is `ABIHash`. It comes from an independent canonical encoding that does not include section offsets, directory offsets, padding, or the physical arrangement of the String Table. Changes in canonical version or hash algorithm both change the ABIHash; package name/version do not participate in the ABIHash.

Each canonical hash value must carry the following description:

```cpp
struct HashDescriptor {
    Algorithm algorithm;
    uint16_t algorithm_version;
    uint16_t digest_bits;
    HashDomain domain;
};
```

`TypeId`, `LayoutHash`, `SignatureHash`, and `ABIHash` use different domains; even with identical input bytes, they cannot be treated as the same hash. Algorithm, version, bit width, and domain collectively determine the hash semantics.

A single `.abix` can carry multiple hash representations, e.g., legacy FNV1A-64 and modern BLAKE3-128. The Runtime chooses which representation it supports; the canonical value, once written into the artifact, cannot be changed at runtime.

The Runtime can generate a 64-bit `RuntimeKey` from a full Hash128 for HashIndex lookups, but `RuntimeKey` is only used to locate candidates; the final match must compare the full digest and descriptor.

User-defined algorithms use reserved algorithm IDs and can only be verified if the Runtime explicitly registers and supports that algorithm. Artifacts with unknown algorithms should report unsupported, and must not silently fall back to another algorithm.

### TypeId (Type Identity)

```cpp
TypeId = hash(canonical type identity)
```

Answers: **Is this the same type?**

### LayoutHash (Layout Hash)

```cpp
LayoutHash = hash(
    TypeId,
    size,
    alignment,
    field_count,
    field TypeId[],
    field offset[],
    field bitfield[],
    base_class LayoutHash[],
    vtable ABI,
    ...
)
```

Answers: **Is the memory layout compatible?**

### SignatureHash (Signature Hash)

```cpp
SignatureHash = hash(
    return TypeId,
    parameter TypeId[],
    calling_convention,
    qualifiers
)
```

Answers: **Is the function call compatible?**

### ABIHash (Overall Hash)

```cpp
ABIHash = hash(
    ABI Identity,
    all TypeId[],
    all LayoutHash[],
    all SignatureHash[],
    all Symbol[]
)
```

Answers: **Is the entire ABI identical?**

### TypeHash ≠ LayoutHash

```cpp
struct A { int a; double b; };   // TypeHash_A, LayoutHash_A
struct B { double b; int a; };   // TypeHash_B, LayoutHash_B

// TypeHash_A ≠ TypeHash_B  (different types)
// LayoutHash_A ≠ LayoutHash_B  (different layouts)
// But sizeof(A) == sizeof(B) && alignof(A) == alignof(B)  (size/align alone is insufficient!)
```

**Distinguishing TypeHash and LayoutHash is fundamental to the ABIX compatibility system.**

---

## Three Consumption Modes

### Mode A: Runtime (Dynamic)

```
.abix
  ↓
mmap
  ↓
ABIX Runtime
  ↓
Registry
  ↓
Dynamic query
```

Applicable scenarios: dynamic libraries, plugins, dynamic ABI, unknown types, runtime compatibility checks.

Supports two loading modes:

| Mode | Description |
|------|------|
| **Lazy mmap View** | Lazy loading, on-demand page faults |
| **Eager Runtime Registry** | One-time full load into Registry, fully available |

### Mode B: Static (Compile-time)

```
.abix
  ↓
AMC
  ↓
foo.abix.hpp
  ↓
constexpr
  ↓
Compiler
  ↓
inline
```

Example generated output:

```cpp
constexpr auto Foo_TypeId    = TypeId{0x...};
constexpr auto Foo_Layout    = TypeLayout{.size=32, .align=8, ...};
constexpr auto Foo_FieldA    = 0;   // offsetof(Foo, a)
constexpr auto Foo_FieldB    = 8;   // offsetof(Foo, b)

// MapPrivate can be fully optimized away by the compiler
template<>
struct MapPrivate<FooV1, FooV2> {
    static FooV2 map(const FooV1& src) {
        FooV2 dst;
        dst.a = src.a;              // COPY_FIELD
        dst.b = static_cast<i64>(src.b);  // CONVERT_INT
        dst.c = 0;                  // DEFAULT_FIELD
        return dst;
    }
};
```

End result: `ABI metadata → compile-time → direct offset/load/store`.

### Mode C: Hybrid (Recommended)

```
                  .abix
                   │
        ┌──────────┴──────────┐
        ↓                     ↓
 Known ABI              Unknown ABI
        ↓                     ↓
 Compile-time            Runtime
 MapPrivate              Registry
        ↓                     ↓
 zero/low overhead       dynamic compatibility
```

**Static when possible, Dynamic when necessary.**

---

## Relationship with .abic

```
                 ┌───────────────┐
                 │    .abic      │
                 │ Intent/Policy │
                 └───────┬───────┘
                         │
                        AMC
                         │
                         ▼
                 ┌───────────────┐
                 │    .abix      │
                 │ ABI Artifact  │
                 │   + ABI IR    │
                 └───────────────┘
```

| Property | `.abic` | `.abix` |
|------|---------|---------|
| Positioning | Configuration/Intent | Facts/Artifact |
| Format | TOML (human-readable) | Binary (machine-readable) |
| Consumer | AMC (build-time) | Runtime / AMC / Compiler |
| Runtime dependency | None | Yes (in Runtime mode) |
| Source of Truth | No | **Yes** |

---

## Projection System

`.abix` is the Source of Truth; all target code is its projection:

```
foo.abix
  ↓
  ├── Runtime projection  → mmap / Registry → Dynamic ABI
  ├── C++ projection      → foo.abix.hpp    → Static ABI (constexpr)
  ├── Rust projection     → foo.abix.rs     → Static ABI
  ├── Zig projection      → foo.abix.zig    → Static ABI
  └── Debug projection    → foo.abix.txt    → Human-readable
```

**`.abix.hpp` should not become a new "ABI source file"**, but rather a compile-time projection of `.abix`.

---

## Differences from Traditional Approaches

| Approach | Essence | ABIX Difference |
|------|------|------------|
| Reflection | Runtime type introspection | ABIX includes compatibility/mapping IR, not just type information |
| Serialization | Data encode/decode | ABIX focuses on ABI layout compatibility, not data format |
| RPC | Remote procedure call | ABIX focuses on local cross-DLL boundaries, not networking |
| IDL | Interface description language | ABIX extracts from actual compiled artifacts, not hand-written interfaces |
| Protobuf/FlatBuffers | Data schema | ABIX describes memory layout ABI, not serialization schema |

ABIX is an **ABI intermediate layer positioned between source code/compiler and ABI Runtime**.

---

## Core Concepts

| Keyword | Description |
|--------|------|
| **Canonical** | ABI information from different compilers/languages/platforms is first normalized |
| **Declarative** | `.abix` describes "what it is", not "how to write C++ code" |
| **Dynamic** | Runtime can load dynamically; both sides' ABI don't need to be fully fixed at compile time |
| **Static** | Known ABIs can be lowered by AMC to constexpr / MapPrivate / inline |
| **Self-hosting** | ABIX itself uses ABIX: `abix.abix` → ABIX Runtime → reads its own ABI |

---

## Complete File Relationship

```
                      Project
                         │
                         ▼
                      xxx.abic
                    Configuration
                         │
                         │ AMC
                         ▼
                      xxx.abix
               Canonical ABI Artifact
                         │
           ┌─────────────┼─────────────┐
           │             │             │
           ▼             ▼             ▼
        Runtime        C++          Rust/Zig
         mmap        .abix.hpp       .abix.rs
           │             │             │
           ▼             ▼             ▼
       Dynamic ABI   Static ABI     Static ABI
```

**One-sentence definition:**

> `.abix` is a normalized binary fact and ABI IR describing "what the ABI actually is"; the same `.abix` can serve as both the Runtime's dynamic ABI database and be lowered by AMC into nearly zero-overhead compile-time ABI code, thus unifying "dynamic ABI" with "static performance".