# The ABIX BinaryImage Capability Layer

<p align="center">
  <a href="binary-image_zh.md">中文</a> · English
</p>

> **Status: implemented (first version).** The layer lives in `ABIX/Binary/` and is wired into AMC; ELF, Mach-O and PE are supported for the capabilities below.

<details>

<summary>Contents</summary>

- [Principle: unify capability, not format](#principle-unify-capability-not-format)
- [Layers](#layers)
- [Format, OS and architecture are separate axes](#format-os-and-architecture-are-separate-axes)
- [The first-version interface](#the-first-version-interface)
- [Format adapters](#format-adapters)
- [Native escape hatch](#native-escape-hatch)
- [Scope of the first version](#scope-of-the-first-version)
- [Consumers](#consumers)

</details>

## Principle: unify capability, not format

ABIX does **not** try to fold `Elf64_Ehdr`, `mach_header_64` and `IMAGE_NT_HEADERS` into a single "binary header" struct. Those containers describe different things — ELF's program/section headers, Mach-O's segment/load-command model, PE's COFF header, section table and data directories — and a merged struct would end up as a pile of `#ifdef`s.

What ABIX unifies is the **question asked of a binary module**: what format is this, what target does it describe, where is a named region, which symbols does it export. Each adapter translates its native structures into that shared model; nothing above `ABIX/Binary/` sees a container header.

> **Format is implementation; capability is the interface.**

## Layers

```text
                    BinaryImage (ABIX/Binary/BinaryImage.h)
                              │
              ┌───────────────┼───────────────┐
             ELF.h          MachO.h          PE.h          (adapters)
              │               │               │
           Elf64_*        Mach-O LC        IMAGE_NT_HEADERS / section table

  Above the line, consumers see: Target · Section · Export · Region
```

The core runtime (`ABIX/Runtime`, `ABIX/Interface`, `ABIX/Metadata`) never includes `<elf.h>`, `<mach-o/...>` or `<windows.h>`; the adapters are dependency-free and read from documented byte offsets.

## Format, OS and architecture are separate axes

PE is not Windows, ELF is not Linux, Mach-O is not macOS. A container can host several platforms (`ELF + FreeBSD`, `ELF + Android`, `PE + Windows arm64`), so the model keeps three independent enums:

```cpp
enum class Format : uint8_t { unknown, elf, macho, pe };
enum class OperatingSystem : uint8_t { unknown, linux, windows, macos, freebsd, android };
enum class Architecture : uint16_t { unknown, x86, x86_64, arm32, arm64, riscv32, riscv64 };
```

## The first-version interface

Deliberately small — enough for `.abix` auto-discovery, interface registration and native binding, and no more:

```cpp
class BinaryImage {
public:
    Format format() const noexcept;
    OperatingSystem os() const noexcept;
    Architecture architecture() const noexcept;
    Endianness endian() const noexcept;
    uint8_t pointer_size() const noexcept;
    uint64_t image_base() const noexcept;
    const Target &target() const noexcept;

    const std::vector<Section> &sections() const noexcept;
    const std::vector<Export> &exports() const noexcept;

    const Section *find_section(std::string_view name) const noexcept;
    const Export  *find_export(std::string_view name) const noexcept;

    const Section *find_region(std::string_view canonical) const noexcept;
    bool read_region(std::string_view canonical, std::vector<uint8_t> &out, std::string &error) const;
};

bool parse_binary_image(const uint8_t *data, size_t size, BinaryImage &image, std::string &error);
```

- **`Section`** — normalised linker section: `name`, `segment`, `file_offset`, `file_size`, `address`, `size`, `alignment`, `type`, `flags`.
- **`Export`** — `name`, `address`, `ordinal`.
- **`Target`** — the `format` / `os` / `architecture` / `endian` / `pointer_size` / `image_base` bundle.
- **`find_region` / `read_region`** — the ABIX region accessor: a canonical name (`.abix.metadata`, `.abix.names`) is translated to the container's native spelling via `native_section_name`, and `read_region` returns the Metadata Region (for PE it also unwraps the private header; see [pe-inspection.md](pe-inspection.md)).

`BinaryImage` does not own the bytes: the caller keeps the buffer alive while the image is in use (the same contract as the previous `find_binary_section`).

## Format adapters

| Adapter | Reads | Exports from |
|---------|-------|--------------|
| `ABIX/Binary/ELF.h` | ELF64 little-endian | `.dynsym` (GLOBAL/WEAK, defined) |
| `ABIX/Binary/MachO.h` | 32/64-bit little-endian Mach-O | `LC_SYMTAB` (external, `N_SECT`) |
| `ABIX/Binary/PE.h` | PE32 / PE32+ | `IMAGE_EXPORT_DIRECTORY` |

Each adapter exposes a cheap `is_*` magic check and a `detail::parse_*` that fills a `ParseResult`. PE additionally resolves RVAs to file offsets (for the export name/function arrays) and computes `address = image_base + RVA`.

## Native escape hatch

ABIX-level callers use canonical names and never touch a container header. When a caller genuinely needs the native structure, it reads it from the image bytes directly — `native()` is deliberately **not** part of the general API. If a second consumer needs a native field, the field is promoted into `Section` / `Export` / `Target` instead of exposing the container header.

## Scope of the first version

Included: `format`, `target`, `sections()`, `exports()`, `find_region()`.

Deliberately **not** included yet (to avoid becoming a new `llvm::ObjectFile`):

- **Segments** — the loader/memory view, distinct from linker sections.
- **Imports** — PE import directory, ELF `.rela.plt` targets, Mach-O bind info.
- **Relocations** — a full relocation model.
- **A universal symbol resolver** — `exports()` is a flat list, not a resolver.
- **Bitcode / fat / universal images** — thin images only, as before.

Segments, imports and relocations are added only when a concrete ABIX workflow needs them.

## Consumers

- **AMC** — `amc metadata --from-elf`, `amc inspect`, `amc verify`, `amc publish` and the LLDB plugin go through a thin compatibility shim (`AMC/include/AMC/Core/ELF.h`) that now also handles PE.
- **Interface auto-discovery (planned)** — scan `exports()` for `abix_module_query_v1`, pair it with the embedded region, and register the interface without the runtime ever knowing the container:

  ```text
  libfoo.so / foo.dll / libfoo.dylib
        │
        ▼
  BinaryImage::parse ── exports()  ── abix_module_query_v1
        │              └ find_region(".abix.metadata") ── ModuleManifest
        ▼
  InterfaceRegistry → InterfaceId → native binding (resolve once)
  ```
