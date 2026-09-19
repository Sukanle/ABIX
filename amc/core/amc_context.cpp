#include "amc_context.h"
#include "amc_error.h"

#include <iomanip>
#include <map>
#include <sstream>
#include <utility>

namespace amc {
namespace {

using TypeKey = std::pair<uint64_t, uint64_t>;
using TypeIndex = std::map<TypeKey, size_t>;

std::string hash_string(Hash128 value) {
    std::ostringstream output;
    output << "0x" << std::hex << std::setfill('0') << std::setw(16) << value.hi << std::setw(16) << value.lo;
    return output.str();
}

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
    return "T" + std::to_string(it->second);
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
    std::ostringstream out;

    out << "# ABIX ABI context\n";
    out << "package: " << (module.package_name.empty() ? "?" : module.package_name) << "\n";
    if (!module.package_version.empty()) out << "package_version: " << module.package_version << "\n";
    out
        << "target: arch="
        << module.arch
        << " os="
        << module.os
        << " abi="
        << module.target_abi
        << " compiler="
        << module.compiler
        << " calling_convention="
        << module.calling_convention
        << "\n";
    out << "abi_hash: " << hash_string(abi_hash(module)) << "\n";
    out
        << "counts: types="
        << module.types.size()
        << " fields="
        << module.fields.size()
        << " functions="
        << module.functions.size()
        << " symbols="
        << module.symbols.size()
        << "\n";
    out
        << "legend: Tn=type Fn=field Pn=function Sn=symbol; cross references use indices"
        << (include_names ? "\n" : "; names omitted\n");

    out << "\ntypes:\n";
    for (size_t i = 0; i < module.types.size(); ++i) {
        const auto &type = module.types[i];
        out << "  T" << i;
        if (include_names && !type.name.empty()) out << " " << type.name;
        out
            << " kind="
            << type_kind_name(type.kind)
            << " size="
            << type.size
            << " align="
            << type.align
            << " id="
            << hash_string(type.id)
            << " layout="
            << hash_string(type.layout_hash)
            << " flags="
            << type.flags;
        if (type.field_count != 0)
            out << " fields=F" << type.field_begin << "..F" << (type.field_begin + type.field_count - 1);
        else
            out << " fields=none";
        if (type.array_count) out << " array_count=" << type.array_count;
        if (type.flags & type_template_primary) out << " template_primary";
        if (include_names)
            if (const auto *source = find_source_origin(module, type.id))
                out << " source=" << source->file << ":" << source->line << ":" << source->column;
        out << "\n";
    }

    out << "\nfields:\n";
    for (size_t i = 0; i < module.fields.size(); ++i) {
        const auto &field = module.fields[i];
        out << "  F" << i;
        if (include_names && !field.name.empty()) out << " " << field.name;
        out
            << " owner="
            << type_ref(field.owner_type, index)
            << " type="
            << type_ref(field.type_id, index)
            << " offset="
            << field.offset
            << " flags="
            << field.flags
            << "\n";
    }

    out << "\nfunctions:\n";
    for (size_t i = 0; i < module.functions.size(); ++i) {
        const auto &function = module.functions[i];
        out << "  P" << i;
        if (include_names && !function.name.empty()) out << " " << function.name;
        out
            << " owner="
            << type_ref(function.owner_type, index)
            << " signature="
            << hash_string(function.signature)
            << " returns="
            << type_ref(function.return_type, index)
            << " cc="
            << function.calling_convention
            << " flags="
            << function.flags
            << " params="
            << function.parameters.size()
            << "\n";
        for (size_t p = 0; p < function.parameters.size(); ++p) {
            const auto &parameter = function.parameters[p];
            out << "    P" << i << "." << p;
            if (include_names && !parameter.name.empty()) out << " " << parameter.name;
            out << " type=" << type_ref(parameter.type_id, index) << " flags=" << parameter.flags << "\n";
        }
    }

    out << "\nsymbols:\n";
    for (size_t i = 0; i < module.symbols.size(); ++i) {
        const auto &symbol = module.symbols[i];
        out << "  S" << i;
        if (include_names && !symbol.name.empty()) out << " " << symbol.name;
        out << " kind=" << symbol_kind_name(symbol.kind) << " target=" << symbol.target_index << "\n";
    }

    return out.str();
}

std::string context_to_json(const AbiModule &module, bool include_names) {
    const TypeIndex index = build_type_index(module);
    std::ostringstream out;
    const auto string = [](std::ostream &o, const std::string &value) { o << '"' << json_escape(value) << '"'; };

    out << "{\"schema\":\"abix.context/1\"";
    out << ",\"package\":{\"name\":";
    string(out, module.package_name);
    out << ",\"version\":";
    string(out, module.package_version);
    out
        << "},\"target\":{\"arch\":"
        << module.arch
        << ",\"os\":"
        << module.os
        << ",\"abi\":"
        << module.target_abi
        << ",\"compiler\":"
        << module.compiler
        << ",\"calling_convention\":"
        << module.calling_convention
        << "}";
    out << ",\"abi_hash\":\"" << hash_string(abi_hash(module)) << '"';
    out
        << ",\"counts\":{\"types\":"
        << module.types.size()
        << ",\"fields\":"
        << module.fields.size()
        << ",\"functions\":"
        << module.functions.size()
        << ",\"symbols\":"
        << module.symbols.size()
        << "}";

    out << ",\"types\":[";
    for (size_t i = 0; i < module.types.size(); ++i) {
        const auto &type = module.types[i];
        if (i) out << ",";
        out << "{\"index\":" << i;
        if (include_names) {
            out << ",\"name\":";
            string(out, type.name);
        }
        out
            << ",\"kind\":\""
            << type_kind_name(type.kind)
            << "\""
            << ",\"id\":\""
            << hash_string(type.id)
            << '"'
            << ",\"layout_hash\":\""
            << hash_string(type.layout_hash)
            << '"'
            << ",\"size\":"
            << type.size
            << ",\"align\":"
            << type.align
            << ",\"flags\":"
            << type.flags
            << ",\"field_begin\":"
            << type.field_begin
            << ",\"field_count\":"
            << type.field_count
            << ",\"array_count\":"
            << type.array_count
            << "}";
    }
    out << "]";

    out << ",\"fields\":[";
    for (size_t i = 0; i < module.fields.size(); ++i) {
        const auto &field = module.fields[i];
        if (i) out << ",";
        out << "{\"index\":" << i;
        if (include_names) {
            out << ",\"name\":";
            string(out, field.name);
        }
        out
            << ",\"owner\":"
            << (field.owner_type == Hash128{} ? "null" : "\"" + hash_string(field.owner_type) + "\"")
            << ",\"type\":\""
            << hash_string(field.type_id)
            << '"'
            << ",\"offset\":"
            << field.offset
            << ",\"flags\":"
            << field.flags
            << "}";
    }
    out << "]";

    out << ",\"functions\":[";
    for (size_t i = 0; i < module.functions.size(); ++i) {
        const auto &function = module.functions[i];
        if (i) out << ",";
        out << "{\"index\":" << i;
        if (include_names) {
            out << ",\"name\":";
            string(out, function.name);
        }
        out
            << ",\"owner\":"
            << (function.owner_type == Hash128{} ? "null" : "\"" + hash_string(function.owner_type) + "\"")
            << ",\"signature\":\""
            << hash_string(function.signature)
            << '"'
            << ",\"return_type\":\""
            << hash_string(function.return_type)
            << '"'
            << ",\"calling_convention\":"
            << function.calling_convention
            << ",\"flags\":"
            << function.flags
            << ",\"parameters\":[";
        for (size_t p = 0; p < function.parameters.size(); ++p) {
            const auto &parameter = function.parameters[p];
            if (p) out << ",";
            out << "{\"index\":" << p;
            if (include_names) {
                out << ",\"name\":";
                string(out, parameter.name);
            }
            out << ",\"type\":\"" << hash_string(parameter.type_id) << '"' << ",\"flags\":" << parameter.flags << "}";
        }
        out << "]}";
    }
    out << "]";

    out << ",\"symbols\":[";
    for (size_t i = 0; i < module.symbols.size(); ++i) {
        const auto &symbol = module.symbols[i];
        if (i) out << ",";
        out << "{\"index\":" << i;
        if (include_names) {
            out << ",\"name\":";
            string(out, symbol.name);
        }
        out << ",\"kind\":\"" << symbol_kind_name(symbol.kind) << '"' << ",\"target\":" << symbol.target_index << "}";
    }
    out << "]";

    out << "}\n";
    (void)index;
    return out.str();
}

}   // namespace amc
