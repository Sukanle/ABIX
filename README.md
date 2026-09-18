<p align="center">
  <img src="assets/abix-wordmark.svg" alt="ABIX" width="320">
</p>

<p align="center">
  <strong>Native ABI as a first-class, verifiable object.</strong><br>
  <sub>An ABI semantic layer for describing, identifying, verifying and evolving native binary interfaces.</sub>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-Apache--2.0-blue.svg" alt="License: Apache-2.0"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C.svg" alt="C++17">
  <img src="https://img.shields.io/badge/CMake-3.20%2B-064F8C.svg" alt="CMake 3.20+">
  <img src="https://img.shields.io/badge/platforms-Linux%20%7C%20macOS-4C8C4A.svg" alt="Platforms: Linux | macOS">
  <img src="https://img.shields.io/badge/status-1.0-orange.svg" alt="Status: 1.0">
</p>

<p align="center">
  <a href="README_ZH.md">中文</a> · English
</p>

<details>
<summary>Contents</summary>

- [Why ABIX?](#why-abix)
- [A Small Example](#a-small-example)
- [Native, Not a VM](#native-not-a-vm)
- [The ABIX Model](#the-abix-model)
- [ABIX IR](#abix-ir)
- [AMC](#amc)
- [Self-Hosting](#self-hosting)
- [Architecture](#architecture)
- [What ABIX Is Not](#what-abix-is-not)
- [Project Structure](#project-structure)
- [Getting Started](#getting-started)
- [Documentation](#documentation)
- [Current Status](#current-status)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [Community](#community)
- [License](#license)

</details>

---

ABIX is an **ABI semantic layer** for describing, identifying, verifying and
evolving native binary interfaces. It makes ABI information explicit and
machine-readable, independent of any particular compiler or language
implementation.

```mermaid
graph TD
    A[Native Ecosystem<br>C / C++ / Rust / Zig / ...] --> B[AMC<br>ABI Toolchain]
    B --> C[ABIX IR]
    C --> D[.abix]
    C --> E[Tooling]
    C --> F[Runtime]
    D --> G[ABI verification<br>ABI diff / analysis<br>Code generation]
    F --> H[Native binding<br>Native execution<br>ABI adaptation]
```

ABIX is currently implemented with **C++ as its first host language**. The ABI
model itself is designed to stay language-independent.

---

## Why ABIX?

In native software the ABI is usually an implicit consequence of the compiler,
target platform, calling convention, data layout, CRT, linker, binary format and
language implementation. It exists, but it is hard to inspect, compare, verify
and reuse as an independent artifact.

ABIX makes it explicit:

```mermaid
graph TD
    subgraph Traditional
        A1[Source] --> A2[Compiler] --> A3[Binary] --> A4[ABI exists implicitly]
    end
    subgraph ABIX
        B1[Source / Binary] --> B2[AMC] --> B3[ABIX IR] --> B4[.abix] --> B5[ABI is an explicit object]
    end
```

Once ABI is an object, it can be consumed across the toolchain:

```mermaid
graph TD
    A[ABIX] --> B[Build]
    A --> C[CI]
    A --> D[Runtime]
    B --> E[Package Manager]
    C --> F[ABI Diff Verification]
    D --> G[Native Binding / Adaptation]
    E --> H[Developer Tools]
    F --> H
    G --> H
```

---

## A Small Example

Suppose a library exposes:

```cpp
struct Foo {
    int id;
    double value;
};

int add(int a, int b);
```

The native ABI contains more than the source declaration:

```mermaid
graph TD
    Foo --> F1[size]
    Foo --> F2[alignment]
    Foo --> F3[field offsets]
    Foo --> F4[field types]
    Foo --> F5[layout identity]
    add --> A1[symbol]
    add --> A2[return type]
    add --> A3[parameter types]
    add --> A4[calling convention]
    add --> A5[ABI identity]
```

ABIX records these facts in a machine-readable model, so an ABI change becomes
directly observable instead of a crash after loading a binary (illustrative
output):

```text
$ amc diff foo-v1.abix foo-v2.abix

ABI BREAK

Foo
 └── field: value
      offset: 8 → 16

LayoutHash
  old: 7e...
  new: 91...

Result: incompatible
```

---

## Native, Not a VM

ABIX is not a virtual machine, an RPC framework, or a universal object runtime.
For a compatible native function the intended execution path is:

```mermaid
graph TD
    A[Application] -->|native call| B[Native Binary]
```

ABIX may participate in **discover / verify / identify / bind / adapt**, but it
does not continuously interpret or dispatch compatible calls.

> **Establish the ABI relationship dynamically, then execute through the native ABI.**

---

## The ABIX Model

ABIX is centered on a small set of concepts:

```mermaid
graph TD
    A[ABIX] --> B[ABI Semantic Model]
    B --> C[Type]
    B --> D[Function]
    B --> E[Module]
    C --> F[Layout]
    D --> G[Parameters]
    E --> H[Target / ABI]
    F --> I[Type Identity]
    F --> J[Layout Identity]
```

The model describes existing native binary contracts. It does not replace C++,
Rust or C with a new universal runtime type system.

Type identity is a 128-bit `TypeID`; layout identity is a separate
`LayoutHash`; the module-level artifact identity is the `ABIHash`. See
[ABI identity & compatibility](docs/compatibility.md).

---

## ABIX IR

The ABIX Intermediate Representation is the common representation shared by
language tooling and the runtime:

```mermaid
graph TD
    A[Language AST] --> B[Language Adapter]
    B --> C[ABIX IR]
    C --> D[.abix]
    C --> E[Code generation]
    C --> F[ABI analysis]
    C --> G[Runtime binding]
```

The IR focuses on ABI semantics rather than source-level detail. Its serialized
form is the `.abix` artifact:

* [`docs/abix.md`](docs/abix.md) — the canonical `.abix` artifact
* [`docs/abic.md`](docs/abic.md) — the `.abic.toml` build configuration
* [`docs/metadata_modes.md`](docs/metadata_modes.md) — the three metadata modes

---

## AMC

**AMC — ABI Meta Compiler** is the toolchain entry point.

```mermaid
graph TD
    A[AMC] --> B[Parse]
    A --> C[Generate]
    A --> D[Analyze]
    B --> E[ABIX IR]
    C --> E
    D --> E
```

Typical workflows:

```bash
# Build ABI metadata from sources
amc build -c package.abic.toml -B build

# Inspect / query
amc inspect build/build/package.abix
amc query  build/build/package.abix --type Foo --layout

# Compare and verify
amc diff  v1.abix v2.abix
amc verify -c package.abic.toml -B build
```

AMC is an extensible toolchain, not a language-specific compiler: frontends
extract ABI from a language AST, ABIX IR stays the common representation.
See [`docs/amc.md`](docs/amc.md).

---

## Self-Hosting

ABIX 1.0 is self-hosting for its own public ABI: it describes its public ABI with
its own model, and uses that to build and verify the next version.

```mermaid
graph TD
    A[ABIX 1.0] --> B[Describe itself]
    B --> C[Verify / Bind]
    C --> D[Build next ABI]
    D --> E[ABIX 2.x]
    E -->|describes itself| E
```

This separates the **stable bootstrap ABI** from **internal implementation**
(runtime internals, data structures, synchronization, caches) so the
implementation can evolve while its external contract stays explicit. See
[`docs/self-hosting.md`](docs/self-hosting.md).

---

## Architecture

```mermaid
graph TD
    A[Language Ecosystem<br>C / C++ / Rust / Zig / ...] --> B[AMC<br>ABI Toolchain / Driver]
    B --> C[ABIX IR<br>ABI Semantic Representation]
    C --> D[.abix<br>ABI Artifact]
    C --> E[ABIX Runtime<br>Native Binding]
    D --> F[CI]
    D --> G[Package Manager]
    D --> H[Tools]
    E --> I[Native Binary]
```

The core architectural rule:

> **There should be one source of ABI truth.** The runtime may use a projection
> of the ABI model, but must not independently redefine ABI semantics.

Tooling is split into `libabix-format` / `libabix-abi` / `libabix-metadata` /
`libabix-tools`, plus the header-only `libabix-runtime`, so every consumer
shares exactly one `.abix` / Metadata Region parser.

---

## What ABIX Is Not

| System                   | ABIX's boundary                                          |
| ------------------------ | -------------------------------------------------------- |
| VM / Interpreter         | Compatible calls remain native                           |
| RPC framework            | No network transport is required                         |
| Universal object model   | Does not define a new object universe                    |
| Debug information format | ABI semantics are separate from source/debug information |
| C ABI wrapper            | Does not reduce every interface to `void*`               |
| Compiler replacement     | AMC builds on language/compiler ecosystems               |
| Reflection-only system   | ABI identity and compatibility are first-class           |

ABIX works alongside existing compiler, linker, debugger, build-system and
language ecosystems rather than replacing them.

---

## Project Structure

```text
ABIX
├── ABIX IR            abix/            ABI model + runtime
├── AMC                amc/             ABI toolchain
├── .abix              serialized ABI artifact
├── tests / benchmarks test/ bench/
├── Aue (experimental) aue/             Lua boundary layer + conformance
└── docs/              specification, design, runtime, AMC, benchmarks
```

---

## Getting Started

### Requirements

* C++17 or later
* CMake 3.20+
* LLVM / Clang tooling (for the AMC C++ frontend)
* a supported native toolchain

### Build

```bash
git clone <repository>
cd ABIX
cmake -B build/Release -DCMAKE_BUILD_TYPE=Release -G Ninja -S .
cmake --build build/Release --parallel
```

### Try it

```bash
# Build an .abix artifact from the example config
./build/Release/bin/amc build -c amc/tests/fixtures/amc_test.abic.toml -B build/demo

# Inspect it
./build/Release/bin/amc inspect build/demo/build/amc_test.abix

# Query a type and its layout
./build/Release/bin/amc query build/demo/build/amc_test.abix --type AmcTestFoo --layout

# LLM-friendly ABI context
./build/Release/bin/amc context build/demo/build/amc_test.abix --format llm
```

For the first complete walkthrough see
[`docs/getting-started.md`](docs/getting-started.md).

---

## Documentation

### Start Here

* [Getting Started](docs/getting-started.md)
* [Architecture](.agents/ARCHITECTURE.md)
* [ABIX Specification](.agents/ABI-SPEC.md)
* [`.abix` — Canonical ABI Artifact](docs/abix.md)

### Core Concepts

* [ABI Identity & Compatibility](docs/compatibility.md)
* [`.abic` — ABI Configuration](docs/abic.md)
* [Metadata Modes](docs/metadata_modes.md)
* [Self-Hosting](docs/self-hosting.md)

### Runtime

* [Runtime Overview](docs/runtime.md)
* [API Reference](docs/api.md)
* [Performance & Benchmarks](docs/benchmark.md)

### AMC Toolchain

* [AMC](docs/amc.md)
* [Language Plugins](.agents/LANGUAGE-PLUGIN.md)
* [MCP: ABI Metadata for AI agents](docs/MCP.md)

### Project

* [Roadmap](.agents/ROADMAP.md)
* [Contributing](CONTRIBUTING.md)
* [Agent Instructions](.agents/AGENTS.md)
* [Design Notes](docs/design-notes.md)

---

## Current Status

**ABIX 1.0** — the initial stable ABI model and bootstrap ABI. The project is
under active development; the C++ implementation is the *first* implementation
of the model, not a limitation of it.

Current focus:

* strengthening the ABIX specification
* improving AMC usability
* language / toolchain integration
* ABI compatibility analysis
* documentation and examples
* external validation and adoption

---

## Roadmap

```mermaid
graph LR
    A[ABIX Core] --> B[AMC]
    B --> C[ABI-aware Toolchain]
    C --> D[Native ABI Ecosystem]
    B --> E[C / C++]
    B --> F[Rust]
    B --> G[Zig]
    B --> H[other language frontends]
```

The roadmap prioritises interoperability and real-world usage over adding
runtime features. See [`ROADMAP.md`](.agents/ROADMAP.md).

---

## Contributing

Contributions are welcome: language frontends, ABI extraction, IR design, code
generation, runtime integration, compatibility testing, build-system
integration, documentation, examples and benchmarks.

See [`CONTRIBUTING.md`](CONTRIBUTING.md).

---

## Community

If ABIX is useful to you: star the repository, open issues and discussions,
share experiments and use cases, or contribute documentation and code. External
feedback is especially valuable while the model and toolchain are still
evolving.

---

## License

See [`LICENSE`](LICENSE).
