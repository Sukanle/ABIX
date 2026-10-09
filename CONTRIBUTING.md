# Contributing to ABIX

ABIX is an open-source project and welcomes contributions. This document covers
the practical basics; the design context lives in [`docs/`](docs/).

## Ways to contribute

- **Language frontends** — extract ABI from another language's AST.
- **ABI extraction** — improve coverage and correctness of the C++ frontend.
- **IR design** — sharpen the ABI model and its serialization.
- **Code generation** — projections, bindings and adapters.
- **Runtime integration** — registry, binding, dispatch, adaptation.
- **Compat testing** — real-world ABI break cases.
- **Build-system integration** — CMake/packaging/CI.
- **Documentation and examples** — especially a first walkthrough for your
  language/toolchain.
- **Benchmarks** — repeatable measurements.

If you are experimenting with ABIX in another language or toolchain, prototypes
and write-ups are especially welcome.

## Read before you write

| Document                                           | Answers                                    |
| -------------------------------------------------- | ------------------------------------------ |
| [`ABI-SPEC.md`](.agents/ABI-SPEC.md)               | what a legal ABIX ABI is (normative)       |
| [`ARCHITECTURE.md`](.agents/ARCHITECTURE.md)       | design map, invariants, forbidden patterns |
| [`LANGUAGE-PLUGIN.md`](.agents/LANGUAGE-PLUGIN.md) | adding a language frontend/backend         |
| [`AGENTS.md`](.agents/AGENTS.md)                   | how AI agents work in this repository      |
| [`ROADMAP.md`](.agents/ROADMAP.md)                 | what is in scope now                       |
| [`docs/`](docs/)                                   | detailed implementation knowledge          |

## Change types

Classify your change; it tells you what to update and test.

| Change type     | Examples                                             | Required                                                                                      |
| --------------- | ---------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| Bug fix         | crash, wrong layout, wrong diagnostic                | regression test                                                                               |
| Feature         | new command, new query                               | docs + test                                                                                   |
| **ABI change**  | identity, layout, calling convention, `.abix` format | [`ABI-SPEC.md`](.agents/ABI-SPEC.md), compatibility analysis, regression test, migration note |
| **IR change**   | new record/field, canonical encoding                 | [`ABI-SPEC.md`](.agents/ABI-SPEC.md), serialization + round-trip tests                        |
| Runtime change  | registry, binding, caching, synchronization          | [`ARCHITECTURE.md`](.agents/ARCHITECTURE.md), `docs/runtime.md`, benchmark if hot-path        |
| AMC change      | CLI, extraction, codegen, diagnostics                | `docs/amc.md`, CLI test                                                                       |
| Language plugin | new language frontend/backend                        | [`LANGUAGE-PLUGIN.md`](.agents/LANGUAGE-PLUGIN.md), extraction + round-trip tests             |
| Documentation   | guides, examples                                     | the matching `_zh` edition when user-visible                                                  |
| Performance     | binding, lookup, call path                           | benchmark before/after; must not change ABI semantics                                         |

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

- C++17 (the project builds as C++17; see `CMakeLists.txt`)
- CMake 3.20+
- LLVM / Clang tooling (links `clang-cpp` for the AMC C++ frontend)
- optional: Lua 5.4 (Aue), `readelf`/`strip`/`lldb`/`clang++` (integration tests)

### Locating LLVM (macOS / Homebrew)

`cmake/AbixDependencies.cmake` locates LLVM with
`find_package(LLVM CONFIG)`. On Homebrew, `llvm` is **keg-only**: installing
`llvm@22` creates `/opt/homebrew/opt/llvm@22`, and the unversioned
`/opt/homebrew/opt/llvm` symlink does **not** exist. Point `CMAKE_PREFIX_PATH`
at the keg root:

```bash
cmake -B build/Release -G Ninja -S . \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix llvm@22)"
```

Two constraints are easy to trip over:

- `CMAKE_PREFIX_PATH` must be the keg **root**. `.../llvm@22/lib` and
  `.../llvm@22/lib/cmake/llvm` both fail to resolve.
- `-DLLVM_DIR=...` / `-DClang_DIR=...` have no effect: the dependency module
  clears those cache entries on every configure so it re-detects a changed
  toolchain. Use `CMAKE_PREFIX_PATH`.

If LLVM is not found, `AMC_BUILD` still succeeds but the AMC tools are skipped
with a warning — `amc`, `amc-cpp`, `amc-rust` and the AMC tests are absent from
`build/<type>/bin`.

Avoid exporting `LDFLAGS` / `CPPFLAGS` pointing at an unversioned LLVM prefix.
CMake copies them into `CMAKE_EXE_LINKER_FLAGS` (and the shared/module
variants) on the **first** configure and then reuses the cached value, so a
stale `-L/opt/homebrew/opt/llvm/lib` keeps reaching every link line — the linker
warns `search path ... not found`, and `CPPFLAGS` can shadow the toolchain's own
headers. To repair an existing build directory after changing the environment:

```bash
cmake -B build/Release -S . \
  -U CMAKE_EXE_LINKER_FLAGS \
  -U CMAKE_SHARED_LINKER_FLAGS \
  -U CMAKE_MODULE_LINKER_FLAGS
```

A missing Clang resource directory is a related failure with a different
symptom: `amc-cpp` prints `ABIX_CLANG_RESOURCE_DIR not set at build time` and
cannot find built-in headers, which breaks the self-description steps of
`AMC/tests/amc_integration_test.py`. Clear the stale detection results to let
CMake re-run `clang -print-resource-dir`:

```bash
cmake -B build/Release -S . -U ABIX_CLANG_EXECUTABLE -U ABIX_CLANG_RESOURCE_DIR
```

## Guidelines

- Keep the build green: run `ctest` before opening a pull request.
- Add tests for behaviour changes. The AMC CLI suite is
  `AMC/tests/amc_integration_test.py`; core assertions live in
  `AMC/tests/core_test.cpp`; runtime tests use Catch2 in `test/`.
- Keep ABI semantics in one place: extend `libabix-*` rather than duplicating a
  parser in a consumer.
- Update the relevant `docs/` page (and its `_zh` edition when you can) for
  user-visible changes.
- Prefer small, focused commits with a clear message.

## Style

The repository uses `.clang-format`. Format changed C++ files accordingly; the
existing code favours explicit types, `noexcept` on hot paths and small
headers.

## Reporting issues

When reporting an ABI problem, include:

- the `.abic.toml` and the exported symbols,
- the produced `.abix` (or the binary with its `.abix.metadata` region),
- the expected vs actual behaviour, and tool output (`amc query`,
  `amc verify --format diagnostics`).

## License

By contributing you agree that your contributions are licensed under the
project's [`LICENSE`](LICENSE).
