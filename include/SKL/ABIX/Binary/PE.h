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
#ifndef SKL_ABIX_BINARY_PE_H
#define SKL_ABIX_BINARY_PE_H

// PE adapter: translates a PE32 / PE32+ image into the shared model.
// Dependency-free (no <windows.h> / <winnt.h>). Section names are 8 bytes, so
// the adapter also carries the .abix region lookup; see
// docs/architecture/pe-inspection.md for why the region is not identified by
// its (truncated) section name alone.

#include "SKL/ABIX/Binary/BinaryImage.h"

namespace skl::abix::binary {

inline bool is_pe(const uint8_t *data, size_t size) noexcept {
    if (data == nullptr || size < 0x40) return false;
    if (data[0] != 'M' || data[1] != 'Z') return false;
    const uint32_t nt = detail::read_u32(data, 0x3C);
    if (!detail::in_bounds(nt, 4, size)) return false;
    return data[nt] == 'P' && data[nt + 1] == 'E' && data[nt + 2] == 0 && data[nt + 3] == 0;
}

namespace detail {

inline std::string read_pe_name(const uint8_t *data, size_t offset) {
    std::string name;
    for (size_t i = 0; i < 8; ++i) {
        const char c = static_cast<char>(data[offset + i]);
        if (c == '\0') break;
        name.push_back(c);
    }
    return name;
}

inline bool parse_pe(const uint8_t *data, size_t size, ParseResult &result, std::string &error) {
    error.clear();
    if (!is_pe(data, size)) {
        error = "not a PE image";
        return false;
    }

    constexpr size_t kFileHeaderSize = 20;
    constexpr size_t kSectionHeaderSize = 40;

    const uint32_t nt = read_u32(data, 0x3C);
    const size_t coff = static_cast<size_t>(nt) + 4;
    // `is_pe` only guarantees Signature lies in the buffer (nt + 4 <= size). The
    // COFF file header that follows may still not fit: a truncated image with
    // e_lfanew near EOF must be rejected before the fields are read.
    if (!in_bounds(coff, kFileHeaderSize, size)) {
        error = "truncated PE header";
        return false;
    }
    const uint16_t machine = read_u16(data, coff + 0);
    const uint16_t section_count = read_u16(data, coff + 2);
    const uint16_t optional_size = read_u16(data, coff + 16);
    const size_t optional = coff + kFileHeaderSize;
    if (!in_bounds(optional, optional_size, size)) {
        error = "PE optional header is out of bounds";
        return false;
    }
    if (optional_size < 2) {
        error = "truncated PE optional header";
        return false;
    }

    const uint16_t optional_magic = read_u16(data, optional);
    const bool pe32plus = optional_magic == 0x20B;
    if (!pe32plus && optional_magic != 0x10B) {
        error = "unsupported PE optional header magic";
        return false;
    }

    // ImageBase and the data directory offset differ between PE32 and PE32+.
    uint64_t image_base = 0;
    size_t data_directory = 0;
    if (pe32plus) {
        if (optional_size < 112 + 8) {
            error = "PE32+ optional header is too small";
            return false;
        }
        image_base = read_u64(data, optional + 24);
        data_directory = optional + 112;
    } else {
        if (optional_size < 96 + 8) {
            error = "PE32 optional header is too small";
            return false;
        }
        image_base = read_u32(data, optional + 28);
        data_directory = optional + 96;
    }

    result.target.format = Format::pe;
    result.target.os = OperatingSystem::windows;
    result.target.endian = Endianness::little;
    result.target.pointer_size = pe32plus ? 8 : 4;
    result.target.image_base = image_base;
    switch (machine) {
        case 0x014c: result.target.architecture = Architecture::x86; break;
        case 0x8664: result.target.architecture = Architecture::x86_64; break;
        case 0x01c0:
        case 0x01c4: result.target.architecture = Architecture::arm32; break;
        case 0xaa64: result.target.architecture = Architecture::arm64; break;
        default:     result.target.architecture = Architecture::unknown; break;
    }

    const size_t section_table = optional + optional_size;
    if (!in_bounds(section_table, static_cast<uint64_t>(section_count) * kSectionHeaderSize, size)) {
        error = "PE section table is out of bounds";
        return false;
    }

    // Local (RVA, raw) ranges for later RVA -> file-offset resolution.
    struct SectionRange {
        uint32_t rva;
        uint32_t virtual_size;
        uint32_t raw_size;
        uint32_t raw_offset;
    };
    std::vector<SectionRange> ranges;
    ranges.reserve(section_count);

    result.sections.reserve(section_count);
    for (uint16_t i = 0; i < section_count; ++i) {
        const size_t header = section_table + static_cast<size_t>(i) * kSectionHeaderSize;
        const uint32_t virtual_size = read_u32(data, header + 8);
        const uint32_t virtual_address = read_u32(data, header + 12);
        const uint32_t raw_size = read_u32(data, header + 16);
        const uint32_t raw_offset = read_u32(data, header + 20);

        Section section;
        section.name = read_pe_name(data, header);
        section.address = image_base + virtual_address;
        section.size = virtual_size;
        section.file_offset = raw_offset;
        section.file_size = raw_size;
        section.flags = read_u32(data, header + 36);
        result.sections.push_back(std::move(section));
        ranges.push_back({virtual_address, virtual_size, raw_size, raw_offset});
    }

    const auto rva_to_offset = [&](uint32_t rva, size_t &offset) -> bool {
        for (const auto &range : ranges) {
            const uint32_t span = range.virtual_size > range.raw_size ? range.virtual_size : range.raw_size;
            if (rva < range.rva || rva >= range.rva + span) continue;
            const uint32_t delta = rva - range.rva;
            if (delta >= range.raw_size) return false;   // in virtual-only (uninitialised) tail
            offset = static_cast<size_t>(range.raw_offset) + delta;
            return in_bounds(offset, 1, size);
        }
        return false;
    };

    // Export directory is data directory entry 0.
    const uint32_t export_rva = read_u32(data, data_directory + 0);
    const uint32_t export_size = read_u32(data, data_directory + 4);
    size_t export_offset = 0;
    if (export_rva != 0
        && export_size >= 40
        && rva_to_offset(export_rva, export_offset)
        && in_bounds(export_offset, 40, size)) {
        const uint32_t ordinal_base = read_u32(data, export_offset + 16);
        const uint32_t name_count = read_u32(data, export_offset + 24);
        const uint32_t functions_rva = read_u32(data, export_offset + 28);
        const uint32_t names_rva = read_u32(data, export_offset + 32);
        const uint32_t ordinals_rva = read_u32(data, export_offset + 36);
        size_t names_offset = 0, functions_offset = 0, ordinals_offset = 0;
        if (name_count != 0
            && rva_to_offset(names_rva, names_offset)
            && rva_to_offset(functions_rva, functions_offset)
            && rva_to_offset(ordinals_rva, ordinals_offset)) {
            for (uint32_t i = 0; i < name_count; ++i) {
                if (!in_bounds(names_offset + static_cast<size_t>(i) * 4, 4, size)) break;
                if (!in_bounds(ordinals_offset + static_cast<size_t>(i) * 2, 2, size)) break;
                size_t name_offset = 0;
                if (!rva_to_offset(read_u32(data, names_offset + static_cast<size_t>(i) * 4), name_offset)) break;
                const uint16_t ordinal = read_u16(data, ordinals_offset + static_cast<size_t>(i) * 2);
                if (!in_bounds(functions_offset + static_cast<size_t>(ordinal) * 4, 4, size)) break;
                const uint32_t function_rva = read_u32(data, functions_offset + static_cast<size_t>(ordinal) * 4);
                std::string name;
                size_t cursor = name_offset;
                while (cursor < size && data[cursor] != 0)
                    name.push_back(static_cast<char>(data[cursor++]));
                if (name.empty()) continue;
                result.exports.push_back({std::move(name), image_base + function_rva, ordinal_base + ordinal});
            }
        }
    }
    return true;
}

}   // namespace detail
}   // namespace skl::abix::binary

#endif   // SKL_ABIX_BINARY_PE_H
