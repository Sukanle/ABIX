#include "amc_verify.h"
#include "amc_error.h"

#include <iomanip>
#include <map>
#include <sstream>
#include <utility>

namespace amc {
namespace {

using TypeKey = std::pair<uint64_t, uint64_t>;

TypeKey key(Hash128 value) { return {value.lo, value.hi}; }

std::string hash_string(Hash128 value) {
    if (value == Hash128{}) return "0x0";
    std::ostringstream output;
    output << "0x" << std::hex << std::setfill('0') << std::setw(16) << value.hi << std::setw(16) << value.lo;
    return output.str();
}

using TypeMap = std::map<TypeKey, const Type *>;
using FieldMap = std::map<TypeKey, std::vector<const Field *>>;
using FunctionMap = std::map<TypeKey, std::vector<const Function *>>;

TypeMap index_types(const AbiModule &module) {
    TypeMap map;
    for (const auto &type : module.types)
        map[key(type.id)] = &type;
    return map;
}

FieldMap index_fields(const AbiModule &module) {
    FieldMap map;
    for (const auto &field : module.fields)
        map[key(field.owner_type)].push_back(&field);
    return map;
}

FunctionMap index_functions(const AbiModule &module) {
    FunctionMap map;
    for (const auto &function : module.functions)
        map[key(function.owner_type)].push_back(&function);
    return map;
}

const Field *find_field(const std::vector<const Field *> &fields, const std::string &name) {
    for (const auto *field : fields)
        if (field->name == name) return field;
    return nullptr;
}

void add_change(VerifyResult &result, ChangeKind kind, std::string type, std::string name, std::string detail) {
    result.consistent = false;
    result.changes.push_back({kind, std::move(type), std::move(name), std::move(detail)});
}

std::string layout_detail(const Type &contract, const Type &implementation) {
    std::ostringstream out;
    out
        << "layout_hash "
        << hash_string(contract.layout_hash)
        << " -> "
        << hash_string(implementation.layout_hash)
        << ", size "
        << contract.size
        << " -> "
        << implementation.size
        << ", align "
        << contract.align
        << " -> "
        << implementation.align;
    return out.str();
}

void compare_type(const Type &contract, const Type &implementation, const FieldMap &contract_fields,
    const FieldMap &implementation_fields, VerifyResult &result) {
    if (!(contract.layout_hash == implementation.layout_hash)
        || contract.size != implementation.size
        || contract.align != implementation.align)
        add_change(result, ChangeKind::layout_changed, contract.name, {}, layout_detail(contract, implementation));

    const auto &contract_owned =
        contract_fields.count(key(contract.id)) ? contract_fields.at(key(contract.id)) : std::vector<const Field *>{};
    const auto &implementation_owned = implementation_fields.count(key(contract.id))
                                         ? implementation_fields.at(key(contract.id))
                                         : std::vector<const Field *>{};

    for (const auto *field : contract_owned) {
        const Field *match = find_field(implementation_owned, field->name);
        if (!match) {
            add_change(result, ChangeKind::field_removed, contract.name, field->name, "field removed");
            continue;
        }
        if (match->offset != field->offset)
            add_change(result, ChangeKind::field_offset_changed, contract.name, field->name,
                "offset " + std::to_string(field->offset) + " -> " + std::to_string(match->offset));
        if (!(match->type_id == field->type_id))
            add_change(result, ChangeKind::field_type_changed, contract.name, field->name,
                "type " + hash_string(field->type_id) + " -> " + hash_string(match->type_id));
    }
    for (const auto *field : implementation_owned)
        if (!find_field(contract_owned, field->name))
            add_change(result, ChangeKind::field_added, contract.name, field->name, "field added");
}

void compare_function(const Function &contract, const Function &match, VerifyResult &result) {
    if (!(contract.signature == match.signature)) {
        add_change(result, ChangeKind::function_signature_changed,
            match.owner_type == Hash128{} ? std::string{} : match.name, match.name,
            "signature " + hash_string(contract.signature) + " -> " + hash_string(match.signature));
        return;
    }
    if (contract.parameters.size() != match.parameters.size()) {
        add_change(result, ChangeKind::function_parameter_changed, {}, match.name,
            "parameter count "
                + std::to_string(contract.parameters.size())
                + " -> "
                + std::to_string(match.parameters.size()));
        return;
    }
    for (size_t i = 0; i < contract.parameters.size(); ++i) {
        if (!(contract.parameters[i].type_id == match.parameters[i].type_id)) {
            add_change(result, ChangeKind::function_parameter_changed, {}, match.name,
                "parameter "
                    + std::to_string(i)
                    + " type "
                    + hash_string(contract.parameters[i].type_id)
                    + " -> "
                    + hash_string(match.parameters[i].type_id));
            return;
        }
    }
}

}   // namespace

const char *change_kind_name(ChangeKind kind) {
    switch (kind) {
        case ChangeKind::type_added:                 return "type_added";
        case ChangeKind::type_removed:               return "type_removed";
        case ChangeKind::layout_changed:             return "layout_changed";
        case ChangeKind::field_added:                return "field_added";
        case ChangeKind::field_removed:              return "field_removed";
        case ChangeKind::field_offset_changed:       return "field_offset_changed";
        case ChangeKind::field_type_changed:         return "field_type_changed";
        case ChangeKind::function_added:             return "function_added";
        case ChangeKind::function_removed:           return "function_removed";
        case ChangeKind::function_signature_changed: return "function_signature_changed";
        case ChangeKind::function_parameter_changed: return "function_parameter_changed";
    }
    return "unknown";
}

bool verify_modules(
    const AbiModule &contract, const AbiModule &implementation, VerifyResult &result, std::string &error) {
    (void)error;
    result = VerifyResult{};
    result.package = contract.package_name;
    result.consistent = true;

    const TypeMap contract_types = index_types(contract);
    const TypeMap implementation_types = index_types(implementation);
    const FieldMap contract_fields = index_fields(contract);
    const FieldMap implementation_fields = index_fields(implementation);
    const FunctionMap contract_functions = index_functions(contract);
    const FunctionMap implementation_functions = index_functions(implementation);

    for (const auto &type : contract.types) {
        const auto it = implementation_types.find(key(type.id));
        if (it == implementation_types.end()) {
            add_change(result, ChangeKind::type_removed, type.name, {}, "type missing from implementation");
            continue;
        }
        compare_type(type, *it->second, contract_fields, implementation_fields, result);
    }
    for (const auto &type : implementation.types)
        if (!contract_types.count(key(type.id)))
            add_change(result, ChangeKind::type_added, type.name, {}, "type not present in contract");

    const auto compare_function_set = [&](const std::vector<const Function *> &contracts,
                                          const std::vector<const Function *> &matches, const std::string &owner_name) {
        for (const auto *contract_function : contracts) {
            const Function *match = nullptr;
            for (const auto *candidate : matches)
                if (candidate->name == contract_function->name) {
                    match = candidate;
                    break;
                }
            if (!match) {
                add_change(
                    result, ChangeKind::function_removed, owner_name, contract_function->name, "function removed");
                continue;
            }
            compare_function(*contract_function, *match, result);
        }
        for (const auto *implementation_function : matches) {
            bool found = false;
            for (const auto *candidate : contracts)
                if (candidate->name == implementation_function->name) {
                    found = true;
                    break;
                }
            if (!found)
                add_change(
                    result, ChangeKind::function_added, owner_name, implementation_function->name, "function added");
        }
    };

    // Compare every owner that appears in either module.
    std::map<TypeKey, std::string> owner_names;
    for (const auto &entry : contract_types)
        owner_names[entry.first] = entry.second->name;
    for (const auto &entry : implementation_types)
        owner_names.emplace(entry.first, entry.second->name);
    owner_names[key(Hash128{})] = "";

    for (const auto &entry : owner_names) {
        const auto contract_it = contract_functions.find(entry.first);
        const auto implementation_it = implementation_functions.find(entry.first);
        const std::vector<const Function *> empty;
        compare_function_set(contract_it == contract_functions.end() ? empty : contract_it->second,
            implementation_it == implementation_functions.end() ? empty : implementation_it->second, entry.second);
    }

    return true;
}

std::string verify_result_to_text(const VerifyResult &result) {
    std::ostringstream out;
    out << "verify: package=" << result.package << " status=" << (result.consistent ? "consistent" : "drift") << "\n";
    if (!result.contract_path.empty()) out << "  contract: " << result.contract_path << "\n";
    if (!result.implementation_path.empty()) out << "  implementation: " << result.implementation_path << "\n";
    for (const auto &change : result.changes) {
        out << "  - " << change_kind_name(change.kind);
        if (!change.type.empty()) out << " type=" << change.type;
        if (!change.name.empty()) out << " name=" << change.name;
        if (!change.detail.empty()) out << " (" << change.detail << ")";
        out << "\n";
    }
    return out.str();
}

std::string verify_result_to_json(const VerifyResult &result) {
    std::ostringstream out;
    out
        << "{\"package\":\""
        << json_escape(result.package)
        << '"'
        << ",\"contract\":\""
        << json_escape(result.contract_path)
        << '"'
        << ",\"implementation\":\""
        << json_escape(result.implementation_path)
        << '"'
        << ",\"status\":\""
        << (result.consistent ? "consistent" : "drift")
        << '"'
        << ",\"changes\":[";
    for (size_t i = 0; i < result.changes.size(); ++i) {
        const auto &change = result.changes[i];
        if (i) out << ",";
        out
            << "{\"kind\":\""
            << change_kind_name(change.kind)
            << '"'
            << ",\"type\":\""
            << json_escape(change.type)
            << '"'
            << ",\"name\":\""
            << json_escape(change.name)
            << '"'
            << ",\"detail\":\""
            << json_escape(change.detail)
            << "\"}";
    }
    out << "]}";
    return out.str();
}

std::string verify_report_to_text(const std::vector<VerifyResult> &results) {
    std::ostringstream out;
    for (const auto &result : results)
        out << verify_result_to_text(result);
    return out.str();
}

std::string verify_report_to_json(const std::vector<VerifyResult> &results, bool consistent) {
    std::ostringstream out;
    out << "{\"schema\":\"abix.verify/1\",\"consistent\":" << (consistent ? "true" : "false") << ",\"modules\":[";
    for (size_t i = 0; i < results.size(); ++i) {
        if (i) out << ",";
        out << verify_result_to_json(results[i]);
    }
    out << "]}\n";
    return out.str();
}

}   // namespace amc
