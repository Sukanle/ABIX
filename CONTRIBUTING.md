# Contributing to ABIX

ABIX is an open-source project and welcomes contributions. This document covers
the practical basics; the design context lives in [`docs/`](docs/).

## Ways to contribute

* **Language frontends** — extract ABI from another language's AST.
* **ABI extraction** — improve coverage and correctness of the C++ frontend.
* **IR design** — sharpen the ABI model and its serialization.
* **Code generation** — projections, bindings and adapters.
* **Runtime integration** — registry, binding, dispatch, adaptation.
* **Compatibility testing** — real-world ABI break cases.
* **Build-system integration** — CMake/packaging/CI.
* **Documentation and examples** — especially a first walkthrough for your
  language/toolchain.
* **Benchmarks** — repeatable measurements.

If you are experimenting with ABIX in another language or toolchain, prototypes
and write-ups are especially welcome.

## Read before you write

| Document | Answers |
|----------|---------|
| [`ABI-SPEC.md`](.agents/ABI-SPEC.md) | what a legal ABIX ABI is (normative) |
| [`ARCHITECTURE.md`](.agents/ARCHITECTURE.md) | design map, invariants, forbidden patterns |
| [`LANGUAGE-PLUGIN.md`](.agents/LANGUAGE-PLUGIN.md) | adding a language frontend/backend |
| [`AGENTS.md`](.agents/AGENTS.md) | how AI agents work in this repository |
| [`ROADMAP.md`](.agents/ROADMAP.md) | what is in scope now |
| [`docs/`](docs/) | detailed implementation knowledge |

## Change types

Classify your change; it tells you what to update and test.

| Change type | Examples | Required |
|-------------|----------|----------|
| Bug fix | crash, wrong layout, wrong diagnostic | regression test |
| Feature | new command, new query | docs + test |
| **ABI change** | identity, layout, calling convention, `.abix` format | [`ABI-SPEC.md`](.agents/ABI-SPEC.md), compatibility analysis, regression test, migration note |
| **IR change** | new record/field, canonical encoding | [`ABI-SPEC.md`](.agents/ABI-SPEC.md), serialization + round-trip tests |
| Runtime change | registry, binding, caching, synchronization | [`ARCHITECTURE.md`](.agents/ARCHITECTURE.md), `docs/runtime.md`, benchmark if hot-path |
| AMC change | CLI, extraction, codegen, diagnostics | `docs/amc.md`, CLI test |
| Language plugin | new language frontend/backend | [`LANGUAGE-PLUGIN.md`](.agents/LANGUAGE-PLUGIN.md), extraction + round-trip tests |
| Documentation | guides, examples | the matching `_zh` edition when user-visible |
| Performance | binding, lookup, call path | benchmark before/after; must not change ABI semantics |

Workflow: **understand → design (classify the change) → implement → test →
ABI verification → documentation → pull request.**

If your change conflicts with an architectural invariant, explain the conflict
before changing the constraint — do not silently relax it.

## Development setup

```bash
git clone <repository>
cd ABIX
cmake -B build/Release -DCMAKE_BUILD_TYPE=Release -G Ninja -S .
cmake --build build/Release --parallel
ctest --test-dir build/Release --output-on-failure
```

Requirements:

* C++17 (the project builds as C++17; see `CMakeLists.txt`)
* CMake 3.20+
* LLVM / Clang tooling (links `clang-cpp` for the AMC C++ frontend)
* optional: Lua 5.4 (Aue), `readelf`/`strip`/`lldb`/`clang++` (integration tests)

## Guidelines

* Keep the build green: run `ctest` before opening a pull request.
* Add tests for behaviour changes. The AMC CLI suite is
  `amc/tests/amc_integration_test.py`; core assertions live in
  `amc/tests/core_test.cpp`; runtime tests use Catch2 in `test/`.
* Keep ABI semantics in one place: extend `libabix-*` rather than duplicating a
  parser in a consumer.
* Update the relevant `docs/` page (and its `_zh` edition when you can) for
  user-visible changes.
* Prefer small, focused commits with a clear message.

## Style

The repository uses `.clang-format`. Format changed C++ files accordingly; the
existing code favours explicit types, `noexcept` on hot paths and small
headers.

## Reporting issues

When reporting an ABI problem, include:

* the `.abic.toml` and the exported symbols,
* the produced `.abix` (or the binary with its `.abix.metadata` region),
* the expected vs actual behaviour, and tool output (`amc query`,
  `amc verify --format diagnostics`).

## License

By contributing you agree that your contributions are licensed under the
project's [`LICENSE`](LICENSE).
