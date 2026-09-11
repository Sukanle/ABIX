#include "amc_core.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace amc {
namespace {

constexpr uint16_t kFormatVersion = 4;
constexpr uint16_t kHashAlgorithm = 0;
constexpr uint32_t kHeaderSize = 20;
constexpr uint32_t kDirectoryEntrySize = 24;
constexpr uint32_t kIdentityEntrySize = 52;
constexpr uint32_t kTypeEntrySize = 68;
constexpr uint32_t kFieldEntrySize = 48;
constexpr uint32_t kFunctionEntrySize = 72;
constexpr uint32_t kParameterEntrySize = 28;
constexpr uint32_t kSymbolEntrySize = 16;
constexpr uint32_t kHashDescriptorEntrySize = 16;
constexpr uint32_t kHashRecordEntrySize = 32;
constexpr uint32_t kCompatibilityEntrySize = 44;
constexpr uint32_t kMapEntrySize = 60;
constexpr uint32_t kMapOperationEntrySize = 40;

enum Section : uint32_t {
    strings = 1, identity, types, fields, functions, parameters, symbols,
    hash_descriptor, hash_records, compatibility, maps, map_operations, target
};

constexpr uint32_t section_required = 1u << 0;

struct SectionData {
    uint32_t id;
    uint32_t count;
    uint32_t entry_size;
    uint32_t flags;
    std::vector<uint8_t> bytes;
};

struct DirectoryEntry { uint32_t offset, byte_length, count, entry_size, flags; };

void put_u16(std::vector<uint8_t> &bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
}
void put_u32(std::vector<uint8_t> &bytes, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(value >> (i * 8)));
}
void put_u64(std::vector<uint8_t> &bytes, uint64_t value) {
    for (int i = 0; i < 8; ++i) bytes.push_back(static_cast<uint8_t>(value >> (i * 8)));
}
void put_hash(std::vector<uint8_t> &bytes, Hash128 value) {
    put_u64(bytes, value.lo);
    put_u64(bytes, value.hi);
}

bool read_u16(const std::vector<uint8_t> &bytes, size_t &offset, uint16_t &value, std::string &error) {
    if (offset > bytes.size() || bytes.size() - offset < 2) { error = "truncated u16"; return false; }
    value = static_cast<uint16_t>(bytes[offset]) | (static_cast<uint16_t>(bytes[offset + 1]) << 8);
    offset += 2;
    return true;
}
bool read_u32(const std::vector<uint8_t> &bytes, size_t &offset, uint32_t &value, std::string &error) {
    if (offset > bytes.size() || bytes.size() - offset < 4) { error = "truncated u32"; return false; }
    value = 0;
    for (int i = 0; i < 4; ++i) value |= static_cast<uint32_t>(bytes[offset++]) << (i * 8);
    return true;
}
bool read_u64(const std::vector<uint8_t> &bytes, size_t &offset, uint64_t &value, std::string &error) {
    if (offset > bytes.size() || bytes.size() - offset < 8) { error = "truncated u64"; return false; }
    value = 0;
    for (int i = 0; i < 8; ++i) value |= static_cast<uint64_t>(bytes[offset++]) << (i * 8);
    return true;
}
bool read_hash(const std::vector<uint8_t> &bytes, size_t &offset, Hash128 &value, std::string &error) {
    return read_u64(bytes, offset, value.lo, error) && read_u64(bytes, offset, value.hi, error);
}

bool in_bounds(uint32_t offset, uint32_t size, size_t total_size) {
    return offset <= total_size && size <= total_size - offset;
}
bool table_size_matches(uint32_t byte_length, uint32_t count, uint32_t entry_size) {
    if (entry_size == 0) return true;
    const uint64_t size = static_cast<uint64_t>(count) * entry_size;
    return size <= std::numeric_limits<uint32_t>::max() && byte_length == size;
}
std::string hash_key(Hash128 value) { return std::to_string(value.lo) + ":" + std::to_string(value.hi); }
bool valid_type_kind(uint32_t value) { return value <= static_cast<uint32_t>(TypeKind::alias); }
bool valid_symbol_kind(uint32_t value) { return value <= static_cast<uint32_t>(SymbolKind::function); }
bool valid_hash_kind(uint32_t value) { return value <= static_cast<uint32_t>(HashKind::signature); }
bool valid_hash_target(uint32_t value) { return value <= static_cast<uint32_t>(HashTargetKind::function); }
bool valid_compatibility(uint32_t value) { return value <= static_cast<uint32_t>(CompatibilityKind::incompatible); }
bool valid_map_opcode(uint8_t value) { return value <= static_cast<uint8_t>(MapOpcode::skip_field); }

}  // namespace

bool operator==(Hash128 lhs, Hash128 rhs) { return lhs.lo == rhs.lo && lhs.hi == rhs.hi; }

Hash128 hash_text(std::string_view text, uint64_t domain) {
    uint64_t lo = 1'469'598'103'934'665'603ULL ^ domain;
    uint64_t hi = 1'099'511'628'211ULL + domain;
    for (unsigned char c : text) {
        lo = (lo ^ c) * 1'099'511'628'211ULL;
        hi = (hi ^ (c + 31)) * 14'029'467'366'897'019'727ULL;
    }
    return {lo, hi};
}

Hash128 layout_hash(const Type &type, const std::vector<Field> &fields) {
    std::string input = type.name + ":" + std::to_string(type.size) + ":" + std::to_string(type.align);
    for (const auto &field : fields)
        input += field.name + std::to_string(field.offset) + std::to_string(field.type_id.lo);
    return hash_text(input, 0x4c41594f5554ULL);
}

Hash128 signature_hash(const Function &function) {
    std::string input = function.name + std::to_string(function.return_type.lo) +
                        std::to_string(function.return_type.hi);
    for (const auto &parameter : function.parameters)
        input += std::to_string(parameter.type_id.lo) + std::to_string(parameter.type_id.hi);
    return hash_text(input, 0x5349474e41545552ULL);
}

namespace {
void canonical_u32(std::vector<uint8_t> &bytes, uint32_t value) { put_u32(bytes, value); }
void canonical_hash(std::vector<uint8_t> &bytes, Hash128 value) { put_hash(bytes, value); }
void canonical_string(std::vector<uint8_t> &bytes, const std::string &value) {
    canonical_u32(bytes, static_cast<uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}
Hash128 effective_layout_hash(const AbiModule &module, const Type &type) {
    if (!(type.layout_hash == Hash128{})) return type.layout_hash;
    std::vector<Field> fields;
    for (uint32_t i = 0; i < type.field_count; ++i)
        fields.push_back(module.fields[type.field_begin + i]);
    return layout_hash(type, fields);
}
std::vector<Symbol> effective_symbols(const AbiModule &module) {
    if (!module.symbols.empty()) return module.symbols;
    std::vector<Symbol> result;
    for (uint32_t i = 0; i < module.types.size(); ++i)
        result.push_back({module.types[i].name, SymbolKind::type, i});
    for (uint32_t i = 0; i < module.fields.size(); ++i)
        result.push_back({module.fields[i].name, SymbolKind::field, i});
    for (uint32_t i = 0; i < module.functions.size(); ++i)
        result.push_back({module.functions[i].name, SymbolKind::function, i});
    return result;
}
}

std::vector<uint8_t> canonical_bytes(const AbiModule &module) {
    std::vector<uint8_t> bytes;
    canonical_string(bytes, "ABIX-CANONICAL");
    canonical_u32(bytes, 1);
    canonical_u32(bytes, module.arch);
    canonical_u32(bytes, module.os);
    canonical_u32(bytes, module.target_abi);
    canonical_u32(bytes, module.compiler);
    canonical_u32(bytes, module.calling_convention);

    std::vector<uint32_t> type_indices(module.types.size());
    for (uint32_t i = 0; i < type_indices.size(); ++i) type_indices[i] = i;
    std::sort(type_indices.begin(), type_indices.end(), [&](uint32_t a, uint32_t b) {
        return module.types[a].id.lo == module.types[b].id.lo
            ? module.types[a].id.hi < module.types[b].id.hi : module.types[a].id.lo < module.types[b].id.lo;
    });
    canonical_u32(bytes, static_cast<uint32_t>(type_indices.size()));
    for (const auto index : type_indices) {
        const auto &type = module.types[index];
        canonical_hash(bytes, type.id); canonical_hash(bytes, effective_layout_hash(module, type));
        canonical_string(bytes, type.name); canonical_u32(bytes, static_cast<uint32_t>(type.kind));
        canonical_u32(bytes, type.flags); canonical_u32(bytes, type.size); canonical_u32(bytes, type.align);
        canonical_u32(bytes, type.array_count); canonical_u32(bytes, type.field_count);
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto &field = module.fields[type.field_begin + i];
            canonical_hash(bytes, field.owner_type); canonical_string(bytes, field.name);
            canonical_hash(bytes, field.type_id); canonical_u32(bytes, field.offset); canonical_u32(bytes, field.flags);
        }
    }

    std::vector<uint32_t> function_indices(module.functions.size());
    for (uint32_t i = 0; i < function_indices.size(); ++i) function_indices[i] = i;
    std::sort(function_indices.begin(), function_indices.end(), [&](uint32_t a, uint32_t b) {
        const auto &x = module.functions[a]; const auto &y = module.functions[b];
        return x.signature.lo == y.signature.lo ? x.signature.hi < y.signature.hi : x.signature.lo < y.signature.lo;
    });
    canonical_u32(bytes, static_cast<uint32_t>(function_indices.size()));
    for (const auto index : function_indices) {
        const auto &function = module.functions[index];
        canonical_hash(bytes, function.owner_type); canonical_string(bytes, function.name);
        canonical_hash(bytes, function.signature); canonical_hash(bytes, function.return_type);
        canonical_u32(bytes, function.calling_convention); canonical_u32(bytes, function.flags);
        canonical_u32(bytes, static_cast<uint32_t>(function.parameters.size()));
        for (const auto &parameter : function.parameters) {
            canonical_string(bytes, parameter.name); canonical_hash(bytes, parameter.type_id);
            canonical_u32(bytes, parameter.flags);
        }
    }

    canonical_u32(bytes, static_cast<uint32_t>(module.maps.size()));
    for (const auto &map : module.maps) {
        canonical_hash(bytes, map.source_type); canonical_hash(bytes, map.target_type);
        canonical_string(bytes, map.source_name); canonical_string(bytes, map.target_name);
        canonical_u32(bytes, map.flags); canonical_u32(bytes, static_cast<uint32_t>(map.operations.size()));
        for (const auto &operation : map.operations) {
            bytes.push_back(static_cast<uint8_t>(operation.opcode));
            canonical_u32(bytes, operation.source_field); canonical_u32(bytes, operation.target_field);
            canonical_hash(bytes, operation.auxiliary);
        }
    }
    canonical_u32(bytes, static_cast<uint32_t>(module.compatibility.size()));
    for (const auto &record : module.compatibility) {
        canonical_hash(bytes, record.source_type); canonical_hash(bytes, record.target_type);
        canonical_u32(bytes, static_cast<uint32_t>(record.kind)); canonical_u32(bytes, record.map_index);
        canonical_u32(bytes, record.flags);
    }

    auto symbols = effective_symbols(module);
    std::sort(symbols.begin(), symbols.end(), [](const Symbol &a, const Symbol &b) {
        return static_cast<uint32_t>(a.kind) == static_cast<uint32_t>(b.kind)
            ? (a.name == b.name ? a.target_index < b.target_index : a.name < b.name)
            : static_cast<uint32_t>(a.kind) < static_cast<uint32_t>(b.kind);
    });
    canonical_u32(bytes, static_cast<uint32_t>(symbols.size()));
    for (const auto &symbol : symbols) {
        canonical_u32(bytes, static_cast<uint32_t>(symbol.kind)); canonical_string(bytes, symbol.name);
        canonical_u32(bytes, symbol.target_index);
    }
    return bytes;
}

Hash128 abi_hash(const AbiModule &module) {
    const auto bytes = canonical_bytes(module);
    return hash_text(std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()), 0x41424948415348ULL);
}

std::vector<HashRecord> hash_table(const AbiModule &module) {
    std::vector<HashRecord> records;
    records.push_back({HashKind::artifact, HashTargetKind::module, 0, 0, abi_hash(module)});
    for (uint32_t i = 0; i < module.types.size(); ++i) {
        records.push_back({HashKind::type_id, HashTargetKind::type, i, 0, module.types[i].id});
        records.push_back({HashKind::layout, HashTargetKind::type, i, 0, effective_layout_hash(module, module.types[i])});
    }
    for (uint32_t i = 0; i < module.functions.size(); ++i)
        records.push_back({HashKind::signature, HashTargetKind::function, i, 0, module.functions[i].signature});
    return records;
}

namespace {
const Type *find_type_by_name(const AbiModule &, const std::string &);
const Type *find_type_by_id(const AbiModule &, Hash128);
}

const char *compatibility_name(CompatibilityKind kind) {
    switch (kind) {
        case CompatibilityKind::identical: return "identical";
        case CompatibilityKind::layout_compatible: return "layout_compatible";
        case CompatibilityKind::map_compatible: return "map_compatible";
        case CompatibilityKind::incompatible: return "incompatible";
    }
    return "unknown";
}

namespace {
const Type *find_type_by_name(const AbiModule &module, const std::string &name) {
    const auto it = std::find_if(module.types.begin(), module.types.end(), [&](const Type &type) {
        return type.name == name;
    });
    return it == module.types.end() ? nullptr : &*it;
}
const Type *find_type_by_id(const AbiModule &module, Hash128 id) {
    const auto it = std::find_if(module.types.begin(), module.types.end(), [&](const Type &type) {
        return type.id == id;
    });
    return it == module.types.end() ? nullptr : &*it;
}
bool scalar_kind(const AbiModule &module, Hash128 id, bool &floating) {
    const auto *type = find_type_by_id(module, id);
    if (!type || type->kind != TypeKind::primitive) return false;
    floating = type->name.find("float") != std::string::npos || type->name.find("double") != std::string::npos;
    return true;
}
}

bool build_compatibility(const AbiModule &source, const AbiModule &target, AbiModule &result, std::string &error) {
    if (!validate(source, error) || !validate(target, error)) return false;
    result = target;
    result.compatibility.clear();
    result.maps.clear();
    for (const auto &from : source.types) {
        const auto *to = find_type_by_name(target, from.name);
        if (!to) continue;
        CompatibilityRecord compatibility{from.id, to->id, CompatibilityKind::incompatible, UINT32_MAX, 0};
        if (from.id == to->id && effective_layout_hash(source, from) == effective_layout_hash(target, *to)) {
            compatibility.kind = CompatibilityKind::identical;
        } else if (effective_layout_hash(source, from) == effective_layout_hash(target, *to)) {
            compatibility.kind = CompatibilityKind::layout_compatible;
        } else if (from.kind == TypeKind::record && to->kind == TypeKind::record) {
            MapRecord map{from.id, to->id, from.name, to->name, 0, 0, 0, {}};
            for (uint32_t target_index = 0; target_index < to->field_count; ++target_index) {
                const auto &target_field = target.fields[to->field_begin + target_index];
                uint32_t source_index = UINT32_MAX;
                for (uint32_t i = 0; i < from.field_count; ++i) {
                    if (source.fields[from.field_begin + i].name == target_field.name) { source_index = i; break; }
                }
                if (source_index == UINT32_MAX) {
                    map.operations.push_back({MapOpcode::add_default, UINT32_MAX, target_index,
                                              0, target_field.offset,
                                              find_type_by_id(target, target_field.type_id)->size, {}});
                    continue;
                }
                const auto &source_field = source.fields[from.field_begin + source_index];
                if (source_field.type_id == target_field.type_id)
                    map.operations.push_back({MapOpcode::copy_field, source_index, target_index,
                                              source_field.offset, target_field.offset,
                                              std::min(find_type_by_id(source, source_field.type_id)->size,
                                                       find_type_by_id(target, target_field.type_id)->size), {}});
                else {
                    bool source_float = false, target_float = false;
                    if (!scalar_kind(source, source_field.type_id, source_float) ||
                        !scalar_kind(target, target_field.type_id, target_float) || source_float != target_float) {
                        map.operations.clear();
                        break;
                    }
                    map.operations.push_back({source_float ? MapOpcode::convert_float : MapOpcode::convert_int,
                                              source_index, target_index, source_field.offset, target_field.offset,
                                              0, target_field.type_id});
                }
            }
            if (!map.operations.empty()) {
                for (uint32_t source_index = 0; source_index < from.field_count; ++source_index) {
                    const auto &source_field = source.fields[from.field_begin + source_index];
                    bool found = false;
                    for (uint32_t i = 0; i < to->field_count; ++i)
                        if (target.fields[to->field_begin + i].name == source_field.name) { found = true; break; }
                    if (!found) map.operations.push_back({MapOpcode::skip_field, source_index, UINT32_MAX,
                                                          source_field.offset, 0, 0, {}});
                }
                map.operation_count = static_cast<uint32_t>(map.operations.size());
                compatibility.kind = CompatibilityKind::map_compatible;
                compatibility.map_index = static_cast<uint32_t>(result.maps.size());
                result.maps.push_back(std::move(map));
            }
        }
        result.compatibility.push_back(compatibility);
    }
    return validate(result, error);
}

bool validate(const AbiModule &module, std::string &error) {
    error.clear();
    std::unordered_map<std::string, const Type *> type_by_id;
    for (const auto &type : module.types) {
        if (type.name.empty() || type.size == 0 || type.align == 0 ||
            (type.align & (type.align - 1)) != 0 || !valid_type_kind(static_cast<uint32_t>(type.kind))) {
            error = "invalid type: " + type.name;
            return false;
        }
        if (type.id == Hash128{}) { error = "type has no id: " + type.name; return false; }
        if (!type_by_id.emplace(hash_key(type.id), &type).second) {
            error = "duplicate type id: " + type.name;
            return false;
        }
        if (type.field_begin > module.fields.size() ||
            type.field_count > module.fields.size() - type.field_begin) {
            error = "type field range is out of bounds: " + type.name;
            return false;
        }
    }
    for (const auto &field : module.fields) {
        if (!(field.owner_type == Hash128{}) && type_by_id.count(hash_key(field.owner_type)) == 0) {
            error = "field references unknown owner: " + field.name;
            return false;
        }
        if (field.name.empty()) {
            error = "field has no ABI name";
            return false;
        }
        if (type_by_id.count(hash_key(field.type_id)) == 0) {
            error = "field references unknown type: " + field.name;
            return false;
        }
    }
    for (const auto &type : module.types) {
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto &field = module.fields[type.field_begin + i];
            const auto *field_type = type_by_id.at(hash_key(field.type_id));
            if (field.flags & field_bitfield) {
                // Bitfield layout check: the entire bit range must fit inside
                // the parent type.  We use the bit-level position so that a
                // trailing 1-bit field (e.g. libc++ __is_long_) whose
                // underlying integer type would overflow the byte-level check
                // is still accepted.
                const uint32_t bit_offset =
                    field.offset * 8 + ((field.flags >> field_bit_offset_shift) & 0xFF);
                const uint32_t bit_width =
                    (field.flags >> field_bit_width_shift) & 0xFF;
                if (bit_offset + bit_width > type.size * 8) {
                    error = "field exceeds type layout: " + field.name;
                    return false;
                }
            } else {
                // Non-bitfield: the field's storage must fit inside the parent.
                if (field.offset > type.size || field_type->size > type.size - field.offset) {
                    error = "field exceeds type layout: " + field.name;
                    return false;
                }
            }
        }
    }
    for (const auto &function : module.functions) {
        if (function.name.empty()) { error = "function has no name"; return false; }
        if (!(function.owner_type == Hash128{}) && type_by_id.count(hash_key(function.owner_type)) == 0) {
            error = "function references unknown owner: " + function.name;
            return false;
        }
        if (type_by_id.count(hash_key(function.return_type)) == 0) {
            error = "function references unknown return type: " + function.name;
            return false;
        }
        for (const auto &parameter : function.parameters) {
            if (type_by_id.count(hash_key(parameter.type_id)) == 0) {
                error = "parameter references unknown type";
                return false;
            }
        }
    }
    for (const auto &symbol : module.symbols) {
        if (!valid_symbol_kind(static_cast<uint32_t>(symbol.kind)) || symbol.name.empty()) {
            error = "invalid symbol: " + symbol.name;
            return false;
        }
        const size_t target_count = symbol.kind == SymbolKind::type ? module.types.size() :
                                    symbol.kind == SymbolKind::field ? module.fields.size() : module.functions.size();
        if (symbol.target_index >= target_count) { error = "invalid symbol target: " + symbol.name; return false; }
    }
    for (const auto &map : module.maps) {
        const auto *target = find_type_by_id(module, map.target_type);
        if (!target || map.operation_count != map.operations.size()) {
            error = "invalid map record"; return false;
        }
        for (const auto &operation : map.operations) {
            if (!valid_map_opcode(static_cast<uint8_t>(operation.opcode)) ||
                (operation.target_field != UINT32_MAX && operation.target_field >= target->field_count)) {
                error = "invalid map operation"; return false;
            }
        }
    }
    for (const auto &record : module.compatibility) {
        if (!valid_compatibility(static_cast<uint32_t>(record.kind)) ||
            !find_type_by_id(module, record.target_type) ||
            (record.map_index != UINT32_MAX && record.map_index >= module.maps.size())) {
            error = "invalid compatibility record"; return false;
        }
    }
    return true;
}

bool project_symbols(const AbiModule &source, const std::vector<std::string> &symbols,
                     AbiModule &result, std::string &error) {
    if (!validate(source, error)) return false;
    auto exact_or_short = [](const std::string &name, const std::string &symbol) {
        const auto separator = name.rfind("::");
        return name == symbol || (separator != std::string::npos && name.substr(separator + 2) == symbol);
    };
    auto has = [&](const std::string &name) {
        return symbols.empty() || std::any_of(symbols.begin(), symbols.end(),
                                               [&](const auto &symbol) { return exact_or_short(name, symbol); });
    };
    auto member_selector = [&](const std::string &owner, const std::string &member) {
        const std::string qualified = owner + "::" + member;
        return std::any_of(symbols.begin(), symbols.end(), [&](const auto &symbol) {
            return symbol == qualified || symbol == member;
        });
    };
    std::unordered_set<std::string> selected_type_ids;
    auto add_type = [&](Hash128 id) { selected_type_ids.insert(hash_key(id)); };
    auto has_type = [&](Hash128 id) { return selected_type_ids.count(hash_key(id)) != 0; };
    result = source;
    result.types.clear(); result.fields.clear(); result.functions.clear(); result.symbols.clear();
    result.compatibility.clear(); result.maps.clear();
    std::unordered_set<std::string> complete_type_ids;
    auto complete = [&](Hash128 id) { complete_type_ids.insert(hash_key(id)); };
    auto is_complete = [&](Hash128 id) { return complete_type_ids.count(hash_key(id)) != 0; };
    for (const auto &type : source.types) {
        if (has(type.name)) {
            add_type(type.id);
            complete(type.id);
        }
        for (uint32_t i = 0; i < type.field_count; ++i)
            if (member_selector(type.name, source.fields[type.field_begin + i].name)) add_type(type.id);
    }
    for (const auto &function : source.functions) {
        const bool owner_complete = !(function.owner_type == Hash128{}) && is_complete(function.owner_type);
        if (!has(function.name) && !owner_complete) continue;
        if (!(function.owner_type == Hash128{}) && !is_complete(function.owner_type)) add_type(function.owner_type);
        result.functions.push_back(function);
        add_type(function.return_type);
        for (const auto &parameter : function.parameters) add_type(parameter.type_id);
    }
    std::unordered_set<std::string> selected_field_keys;
    for (const auto &type : source.types) {
        const bool owner_complete = is_complete(type.id);
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto &field = source.fields[type.field_begin + i];
            if (owner_complete || member_selector(type.name, field.name))
                selected_field_keys.insert(hash_key(type.id) + ":" + field.name);
        }
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto &type : source.types) {
            if (!has_type(type.id)) continue;
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto field_type = source.fields[type.field_begin + i].type_id;
                if (selected_field_keys.count(hash_key(type.id) + ":" + source.fields[type.field_begin + i].name) &&
                    !has_type(field_type)) { add_type(field_type); changed = true; }
            }
        }
    }
    for (const auto &type : source.types) {
        if (!has_type(type.id)) continue;
        Type projected = type;
        projected.field_begin = static_cast<uint32_t>(result.fields.size());
        projected.field_count = 0;
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto &field = source.fields[type.field_begin + i];
            if (!is_complete(type.id) && selected_field_keys.count(hash_key(type.id) + ":" + field.name) == 0)
                continue;
            result.fields.push_back(field);
            ++projected.field_count;
        }
        result.types.push_back(std::move(projected));
    }
    if (!symbols.empty() && result.types.empty() && result.functions.empty()) {
        error = "export symbols did not match any ABI declaration";
        return false;
    }
    return validate(result, error);
}

std::string type_kind_name(TypeKind kind) {
    switch (kind) {
        case TypeKind::primitive: return "primitive";
        case TypeKind::enumeration: return "enum";
        case TypeKind::record: return "record";
        case TypeKind::pointer: return "pointer";
        case TypeKind::array: return "array";
        case TypeKind::function: return "function";
        case TypeKind::namespace_type: return "namespace";
        case TypeKind::alias: return "alias";
    }
    return "unknown";
}

bool write_abix(const AbiModule &module, const std::string &path, std::string &error) {
    if (!validate(module, error)) return false;
    std::unordered_map<std::string, uint32_t> string_offsets;
    std::vector<uint8_t> string_bytes;
    auto intern = [&](const std::string &text) {
        const auto existing = string_offsets.find(text);
        if (existing != string_offsets.end()) return existing->second;
        const uint32_t offset = static_cast<uint32_t>(string_bytes.size());
        string_offsets.emplace(text, offset);
        string_bytes.insert(string_bytes.end(), text.begin(), text.end());
        string_bytes.push_back(0);
        return offset;
    };
    intern(module.package_name); intern(module.package_version);
    for (const auto &type : module.types) intern(type.name);
    for (const auto &field : module.fields) intern(field.name);
    for (const auto &map : module.maps) { intern(map.source_name); intern(map.target_name); }
    std::vector<Parameter> all_parameters;
    std::vector<uint32_t> parameter_begins;
    for (const auto &function : module.functions) {
        intern(function.name);
        parameter_begins.push_back(static_cast<uint32_t>(all_parameters.size()));
        for (const auto &parameter : function.parameters) { intern(parameter.name); all_parameters.push_back(parameter); }
    }
    std::vector<Symbol> artifact_symbols = effective_symbols(module);
    for (const auto &symbol : artifact_symbols) intern(symbol.name);
    std::vector<MapOperation> all_map_operations;
    std::vector<uint32_t> map_operation_begins;
    for (const auto &map : module.maps) {
        map_operation_begins.push_back(static_cast<uint32_t>(all_map_operations.size()));
        all_map_operations.insert(all_map_operations.end(), map.operations.begin(), map.operations.end());
    }

    std::vector<SectionData> sections;
    SectionData string_table{strings, static_cast<uint32_t>(string_offsets.size()), 0, section_required, {}};
    put_u32(string_table.bytes, string_table.count);
    put_u32(string_table.bytes, static_cast<uint32_t>(string_bytes.size()));
    string_table.bytes.insert(string_table.bytes.end(), string_bytes.begin(), string_bytes.end());
    sections.push_back(std::move(string_table));
    SectionData abi_identity{identity, 1, kIdentityEntrySize, section_required, {}};
    put_u32(abi_identity.bytes, module.arch); put_u32(abi_identity.bytes, module.os);
    put_u32(abi_identity.bytes, module.compiler); put_u32(abi_identity.bytes, module.calling_convention);
    put_u32(abi_identity.bytes, 0);
    put_hash(abi_identity.bytes, abi_hash(module));
    put_u32(abi_identity.bytes, string_offsets.at(module.package_name));
    put_u32(abi_identity.bytes, static_cast<uint32_t>(module.package_name.size()));
    put_u32(abi_identity.bytes, string_offsets.at(module.package_version));
    put_u32(abi_identity.bytes, static_cast<uint32_t>(module.package_version.size()));
    sections.push_back(std::move(abi_identity));
    SectionData target_table{target, 1, 12, section_required, {}};
    put_u32(target_table.bytes, module.arch); put_u32(target_table.bytes, module.os);
    put_u32(target_table.bytes, module.target_abi);
    sections.push_back(std::move(target_table));
    SectionData type_table{types, static_cast<uint32_t>(module.types.size()), kTypeEntrySize, section_required, {}};
    for (const auto &type : module.types) {
        put_hash(type_table.bytes, type.id); put_hash(type_table.bytes, type.layout_hash);
        put_u32(type_table.bytes, string_offsets.at(type.name));
        put_u32(type_table.bytes, static_cast<uint32_t>(type.name.size())); put_u32(type_table.bytes, static_cast<uint32_t>(type.kind));
        put_u32(type_table.bytes, type.flags); put_u32(type_table.bytes, type.size); put_u32(type_table.bytes, type.align);
        put_u32(type_table.bytes, type.field_begin); put_u32(type_table.bytes, type.field_count); put_u32(type_table.bytes, type.array_count);
    }
    sections.push_back(std::move(type_table));
    SectionData field_table{fields, static_cast<uint32_t>(module.fields.size()), kFieldEntrySize, section_required, {}};
    for (const auto &field : module.fields) {
        put_hash(field_table.bytes, field.owner_type);
        put_u32(field_table.bytes, string_offsets.at(field.name)); put_u32(field_table.bytes, static_cast<uint32_t>(field.name.size()));
        put_hash(field_table.bytes, field.type_id); put_u32(field_table.bytes, field.offset); put_u32(field_table.bytes, field.flags);
    }
    sections.push_back(std::move(field_table));
    SectionData function_table{functions, static_cast<uint32_t>(module.functions.size()), kFunctionEntrySize, section_required, {}};
    for (uint32_t i = 0; i < module.functions.size(); ++i) {
        const auto &function = module.functions[i];
        put_hash(function_table.bytes, function.owner_type);
        put_u32(function_table.bytes, string_offsets.at(function.name)); put_u32(function_table.bytes, static_cast<uint32_t>(function.name.size()));
        put_hash(function_table.bytes, function.signature); put_hash(function_table.bytes, function.return_type);
        put_u32(function_table.bytes, parameter_begins[i]); put_u32(function_table.bytes, static_cast<uint32_t>(function.parameters.size()));
        put_u32(function_table.bytes, function.calling_convention); put_u32(function_table.bytes, function.flags);
    }
    sections.push_back(std::move(function_table));
    SectionData parameter_table{parameters, static_cast<uint32_t>(all_parameters.size()), kParameterEntrySize, section_required, {}};
    for (const auto &parameter : all_parameters) {
        put_u32(parameter_table.bytes, string_offsets.at(parameter.name)); put_u32(parameter_table.bytes, static_cast<uint32_t>(parameter.name.size()));
        put_hash(parameter_table.bytes, parameter.type_id); put_u32(parameter_table.bytes, parameter.flags);
    }
    sections.push_back(std::move(parameter_table));
    SectionData symbol_table{symbols, static_cast<uint32_t>(artifact_symbols.size()), kSymbolEntrySize, section_required, {}};
    for (const auto &symbol : artifact_symbols) {
        put_u32(symbol_table.bytes, string_offsets.at(symbol.name)); put_u32(symbol_table.bytes, static_cast<uint32_t>(symbol.name.size()));
        put_u32(symbol_table.bytes, static_cast<uint32_t>(symbol.kind)); put_u32(symbol_table.bytes, symbol.target_index);
    }
    sections.push_back(std::move(symbol_table));
    SectionData descriptor{hash_descriptor, 1, kHashDescriptorEntrySize, section_required, {}};
    put_u16(descriptor.bytes, kHashAlgorithm); put_u16(descriptor.bytes, 1);
    put_u32(descriptor.bytes, 1); put_u32(descriptor.bytes, 0); put_u32(descriptor.bytes, 0);
    sections.push_back(std::move(descriptor));
    const auto records = hash_table(module);
    SectionData hash_table_section{hash_records, static_cast<uint32_t>(records.size()), kHashRecordEntrySize, section_required, {}};
    for (const auto &record : records) {
        put_u32(hash_table_section.bytes, static_cast<uint32_t>(record.kind));
        put_u32(hash_table_section.bytes, static_cast<uint32_t>(record.target_kind));
        put_u32(hash_table_section.bytes, record.target_index);
        put_u32(hash_table_section.bytes, record.flags);
        put_hash(hash_table_section.bytes, record.value);
    }
    sections.push_back(std::move(hash_table_section));
    SectionData compatibility_table{compatibility, static_cast<uint32_t>(module.compatibility.size()), kCompatibilityEntrySize, 0, {}};
    for (const auto &record : module.compatibility) {
        put_hash(compatibility_table.bytes, record.source_type); put_hash(compatibility_table.bytes, record.target_type);
        put_u32(compatibility_table.bytes, static_cast<uint32_t>(record.kind)); put_u32(compatibility_table.bytes, record.map_index);
        put_u32(compatibility_table.bytes, record.flags);
    }
    sections.push_back(std::move(compatibility_table));
    SectionData map_table{maps, static_cast<uint32_t>(module.maps.size()), kMapEntrySize, 0, {}};
    for (uint32_t i = 0; i < module.maps.size(); ++i) {
        const auto &map = module.maps[i];
        put_hash(map_table.bytes, map.source_type); put_hash(map_table.bytes, map.target_type);
        put_u32(map_table.bytes, string_offsets.at(map.source_name)); put_u32(map_table.bytes, static_cast<uint32_t>(map.source_name.size()));
        put_u32(map_table.bytes, string_offsets.at(map.target_name)); put_u32(map_table.bytes, static_cast<uint32_t>(map.target_name.size()));
        put_u32(map_table.bytes, map_operation_begins[i]); put_u32(map_table.bytes, static_cast<uint32_t>(map.operations.size()));
        put_u32(map_table.bytes, map.flags);
    }
    sections.push_back(std::move(map_table));
    SectionData operation_table{map_operations, static_cast<uint32_t>(all_map_operations.size()), kMapOperationEntrySize, 0, {}};
    for (const auto &operation : all_map_operations) {
        operation_table.bytes.push_back(static_cast<uint8_t>(operation.opcode)); operation_table.bytes.push_back(0);
        put_u16(operation_table.bytes, 0); put_u32(operation_table.bytes, operation.source_field);
        put_u32(operation_table.bytes, operation.target_field); put_u32(operation_table.bytes, operation.source_offset);
        put_u32(operation_table.bytes, operation.target_offset); put_u32(operation_table.bytes, operation.byte_count);
        put_hash(operation_table.bytes, operation.auxiliary);
    }
    sections.push_back(std::move(operation_table));

    std::vector<uint8_t> bytes = {'A', 'B', 'I', 'X'};
    put_u16(bytes, kFormatVersion); put_u16(bytes, kHashAlgorithm); put_u32(bytes, 0);
    put_u32(bytes, static_cast<uint32_t>(sections.size())); put_u32(bytes, kHeaderSize);
    const size_t directory_offset = bytes.size();
    bytes.resize(directory_offset + sections.size() * kDirectoryEntrySize);
    for (size_t i = 0; i < sections.size(); ++i) {
        const auto &section = sections[i];
        const uint32_t section_offset = static_cast<uint32_t>(bytes.size());
        const size_t entry_offset = directory_offset + i * kDirectoryEntrySize;
        const uint32_t values[] = {section.id, section_offset, static_cast<uint32_t>(section.bytes.size()),
                                   section.count, section.entry_size, section.flags};
        for (size_t field = 0; field < 6; ++field)
            for (int byte = 0; byte < 4; ++byte)
                bytes[entry_offset + field * 4 + byte] = static_cast<uint8_t>(values[field] >> (byte * 8));
        bytes.insert(bytes.end(), section.bytes.begin(), section.bytes.end());
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) { error = "cannot open output: " + path; return false; }
    output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) error = "failed to write output: " + path;
    return static_cast<bool>(output);
}

bool read_abix(const std::string &path, AbiModule &module, std::string &error) {
    error.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) { error = "cannot open input: " + path; return false; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    if (bytes.size() < kHeaderSize || std::memcmp(bytes.data(), "ABIX", 4) != 0) { error = "invalid ABIX header"; return false; }
    size_t header_offset = 4;
    uint16_t version = 0, hash_algorithm = 0;
    uint32_t flags = 0, section_count = 0, directory_offset = 0;
    if (!read_u16(bytes, header_offset, version, error) || !read_u16(bytes, header_offset, hash_algorithm, error) ||
        !read_u32(bytes, header_offset, flags, error) || !read_u32(bytes, header_offset, section_count, error) ||
        !read_u32(bytes, header_offset, directory_offset, error)) return false;
    if (version != kFormatVersion || hash_algorithm != kHashAlgorithm) { error = "unsupported ABIX format or hash algorithm"; return false; }
    if (section_count == 0 || section_count > 64 || !in_bounds(directory_offset, section_count * kDirectoryEntrySize, bytes.size())) {
        error = "invalid section directory"; return false;
    }
    std::unordered_map<uint32_t, DirectoryEntry> directory;
    for (uint32_t i = 0; i < section_count; ++i) {
        size_t entry_offset = directory_offset + i * kDirectoryEntrySize;
        uint32_t id = 0; DirectoryEntry entry{};
        if (!read_u32(bytes, entry_offset, id, error) || !read_u32(bytes, entry_offset, entry.offset, error) ||
            !read_u32(bytes, entry_offset, entry.byte_length, error) || !read_u32(bytes, entry_offset, entry.count, error) ||
            !read_u32(bytes, entry_offset, entry.entry_size, error) || !read_u32(bytes, entry_offset, entry.flags, error)) return false;
        if (id == 0 || directory.count(id) != 0 || !in_bounds(entry.offset, entry.byte_length, bytes.size()) ||
            !table_size_matches(entry.byte_length, entry.count, entry.entry_size)) {
            error = "invalid section directory entry"; return false;
        }
        if (id > target && (entry.flags & section_required) != 0) { error = "unsupported required section"; return false; }
        directory.emplace(id, entry);
    }
    auto required = [&](Section id, uint32_t entry_size) -> const DirectoryEntry * {
        const auto it = directory.find(id);
        if (it == directory.end() || it->second.entry_size != entry_size || (it->second.flags & section_required) == 0) { error = "missing or invalid required section"; return nullptr; }
        return &it->second;
    };
    auto optional = [&](Section id, uint32_t entry_size) -> const DirectoryEntry * {
        const auto it = directory.find(id);
        if (it == directory.end()) return nullptr;
        if (it->second.entry_size != entry_size) { error = "invalid optional section"; return nullptr; }
        return &it->second;
    };
    const auto *string_table = required(strings, 0); const auto *abi_identity = required(identity, kIdentityEntrySize);
    const auto *target_table = required(target, 12);
    const auto *type_table = required(types, kTypeEntrySize); const auto *field_table = required(fields, kFieldEntrySize);
    const auto *function_table = required(functions, kFunctionEntrySize); const auto *parameter_table = required(parameters, kParameterEntrySize);
    const auto *symbol_table = required(symbols, kSymbolEntrySize);
    const auto *descriptor_table = required(hash_descriptor, kHashDescriptorEntrySize);
    const auto *hash_table_section = required(hash_records, kHashRecordEntrySize);
    const auto *compatibility_table = optional(compatibility, kCompatibilityEntrySize);
    const auto *map_table = optional(maps, kMapEntrySize);
    const auto *operation_table = optional(map_operations, kMapOperationEntrySize);
    if (!error.empty()) return false;
    if (!string_table || !abi_identity || !type_table || !field_table || !function_table || !parameter_table ||
        !symbol_table || !descriptor_table || !hash_table_section || !target_table ||
        ((map_table == nullptr) != (operation_table == nullptr))) return false;
    if (abi_identity->count != 1 || target_table->count != 1 || string_table->byte_length < 8) { error = "invalid ABI identity, target, or string table"; return false; }
    size_t string_header_offset = string_table->offset;
    uint32_t string_count = 0, string_byte_count = 0;
    if (!read_u32(bytes, string_header_offset, string_count, error) || !read_u32(bytes, string_header_offset, string_byte_count, error) ||
        string_count != string_table->count || string_byte_count != string_table->byte_length - 8 ||
        !in_bounds(static_cast<uint32_t>(string_header_offset), string_byte_count, bytes.size())) {
        error = "invalid string table"; return false;
    }
    const uint32_t string_base = static_cast<uint32_t>(string_header_offset);
    auto read_string = [&](uint32_t offset, uint32_t length, std::string &result) {
        if (offset > string_byte_count || length > string_byte_count - offset || !in_bounds(string_base + offset, length, bytes.size())) {
            error = "string reference out of bounds"; return false;
        }
        result.assign(reinterpret_cast<const char *>(bytes.data() + string_base + offset), length);
        return true;
    };
    module = {};
    size_t target_offset = target_table->offset;
    if (!read_u32(bytes, target_offset, module.arch, error) || !read_u32(bytes, target_offset, module.os, error) ||
        !read_u32(bytes, target_offset, module.target_abi, error)) return false;
    size_t identity_offset = abi_identity->offset;
    uint32_t identity_flags = 0, package_name_offset = 0, package_name_length = 0, package_version_offset = 0, package_version_length = 0;
    uint32_t identity_arch = 0, identity_os = 0;
    Hash128 artifact_hash{};
    if (!read_u32(bytes, identity_offset, identity_arch, error) || !read_u32(bytes, identity_offset, identity_os, error) ||
        !read_u32(bytes, identity_offset, module.compiler, error) || !read_u32(bytes, identity_offset, module.calling_convention, error) ||
        !read_u32(bytes, identity_offset, identity_flags, error) || !read_hash(bytes, identity_offset, artifact_hash, error) ||
        !read_u32(bytes, identity_offset, package_name_offset, error) || !read_u32(bytes, identity_offset, package_name_length, error) ||
        !read_u32(bytes, identity_offset, package_version_offset, error) || !read_u32(bytes, identity_offset, package_version_length, error) ||
        !read_string(package_name_offset, package_name_length, module.package_name) ||
        !read_string(package_version_offset, package_version_length, module.package_version) ||
        identity_arch != module.arch || identity_os != module.os) return false;
    size_t type_offset = type_table->offset;
    for (uint32_t i = 0; i < type_table->count; ++i) {
        Type type; uint32_t name_offset = 0, name_length = 0, kind = 0;
        if (!read_hash(bytes, type_offset, type.id, error) || !read_hash(bytes, type_offset, type.layout_hash, error) ||
            !read_u32(bytes, type_offset, name_offset, error) ||
            !read_u32(bytes, type_offset, name_length, error) || !read_u32(bytes, type_offset, kind, error) ||
            !read_u32(bytes, type_offset, type.flags, error) || !read_u32(bytes, type_offset, type.size, error) ||
            !read_u32(bytes, type_offset, type.align, error) || !read_u32(bytes, type_offset, type.field_begin, error) ||
            !read_u32(bytes, type_offset, type.field_count, error) || !read_u32(bytes, type_offset, type.array_count, error) ||
            !valid_type_kind(kind) || !read_string(name_offset, name_length, type.name)) {
            if (error.empty()) error = "invalid type kind";
            return false;
        }
        type.kind = static_cast<TypeKind>(kind); module.types.push_back(std::move(type));
    }
    size_t field_offset = field_table->offset;
    for (uint32_t i = 0; i < field_table->count; ++i) {
        Field field; uint32_t name_offset = 0, name_length = 0;
        if (!read_hash(bytes, field_offset, field.owner_type, error) || !read_u32(bytes, field_offset, name_offset, error) || !read_u32(bytes, field_offset, name_length, error) ||
            !read_hash(bytes, field_offset, field.type_id, error) || !read_u32(bytes, field_offset, field.offset, error) ||
            !read_u32(bytes, field_offset, field.flags, error) || !read_string(name_offset, name_length, field.name)) return false;
        module.fields.push_back(std::move(field));
    }
    std::vector<Parameter> all_parameters;
    size_t parameter_offset = parameter_table->offset;
    for (uint32_t i = 0; i < parameter_table->count; ++i) {
        Parameter parameter; uint32_t name_offset = 0, name_length = 0;
        if (!read_u32(bytes, parameter_offset, name_offset, error) || !read_u32(bytes, parameter_offset, name_length, error) ||
            !read_hash(bytes, parameter_offset, parameter.type_id, error) || !read_u32(bytes, parameter_offset, parameter.flags, error) ||
            !read_string(name_offset, name_length, parameter.name)) return false;
        all_parameters.push_back(std::move(parameter));
    }
    size_t function_offset = function_table->offset;
    for (uint32_t i = 0; i < function_table->count; ++i) {
        Function function; uint32_t name_offset = 0, name_length = 0, parameter_begin = 0, parameter_count = 0;
        if (!read_hash(bytes, function_offset, function.owner_type, error) || !read_u32(bytes, function_offset, name_offset, error) || !read_u32(bytes, function_offset, name_length, error) ||
            !read_hash(bytes, function_offset, function.signature, error) || !read_hash(bytes, function_offset, function.return_type, error) ||
            !read_u32(bytes, function_offset, parameter_begin, error) || !read_u32(bytes, function_offset, parameter_count, error) ||
            !read_u32(bytes, function_offset, function.calling_convention, error) || !read_u32(bytes, function_offset, function.flags, error) ||
            parameter_begin > all_parameters.size() || parameter_count > all_parameters.size() - parameter_begin ||
            !read_string(name_offset, name_length, function.name)) {
            if (error.empty()) error = "function parameter range out of bounds";
            return false;
        }
        function.parameters.insert(function.parameters.end(), all_parameters.begin() + parameter_begin,
                                   all_parameters.begin() + parameter_begin + parameter_count);
        module.functions.push_back(std::move(function));
    }
    size_t symbol_offset = symbol_table->offset;
    for (uint32_t i = 0; i < symbol_table->count; ++i) {
        Symbol symbol; uint32_t name_offset = 0, name_length = 0, kind = 0;
        if (!read_u32(bytes, symbol_offset, name_offset, error) || !read_u32(bytes, symbol_offset, name_length, error) ||
            !read_u32(bytes, symbol_offset, kind, error) || !read_u32(bytes, symbol_offset, symbol.target_index, error) ||
            !valid_symbol_kind(kind) || !read_string(name_offset, name_length, symbol.name)) {
            if (error.empty()) error = "invalid symbol kind";
            return false;
        }
        symbol.kind = static_cast<SymbolKind>(kind); module.symbols.push_back(std::move(symbol));
    }
    std::vector<MapOperation> all_operations;
    size_t operation_offset = operation_table ? operation_table->offset : 0;
    for (uint32_t i = 0; operation_table && i < operation_table->count; ++i) {
        if (operation_offset >= bytes.size()) { error = "truncated map operation"; return false; }
        const uint8_t opcode = bytes[operation_offset++];
        ++operation_offset;
        uint16_t reserved = 0;
        MapOperation operation;
        if (!read_u16(bytes, operation_offset, reserved, error) || !read_u32(bytes, operation_offset, operation.source_field, error) ||
            !read_u32(bytes, operation_offset, operation.target_field, error) || !read_u32(bytes, operation_offset, operation.source_offset, error) ||
            !read_u32(bytes, operation_offset, operation.target_offset, error) || !read_u32(bytes, operation_offset, operation.byte_count, error) ||
            !read_hash(bytes, operation_offset, operation.auxiliary, error) ||
            !valid_map_opcode(opcode)) { error = "invalid map operation"; return false; }
        operation.opcode = static_cast<MapOpcode>(opcode); all_operations.push_back(operation);
    }
    size_t map_offset = map_table ? map_table->offset : 0;
    for (uint32_t i = 0; map_table && i < map_table->count; ++i) {
        MapRecord map; uint32_t begin = 0, source_name_offset = 0, source_name_length = 0, target_name_offset = 0, target_name_length = 0;
        if (!read_hash(bytes, map_offset, map.source_type, error) || !read_hash(bytes, map_offset, map.target_type, error) ||
            !read_u32(bytes, map_offset, source_name_offset, error) || !read_u32(bytes, map_offset, source_name_length, error) ||
            !read_u32(bytes, map_offset, target_name_offset, error) || !read_u32(bytes, map_offset, target_name_length, error) ||
            !read_u32(bytes, map_offset, begin, error) || !read_u32(bytes, map_offset, map.operation_count, error) ||
            !read_u32(bytes, map_offset, map.flags, error) || begin > all_operations.size() ||
            map.operation_count > all_operations.size() - begin || !read_string(source_name_offset, source_name_length, map.source_name) ||
            !read_string(target_name_offset, target_name_length, map.target_name)) { error = "invalid map record"; return false; }
        map.operation_begin = begin;
        map.operations.insert(map.operations.end(), all_operations.begin() + begin, all_operations.begin() + begin + map.operation_count);
        module.maps.push_back(std::move(map));
    }
    size_t compatibility_offset = compatibility_table ? compatibility_table->offset : 0;
    for (uint32_t i = 0; compatibility_table && i < compatibility_table->count; ++i) {
        CompatibilityRecord record; uint32_t kind = 0;
        if (!read_hash(bytes, compatibility_offset, record.source_type, error) || !read_hash(bytes, compatibility_offset, record.target_type, error) ||
            !read_u32(bytes, compatibility_offset, kind, error) || !read_u32(bytes, compatibility_offset, record.map_index, error) ||
            !read_u32(bytes, compatibility_offset, record.flags, error) || !valid_compatibility(kind)) { error = "invalid compatibility record"; return false; }
        record.kind = static_cast<CompatibilityKind>(kind); module.compatibility.push_back(record);
    }
    size_t descriptor_offset = descriptor_table->offset;
    uint16_t descriptor_algorithm = 0, descriptor_version = 0;
    uint32_t canonical_version = 0, descriptor_flags = 0, descriptor_reserved = 0;
    if (!read_u16(bytes, descriptor_offset, descriptor_algorithm, error) ||
        !read_u16(bytes, descriptor_offset, descriptor_version, error) ||
        !read_u32(bytes, descriptor_offset, canonical_version, error) ||
        !read_u32(bytes, descriptor_offset, descriptor_flags, error) ||
        !read_u32(bytes, descriptor_offset, descriptor_reserved, error) ||
        descriptor_algorithm != kHashAlgorithm || descriptor_version != 1 || canonical_version != 1) {
        error = "unsupported hash descriptor";
        return false;
    }
    std::vector<HashRecord> loaded_records;
    size_t hash_offset = hash_table_section->offset;
    for (uint32_t i = 0; i < hash_table_section->count; ++i) {
        HashRecord record;
        uint32_t kind = 0, target = 0;
        if (!read_u32(bytes, hash_offset, kind, error) || !read_u32(bytes, hash_offset, target, error) ||
            !read_u32(bytes, hash_offset, record.target_index, error) || !read_u32(bytes, hash_offset, record.flags, error) ||
            !read_hash(bytes, hash_offset, record.value, error) || !valid_hash_kind(kind) || !valid_hash_target(target)) {
            error = "invalid hash table record";
            return false;
        }
        record.kind = static_cast<HashKind>(kind);
        record.target_kind = static_cast<HashTargetKind>(target);
        loaded_records.push_back(record);
    }
    if (!validate(module, error)) return false;
    const auto expected_records = hash_table(module);
    if (loaded_records.size() != expected_records.size()) {
        error = "hash table count does not match artifact";
        return false;
    }
    for (size_t i = 0; i < expected_records.size(); ++i) {
        const auto &actual = loaded_records[i];
        const auto &expected = expected_records[i];
        if (actual.kind != expected.kind || actual.target_kind != expected.target_kind ||
            actual.target_index != expected.target_index || actual.flags != expected.flags ||
            !(actual.value == expected.value)) {
            error = "hash table record does not match artifact";
            return false;
        }
    }
    if (!(artifact_hash == abi_hash(module))) {
        error = "artifact ABIHash does not match canonical content";
        return false;
    }
    return true;
}

}  // namespace amc