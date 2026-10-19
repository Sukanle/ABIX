/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef SKL_ABIX_BINARY_IMAGE_H
#define SKL_ABIX_BINARY_IMAGE_H

// ABIX's capability interface over native binary containers.
//
// ABIX does not unify the ELF / Mach-O / PE *headers* into one struct. It
// unifies the *questions asked of a binary module*: what format is this, what
// target does it describe, where is a named region, which symbols does it
// export. Each format adapter (ELF.h / MachO.h / PE.h) translates its native
// structures into this shared model; nothing above `ABIX/Binary/` sees
// Elf64_Ehdr, mach_header_64 or IMAGE_NT_HEADERS.
//
// Deliberately small for the first version: format / target / sections /
// exports / region lookup. Segments, imports and relocations are future work.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace skl::abix::binary {

// Container format. Format is not the platform: PE != Windows, ELF != Linux,
// Mach-O != macOS (see `OperatingSystem`).
enum class Format : uint8_t {
    unknown,
    elf,
    macho,
    pe
};

enum class OperatingSystem : uint8_t {
    unknown,
    linux,
    windows,
    macos,
    freebsd,
    android
};

enum class Architecture : uint16_t {
    unknown,
    x86,
    x86_64,
    arm32,
    arm64,
    riscv32,
    riscv64
};

enum class Endianness : uint8_t {
    little,
    big
};

// A linker section (ELF section / Mach-O section / PE section), normalised.
// The native spelling is kept in `name` / `segment`; ABIX-level callers ask for
// canonical names (e.g. ".abix.metadata") through `native_section_name`.
struct Section {
    std::string name;      // native name (".text", "__text", ".abix")
    std::string segment;   // Mach-O segment name ("__DATA"); empty otherwise
    uint64_t file_offset = 0;
    uint64_t file_size = 0;
    uint64_t address = 0;   // virtual address
    uint64_t size = 0;      // virtual size
    uint64_t alignment = 0;
    uint32_t type = 0;    // native section type
    uint64_t flags = 0;   // native section flags (ELF sh_flags is 64-bit)
};

// An exported symbol: a named address a consumer may bind to.
struct Export {
    std::string name;
    uint64_t address = 0;   // virtual address
    uint32_t ordinal = 0;
};

// What the image describes: the two-axis target (format, os) plus the machine
// and memory model.
struct Target {
    Format format = Format::unknown;
    OperatingSystem os = OperatingSystem::unknown;
    Architecture architecture = Architecture::unknown;
    Endianness endian = Endianness::little;
    uint8_t pointer_size = 0;   // 4 or 8
    uint64_t image_base = 0;    // preferred load address (0 when not applicable)
};

// The plain result a format adapter produces. Kept separate from BinaryImage so
// adapters never touch the image's internals.
struct ParseResult {
    Target target;
    std::vector<Section> sections;
    std::vector<Export> exports;
};

// Immutable view over a parsed image. It does not own the bytes: the caller
// keeps the buffer alive for as long as the image is used (the same contract as
// the previous `find_binary_section` entry point).
class BinaryImage {
public:
    BinaryImage() = default;
    BinaryImage(const uint8_t *data, size_t size, ParseResult result)
        : data_(data)
        , size_(size)
        , result_(std::move(result)) {}

    const uint8_t *data() const noexcept { return data_; }
    size_t size() const noexcept { return size_; }

    Format format() const noexcept { return result_.target.format; }
    OperatingSystem os() const noexcept { return result_.target.os; }
    Architecture architecture() const noexcept { return result_.target.architecture; }
    Endianness endian() const noexcept { return result_.target.endian; }
    uint8_t pointer_size() const noexcept { return result_.target.pointer_size; }
    uint64_t image_base() const noexcept { return result_.target.image_base; }
    const Target &target() const noexcept { return result_.target; }

    const std::vector<Section> &sections() const noexcept { return result_.sections; }
    const std::vector<Export> &exports() const noexcept { return result_.exports; }

    // `name` is the native section name; use `native_section_name` to translate
    // a canonical ABIX name first.
    const Section *find_section(std::string_view name) const noexcept;
    const Export *find_export(std::string_view name) const noexcept;

    // ABIX region lookup by its canonical name (".abix.metadata", ".abix.names");
    // returns the section that carries the region, or nullptr.
    const Section *find_region(std::string_view canonical) const noexcept;

    // Copies the named region's bytes. Returns false with a descriptive error
    // for a missing section or one with no file content.
    bool read_region(std::string_view canonical, std::vector<uint8_t> &content, std::string &error) const;

private:
    const uint8_t *data_ = nullptr;
    size_t size_ = 0;
    ParseResult result_{};
};

// Translates a canonical ABIX section name into the native spelling of
// `format`. ELF keeps the canonical name; Mach-O uses "__abix_metadata" in the
// "__DATA" segment; PE uses ".abix" / ".abix.n" (PE section names are 8 bytes).
std::string native_section_name(Format format, std::string_view canonical);

// --- shared little-endian byte helpers (also used by the adapters) ---------

namespace detail {

inline bool in_bounds(uint64_t offset, uint64_t length, size_t size) noexcept {
    return offset <= size && length <= size - offset;
}

inline uint16_t read_u16(const uint8_t *data, size_t offset) noexcept {
    return static_cast<uint16_t>(data[offset]) | static_cast<uint16_t>(data[offset + 1]) << 8;
}
inline uint32_t read_u32(const uint8_t *data, size_t offset) noexcept {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
    return value;
}
inline uint64_t read_u64(const uint8_t *data, size_t offset) noexcept {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
    return value;
}

}   // namespace detail

// --- inline implementations ------------------------------------------------

inline std::string native_section_name(Format format, std::string_view canonical) {
    std::string name(canonical);
    if (format == Format::macho) {
        // ".abix.metadata" -> "__abix_metadata" (Mach-O names cap at 16 bytes,
        // so the dot-separated ELF spelling cannot be used verbatim).
        if (name.empty() || name.front() != '.') return name;
        name.front() = '_';
        name.insert(name.begin(), '_');
        for (char &c : name)
            if (c == '.') c = '_';
        return name;
    }
    if (format == Format::pe) {
        // PE section names are 8 bytes; map the canonical region names to short,
        // explicitly chosen (non-`$`) section names. See docs/architecture/pe-inspection.md.
        if (name == ".abix.metadata") return ".abix";
        if (name == ".abix.names") return ".abix.n";
    }
    return name;   // ELF keeps the canonical spelling
}

inline const Section *BinaryImage::find_section(std::string_view name) const noexcept {
    for (const auto &section : result_.sections)
        if (section.name == name) return &section;
    return nullptr;
}

inline const Export *BinaryImage::find_export(std::string_view name) const noexcept {
    for (const auto &record : result_.exports)
        if (record.name == name) return &record;
    return nullptr;
}

inline const Section *BinaryImage::find_region(std::string_view canonical) const noexcept {
    return find_section(native_section_name(result_.target.format, canonical));
}

inline bool BinaryImage::read_region(
    std::string_view canonical, std::vector<uint8_t> &content, std::string &error) const {
    error.clear();
    content.clear();
    const Section *section = find_region(canonical);
    if (section == nullptr) {
        error = "section '" + std::string(canonical) + "' not found";
        return false;
    }
    if (section->file_size == 0 && section->size > 0) {
        error = "section '" + std::string(canonical) + "' has no file content";
        return false;
    }
    if (!detail::in_bounds(section->file_offset, section->file_size, size_)) {
        error = "section '" + std::string(canonical) + "' is out of bounds";
        return false;
    }

    uint64_t offset = section->file_offset;
    uint64_t length = section->file_size;

    // The PE container wraps the Metadata Region in the ABIX private header
    // (docs/architecture/pe-inspection.md): `.abix` = [AbixPeHeader][Region].
    // ELF/Mach-O store the Region directly. Skip the header so callers always
    // receive the Metadata Region.
    if (result_.target.format == Format::pe && length >= 24) {
        const uint8_t *base = data_ + offset;
        if (base[0] == 'A' && base[1] == 'B' && base[2] == 'I' && base[3] == 'X') {
            const uint16_t version = detail::read_u16(base, 4);
            const uint16_t header_size = detail::read_u16(base, 6);
            const uint32_t region_offset = detail::read_u32(base, 12);
            const uint32_t region_size = detail::read_u32(base, 16);
            if (version == 1
                && header_size >= 24
                && region_offset >= header_size
                && static_cast<uint64_t>(region_offset) + region_size <= length) {
                offset += region_offset;
                length = region_size;
            }
        }
    }

    content.assign(data_ + offset, data_ + offset + length);
    return true;
}

}   // namespace skl::abix::binary

// The format adapters and the parser entry point build on the model above; they
// are pulled in here so `#include "ABIX/Binary/BinaryImage.h"` is enough.
#include "ABIX/Binary/ELF.h"
#include "ABIX/Binary/MachO.h"
#include "ABIX/Binary/PE.h"

namespace skl::abix::binary {

inline Format detect_format(const uint8_t *data, size_t size) noexcept {
    if (is_elf(data, size)) return Format::elf;
    if (is_macho(data, size)) return Format::macho;
    if (is_pe(data, size)) return Format::pe;
    return Format::unknown;
}

// Parses any supported container into the shared model. `image` is only
// modified on success.
inline bool parse_binary_image(const uint8_t *data, size_t size, BinaryImage &image, std::string &error) {
    ParseResult result;
    switch (detect_format(data, size)) {
        case Format::elf:
            if (!detail::parse_elf(data, size, result, error)) return false;
            break;
        case Format::macho:
            if (!detail::parse_macho(data, size, result, error)) return false;
            break;
        case Format::pe:
            if (!detail::parse_pe(data, size, result, error)) return false;
            break;
        case Format::unknown: error = "not a supported binary (expected ELF, Mach-O or PE)"; return false;
    }
    image = BinaryImage(data, size, std::move(result));
    return true;
}

}   // namespace skl::abix::binary

#endif   // SKL_ABIX_BINARY_IMAGE_H
