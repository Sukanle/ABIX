# Testing ABIX and AMC

<p align="center">
  <a href="testing_zh.md">中文</a> · English
</p>

This document is the complete test flow: how to build and run the suites, what each tier verifies, why it exists, and what a healthy run looks like. It is the practical companion to [`CONTRIBUTING.md`](../../CONTRIBUTING.md); when a change touches behaviour, run the tier it belongs to (at minimum `ctest`).

> Rule of the repository: **do not claim a test passed unless it was actually run.** The expected strings below are the evidence to look for.

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
- [Keeping the temporary artifacts](#keeping-the-temporary-artifacts)
- [Tool scripts (`tools/`)](#tool-scripts-tools)
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

On Windows use the MSYS2 UCRT64 toolchain (see [`CONTRIBUTING.md#windows-msys2-ucrt64`](../../CONTRIBUTING.md#windows-msys2-ucrt64)).

Binaries are `*.exe`, the test scripts resolve `amc.exe` automatically, and public plugins are `*.dll` next to the executable.

On macOS the Homebrew LLVM/Clang formulae are keg-only and are not on CMake's default search path, so point it at the prefix explicitly:

```bash
# use the installed version (see `brew list --versions llvm`)
cmake -B build/Release -DCMAKE_BUILD_TYPE=Release -G Ninja -S . \
    -DCMAKE_PREFIX_PATH="$(brew --prefix llvm)"
```

Substitute the versioned formula you have. Without it CMake reports `LLVM / Clang not found — AMC ... will not be built` and the AMC targets are skipped, so most of the named tiers are absent from `ctest`.

## Expected result

A healthy checkout passes **all 138 registered tests**. The named tiers and their summary lines:

| #   | Test                         | Summary line                                      |
| --- | ---------------------------- | ------------------------------------------------- |
| 1   | `amc-core-test`              | `amc-core-test: all checks passed`                |
| 2   | `amc-e2e-test`               | `=== Results: N passed, 0 failed ===`             |
| 3   | `amc-integration-test`       | `=== Results: N passed, 0 failed ===`             |
| 4   | `amc-feature-test`           | `AMC feature set: N passed, 0 failed, 0 skipped`  |
| 5   | `amc-cmake-integration-test` | `PASS` (or a `[SKIP]` with a reason)              |
| 6   | `amc-rust-test`              | `N passed, 0 failed`                              |
| 7   | `amc-interop-test`           | `interop: N passed, 0 failed`                     |
| 8   | `amc-cmdly-interop-test`     | `PASS` (or `[SKIP]`)                              |
| 9   | `amc-expose-interop-test`    | `PASS` (or `[SKIP]`)                              |
| 10+ | Catch2 cases (`test/`)       | `All tests passed (N assertions in M test cases)` |

Tiers 5/8/9 print `[SKIP]` (still passing) when their optional toolchain is absent (`rustc`, `fmt`/`cmdly`). Everything else must be green.

## Test tiers

Each tier names what it exercises (**Functionality**), why it exists (**Purpose**) and the observable success (**Expected result**).

### A. Runtime & unit (Catch2, `test/`)

- **Functionality** — the header-only ABI runtime, exercised against real shared libraries:
    - DLL load/resolve/call through the name+sig table;
    - hot reload; RCU unload/timeout/zombie lifecycle; logging;
    - edge/calling-convention cases;
    - cross-compiler and cross-CRT variants;
    - MICS integration;
    - the non-intrusive **Interface** layer;
    - `atomic`, and the `BinaryImage` capability layer (ELF/Mach-O/PE region + export parsing).
- **Purpose** — prove the runtime keeps execution native (no dispatcher), that the lifetime rules hold (a lease/owner handle keeps its module mapped), and that layout/hash/dispatch never silently regress.
- **Expected result** — every suite reports `All tests passed (N assertions in M test cases)`.
    - Notable suites:
        - `interface_dll_test` includes the lease-pinning case ("a lease keeps the module mapped across unload");
        - `interface_module_test` includes the multithreaded register/unregister and lease/unload stress.

### B. AMC unit (`amc-core-test`)

- **Functionality** — the AMC core library directly:
    - type/symbol hashing, layout;
    - `validate`, `.abix` read/write round-trip;
    - the Metadata Region;
    - the instantiation cache;
    - the string/JSON helpers;
    - the binary reader (ELF/Mach-O/PE sections and the CodeView build id).
- **Purpose** — keep the format/verification core correct independently of the CLI and of any host toolchain.
- **Expected result** — `amc-core-test: all checks passed`.

### C. AMC end-to-end (`amc-e2e-test`)

- **Functionality** — `.abix` generate → serialize → deserialize → compare → compatibility, the v4 section directory (including the interface sections), symbol projection and backend projection generation.
- **Purpose** — prove the IR round-trips byte-exactly and that the optional sections are forward/backward compatible.
- **Expected result** — `=== Results: N passed, 0 failed ===` (currently 366 passed).

### D. AMC CLI integration (`amc-integration-test`)

- **Functionality** — drives the real CLI end-to-end:
    - `frontend`, `validate`, `inspect`, `generate` (cpp/lua);
    - a compile check of the generated header;
    - `build`, `dump`, `diff`, `compatibility`;
    - self-description bootstrap;
    - the provider IPC handshake and capability discovery;
    - `context`, `verify` (consistent + drift);
    - the unified JSON error envelope;
    - the metadata region (emit/verify/tamper);
    - `query`, the symbol server;
    - the region-to-module path;
    - the adapter / ABI-check / shim flows.
- **Purpose** — the CLI contract is what users and CI depend on; this is the closest test to "what a build server runs".
- **Expected result** — `=== Results: N passed, 0 failed ===` (currently 106). Steps that need an ELF/Mach-O-only tool (`readelf`, LLDB, `dlopen` shims) print `[INFO] ... skipping` on Windows and do not fail.

### E. AMC feature set (`amc-feature-test`)

A cross-platform test (`AMC/tests/cli/feature_test.py`) that asserts the five AMC capabilities directly:

| Aspect                  | Checks                                                                                                                                                                                                    |
| ----------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Front/back separation   | `--list-languages`/`--describe-language` from `AMC/providers.toml` (cpp/lua/rust/zig); `AMC_PROVIDERS_FILE` override; `amc-cpp`/`amc-rust` `--ipc` **READY + CAPABILITIES** and `abix.plugin/1` envelope |
| Code parsing            | `amc build` parses **C++** (`amc-cpp`) and **Rust** (`amc-rust`, a built-in frontend — no `rustc` needed) into `.abix`                                                                                    |
| IR generation           | `validate`, `inspect`, `query --layout`, `context --format json`                                                                                                                                          |
| Native language binding | one `.abix` → `generate -l cpp\|zig\|rust\|lua`; the **C++** and **Rust** bindings are compiled                                                                                                           |
| IR reuse                | the same `.abix` drives all four backends                                                                                                                                                                 |

- **Purpose** — a single, fast, toolchain-light assertion of the AMC value proposition: one IR, many languages, clean front/back split.
- **Expected result** — `AMC feature set: 26 passed, 0 failed, 0 skipped` (the Rust compile check is `[SKIP]`ped when `rustc` is absent, so `skipped` may be 1).

### F. Rust/Zig interop (`amc-interop-test`)

- **Functionality** — the full cross-language loop: build a native C-ABI shared library (`demo_lib.cpp`), `amc build` → `.abix`, `amc generate -l rust` and `-l zig`, then compile and **run** a Rust consumer and a Zig consumer that link the native library and call it through the generated bindings.
- **Purpose** — the proof that `.abix` is a reusable IR and that the projections are usable native bindings, not just text.
- **Expected result** — `interop: 16 passed, 0 failed`, with consumers printing `cpp interop ok` / `interop ok: ...` / `zig interop ok`. It prints `[SKIP]` if `rustc`/`clang++` are missing or the platform is unsupported.

### G. Rust provider conformance (`amc-rust-test`)

- **Functionality** — the Rust frontend (source-level `#[repr(C)]` / `extern "C"` extraction) and backend (ABIX → Rust `repr(C)` projection): advertised JSONL protocol, artifact production, projection compilation, and **determinism** (identical inputs → byte-identical artifacts).
- **Purpose** — a language plugin must be reproducible and self-consistent.
- **Expected result** — `18 passed, 0 failed`.

### H. cmdly interop (`amc-cmdly-interop-test`)

- **Functionality** — a real C++ library (`cmdly`) over the ABIX facade, with a Rust consumer; exercises a non-trivial type through the expose → thunk → manifest → registry → binding loop.
- **Purpose** — validate the end-to-end story on a realistic library rather than a fixture.
- **Expected result** — `PASS`, or `[SKIP]` when `rustc`/`fmt`/`cmdly` are unavailable.

### I. expose interop (`amc-expose-interop-test`)

- **Functionality** — automatic capability generation: `amc build` emits thunks, a discovery manifest and a Rust binding for an exposed C++ class, then a Rust consumer creates/calls/destroys the object across the module boundary.
- **Purpose** — prove the "no hand-written adapter" path (`.abic.toml` → capabilities) works.
- **Expected result** — `PASS`, or `[SKIP]` when `rustc` is unavailable.

### J. CMake integration (`amc-cmake-integration-test`)

- **Functionality** — `amc build` driven through CMake (a real consumer project), the CLI contract and the generated artifacts.
- **Purpose** — the toolchain integrates with the build system users actually use.
- **Expected result** — `PASS`, or `[SKIP]` with a reason.

## Manual feature walkthrough and generated files

The suites generate into a temporary directory and clean up. To inspect the artifacts, either run the steps by hand or keep the directories with [`SKL_ABIX_KEEP_TEST_ARTIFACTS`](#keeping-the-temporary-artifacts):

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

| Backend | `-l`        | Produced file(s)                                                   |
| ------- | ----------- | ------------------------------------------------------------------ |
| IR      | `amc build` | `build/amc-demo/build/amc_test.abix` (+ `.abix.meta`)              |
| C++     | `cpp`       | `bindings/cpp/amc_generated.hpp`, `bindings/cpp/amc_abi_check.hpp` |
| Rust    | `rust`      | `bindings/rust/amc_generated.rs`                                   |
| Zig     | `zig`       | `bindings/zig/amc_generated.zig`                                   |
| Lua     | `lua`       | `bindings/lua/amc_lua_contract.hpp`                                |

`-o` accepts a directory (the file name is fixed as above) or an explicit file path.

Fixtures:

- C++ `AMC/tests/fixtures/amc_test.abic.toml`;
- Rust frontend `AMC/tests/fixtures/rust_demo.abic.toml`.

## Native binding (Rust) walkthrough

The interop test performs exactly this in a temp directory:

```bash
# native C-ABI library (Windows also emits the import library rustc links against)
clang++ -std=c++17 -shared -O2 AMC/tests/interop/demo_lib.cpp \
    -o /tmp/interop_demo.dll -Wl,--out-implib,/tmp/libinterop_demo.dll.a
# Linux: clang++ -std=c++17 -shared -fPIC -O2 ... -o /tmp/libinterop_demo.so

amc build -c AMC/tests/interop/interop_demo.abic.toml -B /tmp/abix     # -> /tmp/abix/interop_demo.abix
amc generate /tmp/abix/interop_demo.abix -l rust -o /tmp/gen           # -> /tmp/gen/amc_generated.rs

# rust_consumer.rs includes the projection via #[path = "gen/amc_generated.rs"]
rustc --edition 2021 -O /tmp/rust_consumer.rs --cfg abix_link -L /tmp -o /tmp/rust_consumer
/tmp/rust_consumer
```

`--cfg abix_link` turns on the projection's `#[link(name = "interop_demo")]`, so the symbols bind to the native library. Expected final line: `interop ok: add=5 scale=13 div=5 err=1 types=31`. The consumer's assert_eq!s are the real assertions (layout, ownership, error boundary, ABIX-STD, Interface).

## Keeping the temporary artifacts

Every suite that generates into a temporary directory deletes it when it finishes, so a green run leaves nothing to inspect. Configure with `-DSKL_ABIX_KEEP_TEST_ARTIFACTS=ON` to run them with `KEEP_ARTIFACTS=1` instead: each suite then keeps its working directory and prints the path.

```bash
cmake -B build/Release -DSKL_ABIX_KEEP_TEST_ARTIFACTS=ON -G Ninja -S .
cmake --build build/Release --parallel
ctest --test-dir build/Release -R amc- --output-on-failure
```

```
  [KEEP] retained artifacts: C:/Users/.../Temp/amc-feature.a1b2c3
```

The same switch works for one run without reconfiguring, either through the environment variable or the flag every suite accepts:

```bash
KEEP_ARTIFACTS=1 python AMC/tests/cli/feature_test.py build/Release/bin .
python AMC/tests/interop/interop_test.py build/Release/bin . --keep-artifacts
```

Retained locations (all under the platform temp directory):

| Suite                        | Retained                                                                |
| ---------------------------- | ----------------------------------------------------------------------- |
| `amc-integration-test`       | `amc-integration.*` (`autogen-separate/`, `autogen-full/`)              |
| `amc-feature-test`           | `amc-feature.*` (`.abix`, the generated `cpp/rust/zig/lua` projections) |
| `amc-cmake-integration-test` | `amc-cmake.*` (the configured CMake fixture build tree)                 |
| `amc-rust-test`              | `amc-rust.*` (the provider artifacts and projections)                   |
| `amc-interop-test`           | `amc-interop.*` (native library, consumers, `.abix`)                    |
| `amc-cmdly-interop-test`     | `amc-cmdly.*`                                                           |
| `amc-expose-interop-test`    | `amc-expose.*` (thunks, manifest, bindings)                             |
| `amc-e2e-test`               | `amc_e2e_*` files                                                       |
| `amc-core-test`              | `amc_cache_test_*`, `amc_symbol_test_store`                             |

Retain deliberately: the directories are not removed afterwards, so clean them up once you are done inspecting.

The switch only reaches the suites registered in `ctest` (CMake injects `KEEP_ARTIFACTS=1` for them). The `tools/` scripts are not registered and carry their own keep switches (`--keep`, `--keep-artifacts`, `KEEP_ARTIFACTS=1`); see the next section.

## Tool scripts (`tools/`)

These scripts are **not registered in `ctest`** and are run by hand. Only the second group produces test verdicts.

### Build and code-generation helpers

| Script                | What                                                                                                                                                                                                                                                                | Purpose                                                                                                                                                                                                                                | Expected                                                                                                                                   |
| --------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| `run.py`              | Configures and builds the CMake project, delegates to `build_variants.py`, then runs `test_all` and `bench_all` from `build/<type>/bin/` as requested                                                                                                               | One command for the everyday "build -> variants -> test -> bench" loop                                                                                                                                                                 | `== Build complete ==` -> `== Running variant build: ... ==` -> `== Running tests ==` (green `test_all`) -> `== Running benchmarks ==`     |
| `build_variants.py`   | Compiles the plugin DLL variants with `g++` (`tool_x`) / `clang++` (`tool_y`) into `build/<type>/bin/variants/<tag>/`; with `--with-msvc` on Windows it also emits `version_dll` / `resource_dll` / `interface_dll` / `owner_dll` through `cl.exe` (tag `msvc_x64`) | Turns "cross-compiler / cross-CRT" into an executable case rather than a claim; these are what `2.cross_compiler_variant`, `12.socket_resource_lifecycle`, `13.cross_crt_msvc_resource` and `13b.…across a cross-CRT MSVC DLL` resolve | `== [<tag>] <compiler> ==` plus `Generated: <path>` per DLL, ending in `== Variant build complete ==`; the main build must already be done |
| `gen_hotcache_dll.py` | Generates `dlls/hotcache_dll.cpp` (1000 regular plus 20 hot exports registered through `SKL_ABIX_DEFINE_TABLE`)                                                                                                                                                     | Provides a stable sample for tables past `SKL_ABIX_HASH_THRESHOLD` instead of hand-writing one                                                                                                                                         | `generated .../dlls/hotcache_dll.cpp: 1000 regular + 20 hot functions`. It is a code generator and produces no test verdict                |

```bash
python tools/run.py                          # build + tests + benchmarks
python tools/run.py --build-only             # build only (delegates to run_build.py)
python tools/run_build.py --clean            # build stage alone, clean first
python tools/run.py --with-msvc --vs-path "C:/Program Files/Microsoft Visual Studio/2022/Community"
python tools/build_variants.py --with-msvc   # rebuild the cross-compiler/MSVC variants alone
```

`run.py` picks the stages with `--build-only` / `--test-only` / `--bench-only` / `--build-test` / `--build-bench` and forwards the remaining flags to the stage scripts (`run_build.py`, `run_test.py`, `run_bench.py`). Build options: `--clean`, `--build-type`, `--generator`, `--with-dev`, `--skip-variants`, `--skip-hotcache`; `--cmake-env KEY=VALUE` and `--cmake-var KEY=VALUE` are repeatable and forward environment variables to, and `-DKEY=VALUE` cache variables through, the CMake configure/build. Test options: `--catch2` / `--elf-pe` / `--bootstrap` / `--interop` (see [test tiers](#test-tiers)). The benchmark stage delegates to `run_bench.py` (one process per family), tuned by `--bench-repetitions`, `--bench-out` and `--bench-min-time`; `--bench-raw` restores the old single-process `bench_all` run. It runs the aggregate executables, so it is **not** a substitute for `ctest`.

### Tests and verification

| Script                  | What                                                                                                                                                                                                                                                                                                         | Purpose                                                                                                                                                                     | Expected                                                                                                                                                                                           |
| ----------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `run_test.py --bootstrap` | Verifies AMC's metadata bootstrap for its own core IR: `amc_add_abi` (`AMC.cmake`) attaches `amc/self.abic.toml` to the `AMC/tests/consumers` CMake build graph, so one `cmake --build` runs `amc build` -> `validate` -> `generate` -> compiles and runs the consumer that reads types back through `RuntimeRegistry::type_of<T>()` in the generated C++ projection | Shows the compiled AMC stage can describe its core model and consume its projection (not compiler-source self-hosting: `amc-cpp` still uses Clang to extract C++ semantics); editing the `.abic.toml` or one of its declared dependencies re-runs the whole pipeline | `[PASS] AMC core metadata bootstrap succeeded` and `[PASS] verified amc_add_abi -> build -> validate -> generate -> RuntimeRegistry::type_of<T>()`; needs `cmake` + `clang++` (and prefers `ninja` when it is installed) |
| `run_test.py --elf-pe`    | Nine phases over the metadata section: `amc build` -> `amc generate` (a `amc_generated.hpp` with the platform section attribute) -> `clang++` links a native binary carrying `.abix.metadata` -> `readelf -S` -> `objdump -s` -> `amc metadata --from-elf` -> `--verify` -> export round-trip -> error cases | Shows the section really lands in the binary and can be read back, rather than round-tripping in memory only                                                                | Each `[Phase N]` prints `[PASS]` (with a `✓`) and the run ends with a summary; needs `readelf` / `objdump` on Linux, `otool` on macOS and `llvm-readobj` / `llvm-objcopy` on Windows |
| `abix_mcp_compat.py`    | Drives `amc-mcp` over stdio JSON-RPC to answer "is `plugin` ABI-compatible with `host`?" and prints a JSON report                                                                                                                                                                                            | Makes the MCP tool protocol verifiable in CI: the LLM is removed from the loop while the transport and tool protocol stay real                                              | JSON report on stdout; stderr prints `COMPATIBLE`, otherwise `INCOMPATIBLE: N difference(s)` with a non-zero exit                                                                                  |
| `abix_token_cost.py`    | Compares the token cost of learning one module's ABI from source against `amc context <file.abix> --format llm`                                                                                                                                                                                              | Gives a reproducible order-of-magnitude for "the metadata view is cheaper"                                                                                                  | `{"source_tokens", "metadata_tokens", "source_over_metadata"}` on stdout; the counts come from a lexical tokenizer, not a model tokenizer, so treat the ratio as an **indicator, not a benchmark** |
| `run_bench.py` | Runs each benchmark **family in its own process** and summarises the results (`mean`/`stddev`); `--list` shows what would run. It discovers every family in `bench_all` via `--benchmark_list_tests` and every Google Benchmark suite is linked into `bench_all`, so no standalone executable is run by default; pass explicit filters to narrow it; reads the `cpu0` cpufreq governor up front and prints the fix command when CPU scaling is active, and `--pin` (default `auto`) restricts every child to the highest-frequency cores (`--pin off` disables it, `--pin 0-7` pins explicitly)                          | The EBR/RCU families share the process-wide `rcu::Domain`, so a family measured after the others reads several times slower (`BM_EBR_SyncPhase/1T` spans ~6 ns to ~1 us); isolating them is what makes numbers comparable across families and machines | Prints `N benchmarks -> <json>` per family, then a `| family | benchmark | threads | mean ns | sd |` table; one JSON per family |

```bash
python tools/run_test.py --bootstrap                       # AMC_BIN_DIR=... reuses an external build
KEEP_ARTIFACTS=1 python tools/run_test.py --bootstrap       # keeps amc-bootstrap.*
python tools/run_test.py --elf-pe --keep                    # keeps amc_elf_test_*
python tools/abix_mcp_compat.py --host host.abix --plugin plugin.abix
python tools/abix_token_cost.py --abix module.abix --source a.hpp b.hpp
```

### Debugger integration

| File            | What                                                                                                                                  | Purpose                                                                                                                                                                            | Expected                                                                                                                  |
| --------------- | ------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| `lldb_abix.cpp` | Native plugin built by the CMake target `abix-lldb` (when LLDB headers and library are found), producing `build/<type>/bin/abix_lldb` | Queries the `.abix.metadata` embedded in the loaded binary from the debugger (`abix info` / `type` / `function` / `verify` / `check`); parsing and querying are delegated to `amc` | `plugin load build/Release/bin/abix_lldb` in an LLDB session, then `abix …`; the target is skipped when LLDB is not found |
| `lldb_abix.py`  | Script implementation of the same commands                                                                                            | Alternative to the native plugin, for environments without an LLDB C++ toolchain                                                                                                   | `command script import tools/lldb_abix.py`, then `abix …`; set `ABIX_AMC` when `amc` is not on PATH                       |

Both follow "LLDB never grows a second `.abix`/ELF parser". `tools/__pycache__/` is just the Python cache directory, not a script.

## Sanitizers and platform notes

- **Sanitizers** — Debug builds accept `-DSANITIZER=asan` / `-DSANITIZER=tsan` (Linux/macOS only; disabled on Windows). Run the concurrency suites (`interface_module_test`, `concurrency_test`) under TSan to catch data races.
- **Windows** — MSYS2 UCRT64 (GCC or Clang). Tests are `*.exe` and run with the working directory at `build/<type>/bin`, so plugins (`*.dll`) and staged cross-toolchain variants (`bin/variants/...`) resolve. Cross-CRT coverage needs `tools/build_variants.py --with-msvc` (builds the MSVC `*_dll` variants);`13.cross_crt_msvc_resource` and `13b.owner and interface across a cross-CRT MSVC DLL` then run for real instead of skipping.
- **ELF/Mach-O-only steps** (`readelf`/`objdump`/`otool`, LLDB, `dlopen` shims) skip on Windows and are covered on Linux/macOS.

## Reading a failure

- Re-run the single tier with `--output-on-failure`, or run the script/exe directly (see the [build and run](#build-and-run) commands).
- The tiers are ordered so a failure points at one layer: `amc-core-test` (core) → `amc-e2e-test` (IR round-trip) → `amc-feature-test` (parsing/IR/binding/reuse/front-back) → `amc-interop-test` (cross-language) → `amc-integration-test` (CLI).
- A `[SKIP]` is not a pass by accident: it means the optional toolchain is absent. Install it (`rustc`, `zig`, `lldb`) to exercise that tier.
