#include "test_common.hpp"

#include "ABIX/Binary/BinaryImage.h"

#include <cstdint>
#include <string>
#include <vector>

using skl::abix::binary::Architecture;
using skl::abix::binary::BinaryImage;
using skl::abix::binary::Format;
using skl::abix::binary::native_section_name;
using skl::abix::binary::OperatingSystem;
using skl::abix::binary::parse_binary_image;
using skl::abix::binary::Section;

namespace {

void put_u16(std::vector<uint8_t> &bytes, size_t offset, uint16_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}
void put_u32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}
void put_u64(std::vector<uint8_t> &bytes, size_t offset, uint64_t value) {
    for (int i = 0; i < 8; ++i)
        bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

// A minimal PE32+ image: DOS header, one `.abix` section carrying 8 bytes of
// payload, and no data directories (so no exports).
std::vector<uint8_t> make_pe() {
    std::vector<uint8_t> bytes(0x208, 0);
    bytes[0] = 'M';
    bytes[1] = 'Z';
    put_u32(bytes, 0x3C, 0x40);   // e_lfanew

    const size_t nt = 0x40;
    bytes[nt] = 'P';
    bytes[nt + 1] = 'E';

    const size_t coff = nt + 4;
    put_u16(bytes, coff + 0, 0x8664);    // Machine = amd64
    put_u16(bytes, coff + 2, 1);         // NumberOfSections
    put_u16(bytes, coff + 16, 240);      // SizeOfOptionalHeader (PE32+)

    const size_t optional = coff + 20;
    put_u16(bytes, optional + 0, 0x20B);            // PE32+
    put_u64(bytes, optional + 24, 0x140000000ull);  // ImageBase

    const size_t sect = optional + 240;
    const char name[] = ".abix";
    for (size_t i = 0; i < 5; ++i)
        bytes[sect + i] = static_cast<uint8_t>(name[i]);
    put_u32(bytes, sect + 8, 8);        // VirtualSize
    put_u32(bytes, sect + 12, 0x1000);  // VirtualAddress
    put_u32(bytes, sect + 16, 8);       // SizeOfRawData
    put_u32(bytes, sect + 20, 0x200);   // PointerToRawData

    const char payload[] = {'A', 'B', 'I', 'X', 'D', 'A', 'T', 'A'};
    for (size_t i = 0; i < 8; ++i)
        bytes[0x200 + i] = static_cast<uint8_t>(payload[i]);
    return bytes;
}

// A minimal PE32+ image carrying one named export ("my_export"), so the export
// parser can be exercised without a real toolchain.
std::vector<uint8_t> make_pe_with_export() {
    std::vector<uint8_t> bytes(0x400, 0);
    bytes[0] = 'M';
    bytes[1] = 'Z';
    put_u32(bytes, 0x3C, 0x40);

    const size_t nt = 0x40;
    bytes[nt] = 'P';
    bytes[nt + 1] = 'E';

    const size_t coff = nt + 4;
    put_u16(bytes, coff + 0, 0x8664);   // amd64
    put_u16(bytes, coff + 2, 1);        // one section
    put_u16(bytes, coff + 16, 240);     // PE32+ optional header

    const size_t optional = coff + 20;
    put_u16(bytes, optional + 0, 0x20B);
    put_u64(bytes, optional + 24, 0x140000000ull);

    const size_t data_dir = optional + 112;
    put_u32(bytes, data_dir + 0, 0x1000);   // export directory RVA
    put_u32(bytes, data_dir + 4, 40);       // export directory size

    const size_t sect = optional + 240;
    const char name[] = ".rdata";
    for (size_t i = 0; i < 6; ++i)
        bytes[sect + i] = static_cast<uint8_t>(name[i]);
    put_u32(bytes, sect + 8, 0x100);   // VirtualSize
    put_u32(bytes, sect + 12, 0x1000); // VirtualAddress
    put_u32(bytes, sect + 16, 0x200);  // SizeOfRawData
    put_u32(bytes, sect + 20, 0x200);  // PointerToRawData

    const size_t raw = 0x200;
    put_u32(bytes, raw + 16, 1);        // export Base
    put_u32(bytes, raw + 20, 1);        // NumberOfFunctions
    put_u32(bytes, raw + 24, 1);        // NumberOfNames
    put_u32(bytes, raw + 28, 0x1028);   // AddressOfFunctions
    put_u32(bytes, raw + 32, 0x102C);   // AddressOfNames
    put_u32(bytes, raw + 36, 0x1030);   // AddressOfNameOrdinals
    put_u32(bytes, raw + 0x28, 0x2000); // function RVA
    put_u32(bytes, raw + 0x2C, 0x1040); // name RVA
    put_u16(bytes, raw + 0x30, 0);      // ordinal index
    const char symbol[] = "my_export";
    for (size_t i = 0; i < 9; ++i)
        bytes[raw + 0x40 + i] = static_cast<uint8_t>(symbol[i]);
    return bytes;
}

// A minimal PE32 (0x10B, 32-bit) image with the same shape as make_pe(): the
// 32-bit optional header stores ImageBase at +28 and the data directories at
// +96, so this exercises the non-PE32+ layout branch.
std::vector<uint8_t> make_pe32() {
    std::vector<uint8_t> bytes(0x208, 0);
    bytes[0] = 'M';
    bytes[1] = 'Z';
    put_u32(bytes, 0x3C, 0x40);   // e_lfanew

    const size_t nt = 0x40;
    bytes[nt] = 'P';
    bytes[nt + 1] = 'E';

    const size_t coff = nt + 4;
    put_u16(bytes, coff + 0, 0x014c);   // Machine = i386
    put_u16(bytes, coff + 2, 1);        // NumberOfSections
    put_u16(bytes, coff + 16, 224);     // SizeOfOptionalHeader (PE32)

    const size_t optional = coff + 20;
    put_u16(bytes, optional + 0, 0x10B);        // PE32
    put_u32(bytes, optional + 28, 0x00400000);  // ImageBase (u32 in PE32)

    const size_t sect = optional + 224;
    const char name[] = ".abix";
    for (size_t i = 0; i < 5; ++i)
        bytes[sect + i] = static_cast<uint8_t>(name[i]);
    put_u32(bytes, sect + 8, 8);        // VirtualSize
    put_u32(bytes, sect + 12, 0x1000);  // VirtualAddress
    put_u32(bytes, sect + 16, 8);       // SizeOfRawData
    put_u32(bytes, sect + 20, 0x200);   // PointerToRawData

    const char payload[] = {'A', 'B', 'I', 'X', 'D', 'A', 'T', 'A'};
    for (size_t i = 0; i < 8; ++i)
        bytes[0x200 + i] = static_cast<uint8_t>(payload[i]);
    return bytes;
}

// A PE whose e_lfanew points at the last four bytes: the `PE\0\0` signature fits
// but the 20-byte COFF file header does not. `is_pe` accepts it, so the parser
// must reject it before reading the header fields (guards against a
// heap-buffer-overflow on truncated input).
std::vector<uint8_t> make_truncated_pe() {
    std::vector<uint8_t> bytes(0x44, 0);
    bytes[0] = 'M';
    bytes[1] = 'Z';
    put_u32(bytes, 0x3C, 0x40);   // signature fits, COFF header does not
    bytes[0x40] = 'P';
    bytes[0x41] = 'E';
    return bytes;
}

}   // namespace

TEST_CASE("39.binary image reads a PE region", "[binary]") {
    auto bytes = make_pe();
    BinaryImage image;
    std::string error;
    REQUIRE(parse_binary_image(bytes.data(), bytes.size(), image, error));

    CHECK(image.format() == Format::pe);
    CHECK(image.os() == OperatingSystem::windows);
    CHECK(image.architecture() == Architecture::x86_64);
    CHECK(image.pointer_size() == 8);
    CHECK(image.image_base() == 0x140000000ull);

    REQUIRE(image.sections().size() == 1);
    CHECK(image.sections()[0].name == ".abix");

    const Section *region = image.find_region(".abix.metadata");
    REQUIRE(region != nullptr);
    CHECK(region->file_offset == 0x200);
    CHECK(region->file_size == 8);

    std::vector<uint8_t> content;
    REQUIRE(image.read_region(".abix.metadata", content, error));
    REQUIRE(content.size() == 8);
    CHECK(std::string(content.begin(), content.end()) == "ABIXDATA");

    // A missing region is reported, not silently empty.
    CHECK_FALSE(image.read_region(".abix.names", content, error));
    CHECK(error.find("not found") != std::string::npos);
}

TEST_CASE("40.native section names map per format", "[binary]") {
    CHECK(native_section_name(Format::elf, ".abix.metadata") == ".abix.metadata");
    CHECK(native_section_name(Format::macho, ".abix.metadata") == "__abix_metadata");
    CHECK(native_section_name(Format::macho, ".abix.names") == "__abix_names");
    CHECK(native_section_name(Format::pe, ".abix.metadata") == ".abix");
    CHECK(native_section_name(Format::pe, ".abix.names") == ".abix.n");
    CHECK(native_section_name(Format::pe, ".text") == ".text");
}

TEST_CASE("41.binary image reads a PE export table", "[binary]") {
    auto bytes = make_pe_with_export();
    BinaryImage image;
    std::string error;
    REQUIRE(parse_binary_image(bytes.data(), bytes.size(), image, error));

    REQUIRE(image.exports().size() == 1);
    const auto *record = image.find_export("my_export");
    REQUIRE(record != nullptr);
    CHECK(record->address == 0x140002000ull);
    CHECK(record->ordinal == 1);
    CHECK(image.find_export("absent") == nullptr);
}

TEST_CASE("42.binary image reads a PE32 region", "[binary]") {
    auto bytes = make_pe32();
    BinaryImage image;
    std::string error;
    REQUIRE(parse_binary_image(bytes.data(), bytes.size(), image, error));

    CHECK(image.format() == Format::pe);
    CHECK(image.os() == OperatingSystem::windows);
    CHECK(image.architecture() == Architecture::x86);
    CHECK(image.pointer_size() == 4);
    CHECK(image.image_base() == 0x00400000ull);

    REQUIRE(image.sections().size() == 1);
    CHECK(image.sections()[0].name == ".abix");

    const Section *region = image.find_region(".abix.metadata");
    REQUIRE(region != nullptr);
    CHECK(region->file_offset == 0x200);
    CHECK(region->file_size == 8);

    std::vector<uint8_t> content;
    REQUIRE(image.read_region(".abix.metadata", content, error));
    CHECK(std::string(content.begin(), content.end()) == "ABIXDATA");
}

TEST_CASE("43.binary image rejects a truncated PE header", "[binary]") {
    auto bytes = make_truncated_pe();
    // The signature is present, so detection succeeds...
    CHECK(skl::abix::binary::is_pe(bytes.data(), bytes.size()));

    // ...but the COFF file header does not fit, so parsing must fail cleanly
    // instead of reading past the end of the buffer.
    BinaryImage image;
    std::string error;
    CHECK_FALSE(parse_binary_image(bytes.data(), bytes.size(), image, error));
    CHECK(error == "truncated PE header");
}
