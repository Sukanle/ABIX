# Getting Started

This walkthrough builds ABIX, produces an `.abix` artifact from a small C++ ABI,
and shows the core operations: inspect, query, diff, verify and generate.

> ABIX is under active development; commands shown here match the current CLI.

## 1. Requirements

* C++17 or later
* CMake 3.20+
* LLVM / Clang tooling (the AMC C++ frontend links `clang-cpp`)
* a native toolchain (gcc/clang, GNU or Ninja)

Optional but used by parts of the toolchain and tests:

* Lua 5.4 — builds the experimental Aue boundary layer (`aue/`)
* `readelf`, `strip`, `lldb`, `clang++` — used by integration tests

## 2. Build

```bash
git clone <repository>
cd ABIX
cmake -B build/Release -DCMAKE_BUILD_TYPE=Release -G Ninja -S .
cmake --build build/Release --parallel
```

Artifacts land in `build/bin`, including:

| Tool | Purpose |
|------|---------|
| `amc` | ABI toolchain driver (build / inspect / query / verify / generate / metadata / publish / fetch) |
| `amc-cpp` | C++ frontend/backend provider (JSON-lines IPC) |
| `amc-dump` | raw `.abix` dump (text / JSON) |
| `amc-mcp` | MCP server exposing ABIX tools to AI agents |
| `abix-conformance` | Aue L0/L1 differential runner (when Lua is present) |

Run the test suite:

```bash
ctest --test-dir build/Release --output-on-failure
```

## 3. Describe an ABI

ABI extraction is driven by an `.abic.toml` configuration. A minimal example:

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

```cpp
// math_api.hpp
namespace math {
struct Point { int x; int y; };
int add(int a, int b);
}
```

Build the metadata:

```bash
./build/Release/bin/amc build -c math_api.abic.toml -B build
# -> build/build/math_api.abix
# -> build/build/math_api.abix.meta
```

See [`abic.md`](abic.md) for the full configuration reference.

## 4. Inspect and query

```bash
# one-line summary
./build/Release/bin/amc inspect build/build/math_api.abix

# a single type, with its physical layout
./build/Release/bin/amc query build/build/math_api.abix --type math::Point --layout

# a function signature
./build/Release/bin/amc query build/build/math_api.abix --function math::add --format json

# an LLM-friendly ABI context (dense, index-addressable)
./build/Release/bin/amc context build/build/math_api.abix --format llm
```

Every consumer also accepts a compiled binary: the generated C++ header embeds
the ABI as an `.abix.metadata` section, so `amc inspect libfoo.so` works without
the sidecar `.abix`:

```bash
./build/Release/bin/amc query libfoo.so --type math::Point --layout
```

## 5. Compare and verify

```bash
# compare two artifacts (source -> target)
./build/Release/bin/amc diff v1.abix v2.abix

# exit 1 when incompatible
./build/Release/bin/amc compatibility v1.abix v2.abix

# verify that current sources still match the frozen contract
./build/Release/bin/amc verify -c math_api.abic.toml -B build

# editor-consumable diagnostics
./build/Release/bin/amc verify v1.abix v2.abix --format diagnostics
```

`amc verify` treats any difference — including added types or functions — as
drift, because widening the ABI surface must be an explicit decision.

## 6. Generate

```bash
# native C++17 projection (descriptors for RuntimeRegistry)
./build/Release/bin/amc generate build/build/math_api.abix -l cpp -o generated.hpp

# Aue (Lua) boundary contract
./build/Release/bin/amc generate build/build/math_api.abix -l lua -o aue_contract.hpp
```

## 7. Metadata Region and symbol server

```bash
# emit and verify the self-describing Metadata Region
./build/Release/bin/amc metadata build/build/math_api.abix -o math_api.abixmeta
./build/Release/bin/amc metadata --verify math_api.abixmeta

# scan the region embedded in a compiled binary
./build/Release/bin/amc metadata --from-elf libfoo.so --format json

# local symbol server keyed by ELF GNU BuildID
./build/Release/bin/amc publish libfoo.so --root ~/.abix/symbols
./build/Release/bin/amc fetch libfoo.so -o libfoo.abixmeta
```

## 8. AI agent integration

```bash
# start the MCP server over stdio
./build/Release/bin/amc-mcp build/build/math_api.abix

# or drive it from a script
python3 tools/abix_mcp_compat.py --host host.abix --plugin plugin.abix \
        --amc-mcp ./build/Release/bin/amc-mcp
```

See [`MCP.md`](MCP.md) for the tool catalogue.

## Next steps

* [`abix.md`](abix.md) — the `.abix` artifact format
* [`compatibility.md`](compatibility.md) — ABI identity and compatibility
* [`amc.md`](amc.md) — the AMC toolchain
* [`architecture.md`](architecture.md) — how the pieces fit together
* [`design-notes.md`](design-notes.md) — design principles and history
* [`bootstrap.md`](bootstrap.md) — bootstrap model and milestones
