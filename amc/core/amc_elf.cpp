#include "amc_elf.h"

namespace amc {
namespace {

constexpr uint8_t kElfClass64 = 2;
constexpr uint8_t kElfDataLittle = 1;

// ELF64 header / section header offsets.
constexpr size_t kIdentSize = 16;
constexpr size_t kOffsetSectionHeaderOffset = 0x28;   // e_shoff
constexpr size_t kOffsetSectionHeaderSize = 0x3A;     // e_shentsize
constexpr size_t kOffsetSectionCount = 0x3C;          // e_shnum
constexpr size_t kOffsetSectionNames = 0x3E;          // e_shstrndx
constexpr size_t kElf64SectionHeaderSize = 64;
constexpr size_t kSectionNameOffset = 0;       // sh_name
constexpr size_t kSectionTypeOffset = 4;       // sh_type
constexpr size_t kSectionFlagsOffset = 8;      // sh_flags
constexpr size_t kSectionAddressOffset = 16;   // sh_addr
constexpr size_t kSectionFileOffset = 24;      // sh_offset
constexpr size_t kSectionSizeOffset = 32;      // sh_size

bool in_bounds(uint64_t offset, uint64_t length, size_t size) { return offset <= size && length <= size - offset; }

uint16_t read_u16(const uint8_t *data, size_t offset) {
    return static_cast<uint16_t>(data[offset]) | static_cast<uint16_t>(data[offset + 1]) << 8;
}
uint32_t read_u32(const uint8_t *data, size_t offset) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
    return value;
}
uint64_t read_u64(const uint8_t *data, size_t offset) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
    return value;
}

}   // namespace

bool is_elf(const uint8_t *data, size_t size) noexcept {
    return data != nullptr && size >= 4 && data[0] == 0x7f && data[1] == 'E' && data[2] == 'L' && data[3] == 'F';
}

bool read_elf_sections(const uint8_t *data, size_t size, std::vector<ElfSection> &sections, std::string &error) {
    error.clear();
    sections.clear();
    if (!is_elf(data, size)) {
        error = "not an ELF file";
        return false;
    }
    if (size < kIdentSize) {
        error = "truncated ELF identification";
        return false;
    }
    if (data[4] != kElfClass64) {
        error = "unsupported ELF class (only ELF64 is supported)";
        return false;
    }
    if (data[5] != kElfDataLittle) {
        error = "unsupported ELF endianness (only little-endian is supported)";
        return false;
    }
    if (size < kOffsetSectionNames + 2) {
        error = "truncated ELF header";
        return false;
    }

    const uint64_t section_header_offset = read_u64(data, kOffsetSectionHeaderOffset);
    const uint16_t section_header_size = read_u16(data, kOffsetSectionHeaderSize);
    const uint16_t section_count = read_u16(data, kOffsetSectionCount);
    const uint16_t section_names_index = read_u16(data, kOffsetSectionNames);

    if (section_header_size != kElf64SectionHeaderSize) {
        error = "unexpected ELF64 section header size";
        return false;
    }
    if (section_count == 0) {
        error = "ELF has no section headers";
        return false;
    }
    if (!in_bounds(section_header_offset, static_cast<uint64_t>(section_header_size) * section_count, size)) {
        error = "ELF section header table is out of bounds";
        return false;
    }
    if (section_names_index >= section_count) {
        error = "ELF section name table index is out of range";
        return false;
    }

    const auto header_at = [&](uint16_t index) {
        return static_cast<size_t>(section_header_offset) + static_cast<size_t>(index) * kElf64SectionHeaderSize;
    };
    const size_t names_header = header_at(section_names_index);
    const uint64_t names_offset = read_u64(data, names_header + kSectionFileOffset);
    const uint64_t names_size = read_u64(data, names_header + kSectionSizeOffset);
    if (!in_bounds(names_offset, names_size, size)) {
        error = "ELF section name table is out of bounds";
        return false;
    }

    sections.reserve(section_count);
    for (uint16_t i = 0; i < section_count; ++i) {
        const size_t header = header_at(i);
        ElfSection section;
        section.type = read_u32(data, header + kSectionTypeOffset);
        section.flags = read_u64(data, header + kSectionFlagsOffset);
        section.address = read_u64(data, header + kSectionAddressOffset);
        section.offset = read_u64(data, header + kSectionFileOffset);
        section.size = read_u64(data, header + kSectionSizeOffset);

        const uint32_t name_offset = read_u32(data, header + kSectionNameOffset);
        if (name_offset < names_size) {
            const size_t begin = static_cast<size_t>(names_offset) + name_offset;
            const size_t end = static_cast<size_t>(names_offset) + static_cast<size_t>(names_size);
            size_t cursor = begin;
            std::string name;
            while (cursor < end && data[cursor] != 0)
                name.push_back(static_cast<char>(data[cursor++]));
            section.name = std::move(name);
        }
        sections.push_back(std::move(section));
    }
    return true;
}

bool find_elf_section(
    const uint8_t *data, size_t size, std::string_view name, std::vector<uint8_t> &content, std::string &error) {
    std::vector<ElfSection> sections;
    if (!read_elf_sections(data, size, sections, error)) return false;
    for (const auto &section : sections) {
        if (section.name != name) continue;
        if (!in_bounds(section.offset, section.size, size)) {
            error = "section '" + std::string(name) + "' is out of bounds";
            return false;
        }
        content.assign(data + section.offset, data + section.offset + section.size);
        return true;
    }
    error = "section '" + std::string(name) + "' not found";
    return false;
}

}   // namespace amc
