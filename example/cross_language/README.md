# Cross-language example: one ABI, three consumers

This is a standalone, reusable version of the C++ → ABIX → Rust/Zig flow. It is
not part of the main test suite and has no build-time dependency on ABIX headers
or libraries.

```text
native/shapes.hpp       public C ABI (the source of truth for the example)
native/shapes.cpp       ordinary C++ implementation
shapes.abic.toml        AMC import, ownership/error contracts and export
cpp_consumer.cpp        generated C++ projection + native calls
rust_consumer.rs        generated Rust projection + ownership wrapper
zig_consumer.zig        generated Zig projection + comptime layout checks
run_example.py          builds the library, runs AMC and runs all consumers
```

## Run it

From the repository root:

```bash
python3 example/cross_language/run_example.py
```

Or point at a specific toolchain:

```bash
python3 example/cross_language/run_example.py \
    --amc build/Release/bin/amc \
    --cxx clang++ \
    --rustc rustc \
    --zig zig
```

The script also accepts `AMC`, `CXX`, `RUSTC` and `ZIG` environment variables.
It creates `example/cross_language/build/` and leaves the artifact, generated
projections and compiled consumers there for inspection.

Expected output ends with:

```text
[example] running C++ consumer
cpp example ok
[example] running Rust consumer
rust example ok
[example] running Zig consumer
zig example ok
[example] done
```

## What it demonstrates

- **One source of ABI truth.** `amc build` reads `native/shapes.hpp` once and
  writes `build/shapes.abix`. The same artifact drives all three projections.
- **Ownership contract.** `shape_create` returns an owned `ShapeVec` and
  `shape_destroy` is the release entry point. The Rust projection turns that
  into `OwnedShapeVec` with `Drop`; the consumer never frees foreign memory.
- **Error boundary.** `shape_div` returns a status and writes its result
  through parameter 2, recorded by `[[error]]`. Rust/Zig consumers can inspect
  the same contract instead of guessing from a bare integer return; `shape_scale`
  shows the same status-plus-out-parameter shape without a declared boundary.
- **Layout verification.** The generated C++ header has `static_assert`s, the
  Rust projection has `const assert!`, and the Zig projection has a `comptime`
  block. A layout drift fails at compile time.
- **No hand-written bindings.** The generated projections contain the types,
  symbols and metadata; the consumers only add application logic.
- **One-way scope.** The example intentionally covers C++ → Rust/Zig. Rust/Zig
  source extraction and reverse projection are outside this example.

## Change it

1. Add a function or field to `native/shapes.hpp` / `shapes.cpp`.
2. Add its name to `symbols` in `shapes.abic.toml`.
3. Re-run `run_example.py`.
4. Update the consumers to use the new entry point.

If a function takes or returns a borrowed view, add an explicit ownership or
lifetime contract in `shapes.abic.toml`; do not rely on the generated code to
infer lifetime safety.

## Relation to the interop suite

`AMC/tests/interop/` is the broader CI fixture: it covers ABIX-STD views,
containers, interfaces, exposed classes and `amc target` verification. This
example is deliberately smaller so it can be copied into a new project without
pulling in the test harness.
