#include "AMC/Core/Metadata.h"
#include "AMC/Core/ELF.h"
#include "AMC/Core/Error.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string_view>
#include <utility>

#include <fmt/format.h>

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

std::string hash_string(Hash128 value) { return fmt::format("0X{:016x}{:016x}", value.hi, value.lo); }

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

    // Optional contract tables, discovered through a trailing extension
    // directory. With no extensions the desc ends at the symbol table, so the
    // bytes are identical to a region produced before the directory existed.
    const auto function_index_by_signature = [&](Hash128 signature) -> uint32_t {
        for (size_t i = 0; i < module.functions.size(); ++i)
            if (module.functions[i].signature == signature) return static_cast<uint32_t>(i);
        return UINT32_MAX;
    };
    std::vector<uint8_t> ownership_records;
    for (const auto &contract : module.contracts) {
        MetadataOwnershipRecord record{};
        record.target_kind = static_cast<uint32_t>(contract.target);
        record.target_index = contract.target_index;
        record.mode = static_cast<uint32_t>(contract.mode);
        record.release_function =
            (contract.release == Hash128{}) ? UINT32_MAX : function_index_by_signature(contract.release);
        record.flags = contract.flags;
        append_record(ownership_records, &record, sizeof(record));
    }
    std::vector<uint8_t> error_records;
    for (const auto &contract : module.errors) {
        MetadataErrorRecord record{};
        record.function_index = contract.function_index;
        record.status_source = static_cast<uint32_t>(contract.status_source);
        record.status_parameter = contract.status_parameter;
        record.value_parameter = contract.value_parameter;
        record.success_value = contract.success_value;
        record.flags = contract.flags;
        append_record(error_records, &record, sizeof(record));
    }
    struct PendingExtension {
        uint32_t kind;
        uint32_t version;
        uint32_t count;
        const std::vector<uint8_t> *bytes;
    };
    std::vector<uint8_t> target_records;
    if (module.arch != 0
        || module.os != 0
        || module.target_abi != 0
        || module.pointer_bits != 0
        || module.endianness != 0) {
        MetadataTargetRecord record{};
        record.arch = module.arch;
        record.os = module.os;
        record.target_abi = module.target_abi;
        record.compiler = module.compiler;
        record.calling_convention = module.calling_convention;
        record.pointer_bits = module.pointer_bits;
        record.endianness = module.endianness;
        append_record(target_records, &record, sizeof(record));
    }
    std::vector<PendingExtension> pending;
    if (!ownership_records.empty())
        pending.push_back(
            {metadata_extension_ownership, 1, static_cast<uint32_t>(module.contracts.size()), &ownership_records});
    if (!error_records.empty())
        pending.push_back({metadata_extension_error, 1, static_cast<uint32_t>(module.errors.size()), &error_records});
    if (!target_records.empty()) pending.push_back({metadata_extension_target, 1, 1, &target_records});
    if (!pending.empty()) {
        const uint64_t directory_begin = desc.size();
        const uint64_t entries_begin = directory_begin + sizeof(MetadataExtensionHeader);
        uint64_t record_cursor = entries_begin + pending.size() * sizeof(MetadataExtensionEntry);
        MetadataExtensionHeader extension_header{};
        extension_header.version = metadata_extension_version;
        extension_header.count = static_cast<uint32_t>(pending.size());
        append_record(desc, &extension_header, sizeof(extension_header));
        std::vector<MetadataExtensionEntry> entries(pending.size());
        for (size_t i = 0; i < pending.size(); ++i) {
            entries[i].kind = pending[i].kind;
            entries[i].version = pending[i].version;
            entries[i].count = pending[i].count;
            entries[i].byte_offset = static_cast<uint32_t>(record_cursor);
            entries[i].byte_size = static_cast<uint32_t>(pending[i].bytes->size());
            record_cursor += pending[i].bytes->size();
        }
        for (const auto &entry : entries)
            append_record(desc, &entry, sizeof(entry));
        for (const auto &extension : pending)
            desc.insert(desc.end(), extension.bytes->begin(), extension.bytes->end());
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
    if (header.desc_size < expected_desc_size(header)) {
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
    return fmt::format(
        "{{\"schema\":\"abix.metadata/1\",\"magic\":\"ABIX\""
        ",\"version\":{{\"major\":{}"
        ",\"minor\":{}"
        "}}"
        ",\"flags\":{}"
        ",\"has_names\":{}"
        ",\"has_hash_index\":{}"
        ",\"build_id\":\"{}\""
        ",\"metadata_id\":\"{}\""
        ",\"hash_algorithm\":{}"
        ",\"hash_version\":{}"
        ",\"counts\":{{\"types\":{}"
        ",\"fields\":{}"
        ",\"functions\":{}"
        ",\"parameters\":{}"
        ",\"symbols\":{}"
        "}}"
        ",\"sections\":{{"
        "\"desc\":{{\"offset\":{}"
        ",\"size\":{}"
        "}}"
        ",\"hash\":{{\"offset\":{}"
        ",\"size\":{}"
        "}}"
        ",\"names\":{{\"offset\":{}"
        ",\"size\":{}"
        "}}}}"
        "}}\n",
        header.version_major, header.version_minor, header.flags,
        header.flags & metadata_flag_value(MetadataFlag::has_names),
        header.flags & metadata_flag_value(MetadataFlag::has_hash_index), hash_string(header.build_id),
        hash_string(header.metadata_id), header.hash_algorithm, header.hash_version, header.type_count,
        header.field_count, header.function_count, header.parameter_count, header.symbol_count, header.desc_offset,
        header.desc_size, header.hash_offset, header.hash_size, header.names_offset, header.names_size);
}

std::string metadata_meta_document(const AbiModule &module, const MetadataHeader &header) {
    return fmt::format(
        "[metadata]\n"
        "build_id = \"{}\"\n"
        "metadata_id = \"{}\"\n"
        "abi_version = {}\n"
        "hash_algorithm = {}\n"
        "hash_version = {}\n"
        "type_count = {}\n"
        "field_count = {}\n"
        "function_count = {}\n"
        "parameter_count = {}\n"
        "symbol_count = {}\n"
        "has_names = {}\n"
        "has_hash_index = {}\n"
        "generated_by = \"AMC\"\n"
        "[package]\n"
        "name = \"{}\"\n"
        "version = \"{}\"\n"
        "abi_hash = \"{}\"\n",
        hash_string(header.build_id), hash_string(header.metadata_id), header.version_major, header.hash_algorithm,
        header.hash_version, header.type_count, header.field_count, header.function_count, header.parameter_count,
        header.symbol_count, header.flags & metadata_flag_value(MetadataFlag::has_names),
        header.flags & metadata_flag_value(MetadataFlag::has_hash_index), json_escape(module.package_name),
        json_escape(module.package_version), hash_string(abi_hash(module)));
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

namespace {

// Locate one extension payload by kind. Returns false only on a structural
// error; `present` reports whether the kind exists. An unknown kind is not an
// error -- the directory fixes its boundaries, so it can be skipped safely.
bool find_extension(const uint8_t *data, size_t size, const MetadataHeader &header, uint32_t kind, bool &present,
    uint32_t &version, uint32_t &count, const uint8_t *&records, size_t &byte_size, std::string &error) {
    present = false;
    version = 0;
    count = 0;
    records = nullptr;
    byte_size = 0;
    const uint64_t known = static_cast<uint64_t>(header.type_count) * sizeof(MetadataTypeRecord)
                         + static_cast<uint64_t>(header.field_count) * sizeof(MetadataFieldRecord)
                         + static_cast<uint64_t>(header.function_count) * sizeof(MetadataFunctionRecord)
                         + static_cast<uint64_t>(header.parameter_count) * sizeof(MetadataParameterRecord)
                         + static_cast<uint64_t>(header.symbol_count) * sizeof(MetadataSymbolRecord);
    if (header.desc_size == known) return true;   // no extension directory
    if (header.desc_size < known + sizeof(MetadataExtensionHeader)) {
        error = "truncated extension directory";
        return false;
    }
    const uint8_t *base = data + header.desc_offset;
    MetadataExtensionHeader directory{};
    std::memcpy(&directory, base + known, sizeof(directory));
    if (directory.version != metadata_extension_version) {
        error = "unsupported extension directory version";
        return false;
    }
    if (directory.count > 64) {
        error = "extension directory has too many entries";
        return false;
    }
    const uint64_t entries_begin = known + sizeof(MetadataExtensionHeader);
    const uint64_t entries_end =
        entries_begin + static_cast<uint64_t>(directory.count) * sizeof(MetadataExtensionEntry);
    if (entries_end > header.desc_size) {
        error = "extension directory entries are out of bounds";
        return false;
    }
    std::vector<uint32_t> seen;
    for (uint32_t i = 0; i < directory.count; ++i) {
        MetadataExtensionEntry entry{};
        std::memcpy(
            &entry, base + entries_begin + static_cast<uint64_t>(i) * sizeof(MetadataExtensionEntry), sizeof(entry));
        for (const uint32_t previous : seen)
            if (previous == entry.kind) {
                error = "duplicate extension kind";
                return false;
            }
        seen.push_back(entry.kind);
        if (entry.byte_offset < entries_end
            || entry.byte_offset > header.desc_size
            || entry.byte_size > header.desc_size - entry.byte_offset) {
            error = "extension entry is out of bounds";
            return false;
        }
        if (entry.kind == kind) {
            present = true;
            version = entry.version;
            count = entry.count;
            records = base + entry.byte_offset;
            byte_size = entry.byte_size;
        }
    }
    return true;
}

}   // namespace

bool read_ownership_contracts(
    const uint8_t *data, size_t size, std::vector<MetadataOwnershipContract> &out, std::string &error) {
    out.clear();
    MetadataHeader header;
    if (!read_metadata_header(data, size, header, error)) return false;
    bool present = false;
    uint32_t version = 0, count = 0;
    const uint8_t *records = nullptr;
    size_t byte_size = 0;
    if (!find_extension(
            data, size, header, metadata_extension_ownership, present, version, count, records, byte_size, error))
        return false;
    if (!present) return true;
    if (version != 1) {
        error = "unsupported ownership contract version";
        return false;
    }
    if (byte_size != static_cast<size_t>(count) * sizeof(MetadataOwnershipRecord)) {
        error = "ownership extension size mismatch";
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        MetadataOwnershipRecord record{};
        std::memcpy(&record, records + static_cast<size_t>(i) * sizeof(record), sizeof(record));
        if (record.target_kind > 1 || record.mode > static_cast<uint32_t>(OwnershipMode::copy)) {
            error = "invalid ownership contract record";
            return false;
        }
        if (record.release_function != UINT32_MAX && record.release_function >= header.function_count) {
            error = "ownership contract release function is out of range";
            return false;
        }
        MetadataOwnershipContract contract;
        contract.target_kind = record.target_kind;
        contract.target_index = record.target_index;
        contract.mode = static_cast<OwnershipMode>(record.mode);
        contract.release_function = record.release_function;
        out.push_back(contract);
    }
    return true;
}

bool read_ownership_contracts(
    const std::vector<uint8_t> &region, std::vector<MetadataOwnershipContract> &out, std::string &error) {
    return read_ownership_contracts(region.data(), region.size(), out, error);
}

bool read_error_contracts(
    const uint8_t *data, size_t size, std::vector<MetadataErrorContract> &out, std::string &error) {
    out.clear();
    MetadataHeader header;
    if (!read_metadata_header(data, size, header, error)) return false;
    bool present = false;
    uint32_t version = 0, count = 0;
    const uint8_t *records = nullptr;
    size_t byte_size = 0;
    if (!find_extension(
            data, size, header, metadata_extension_error, present, version, count, records, byte_size, error))
        return false;
    if (!present) return true;
    if (version != 1) {
        error = "unsupported error contract version";
        return false;
    }
    if (byte_size != static_cast<size_t>(count) * sizeof(MetadataErrorRecord)) {
        error = "error extension size mismatch";
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        MetadataErrorRecord record{};
        std::memcpy(&record, records + static_cast<size_t>(i) * sizeof(record), sizeof(record));
        if (record.status_source > static_cast<uint32_t>(StatusSource::parameter)) {
            error = "invalid error contract record";
            return false;
        }
        if (record.function_index >= header.function_count) {
            error = "error contract function is out of range";
            return false;
        }
        MetadataErrorContract contract;
        contract.function_index = record.function_index;
        contract.status_source = static_cast<StatusSource>(record.status_source);
        contract.status_parameter = record.status_parameter;
        contract.success_value = record.success_value;
        contract.value_parameter = record.value_parameter;
        out.push_back(contract);
    }
    return true;
}

bool read_error_contracts(
    const std::vector<uint8_t> &region, std::vector<MetadataErrorContract> &out, std::string &error) {
    return read_error_contracts(region.data(), region.size(), out, error);
}

bool read_target_description(const uint8_t *data, size_t size, TargetDescription &out, std::string &error) {
    out = {};
    MetadataHeader header;
    if (!read_metadata_header(data, size, header, error)) return false;
    bool present = false;
    uint32_t version = 0, count = 0;
    const uint8_t *records = nullptr;
    size_t byte_size = 0;
    if (!find_extension(
            data, size, header, metadata_extension_target, present, version, count, records, byte_size, error))
        return false;
    if (!present) return true;
    if (version != 1) {
        error = "unsupported target description version";
        return false;
    }
    if (count < 1 || byte_size < sizeof(MetadataTargetRecord)) {
        error = "target description is malformed";
        return false;
    }
    MetadataTargetRecord record{};
    std::memcpy(&record, records, sizeof(record));
    out.arch = record.arch;
    out.os = record.os;
    out.target_abi = record.target_abi;
    out.compiler = record.compiler;
    out.calling_convention = record.calling_convention;
    out.pointer_bits = record.pointer_bits;
    out.endianness = record.endianness;
    out.present = true;
    return true;
}

bool read_target_description(const std::vector<uint8_t> &region, TargetDescription &out, std::string &error) {
    return read_target_description(region.data(), region.size(), out, error);
}

bool verify_target_abi(
    const uint8_t *data, size_t size, const TargetExpectation &expected, TargetVerification &out, std::string &error) {
    out = {};
    TargetDescription target;
    if (!read_target_description(data, size, target, error)) return false;
    if (!target.present) {
        out.status = TargetVerificationStatus::unverifiable;
        out.reasons.push_back("region carries no target description");
        return true;
    }
    bool incompatible = false;
    bool unverifiable = false;
    const auto compare = [&](uint32_t want, uint32_t have, const char *field) {
        if (want == 0) return;
        if (have == 0) {
            unverifiable = true;
            out.reasons.push_back(std::string("target ") + field + " is unknown in the region");
        } else if (have != want) {
            incompatible = true;
            out.reasons.push_back(std::string("target ") + field + " mismatch");
        }
    };
    compare(expected.arch, target.arch, "arch");
    compare(expected.os, target.os, "os");
    compare(expected.target_abi, target.target_abi, "abi");
    compare(expected.pointer_bits, target.pointer_bits, "pointer width");
    compare(expected.endianness, target.endianness, "endianness");

    // Pointer width consistency: a pointer-typed record must be pointer-sized.
    if (target.pointer_bits != 0) {
        MetadataHeader header;
        if (!read_metadata_header(data, size, header, error)) return false;
        const uint8_t *base = data + header.desc_offset;
        const uint32_t pointer_bytes = target.pointer_bits / 8;
        for (uint32_t i = 0; i < header.type_count; ++i) {
            MetadataTypeRecord record{};
            std::memcpy(&record, base + static_cast<size_t>(i) * sizeof(record), sizeof(record));
            if (record.kind == static_cast<uint32_t>(TypeKind::pointer) && record.size != pointer_bytes) {
                incompatible = true;
                out.reasons.push_back("a pointer type is not pointer-sized");
                break;
            }
        }
    }

    if (incompatible)
        out.status = TargetVerificationStatus::incompatible;
    else if (unverifiable)
        out.status = TargetVerificationStatus::unverifiable;
    else
        out.status = TargetVerificationStatus::compatible;
    return true;
}

bool verify_target_abi(const std::vector<uint8_t> &region, const TargetExpectation &expected, TargetVerification &out,
    std::string &error) {
    return verify_target_abi(region.data(), region.size(), expected, out, error);
}

bool load_module_source(const std::string &path, AbiModule &module, std::string &error) {
    error.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open input: " + path;
        return false;
    }
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (is_binary(bytes.data(), bytes.size())) {
        std::vector<uint8_t> region;
        if (!find_binary_section(bytes.data(), bytes.size(), ".abix.metadata", region, error)) return false;
        return module_from_region(region, module, error);
    }
    return read_abix(path, module, error);
}

}   // namespace amc
