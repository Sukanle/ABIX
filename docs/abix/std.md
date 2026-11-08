# ABIX-STD — cross-language ABI primitives

<p align="center">
  <a href="std_zh.md">中文</a> · English
</p>

ABIX-STD is the **standard cross-language ABI primitive layer**. It is not a replacement STL and it does not put a standard library into every `.abix`. It defines a small set of ABI types and contracts that different languages can consume without each re-inventing a library interface.

See [`ABI-SPEC.md`](../../.agents/ABI-SPEC.md) §9.1 for the normative rules.

## Three data-passing semantics

| Semantics | Meaning                                                             | Who releases                                                      |
| --------- | ------------------------------------------------------------------- | ----------------------------------------------------------------- |
| `borrow`  | non-owning view, valid only while the producer keeps the data alive | nobody — the receiver must not release                            |
| `owned`   | receiver takes ownership                                            | the receiver, through the producer's declared release entry point |
| `copy`    | independent data from an explicit copy                              | the receiver owns the copy                                        |

These are **contracts, not layouts**: they never change `TypeID` / `LayoutHash` / `ABIHash`. They are carried by the ownership contract (`[[owned]]` / `[[contract]]`) and are readable at run time from the metadata-region extension directory.

## Contracts by capability, not by container

Contracts are matched by what a type can **prove**, never by a name table such as `vector ↔ Vec ↔ ArrayList`:

| Contract          | Capability                     | Typical use                         |
| ----------------- | ------------------------------ | ----------------------------------- |
| `SequenceView<T>` | contiguous read/write view     | arrays, slices, vector borrowing    |
| `StringView`      | text bytes + length + encoding | string args, parsing                |
| `Owner<T>`        | ownership + release protocol   | cross-DLL objects, resource handles |
| `Optional<T>`     | value may be absent            | optional args, query results        |
| `Result<T, E>`    | success or failure             | cross-language errors               |
| `Range<T>`        | iterable range                 | non-contiguous containers           |
| `MapView<K, V>`   | keyed lookup                   | associative containers              |
| `Opaque<T>`       | hidden representation          | special / custom containers         |

| Priority | Contracts                                                            |
| -------- | -------------------------------------------------------------------- |
| P0       | sequence view, string view, opaque handle, ownership, error boundary |
| P1       | optional, result, callback, generic range                            |
| P2       | map, set, deque, complex graphs, language-specific containers        |

P2 types are exposed through `Opaque` handles with accessors, capability adapters, or explicit copies — **not** a unified implementation. A `std::vector<T>` is not a contract of its own: AMC reads `data()`/`size()` and lowers it to a `SequenceView<T>` when element type, layout, borrowing lifetime and invalidation conditions allow; otherwise it uses `Opaque` or a copy.

```toml
[[view]]
type = "DemoBuffer"                  # opaque container layout
data_accessor = "demo_buffer_data"
size_accessor = "demo_buffer_size"
```

AMC generates a thin adapter (`DemoBuffer_view(value) -> abix_span_i32`) that calls the accessors; the container's internal layout is never exposed.

## P1 contracts

P1 adds four contracts, each matched by capability (shape + element `TypeID`):

| Contract   | Layout / capability                                                       |
| ---------- | ------------------------------------------------------------------------- |
| `optional` | `{ uint32 discriminant; uint32 reserved; alignas(T) T value; }`           |
| `result`   | `{ int32 status; uint32 reserved; alignas(T) T value; }` (error boundary) |
| `callback` | `{ function entry; opaque context }`                                      |
| `range`    | non-contiguous container via length + indexed access                      |

The v0.4.0 optional and result layouts are explicit:

```cpp
template<class T>
struct abix_optional {
    uint32_t discriminant;   // 0 = None, 1 = Some
    uint32_t reserved;       // must be 0
    alignas(T) T value;      // valid only when discriminant == 1
};

template<class T>
struct abix_result {
    int32_t status;          // transport status, not ErrorCode
    uint32_t reserved;       // must be 0
    alignas(T) T value;      // valid when status is the contract success value
};
```

A consumer rejects a discriminant other than `0` or `1`, and rejects a non-zero `reserved` word; it must not read `value` when the optional discriminant is `0`. `reserved` is a validity sentinel in these two types, not a forward-compatible reserved bit.

`abix_callback` is the borrowed form: the caller keeps the context alive and unregisters it. `abix_callback_owned` is the additive owned form:

```c
typedef void (*abix_callback_release_fn)(void *context);

typedef struct abix_callback_owned {
    abix_callback_fn invoke;
    void *context;
    abix_callback_release_fn release;
    uint32_t flags;       // zero in v1
    uint32_t reserved;    // must be zero
} abix_callback_owned;
```

The producer owns the context and invokes `release` exactly once after the last call. `release` is required for an owned callback, must not unwind, and must not be called by the consumer directly. The consumer must not call the callback after release.

> **v0.4.0:** the `abix_optional`, `abix_result` and `abix_str` layouts in this document are the explicit v0.4.0 layouts. They replace the v0.3.x layouts without a compatibility layer, so old `.abix` artifacts and generated bindings must be regenerated.

## First primitive: borrowed span

The ABIX-STD span is written as a source-level template:

```cpp
namespace abix_std {
template <class T> struct abix_span { const T *data; uint64_t len; };
}
typedef abix_std::abix_span<int32_t> abix_span_i32;   // the exported ABI entity
```

ABIX records the **concrete instantiation**, not the template. The frontend instantiates it on request and AMC records the result under the exported name:

```toml
[[instantiate]]
template = "abix_std::abix_span"
args = ["int32_t"]
as = "abix_span_i32"
```

The concrete entity projects to the same layout in every language:

```rust
// Rust — generated by `amc generate -l rust`
#[repr(C)]
pub struct abix_span_i32 { pub data: *const core::ffi::c_int, pub len: core::ffi::c_ulonglong }
```

```zig
// Zig — generated by `amc generate -l zig`
pub const abix_span_i32 = extern struct { data: [*c]const c_int, len: c_ulonglong };
```

The producer keeps the backing memory alive and hands out a view; the consumer reads it and never releases it. The ownership contract marks the interaction as `borrowed`:

```toml
[[contract]]
function = "demo_numbers"
returns = "borrowed"
```

Instantiation and contract matching are separate: a successful instantiation of `abix_span<int32_t>` does not by itself claim it satisfies the ABIX-STD `SequenceView<i32>` contract; AMC proves compatibility or generates a thin adapter. Declaring the contract makes AMC lower the concrete type:

```toml
[[instantiate]]
template = "abix_std::abix_span"
args = ["int32_t"]
as = "abix_span_i32"
contract = "sequence-view"     # -> direct
```

A record with the same two fields in a different order is `needs_adapter`, and AMC generates the conversion to the canonical view:

```rust
pub fn abix_view_rev_i32_to_abix_span_i32(value: abix_view_rev_i32) -> abix_span_i32 {
    abix_span_i32 { data: value.data, len: value.len as _ }
}
```

Generic parameters are a frontend concern; see [`LANGUAGE-PLUGIN.md`](../../.agents/LANGUAGE-PLUGIN.md) and [`ABI-SPEC.md`](../../.agents/ABI-SPEC.md) §18.

## Second primitive: string view

`abix_str_utf8` is `{ const char *data; uint64_t len; uint32_t encoding; uint32_t reserved; }`, lowered against the `string-view` contract:

```toml
[[instantiate]]
template = "abix_std::abix_str"
args = ["char"]
as = "abix_str_utf8"
contract = "string-view"
element = "char"
```

The code unit is matched by the data pointer's pointee `TypeID`; the encoding is a runtime value (the build checks the field's presence and type). It is read-only and borrowed by default. `std::string_view`, Rust `&str` and Zig slices are not treated as inherently equivalent — encoding, length unit, layout and borrowing are verified first.

## Owner projection

`abix_std::Owner<T>` is a source-level projection of the existing `owner::handle`, not a new ABI type. The template parameter is a label for generated bindings; the layout is exactly `owner::handle`:

```text
Owner<T> = owner::handle
instances = abix_owner_<Type>   // generated names
runtime = owner::ref (C++) or a language wrapper
```

The handle carries identity, mode, destroy and module instance. The module lease that keeps the destroy entry valid is the separate `abix_lease` C ABI described in [`lease.md`](lease.md).

### Final API

- source type: `abix_std::Owner<T>`
- generated instance: `abix_owner_<Type>`
- runtime: `owner::ref` in C++; generated wrappers in Rust/Zig
- `T` is a label and adds no field
- destroy must come from the creating module; hold an `abix_lease` until release
- no bare destroy function pointer ABI

## Language adapter rules

ABIX describes layouts, not language types. AMC lowers native types with an explicit adapter; native layouts are never assumed to be equivalent:

| ABIX type             | Rust                         | Zig                    | Rule                                                                                                                                                   |
| --------------------- | ---------------------------- | ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `abix_optional<T>`    | `Option<T>`                  | `?T`                   | not layout-equivalent; explicit `#[repr(C)]` / `extern struct`; `discriminant` is `u32` and must be 0/1; `reserved` must be 0; decode `reserved` first |
| `abix_result<T>`      | `Result<T, E>`               | error union / custom   | `status` is `int32_t`; `reserved` must be 0; map `E` through the `[[error]]` contract; never store `ErrorCode` in `status`                             |
| `abix_str`            | `&str`                       | slice                  | borrowed only; verify encoding, length unit and lifetime; `reserved` must be 0; do not assume UTF-8                                                    |
| `abix_span<T>`        | `&[T]`                       | slice                  | borrowed only; element `TypeID` and length unit must match                                                                                             |
| `abix_callback`       | function + context           | fn + context           | borrowed: the caller keeps the context alive and unregisters it                                                                                        |
| `abix_callback_owned` | function + context + release | fn + context + release | producer owns the context; release exactly once after the last call; no unwind; no calls after release                                                 |
| `Owner<T>`            | wrapper                      | wrapper                | alias of `owner::handle`; destroy from the creating module; hold an `abix_lease` until release                                                         |

Rust `Option<T>` and `Result<T, E>` use niche optimization and may have a different size, alignment or discriminant range; `transmute` and `#[repr(transparent)]` casts are not valid adapters. A Zig error union is a tagged union, not `{ status, value }`; lower it explicitly.

`abix_result.status == 0` means success. Any other value is an error selected by the `[[error]]` contract; `ErrorCode` is a 16-byte boundary identity, not the transport status. An unknown status must be carried through unchanged so the receiver can decide whether to reject it.

## Layout matrix and conformance

The v0.4.0 concrete values for the first supported instances are:

| Instance                               | Size | Align | Payload offset                                  |
| -------------------------------------- | ---- | ----- | ----------------------------------------------- |
| `abix_optional<int32_t>`               | 12   | 4     | 8                                               |
| `abix_optional<int64_t>`               | 16   | 8     | 8                                               |
| `abix_optional<void *>` (64-bit)       | 16   | 8     | 8                                               |
| `abix_result<int32_t>`                 | 12   | 4     | 8                                               |
| `abix_result<int64_t>`                 | 16   | 8     | 8                                               |
| `abix_str<char>` (64-bit)              | 24   | 8     | `data=0`, `len=8`, `encoding=16`, `reserved=20` |
| `abix_str<char>` (32-bit, u64 align 4) | 20   | 4     | `data=0`, `len=4`, `encoding=12`, `reserved=16` |

The pointer, 64-bit and over-aligned instances are computed from the target's `alignof(T)`; the table is a check, not a substitute for the formula. `test/conformance/run_std_conformance.py` builds one native library and consumes it from C, C++, Rust and Zig; it also runs the C/C++ matrix at 32-bit where the host supports `-m32`. `test/std_contract_test.cpp` pins the C++ assertions and the v0.3.x-to-v0.4.0 negative cases.

## Scope

In scope: the ABI types and contracts languages share (span, string view, owned handle, result, callback). Out of scope: re-implementing a full STL, defining a universal object model, or a runtime ownership system (see [`AGENTS.md`](../../.agents/AGENTS.md) §1.3).

The end-to-end example lives in `tests/interop/`: one C++ library, one `.abix`, and both a Rust and a Zig consumer reading the same borrowed span.
