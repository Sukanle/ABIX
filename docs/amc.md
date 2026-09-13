# AMC — ABI Meta Compiler

AMC is the ABIX toolchain entry point. It extracts ABI information from a
language AST, projects it into ABIX IR, and provides inspection, comparison,
verification, generation and distribution tooling.

```text
                   AMC
                    │
       ┌────────────┼────────────┐
       ▼            ▼            ▼
     Parse        Generate      Analyze
       │            │            │
       └────────────┼────────────┘
                    ▼
                 ABIX IR
```

AMC is an extensible toolchain, not a language-specific compiler: language
frontends extract ABI, ABIX IR stays the common representation.

## Configuration

Builds are described by an `.abic.toml` file:

```toml
[package]
name = "math_api"
version = "1.0"

[[import]]
language = "cpp"
flags = ["-std=c++17"]
files = ["math_api.hpp"]
symbols = ["math::add", "math::Point"]

[[export]]
output = "build/math_api.abix"
```

See [`abic.md`](abic.md) for the complete reference.

## Commands

### `amc build`

Runs the configured frontend, projects the requested symbols and writes the
`.abix` artifact (plus a `.abix.meta` indexing sidecar):

```bash
amc build -c package.abic.toml [-B <build_dir>]
```

### `amc inspect` / `amc validate`

```bash
amc inspect  file.abix      # one-line summary
amc validate file.abix      # structural + hash validation
amc inspect  libfoo.so      # decode the embedded Metadata Region
```

### `amc query`

Structured queries for humans and agents. Accepts `.abix` or a compiled binary.

```bash
amc query file.abix --type Foo --layout          # type + field offsets
amc query file.abix --function Foo::bar          # signature, params, return
amc query file.abix --compatible other.abix      # strict ABI comparison
amc query file.abix --type Foo --format json     # abix.query/1 JSON
```

`--type`/`--function` accept exact names, `Owner::name`, substrings, or a
`0x`-prefixed `TypeID`.

### `amc context`

Dense ABI context for LLMs: types/fields/functions are indexed (`T0/F0/P0`) so
cross references stay short instead of repeating 128-bit hashes.

```bash
amc context file.abix --format llm
amc context file.abix --format json --no-names
```

### `amc diff` / `amc compatibility`

```bash
amc diff v1.abix v2.abix [-o report.abix]
amc compatibility v1.abix v2.abix        # exit 1 when incompatible
```

### `amc verify`

Compares a frozen contract against a regenerated implementation, or two
artifacts directly.

```bash
amc verify -c package.abic.toml [-B <build_dir>]
amc verify contract.abix implementation.abix
amc verify contract.abix implementation.abix --format diagnostics
```

`--format diagnostics` emits `file:line:column: error: ABI <kind> ...` lines
using the contract's Source Origin, for editors and CI annotations.

### `amc generate`

```bash
amc generate file.abix -l cpp -o generated.hpp    # native C++17 projection
amc generate file.abix -l lua -o aue_contract.hpp # Aue contract (header)
amc generate file.abix -l lua -o conformance.lua  # generated Lua test case
```

### `amc metadata`

The self-describing Metadata Region (manifest + desc + hash + names), emitted,
verified and scanned offline.

```bash
amc metadata file.abix -o region.abixmeta [--no-names] [--build-id 0x...]
amc metadata --verify region.abixmeta
amc metadata --from-elf libfoo.so --format json
amc metadata --verify libfoo.so --format json
```

### `amc publish` / `amc fetch`

A local symbol server keyed by the ELF GNU BuildID:

```bash
amc publish libfoo.so --root ~/.abix/symbols
amc publish file.abix --root ~/.abix/symbols --build-id 0x...
amc fetch libfoo.so -o libfoo.abixmeta
amc fetch --build-id 0x... -o libfoo.abix
```

Store layout: `{root}/{build_id[0:2]}/{build_id[2:]}.{abix|abixmeta}` plus
`.meta`. Remote fallback is not implemented yet.

## Structured errors

Every AMC tool can emit one error envelope on stderr:

```bash
amc validate missing.abix --error-format json
```

```json
{"schema":"abix.error/1","error":{"code":"AMC-IO","category":"io",
 "stage":"read_abix","message":"cannot open input: missing.abix",
 "file":"missing.abix","detail":null}}
```

`--format json` on a command also switches errors to JSON.

## Other tools

| Tool | Purpose |
|------|---------|
| `amc-cpp` | C++ frontend/backend provider (JSON-lines IPC) |
| `amc-dump` | raw `.abix` dump (text / JSON) |
| `amc-mcp` | MCP server exposing ABIX tools ([MCP.md](MCP.md)) |
| `abix-conformance` | Aue L0/L1 differential runner (when Lua is present) |
