# ABIX MCP: ABI Metadata as the AI Agent's ABI Information Gateway

## Positioning

Standalone ABIX Metadata Region + external `.abix` files provide AMC with a
machine/AI-agent-facing ABI information gateway.

```
              AI Agent
                  │
                 MCP
                  │
         ┌────────▼────────┐
         │   AMC ABI API   │
         │ query / compare │
         └────────┬────────┘
                  │
         ┌────────▼────────┐
         │ ABIX Metadata   │
         │   .abix         │
         └────────┬────────┘
                  │
         ┌────────▼────────┐
         │ ABIX Runtime    │
         │ compact metadata│
         └─────────────────┘
```

## `amc-mcp` Prototype

`amc-mcp` implements MCP JSON-RPC over stdio (newline-delimited), supporting
`initialize` / `ping` / `tools/list` / `tools/call`; notifications produce no
response, unknown methods return `-32601`, and parse failures return `-32700`.
Two MCP protocol versions are supported, `2024-11-05` and `2025-06-18`, with
`2025-06-18` the preferred newest. On `initialize` the server echoes the
client's requested `protocolVersion` when that version is supported, and
otherwise negotiates down to the preferred newest supported version;
`serverInfo` always reports `{"name": "amc-mcp", "version": "1.0.0"}`.
The exposed tools are:

| Tool | Parameters | Description |
|------|------------|-------------|
| `abix.get_module` | `module?` | package/target/ABIHash/counts |
| `abix.list_types` | `module?` | all types with TypeID / LayoutHash / size / align |
| `abix.get_type` | `name`\|`id`, `layout?`, `module?` | single type; `layout` includes field offsets |
| `abix.list_functions` | `module?` | all functions: signature, return type, parameter count |
| `abix.get_function` | `name`, `module?` | function signature, return type, parameters |
| `abix.get_layout` | `name`\|`id`, `module?` | single type memory layout (size/align/field offsets) |
| `abix.resolve_type` | `name`\|`id`, `module?` | resolve matching types by name or partial TypeID |
| `abix.compare_abi` | `other`, `module?` | strict ABI comparison (missing types/layout changes count as drift) |
| `abix.compare_types` | `name`, `other`, `module?` | cross-artifact single type comparison |
| `abix.find_compatible` | `name`, `other?`, `module?` | cross-artifact compatibility check; omitting `other` searches the knowledge base |
| `abix.list_modules` | — | list all indexed modules in the knowledge base |
| `abix.search_type` | `name`\|`id` | cross-module type search, grouped by name with ABI consistency flags |

All tools reuse AMC's query and verify implementations, returning
`structuredContent` (structured JSON) and `content[].text` (text form of the
same JSON), so the CLI, MCP, and other consumers share one implementation.
Usage:

```sh
amc-mcp path/to/module.abix        # use a module as default data source
amc-mcp --list-tools               # print the tool catalogue
```

### Human-readable Names in `abix.get_module`

The `target` object returned by `abix.get_module` pairs each numeric code with
a human-readable name:

```json
{
  "arch": 0,
  "arch_name": "x86_64",
  "os": 0,
  "os_name": "linux",
  "compiler": 0,
  "compiler_name": "gcc",
  "calling_convention": 0,
  "calling_convention_name": "sysv_abi"
}
```

The encodings are: `arch` (0=x86_64, 1=aarch64, 2=riscv64, ...), `os`
(0=linux, 1=windows, 2=macos, ...), `compiler` (0=gcc, 1=clang, 2=msvc, ...)
and `calling_convention` (0=sysv_abi, 1=ms_abi, 2=aapcs, ...). `target.abi`
stays numeric only: there is no documented encoding for it, so no name field
exists for it.

These `*_name` fields are diagnostic only. They never participate in ABI
identity or compatibility, which remain decided by TypeID / LayoutHash /
ABIHash.

### `abix.list_functions`: Calling Convention Names

Each function item also carries `calling_convention_name` next to the numeric
`calling_convention`, using the per-function AMC numbering: 0=unspecified,
1=c, 2=stdcall, 3=fastcall, 4=thiscall, 5=aarch64_sve.

### Type Lookup Parameters

`abix.get_type`, `abix.get_layout`, `abix.resolve_type` and `abix.search_type`
formally require one of `name` or `id` in their input schemas (JSON Schema
`anyOf`), so a call must pass exactly one of the two.

For `abix.resolve_type` and `abix.search_type`, `id` accepts a partial TypeID
prefix such as `0x18da104f` as well as a full TypeID, and `name` accepts a
full name or a name substring.

### Multi-module ABI Knowledge Base

`amc-mcp` can index multiple artifacts (`.abix` or binaries with embedded
Metadata Regions) at once, loading them all at startup so that cross-module
queries are a single index walk instead of repeated file parsing:

```sh
amc-mcp libfoo.abix libbar.abix plugin.so    # positional args are all indexed
amc-mcp --index libfoo.abix --index plugin.so # equivalent form
```

After indexing:

* `abix.list_modules` enumerates each module's path / package / ABIHash / counts;
* `abix.search_type "Foo"` looks up matching types in every module, groups hits
  by type name, and flags each group as `consistent` — whether all occurrences
  share one TypeID and LayoutHash. This directly answers "find all types that
  implement the same ABI";
* `abix.find_compatible "Foo"` (without `other`) uses the default module as
  source and checks compatibility against every other indexed module; when no
  other indexed module declares the requested type, the result reports a
  `reason` field saying so.

Single-module usage is unchanged: giving one path makes that module the default
data source.

## Simplifying `amc dump`

Traditional path: `amc dump` required ELF parsing.

```
.so / executable
    ↓
ELF/Mach-O/PE parsing
    ↓
scan various sections
    ↓
locate Descriptor
    ↓
handle relocation / pointer
    ↓
parse strings
```

With standalone Metadata, `amc dump` only needs to parse the Metadata itself.

```
binary
   ↓
ABIX Metadata Header (magic-based location)
   ↓
offset + size (self-describing)
   ↓
direct mmap
   ↓
amc dump
```

After adopting Header + TypeRecords + FieldRecords + FunctionRecords with an
**offset-based, pointer-free** layout, `amc dump` is primarily a Metadata
parser, not an ELF parser.

### Supported Operations

```bash
amc dump libfoo.so           # scan Metadata Region from binary
amc dump foo.abix            # read from standalone .abix file
amc dump foo.abix --type Foo           # query a specific type
amc dump foo.abix --function bar       # query a specific function
amc dump foo.abix --layout Foo         # query layout information
amc query foo.abix "Foo::bar"          # query by name
```

## MCP Integration

The design targets MCP: agents no longer need LLMs to read tens of MB of ELF,
headers, or decompiled output for each query.

### Architecture

```
                 AI Agent
                    │
                   MCP
                    │
              ┌─────┴─────┐
              │   AMC     │
              │ MCP Tool  │
              └─────┬─────┘
                    │
             ABIX Metadata
                    │
       ┌────────────┼────────────┐
       ▼            ▼            ▼
     Type        Function      Layout
       │            │            │
       ▼            ▼            ▼
    TypeID       Signature    LayoutHash
```

### Query Interface

```
get_type("Foo")                 → structured type information
get_function("Foo::bar")        → function signature + ABI information
get_layout(type_id)             → layout details
find_compatible_type(type_id)   → compatible type list
find_function("create")         → find functions by name
get_module_info()               → module metadata
```

Return values are structured data (JSON / protobuf), not large text blobs.

## Token Cost

Traditional approach: have the agent read C++ source to infer ABI.

```cpp
class Foo {
public:
    virtual void update(
        const std::string& name,
        std::vector<int>& values
    );
private:
    // ...
};
```

The agent must infer from source: ABI, layout, visibility, calling convention,
parameter types, inheritance, template instantiation, actual export status.

ABIX directly provides already-parsed results:

```json
{
  "type": "Foo",
  "type_id": "abc123...",
  "layout_hash": "def456...",
  "size": 128,
  "align": 8,
  "functions": [
    {
      "name": "update",
      "signature": "abc789...",
      "abi": "aarch64_linux",
      "parameter_count": 2,
      "parameters": [
        { "type": "const std::string&", "type_id": "..." },
        { "type": "std::vector<int>&", "type_id": "..." }
      ]
    }
  ]
}
```

What is returned is ABI fact after AMC semantic normalization, not an inference
the AI derives by analyzing the C++ AST. The [TROI](troi.md) metric measures
this token-efficiency benefit for AMC and MCP agent workflows.

## ABIX as the Agent's "ABI API"

MCP exposes a higher-level ABI query API, not raw `read_abix_file()`:

```
abix.get_module()          → module basic information
abix.list_types()          → type list
abix.get_type()            → single type details
abix.get_layout()          → layout information
abix.list_functions()      → function list
abix.get_function()        → single function details
abix.compare_types()       → type comparison
abix.compare_abi()         → ABI compatibility analysis
abix.find_compatible()     → find compatible types
abix.resolve_type()        → resolve type by name/ID
```

### Example: Cross-artifact Type Compatibility Query

```
libA.Foo
   ↓
TypeID + LayoutHash
   ↓
libB.Foo
   ↓
TypeID + LayoutHash
   ↓
ABIX compatibility analysis
   ↓
Result:

Semantic type:      compatible
Layout:             compatible
Calling convention: compatible
Fields:             compatible
Functions:          compatible
ABI patch:          possible
```

This query is performed at the Metadata layer by ABIX, with no source reading
required.

## External `.abix`

### Separating Production and Analysis Environments

Release:
```
program
├── code
└── compact ABIX Runtime Metadata
```

Debug / AI / Analysis environment:
```
program
       │
       └── BuildID
              ↓
          symbol server
              ↓
          foo.abix
              ↓
        MCP / AMC / Debugger
```

The release binary contains only minimal Runtime Metadata; the analysis
environment associates `.abix` via BuildID to obtain full ABI information.

## `.abix` as an ABI Knowledge Base

Multiple `.abix` files can form an indexable ABI Knowledge Base:

```
ABIX Repository
│
├── libfoo.abix
├── libbar.abix
├── pluginA.abix
└── pluginB.abix
```

MCP builds an index on top of it:

```
TypeID      → Type
FunctionID  → Function
LayoutHash  → Layout
BuildID     → Module
```

### Example Reasoning Queries

- "Find all types that implement the same ABI"
- "Find compatible versions of Foo"
- "Is this plugin compatible with the current host?"
- "Which type does this ABI crash correspond to?"
- "Which version changed Foo's layout?"
- "Can we auto-generate ABIX bindings?"

This structure maps from Metadata to an ABI Knowledge Graph to Agent reasoning:

> ABIX Metadata → ABI Knowledge Graph → AI Agent ABI Reasoning

## Three-layer Unified Entry Point

"Compact Runtime Metadata + standalone Metadata Region + external `.abix`"
simultaneously correspond to ABIX's three entry points:

| Entry Point | Consumption Method | Purpose |
|-------------|-------------------|---------|
| **Runtime** | high-speed ABI lookup | dynamic binding, contract verification |
| **AMC** | fast dump / query / verify / generate | development debugging, compatibility analysis |
| **AI Agent / MCP** | structured ABI query / comparison / reasoning / auto-generation | intelligent code generation, ABI reasoning |

ABIX Metadata is therefore also the machine-readable ABI interface in the ABIX
ecosystem, not merely descriptive data attached to the Runtime for dynamic
binding.
