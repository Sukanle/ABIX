# .abic — ABI Configuration

## Positioning

`.abic` is a **declarative ABI configuration file** that describes the intent and strategy for "how to build ABI", rather than the ABI facts themselves.

```
.abic = Intent / Policy
.abix = Fact / Artifact
```

`.abic` is closer to CMakeLists.txt / Cargo.toml / protobuf options / compiler configuration, rather than a final ABI database.

---

## Core Responsibilities

| Responsibility | Description |
|------|------|
| Declare source file paths | Specify which source files to scan, avoiding full-project scanning |
| Declare export scope | Which types and functions need to be exported |
| Declare target platform | arch / OS |
| Declare ABI conventions | Compiler / calling convention |
| Declare normalization strategy | How type names are normalized |
| Declare compatibility strategy | Whether to generate compatibility relationships and rules |
| Declare mapping strategy | Whether to generate Map, mapping direction |
| Declare generation targets | Target language, output format |
| Declare runtime mode | Static / Dynamic / Hybrid |

---

## Format: TOML

`.abic` is intended for **humans**, therefore it uses a highly readable format like TOML (or YAML).

---

## Complete Example

```toml
[package]
name = "mylib"
version = "2.0"
abi_version = 1

[source]
files = [
    "src/foo.hpp",
    "src/bar.hpp",
    "src/baz.cpp"
]
include_dirs = ["include", "src"]
exclude = ["src/internal/*"]

[target]
arch = "x86_64"
os = "linux"

[abi]
compiler = "gcc"
calling_convention = "sysv_abi"

[export]
types = ["Foo", "Bar", "Baz", "FooKind", "BarFlags"]
functions = ["foo_create", "foo_destroy", "bar_process"]

[normalization]
integer = "fixed-width"       # int → i32, long → i64, ...
pointer = "opaque"             # T* → Ptr<T>
string = "slice"               # std::string → StringSlice
container = "abi_stable"       # std::vector → AbiVector

[compatibility]
enable = true
mode = "layout_hash"           # layout_hash | type_id | strict
max_minor_version = 4

[map]
Foo_v1 = "Foo_v2"              # Version mapping
Bar_old = "Bar_new"            # Rename mapping

[generator]
language = ["cpp", "rust"]
hash_algorithm = "fnv1a64"     # fnv1a64 | xxh3 | blake3
emit_map_ir = true             # Generate Map IR in .abix
emit_static_map = true         # Generate MapPrivate static conversion

[runtime]
mode = "hybrid"                # static | dynamic | hybrid
lazy_load = true               # Runtime lazy loading

[output]
format = "abix"                # abix | json | none
compact = true                 # Binary compact layout
debug_info = false             # Embed debug strings (mutually exclusive with compact; debug_info takes priority)
```

---

## Configuration Section Details

### `[package]`

| Field | Type | Description |
|------|------|------|
| `name` | string | Package name |
| `version` | string | Semantic version string |
| `abi_version` | uint | ABI format version (decoupled from package version) |

### `[source]`

| Field | Type | Description |
|------|------|------|
| `files` | string[] | Source file paths to scan (relative to .abic directory) |
| `include_dirs` | string[] | Header file search paths |
| `exclude` | string[] | File patterns to exclude (glob) |

**Design intent**: AMC only scans the files specified in `[source]`, not the entire project. This significantly speeds up compilation, especially in large projects, by avoiding unnecessary AST parsing.

If `[source]` is missing, AMC will scan all `.hpp` / `.h` / `.cpp` files in the same directory as .abic (backward compatible, but emits a warning).

### `[target]`

| Field | Type | Description |
|------|------|------|
| `arch` | string | Target architecture: `x86_64` / `aarch64` / `riscv64` / ... |
| `os` | string | Target OS: `linux` / `windows` / `macos` / ... |

`[target]` only describes the **target platform**, not the compiler/calling convention (the latter belongs to `[abi]`).

### `[abi]`

| Field | Type | Description |
|------|------|------|
| `compiler` | string | Compiler: `gcc` / `clang` / `msvc` / ... |
| `calling_convention` | string | Calling convention: `sysv_abi` / `ms_abi` / `aapcs` / ... |

`[abi]` describes the **compiler and ABI convention**, with a clear separation of concerns from `[target]` (platform).

### `[export]`

| Field | Type | Description |
|------|------|------|
| `types` | string[] | Type names to export ABI for (including enums) |
| `functions` | string[] | Function names to export ABI for |

Enums are essentially Types and are uniformly listed under `types`. No separate `enums` field to avoid semantic overlap.

### `[normalization]`

| Field | Type | Description |
|------|------|------|
| `integer` | enum | Integer normalization strategy: `fixed-width` / `native` / `none` |
| `pointer` | enum | Pointer normalization strategy: `opaque` / `typed` / `none` |
| `string` | enum | String normalization strategy: `slice` / `ptr` / `none` |
| `container` | enum | Container normalization strategy: `abi_stable` / `none` |

The purpose of normalization is to map type names produced by different compilers/languages to the same canonical representation:

```
C++ int        → i32
C++ long       → i64 (on LP64)
Rust i32       → i32
Zig i32        → i32
```

### `[compatibility]`

| Field | Type | Description |
|------|------|------|
| `enable` | bool | Whether to generate compatibility information |
| `mode` | enum | Compatibility check mode: `layout_hash` / `type_id` / `strict` |
| `max_minor_version` | uint | Maximum compatible minor version number |

### `[map]`

Key-value pairs declaring type mappings:

- Version mapping: `Foo_v1 = "Foo_v2"` — maps from old version to new version
- Rename mapping: `Bar_old = "Bar_new"` — maps from old name to new name

### `[generator]`

| Field | Type | Description |
|------|------|------|
| `language` | string[] | Target language: `cpp` / `rust` / `zig` / ... |
| `hash_algorithm` | enum | Hash algorithm: `fnv1a64` / `xxh3` / `blake3` |
| `emit_map_ir` | bool | Whether to generate Map IR in .abix |
| `emit_static_map` | bool | Whether to generate MapPrivate static conversion |

`emit_map_ir` and `emit_static_map` are independent generation switches, decoupled from `[runtime].mode`:
- `emit_map_ir = true, emit_static_map = false`: Only save Map IR in .abix, execute dynamically at runtime
- `emit_map_ir = true, emit_static_map = true`: Generate both runtime IR and compile-time MapPrivate
- `emit_map_ir = false, emit_static_map = true`: Only generate MapPrivate (no Map IR in .abix)

`hash_algorithm` is **generation strategy** (input); the `hash_algorithm` in .abix Header is the **actually used algorithm** (output) — a reasonable configuration-to-artifact relationship.

### `[runtime]`

| Field | Type | Description |
|------|------|------|
| `mode` | enum | Runtime mode: `static` / `dynamic` / `hybrid` |
| `lazy_load` | bool | Whether Runtime uses lazy loading |

### `[output]`

| Field | Type | Description |
|------|------|------|
| `format` | enum | Output format: `abix` / `json` / `none` |
| `compact` | bool | Whether binary uses compact layout |
| `debug_info` | bool | Whether to embed debug strings |

`debug_info` is mutually exclusive with `compact`: when `debug_info = true`, `compact` is ignored (debug information takes priority).

---

## Runtime Modes

### `static`

All ABI information is determined at compile time, generating `constexpr` metadata and `MapPrivate`.

```
.abic → AMC → .abix → AMC → .abix.hpp → Compiler → inline
```

### `dynamic`

All ABI information is loaded at runtime, queried dynamically via `mmap` + Registry.

```
.abic → AMC → .abix → Runtime → mmap → Registry → dynamic query
```

### `hybrid` (recommended)

Known ABIs take the static path, unknown ABIs take the dynamic path.

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

---

## Relationship with .abix

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

- `.abic` is input: describes intent
- `.abix` is output: records facts
- AMC is the compiler: transforms intent into facts

**The Source of Truth is always `.abix`**, not `.abic`. `.abic` is only used at build time and does not participate at runtime.

---

## Minimal Example

Export types only, using default configuration:

```toml
[package]
name = "minimal"
version = "1.0"

[source]
files = ["src/foo.hpp"]

[export]
types = ["Foo"]
```

AMC will fill in defaults for all omitted fields.

---

## Design Principles

1. **Declarative**: describes "what you want", not "how to do it"
2. **Human-readable**: TOML format, easy to hand-write and version-control
3. **Completable**: all fields have sensible defaults
4. **Decoupled from version**: `abi_version` is independent of `version`
5. **Build-time only**: no dependency on `.abic` at runtime
6. **No semantic overlap**: each configuration item has a unique responsibility; no conflicting combinations across different sections