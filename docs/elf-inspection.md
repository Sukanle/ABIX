# Inspecting ABIX Metadata in ELF Binaries

AMC embeds ABI metadata into compiled ELF binaries via custom sections. This
document explains how to locate, inspect and verify those sections using
standard tools (`readelf`, `objdump`) and the AMC CLI.

## How Metadata Gets Into the Binary

The AMC C++ backend (`amc generate -l cpp`) produces a header that uses
`__attribute__((section(...)))` to place data into two ELF sections:

```cpp
// Names table — diagnostic only, safe to strip
__attribute__((used, section(".abix.names")))
inline constexpr const char *amc_type_names[] = { ... };

// Self-describing Metadata Region (manifest + desc + hash + names)
__attribute__((used, section(".abix.metadata"), aligned(8)))
inline constexpr unsigned char amc_metadata_region[] = { ... };
```

When a user compiles code that includes this header with a standard compiler
(clang++, g++), the resulting ELF binary contains both sections.

```
amc build    →  .abix + .abix.meta
amc generate →  amc_generated.hpp  (section attributes)
clang++/g++  →  ELF binary  (.abix.metadata + .abix.names)
```

## Section Layout

| Section | Content | Strippable? |
|---------|---------|-------------|
| `.abix.metadata` | Self-describing Metadata Region: manifest + desc + hash + names (offset-based, pointer-free) | Yes, but `amc metadata --from-elf` will fail |
| `.abix.names` | Array of `const char*` pointers to type/field/function name strings (strings live in `.rodata`) | Yes, safe — runtime does not reference it |

## Using readelf

```bash
# List all ABIX sections
readelf -S <binary> | grep abix

# Example output:
#   [13] .abix.metadata    PROGBITS   0000000000002048  00002048
#   [25] .abix.names       PROGBITS   0000000000004010  00003010

# Detailed section info (size, offset, flags)
readelf -S --wide <binary> | grep -A1 "\.abix"
```

## Using objdump

```bash
# Dump .abix.metadata raw bytes (look for ABIX magic at offset 0)
objdump -s -j .abix.metadata <binary>

# Dump .abix.names pointer array
objdump -s -j .abix.names <binary>
```

The `.abix.metadata` section starts with the 4-byte magic `ABIX` (hex `41 42 49 58`).

## Using AMC CLI

```bash
# Read metadata directly from an ELF binary (parses .abix.metadata section)
amc metadata --from-elf <binary> --format json

# Verify metadata consistency
amc metadata --verify <binary> --format json

# Export the Metadata Region as a standalone .abixmeta file
amc metadata <file.abix> -o region.abixmeta

# Verify a standalone region file
amc metadata --verify region.abixmeta
```

## Stripping

```bash
# Remove .abix.names (safe — runtime does not use it)
strip --remove-section=.abix.names <binary>

# Verify removal
readelf -S <binary> | grep .abix.names   # no output
```

After stripping, `amc metadata --from-elf` still works because it reads
`.abix.metadata`, not `.abix.names`.

## Automated Testing

The project includes a test script that exercises the full pipeline:

```bash
python3 tools/test_amc_elf-pe.py           # run and auto-cleanup
python3 tools/test_amc_elf-pe.py --keep    # keep temp files for inspection
```

The script runs 9 phases:

1. `amc build` — produce `.abix` + `.abix.meta`
2. `amc generate` — produce C++ header with section attributes
3. `clang++` — compile into ELF binary
4. `readelf -S` — verify `.abix.metadata` and `.abix.names` exist
5. `objdump -s` — dump section contents, verify ABIX magic
6. `amc metadata --from-elf` — parse metadata from ELF
7. `amc metadata --verify` — verify consistency
8. Export roundtrip — `.abixmeta` export + verify
9. Error cases — non-ELF rejection, truncated ELF rejection

## Notes

- Only **ELF64 little-endian** is supported (x86_64 / aarch64 Linux).
- PE/COFF support is planned but not yet implemented.
- `.abix.names` contains `const char*` pointers; the actual strings reside in
  `.rodata`. Use `strings <binary> | grep -i amc` to find them.
