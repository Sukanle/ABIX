#include "AMC/Core/Query.h"
#include "AMC/Core/Error.h"

#include <algorithm>
#include <cctype>
#include <map>

#include <fmt/format.h>

namespace amc {
namespace {

using TypeKey = std::pair<uint64_t, uint64_t>;

std::string hash_string(Hash128 value) { return fmt::format("0X{:016x}{:016x}", value.hi, value.lo); }

bool is_hex_digit(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

// Accepts "0x<hi><lo>" or "<hi><lo>" (the canonical AMC rendering).
bool parse_type_id(std::string_view text, Hash128 &value) {
    std::string hex(text);
    if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0) hex = hex.substr(2);
    if (hex.size() != 32) return false;
    for (char c : hex)
        if (!is_hex_digit(c)) return false;
    value.hi = std::stoull(hex.substr(0, 16), nullptr, 16);
    value.lo = std::stoull(hex.substr(16, 16), nullptr, 16);
    return true;
}

std::map<TypeKey, const Type *> index_types(const AbiModule &module) {
    std::map<TypeKey, const Type *> index;
    for (const auto &type : module.types)
        index[{type.id.lo, type.id.hi}] = &type;
    return index;
}

std::string type_name(const std::map<TypeKey, const Type *> &index, Hash128 id) {
    const auto it = index.find({id.lo, id.hi});
    return it == index.end() ? hash_string(id) : it->second->name;
}

}   // namespace

const Type *find_type_by_id(const AbiModule &module, Hash128 id) {
    for (const auto &type : module.types)
        if (type.id == id) return &type;
    return nullptr;
}

std::vector<size_t> query_type_indices(const AbiModule &module, std::string_view needle) {
    std::vector<size_t> exact;
    Hash128 id{};
    const bool has_id = parse_type_id(needle, id);
    for (size_t i = 0; i < module.types.size(); ++i) {
        const auto &type = module.types[i];
        if (has_id && type.id == id)
            exact.push_back(i);
        else if (!has_id && type.name == needle)
            exact.push_back(i);
    }
    if (!exact.empty() || has_id) return exact;

    std::vector<size_t> partial;
    for (size_t i = 0; i < module.types.size(); ++i)
        if (module.types[i].name.find(needle) != std::string::npos) partial.push_back(i);
    if (!partial.empty()) return partial;

    // Partial TypeID fallback: "0x" + 1..31 hex digits, matched as a prefix of
    // the canonical 32-char lowercase hex body rendered by hash_string().
    std::string prefix(needle);
    if (prefix.rfind("0x", 0) == 0 || prefix.rfind("0X", 0) == 0) prefix = prefix.substr(2);
    if (!prefix.empty() && prefix.size() < 32) {
        bool all_hex = true;
        for (char c : prefix)
            if (!is_hex_digit(c)) {
                all_hex = false;
                break;
            }
        if (all_hex) {
            std::transform(prefix.begin(), prefix.end(), prefix.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::vector<size_t> prefix_matches;
            for (size_t i = 0; i < module.types.size(); ++i) {
                const std::string body = hash_string(module.types[i].id).substr(2);
                if (body.compare(0, prefix.size(), prefix) == 0) prefix_matches.push_back(i);
            }
            return prefix_matches;
        }
    }
    return partial;
}

std::vector<size_t> query_function_indices(const AbiModule &module, std::string_view needle) {
    const auto index = index_types(module);
    std::vector<size_t> exact;
    for (size_t i = 0; i < module.functions.size(); ++i) {
        const auto &function = module.functions[i];
        if (function.name == needle) {
            exact.push_back(i);
            continue;
        }
        // "Owner::name", where Owner is the declaring type registered in this
        // module (AMC stores the bare member name plus owner_type).
        if (!(function.owner_type == Hash128{})) {
            const auto owner = index.find({function.owner_type.lo, function.owner_type.hi});
            if (owner != index.end() && owner->second->name + "::" + function.name == needle) {
                exact.push_back(i);
                continue;
            }
        }
        // The artifact may already spell the function fully qualified.
        const bool suffix = function.name.size() >= needle.size() + 2
                         && function.name.compare(
                                function.name.size() - needle.size() - 2, needle.size() + 2, "::" + std::string(needle))
                                == 0;
        if (suffix) exact.push_back(i);
    }
    if (!exact.empty()) return exact;

    std::vector<size_t> partial;
    for (size_t i = 0; i < module.functions.size(); ++i)
        if (module.functions[i].name.find(needle) != std::string::npos) partial.push_back(i);
    return partial;
}

std::string query_types_text(const AbiModule &module, const std::vector<size_t> &indices, bool include_layout) {
    const auto index = index_types(module);
    std::string out = fmt::format("query: types matches={}\n", indices.size());
    for (const auto i : indices) {
        const auto &type = module.types[i];
        out += fmt::format("  [{}] {} kind={} size={} align={} id={} layout={} flags={} fields={}", i, type.name,
            type_kind_name(type.kind), type.size, type.align, hash_string(type.id), hash_string(type.layout_hash),
            type.flags, type.field_count);
        if (const auto *source = find_source_origin(module, type.id))
            out += fmt::format(" source={}:{}:{}", source->file, source->line, source->column);
        out += "\n";
        if (!include_layout) continue;
        for (uint32_t f = 0; f < type.field_count; ++f) {
            const auto &field = module.fields[type.field_begin + f];
            out += fmt::format("      field {} type={} offset={} flags={}\n", field.name,
                type_name(index, field.type_id), field.offset, field.flags);
        }
    }
    return out;
}

std::string query_types_json(
    const AbiModule &module, const std::vector<size_t> &indices, bool include_layout, std::string_view needle) {
    const auto index = index_types(module);
    std::string out = fmt::format(
        "{{\"schema\":\"abix.query/1\",\"query\":{{\"kind\":\"type\",\"needle\":\"{}\"}},\"match_count\":{},"
        "\"matches\":[",
        json_escape(needle), indices.size());
    for (size_t m = 0; m < indices.size(); ++m) {
        const auto i = indices[m];
        const auto &type = module.types[i];
        if (m) out += ",";
        out += fmt::format(
            "{{\"index\":{},\"name\":\"{}\",\"kind\":\"{}\",\"id\":\"{}\",\"layout_hash\":\"{}\",\"size\":{},"
            "\"align\":{},\"flags\":{},\"field_count\":{}",
            i, json_escape(type.name), type_kind_name(type.kind), hash_string(type.id), hash_string(type.layout_hash),
            type.size, type.align, type.flags, type.field_count);
        if (const auto *source = find_source_origin(module, type.id))
            out += fmt::format(",\"source\":{{\"file\":\"{}\",\"line\":{},\"column\":{}}}", json_escape(source->file),
                source->line, source->column);
        if (include_layout) {
            out += ",\"fields\":[";
            for (uint32_t f = 0; f < type.field_count; ++f) {
                const auto &field = module.fields[type.field_begin + f];
                if (f) out += ",";
                out += fmt::format(
                    "{{\"index\":{},\"name\":\"{}\",\"type_id\":\"{}\",\"type_name\":\"{}\",\"offset\":{},"
                    "\"flags\":{}}}",
                    type.field_begin + f, json_escape(field.name), hash_string(field.type_id),
                    json_escape(type_name(index, field.type_id)), field.offset, field.flags);
            }
            out += "]";
        }
        out += "}";
    }
    out += "]}\n";
    return out;
}

std::string query_functions_text(const AbiModule &module, const std::vector<size_t> &indices) {
    const auto index = index_types(module);
    std::string out = fmt::format("query: functions matches={}\n", indices.size());
    for (const auto i : indices) {
        const auto &function = module.functions[i];
        out += fmt::format("  [{}] {} owner={} signature={} returns={} calling_convention={} flags={} parameters={}\n",
            i, function.name, function.owner_type == Hash128{} ? "-" : type_name(index, function.owner_type),
            hash_string(function.signature), type_name(index, function.return_type), function.calling_convention,
            function.flags, function.parameters.size());
        for (size_t p = 0; p < function.parameters.size(); ++p) {
            const auto &parameter = function.parameters[p];
            out += fmt::format("      param {} {} type={} flags={}\n", p, parameter.name,
                type_name(index, parameter.type_id), parameter.flags);
        }
    }
    return out;
}

std::string query_functions_json(const AbiModule &module, const std::vector<size_t> &indices, std::string_view needle) {
    const auto index = index_types(module);
    std::string out = fmt::format(
        "{{\"schema\":\"abix.query/1\",\"query\":{{\"kind\":\"function\",\"needle\":\"{}\"}},\"match_count\":{},"
        "\"matches\":[",
        json_escape(needle), indices.size());
    for (size_t m = 0; m < indices.size(); ++m) {
        const auto i = indices[m];
        const auto &function = module.functions[i];
        if (m) out += ",";
        out += fmt::format(
            "{{\"index\":{},\"name\":\"{}\",\"owner_type\":\"{}\",\"owner_name\":\"{}\",\"signature\":\"{}\","
            "\"return_type\":\"{}\",\"return_type_name\":\"{}\",\"calling_convention\":{},\"flags\":{},"
            "\"parameters\":[",
            i, json_escape(function.name), hash_string(function.owner_type),
            json_escape(function.owner_type == Hash128{} ? "" : type_name(index, function.owner_type)),
            hash_string(function.signature), hash_string(function.return_type),
            json_escape(type_name(index, function.return_type)), function.calling_convention, function.flags);
        for (size_t p = 0; p < function.parameters.size(); ++p) {
            const auto &parameter = function.parameters[p];
            if (p) out += ",";
            out += fmt::format("{{\"index\":{},\"name\":\"{}\",\"type_id\":\"{}\",\"type_name\":\"{}\",\"flags\":{}}}",
                p, json_escape(parameter.name), hash_string(parameter.type_id),
                json_escape(type_name(index, parameter.type_id)), parameter.flags);
        }
        out += "]}";
    }
    out += "]}\n";
    return out;
}

std::string compatibility_query_text(const AbiModule &source, const AbiModule &target, const VerifyResult &result) {
    std::string out = fmt::format("query: compatibility\n  source: {}\n  target: {}\n  compatible={}\n",
        source.package_name, target.package_name, result.consistent ? "true" : "false");
    for (const auto &change : result.changes) {
        out += fmt::format("  - {}", change_kind_name(change.kind));
        if (!change.type.empty()) out += fmt::format(" type={}", change.type);
        if (!change.name.empty()) out += fmt::format(" name={}", change.name);
        if (!change.detail.empty()) out += fmt::format(" ({})", change.detail);
        out += "\n";
    }
    return out;
}

std::string compatibility_query_json(const AbiModule &source, const AbiModule &target, const VerifyResult &result) {
    std::string out = fmt::format(
        "{{\"schema\":\"abix.query/1\",\"query\":{{\"kind\":\"compatibility\"}},\"source\":{{\"package\":\"{}\"}},"
        "\"target\":{{\"package\":\"{}\"}},\"compatible\":{},\"status\":\"{}\",\"changes\":[",
        json_escape(source.package_name), json_escape(target.package_name), result.consistent ? "true" : "false",
        result.consistent ? "consistent" : "drift");
    for (size_t i = 0; i < result.changes.size(); ++i) {
        const auto &change = result.changes[i];
        if (i) out += ",";
        out += fmt::format("{{\"kind\":\"{}\",\"type\":\"{}\",\"name\":\"{}\",\"detail\":\"{}\"}}",
            change_kind_name(change.kind), json_escape(change.type), json_escape(change.name),
            json_escape(change.detail));
    }
    out += "]}\n";
    return out;
}

}   // namespace amc
