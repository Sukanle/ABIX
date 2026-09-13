#include "amc_metadata.h"
#include "amc_elf.h"
#include "amc_error.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string_view>
#include <utility>

namespace amc {
namespace {

// Fixed wire offsets into the manifest.  Serialization is written field by
// field so the on-disk layout never depends on compiler padding.
constexpr size_t kOffsetMagic = 0;
constexpr size_t kOffsetVersionMajor = 4;
constexpr size_t kOffsetVersionMinor = 6;
constexpr size_t kOffsetFlags = 8;
constexpr size_t kOffsetBuildId = 16;
constexpr size_t kOffsetMetadataId = 32;
constexpr size_t kOffsetHashAlgorithm = 48;
constexpr size_t kOffsetHashVersion = 52;
constexpr size_t kOffsetTypeCount = 56;
constexpr size_t kOffsetFieldCount = 60;
constexpr size_t kOffsetFunctionCount = 64;
constexpr size_t kOffsetParameterCount = 68;
constexpr size_t kOffsetSymbolCount = 72;
constexpr size_t kOffsetDescOffset = 80;
constexpr size_t kOffsetDescSize = 88;
constexpr size_t kOffsetHashOffset = 96;
constexpr size_t kOffsetHashSize = 104;
constexpr size_t kOffsetNamesOffset = 112;
constexpr size_t kOffsetNamesSize = 120;

constexpr uint64_t kMetadataIdDomain = 0x4d45544144415441ULL;   // "METADATA"

void append_record(std::vector<uint8_t> &bytes, const void *record, size_t size) {
    const auto *first = static_cast<const uint8_t *>(record);
    bytes.insert(bytes.end(), first, first + size);
}

void write_u16(std::vector<uint8_t> &bytes, size_t offset, uint16_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}
void write_u32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}
void write_u64(std::vector<uint8_t> &bytes, size_t offset, uint64_t value) {
    for (int i = 0; i < 8; ++i)
        bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}
void write_hash(std::vector<uint8_t> &bytes, size_t offset, Hash128 value) {
    write_u64(bytes, offset, value.lo);
    write_u64(bytes, offset + 8, value.hi);
}

uint16_t read_u16(const uint8_t *bytes, size_t offset) {
    return static_cast<uint16_t>(bytes[offset]) | static_cast<uint16_t>(bytes[offset + 1]) << 8;
}
uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value |= static_cast<uint32_t>(bytes[offset + i]) << (i * 8);
    return value;
}
uint64_t read_u64(const uint8_t *bytes, size_t offset) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(bytes[offset + i]) << (i * 8);
    return value;
}
Hash128 read_hash(const uint8_t *bytes, size_t offset) {
    return {read_u64(bytes, offset), read_u64(bytes, offset + 8)};
}

std::string hash_string(Hash128 value) {
    std::ostringstream output;
    output << "0x" << std::hex << std::setfill('0') << std::setw(16) << value.hi << std::setw(16) << value.lo;
    return output.str();
}

uint64_t expected_desc_size(const MetadataHeader &header) {
    return static_cast<uint64_t>(header.type_count) * sizeof(MetadataTypeRecord)
         + static_cast<uint64_t>(header.field_count) * sizeof(MetadataFieldRecord)
         + static_cast<uint64_t>(header.function_count) * sizeof(MetadataFunctionRecord)
         + static_cast<uint64_t>(header.parameter_count) * sizeof(MetadataParameterRecord)
         + static_cast<uint64_t>(header.symbol_count) * sizeof(MetadataSymbolRecord);
}

bool in_bounds(uint64_t offset, uint64_t length, size_t size) { return offset <= size && length <= size - offset; }

}   // namespace

MetadataID metadata_content_id(const uint8_t *data, size_t size) {
    const char *chars = reinterpret_cast<const char *>(data);
    return hash_text(std::string_view(chars ? chars : "", size), kMetadataIdDomain);
}

bool build_metadata_region(
    const AbiModule &module, const MetadataOptions &options, std::vector<uint8_t> &region, std::string &error) {
    error.clear();

    // --- names ---------------------------------------------------------
    // Offset 0 is reserved for "no name", so the table starts with a NUL.
    std::vector<uint8_t> names;
    names.push_back(0);
    const auto add_name = [&](const std::string &name) -> uint32_t {
        if (!options.include_names || name.empty()) return 0;
        const uint32_t offset = static_cast<uint32_t>(names.size());
        names.insert(names.end(), name.begin(), name.end());
        names.push_back(0);
        return offset;
    };

    std::vector<uint32_t> type_names(module.types.size());
    for (size_t i = 0; i < module.types.size(); ++i)
        type_names[i] = add_name(module.types[i].name);
    std::vector<uint32_t> field_names(module.fields.size());
    for (size_t i = 0; i < module.fields.size(); ++i)
        field_names[i] = add_name(module.fields[i].name);
    std::vector<uint32_t> function_names(module.functions.size());
    for (size_t i = 0; i < module.functions.size(); ++i)
        function_names[i] = add_name(module.functions[i].name);
    std::vector<std::vector<uint32_t>> parameter_names(module.functions.size());
    for (size_t f = 0; f < module.functions.size(); ++f) {
        parameter_names[f].resize(module.functions[f].parameters.size());
        for (size_t p = 0; p < module.functions[f].parameters.size(); ++p)
            parameter_names[f][p] = add_name(module.functions[f].parameters[p].name);
    }
    std::vector<uint32_t> symbol_names(module.symbols.size());
    for (size_t i = 0; i < module.symbols.size(); ++i)
        symbol_names[i] = add_name(module.symbols[i].name);

    // --- desc ----------------------------------------------------------
    std::vector<uint8_t> desc;
    for (size_t i = 0; i < module.types.size(); ++i) {
        const auto &type = module.types[i];
        MetadataTypeRecord record{};
        record.type_id = type.id;
        record.layout_hash = type.layout_hash;
        record.flags = type.flags;
        record.kind = static_cast<uint32_t>(type.kind);
        record.size = type.size;
        record.align = type.align;
        record.field_begin = type.field_begin;
        record.field_count = type.field_count;
        record.array_count = type.array_count;
        record.name_offset = type_names[i];
        append_record(desc, &record, sizeof(record));
    }
    for (size_t i = 0; i < module.fields.size(); ++i) {
        const auto &field = module.fields[i];
        MetadataFieldRecord record{};
        record.owner_type = field.owner_type;
        record.type_id = field.type_id;
        record.offset = field.offset;
        record.flags = field.flags;
        record.name_offset = field_names[i];
        append_record(desc, &record, sizeof(record));
    }
    uint32_t parameter_begin = 0;
    for (size_t f = 0; f < module.functions.size(); ++f) {
        const auto &function = module.functions[f];
        MetadataFunctionRecord record{};
        record.owner_type = function.owner_type;
        record.signature = function.signature;
        record.return_type = function.return_type;
        record.calling_convention = function.calling_convention;
        record.flags = function.flags;
        record.parameter_begin = parameter_begin;
        record.parameter_count = static_cast<uint32_t>(function.parameters.size());
        record.name_offset = function_names[f];
        append_record(desc, &record, sizeof(record));
        parameter_begin += record.parameter_count;
    }
    for (size_t f = 0; f < module.functions.size(); ++f) {
        const auto &function = module.functions[f];
        for (size_t p = 0; p < function.parameters.size(); ++p) {
            MetadataParameterRecord record{};
            record.type_id = function.parameters[p].type_id;
            record.flags = function.parameters[p].flags;
            record.name_offset = parameter_names[f][p];
            append_record(desc, &record, sizeof(record));
        }
    }
    for (size_t i = 0; i < module.symbols.size(); ++i) {
        const auto &symbol = module.symbols[i];
        MetadataSymbolRecord record{};
        record.name_offset = symbol_names[i];
        record.kind = static_cast<uint32_t>(symbol.kind);
        record.target_index = symbol.target_index;
        append_record(desc, &record, sizeof(record));
    }

    // --- hash index ----------------------------------------------------
    std::vector<uint8_t> hash_index;
    if (options.include_hash_index) {
        std::vector<size_t> order(module.types.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return module.types[a].id.lo == module.types[b].id.lo ? module.types[a].id.hi < module.types[b].id.hi
                                                                  : module.types[a].id.lo < module.types[b].id.lo;
        });
        for (const auto index : order) {
            MetadataHashRecord record{};
            record.type_id = module.types[index].id;
            record.type_index = static_cast<uint32_t>(index);
            append_record(hash_index, &record, sizeof(record));
        }
    }

    // --- compose -------------------------------------------------------
    region.assign(metadata_manifest_size, 0);
    region.insert(region.end(), desc.begin(), desc.end());
    const uint64_t desc_offset = metadata_manifest_size;
    const uint64_t desc_size = desc.size();

    uint64_t hash_offset = 0, hash_size = 0;
    if (options.include_hash_index) {
        hash_offset = region.size();
        region.insert(region.end(), hash_index.begin(), hash_index.end());
        hash_size = hash_index.size();
    }
    uint64_t names_offset = 0, names_size = 0;
    if (options.include_names) {
        names_offset = region.size();
        region.insert(region.end(), names.begin(), names.end());
        names_size = names.size();
    }

    // MetadataID covers the content after the manifest (desc + hash + names).
    const MetadataID metadata_id =
        metadata_content_id(region.data() + desc_offset, static_cast<size_t>(region.size() - desc_offset));

    uint32_t flags = 0;
    if (options.include_names) flags |= metadata_flag_value(MetadataFlag::has_names);
    if (options.include_hash_index) flags |= metadata_flag_value(MetadataFlag::has_hash_index);

    write_u32(region, kOffsetMagic, metadata_magic);
    write_u16(region, kOffsetVersionMajor, options.version_major);
    write_u16(region, kOffsetVersionMinor, options.version_minor);
    write_u32(region, kOffsetFlags, flags);
    write_hash(region, kOffsetBuildId, options.build_id);
    write_hash(region, kOffsetMetadataId, metadata_id);
    write_u32(region, kOffsetHashAlgorithm, 0);
    write_u32(region, kOffsetHashVersion, 1);
    write_u32(region, kOffsetTypeCount, static_cast<uint32_t>(module.types.size()));
    write_u32(region, kOffsetFieldCount, static_cast<uint32_t>(module.fields.size()));
    write_u32(region, kOffsetFunctionCount, static_cast<uint32_t>(module.functions.size()));
    write_u32(region, kOffsetParameterCount, parameter_begin);
    write_u32(region, kOffsetSymbolCount, static_cast<uint32_t>(module.symbols.size()));
    write_u64(region, kOffsetDescOffset, desc_offset);
    write_u64(region, kOffsetDescSize, desc_size);
    write_u64(region, kOffsetHashOffset, hash_offset);
    write_u64(region, kOffsetHashSize, hash_size);
    write_u64(region, kOffsetNamesOffset, names_offset);
    write_u64(region, kOffsetNamesSize, names_size);
    return true;
}

bool read_metadata_header(const uint8_t *data, size_t size, MetadataHeader &header, std::string &error) {
    error.clear();
    if (data == nullptr) {
        error = "null metadata buffer";
        return false;
    }
    if (size < metadata_manifest_size) {
        error = "truncated metadata manifest";
        return false;
    }

    header.magic = read_u32(data, kOffsetMagic);
    header.version_major = read_u16(data, kOffsetVersionMajor);
    header.version_minor = read_u16(data, kOffsetVersionMinor);
    header.flags = read_u32(data, kOffsetFlags);
    header.build_id = read_hash(data, kOffsetBuildId);
    header.metadata_id = read_hash(data, kOffsetMetadataId);
    header.hash_algorithm = read_u32(data, kOffsetHashAlgorithm);
    header.hash_version = read_u32(data, kOffsetHashVersion);
    header.type_count = read_u32(data, kOffsetTypeCount);
    header.field_count = read_u32(data, kOffsetFieldCount);
    header.function_count = read_u32(data, kOffsetFunctionCount);
    header.parameter_count = read_u32(data, kOffsetParameterCount);
    header.symbol_count = read_u32(data, kOffsetSymbolCount);
    header.desc_offset = read_u64(data, kOffsetDescOffset);
    header.desc_size = read_u64(data, kOffsetDescSize);
    header.hash_offset = read_u64(data, kOffsetHashOffset);
    header.hash_size = read_u64(data, kOffsetHashSize);
    header.names_offset = read_u64(data, kOffsetNamesOffset);
    header.names_size = read_u64(data, kOffsetNamesSize);

    if (header.magic != metadata_magic) {
        error = "invalid metadata magic";
        return false;
    }
    if (header.version_major != metadata_version_major) {
        error = "unsupported metadata version";
        return false;
    }
    if (header.desc_offset != metadata_manifest_size || !in_bounds(header.desc_offset, header.desc_size, size)) {
        error = "invalid metadata desc section bounds";
        return false;
    }
    if (header.desc_size != expected_desc_size(header)) {
        error = "metadata desc section does not match record counts";
        return false;
    }

    const bool has_hash_index = (header.flags & metadata_flag_value(MetadataFlag::has_hash_index)) != 0;
    const bool has_names = (header.flags & metadata_flag_value(MetadataFlag::has_names)) != 0;
    if (has_hash_index) {
        if (header.hash_offset != header.desc_offset + header.desc_size
            || header.hash_size != static_cast<uint64_t>(header.type_count) * sizeof(MetadataHashRecord)
            || !in_bounds(header.hash_offset, header.hash_size, size)) {
            error = "invalid metadata hash section bounds";
            return false;
        }
    } else if (header.hash_offset != 0 || header.hash_size != 0) {
        error = "metadata hash section present without flag";
        return false;
    }
    if (has_names) {
        const uint64_t expected_offset = header.desc_offset + header.desc_size + header.hash_size;
        if (header.names_offset != expected_offset || !in_bounds(header.names_offset, header.names_size, size)) {
            error = "invalid metadata names section bounds";
            return false;
        }
    } else if (header.names_offset != 0 || header.names_size != 0) {
        error = "metadata names section present without flag";
        return false;
    }
    if (header.desc_offset + header.desc_size + header.hash_size + header.names_size != size) {
        error = "metadata region has trailing or missing bytes";
        return false;
    }
    return true;
}

bool read_metadata_header(const std::vector<uint8_t> &region, MetadataHeader &header, std::string &error) {
    return read_metadata_header(region.data(), region.size(), header, error);
}

bool verify_metadata_region(const uint8_t *data, size_t size, std::string &error) {
    MetadataHeader header;
    if (!read_metadata_header(data, size, header, error)) return false;
    const MetadataID computed =
        metadata_content_id(data + header.desc_offset, static_cast<size_t>(size - header.desc_offset));
    if (!(computed == header.metadata_id)) {
        error = "metadata id does not match region content";
        return false;
    }
    return true;
}

bool verify_metadata_region(const std::vector<uint8_t> &region, std::string &error) {
    return verify_metadata_region(region.data(), region.size(), error);
}

std::string metadata_header_to_json(const MetadataHeader &header) {
    std::ostringstream out;
    out
        << "{\"schema\":\"abix.metadata/1\",\"magic\":\"ABIX\""
        << ",\"version\":{\"major\":"
        << header.version_major
        << ",\"minor\":"
        << header.version_minor
        << "}"
        << ",\"flags\":"
        << header.flags
        << ",\"has_names\":"
        << ((header.flags & metadata_flag_value(MetadataFlag::has_names)) ? "true" : "false")
        << ",\"has_hash_index\":"
        << ((header.flags & metadata_flag_value(MetadataFlag::has_hash_index)) ? "true" : "false")
        << ",\"build_id\":\""
        << hash_string(header.build_id)
        << '"'
        << ",\"metadata_id\":\""
        << hash_string(header.metadata_id)
        << '"'
        << ",\"hash_algorithm\":"
        << header.hash_algorithm
        << ",\"hash_version\":"
        << header.hash_version
        << ",\"counts\":{\"types\":"
        << header.type_count
        << ",\"fields\":"
        << header.field_count
        << ",\"functions\":"
        << header.function_count
        << ",\"parameters\":"
        << header.parameter_count
        << ",\"symbols\":"
        << header.symbol_count
        << "}"
        << ",\"sections\":{"
        << "\"desc\":{\"offset\":"
        << header.desc_offset
        << ",\"size\":"
        << header.desc_size
        << "}"
        << ",\"hash\":{\"offset\":"
        << header.hash_offset
        << ",\"size\":"
        << header.hash_size
        << "}"
        << ",\"names\":{\"offset\":"
        << header.names_offset
        << ",\"size\":"
        << header.names_size
        << "}}"
        << "}\n";
    return out.str();
}

std::string metadata_meta_document(const AbiModule &module, const MetadataHeader &header) {
    std::ostringstream out;
    out
        << "[metadata]\n"
        << "build_id = \""
        << hash_string(header.build_id)
        << "\"\n"
        << "metadata_id = \""
        << hash_string(header.metadata_id)
        << "\"\n"
        << "abi_version = "
        << header.version_major
        << "\n"
        << "hash_algorithm = "
        << header.hash_algorithm
        << "\n"
        << "hash_version = "
        << header.hash_version
        << "\n"
        << "type_count = "
        << header.type_count
        << "\n"
        << "field_count = "
        << header.field_count
        << "\n"
        << "function_count = "
        << header.function_count
        << "\n"
        << "parameter_count = "
        << header.parameter_count
        << "\n"
        << "symbol_count = "
        << header.symbol_count
        << "\n"
        << "has_names = "
        << ((header.flags & metadata_flag_value(MetadataFlag::has_names)) ? "true" : "false")
        << "\n"
        << "has_hash_index = "
        << ((header.flags & metadata_flag_value(MetadataFlag::has_hash_index)) ? "true" : "false")
        << "\n"
        << "generated_by = \"AMC\"\n"
        << "\n[package]\n"
        << "name = \""
        << json_escape(module.package_name)
        << "\"\n"
        << "version = \""
        << json_escape(module.package_version)
        << "\"\n"
        << "abi_hash = \""
        << hash_string(abi_hash(module))
        << "\"\n";
    return out.str();
}

bool module_from_region(const uint8_t *data, size_t size, AbiModule &module, std::string &error) {
    error.clear();
    module = AbiModule{};
    MetadataHeader header;
    if (!read_metadata_header(data, size, header, error)) return false;

    const auto name_at = [&](uint32_t offset) -> std::string {
        if ((header.flags & metadata_flag_value(MetadataFlag::has_names)) == 0) return {};
        if (offset == 0) return {};
        const uint64_t begin = header.names_offset + offset;
        if (begin >= size) return {};
        std::string result;
        for (uint64_t i = begin; i < size && data[i] != 0; ++i)
            result.push_back(static_cast<char>(data[i]));
        return result;
    };

    const uint8_t *desc = data + header.desc_offset;
    size_t cursor = 0;

    module.types.reserve(header.type_count);
    for (uint32_t i = 0; i < header.type_count; ++i) {
        MetadataTypeRecord record;
        std::memcpy(&record, desc + cursor, sizeof(record));
        cursor += sizeof(record);
        Type type;
        type.name = name_at(record.name_offset);
        type.kind = static_cast<TypeKind>(record.kind);
        type.id = record.type_id;
        type.layout_hash = record.layout_hash;
        type.size = record.size;
        type.align = record.align;
        type.flags = record.flags;
        type.field_begin = record.field_begin;
        type.field_count = record.field_count;
        type.array_count = record.array_count;
        module.types.push_back(std::move(type));
    }

    module.fields.reserve(header.field_count);
    for (uint32_t i = 0; i < header.field_count; ++i) {
        MetadataFieldRecord record;
        std::memcpy(&record, desc + cursor, sizeof(record));
        cursor += sizeof(record);
        Field field;
        field.owner_type = record.owner_type;
        field.type_id = record.type_id;
        field.name = name_at(record.name_offset);
        field.offset = record.offset;
        field.flags = record.flags;
        module.fields.push_back(std::move(field));
    }

    struct ParameterRange {
        uint32_t begin;
        uint32_t count;
    };
    std::vector<ParameterRange> ranges;
    ranges.reserve(header.function_count);
    module.functions.reserve(header.function_count);
    for (uint32_t i = 0; i < header.function_count; ++i) {
        MetadataFunctionRecord record;
        std::memcpy(&record, desc + cursor, sizeof(record));
        cursor += sizeof(record);
        Function function;
        function.owner_type = record.owner_type;
        function.name = name_at(record.name_offset);
        function.signature = record.signature;
        function.return_type = record.return_type;
        function.calling_convention = record.calling_convention;
        function.flags = record.flags;
        module.functions.push_back(std::move(function));
        ranges.push_back({record.parameter_begin, record.parameter_count});
    }

    std::vector<Parameter> parameters;
    parameters.reserve(header.parameter_count);
    for (uint32_t i = 0; i < header.parameter_count; ++i) {
        MetadataParameterRecord record;
        std::memcpy(&record, desc + cursor, sizeof(record));
        cursor += sizeof(record);
        Parameter parameter;
        parameter.name = name_at(record.name_offset);
        parameter.type_id = record.type_id;
        parameter.flags = record.flags;
        parameters.push_back(std::move(parameter));
    }
    for (size_t i = 0; i < module.functions.size(); ++i) {
        const auto &range = ranges[i];
        const uint32_t begin = std::min(range.begin, static_cast<uint32_t>(parameters.size()));
        const uint32_t end = std::min(begin + range.count, static_cast<uint32_t>(parameters.size()));
        module.functions[i].parameters.assign(parameters.begin() + begin, parameters.begin() + end);
    }

    module.symbols.reserve(header.symbol_count);
    for (uint32_t i = 0; i < header.symbol_count; ++i) {
        MetadataSymbolRecord record;
        std::memcpy(&record, desc + cursor, sizeof(record));
        cursor += sizeof(record);
        Symbol symbol;
        symbol.name = name_at(record.name_offset);
        symbol.kind = static_cast<SymbolKind>(record.kind);
        symbol.target_index = record.target_index;
        module.symbols.push_back(std::move(symbol));
    }
    return true;
}

bool module_from_region(const std::vector<uint8_t> &region, AbiModule &module, std::string &error) {
    return module_from_region(region.data(), region.size(), module, error);
}

bool load_module_source(const std::string &path, AbiModule &module, std::string &error) {
    error.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open input: " + path;
        return false;
    }
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (is_elf(bytes.data(), bytes.size())) {
        std::vector<uint8_t> region;
        if (!find_elf_section(bytes.data(), bytes.size(), ".abix.metadata", region, error)) return false;
        return module_from_region(region, module, error);
    }
    return read_abix(path, module, error);
}

}   // namespace amc
