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
#ifndef SKL_ABIX_BINARY_MACHO_H
#define SKL_ABIX_BINARY_MACHO_H

// Mach-O adapter: translates a 32/64-bit little-endian Mach-O image into the
// shared model. Dependency-free (no <mach-o/...>).

#include "SKL/ABIX/Binary/BinaryImage.h"

namespace skl::abix::binary {

inline bool is_macho(const uint8_t *data, size_t size) noexcept {
    if (data == nullptr || size < 4) return false;
    const uint32_t magic = detail::read_u32(data, 0);
    return magic == 0xFEEDFACFu || magic == 0xFEEDFACEu;
}

namespace detail {

inline std::string read_fixed_name(const uint8_t *data, size_t offset, size_t capacity) {
    std::string name;
    for (size_t i = 0; i < capacity; ++i) {
        const char c = static_cast<char>(data[offset + i]);
        if (c == '\0') break;
        name.push_back(c);
    }
    return name;
}

inline bool parse_macho(const uint8_t *data, size_t size, ParseResult &result, std::string &error) {
    error.clear();
    if (!is_macho(data, size)) {
        error = "not a Mach-O file";
        return false;
    }

    const uint32_t magic = read_u32(data, 0);
    const bool is64 = magic == 0xFEEDFACFu;

    constexpr size_t kHeader64Size = 32;
    constexpr size_t kHeader32Size = 28;
    constexpr size_t kNcmdsOffset = 16;
    constexpr size_t kSizeofcmdsOffset = 20;
    constexpr uint32_t kCpuTypeX86 = 7;
    constexpr uint32_t kCpuTypeX86_64 = 0x01000007u;
    constexpr uint32_t kCpuTypeArm = 12;
    constexpr uint32_t kCpuTypeArm64 = 0x0100000Cu;
    constexpr uint32_t kLcSegment = 0x1;
    constexpr uint32_t kLcSegment64 = 0x19;
    constexpr uint32_t kLcSymtab = 0x2;

    const size_t header_size = is64 ? kHeader64Size : kHeader32Size;
    if (size < header_size) {
        error = "truncated Mach-O header";
        return false;
    }

    const uint32_t command_count = read_u32(data, kNcmdsOffset);
    const uint32_t commands_size = read_u32(data, kSizeofcmdsOffset);
    if (!in_bounds(header_size, commands_size, size)) {
        error = "Mach-O load commands are out of bounds";
        return false;
    }

    result.target.format = Format::macho;
    result.target.os = OperatingSystem::macos;
    result.target.endian = Endianness::little;
    result.target.pointer_size = is64 ? 8 : 4;
    result.target.image_base = 0;
    switch (read_u32(data, 4)) {   // cputype
        case kCpuTypeX86:    result.target.architecture = Architecture::x86; break;
        case kCpuTypeX86_64: result.target.architecture = Architecture::x86_64; break;
        case kCpuTypeArm:    result.target.architecture = Architecture::arm32; break;
        case kCpuTypeArm64:  result.target.architecture = Architecture::arm64; break;
        default:             result.target.architecture = Architecture::unknown; break;
    }

    // LC_SYMTAB location (file offsets), used for exports after the walk.
    uint32_t symtab_symoff = 0, symtab_nsyms = 0, symtab_stroff = 0, symtab_strsize = 0;
    bool has_symtab = false;

    const size_t commands_end = header_size + commands_size;
    size_t cursor = header_size;
    bool saw_text_base = false;
    for (uint32_t command = 0; command < command_count; ++command) {
        if (cursor + 8 > commands_end) {
            error = "truncated Mach-O load command";
            return false;
        }
        const uint32_t cmd = read_u32(data, cursor);
        const uint32_t cmdsize = read_u32(data, cursor + 4);
        if (cmdsize < 8 || cursor + cmdsize > commands_end) {
            error = "malformed Mach-O load command";
            return false;
        }

        if (cmd == kLcSymtab && cmdsize >= 24) {
            symtab_symoff = read_u32(data, cursor + 8);
            symtab_nsyms = read_u32(data, cursor + 12);
            symtab_stroff = read_u32(data, cursor + 16);
            symtab_strsize = read_u32(data, cursor + 20);
            has_symtab = true;
        }

        const bool segment64 = is64 && cmd == kLcSegment64;
        const bool segment32 = !is64 && cmd == kLcSegment;
        if (segment64 || segment32) {
            const size_t segment_name_offset = 8;   // segname[16] right after cmd,cmdsize
            const std::string segment_name = read_fixed_name(data, cursor + segment_name_offset, 16);
            const size_t nsects_offset = segment64 ? 64 : 48;
            const size_t section_start = segment64 ? 72 : 56;
            const size_t section_size = segment64 ? 80 : 68;
            if (cmdsize < section_start) {
                error = "malformed Mach-O segment command";
                return false;
            }
            if (!saw_text_base) {
                result.target.image_base = segment64 ? read_u64(data, cursor + 24) : read_u32(data, cursor + 24);
                saw_text_base = true;
            }
            const uint32_t section_count = read_u32(data, cursor + nsects_offset);
            if (section_start + static_cast<size_t>(section_count) * section_size > cmdsize) {
                error = "Mach-O segment section count exceeds command size";
                return false;
            }
            for (uint32_t s = 0; s < section_count; ++s) {
                const size_t header = cursor + section_start + static_cast<size_t>(s) * section_size;
                Section section;
                section.name = read_fixed_name(data, header, 16);
                section.segment = read_fixed_name(data, header + 16, 16);
                if (segment64) {
                    section.address = read_u64(data, header + 32);
                    section.size = read_u64(data, header + 40);
                    section.file_offset = read_u32(data, header + 48);
                    section.alignment = read_u32(data, header + 52);
                    section.flags = read_u32(data, header + 64);
                } else {
                    section.address = read_u32(data, header + 32);
                    section.size = read_u32(data, header + 36);
                    section.file_offset = read_u32(data, header + 40);
                    section.alignment = read_u32(data, header + 44);
                    section.flags = read_u32(data, header + 56);
                }
                // A zero file offset denotes a zero-fill section (no file content).
                section.file_size = (section.file_offset == 0 && section.size > 0) ? 0 : section.size;
                result.sections.push_back(std::move(section));
            }
        }
        cursor += cmdsize;
    }

    // Exported symbols: external, defined-in-a-section entries of LC_SYMTAB.
    constexpr uint8_t kNExt = 0x01;
    constexpr uint8_t kNType = 0x0e;
    constexpr uint8_t kNSect = 0x0e;
    if (has_symtab
        && in_bounds(symtab_symoff, static_cast<uint64_t>(symtab_nsyms) * (is64 ? 16 : 12), size)
        && in_bounds(symtab_stroff, symtab_strsize, size)) {
        const size_t entry_size = is64 ? 16 : 12;
        for (uint32_t i = 0; i < symtab_nsyms; ++i) {
            const size_t entry = static_cast<size_t>(symtab_symoff) + static_cast<size_t>(i) * entry_size;
            const uint32_t n_strx = read_u32(data, entry + 0);
            const uint8_t n_type = data[entry + 4];
            const uint64_t n_value = is64 ? read_u64(data, entry + 8) : read_u32(data, entry + 8);
            if ((n_type & kNExt) == 0 || (n_type & kNType) != kNSect) continue;
            if (n_strx == 0) continue;
            std::string name;
            size_t cursor2 = static_cast<size_t>(symtab_stroff) + n_strx;
            const size_t end = static_cast<size_t>(symtab_stroff) + static_cast<size_t>(symtab_strsize);
            while (cursor2 < end && data[cursor2] != 0)
                name.push_back(static_cast<char>(data[cursor2++]));
            if (!name.empty()) result.exports.push_back({std::move(name), n_value, 0});
        }
    }
    return true;
}

}   // namespace detail
}   // namespace skl::abix::binary

#endif   // SKL_ABIX_BINARY_MACHO_H
