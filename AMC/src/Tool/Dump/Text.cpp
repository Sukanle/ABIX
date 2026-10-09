#include "AMC/Core/Core.h"
#include "AMC/Tool/Dump.h"

#include "AMC/Util/Hex.h"
#include "AMC/Util/Json.h"
#include "AMC/Util/Names.h"

#include <fmt/format.h>

#include <string>
#include <vector>

namespace amc::dump {

using namespace amc::util;

void write_text(const amc::AbiModule &module, std::string &output) {
    output +=
        "artifact: format_version=4 hash_algorithm=0 sections="
        "strings,identity,target,types,fields,functions,parameters,symbols,hash_descriptor,hash_table,"
        "compatibility,maps,map_operations\n";
    output += "package: ";
    output += module.package_name;
    if (!module.package_version.empty()) {
        output += " (";
        output += module.package_version;
        output += ")";
    }
    output += "\ntarget: arch=";
    output += fmt::to_string(module.arch);
    output += " os=";
    output += fmt::to_string(module.os);
    output += " abi=";
    output += fmt::to_string(module.target_abi);
    output += " compiler=";
    output += fmt::to_string(module.compiler);
    output += " calling_convention=";
    output += fmt::to_string(module.calling_convention);
    output += "\n";
    output += "hash: algorithm=0 canonical_version=1 abi=";
    output += hash_hex(amc::abi_hash(module));
    output += " records=";
    output += fmt::to_string(amc::hash_table(module).size());
    output += "\n";
    output += "runtime_descriptor: type_id_bits=128 registry=runtime::Registry types=";
    output += fmt::to_string(module.types.size());
    output += " functions=";
    output += fmt::to_string(module.functions.size());
    output += " symbols=";
    output += fmt::to_string(module.symbols.size());
    output += "\n";
    output += "types (";
    output += fmt::to_string(module.types.size());
    output += "):\n";
    for (size_t i = 0; i < module.types.size(); ++i) {
        const auto &type = module.types[i];
        output += "  [";
        output += fmt::to_string(i);
        output += "] ";
        output += type.name;
        output += " kind=";
        output += amc::type_kind_name(type.kind);
        output += " id=";
        output += hash_hex(type.id);
        output += " size=";
        output += fmt::to_string(type.size);
        output += " align=";
        output += fmt::to_string(type.align);
        output += " layout_hash=";
        output += hash_hex(type.layout_hash);
        output += " flags=";
        output += fmt::to_string(type.flags);
        output += " fields=";
        output += fmt::to_string(type.field_begin);
        output += "+";
        output += fmt::to_string(type.field_count);
        if (type.array_count) {
            output += " array_count=";
            output += fmt::to_string(type.array_count);
        }
        if (type.kind == amc::TypeKind::primitive) {
            const uint32_t width = (type.primitive_abi & amc::primitive_width_mask) >> amc::primitive_width_shift;
            output += " primitive_abi={kind=";
            output += primitive_kind_name(type.primitive_abi);
            output += " width=";
            output += fmt::to_string(width);
            output += " signed=";
            output += ((type.primitive_abi & amc::primitive_signed_bit) != 0 ? "true" : "false");
            output += " float_format=";
            output += float_format_name(type.primitive_abi);
            output += "}";
        }
        output += '\n';
    }
    output += "fields (";
    output += fmt::to_string(module.fields.size());
    output += "):\n";
    for (size_t i = 0; i < module.fields.size(); ++i) {
        const auto &field = module.fields[i];
        output += "  [";
        output += fmt::to_string(i);
        output += "] ";
        output += field.name;
        output += " type=";
        output += hash_hex(field.type_id);
        output += " owner=";
        output += hash_hex(field.owner_type);
        output += " offset=";
        output += fmt::to_string(field.offset);
        output += " flags=";
        output += fmt::to_string(field.flags);
        output += '\n';
    }
    output += "functions (";
    output += fmt::to_string(module.functions.size());
    output += "):\n";
    for (size_t i = 0; i < module.functions.size(); ++i) {
        const auto &function = module.functions[i];
        output += "  [";
        output += fmt::to_string(i);
        output += "] ";
        output += function.name;
        output += " signature=";
        output += hash_hex(function.signature);
        output += " owner=";
        output += hash_hex(function.owner_type);
        output += " returns=";
        output += hash_hex(function.return_type);
        output += " calling_convention=";
        output += fmt::to_string(function.calling_convention);
        output += " flags=";
        output += fmt::to_string(function.flags);
        output += '\n';
        for (size_t p = 0; p < function.parameters.size(); ++p) {
            const auto &parameter = function.parameters[p];
            output += "    (";
            output += fmt::to_string(p);
            output += ") ";
            output += parameter.name;
            output += " type=";
            output += hash_hex(parameter.type_id);
            output += " flags=";
            output += fmt::to_string(parameter.flags);
            output += '\n';
        }
    }
    output += "symbols (";
    output += fmt::to_string(module.symbols.size());
    output += "):\n";
    for (size_t i = 0; i < module.symbols.size(); ++i) {
        const auto &symbol = module.symbols[i];
        output += "  [";
        output += fmt::to_string(i);
        output += "] ";
        output += symbol.name;
        output += " kind=";
        output += (symbol.kind == amc::SymbolKind::type    ? "type"
                   : symbol.kind == amc::SymbolKind::field ? "field"
                                                           : "function");
        output += " target_index=";
        output += fmt::to_string(symbol.target_index);
        output += '\n';
    }
    output += "compatibility (";
    output += fmt::to_string(module.compatibility.size());
    output += "):\n";
    for (const auto &record : module.compatibility) {
        output += "  ";
        output += amc::compatibility_name(record.kind);
        output += " ";
        output += hash_hex(record.source_type);
        output += " -> ";
        output += hash_hex(record.target_type);
        output += " map=";
        output += fmt::to_string(record.map_index);
        output += " flags=";
        output += fmt::to_string(record.flags);
        output += '\n';
    }
    output += "maps (";
    output += fmt::to_string(module.maps.size());
    output += "):\n";
    for (const auto &map : module.maps) {
        output += "  ";
        output += map.source_name;
        output += " (";
        output += hash_hex(map.source_type);
        output += ") -> ";
        output += map.target_name;
        output += " (";
        output += hash_hex(map.target_type);
        output += ") begin=";
        output += fmt::to_string(map.operation_begin);
        output += " operations=";
        output += fmt::to_string(map.operations.size());
        output += " flags=";
        output += fmt::to_string(map.flags);
        output += '\n';
        for (size_t i = 0; i < map.operations.size(); ++i) {
            const auto &operation = map.operations[i];
            output += "    (";
            output += fmt::to_string(i);
            output += ") ";
            output += map_opcode_name(operation.opcode);
            output += " source_field=";
            output += fmt::to_string(operation.source_field);
            output += " target_field=";
            output += fmt::to_string(operation.target_field);
            output += " source_offset=";
            output += fmt::to_string(operation.source_offset);
            output += " target_offset=";
            output += fmt::to_string(operation.target_offset);
            output += " bytes=";
            output += fmt::to_string(operation.byte_count);
            output += " auxiliary=";
            output += hash_hex(operation.auxiliary);
            output += '\n';
        }
    }
}

void write_diff_text(
    const amc::AbiModule &source, const amc::AbiModule &target, const amc::AbiModule &report, std::string &output) {
    output += "diff:\n  source: ";
    output += source.package_name;
    output += " abi=";
    output += hash_hex(amc::abi_hash(source));
    output += "\n  target: ";
    output += target.package_name;
    output += " abi=";
    output += hash_hex(amc::abi_hash(target));
    output += '\n';
    write_text(report, output);
}

void write_diff_json(
    const amc::AbiModule &source, const amc::AbiModule &target, const amc::AbiModule &report, std::string &output) {
    output += "{\n  \"source\": {\"package\": ";
    json_string(output, source.package_name);
    output += ", \"abi_hash\": ";
    json_string(output, hash_hex(amc::abi_hash(source)));
    output += "},\n  \"target\": {\"package\": ";
    json_string(output, target.package_name);
    output += ", \"abi_hash\": ";
    json_string(output, hash_hex(amc::abi_hash(target)));
    output += "},\n  \"report\": ";
    write_json(report, output);
    output += "}\n";
}

}   // namespace amc::dump
