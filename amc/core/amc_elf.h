#ifndef AMC_ELF_H
#define AMC_ELF_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace amc {

// Minimal, dependency-free reader for the custom sections that carry an ABIX
// Metadata Region inside a compiled binary. It supports the two container
// formats AMC emits today:
//
//   * ELF64  (little-endian, x86_64 / aarch64 Linux)
//   * Mach-O (64-bit, x86_64 / arm64 macOS)
//
// It reads nothing beyond the section metadata and the section it is asked
// for, so `amc dump`/`amc metadata` can scan a binary offline without LLVM.

struct BinarySection {
    std::string name;      // native section name (e.g. ".abix.metadata")
    std::string segment;   // Mach-O segment name ("__DATA"); empty for ELF
    uint32_t type = 0;
    uint64_t flags = 0;
    uint64_t address = 0;
    uint64_t offset = 0;
    uint64_t size = 0;
};

// Historical name — the reader used to be ELF-only. Kept so existing callers
// keep compiling.
using ElfSection = BinarySection;

enum class BinaryFormat { unknown, elf, macho };

BinaryFormat detect_binary_format(const uint8_t *data, size_t size) noexcept;

bool is_elf(const uint8_t *data, size_t size) noexcept;
bool is_macho(const uint8_t *data, size_t size) noexcept;

// True for any container format this reader understands (ELF or Mach-O).
bool is_binary(const uint8_t *data, size_t size) noexcept;

// Native section listing for the detected format. Dispatches to
// `read_elf_sections` / `read_macho_sections`.
bool read_binary_sections(const uint8_t *data, size_t size, std::vector<BinarySection> &sections, std::string &error);

bool read_elf_sections(const uint8_t *data, size_t size, std::vector<BinarySection> &sections, std::string &error);
bool read_macho_sections(const uint8_t *data, size_t size, std::vector<BinarySection> &sections, std::string &error);

// Translates a canonical ABIX section name in the ELF spelling (".abix.metadata")
// to the native spelling of `format`. Mach-O section names are limited to 16
// bytes and use "__abix_metadata" in the "__DATA" segment. Unknown names are
// returned unchanged.
std::string native_section_name(BinaryFormat format, std::string_view canonical);

// Copies the bytes of the named section into `content`. `name` is given in the
// canonical ELF spelling and translated for the detected container. Returns
// false with a descriptive `error` for an unsupported/malformed image or a
// missing section.
bool find_binary_section(
    const uint8_t *data, size_t size, std::string_view name, std::vector<uint8_t> &content, std::string &error);

// Container-specific lookups. `name` is the native section name (".abix.metadata"
// for ELF, "__abix_metadata" for Mach-O); use `native_section_name` to
// translate a canonical name first.
bool find_elf_section(
    const uint8_t *data, size_t size, std::string_view name, std::vector<uint8_t> &content, std::string &error);
bool find_macho_section(
    const uint8_t *data, size_t size, std::string_view name, std::vector<uint8_t> &content, std::string &error);

}   // namespace amc

#endif
