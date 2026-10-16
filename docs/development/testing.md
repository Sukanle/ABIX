# Testing ABIX and AMC

<p align="center">
  <a href="testing_zh.md">中文</a> · English
</p>

This document is the complete test flow: how to build and run the suites, what
each tier verifies, why it exists, and what a healthy run looks like. It is the
practical companion to [`CONTRIBUTING.md`](../../CONTRIBUTING.md); when a change
touches behaviour, run the tier it belongs to (at minimum `ctest`).

> Rule of the repository: **do not claim a test passed unless it was actually
> run.** The expected strings below are the evidence to look for.

<details>

<summary>Contents</summary>

- [Build and run](#build-and-run)
- [Expected result](#expected-result)
- [Test tiers](#test-tiers)
  - [A. Runtime & unit (Catch2, `test/`)](#a-runtime--unit-catch2-test)
  - [B. AMC unit (`amc-core-test`)](#b-amc-unit-amc-core-test)
  - [C. AMC end-to-end (`amc-e2e-test`)](#c-amc-end-to-end-amc-e2e-test)
  - [D. AMC CLI integration (`amc-integration-test`)](#d-amc-cli-integration-amc-integration-test)
  - [E. AMC feature set (`amc-feature-test`)](#e-amc-feature-set-amc-feature-test)
  - [F. Rust/Zig interop (`amc-interop-test`)](#f-rustzig-interop-amc-interop-test)
  - [G. Rust provider conformance (`amc-rust-test`)](#g-rust-provider-conformance-amc-rust-test)
  - [H. cmdly interop (`amc-cmdly-interop-test`)](#h-cmdly-interop-amc-cmdly-interop-test)
  - [I. expose interop (`amc-expose-interop-test`)](#i-expose-interop-amc-expose-interop-test)
  - [J. CMake integration (`amc-cmake-integration-test`)](#j-cmake-integration-amc-cmake-integration-test)
- [Manual feature walkthrough and generated files](#manual-feature-walkthrough-and-generated-files)
- [Native binding (Rust) walkthrough](#native-binding-rust-walkthrough)
- [Sanitizers and platform notes](#sanitizers-and-platform-notes)
- [Reading a failure](#reading-a-failure)

</details>

## Build and run

```bash
# Release build
cmake -B build/Release -DCMAKE_BUILD_TYPE=Release -G Ninja -S .
cmake --build build/Release --parallel

# Run everything
ctest --test-dir build/Release --output-on-failure

# Run one tier
ctest --test-dir build/Release -R amc-feature-test --output-on-failure
```

On Windows use the MSYS2 UCRT64 toolchain (see
[`CONTRIBUTING.md#windows-msys2-ucrt64`](../../CONTRIBUTING.md#windows-msys2-ucrt64));
binaries are `*.exe`, the test scripts resolve `amc.exe` automatically, and
public plugins are `*.dll` next to the executable.

## Expected result

A healthy checkout passes **all 134 registered tests**. The named tiers and
their summary lines:

| # | Test | Summary line |
|---|------|--------------|
| 1 | `amc-core-test` | `amc-core-test: all checks passed` |
| 2 | `amc-e2e-test` | `=== Results: N passed, 0 failed ===` |
| 3 | `amc-integration-test` | `=== Results: N passed, 0 failed ===` |
| 4 | `amc-feature-test` | `AMC feature set: N passed, 0 failed, 0 skipped` |
| 5 | `amc-cmake-integration-test` | `PASS` (or a `[SKIP]` with a reason) |
| 6 | `amc-rust-test` | `N passed, 0 failed` |
| 7 | `amc-interop-test` | `interop: N passed, 0 failed` |
| 8 | `amc-cmdly-interop-test` | `PASS` (or `[SKIP]`) |
| 9 | `amc-expose-interop-test` | `PASS` (or `[SKIP]`) |
| 10+ | Catch2 cases (`test/`) | `All tests passed (N assertions in M test cases)` |

Tiers 5/8/9 print `[SKIP]` (still passing) when their optional toolchain is
absent (`rustc`, `fmt`/`cmdly`). Everything else must be green.

## Test tiers

Each tier names what it exercises (**功能**), why it exists (**目的**) and the
observable success (**预期现象**).

### A. Runtime & unit (Catch2, `test/`)

- **功能** — the header-only ABI runtime, exercised against real shared
  libraries: DLL load/resolve/call through the name+sig table, hot reload,
  RCU unload/timeout/zombie lifecycle, logging, edge/calling-convention cases,
  cross-compiler and cross-CRT variants, MICS integration, the non-intrusive
  **Interface** layer (`interface_dispatch`, `interface_module`, `owner_abi`,
  `interface_dll`, `owner_dll`), `atomic`, and the `BinaryImage` capability layer
  (ELF/Mach-O/PE region + export parsing).
- **目的** — prove the runtime keeps execution native (no dispatcher), that the
  lifetime rules hold (a lease/owner handle keeps its module mapped), and that
  layout/hash/dispatch never silently regress.
- **预期现象** — every suite reports `All tests passed (N assertions in M test
  cases)`. Notable suites: `interface_dll_test` includes the lease-pinning case
  ("a lease keeps the module mapped across unload"), `interface_module_test`
  includes the multithreaded register/unregister and lease/unload stress.

### B. AMC unit (`amc-core-test`)

- **功能** — the AMC core library directly: type/symbol hashing, layout,
  `validate`, `.abix` read/write round-trip, the Metadata Region, the
  instantiation cache, the string/JSON helpers, and the binary reader
  (ELF/Mach-O/PE sections and the CodeView build id).
- **目的** — keep the format/verification core correct independently of the CLI
  and of any host toolchain.
- **预期现象** — `amc-core-test: all checks passed`.

### C. AMC end-to-end (`amc-e2e-test`)

- **功能** — `.abix` generate → serialize → deserialize → compare →
  compatibility, the v4 section directory (including the interface sections),
  symbol projection and backend projection generation.
- **目的** — prove the IR round-trips byte-exactly and that the optional sections
  are forward/backward compatible.
- **预期现象** — `=== Results: N passed, 0 failed ===` (currently 366 passed).

### D. AMC CLI integration (`amc-integration-test`)

- **功能** — drives the real CLI end-to-end: `frontend`, `validate`, `inspect`,
  `generate` (cpp/lua), a compile check of the generated header, `build`,
  `dump`, `diff`, `compatibility`, self-description bootstrap, the provider IPC
  handshake and capability discovery, `context`, `verify` (consistent + drift),
  the unified JSON error envelope, the metadata region (emit/verify/tamper),
  `query`, the symbol server, the region-to-module path, and the adapter /
  ABI-check / shim flows.
- **目的** — the CLI contract is what users and CI depend on; this is the closest
  test to "what a build server runs".
- **预期现象** — `=== Results: N passed, 0 failed ===` (currently 90). Steps that
  need an ELF/Mach-O-only tool (`readelf`, LLDB, `dlopen` shims) print `[INFO]
  ... skipping` on Windows and do not fail.

### E. AMC feature set (`amc-feature-test`)

A cross-platform test (`AMC/tests/cli/feature_test.py`) that asserts the five
AMC capabilities directly:

| Aspect | Checks |
|--------|--------|
| Front/back separation | `--list-languages` (cpp/lua/rust/zig); `--describe-language` (`cpp`/`rust` = frontend+backend, `zig` = backend, all `protocol=jsonl-v1`); `amc-cpp`/`amc-rust` `--ipc` **READY + CAPABILITIES** handshake |
| Code parsing | `amc build` parses **C++** (`amc-cpp`) and **Rust** (`amc-rust`, a built-in frontend — no `rustc` needed) into `.abix` |
| IR generation | `validate`, `inspect`, `query --layout`, `context --format json` |
| Native language binding | one `.abix` → `generate -l cpp\|zig\|rust\|lua`; the **C++** and **Rust** bindings are compiled |
| IR reuse | the same `.abix` drives all four backends |

- **目的** — a single, fast, toolchain-light assertion of the AMC value
  proposition: one IR, many languages, clean front/back split.
- **预期现象** — `AMC feature set: 19 passed, 0 failed, 0 skipped` (the Rust
  compile check is `[SKIP]`ped when `rustc` is absent, so `skipped` may be 1).

### F. Rust/Zig interop (`amc-interop-test`)

- **功能** — the full cross-language loop: build a native C-ABI shared library
  (`demo_lib.cpp`), `amc build` → `.abix`, `amc generate -l rust` and `-l zig`,
  then compile and **run** a Rust consumer and a Zig consumer that link the
  native library and call it through the generated bindings.
- **目的** — the proof that `.abix` is a reusable IR and that the projections are
  usable native bindings, not just text.
- **预期现象** — `interop: 13 passed, 0 failed`, with consumers printing
  `interop ok: ...` / `zig interop ok`. It prints `[SKIP]` if `rustc`/`clang++`
  are missing or the platform is unsupported.

### G. Rust provider conformance (`amc-rust-test`)

- **功能** — the Rust frontend (source-level `#[repr(C)]` / `extern "C"`
  extraction) and backend (ABIX → Rust `repr(C)` projection): advertised JSONL
  protocol, artifact production, projection compilation, and **determinism**
  (identical inputs → byte-identical artifacts).
- **目的** — a language plugin must be reproducible and self-consistent.
- **预期现象** — `16 passed, 0 failed`.

### H. cmdly interop (`amc-cmdly-interop-test`)

- **功能** — a real C++ library (`cmdly`) over the ABIX facade, with a Rust
  consumer; exercises a non-trivial type through the expose → thunk → manifest →
  registry → binding loop.
- **目的** — validate the end-to-end story on a realistic library rather than a
  fixture.
- **预期现象** — `PASS`, or `[SKIP]` when `rustc`/`fmt`/`cmdly` are unavailable.

### I. expose interop (`amc-expose-interop-test`)

- **功能** — automatic capability generation: `amc build` emits thunks, a
  discovery manifest and a Rust binding for an exposed C++ class, then a Rust
  consumer creates/calls/destroys the object across the module boundary.
- **目的** — prove the "no hand-written adapter" path (`.abic.toml` →
  capabilities) works.
- **预期现象** — `PASS`, or `[SKIP]` when `rustc` is unavailable.

### J. CMake integration (`amc-cmake-integration-test`)

- **功能** — `amc build` driven through CMake (a real consumer project), the CLI
  contract and the generated artifacts.
- **目的** — the toolchain integrates with the build system users actually use.
- **预期现象** — `PASS`, or `[SKIP]` with a reason.

## Manual feature walkthrough and generated files

The suites generate into a temporary directory and clean up. To inspect the
artifacts, run the steps by hand:

```bash
AMC=build/Release/bin/amc.exe           # drop .exe on Linux/macOS

# parse source -> IR
"$AMC" build -c AMC/tests/fixtures/amc_test.abic.toml -B build/amc-demo

# one IR -> four language bindings
ABIX=build/amc-demo/build/amc_test.abix
"$AMC" generate "$ABIX" -l cpp  -o build/amc-demo/bindings/cpp
"$AMC" generate "$ABIX" -l rust -o build/amc-demo/bindings/rust
"$AMC" generate "$ABIX" -l zig  -o build/amc-demo/bindings/zig
"$AMC" generate "$ABIX" -l lua  -o build/amc-demo/bindings/lua
```

| Backend | `-l` | Produced file(s) |
|---------|------|------------------|
| IR | `amc build` | `build/amc-demo/build/amc_test.abix` (+ `.abix.meta`) |
| C++ | `cpp` | `bindings/cpp/amc_generated.hpp`, `bindings/cpp/amc_abi_check.hpp` |
| Rust | `rust` | `bindings/rust/amc_generated.rs` |
| Zig | `zig` | `bindings/zig/amc_generated.zig` |
| Lua | `lua` | `bindings/lua/amc_lua_contract.hpp` |

`-o` accepts a directory (the file name is fixed as above) or an explicit file
path. Fixtures: C++ `AMC/tests/fixtures/amc_test.abic.toml`, Rust frontend
`AMC/tests/fixtures/rust_demo.abic.toml`.

## Native binding (Rust) walkthrough

The interop test performs exactly this in a temp directory:

```bash
# native C-ABI library (Windows also emits the import library rustc links against)
clang++ -std=c++17 -shared -O2 AMC/tests/interop/demo_lib.cpp \
    -o /tmp/interop_demo.dll -Wl,--out-implib,/tmp/libinterop_demo.dll.a
# Linux: clang++ -std=c++17 -shared -fPIC -O2 ... -o /tmp/libinterop_demo.so

amc build -c AMC/tests/interop/interop_demo.abic.toml -B /tmp/abix     # -> /tmp/abix/interop_demo.abix
amc generate /tmp/abix/interop_demo.abix -l rust -o /tmp/gen           # -> /tmp/gen/amc_generated.rs

# consumer.rs includes the projection via #[path = "gen/amc_generated.rs"]
rustc --edition 2021 -O /tmp/consumer.rs --cfg abix_link -L /tmp -o /tmp/consumer
/tmp/consumer
```

`--cfg abix_link` turns on the projection's `#[link(name = "interop_demo")]`, so
the symbols bind to the native library. Expected final line:
`interop ok: add=5 scale=13 div=5 err=1 types=31`. The consumer's assert_eq!s are
the real assertions (layout, ownership, error boundary, ABIX-STD, Interface).

## Sanitizers and platform notes

- **Sanitizers** — Debug builds accept `-DSANITIZER=asan` / `-DSANITIZER=tsan`
  (Linux/macOS only; disabled on Windows). Run the concurrency suites
  (`interface_module_test`, `concurrency_test`) under TSan to catch data races.
- **Windows** — MSYS2 UCRT64 (GCC or Clang). Tests are `*.exe` and run with the
  working directory at `build/<type>/bin`, so plugins (`*.dll`) and staged
  cross-toolchain variants (`bin/variants/...`) resolve. Cross-CRT coverage needs
  `tools/build_variants.py --with-msvc` (builds the MSVC `*_dll` variants);
  `13.cross_crt_msvc_resource` and `13b.owner and interface across a cross-CRT
  MSVC DLL` then run for real instead of skipping.
- **ELF/Mach-O-only steps** (`readelf`/`objdump`/`otool`, LLDB, `dlopen` shims)
  skip on Windows and are covered on Linux/macOS.

## Reading a failure

- Re-run the single tier with `--output-on-failure`, or run the script/exe
  directly (see the [build and run](#build-and-run) commands).
- The tiers are ordered so a failure points at one layer: `amc-core-test`
  (core) → `amc-e2e-test` (IR round-trip) → `amc-feature-test` (parsing/IR/
  binding/reuse/front-back) → `amc-interop-test` (cross-language) →
  `amc-integration-test` (CLI).
- A `[SKIP]` is not a pass by accident: it means the optional toolchain is
  absent. Install it (`rustc`, `zig`, `lldb`) to exercise that tier.
