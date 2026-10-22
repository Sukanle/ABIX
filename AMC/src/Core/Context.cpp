#include "AMC/Core/Context.h"
#include "AMC/Core/Error.h"

#include <map>
#include <utility>

#include <fmt/format.h>

namespace amc {
namespace {

using TypeKey = std::pair<uint64_t, uint64_t>;
using TypeIndex = std::map<TypeKey, size_t>;

std::string hash_string(Hash128 value) { return fmt::format("0X{:016x}{:016x}", value.hi, value.lo); }

TypeIndex build_type_index(const AbiModule &module) {
    TypeIndex index;
    for (size_t i = 0; i < module.types.size(); ++i)
        index[{module.types[i].id.lo, module.types[i].id.hi}] = i;
    return index;
}

// Renders "T3" for a resolvable type id, or "T?<hash>" when the id is not
// part of this module (e.g. a primitive declared by the toolchain).
std::string type_ref(Hash128 id, const TypeIndex &index) {
    if (id == Hash128{}) return "T?";
    const auto it = index.find({id.lo, id.hi});
    if (it == index.end()) return "T?" + hash_string(id);
    return fmt::format("T{}", it->second);
}

const char *symbol_kind_name(SymbolKind kind) {
    switch (kind) {
        case SymbolKind::type:     return "type";
        case SymbolKind::field:    return "field";
        case SymbolKind::function: return "function";
    }
    return "unknown";
}

}   // namespace

std::string context_to_llm(const AbiModule &module, bool include_names) {
    const TypeIndex index = build_type_index(module);
    std::string out;

    out += "# ABIX ABI context\n";
    out += fmt::format("package: {}\n", module.package_name.empty() ? "?" : module.package_name);
    if (!module.package_version.empty()) out += fmt::format("package_version: {}\n", module.package_version);
    out += fmt::format("target: arch={} os={} abi={} compiler={} calling_convention={}\n", module.arch, module.os,
        module.target_abi, module.compiler, module.calling_convention);
    out += fmt::format("abi_hash: {}\n", hash_string(abi_hash(module)));
    out += fmt::format("counts: types={} fields={} functions={} symbols={}\n", module.types.size(),
        module.fields.size(), module.functions.size(), module.symbols.size());
    out += fmt::format("legend: Tn=type Fn=field Pn=function Sn=symbol; cross references use indices{}",
        include_names ? "\n" : "; names omitted\n");

    out += "\ntypes:\n";
    for (size_t i = 0; i < module.types.size(); ++i) {
        const auto &type = module.types[i];
        out += fmt::format("  T{}", i);
        if (include_names && !type.name.empty()) out += fmt::format(" {}", type.name);
        out += fmt::format(" kind={} size={} align={} id={} layout={} flags={}", type_kind_name(type.kind), type.size,
            type.align, hash_string(type.id), hash_string(type.layout_hash), type.flags);
        if (type.field_count != 0)
            out += fmt::format(" fields=F{}..F{}", type.field_begin, type.field_begin + type.field_count - 1);
        else
            out += " fields=none";
        if (type.array_count) out += fmt::format(" array_count={}", type.array_count);
        if (type.flags & type_template_primary) out += " template_primary";
        if (include_names)
            if (const auto *source = find_source_origin(module, type.id))
                out += fmt::format(" source={}:{}:{}", source->file, source->line, source->column);
        out += "\n";
    }

    out += "\nfields:\n";
    for (size_t i = 0; i < module.fields.size(); ++i) {
        const auto &field = module.fields[i];
        out += fmt::format("  F{}", i);
        if (include_names && !field.name.empty()) out += fmt::format(" {}", field.name);
        out += fmt::format(" owner={} type={} offset={} flags={}\n", type_ref(field.owner_type, index),
            type_ref(field.type_id, index), field.offset, field.flags);
    }

    out += "\nfunctions:\n";
    for (size_t i = 0; i < module.functions.size(); ++i) {
        const auto &function = module.functions[i];
        out += fmt::format("  P{}", i);
        if (include_names && !function.name.empty()) out += fmt::format(" {}", function.name);
        out += fmt::format(" owner={} signature={} returns={} cc={} flags={} params={}\n",
            type_ref(function.owner_type, index), hash_string(function.signature),
            type_ref(function.return_type, index), function.calling_convention, function.flags,
            function.parameters.size());
        for (size_t p = 0; p < function.parameters.size(); ++p) {
            const auto &parameter = function.parameters[p];
            out += fmt::format("    P{}.{}", i, p);
            if (include_names && !parameter.name.empty()) out += fmt::format(" {}", parameter.name);
            out += fmt::format(" type={} flags={}\n", type_ref(parameter.type_id, index), parameter.flags);
        }
    }

    out += "\nsymbols:\n";
    for (size_t i = 0; i < module.symbols.size(); ++i) {
        const auto &symbol = module.symbols[i];
        out += fmt::format("  S{}", i);
        if (include_names && !symbol.name.empty()) out += fmt::format(" {}", symbol.name);
        out += fmt::format(" kind={} target={}\n", symbol_kind_name(symbol.kind), symbol.target_index);
    }

    return out;
}

std::string context_to_json(const AbiModule &module, bool include_names) {
    const TypeIndex index = build_type_index(module);
    std::string out;
    const auto string = [](const std::string &value) { return fmt::format("\"{}\"", json_escape(value)); };

    out += "{\"schema\":\"abix.context/1\"";
    out += ",\"package\":{\"name\":";
    out += string(module.package_name);
    out += ",\"version\":";
    out += string(module.package_version);
    out += fmt::format("}},\"target\":{{\"arch\":{},\"os\":{},\"abi\":{},\"compiler\":{},\"calling_convention\":{}}}",
        module.arch, module.os, module.target_abi, module.compiler, module.calling_convention);
    out += fmt::format(",\"abi_hash\":\"{}\"", hash_string(abi_hash(module)));
    out += fmt::format(",\"counts\":{{\"types\":{},\"fields\":{},\"functions\":{},\"symbols\":{}}}",
        module.types.size(), module.fields.size(), module.functions.size(), module.symbols.size());

    out += ",\"types\":[";
    for (size_t i = 0; i < module.types.size(); ++i) {
        const auto &type = module.types[i];
        if (i) out += ",";
        out += fmt::format("{{\"index\":{}", i);
        if (include_names) {
            out += ",\"name\":";
            out += string(type.name);
        }
        out += fmt::format(
            ",\"kind\":\"{}\",\"id\":\"{}\",\"layout_hash\":\"{}\",\"size\":{},\"align\":{},\"flags\":{},"
            "\"field_begin\":{},\"field_count\":{},\"array_count\":{}}}",
            type_kind_name(type.kind), hash_string(type.id), hash_string(type.layout_hash), type.size, type.align,
            type.flags, type.field_begin, type.field_count, type.array_count);
    }
    out += "]";

    out += ",\"fields\":[";
    for (size_t i = 0; i < module.fields.size(); ++i) {
        const auto &field = module.fields[i];
        if (i) out += ",";
        out += fmt::format("{{\"index\":{}", i);
        if (include_names) {
            out += ",\"name\":";
            out += string(field.name);
        }
        out += fmt::format(",\"owner\":{},\"type\":\"{}\",\"offset\":{},\"flags\":{}}}",
            field.owner_type == Hash128{} ? "null" : "\"" + hash_string(field.owner_type) + "\"",
            hash_string(field.type_id), field.offset, field.flags);
    }
    out += "]";

    out += ",\"functions\":[";
    for (size_t i = 0; i < module.functions.size(); ++i) {
        const auto &function = module.functions[i];
        if (i) out += ",";
        out += fmt::format("{{\"index\":{}", i);
        if (include_names) {
            out += ",\"name\":";
            out += string(function.name);
        }
        out += fmt::format(
            ",\"owner\":{},\"signature\":\"{}\",\"return_type\":\"{}\",\"calling_convention\":{},\"flags\":{},"
            "\"parameters\":[",
            function.owner_type == Hash128{} ? "null" : "\"" + hash_string(function.owner_type) + "\"",
            hash_string(function.signature), hash_string(function.return_type), function.calling_convention,
            function.flags);
        for (size_t p = 0; p < function.parameters.size(); ++p) {
            const auto &parameter = function.parameters[p];
            if (p) out += ",";
            out += fmt::format("{{\"index\":{}", p);
            if (include_names) {
                out += ",\"name\":";
                out += string(parameter.name);
            }
            out += fmt::format(",\"type\":\"{}\",\"flags\":{}}}", hash_string(parameter.type_id), parameter.flags);
        }
        out += "]}";
    }
    out += "]";

    out += ",\"symbols\":[";
    for (size_t i = 0; i < module.symbols.size(); ++i) {
        const auto &symbol = module.symbols[i];
        if (i) out += ",";
        out += fmt::format("{{\"index\":{}", i);
        if (include_names) {
            out += ",\"name\":";
            out += string(symbol.name);
        }
        out += fmt::format(",\"kind\":\"{}\",\"target\":{}}}", symbol_kind_name(symbol.kind), symbol.target_index);
    }
    out += "]";

    out += "}\n";
    (void)index;
    return out;
}

}   // namespace amc
