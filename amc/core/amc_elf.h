#ifndef AMC_ELF_H
#define AMC_ELF_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace amc {

// Minimal, dependency-free ELF64 reader used to locate an ABIX Metadata Region
// inside a compiled binary. It deliberately supports only little-endian
// ELF64 (x86_64 / aarch64 Linux, the ELF targets AMC emits today) and reads
// nothing beyond the section header table and the section it is asked for, so
// `amc dump`/`amc metadata` can scan a binary offline without LLVM.

struct ElfSection {
    std::string name;
    uint32_t type = 0;
    uint64_t flags = 0;
    uint64_t address = 0;
    uint64_t offset = 0;
    uint64_t size = 0;
};

bool is_elf(const uint8_t *data, size_t size) noexcept;

bool read_elf_sections(const uint8_t *data, size_t size, std::vector<ElfSection> &sections, std::string &error);

// Copies the bytes of the named section into `content`. Returns false with a
// descriptive `error` for a non-ELF input, a malformed table, or a missing
// section.
bool find_elf_section(
    const uint8_t *data, size_t size, std::string_view name, std::vector<uint8_t> &content, std::string &error);

}   // namespace amc

#endif
