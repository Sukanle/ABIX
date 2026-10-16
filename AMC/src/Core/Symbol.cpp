#include "AMC/Core/Symbol.h"
#include "AMC/Core/ELF.h"

#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>

#include <fmt/format.h>
#include <fmt/os.h>

namespace fs = std::filesystem;

namespace amc {
namespace {

uint32_t read_u32(const uint8_t *data, size_t offset) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
    return value;
}

uint16_t read_u16(const uint8_t *data, size_t offset) {
    return static_cast<uint16_t>(data[offset]) | static_cast<uint16_t>(data[offset + 1]) << 8;
}

bool in_bounds(uint64_t offset, uint64_t length, size_t size) { return offset <= size && length <= size - offset; }

constexpr uint32_t kNoteTypeGnuBuildId = 3;
constexpr size_t kNoteHeaderSize = 12;
constexpr size_t kNoteAlign = 4;

// Mach-O load command constants (mirrors amc_elf.cpp).
constexpr uint32_t kMachMagic64 = 0xFEEDFACFu;
constexpr size_t kMachHeader64Size = 32;
constexpr size_t kMachHeader32Size = 28;
constexpr size_t kMachNcmdsOffset = 16;
constexpr size_t kMachSizeofcmdsOffset = 20;
constexpr uint32_t kLcUuid = 0x1B;
constexpr size_t kLcUuidSize = 24;

size_t align4(size_t value) { return (value + kNoteAlign - 1) & ~(kNoteAlign - 1); }

}   // namespace

bool read_build_id(const uint8_t *data, size_t size, std::vector<uint8_t> &build_id, std::string &error) {
    switch (detect_binary_format(data, size)) {
        case BinaryFormat::elf: return read_gnu_build_id(data, size, build_id, error);
        case BinaryFormat::macho: return read_macho_uuid(data, size, build_id, error);
        case BinaryFormat::pe: return read_pe_debug_id(data, size, build_id, error);
        case BinaryFormat::unknown:
            error = "not a supported binary (expected ELF, Mach-O or PE)";
            build_id.clear();
            return false;
    }
    error = "not a supported binary (expected ELF, Mach-O or PE)";
    build_id.clear();
    return false;
}

bool read_macho_uuid(const uint8_t *data, size_t size, std::vector<uint8_t> &uuid, std::string &error) {
    error.clear();
    uuid.clear();
    if (!is_macho(data, size)) {
        error = "not a Mach-O file";
        return false;
    }
    const bool is64 = read_u32(data, 0) == kMachMagic64;
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
        if (cmd == kLcUuid) {
            if (cmdsize < kLcUuidSize) {
                error = "malformed Mach-O LC_UUID command";
                return false;
            }
            uuid.assign(data + cursor + 8, data + cursor + 8 + 16);
            return true;
        }
        cursor += cmdsize;
    }
    error = "no Mach-O LC_UUID load command found";
    return false;
}

bool read_pe_debug_id(const uint8_t *data, size_t size, std::vector<uint8_t> &build_id, std::string &error) {
    error.clear();
    build_id.clear();
    if (!is_pe(data, size)) {
        error = "not a PE image";
        return false;
    }

    constexpr size_t kFileHeaderSize = 20;
    constexpr size_t kSectionHeaderSize = 40;
    constexpr uint32_t kDebugTypeCodeView = 2;   // IMAGE_DEBUG_TYPE_CODEVIEW

    const uint32_t nt = read_u32(data, 0x3C);
    const size_t coff = static_cast<size_t>(nt) + 4;
    if (!in_bounds(coff, kFileHeaderSize, size)) {
        error = "truncated PE header";
        return false;
    }
    const uint16_t section_count = read_u16(data, coff + 2);
    const uint16_t optional_size = read_u16(data, coff + 16);
    const size_t optional = coff + kFileHeaderSize;
    if (!in_bounds(optional, optional_size, size) || optional_size < 2) {
        error = "PE optional header is out of bounds";
        return false;
    }
    const bool pe32plus = read_u16(data, optional) == 0x20B;
    const size_t data_directory = optional + (pe32plus ? 112 : 96);
    if (!in_bounds(data_directory, 7 * 8, size) || data_directory + 7 * 8 > optional + optional_size) {
        error = "PE has no debug data directory";
        return false;
    }
    const uint32_t debug_rva = read_u32(data, data_directory + 6 * 8);
    const uint32_t debug_size = read_u32(data, data_directory + 6 * 8 + 4);
    if (debug_rva == 0 || debug_size < 28) {
        error = "PE has no debug directory";
        return false;
    }

    const size_t section_table = optional + optional_size;
    if (!in_bounds(section_table, static_cast<uint64_t>(section_count) * kSectionHeaderSize, size)) {
        error = "PE section table is out of bounds";
        return false;
    }
    const auto rva_to_offset = [&](uint32_t rva, size_t &offset) -> bool {
        for (uint16_t i = 0; i < section_count; ++i) {
            const size_t header = section_table + static_cast<size_t>(i) * kSectionHeaderSize;
            const uint32_t virtual_size = read_u32(data, header + 8);
            const uint32_t virtual_address = read_u32(data, header + 12);
            const uint32_t raw_size = read_u32(data, header + 16);
            const uint32_t raw_offset = read_u32(data, header + 20);
            const uint32_t span = virtual_size > raw_size ? virtual_size : raw_size;
            if (rva < virtual_address || rva >= virtual_address + span) continue;
            const uint32_t delta = rva - virtual_address;
            if (delta >= raw_size) return false;
            offset = static_cast<size_t>(raw_offset) + delta;
            return in_bounds(offset, 1, size);
        }
        return false;
    };

    size_t debug_offset = 0;
    if (!rva_to_offset(debug_rva, debug_offset) || !in_bounds(debug_offset, debug_size, size)) {
        error = "PE debug directory is out of bounds";
        return false;
    }

    for (uint32_t entry = 0; entry + 28 <= debug_size; entry += 28) {
        const size_t base = debug_offset + entry;
        if (read_u32(data, base + 12) != kDebugTypeCodeView) continue;
        const uint32_t raw = read_u32(data, base + 24);   // PointerToRawData
        if (!in_bounds(raw, 24, size)) continue;
        // CodeView RSDS record: "RSDS" + GUID(16) + Age(4) + PdbFileName.
        if (!(data[raw] == 'R' && data[raw + 1] == 'S' && data[raw + 2] == 'D' && data[raw + 3] == 'S')) continue;
        build_id.assign(data + raw + 4, data + raw + 20);   // GUID
        build_id.insert(build_id.end(), data + raw + 20, data + raw + 24);   // Age
        return true;
    }
    error = "no CodeView (RSDS) debug id found in the PE";
    return false;
}

bool read_gnu_build_id(const uint8_t *data, size_t size, std::vector<uint8_t> &build_id, std::string &error) {
    error.clear();
    build_id.clear();
    std::vector<uint8_t> note;
    if (!find_elf_section(data, size, ".note.gnu.build-id", note, error)) return false;

    size_t cursor = 0;
    while (cursor + kNoteHeaderSize <= note.size()) {
        const uint32_t namesz = read_u32(note.data(), cursor);
        const uint32_t descsz = read_u32(note.data(), cursor + 4);
        const uint32_t type = read_u32(note.data(), cursor + 8);
        const size_t name_offset = cursor + kNoteHeaderSize;
        const size_t desc_offset = name_offset + align4(namesz);
        const size_t next = desc_offset + align4(descsz);
        if (name_offset > note.size() || desc_offset > note.size() || next > note.size()) {
            error = "malformed GNU build-id note";
            return false;
        }
        const std::string name(reinterpret_cast<const char *>(note.data() + name_offset), namesz > 0 ? namesz - 1 : 0);
        if (type == kNoteTypeGnuBuildId && name == "GNU" && descsz > 0) {
            build_id.assign(note.begin() + static_cast<std::ptrdiff_t>(desc_offset),
                note.begin() + static_cast<std::ptrdiff_t>(desc_offset + descsz));
            return true;
        }
        cursor = next;
    }
    error = "no GNU build-id note found";
    return false;
}

std::string hex_encode(const uint8_t *data, size_t size) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0f]);
    }
    return out;
}

bool hex_decode(std::string_view hex, std::vector<uint8_t> &bytes) {
    bytes.clear();
    if (hex.empty() || hex.size() % 2 != 0) return false;
    const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = digit(hex[i]);
        const int lo = digit(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

bool normalize_build_id(std::string_view text, std::string &normalized) {
    std::string hex(text);
    if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0) hex = hex.substr(2);
    if (hex.size() < 4 || hex.size() % 2 != 0) return false;
    std::vector<uint8_t> bytes;
    if (!hex_decode(hex, bytes)) return false;
    normalized = hex_encode(bytes.data(), bytes.size());
    return true;
}

std::string symbol_store_path(const std::string &root, std::string_view key, const char *suffix) {
    if (key.size() <= 2) return (fs::path(root) / std::string(key)).string() + suffix;
    const fs::path path = fs::path(root) / std::string(key.substr(0, 2)) / (std::string(key.substr(2)) + suffix);
    return path.string();
}

bool publish_symbol(const std::string &root, std::string_view key, const std::vector<uint8_t> &artifact,
    const std::string &meta, const char *suffix, std::string &stored_path, std::string &error) {
    error.clear();
    stored_path = symbol_store_path(root, key, suffix);
    std::error_code ec;
    fs::create_directories(fs::path(stored_path).parent_path(), ec);
    if (ec && !fs::exists(fs::path(stored_path).parent_path())) {
        error = "cannot create symbol store directory: " + ec.message();
        return false;
    }
    // Binary mode: the symbol artifact is a binary image and the Windows CRT
    // text mode would translate 0x0A into 0x0D 0x0A.
    try {
        std::ofstream output(stored_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "cannot write symbol artifact: " + stored_path;
            return false;
        }
        output.write(reinterpret_cast<const char *>(artifact.data()), static_cast<std::streamsize>(artifact.size()));
        if (!output.good()) {
            error = "cannot write symbol artifact: " + stored_path;
            return false;
        }
    } catch (const std::exception &e) {
        error = "cannot write symbol artifact: " + stored_path + ": " + e.what();
        return false;
    }
    if (!meta.empty()) {
        try {
            auto output = fmt::output_file(stored_path + ".meta");
            output.print("{}", meta);
        } catch (const std::exception &e) {
            error = "cannot write symbol metadata: " + stored_path + ".meta: " + e.what();
            return false;
        }
    }
    return true;
}

bool fetch_symbol(const std::string &root, std::string_view key, std::vector<uint8_t> &artifact, std::string &path,
    std::string &error) {
    error.clear();
    artifact.clear();
    path.clear();
    const char *suffixes[] = {".abix", ".abixmeta"};
    for (const char *suffix : suffixes) {
        const std::string candidate = symbol_store_path(root, key, suffix);
        std::ifstream input(candidate, std::ios::binary);
        if (!input) continue;
        artifact.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        path = candidate;
        return true;
    }
    error = "no symbol artifact for build id '" + std::string(key) + "' under " + root;
    return false;
}

std::string default_symbol_store_root() {
    if (const char *configured = std::getenv("ABIX_SYMBOL_STORE"))
        if (*configured) return configured;
    if (const char *home = std::getenv("HOME"))
        if (*home) return (fs::path(home) / ".abix" / "symbols").string();
    return (fs::path(".abix") / "symbols").string();
}

}   // namespace amc
