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

void write_json_primitive_abi(const amc::Type &type, std::string &output) {
    const uint32_t width = (type.primitive_abi & amc::primitive_width_mask) >> amc::primitive_width_shift;
    output += "{\"kind\": ";
    json_string(output, primitive_kind_name(type.primitive_abi));
    output += ", \"width\": ";
    output += fmt::to_string(width);
    output += ", \"signed\": ";
    output += ((type.primitive_abi & amc::primitive_signed_bit) != 0 ? "true" : "false");
    output += ", \"float_format\": ";
    json_string(output, float_format_name(type.primitive_abi));
    output += "}";
}

void write_json_operations(const std::vector<amc::MapOperation> &operations, std::string &output) {
    output += "[";
    for (size_t i = 0; i < operations.size(); ++i) {
        if (i) output += ", ";
        const auto &operation = operations[i];
        output += "{\"opcode\": ";
        json_string(output, map_opcode_name(operation.opcode));
        output += ", \"source_field\": ";
        output += fmt::to_string(operation.source_field);
        output += ", \"target_field\": ";
        output += fmt::to_string(operation.target_field);
        output += ", \"source_offset\": ";
        output += fmt::to_string(operation.source_offset);
        output += ", \"target_offset\": ";
        output += fmt::to_string(operation.target_offset);
        output += ", \"byte_count\": ";
        output += fmt::to_string(operation.byte_count);
        output += ", \"auxiliary\": ";
        json_string(output, hash_hex(operation.auxiliary));
        output += "}";
    }
    output += "]";
}

void write_json(const amc::AbiModule &module, std::string &output) {
    output += "{\n";
    indent(output, 1);
    output +=
        "\"artifact\": {\"format_version\": 4, \"hash_algorithm\": 0, "
        "\"sections\": [\"strings\", \"identity\", \"target\", \"types\", \"fields\", "
        "\"functions\", \"parameters\", \"symbols\", \"hash_descriptor\", "
        "\"hash_table\", \"compatibility\", \"maps\", \"map_operations\"]},\n";
    indent(output, 1);
    output += "\"package\": {\"name\": ";
    json_string(output, module.package_name);
    output += ", \"version\": ";
    json_string(output, module.package_version);
    output += "},\n";
    indent(output, 1);
    output += "\"target\": {\"arch\": ";
    output += fmt::to_string(module.arch);
    output += ", \"os\": ";
    output += fmt::to_string(module.os);
    output += ", \"abi\": ";
    output += fmt::to_string(module.target_abi);
    output += ", \"compiler\": ";
    output += fmt::to_string(module.compiler);
    output += ", \"calling_convention\": ";
    output += fmt::to_string(module.calling_convention);
    output += "},\n";
    const auto hashes = amc::hash_table(module);
    indent(output, 1);
    output +=
        "\"hash\": {\"algorithm\": 0, \"descriptor_version\": 1, "
        "\"canonical_version\": 1, \"flags\": 0, \"abi\": ";
    json_string(output, hash_hex(amc::abi_hash(module)));
    output += ", \"records\": [";
    for (size_t i = 0; i < hashes.size(); ++i) {
        if (i) output += ", ";
        output += "{\"kind\": ";
        output += fmt::to_string(static_cast<uint32_t>(hashes[i].kind));
        output += ", \"target_kind\": ";
        output += fmt::to_string(static_cast<uint32_t>(hashes[i].target_kind));
        output += ", \"target_index\": ";
        output += fmt::to_string(hashes[i].target_index);
        output += ", \"flags\": ";
        output += fmt::to_string(hashes[i].flags);
        output += ", \"value\": ";
        json_string(output, hash_hex(hashes[i].value));
        output += "}";
    }
    output += "]},\n";
    indent(output, 1);
    output +=
        "\"runtime_descriptor\": {\"type_id_bits\": 128, \"registry\": \"runtime::Registry\", "
        "\"type_count\": ";
    output += fmt::to_string(module.types.size());
    output += ", \"function_count\": ";
    output += fmt::to_string(module.functions.size());
    output += ", \"symbol_count\": ";
    output += fmt::to_string(module.symbols.size());
    output += "},\n";
    indent(output, 1);
    output += "\"types\": [\n";
    for (size_t i = 0; i < module.types.size(); ++i) {
        const auto &type = module.types[i];
        indent(output, 2);
        output += "{\"name\": ";
        json_string(output, type.name);
        output += ", \"kind\": ";
        json_string(output, amc::type_kind_name(type.kind));
        output += ", \"id\": ";
        json_string(output, hash_hex(type.id));
        output += ", \"layout_hash\": ";
        json_string(output, hash_hex(type.layout_hash));
        output += ", \"size\": ";
        output += fmt::to_string(type.size);
        output += ", \"align\": ";
        output += fmt::to_string(type.align);
        output += ", \"flags\": ";
        output += fmt::to_string(type.flags);
        output += ", \"field_begin\": ";
        output += fmt::to_string(type.field_begin);
        output += ", \"field_count\": ";
        output += fmt::to_string(type.field_count);
        output += ", \"array_count\": ";
        output += fmt::to_string(type.array_count);
        output += ", \"primitive_abi\": ";
        if (type.kind == amc::TypeKind::primitive)
            write_json_primitive_abi(type, output);
        else
            output += "null";
        output += "}";
        output += (i + 1 == module.types.size() ? "\n" : ",\n");
    }
    indent(output, 1);
    output += "],\n";
    indent(output, 1);
    output += "\"fields\": [\n";
    for (size_t i = 0; i < module.fields.size(); ++i) {
        const auto &field = module.fields[i];
        indent(output, 2);
        output += "{\"name\": ";
        json_string(output, field.name);
        output += ", \"owner_type\": ";
        json_string(output, hash_hex(field.owner_type));
        output += ", \"type_id\": ";
        json_string(output, hash_hex(field.type_id));
        output += ", \"offset\": ";
        output += fmt::to_string(field.offset);
        output += ", \"flags\": ";
        output += fmt::to_string(field.flags);
        output += "}";
        output += (i + 1 == module.fields.size() ? "\n" : ",\n");
    }
    indent(output, 1);
    output += "],\n";
    indent(output, 1);
    output += "\"functions\": [\n";
    for (size_t i = 0; i < module.functions.size(); ++i) {
        const auto &function = module.functions[i];
        indent(output, 2);
        output += "{\"name\": ";
        json_string(output, function.name);
        output += ", \"owner_type\": ";
        json_string(output, hash_hex(function.owner_type));
        output += ", \"signature\": ";
        json_string(output, hash_hex(function.signature));
        output += ", \"return_type\": ";
        json_string(output, hash_hex(function.return_type));
        output += ", \"calling_convention\": ";
        output += fmt::to_string(function.calling_convention);
        output += ", \"flags\": ";
        output += fmt::to_string(function.flags);
        output += ", \"parameters\": [";
        for (size_t p = 0; p < function.parameters.size(); ++p) {
            const auto &parameter = function.parameters[p];
            if (p) output += ", ";
            output += "{\"name\": ";
            json_string(output, parameter.name);
            output += ", \"type_id\": ";
            json_string(output, hash_hex(parameter.type_id));
            output += ", \"flags\": ";
            output += fmt::to_string(parameter.flags);
            output += "}";
        }
        output += "]}";
        output += (i + 1 == module.functions.size() ? "\n" : ",\n");
    }
    indent(output, 1);
    output += "],\n";
    indent(output, 1);
    output += "\"symbols\": [\n";
    for (size_t i = 0; i < module.symbols.size(); ++i) {
        const auto &symbol = module.symbols[i];
        indent(output, 2);
        output += "{\"name\": ";
        json_string(output, symbol.name);
        output += ", \"kind\": ";
        json_string(output, symbol.kind == amc::SymbolKind::type    ? "type"
                            : symbol.kind == amc::SymbolKind::field ? "field"
                                                                    : "function");
        output += ", \"target_index\": ";
        output += fmt::to_string(symbol.target_index);
        output += "}";
        output += (i + 1 == module.symbols.size() ? "\n" : ",\n");
    }
    indent(output, 1);
    output += "],\n";
    indent(output, 1);
    output += "\"compatibility\": [";
    for (size_t i = 0; i < module.compatibility.size(); ++i) {
        if (i) output += ", ";
        const auto &record = module.compatibility[i];
        output += "{\"kind\": ";
        json_string(output, amc::compatibility_name(record.kind));
        output += ", \"source_type\": ";
        json_string(output, hash_hex(record.source_type));
        output += ", \"target_type\": ";
        json_string(output, hash_hex(record.target_type));
        output += ", \"map_index\": ";
        output += fmt::to_string(record.map_index);
        output += ", \"flags\": ";
        output += fmt::to_string(record.flags);
        output += "}";
    }
    output += "],\n";
    indent(output, 1);
    output += "\"maps\": [";
    for (size_t i = 0; i < module.maps.size(); ++i) {
        if (i) output += ", ";
        const auto &map = module.maps[i];
        output += "{\"source\": ";
        json_string(output, map.source_name);
        output += ", \"target\": ";
        json_string(output, map.target_name);
        output += ", \"source_type\": ";
        json_string(output, hash_hex(map.source_type));
        output += ", \"target_type\": ";
        json_string(output, hash_hex(map.target_type));
        output += ", \"operation_begin\": ";
        output += fmt::to_string(map.operation_begin);
        output += ", \"operation_count\": ";
        output += fmt::to_string(map.operations.size());
        output += ", \"flags\": ";
        output += fmt::to_string(map.flags);
        output += ", \"operations\": ";
        write_json_operations(map.operations, output);
        output += "}";
    }
    output += "]\n}\n";
}

}   // namespace amc::dump
