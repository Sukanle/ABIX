#include "amc_symbol_store.h"
#include "amc_elf.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace amc {
namespace {

uint32_t read_u32(const uint8_t *data, size_t offset) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
    return value;
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
        case BinaryFormat::unknown:
            error = "not a supported binary (expected ELF or Mach-O)";
            build_id.clear();
            return false;
    }
    error = "not a supported binary (expected ELF or Mach-O)";
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
    {
        std::ofstream output(stored_path, std::ios::binary);
        if (!output) {
            error = "cannot write symbol artifact: " + stored_path;
            return false;
        }
        output.write(reinterpret_cast<const char *>(artifact.data()), static_cast<std::streamsize>(artifact.size()));
        if (!output) {
            error = "failed to write symbol artifact: " + stored_path;
            return false;
        }
    }
    if (!meta.empty()) {
        std::ofstream output(stored_path + ".meta", std::ios::binary);
        if (!output) {
            error = "cannot write symbol metadata: " + stored_path + ".meta";
            return false;
        }
        output << meta;
        if (!output) {
            error = "failed to write symbol metadata: " + stored_path + ".meta";
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
