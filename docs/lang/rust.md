# Rust

<p align="center">
  <a href="rust_zh.md">中文</a> · English
</p>

<details>
<summary>Contents</summary>

- [Role](#role)
- [Frontend](#frontend)
- [Generated projection](#generated-projection)
- [Ownership and errors](#ownership-and-errors)
- [Example](#example)
- [Limits and notes](#limits-and-notes)

</details>

Rust is ABIX's first non-C++ provider. `amc-rust` has a source-level frontend and a Rust backend, and does not link Clang/LLVM.

## Role

- `amc --describe-language rust` reports `frontend backend protocol=jsonl-v1`.
- The frontend reads `#[repr(C)]` structs/enums and `extern "C"` functions from `.rs` files and computes the C layout itself.
- The backend emits `amc_generated.rs` from an existing `.abix` artifact.
- `amc-rust` is a standalone provider process; it never changes ABIX IR.

## Frontend

`.abic.toml` selects `language = "rust"` and lists the source and exported symbols:

```toml
[[import]]
language = "rust"
files = ["src/lib.rs"]
symbols = ["DemoVec2", "demo_add"]

[[export]]
output = "demo.abix"
```

The frontend is intentionally a preliminary extractor: it understands the subset that crosses an FFI boundary — `#[repr(C)]` structs/enums and `extern "C"` functions — and computes the same layout the C++ frontend gets from Clang. Generics, traits, lifetimes and bit-fields are out of scope; unsupported constructs are ignored rather than guessed.

A provider source looks like this:

```rust
#[repr(C)]
pub struct DemoVec2 {
    pub x: i32,
    pub y: f64,
}

#[no_mangle]
pub extern "C" fn demo_add(a: i32, b: i32) -> i32 {
    a + b
}
```

## Generated projection

```bash
amc generate build/package.abix -l rust -o gen/
```

`amc_generated.rs` contains:

- `#[repr(C)]` records and enums;
- `unsafe extern "C"` declarations for the exported functions;
- `ABIX_*` constants for the type identity, size, align and field offsets;
- `const _: () = assert!(core::mem::size_of::<T>() == ...)` / `align_of` / `offset_of!` checks;
- an optional metadata-region module for run-time inspection.

C-ABI scalars project to `core::ffi::c_*` aliases (`c_int`, `c_double`, ...) derived from the ABI-normalised primitive identity, rather than fixed-width spellings, so the alias encodes the target's C ABI.

## Ownership and errors

The backend turns recorded contracts into Rust-native wrappers:

- an `owned` factory becomes an `Owned*` wrapper with `Drop`, `as_ref`, `as_ptr` and `into_raw`; the release entry point is the one recorded in the artifact, so Rust never frees foreign memory itself;
- an error boundary becomes a typed `Result<T, E>` wrapper such as `demo_div_result`;
- ABIX-STD span/string/optional/callback types project as typed records with their recorded layout.

The raw `extern "C"` calls remain `unsafe`; the wrappers reduce the unsafe surface without hiding the ABI.

## Example

```rust
#[path = "gen/amc_generated.rs"]
mod abi;

use abi::*;

fn main() {
    unsafe {
        assert_eq!(demo_add(2, 3), 5);

        let v = DemoVec2 { x: 4, y: 2.5 };
        assert_eq!(demo_scale(&v, 2.0), 13.0);

        let owned = OwnedDemoVec2::create(7, 1.5).expect("create");
        assert_eq!(owned.as_ref().x, 7);
        assert_eq!(demo_div_result(10, 2), Ok(5));
    }
}
```

The Rust file still needs to link the native library; the projection provides `#[link]` support through `ABIX_LIBRARY_NAME` when the `abix_link` option is enabled.

## Limits and notes

- The frontend is not a full `rustc` AST: only `#[repr(C)]` / `extern "C"` surfaces are extracted; generics, traits, lifetimes and bit-fields stay inside the module.
- The recorded ABI is target-specific; compile the Rust consumer for the same target and C ABI as the provider.
- Panics and exceptions must not cross the boundary; the error boundary contract is the supported path.
- The generated file is ordinary Rust and can be wrapped in a safe crate API.
- `amc-rust` does not link Clang/LLVM; its frontend computes the layout itself.
