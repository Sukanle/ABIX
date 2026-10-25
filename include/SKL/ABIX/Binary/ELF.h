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
#ifndef SKL_ABIX_BINARY_ELF_H
#define SKL_ABIX_BINARY_ELF_H

// ELF adapter: translates an ELF64 little-endian image into the shared model.
// Dependency-free (no <elf.h>): the fields AMC needs are read from documented
// offsets, so this header stays usable in the header-only ABIX tree.

#include "SKL/ABIX/Binary/BinaryImage.h"

namespace skl::abix::binary {

inline bool is_elf(const uint8_t *data, size_t size) noexcept {
    return data != nullptr && size >= 4 && data[0] == 0x7f && data[1] == 'E' && data[2] == 'L' && data[3] == 'F';
}

namespace detail {

inline bool parse_elf(const uint8_t *data, size_t size, ParseResult &result, std::string &error) {
    error.clear();
    if (!is_elf(data, size)) {
        error = "not an ELF file";
        return false;
    }
    if (size < 16) {
        error = "truncated ELF identification";
        return false;
    }
    if (data[4] != 2) {
        error = "unsupported ELF class (only ELF64 is supported)";
        return false;
    }
    if (data[5] != 1) {
        error = "unsupported ELF endianness (only little-endian is supported)";
        return false;
    }
    if (size < 0x40) {
        error = "truncated ELF header";
        return false;
    }

    constexpr size_t kSectionHeaderSize = 64;
    const uint64_t shoff = read_u64(data, 0x28);
    const uint16_t shentsize = read_u16(data, 0x3A);
    const uint16_t shnum = read_u16(data, 0x3C);
    const uint16_t shstrndx = read_u16(data, 0x3E);

    if (shentsize != kSectionHeaderSize) {
        error = "unexpected ELF64 section header size";
        return false;
    }
    if (shnum == 0) {
        error = "ELF has no section headers";
        return false;
    }
    if (!in_bounds(shoff, static_cast<uint64_t>(shentsize) * shnum, size)) {
        error = "ELF section header table is out of bounds";
        return false;
    }
    if (shstrndx >= shnum) {
        error = "ELF section name table index is out of range";
        return false;
    }

    const auto header_at = [&](uint16_t index) {
        return static_cast<size_t>(shoff) + static_cast<size_t>(index) * kSectionHeaderSize;
    };
    const size_t names_header = header_at(shstrndx);
    const uint64_t names_offset = read_u64(data, names_header + 24);
    const uint64_t names_size = read_u64(data, names_header + 32);
    if (!in_bounds(names_offset, names_size, size)) {
        error = "ELF section name table is out of bounds";
        return false;
    }

    const auto name_of = [&](uint32_t name_offset) {
        std::string name;
        if (name_offset >= names_size) return name;
        size_t cursor = static_cast<size_t>(names_offset) + name_offset;
        const size_t end = static_cast<size_t>(names_offset) + static_cast<size_t>(names_size);
        while (cursor < end && data[cursor] != 0)
            name.push_back(static_cast<char>(data[cursor++]));
        return name;
    };

    result.target.format = Format::elf;
    result.target.endian = Endianness::little;
    result.target.pointer_size = 8;
    result.target.image_base = 0;
    result.target.os = data[7] == 9 ? OperatingSystem::freebsd : OperatingSystem::linux;
    switch (read_u16(data, 0x12)) {   // e_machine
        case 3:   result.target.architecture = Architecture::x86; break;
        case 62:  result.target.architecture = Architecture::x86_64; break;
        case 40:  result.target.architecture = Architecture::arm32; break;
        case 183: result.target.architecture = Architecture::arm64; break;
        case 243: result.target.architecture = Architecture::riscv64; break;
        default:  result.target.architecture = Architecture::unknown; break;
    }

    constexpr uint32_t kSectionNoBits = 8;   // SHT_NOBITS (.bss): no file content

    result.sections.reserve(shnum);
    for (uint16_t i = 0; i < shnum; ++i) {
        const size_t header = header_at(i);
        Section section;
        section.type = read_u32(data, header + 4);
        section.flags = read_u64(data, header + 8);
        section.address = read_u64(data, header + 16);
        section.file_offset = read_u64(data, header + 24);
        section.size = read_u64(data, header + 32);
        section.file_size = section.type == kSectionNoBits ? 0 : section.size;
        section.alignment = read_u64(data, header + 48);
        section.name = name_of(read_u32(data, header + 0));
        result.sections.push_back(std::move(section));
    }

    // Exported symbols come from .dynsym (GLOBAL/WEAK, defined), resolved through
    // the string table referenced by .dynsym's sh_link.
    size_t dynsym_index = result.sections.size();
    for (size_t i = 0; i < result.sections.size(); ++i) {
        if (result.sections[i].name == ".dynsym") {
            dynsym_index = i;
            break;
        }
    }
    if (dynsym_index < result.sections.size()) {
        const size_t dynsym_header = header_at(static_cast<uint16_t>(dynsym_index));
        const uint32_t link = read_u32(data, dynsym_header + 40);   // sh_link
        const uint64_t entsize = read_u64(data, dynsym_header + 56);
        const uint64_t step = entsize != 0 ? entsize : 24;
        const Section &dynsym = result.sections[dynsym_index];
        if (link < shnum && in_bounds(dynsym.file_offset, dynsym.file_size, size)) {
            const size_t link_header = header_at(static_cast<uint16_t>(link));
            const uint64_t str_offset = read_u64(data, link_header + 24);
            const uint64_t str_size = read_u64(data, link_header + 32);
            if (in_bounds(str_offset, str_size, size)) {
                for (uint64_t off = 0; off + 24 <= dynsym.file_size; off += step) {
                    const size_t entry = static_cast<size_t>(dynsym.file_offset) + static_cast<size_t>(off);
                    const uint32_t st_name = read_u32(data, entry + 0);
                    const uint8_t st_info = data[entry + 4];
                    const uint16_t st_shndx = read_u16(data, entry + 6);
                    const uint64_t st_value = read_u64(data, entry + 8);
                    const uint8_t binding = static_cast<uint8_t>(st_info >> 4);
                    if (st_shndx == 0 || st_name == 0) continue;   // undefined / anonymous
                    if (binding != 1 && binding != 2) continue;    // GLOBAL / WEAK
                    std::string name;
                    size_t cursor = static_cast<size_t>(str_offset) + st_name;
                    const size_t end = static_cast<size_t>(str_offset) + static_cast<size_t>(str_size);
                    while (cursor < end && data[cursor] != 0)
                        name.push_back(static_cast<char>(data[cursor++]));
                    if (!name.empty()) result.exports.push_back({std::move(name), st_value, 0});
                }
            }
        }
    }
    return true;
}

}   // namespace detail
}   // namespace skl::abix::binary

#endif   // SKL_ABIX_BINARY_ELF_H
