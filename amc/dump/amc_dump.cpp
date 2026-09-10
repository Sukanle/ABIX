#include "amc_core.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string hash_string(amc::Hash128 value) {
    std::ostringstream output;
    output << "0x" << std::hex << std::setfill('0') << std::setw(16) << value.hi
           << std::setw(16) << value.lo;
    return output.str();
}

std::string escape_json(const std::string &value) {
    std::ostringstream output;
    for (unsigned char c : value) {
        switch (c) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (c < 0x20)
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<unsigned>(c) << std::dec << std::setfill(' ');
                else output << c;
        }
    }
    return output.str();
}

void json_string(std::ostream &output, const std::string &value) {
    output << '"' << escape_json(value) << '"';
}

void indent(std::ostream &output, unsigned depth) { output << std::string(depth * 2, ' '); }

const char *map_opcode_name(amc::MapOpcode opcode) {
    switch (opcode) {
        case amc::MapOpcode::copy_field: return "copy_field";
        case amc::MapOpcode::convert_int: return "convert_int";
        case amc::MapOpcode::convert_float: return "convert_float";
        case amc::MapOpcode::add_default: return "add_default";
        case amc::MapOpcode::skip_field: return "skip_field";
    }
    return "unknown";
}

void write_json_operations(const std::vector<amc::MapOperation> &operations, std::ostream &output) {
    output << "[";
    for (size_t i = 0; i < operations.size(); ++i) {
        if (i) output << ", ";
        const auto &operation = operations[i];
        output << "{\"opcode\": "; json_string(output, map_opcode_name(operation.opcode));
        output << ", \"source_field\": " << operation.source_field
               << ", \"target_field\": " << operation.target_field
               << ", \"source_offset\": " << operation.source_offset
               << ", \"target_offset\": " << operation.target_offset
               << ", \"byte_count\": " << operation.byte_count
               << ", \"auxiliary\": ";
        json_string(output, hash_string(operation.auxiliary));
        output << "}";
    }
    output << "]";
}

void write_json(const amc::AbiModule &module, std::ostream &output) {
    output << "{\n";
    indent(output, 1); output << "\"artifact\": {\"format_version\": 4, \"hash_algorithm\": 0, "
                             << "\"sections\": [\"strings\", \"identity\", \"target\", \"types\", \"fields\", "
                             << "\"functions\", \"parameters\", \"symbols\", \"hash_descriptor\", "
                             << "\"hash_table\", \"compatibility\", \"maps\", \"map_operations\"]},\n";
    indent(output, 1); output << "\"package\": {\"name\": "; json_string(output, module.package_name);
    output << ", \"version\": "; json_string(output, module.package_version); output << "},\n";
    indent(output, 1); output << "\"target\": {\"arch\": " << module.arch << ", \"os\": " << module.os
                              << ", \"abi\": " << module.target_abi
                              << ", \"compiler\": " << module.compiler << ", \"calling_convention\": "
                              << module.calling_convention << "},\n";
    const auto hashes = amc::hash_table(module);
    indent(output, 1); output << "\"hash\": {\"algorithm\": 0, \"descriptor_version\": 1, "
                             << "\"canonical_version\": 1, \"flags\": 0, \"abi\": ";
    json_string(output, hash_string(amc::abi_hash(module)));
    output << ", \"records\": [";
    for (size_t i = 0; i < hashes.size(); ++i) {
        if (i) output << ", ";
        output << "{\"kind\": " << static_cast<uint32_t>(hashes[i].kind)
               << ", \"target_kind\": " << static_cast<uint32_t>(hashes[i].target_kind)
               << ", \"target_index\": " << hashes[i].target_index
               << ", \"flags\": " << hashes[i].flags << ", \"value\": ";
        json_string(output, hash_string(hashes[i].value));
        output << "}";
    }
    output << "]},\n";
    indent(output, 1); output << "\"runtime_descriptor\": {\"type_id_bits\": 128, \"registry\": \"RuntimeRegistry\", "
                             << "\"type_count\": " << module.types.size()
                             << ", \"function_count\": " << module.functions.size()
                             << ", \"symbol_count\": " << module.symbols.size() << "},\n";
    indent(output, 1); output << "\"types\": [\n";
    for (size_t i = 0; i < module.types.size(); ++i) {
        const auto &type = module.types[i];
        indent(output, 2); output << "{\"name\": "; json_string(output, type.name);
        output << ", \"kind\": "; json_string(output, amc::type_kind_name(type.kind));
        output << ", \"id\": "; json_string(output, hash_string(type.id));
        output << ", \"layout_hash\": "; json_string(output, hash_string(type.layout_hash));
        output << ", \"size\": " << type.size << ", \"align\": " << type.align << ", \"flags\": " << type.flags
               << ", \"field_begin\": " << type.field_begin << ", \"field_count\": " << type.field_count
               << ", \"array_count\": " << type.array_count << "}" << (i + 1 == module.types.size() ? "\n" : ",\n");
    }
    indent(output, 1); output << "],\n";
    indent(output, 1); output << "\"fields\": [\n";
    for (size_t i = 0; i < module.fields.size(); ++i) {
        const auto &field = module.fields[i];
        indent(output, 2); output << "{\"name\": "; json_string(output, field.name);
        output << ", \"owner_type\": "; json_string(output, hash_string(field.owner_type));
        output << ", \"type_id\": "; json_string(output, hash_string(field.type_id));
        output << ", \"offset\": " << field.offset << ", \"flags\": " << field.flags << "}"
               << (i + 1 == module.fields.size() ? "\n" : ",\n");
    }
    indent(output, 1); output << "],\n";
    indent(output, 1); output << "\"functions\": [\n";
    for (size_t i = 0; i < module.functions.size(); ++i) {
        const auto &function = module.functions[i];
        indent(output, 2); output << "{\"name\": "; json_string(output, function.name);
        output << ", \"owner_type\": "; json_string(output, hash_string(function.owner_type));
        output << ", \"signature\": "; json_string(output, hash_string(function.signature));
        output << ", \"return_type\": "; json_string(output, hash_string(function.return_type));
        output << ", \"calling_convention\": " << function.calling_convention << ", \"flags\": " << function.flags
               << ", \"parameters\": [";
        for (size_t p = 0; p < function.parameters.size(); ++p) {
            const auto &parameter = function.parameters[p];
            if (p) output << ", ";
            output << "{\"name\": "; json_string(output, parameter.name);
            output << ", \"type_id\": "; json_string(output, hash_string(parameter.type_id));
            output << ", \"flags\": " << parameter.flags << "}";
        }
        output << "]}" << (i + 1 == module.functions.size() ? "\n" : ",\n");
    }
    indent(output, 1); output << "],\n";
    indent(output, 1); output << "\"symbols\": [\n";
    for (size_t i = 0; i < module.symbols.size(); ++i) {
        const auto &symbol = module.symbols[i];
        indent(output, 2); output << "{\"name\": "; json_string(output, symbol.name);
        output << ", \"kind\": ";
        json_string(output, symbol.kind == amc::SymbolKind::type ? "type" :
                           symbol.kind == amc::SymbolKind::field ? "field" : "function");
        output << ", \"target_index\": " << symbol.target_index << "}"
               << (i + 1 == module.symbols.size() ? "\n" : ",\n");
    }
    indent(output, 1); output << "],\n";
    indent(output, 1); output << "\"compatibility\": [";
    for (size_t i = 0; i < module.compatibility.size(); ++i) {
        if (i) output << ", ";
        const auto &record = module.compatibility[i];
        output << "{\"kind\": "; json_string(output, amc::compatibility_name(record.kind));
        output << ", \"source_type\": "; json_string(output, hash_string(record.source_type));
        output << ", \"target_type\": "; json_string(output, hash_string(record.target_type));
        output << ", \"map_index\": " << record.map_index << ", \"flags\": " << record.flags << "}";
    }
    output << "],\n";
    indent(output, 1); output << "\"maps\": [";
    for (size_t i = 0; i < module.maps.size(); ++i) {
        if (i) output << ", ";
        const auto &map = module.maps[i];
        output << "{\"source\": "; json_string(output, map.source_name);
        output << ", \"target\": "; json_string(output, map.target_name);
        output << ", \"source_type\": "; json_string(output, hash_string(map.source_type));
        output << ", \"target_type\": "; json_string(output, hash_string(map.target_type));
        output << ", \"operation_begin\": " << map.operation_begin
               << ", \"operation_count\": " << map.operations.size() << ", \"flags\": " << map.flags
               << ", \"operations\": ";
        write_json_operations(map.operations, output);
        output << "}";
    }
    output << "]\n}\n";
}

void write_text(const amc::AbiModule &module, std::ostream &output) {
    output << "artifact: format_version=4 hash_algorithm=0 sections="
           << "strings,identity,target,types,fields,functions,parameters,symbols,hash_descriptor,hash_table,"
           << "compatibility,maps,map_operations\n";
    output << "package: " << module.package_name;
    if (!module.package_version.empty()) output << " (" << module.package_version << ")";
    output << "\ntarget: arch=" << module.arch << " os=" << module.os << " abi=" << module.target_abi
           << " compiler=" << module.compiler
           << " calling_convention=" << module.calling_convention << "\n";
    output << "hash: algorithm=0 canonical_version=1 abi=" << hash_string(amc::abi_hash(module))
           << " records=" << amc::hash_table(module).size() << "\n";
    output << "runtime_descriptor: type_id_bits=128 registry=RuntimeRegistry types="
           << module.types.size() << " functions=" << module.functions.size()
           << " symbols=" << module.symbols.size() << "\n";
    output << "types (" << module.types.size() << "):\n";
    for (size_t i = 0; i < module.types.size(); ++i) {
        const auto &type = module.types[i];
        output << "  [" << i << "] " << type.name << " kind=" << amc::type_kind_name(type.kind)
               << " id=" << hash_string(type.id) << " size=" << type.size << " align=" << type.align
               << " layout_hash=" << hash_string(type.layout_hash)
               << " flags=" << type.flags << " fields=" << type.field_begin << "+" << type.field_count;
        if (type.array_count) output << " array_count=" << type.array_count;
        output << '\n';
    }
    output << "fields (" << module.fields.size() << "):\n";
    for (size_t i = 0; i < module.fields.size(); ++i) {
        const auto &field = module.fields[i];
        output << "  [" << i << "] " << field.name << " type=" << hash_string(field.type_id)
               << " owner=" << hash_string(field.owner_type)
               << " offset=" << field.offset << " flags=" << field.flags << '\n';
    }
    output << "functions (" << module.functions.size() << "):\n";
    for (size_t i = 0; i < module.functions.size(); ++i) {
        const auto &function = module.functions[i];
        output << "  [" << i << "] " << function.name << " signature=" << hash_string(function.signature)
               << " owner=" << hash_string(function.owner_type)
               << " returns=" << hash_string(function.return_type) << " calling_convention=" << function.calling_convention
               << " flags=" << function.flags << '\n';
        for (size_t p = 0; p < function.parameters.size(); ++p) {
            const auto &parameter = function.parameters[p];
            output << "    (" << p << ") " << parameter.name << " type=" << hash_string(parameter.type_id)
                   << " flags=" << parameter.flags << '\n';
        }
    }
    output << "symbols (" << module.symbols.size() << "):\n";
    for (size_t i = 0; i < module.symbols.size(); ++i) {
        const auto &symbol = module.symbols[i];
        output << "  [" << i << "] " << symbol.name << " kind="
               << (symbol.kind == amc::SymbolKind::type ? "type" :
                   symbol.kind == amc::SymbolKind::field ? "field" : "function")
               << " target_index=" << symbol.target_index << '\n';
    }
    output << "compatibility (" << module.compatibility.size() << "):\n";
    for (const auto &record : module.compatibility)
        output << "  " << amc::compatibility_name(record.kind)
               << " " << hash_string(record.source_type) << " -> " << hash_string(record.target_type)
               << " map=" << record.map_index << " flags=" << record.flags << '\n';
    output << "maps (" << module.maps.size() << "):\n";
    for (const auto &map : module.maps) {
        output << "  " << map.source_name << " (" << hash_string(map.source_type) << ") -> "
               << map.target_name << " (" << hash_string(map.target_type) << ") begin=" << map.operation_begin
               << " operations=" << map.operations.size() << " flags=" << map.flags << '\n';
        for (size_t i = 0; i < map.operations.size(); ++i) {
            const auto &operation = map.operations[i];
            output << "    (" << i << ") " << map_opcode_name(operation.opcode)
                   << " source_field=" << operation.source_field << " target_field=" << operation.target_field
                   << " source_offset=" << operation.source_offset << " target_offset=" << operation.target_offset
                   << " bytes=" << operation.byte_count << " auxiliary=" << hash_string(operation.auxiliary) << '\n';
        }
    }
}

void write_diff_text(const amc::AbiModule &source, const amc::AbiModule &target,
                     const amc::AbiModule &report, std::ostream &output) {
    output << "diff:\n  source: " << source.package_name << " abi=" << hash_string(amc::abi_hash(source))
           << "\n  target: " << target.package_name << " abi=" << hash_string(amc::abi_hash(target)) << '\n';
    write_text(report, output);
}

void write_diff_json(const amc::AbiModule &source, const amc::AbiModule &target,
                     const amc::AbiModule &report, std::ostream &output) {
    output << "{\n  \"source\": {\"package\": "; json_string(output, source.package_name);
    output << ", \"abi_hash\": "; json_string(output, hash_string(amc::abi_hash(source)));
    output << "},\n  \"target\": {\"package\": "; json_string(output, target.package_name);
    output << ", \"abi_hash\": "; json_string(output, hash_string(amc::abi_hash(target)));
    output << "},\n  \"report\": ";
    write_json(report, output);
    output << "}\n";
}

void print_usage() {
    std::cerr << "usage: amc-dump <file.abix> [--json <output.json>]\n"
                 "       amc-dump diff <source.abix> <target.abix> [--json <output.json>]\n";
}

}  // namespace

int main(int argc, char **argv) {
    const bool diff = argc >= 2 && std::string(argv[1]) == "diff";
    const bool json = (diff && argc == 6 && std::string(argv[4]) == "--json") ||
                      (!diff && argc == 4 && std::string(argv[2]) == "--json");
    if ((diff && argc != 4 && !json) || (!diff && argc != 2 && !json)) {
        print_usage();
        return 2;
    }

    if (diff) {
        amc::AbiModule source, target, report;
        std::string error;
        if (!amc::read_abix(argv[2], source, error) || !amc::read_abix(argv[3], target, error) ||
            !amc::build_compatibility(source, target, report, error)) {
            std::cerr << "amc-dump diff: " << error << '\n';
            return 1;
        }
        if (!json) {
            write_diff_text(source, target, report, std::cout);
            return 0;
        }
        std::ofstream output(argv[5], std::ios::binary);
        if (!output) { std::cerr << "amc-dump: cannot open JSON output: " << argv[5] << '\n'; return 1; }
        write_diff_json(source, target, report, output);
        if (!output) { std::cerr << "amc-dump: failed to write JSON output: " << argv[5] << '\n'; return 1; }
        return 0;
    }

    amc::AbiModule module;
    std::string error;
    if (!amc::read_abix(argv[1], module, error)) { std::cerr << "amc-dump: " << error << '\n'; return 1; }
    if (!json) { write_text(module, std::cout); return 0; }
    std::ofstream output(argv[3], std::ios::binary);
    if (!output) { std::cerr << "amc-dump: cannot open JSON output: " << argv[3] << '\n'; return 1; }
    write_json(module, output);
    if (!output) { std::cerr << "amc-dump: failed to write JSON output: " << argv[3] << '\n'; return 1; }
    return 0;
}
