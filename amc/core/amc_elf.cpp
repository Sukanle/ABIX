#include "amc_elf.h"

namespace amc {
namespace {

// --- shared little-endian helpers -----------------------------------------

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

// Reads a fixed-size, NUL-padded name field (Mach-O `sectname` / `segname`).
std::string read_fixed_name(const uint8_t *data, size_t offset, size_t capacity) {
    std::string name;
    for (size_t i = 0; i < capacity; ++i) {
        const char c = static_cast<char>(data[offset + i]);
        if (c == '\0') break;
        name.push_back(c);
    }
    return name;
}

// --- ELF64 constants -------------------------------------------------------

constexpr uint8_t kElfClass64 = 2;
constexpr uint8_t kElfDataLittle = 1;

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

// --- Mach-O constants ------------------------------------------------------

constexpr uint32_t kMachMagic64 = 0xFEEDFACFu;
constexpr uint32_t kMachMagic32 = 0xFEEDFACEu;

constexpr size_t kMachHeader64Size = 32;   // magic..reserved
constexpr size_t kMachHeader32Size = 28;
constexpr size_t kMachNcmdsOffset = 16;      // ncmds
constexpr size_t kMachSizeofcmdsOffset = 20; // sizeofcmds

constexpr uint32_t kLcSegment = 0x1;
constexpr uint32_t kLcSegment64 = 0x19;

// LC_SEGMENT_64: cmd,cmdsize,segname[16],vmaddr,vmsize,fileoff,filesize,
//                maxprot,initprot,nsects,flags (4+4+16+8*4+4*4=72 bytes)
constexpr size_t kSegment64NsectsOffset = 64;
constexpr size_t kSegment64SectionStart = 72;
constexpr size_t kSegment64SectionSize = 80;
// section_64: sectname[16],segname[16],addr,size,offset,align,reloff,nreloc,
//             flags,reserved1,reserved2,reserved3
constexpr size_t kSection64SegmentNameOffset = 16;
constexpr size_t kSection64AddressOffset = 32;
constexpr size_t kSection64SizeOffset = 40;
constexpr size_t kSection64FileOffset = 48;
constexpr size_t kSection64FlagsOffset = 64;

// LC_SEGMENT: cmd,cmdsize,segname[16],vmaddr,vmsize,fileoff,filesize,
//             maxprot,initprot,nsects,flags (4+4+16+4*4+4*4=56 bytes)
constexpr size_t kSegment32NsectsOffset = 48;
constexpr size_t kSegment32SectionStart = 56;
constexpr size_t kSegment32SectionSize = 68;
// section: sectname[16],segname[16],addr,size,offset,align,reloff,nreloc,flags,...
constexpr size_t kSection32SegmentNameOffset = 16;
constexpr size_t kSection32AddressOffset = 32;
constexpr size_t kSection32SizeOffset = 36;
constexpr size_t kSection32FileOffset = 40;
constexpr size_t kSection32FlagsOffset = 56;

constexpr size_t kMachSectionNameCapacity = 16;
constexpr size_t kMachSegmentNameCapacity = 16;

}   // namespace

BinaryFormat detect_binary_format(const uint8_t *data, size_t size) noexcept {
    if (is_elf(data, size)) return BinaryFormat::elf;
    if (is_macho(data, size)) return BinaryFormat::macho;
    return BinaryFormat::unknown;
}

bool is_elf(const uint8_t *data, size_t size) noexcept {
    return data != nullptr && size >= 4 && data[0] == 0x7f && data[1] == 'E' && data[2] == 'L' && data[3] == 'F';
}

bool is_macho(const uint8_t *data, size_t size) noexcept {
    if (data == nullptr || size < 4) return false;
    const uint32_t magic = read_u32(data, 0);
    return magic == kMachMagic64 || magic == kMachMagic32;
}

bool is_binary(const uint8_t *data, size_t size) noexcept {
    return detect_binary_format(data, size) != BinaryFormat::unknown;
}

bool read_elf_sections(const uint8_t *data, size_t size, std::vector<BinarySection> &sections, std::string &error) {
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
        BinarySection section;
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

bool read_macho_sections(const uint8_t *data, size_t size, std::vector<BinarySection> &sections, std::string &error) {
    error.clear();
    sections.clear();
    if (!is_macho(data, size)) {
        error = "not a Mach-O file";
        return false;
    }

    const uint32_t magic = read_u32(data, 0);
    const bool is64 = magic == kMachMagic64;
    const size_t header_size = is64 ? kMachHeader64Size : kMachHeader32Size;
    if (size < header_size) {
        error = "truncated Mach-O header";
        return false;
    }

    const uint32_t command_count = read_u32(data, kMachNcmdsOffset);
    const uint32_t commands_size = read_u32(data, kMachSizeofcmdsOffset);
    if (!in_bounds(header_size, commands_size, size)) {
        error = "Mach-O load commands are out of bounds";
        return false;
    }

    const size_t commands_end = header_size + commands_size;
    size_t cursor = header_size;
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

        const bool segment64 = is64 && cmd == kLcSegment64;
        const bool segment32 = !is64 && cmd == kLcSegment;
        if (segment64 || segment32) {
            const size_t nsects_offset = segment64 ? kSegment64NsectsOffset : kSegment32NsectsOffset;
            const size_t section_start = segment64 ? kSegment64SectionStart : kSegment32SectionStart;
            const size_t section_size = segment64 ? kSegment64SectionSize : kSegment32SectionSize;
            if (cmdsize < section_start) {
                error = "malformed Mach-O segment command";
                return false;
            }
            const uint32_t section_count = read_u32(data, cursor + nsects_offset);
            if (section_start + static_cast<size_t>(section_count) * section_size > cmdsize) {
                error = "Mach-O segment section count exceeds command size";
                return false;
            }
            for (uint32_t s = 0; s < section_count; ++s) {
                const size_t header = cursor + section_start + static_cast<size_t>(s) * section_size;
                BinarySection section;
                section.name = read_fixed_name(data, header, kMachSectionNameCapacity);
                if (segment64) {
                    section.segment = read_fixed_name(data, header + kSection64SegmentNameOffset, kMachSegmentNameCapacity);
                    section.address = read_u64(data, header + kSection64AddressOffset);
                    section.size = read_u64(data, header + kSection64SizeOffset);
                    section.offset = read_u32(data, header + kSection64FileOffset);
                    section.flags = read_u32(data, header + kSection64FlagsOffset);
                } else {
                    section.segment = read_fixed_name(data, header + kSection32SegmentNameOffset, kMachSegmentNameCapacity);
                    section.address = read_u32(data, header + kSection32AddressOffset);
                    section.size = read_u32(data, header + kSection32SizeOffset);
                    section.offset = read_u32(data, header + kSection32FileOffset);
                    section.flags = read_u32(data, header + kSection32FlagsOffset);
                }
                sections.push_back(std::move(section));
            }
        }
        cursor += cmdsize;
    }
    return true;
}

namespace {

bool find_section_in(const std::vector<BinarySection> &sections, std::string_view name, const uint8_t *data, size_t size,
    std::vector<uint8_t> &content, std::string &error) {
    for (const auto &section : sections) {
        if (section.name != name) continue;
        // Zerofill sections (e.g. __bss) have no file content.
        if (section.offset == 0 && section.size > 0) {
            error = "section '" + std::string(name) + "' has no file content";
            return false;
        }
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

}   // namespace

std::string native_section_name(BinaryFormat format, std::string_view canonical) {
    std::string name(canonical);
    if (format != BinaryFormat::macho || name.empty() || name.front() != '.') return name;
    // ".abix.metadata" -> "__abix_metadata" (Mach-O names are capped at 16
    // bytes, so the dot-separated ELF spelling cannot be used verbatim).
    name.front() = '_';
    name.insert(name.begin(), '_');
    for (char &c : name)
        if (c == '.') c = '_';
    return name;
}

bool find_elf_section(
    const uint8_t *data, size_t size, std::string_view name, std::vector<uint8_t> &content, std::string &error) {
    std::vector<BinarySection> sections;
    if (!read_elf_sections(data, size, sections, error)) return false;
    return find_section_in(sections, name, data, size, content, error);
}

bool find_macho_section(
    const uint8_t *data, size_t size, std::string_view name, std::vector<uint8_t> &content, std::string &error) {
    std::vector<BinarySection> sections;
    if (!read_macho_sections(data, size, sections, error)) return false;
    return find_section_in(sections, name, data, size, content, error);
}

bool read_binary_sections(const uint8_t *data, size_t size, std::vector<BinarySection> &sections, std::string &error) {
    switch (detect_binary_format(data, size)) {
        case BinaryFormat::elf: return read_elf_sections(data, size, sections, error);
        case BinaryFormat::macho: return read_macho_sections(data, size, sections, error);
        case BinaryFormat::unknown:
            error = "not a supported binary (expected ELF or Mach-O)";
            sections.clear();
            return false;
    }
    error = "not a supported binary (expected ELF or Mach-O)";
    sections.clear();
    return false;
}

bool find_binary_section(
    const uint8_t *data, size_t size, std::string_view name, std::vector<uint8_t> &content, std::string &error) {
    const BinaryFormat format = detect_binary_format(data, size);
    if (format == BinaryFormat::unknown) {
        error = "not a supported binary (expected ELF or Mach-O)";
        content.clear();
        return false;
    }
    const std::string native = native_section_name(format, name);
    if (format == BinaryFormat::elf) return find_elf_section(data, size, native, content, error);
    return find_macho_section(data, size, native, content, error);
}

}   // namespace amc
