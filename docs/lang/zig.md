# Zig

<p align="center">
  <a href="zig_zh.md">中文</a> · English
</p>

<details>
<summary>Contents</summary>

- [Role](#role)
- [Generated projection](#generated-projection)
- [Layout checks](#layout-checks)
- [Example](#example)
- [Limits and notes](#limits-and-notes)

</details>

Zig is a **backend-only** ABIX target: ABIX consumes an existing `.abix` artifact and projects it to Zig. There is no Zig frontend and no Zig-specific model in AMC.

## Role

- `amc --describe-language zig` reports `backend protocol=jsonl-v1`.
- `amc-zig` is a standalone provider process; it consumes the same ABIX IR as the Rust and C++ backends.
- Zig is used as a projection target, not as an import language. Build the artifact from a C++ or Rust source first, then run `amc generate -l zig`.

## Generated projection

```bash
amc generate build/package.abix -l zig -o gen/
```

`amc_generated.zig` contains:

- `extern struct` declarations for layout-compatible records;
- `extern fn` declarations for exported functions;
- `[*c]` typed pointers, with nullable pointers for opaque pointees;
- `ABIX_*` constants for type identity, size, align and field offsets;
- a `comptime` block with `@sizeOf`, `@alignOf` and `@offsetOf` assertions.

The same artifact that drives the Rust projection drives the Zig projection. `AMC/tests/interop/` builds one native library and runs both consumers against it; no Zig-specific information is added to AMC.

## Layout checks

The `comptime` block turns a layout mismatch into a build failure:

```zig
comptime {
    if (@sizeOf(DemoVec2) != 16) @compileError("DemoVec2 size mismatch");
    if (@alignOf(DemoVec2) != 8) @compileError("DemoVec2 align mismatch");
    if (@offsetOf(DemoVec2, "y") != 8) @compileError("DemoVec2::y offset mismatch");
}
```

This is the Zig counterpart of the C++ `static_assert` checks and the Rust `const assert!` checks: the recorded ABI is verified at compile time, before the native call can go wrong.

## Example

```zig
const std = @import("std");
const abi = @import("gen/amc_generated.zig");

pub fn main() void {
    const sum = abi.demo_add(2, 3);
    if (sum != 5) @panic("demo_add");

    var v = abi.DemoVec2{ .x = 4, .y = 2.5 };
    const scaled = abi.demo_scale(&v, 2.0);
    if (scaled != 13.0) @panic("demo_scale");

    var out: c_int = 0;
    const rc = abi.demo_div(10, 2, &out);
    if (rc != 0 or out != 5) @panic("demo_div");
}
```

The Zig consumer links the same native library as the C++ and Rust consumers; it does not need a C header or a generated C shim.

## Limits and notes

- Zig is backend-only today: there is no Zig source extractor, so a Zig provider must expose an ABI-compatible boundary through another frontend or a generated artifact.
- The exported surface still has to be representable in the native ABI; internal Zig types and allocators stay inside the module.
- The recorded ABI is target-specific; build the Zig consumer for the same target and C ABI as the provider.
- `[*c]` pointers keep C pointer semantics, while `extern struct` and `comptime` checks pin the layout.
- Error boundaries are not projected to Zig error sets / error unions yet; the current output keeps the plain status/payload parameters.
- No Zig-specific concept is added to ABIX IR; the projection is derived only from the artifact.
