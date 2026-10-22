# Designing the ABIX Private Header for Windows PE Images

<p align="center">
  <a href="pe-inspection_zh.md">中文</a> · English
</p>

> **Status: implemented.** `amc generate -l cpp` emits the `.abix` / `.abix.n` sections for `_WIN32` (GNU/clang attributes and MSVC `#pragma section` + `__declspec(allocate)`), and AMC reads the region back through the capability layer ([binary-image.md](binary-image.md), `ABIX/Binary/PE.h`). Verified with both clang++ and cl.exe, including `amc metadata --from-elf` and `amc publish` (CodeView GUID + Age). ABIX builds on Windows with the MSYS2 UCRT64 toolchain, and PE images are handled end to end.

<details>

<summary>Contents</summary>

- [Why PE needs its own design](#why-pe-needs-its-own-design)
- [The ABIX private header](#the-abix-private-header)
- [Section layout and names](#section-layout-and-names)
- [Why not `.abix$m` / `.abix$d` fragments](#why-not-abixm--abixd-fragments)
- [Getting the section into the image](#getting-the-section-into-the-image)
- [Reading a PE image](#reading-a-pe-image)
- [Inspecting with standard tools](#inspecting-with-standard-tools)
- [Stripping](#stripping)
- [AMC CLI](#amc-cli)
- [Limitations](#limitations)
- [Testing](#testing)

</details>

## Why PE needs its own design

AMC embeds the Metadata Region in a custom section. On ELF that section is `.abix.metadata`; on Mach-O it is `__abix_metadata` in `__DATA`. Both are chosen because the canonical ELF spelling is legal there. Windows PE is different in a way that forces a design of its own:

- **PE section names are exactly 8 bytes.** `IMAGE_SECTION_HEADER.Name` is `BYTE Name[8]` and is *not* NUL-terminated when full. The canonical name `.abix.metadata` (14 bytes) does not fit.

That is not merely cosmetic: truncation is silent, and it can collide. A compile probe on this toolchain shows the linker truncating the canonical names:

  ```text
  # clang++ -shared probe.cpp  (probe uses __attribute__((section(".abix.metadata"))))
  Name: .abix.me (2E 61 62 69 78 2E 6D 65)   # ".abix.me"
  Name: .abix.na (2E 61 62 69 78 2E 6E 61)   # ".abix.na"
  ```

`.abix.metadata` and `.abix.meta` would both truncate to `.abix.me` and silently merge into one section.

- **The toolchains disagree.** MSVC and MinGW/GNU-ld do not treat section names identically (see [Why not `.abix$m`](#why-not-abixm--abixd-fragments) below).

- **A name is not a contract.** A section name is a string the toolchain may truncate, reorder, merge or rewrite.

So ABIX does not identify its payload by section name. It writes a self-describing **private header** and the reader locates the payload by *content* (the `ABIX` magic), using the section name only as a fast path.

## The ABIX private header

The private header is a small, pointer-free, 8-byte aligned structure placed at offset 0 of the `.abix` section, immediately before the Metadata Region. It is ABIX's own structure — not a PE structure — hence "private". All fields are little-endian.

```c
// At offset 0 of the `.abix` section. Pointer-free; safe to copy, mmap or
// relocate as a block.
struct AbixPeHeader {              // 24 bytes
    uint32_t magic;                // 'A''B''I''X'  = 0x58494241
    uint16_t header_version;       // 1
    uint16_t header_size;          // 24 (8-byte aligned)
    uint32_t flags;                // bit0 = NAMES_SECTION_PRESENT
    uint32_t region_offset;        // from the section start; 24
    uint32_t region_size;          // Metadata Region byte length
    uint32_t reserved;             // 0 (keeps region_offset 8-byte aligned)
};
```

The payload of `.abix` is therefore:

```text
.abix section
├── AbixPeHeader        (24 B, private header — this file)
├── Metadata Region     (region_size B: manifest + desc + hash, …)
└── padding             (to 8 B)
```

The reader validates `magic`, `header_version` and `header_size`, then treats `region_offset … region_offset + region_size` as an ordinary **Metadata Region** and hands it to the existing, container-agnostic path (`read_metadata_header` / `verify_metadata_region`). No new region format is introduced; the private header only *locates* the region and survives whatever the toolchain did to the section name.

The `AbixPeHeader` intentionally carries no pointers or absolute addresses: the Metadata Region is already a self-contained, pointer-free image, and keeping the header pointer-free means the pair can be dumped, hashed and compared byte for byte with the emit-time bytes.

## Section layout and names

| Canonical (ELF) | PE section | Content | Strippable? |
|-----------------|------------|---------|-------------|
| `.abix.metadata` | `.abix` | `AbixPeHeader` + Metadata Region (pointer-free) | No — required |
| `.abix.names` | `.abix.n` | Array of `const char *` pointers to name strings | Yes — runtime does not use it |

Both names are ≤ 8 bytes and, crucially, **do not share the `$` grouping convention** (see the next section). `native_section_name(BinaryFormat::pe, …)` performs the canonical → PE translation; unknown names pass through unchanged, as they do for ELF/Mach-O.

The names are emitted with an explicit dot (`.abix.n`) rather than relying on truncation of `.abix.names` (which would silently become `.abix.na`). Explicit short names keep the mapping stable and readable:

```cpp
// AMC C++ backend, Windows PE target
__attribute__((used, section(".abix"), aligned(8)))
inline constexpr unsigned char amc_abix_region[] = { /* AbixPeHeader + Region */ };

__attribute__((used, section(".abix.n")))          // Debug only
inline constexpr const char *amc_type_names[] = { "MyClass", /* ... */ };
```

## Why not `.abix$m` / `.abix$d` fragments

COFF/PE gives `name$suffix` a special meaning: the linker **folds the fragments into a single `name` section, ordered by `suffix`** (this is the `.CRT$XCU` idiom). An earlier sketch in [`metadata_modes_zh.md`](../abix/metadata_modes_zh.md) therefore proposed `.abix$m` / `.abix$d` / `.abix$h` / `.abix$n`. That scheme does not survive contact with the linker:

1. **Fragment offsets are link-time, not compile-time.** A self-describing region references its parts by byte offset from the region start. Once the parts are separate `$` fragments, their final placement inside `.abix` is decided by the linker, so the compiler cannot emit a correct `AbixPeHeader.region_offset`.
2. **Folding order is fixed by the suffix, not by us.** `.abix$d` sorts before `.abix$m`, so the manifest would not sit first in the merged section.
3. **The behavior is toolchain-specific.** MSVC folds `$` fragments; MinGW GNU-ld does not fold them by default. The same source would produce a merged `.abix` on MSVC and loose `.abix$*` sections under MinGW.

For names there is a fourth problem: `.abix$n` shares the `.abix` base, so MSVC would fold it *into* `.abix`, and the names could no longer be stripped independently — which the Debug/Release model depends on.

Hence: **one self-contained `.abix` section** (private header + region), plus a separate, non-`$` `.abix.n` for the strippable names. The `$` fragments are kept here only as a documented rejected alternative.

## Getting the section into the image

**MinGW / Clang** (the `SKL_ABIX_BUILD_DLL_VARIANTS` cross-toolchain path uses clang): a section attribute with a name of at most 8 bytes, kept even when unreferenced:

```cpp
__attribute__((used, section(".abix"), aligned(8)))
inline constexpr unsigned char amc_abix_region[] = { /* ... */ };
```

**MSVC**: sections must be declared with `#pragma section`, allocated with `__declspec(allocate(...))`, and given their attributes with a linker directive:

```cpp
#pragma section(".abix", read)
#pragma section(".abix.n", read)                    // Debug only
__declspec(allocate(".abix")) alignas(8)
extern const unsigned char amc_abix_region[];
#pragma comment(linker, "/SECTION:.abix,R")
```

The region must be emitted into a read-only section and must not participate in `--gc-sections`-style removal (`__attribute__((used))` / an explicit `KEEP` in a linker script, or `/INCLUDE:` on MSVC).

## Reading a PE image

`find_binary_section` grows a PE branch. The algorithm, per image, is:

1. Read the DOS header; require `e_magic == 'MZ'`; take `e_lfanew` (offset `0x3C`) as the NT headers offset.
2. Require `Signature == 'PE\0\0'`.
3. Read the COFF header (20 bytes): `NumberOfSections` (offset +2) and `SizeOfOptionalHeader` (offset +16).
4. Read the optional header magic (`0x10B` = PE32, `0x20B` = PE32+) and `ImageBase` (needed later for name resolution).
5. Walk the section table at `nt + 24 + SizeOfOptionalHeader`: `NumberOfSections` × 40-byte entries, each `Name[8]`, `VirtualSize` (offset +8), `VirtualAddress` (+12), `SizeOfRawData` (+16), `PointerToRawData` (+20).
6. Locate the payload:
   - fast path: a section whose `Name` is `.abix`; otherwise
   - fallback: scan every section's raw bytes for the `AbixPeHeader` magic at the section start and validate `header_version` / `header_size`. The fallback is why the private header exists: it makes the reader independent of the section name.
7. Copy `[PointerToRawData, +SizeOfRawData)` (clamped to the file length) and validate `AbixPeHeader`; take `region_offset` / `region_size` with bounds checks and hand the region to the shared metadata path.

**Names.** `.abix.n` holds `const char *` pointers, not the strings (they live in `.rdata`). Unlike an ELF section, those pointers are *absolute virtual addresses*, so an offline reader must map them back:

```text
pointer (VA) → RVA = VA − ImageBase → file offset via the section table
```

This VA → RVA → file-offset step is PE-specific and is the one place the PE reader does more work than the ELF/Mach-O readers. It is required only for the optional names section; `amc metadata` (region only) does not need it.

## Inspecting with standard tools

```bash
# Section headers (binutils / LLVM / MSVC)
objdump -h app.exe | grep -i abix
llvm-readobj --sections app.exe | grep -A2 -i abix
dumpbin /HEADERS app.exe | findstr /i abix

# The image section carries the ABIX magic at offset 0
objcopy --dump-section .abix=region.bin app.exe
llvm-objcopy --dump-section .abix=region.bin app.exe
# region.bin now starts with 41 42 49 58 ("ABIX")
```

After a merge, `dumpbin /HEADERS app.exe` lists the image's `.abix` section with its `PointerToRawData` / `SizeOfRawData`, which is exactly what step 7 reads.

## Stripping

PE has no per-section `strip`; remove the names section with `objcopy`:

```bash
objcopy --remove-section=.abix.n app.exe
llvm-objcopy --remove-section=.abix.n app.exe app.stripped.exe

# The region section is untouched:
objcopy --dump-section .abix=region.bin app.stripped.exe
```

Removing `.abix.n` must not change program behavior — the runtime never references the names (the same invariant as ELF's `.abix.names`).

## AMC CLI

The existing CLI is container-agnostic and needs no new flags:

```bash
amc metadata --from-elf app.exe --format json   # PE auto-detected
amc metadata --verify app.exe --format json
amc metadata app.abix -o region.abixmeta
```

> `--from-elf` is a historical name; the container (ELF, Mach-O, PE) is detected from the file's magic bytes, so PE images are accepted once the reader lands.

## Limitations

- **PE is implemented.** `amc generate -l cpp` emits `.abix` / `.abix.n`, and `detect_binary_format` / `find_binary_section` / `amc metadata` / `amc publish` read them back (see the [BinaryImage layer](binary-image.md)).
- **PE32+ first.** 64-bit PE (`0x20B`) is the primary target; PE32 (`0x10B`) is structurally supported because the region is pointer-free and word-size agnostic.
- **Little-endian only**, matching ELF/Mach-O.
- **Names need RVA mapping.** Reading `.abix.n` offline requires `VA → RVA → file offset`; `amc metadata` (region only) does not.
- **`$` fragments are deliberately avoided** (section merging and link-time offsets); the region is a single `.abix` blob.
- **Signed images.** Authenticode signing hashes the section table and section bytes; adding `.abix` after signing invalidates the signature. Embed ABIX **before** signing (the same rule as adding any custom section).

## Testing

The harness `tools/run_test.py --elf-pe` covers the PE path alongside ELF and Mach-O. Its Windows branch runs:

1. `amc build` → `.abix` + `.abix.meta`
2. `amc generate -l cpp` → header with the PE section attributes
3. `clang++`/`cl.exe` → PE image with the `.abix` (+ `.abix.n`) sections
4. `llvm-readobj --sections` — verify the sections exist
5. `llvm-objcopy --dump-section` — verify the `ABIX` magic
6. `amc metadata --from-elf` / `--verify` — parse and verify the region
7. `llvm-objcopy --remove-section=.abix.n` — strip round-trip
8. error cases — non-PE rejection, truncated PE rejection
