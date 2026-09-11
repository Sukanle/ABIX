# ABIX Metadata Three-Level Modes: Embedded Readable / Full Hash / Stripped File

## Design Background

ABIX runtime type metadata needs to make a trade-off between **diagnostic capability** and **production binary size**.

In early implementations, `TypeDescriptor` carried a `name` pointer (72 bytes), string constants fell into `.rdata`, and each type's metadata was ~120 bytes. This was convenient for development debugging, but for production releases, this overhead becomes a noticeable space cost when accumulated across hundreds of types.

The DWARF/PDB approach is: debug information is a separate section; `strip` can remove it without affecting `.text` and `.data`. ABIX borrows this model but goes further with deeper layout optimization — **not just separating sections, but also adjusting the descriptor layout itself based on the mode**.

## Core Concept

```
┌──────────────────────────────────────────────────────────────┐
│  Names are symbols, not data                                  │
│                                                               │
│  type_id / layout_hash = data (required for runtime contract verification)│
│  name / field name strings   = symbols (only needed for diagnostics)     │
└──────────────────────────────────────────────────────────────┘
```

This mirrors the relationship between `.symtab` and `.strtab` in ELF — the runtime only needs addresses; names are only for debuggers. ABIX does the same thing, just elevated to the ABI metadata layer.

## Three Modes Overview

| Mode | TypeDescriptor | String Location | .abix File | Typical Scenario |
|------|---------------|-----------|-----------|---------|
| **Debug** | 72 B (with `name`) | Embedded `.abix.names` section | Optional generation | Development / step debugging |
| **RelWithDebInfo** | 56 B (no `name`) | Standalone `.abix` archive | Must archive | Staging / crash analysis |
| **Release** | 56 B (no `name`) | None | Archive recommended | Production deployment |

Key properties:
- **RelWithDebInfo and Release binaries are bit-identical**, differing only in whether the `.abix` file is archived
- **Same binary**: debug before strip, release after strip
- **Compile once, two forms**, enabling differential testing

---

## Debug Mode: Embedded Readable

### TypeDescriptor Layout (72 bytes)

```cpp
struct TypeDescriptor {           // sizeof = 72
    const char *name;             //  8 B  ← Points to .abix.names section
    Hash128 type_id;              // 16 B
    LayoutHash layout_hash;       // 16 B
    uint32_t flags;               //  4 B
    uint32_t size;                //  4 B
    uint32_t align;               //  4 B
    const FieldDescriptor *fields; //  8 B
    uint32_t field_count;         //  4 B
    // padding                    //  8 B
};
```

### .abix.names Section

The AMC C++ backend generates a dedicated section containing string constants for all type/field/function names:

```cpp
#ifdef __GNUC__
__attribute__((section(".abix.names")))
#endif
inline constexpr const char *amc_type_names[] = {
    "MyClass",
    "MyStruct",
    "MyEnum",
    // ...
};
```

This section:
- Falls into `.abix.names` on ELF (a custom section)
- **Is not referenced by any runtime code** — the runtime path only uses `type_id`/`layout_hash`
- Can be safely removed by `strip` without affecting program behavior
- Is consumed by external tools like `amc-dump` and `ABIX symbol server`

### Applicable Scenarios

- Local development, where type names are needed during step debugging
- Unit tests, where readable type information aids assertion failures
- Scenarios where binary size is not a concern

---

## Release Mode: Full Hash

### TypeDescriptor Layout (56 bytes)

```cpp
struct TypeDescriptor {           // sizeof = 56
    const FieldDescriptor *fields; //  8 B
    Hash128 type_id;              // 16 B
    LayoutHash layout_hash;       // 16 B
    uint32_t flags;               //  4 B
    uint32_t size;                //  4 B
    uint32_t align;               //  4 B
    uint32_t field_count;         //  4 B
};                                // 56 B, no name, no padding
```

All other descriptors similarly remove the `name` field:

| Structure | Debug Size | Release Size | Savings |
|--------|-----------|-------------|------|
| `TypeDescriptor` | 72 B | 56 B | **22%** |
| `FieldDescriptor` | 32 B | 24 B | **25%** |
| `FunctionDescriptor` | 56 B | 48 B | **14%** |
| `ParameterDescriptor` | 32 B | 24 B | **25%** |
| `SymbolDescriptor` | 24 B | 8 B | **67%** |

> `ModuleDescriptor` retains `name` and `version` because they are not diagnostic names but logical package identifiers needed by module loading and verification paths.

### Runtime Contract: Fully Hash-Based

In Release mode, all runtime queries use only hashes:

```cpp
// Supported in both Debug and Release
const auto *entry = registry.find_by_id(type_id);

// Debug only; returns nullptr in Release
const auto *entry = registry.find_by_name("MyClass");
```

The `type_id` is identical across both build modes because it is the **hash of canonical type identity** — independent of name, layout, and compiler version.

### Constraint: `TypeDescriptor` is Not Part of the ABI Contract

Since the layout varies by build mode, `TypeDescriptor` cannot be a type passed across DLL/so boundaries. It is an in-process structure only. If future use cases require cross-boundary descriptor passing, a fixed layout will be needed.

---

## RelWithDebInfo: Stripped File

### Core Idea

A RelWithDebInfo build produces a binary **bit-identical to Release** (56-byte descriptor, no names), but archives an `.abix` file as a companion artifact.

```
release binary         relwithdebinfo binary         debug binary
     │                       │                            │
     │                  56 B descriptor              72 B descriptor
     │                  no .abix.names                .abix.names section
     │                       │                            │
     ▼                       ▼                            ▼
 production              staging / crash analysis      local development
                             │
                             ▼
                    companion .abix artifact
                    (contains all type/field names)
```

This means:
- **Same binary**: debug before strip, release after strip
- Staging and production run exactly the same binary
- The staging environment archives `.abix` for post-hoc diagnostics
- The production environment does not archive `.abix`, achieving minimal size

### Version Pairing: Hash as the Matching Key

```
Crash stack → extract type_id (Hash128)
            → look up this hash in the archived .abix file
            → retrieve name / layout / field information
```

Key point: `type_id` is the hash of canonical type identity; it is identical under debug and release. So debug and release binaries can cross-validate each other; `.abix` files and binaries can be bi-directionally verified. A hash mismatch indicates version mismatch — **a detectable error at load time**, not one that only manifests at runtime.

This is safer than DWARF. DWARF's `DW_AT_name` is a string with no built-in validation; ABIX's hash provides strong verification.

---

## Section Separation Mechanism

### Generation

After generating the descriptor arrays, the AMC C++ backend additionally generates a `.abix.names` section:

```cpp
// Auto-generated by AMC, not referenced by any runtime code
#ifdef __GNUC__
__attribute__((section(".abix.names")))
#endif
inline constexpr const char *amc_type_names[] = {
    "MyClass", "MyStruct", "MyEnum", ...
};
```

### Strip

```bash
# Debug → Release (remove .abix.names section)
strip --strip-section=.abix.names <binary>

# Verify the section has been removed
readelf -S <binary> | grep .abix.names  # No output
```

### Inspecting in the Binary

```bash
# List all custom sections
readelf -S <binary> | grep abix

# View .abix.names content
objcopy --dump-section .abix.names=/dev/stdout <binary> | strings
```

### Non-GCC Compiler Handling

On compilers that do not support `__attribute__((section))`, `.abix.names` content degrades to regular `.rodata`, but the runtime still does not reference them:

```cpp
// Non-GCC compilers: name strings are still in the binary, but in normal .rodata
// Diagnostic tools can still find them via symbol tables
inline constexpr const char *amc_type_names[] = {
    "MyClass", ...
};
```

---

## ABIX Symbol Server

This design unlocks a natural capability: the **ABIX symbol server**.

```
Production crash → extract list of type_id hashes
                 → query ABIX symbol server
                 → fetch corresponding .abix fragment
                 → restore type names / field names / layout
```

This follows the same pattern as Microsoft's symbol server and Mozilla's Tecken. ABIX has a natural advantage: **hashes are content-addressed**, so the same `.abix` artifact can be shared across multiple projects without being stored per-build-version.

### .abix.meta Metadata File

It is recommended that AMC generates, in addition to `.abix`, an `.abix.meta` metadata file recording:

```ini
build_id = "20260315-abcdef"
timestamp = 2026-03-15T10:30:00Z
compiler = "Clang 22.0"
hash_algorithm = "xxh3_128"
type_count = 61
```

This allows the symbol server to retrieve by build ID, without relying on filename conventions.

---

## Quantitative Savings

### Estimate for 500 Types

| Mode | Total Metadata |
|------|---------|
| Debug (embedded strings) | ~123 KB |
| Release (descriptor + hash) | ~28 KB |
| **Relative Savings** | **77%** |

### Actual Measurement (ABIX Runtime Self-Description, 61 Types)

| Metric | Debug | Release |
|------|-------|---------|
| TypeDescriptor array | 4,392 B (72 B × 61) | 3,416 B (56 B × 61) |
| Name strings | ~3 KB | 0 B |
| .data.rel.ro segment | 11.4 KB | ~8 KB |
| **Binary delta** | ~100 KB | ~50 KB (including descriptor array) |

### Source of Savings

```
Debug:   72 B (TypeDescriptor with name) + variable-length string constants ≈ 120 B/type
Release: 56 B (TypeDescriptor without name) + no string constants           ≈ 56 B/type
```

The 22% Release savings come from removing the `name` pointer from `TypeDescriptor` itself (along with reduced padding), while the savings from string constants (~20-50 B/type) are an additional benefit.

---

## Operation Guide

### Scenario 1: Local Development (Debug)

```bash
amc build -c my_project.abic.toml -B build/debug
# AMC generates Debug-mode C++ projection containing .abix.names
# Compile directly; name strings are available
```

### Scenario 2: Production Release (Release)

```bash
amc build -c my_project.abic.toml -B build/release
# AMC generates Release-mode C++ projection without name strings
# After compilation, strip to confirm no .abix.names section
strip --strip-section=.abix.names bin/my_app
```

### Scenario 3: Staging/Crash Analysis (RelWithDebInfo)

```bash
amc build -c my_project.abic.toml -B build/relwithdebinfo

# Archive .abix file to symbol server
cp build/relwithdebinfo/build/my_project.abix /symbol-server/releases/v1.2.3/

# Compile and deploy a binary identical to Release
# On production crash, use type_id from crash stack to query symbol server
```

### Verification: Pre- and Post-Strip Consistency

```bash
# 1. Compile Debug version
# 2. Run tests to confirm correct behavior
./bin/test_all

# 3. Strip to remove .abix.names
strip --strip-section=.abix.names bin/test_all

# 4. Run the same tests again; behavior remains identical
./bin/test_all

# 5. Verify .abix.names has been removed
readelf -S bin/test_all | grep -c .abix.names  # Output 0
```

---

## Boundaries That Must Be Maintained

### Boundary 1: `.abix` File Must Exactly Match the Binary Version

Hash validation can prevent mismatches, but only if **validation is actually executed**. A `type_id` set comparison must be performed at load time; trusting the file's mere existence is insufficient.

### Boundary 2: No Runtime Logic May Depend on `name` in Release Mode

Once a hot path depends on the `name` string (e.g., logging, error messages), Release builds will encounter null pointers or placeholders. Rule: **names are only used in diagnostic paths, and diagnostic paths must tolerate missing names.**

### Boundary 3: `TypeDescriptor` Layout Varies by Mode and Must Not Enter the ABI Contract

If cross-DLL/so descriptor passing is needed, a fixed layout must be used — at which point the `name` field should either always exist (an 56 B + 8 B trade-off) or be accessed indirectly via a handle.

### Boundary 4: `.abix` Archival Strategy Must Be Explicit

It is recommended that AMC generates, in addition to `.abix`, an `.abix.meta` metadata file recording build ID, timestamp, compiler version, and hash algorithm version. This allows the symbol server to retrieve by build ID, without relying on filename conventions.

---

## Summary

This design downgrades "metadata bloat" from a **structural cost** to a **configurable debugging overhead**. The default path is optimized for production, with diagnostic capabilities added on demand.

```
Debug:       72 B descriptor + .abix.names section  → full readability
Release:     56 B descriptor + no name strings       → minimal size
RelWithDeb:  56 B descriptor + companion .abix file  → post-hoc diagnostics
                                                     ↑
                                      Same binary, only .abix file differs
```