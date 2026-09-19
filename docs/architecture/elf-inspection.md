# Inspecting ABIX Metadata in ELF and Mach-O Binaries

<p align="center">
  <a href="elf-inspection_zh.md">中文</a> · English
</p>

<details>

<summary>Contents</summary>

- [How Metadata Gets Into the Binary](#how-metadata-gets-into-the-binary)
- [Section Layout](#section-layout)
- [Using readelf / objdump (ELF)](#using-readelf-objdump-elf)
- [Using otool (macOS)](#using-otool-macos)
- [Using AMC CLI](#using-amc-cli)
- [Stripping](#stripping)
- [Automated Testing](#automated-testing)
- [Supported formats](#supported-formats)
- [Limitations](#limitations)

</details>

AMC embeds ABI metadata into compiled binaries via custom sections. This
document explains how to locate, inspect and verify those sections using
standard tools (`readelf`/`objdump` on ELF, `otool` on Mach-O) and the AMC CLI.

## How Metadata Gets Into the Binary

The AMC C++ backend (`amc generate -l cpp`) produces a header that uses
`__attribute__((section(...)))` to place data into two sections. The attribute
is selected per target: ELF uses the dot-separated names, while Mach-O requires
a `segment,section` pair and a name of at most 16 bytes.

```cpp
// ELF
__attribute__((used, section(".abix.names")))
inline constexpr const char *amc_type_names[] = { ... };

__attribute__((used, section(".abix.metadata"), aligned(8)))
inline constexpr unsigned char amc_metadata_region[] = { ... };

// Mach-O (macOS)
__attribute__((used, section("__DATA,__abix_names")))
__attribute__((used, section("__DATA,__abix_metadata"), aligned(8)))
```

When a user compiles code that includes this header with a standard compiler
(clang++, g++) on Linux or macOS, the resulting binary contains both sections.

```
amc build    →  .abix + .abix.meta
amc generate →  amc_generated.hpp  (section attributes)
clang++/g++  →  ELF or Mach-O binary  (metadata + names sections)
```

## Section Layout

| Canonical (ELF) | Mach-O | Content | Strippable? |
|-----------------|--------|---------|-------------|
| `.abix.metadata` | `__DATA,__abix_metadata` | Self-describing Metadata Region: manifest + desc + hash + names (offset-based, pointer-free) | Yes, but `amc metadata --from-elf` will fail |
| `.abix.names` | `__DATA,__abix_names` | Array of `const char*` pointers to type/field/function name strings (strings live in `.rodata`/`__TEXT,__cstring`) | Yes, safe — runtime does not reference it |

AMC accepts the canonical ELF spelling everywhere and translates it to the
native spelling of the container it detects.

## Using readelf / objdump (ELF)

```bash
# List all ABIX sections
readelf -S <binary> | grep abix

# Example output:
#   [13] .abix.metadata    PROGBITS   0000000000002048  00002048
#   [25] .abix.names       PROGBITS   0000000000004010  00003010

# Dump .abix.metadata raw bytes (look for ABIX magic at offset 0)
objdump -s -j .abix.metadata <binary>

# Dump .abix.names pointer array
objdump -s -j .abix.names <binary>
```

## Using otool (macOS)

```bash
# List all ABIX sections (sectname/segname)
otool -l <binary> | grep -A3 -i abix

# Dump the metadata section (the first word prints as 58494241 == "ABIX")
otool -s __DATA __abix_metadata <binary>

# Dump the names pointer array
otool -s __DATA __abix_names <binary>
```

The `.abix.metadata` region starts with the 4-byte magic `ABIX` (hex `41 42 49 58`).

## Using AMC CLI

```bash
# Read metadata directly from an ELF or Mach-O binary (parses the region)
amc metadata --from-elf <binary> --format json

# Verify metadata consistency
amc metadata --verify <binary> --format json

# Export the Metadata Region as a standalone .abixmeta file
amc metadata <file.abix> -o region.abixmeta

# Verify a standalone region file
amc metadata --verify region.abixmeta
```

> `--from-elf` is retained for compatibility; the input container is detected
> automatically (ELF or Mach-O).

## Stripping

```bash
# ELF: remove .abix.names (safe — runtime does not use it)
strip --remove-section=.abix.names <binary>
readelf -S <binary> | grep .abix.names   # no output

# Mach-O: remove __abix_names (safe — runtime does not use it)
strip -R __abix_names <binary>
otool -l <binary> | grep __abix_names    # no output
```

After stripping, `amc metadata --from-elf` still works because it reads the
metadata region, not the names section.

## Automated Testing

The project includes a test script that exercises the full pipeline on the host
platform (readelf/objdump on ELF, otool on Mach-O):

```bash
python3 tools/test_amc_elf-pe.py           # run and auto-cleanup
python3 tools/test_amc_elf-pe.py --keep    # keep temp files for inspection
```

The script runs 9 phases:

1. `amc build` — produce `.abix` + `.abix.meta`
2. `amc generate` — produce C++ header with section attributes
3. `clang++` — compile into a native (ELF/Mach-O) binary
4. `readelf -S` / `otool -l` — verify the metadata and names sections exist
5. `objdump -s` / `otool -s` — dump section contents, verify ABIX magic
6. `amc metadata --from-elf` — parse metadata from the binary
7. `amc metadata --verify` — verify consistency
8. Export roundtrip — `.abixmeta` export + verify
9. Error cases — non-binary rejection, truncated ELF rejection

## Supported formats

- **Linux / ELF**: ELF64 little-endian (x86_64 / aarch64), thin images.
- **macOS / Mach-O**: 64-bit little-endian Mach-O (arm64), thin images.
- **Windows / PE**: planned, not yet implemented.

## Limitations

- **Byte order**: little-endian is currently the **only** supported byte order
  for both ELF and Mach-O. Big-endian support is intentionally deferred until a
  concrete networking / RPC requirement needs it.
- **macOS hardware**: Apple Silicon (arm64) is supported. **Intel Mac
  (x86_64 macOS) is not supported for now** — there is no test device available
  to validate it, so it is out of scope until one is.
- **Mach-O images** must be thin (single architecture); universal / fat
  binaries are not parsed yet.
- **Cross-platform goal**: Windows (PE), macOS (Mach-O) and Linux (ELF). PE/COFF
  parsing is planned but not yet implemented.
- `.abix.names` contains `const char*` pointers; the actual strings reside in
  `.rodata` / `__TEXT,__cstring`. Use `strings <binary> | grep -i amc` to find them.
