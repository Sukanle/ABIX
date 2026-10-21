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

The second reference provider is `amc-rust` (`AMC/src/Lang/Rust/`). Unlike
`amc-cpp` it does not link Clang/LLVM: its frontend is a source-level extractor
for the Rust FFI subset (`#[repr(C)]` structs/enums and `extern "C"`
functions), and its backend projects ABIX IR to Rust `repr(C)` types, using the
ABI-normalised primitive identity to select `core::ffi::c_*` aliases (§5). It is
the working template for a provider that computes layout itself rather than
delegating to a language compiler.

`amc-zig` (`AMC/src/Lang/Zig/`) is a **backend-only** provider: it consumes the
same `.abix` and projects it to Zig (`extern struct` types, `extern fn`
declarations, `[*c]` typed pointers and `comptime` `@sizeOf`/`@alignOf`/
`@offsetOf` assertions). It exists to demonstrate that a backend needs no
language-specific information in the model — the same artifact that drives the
Rust backend drives the Zig backend, and both consumers link the same native
library. There is no Zig frontend yet.

## 2.1 Provider manifest

`AMC/providers.toml` is the source of truth for `amc --list-languages` and
`amc --describe-language`. A provider entry declares its language, executable,
roles, protocol and optional feature set; `amc` does not keep a second
hard-coded language table.

```toml
version = 1

[[provider]]
language = "rust"
executable = "amc-rust"
roles = ["frontend", "backend"]
protocol = "jsonl-v1"
features = ["templates", "interfaces", "ownership", "error", "target"]

[[provider]]
language = "lua"
roles = ["backend"]
protocol = "direct"
contract = "aue"
```

At build time the manifest is copied next to `amc` as
`amc-providers.toml`. At run time `amc` resolves it in this order:

1. `AMC_PROVIDERS_FILE`;
2. `amc-providers.toml` next to the `amc` executable;
3. `AMC_DEFAULT_PROVIDERS_FILE`, compiled in for source-tree development.

`cmake --install` places `amc`, the language providers and
`amc-providers.toml` in the same `bin` directory, so the compiled-in source-tree
fallback is not needed after installation.

`protocol = "jsonl-v1"` entries require an `executable`; `protocol = "direct"`
entries are built-in backends (currently the Aue/Lua contract writer). The
`executable` field also drives `amc-<language>` resolution, so a provider can
move without changing the driver.

## 2.2 Producer/consumer protocol

The provider process speaks newline-delimited JSON over stdin/stdout. stderr is
for human diagnostics only. Every message carries `schema` and `id`; a request
receives exactly one final `RESULT` or `ERROR`, with optional `EVENT` progress
messages in between.

```json
{"schema":"abix.plugin/1","id":1,"type":"HELLO","protocol":{"major":1,"minor":0}}
{"schema":"abix.plugin/1","id":1,"type":"READY","protocol":{"major":1,"minor":0},"language":"cpp","roles":["frontend","backend"],"ir":{"min":4,"max":4}}
{"schema":"abix.plugin/1","id":2,"type":"QUERY_CAPABILITIES"}
{"schema":"abix.plugin/1","id":2,"type":"CAPABILITIES","language":"cpp","capabilities":["frontend","backend"]}
{"schema":"abix.plugin/1","id":3,"type":"ANALYZE","capability":"frontend","input":"demo.abic.toml","output":"demo.abix"}
{"schema":"abix.plugin/1","id":3,"type":"RESULT","status":"ok","artifacts":[{"kind":"abix","path":"demo.abix"}]}
{"schema":"abix.plugin/1","id":4,"type":"DONE"}
```

`DESCRIBE` returns the provider's own manifest-shaped description, which
`amc --describe-language` can use to verify `AMC/providers.toml`. The legacy
`{"type":"INIT","protocol":1}` handshake remains accepted during the transition,
but new providers should send `HELLO` with an object `protocol`.

`ANALYZE` carries a frontend extraction request (`capability`, `input`,
`output`). `PROJECT` carries a backend projection request (`input`, `output`);
the driver sends `PROJECT` for a backend capability and only accepts a provider
that advertises `backend`. Both may emit any number of `EVENT` messages with the
same `id` before the final `RESULT`; the driver ignores unknown event kinds.

```json
{"schema":"abix.plugin/1","id":3,"type":"EVENT","event":{"kind":"progress","message":"parsing","current":1,"total":4}}
{"schema":"abix.plugin/1","id":4,"type":"PROJECT","input":"demo.abix","output":"gen"}
{"schema":"abix.plugin/1","id":4,"type":"RESULT","status":"ok","artifacts":[{"kind":"source","path":"gen/amc_generated.rs"}],"diagnostics":[]}
```

`CANCEL` requests cancellation of an earlier request:

```json
{"schema":"abix.plugin/1","id":5,"type":"CANCEL","target":4}
{"schema":"abix.plugin/1","id":5,"type":"CANCELLED","target":4,"status":"cancelled"}
```

A provider acknowledges `CANCEL` when it is idle. A capability call is
synchronous, so a provider that is already inside `ANALYZE`/`PROJECT` may finish
instead of observing the cancel. The driver applies `AMC_PROVIDER_TIMEOUT_MS`
(disabled when unset) to every response wait: on timeout it sends `CANCEL` as a
best effort, terminates the provider process and reports a provider error. This
keeps the protocol deterministic and prevents a stuck provider from hanging the
toolchain.

Capability stderr is captured and attached to the `ERROR` / `RESULT`
`diagnostics` array as `{"severity":"error"|"warning","message":...}`. The driver
includes the first diagnostic in the human-readable error text. A provider must
still use the JSON `ERROR` envelope for protocol-level failures; stderr is for
capability diagnostics, not control flow.

Message summary:

| Message | Direction | Purpose |
| --- | --- | --- |
| `HELLO` / `READY` | driver → provider / provider → driver | protocol and capability negotiation |
| `INIT` | driver → provider | legacy numeric-protocol handshake |
| `QUERY_CAPABILITIES` / `CAPABILITIES` | driver → provider / provider → driver | capability discovery |
| `DESCRIBE` / `DESCRIBE_RESULT` | driver → provider / provider → driver | manifest-shaped provider description |
| `ANALYZE` / `RESULT` | driver → provider / provider → driver | frontend extraction |
| `PROJECT` / `RESULT` | driver → provider / provider → driver | backend projection |
| `EVENT` | provider → driver | optional progress or diagnostic event |
| `CANCEL` / `CANCELLED` | driver → provider / provider → driver | best-effort cancellation |
| `ERROR` | provider → driver | structured `abix.error/1` failure |
| `DONE` / `DONE` | driver → provider / provider → driver | orderly shutdown |

Version rules:

- `protocol.major` must match `1`; an unsupported major is a fatal
  `PLUGIN-PROTOCOL-MISMATCH`.
- `protocol.minor` may add optional fields; unknown optional fields are ignored.
- An unsupported message type returns `PLUGIN-UNSUPPORTED-MESSAGE`.
- A malformed line returns `PLUGIN-INVALID-JSON` and terminates the session.
- A capability failure returns `PLUGIN-CAPABILITY-FAILED` with the provider's
  diagnostic, not a generic driver error.
- The same inputs, target, options and provider version must produce
  byte-identical artifacts and projections.

The `.abix` artifact remains the payload of `ANALYZE` for frontend extraction and
of `PROJECT` for backend projection; the JSONL envelope only carries control,
correlation and diagnostics. This keeps the language-independent IR stable while
provider internals remain free to change.

## 3. Responsibilities

A plugin **should**:

- parse the language's constructs;
- resolve types to concrete ABI types;
- extract ABI-relevant semantics (size, alignment, offsets, calling convention,
  parameter/return types, symbol naming);
- normalize language-specific constructs into the ABIX model;
- perform generic instantiation and constraint checking for its language, and
  report the concrete entity (ABIX records concrete instantiations, not generic
  parameters — see [`ABI-SPEC.md`](ABI-SPEC.md) §18);
- declare the target (arch / os / ABI / compiler / calling convention).

A plugin **must not**:

- redefine `TypeID` or `LayoutHash` semantics;
- redefine compatibility rules;
- create a second ABI IR;
- depend on runtime internals;
- leak language-specific concepts into the core model without an ABI meaning.

## 4. Supported ABI Concepts

The core model currently covers: primitive, enumeration, record, pointer, array,
function, namespace, alias; fields with offsets/flags; functions with signatures,
parameters, calling convention and flags; symbols; hashes; compatibility and
map records.

See [`ABI-SPEC.md`](ABI-SPEC.md) §3–§8.

## 5. Type Mapping

| Language construct          | ABIX representation                   |
| --------------------------- | ------------------------------------- |
| fixed-width integer / float | primitive (size, align)               |
| enum                        | enumeration (underlying integer type) |
| struct / class / union      | record + fields                       |
| pointer / reference         | pointer                               |
| fixed-size array            | array (`array_count`)                 |
| function pointer            | function                              |
| namespace / module          | namespace type                        |
| `typedef` / `using`         | alias (with an `underlying` field)    |

The mapping must describe the **ABI**, not the source spelling: two source types
with the same ABI must produce the same identity.

For scalar/primitive types this is enforced by the core model: the `TypeID` is
the hash of an ABI descriptor (kind, size, align, signedness, floating-point
format), not of the spelling. A frontend must supply that descriptor rather than
inventing a name-based identity, so `long`/`long long` (LP64) and a C `double` /
a Rust `f64` of the same target collapse onto a single `TypeID`. Character kinds
are split by signedness: plain `char` and `wchar_t` must record their
target-defined signedness so a signed and an unsigned `char` never merge. When projecting
_a C-ABI scalar to a target language_, emit the target's C-ABI alias
(for Rust, `core::ffi::c_*` such as `c_double`/`c_int`/`c_float`) rather than a
fixed-width `f64`/`i32`, because the alias is what encodes the target's C ABI;
use fixed-width spellings only when the contract explicitly fixes the width.

## 6. Generics / Templates

- A template **primary** is recorded as a type with the
  `type_template_primary` flag; it is metadata for dependency closure, not a
  concrete native type.
- A concrete **specialization** with a stable instantiated layout is recorded as
  its own type.
- Do not expose dependent AST nodes; if a construct cannot be resolved to a
  concrete ABI, record it as opaque rather than guessing.

## 7. Namespaces / Modules

- Namespaces become `namespace_type` records; they are part of the diagnostic
  naming, not of compatibility.
- A symbol's fully qualified name uses the target language's separator mapped to
  a stable canonical form (`Owner::name` for C++).
- The runtime never resolves by name; names exist for tools and humans.

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

- **extraction tests** — known source → expected ABI records;
- **serialization round-trip** — write/read `.abix` preserves the model;
- **compatibility tests** — intentional ABI changes are detected;
- **determinism** — identical inputs produce byte-identical artifacts;
- **cross-check** — at least one consumer (`amc query` / runtime projection)
  works against the produced artifact.

The reference coverage lives in `AMC/tests/`: `unit/` and `e2e/` (C++),
`cli/` (AMC CLI), `conformance/provider_harness.py` (the shared provider
conformance harness), `providers/<language>/` (per-provider cases, for example
`providers/rust/`) and `interop/` (the C++ → ABIX → Rust/Zig end-to-end test
that builds a real library, projects the same `.abix` to two languages and runs
both consumers against it).

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

## 12. Version Compat

- A plugin targets a `.abix` format version and the ABIX IR revision.
- When ABIX IR changes, plugins must be updated in the same change window; the
  format version guards readers against mismatched artifacts.
- Plugins must tolerate unknown optional sections (see [`ABI-SPEC.md`](ABI-SPEC.md) §17).
