#include "AMC/Core/ELF.h"

#include "ABIX/Binary/BinaryImage.h"

namespace amc {
namespace {

namespace binary = skl::abix::binary;

BinaryFormat map_format(binary::Format format) {
    switch (format) {
        case binary::Format::elf:     return BinaryFormat::elf;
        case binary::Format::macho:   return BinaryFormat::macho;
        case binary::Format::pe:      return BinaryFormat::pe;
        case binary::Format::unknown: break;
    }
    return BinaryFormat::unknown;
}

binary::Format to_binary_format(BinaryFormat format) {
    switch (format) {
        case BinaryFormat::elf:     return binary::Format::elf;
        case BinaryFormat::macho:   return binary::Format::macho;
        case BinaryFormat::pe:      return binary::Format::pe;
        case BinaryFormat::unknown: break;
    }
    return binary::Format::unknown;
}

void to_binary_section(const binary::Section &src, BinarySection &dst) {
    dst.name = src.name;
    dst.segment = src.segment;
    dst.type = src.type;
    dst.flags = src.flags;
    dst.address = src.address;
    dst.offset = src.file_offset;
    dst.size = src.size;
}

void collect_sections(const binary::BinaryImage &image, std::vector<BinarySection> &sections) {
    sections.clear();
    sections.reserve(image.sections().size());
    for (const auto &section : image.sections()) {
        BinarySection mapped;
        to_binary_section(section, mapped);
        sections.push_back(std::move(mapped));
    }
}

bool find_in_sections(const std::vector<binary::Section> &sections, std::string_view name, const uint8_t *data,
    size_t size, std::vector<uint8_t> &content, std::string &error) {
    for (const auto &section : sections) {
        if (section.name != name) continue;
        // Zerofill sections (e.g. __bss) have no file content.
        if (section.file_size == 0 && section.size > 0) {
            error = "section '" + std::string(name) + "' has no file content";
            return false;
        }
        if (!binary::detail::in_bounds(section.file_offset, section.file_size, size)) {
            error = "section '" + std::string(name) + "' is out of bounds";
            return false;
        }
        content.assign(data + section.file_offset, data + section.file_offset + section.file_size);
        return true;
    }
    error = "section '" + std::string(name) + "' not found";
    return false;
}

}   // namespace

BinaryFormat detect_binary_format(const uint8_t *data, size_t size) noexcept {
    return map_format(binary::detect_format(data, size));
}

bool is_elf(const uint8_t *data, size_t size) noexcept { return binary::is_elf(data, size); }
bool is_macho(const uint8_t *data, size_t size) noexcept { return binary::is_macho(data, size); }
bool is_pe(const uint8_t *data, size_t size) noexcept { return binary::is_pe(data, size); }

bool is_binary(const uint8_t *data, size_t size) noexcept {
    return detect_binary_format(data, size) != BinaryFormat::unknown;
}

std::string native_section_name(BinaryFormat format, std::string_view canonical) {
    return binary::native_section_name(to_binary_format(format), canonical);
}

bool read_elf_sections(const uint8_t *data, size_t size, std::vector<BinarySection> &sections, std::string &error) {
    error.clear();
    if (!binary::is_elf(data, size)) {
        error = "not an ELF file";
        return false;
    }
    binary::BinaryImage image;
    if (!binary::parse_binary_image(data, size, image, error)) return false;
    if (image.format() != binary::Format::elf) {
        error = "not an ELF file";
        return false;
    }
    collect_sections(image, sections);
    return true;
}

bool read_macho_sections(const uint8_t *data, size_t size, std::vector<BinarySection> &sections, std::string &error) {
    error.clear();
    if (!binary::is_macho(data, size)) {
        error = "not a Mach-O file";
        return false;
    }
    binary::BinaryImage image;
    if (!binary::parse_binary_image(data, size, image, error)) return false;
    if (image.format() != binary::Format::macho) {
        error = "not a Mach-O file";
        return false;
    }
    collect_sections(image, sections);
    return true;
}

bool read_pe_sections(const uint8_t *data, size_t size, std::vector<BinarySection> &sections, std::string &error) {
    error.clear();
    if (!binary::is_pe(data, size)) {
        error = "not a PE image";
        return false;
    }
    binary::BinaryImage image;
    if (!binary::parse_binary_image(data, size, image, error)) return false;
    if (image.format() != binary::Format::pe) {
        error = "not a PE image";
        return false;
    }
    collect_sections(image, sections);
    return true;
}

bool read_binary_sections(const uint8_t *data, size_t size, std::vector<BinarySection> &sections, std::string &error) {
    binary::BinaryImage image;
    if (!binary::parse_binary_image(data, size, image, error)) {
        sections.clear();
        return false;
    }
    collect_sections(image, sections);
    return true;
}

bool find_elf_section(
    const uint8_t *data, size_t size, std::string_view name, std::vector<uint8_t> &content, std::string &error) {
    error.clear();
    if (!binary::is_elf(data, size)) {
        error = "not an ELF file";
        return false;
    }
    binary::BinaryImage image;
    if (!binary::parse_binary_image(data, size, image, error)) return false;
    if (image.format() != binary::Format::elf) {
        error = "not an ELF file";
        return false;
    }
    return find_in_sections(image.sections(), name, data, size, content, error);
}

bool find_macho_section(
    const uint8_t *data, size_t size, std::string_view name, std::vector<uint8_t> &content, std::string &error) {
    error.clear();
    if (!binary::is_macho(data, size)) {
        error = "not a Mach-O file";
        return false;
    }
    binary::BinaryImage image;
    if (!binary::parse_binary_image(data, size, image, error)) return false;
    if (image.format() != binary::Format::macho) {
        error = "not a Mach-O file";
        return false;
    }
    return find_in_sections(image.sections(), name, data, size, content, error);
}

bool find_pe_section(
    const uint8_t *data, size_t size, std::string_view name, std::vector<uint8_t> &content, std::string &error) {
    error.clear();
    if (!binary::is_pe(data, size)) {
        error = "not a PE image";
        return false;
    }
    binary::BinaryImage image;
    if (!binary::parse_binary_image(data, size, image, error)) return false;
    if (image.format() != binary::Format::pe) {
        error = "not a PE image";
        return false;
    }
    return find_in_sections(image.sections(), name, data, size, content, error);
}

bool find_binary_section(
    const uint8_t *data, size_t size, std::string_view name, std::vector<uint8_t> &content, std::string &error) {
    error.clear();
    content.clear();
    binary::BinaryImage image;
    if (!binary::parse_binary_image(data, size, image, error)) return false;
    return image.read_region(name, content, error);
}

}   // namespace amc
