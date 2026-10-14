#include "AMC/CLI/CLI.h"

#include <string>
#include <vector>

namespace amc::cli {

namespace {

// Resolve the `[[owned]]` ownership declarations against the frontend module:
// map each declared value type and its create/destroy entry points to their
// stable TypeID / signature hashes before projection.
bool resolve_ownership(const toml::table &config, amc::AbiModule &module, std::string &error) {
    const auto *owned = config["owned"].as_array();
    if (!owned) return true;
    const auto find_type = [&](const std::string &name) -> const amc::Type * {
        for (const auto &type : module.types)
            if (type.name == name) return &type;
        return nullptr;
    };
    const auto find_function = [&](const std::string &name) -> const amc::Function * {
        for (const auto &function : module.functions)
            if (function.name == name) return &function;
        return nullptr;
    };
    for (auto &&entry : *owned) {
        const auto *table = entry.as_table();
        if (!table) continue;
        const std::string type_name = (*table)["type"].value_or("");
        if (type_name.empty()) {
            error = "[[owned]] requires a type";
            return false;
        }
        const amc::Type *type = find_type(type_name);
        if (!type) {
            error = "[[owned]] references unknown type: " + type_name;
            return false;
        }
        amc::OwnedType record;
        record.type_id = type->id;
        const std::string create = (*table)["create"].value_or("");
        const std::string destroy = (*table)["destroy"].value_or("");
        if (!create.empty()) {
            const amc::Function *function = find_function(create);
            if (!function) {
                error = "[[owned]] references unknown create function: " + create;
                return false;
            }
            record.create = function->signature;
        }
        if (!destroy.empty()) {
            const amc::Function *function = find_function(destroy);
            if (!function) {
                error = "[[owned]] references unknown destroy function: " + destroy;
                return false;
            }
            record.destroy = function->signature;
        }
        if (destroy.empty()) {
            error = "[[owned]] release policy requires a destroy function: " + type_name;
            return false;
        }
        module.owned.push_back(record);
    }
    return true;
}

amc::OwnershipMode parse_ownership_mode(const std::string &mode) {
    if (mode == "borrowed" || mode == "borrow") return amc::OwnershipMode::borrowed;
    if (mode == "owned") return amc::OwnershipMode::owned;
    if (mode == "copy") return amc::OwnershipMode::copy;
    return amc::OwnershipMode::unspecified;
}

// Resolve `[[contract]]` declarations into per-interaction ownership records:
// the mode of a function's return value and of its positional parameters, and
// the release entry point of an `owned` return.
bool resolve_contracts(const toml::table &config, amc::AbiModule &module, std::string &error) {
    const auto *contracts = config["contract"].as_array();
    if (!contracts) return true;
    std::vector<uint32_t> parameter_base(module.functions.size(), 0);
    uint32_t running = 0;
    for (size_t i = 0; i < module.functions.size(); ++i) {
        parameter_base[i] = running;
        running += static_cast<uint32_t>(module.functions[i].parameters.size());
    }
    const auto function_index = [&](const std::string &name) -> int {
        for (size_t i = 0; i < module.functions.size(); ++i)
            if (module.functions[i].name == name) return static_cast<int>(i);
        return -1;
    };
    for (auto &&entry : *contracts) {
        const auto *table = entry.as_table();
        if (!table) continue;
        const std::string function_name = (*table)["function"].value_or("");
        if (function_name.empty()) {
            error = "[[contract]] requires a function";
            return false;
        }
        const int index = function_index(function_name);
        if (index < 0) {
            error = "[[contract]] references unknown function: " + function_name;
            return false;
        }
        const amc::Function &function = module.functions[static_cast<size_t>(index)];
        const std::string returns = (*table)["returns"].value_or("");
        if (!returns.empty()) {
            amc::OwnershipContract contract;
            contract.target = amc::OwnershipTarget::return_value;
            contract.target_index = static_cast<uint32_t>(index);
            contract.mode = parse_ownership_mode(returns);
            const std::string release = (*table)["release"].value_or("");
            if (!release.empty()) {
                const int release_index = function_index(release);
                if (release_index < 0) {
                    error = "[[contract]] references unknown release function: " + release;
                    return false;
                }
                contract.release = module.functions[static_cast<size_t>(release_index)].signature;
            }
            if (contract.mode == amc::OwnershipMode::owned && release.empty()) {
                error = "[[contract]] owned return requires a release function: " + function_name;
                return false;
            }
            module.contracts.push_back(contract);
        }
        if (auto *params = (*table)["params"].as_array()) {
            size_t position = 0;
            for (auto &&value : *params) {
                const std::string mode = value.value_or("");
                if (mode.empty()) {
                    ++position;
                    continue;
                }
                if (position >= function.parameters.size()) {
                    error = "[[contract]] has more params than " + function_name + " declares";
                    return false;
                }
                amc::OwnershipContract contract;
                contract.target = amc::OwnershipTarget::parameter;
                contract.target_index = parameter_base[static_cast<size_t>(index)] + static_cast<uint32_t>(position);
                contract.mode = parse_ownership_mode(mode);
                module.contracts.push_back(contract);
                ++position;
            }
        }
    }
    return true;
}

// Resolve `[[error]]` declarations into per-function error-boundary records.
bool resolve_errors(const toml::table &config, amc::AbiModule &module, std::string &error) {
    const auto *errors = config["error"].as_array();
    if (!errors) return true;
    const auto function_index = [&](const std::string &name) -> int {
        for (size_t i = 0; i < module.functions.size(); ++i)
            if (module.functions[i].name == name) return static_cast<int>(i);
        return -1;
    };
    for (auto &&entry : *errors) {
        const auto *table = entry.as_table();
        if (!table) continue;
        const std::string function_name = (*table)["function"].value_or("");
        if (function_name.empty()) {
            error = "[[error]] requires a function";
            return false;
        }
        const int index = function_index(function_name);
        if (index < 0) {
            error = "[[error]] references unknown function: " + function_name;
            return false;
        }
        amc::ErrorContract contract;
        contract.function_index = static_cast<uint32_t>(index);
        const std::string status = (*table)["status"].value_or(std::string("return"));
        contract.status_source = status == "parameter" ? amc::StatusSource::parameter : amc::StatusSource::return_value;
        contract.success_value = (*table)["success"].value_or(int64_t{0});
        const int64_t value = (*table)["value"].value_or(int64_t{-1});
        contract.value_parameter = value < 0 ? UINT32_MAX : static_cast<uint32_t>(value);
        if (contract.value_parameter != UINT32_MAX
            && contract.value_parameter >= module.functions[static_cast<size_t>(index)].parameters.size()) {
            error = "[[error]] value parameter is out of range: " + function_name;
            return false;
        }
        module.errors.push_back(contract);
    }
    return true;
}

}   // namespace

int cmd_build(char **args, int arg_count, const fs::path &provider) {
    const std::string command = args[1];
    if (command == "build") {
        fs::path config_path;
        fs::path build_dir;
        for (int i = 2; i < arg_count; ++i) {
            const std::string argument = args[i];
            if ((argument == "-c" || argument == "-B") && i + 1 < arg_count) {
                const fs::path value = args[++i];
                if (argument == "-c")
                    config_path = value;
                else
                    build_dir = fs::absolute(value);
            } else if (config_path.empty()) {
                config_path = argument;
            } else {
                print_usage();
                return 2;
            }
        }
        if (config_path.empty()) {
            print_usage();
            return 2;
        }
        try {
            const auto config = toml::parse_file(config_path.string());
            const auto *imports = config["import"].as_array();
            if (!imports || imports->size() != 1)
                return fail(
                    amc::ErrorCategory::config, "build", "MVP requires exactly one [[import]]", config_path.string());
            std::vector<ExportSpec> exports;
            std::string error;
            if (!parse_exports(config, exports, error))
                return fail(amc::ErrorCategory::config, "build", error, config_path.string());
            const auto *import_table = (*imports)[0].as_table();
            const std::string language =
                import_table ? import_table->get("language")->value_or(std::string("cpp")) : "cpp";
            const fs::path frontend = provider_executable(provider, language);
            const fs::path base = build_dir.empty() ? fs::absolute(config_path).parent_path() : build_dir;
            const fs::path temporary = base / ".amc" / "frontend.abix";
            std::error_code ec;
            fs::create_directories(temporary.parent_path(), ec);
            if (ec)
                return fail(
                    amc::ErrorCategory::io, "build", "cannot create build directory", base.string(), ec.message());
            if (!dispatch_provider(frontend, "frontend", config_path, temporary, error))
                return fail(amc::ErrorCategory::provider, "frontend", error, config_path.string());

            amc::AbiModule full;
            if (!amc::read_abix(temporary.string(), full, error))
                return fail(read_error_category(error), "read_abix", error, temporary.string());
            full.package_name = config["package"]["name"].value_or("cpp");
            full.package_version = config["package"]["version"].value_or("");
            if (!resolve_ownership(config, full, error))
                return fail(amc::ErrorCategory::config, "build", error, config_path.string());
            if (!resolve_contracts(config, full, error))
                return fail(amc::ErrorCategory::config, "build", error, config_path.string());
            if (!resolve_errors(config, full, error))
                return fail(amc::ErrorCategory::config, "build", error, config_path.string());
            if (const auto *target = config["target"].as_table()) {
                if (auto value = (*target)["arch"].value<int64_t>()) full.arch = static_cast<uint32_t>(*value);
                if (auto value = (*target)["os"].value<int64_t>()) full.os = static_cast<uint32_t>(*value);
                if (auto value = (*target)["abi"].value<int64_t>()) full.target_abi = static_cast<uint32_t>(*value);
                if (auto value = (*target)["pointer_bits"].value<int64_t>())
                    full.pointer_bits = static_cast<uint32_t>(*value);
                const std::string endianness = (*target)["endianness"].value_or(std::string{});
                if (!endianness.empty())
                    full.endianness = endianness == "little" ? 1u : (endianness == "big" ? 2u : 0u);
            }
            // ABIX-STD lowering: match each contracted instantiation against its
            // contract. Instantiation succeeded already (the frontend did it);
            // this decides whether the concrete type is the boundary entity
            // (direct), needs a thin adapter, or cannot be lowered (rejected).
            std::vector<std::pair<uint32_t, amc::Hash128>> canonical;   // kind -> direct canonical type
            if (const auto *instantiations = config["instantiate"].as_array()) {
                std::vector<amc::LoweringResult> results;
                for (auto &&entry : *instantiations) {
                    const auto *table = entry.as_table();
                    if (!table) continue;
                    const std::string contract = (*table)["contract"].value_or("");
                    // `purpose` is the design's name for the same thing.
                    const std::string purpose = (*table)["purpose"].value_or("");
                    const std::string selected_contract = contract.empty() ? purpose : contract;
                    if (selected_contract.empty()) continue;
                    const bool string_view =
                        selected_contract == "string-view" || selected_contract == "abix-std.string-view";
                    if (!string_view
                        && selected_contract != "sequence-view"
                        && selected_contract != "abix-std.sequence-view") {
                        error = "unsupported ABIX-STD contract: " + selected_contract;
                        return fail(amc::ErrorCategory::config, "build", error, config_path.string());
                    }
                    const std::string as = (*table)["as"].value_or("");
                    amc::Hash128 type_id{};
                    bool found = false;
                    for (const auto &type : full.types)
                        if (type.name == as) {
                            type_id = type.id;
                            found = true;
                            break;
                        }
                    if (!found) {
                        error = "[[instantiate]] contract references an unknown concrete type: " + as;
                        return fail(amc::ErrorCategory::config, "build", error, config_path.string());
                    }
                    const std::string element_name = (*table)["element"].value_or(std::string{});
                    amc::Hash128 element_id{};
                    if (!element_name.empty()) {
                        element_id = amc::find_type_id_by_spelling(full, element_name);
                        if (element_id == amc::Hash128{}) {
                            error = "[[instantiate]] element type not found: " + element_name;
                            return fail(amc::ErrorCategory::config, "build", error, config_path.string());
                        }
                    }
                    const bool mutable_view = (*table)["mutable"].value_or(false);
                    amc::LoweringResult result =
                        string_view
                            ? amc::evaluate_string_view(full, type_id, element_id, element_name, mutable_view)
                            : amc::evaluate_sequence_view(full, type_id, element_id, element_name, mutable_view);
                    if (result.status == static_cast<uint32_t>(amc::LoweringStatus::direct)) {
                        bool replaced = false;
                        for (auto &entry2 : canonical)
                            if (entry2.first == result.kind) {
                                entry2.second = result.type_id;
                                replaced = true;
                            }
                        if (!replaced) canonical.emplace_back(result.kind, result.type_id);
                    }
                    results.push_back(result);
                }
                for (auto &result : results) {
                    if (result.status == static_cast<uint32_t>(amc::LoweringStatus::unsupported)) {
                        error = "an instantiation does not satisfy its ABIX-STD contract";
                        return fail(amc::ErrorCategory::validation, "build", error, config_path.string());
                    }
                    if (result.status == static_cast<uint32_t>(amc::LoweringStatus::needs_adapter)) {
                        amc::Hash128 target{};
                        for (const auto &entry2 : canonical)
                            if (entry2.first == result.kind) target = entry2.second;
                        if (target == amc::Hash128{}) {
                            error = "an ABIX-STD adapter requires a direct canonical instantiation";
                            return fail(amc::ErrorCategory::validation, "build", error, config_path.string());
                        }
                        result.contract_type = target;
                    }
                    full.lowerings.push_back(result);
                }
            }
            // Container -> sequence-view adapters (for example a vector-like type
            // exposed through data()/size()). The container layout is not exposed.
            if (const auto *views = config["view"].as_array()) {
                for (auto &&entry : *views) {
                    const auto *table = entry.as_table();
                    if (!table) continue;
                    const std::string type_name = (*table)["type"].value_or("");
                    if (type_name.empty()) continue;
                    const amc::Hash128 source = amc::find_type_id_by_spelling(full, type_name);
                    if (source == amc::Hash128{}) {
                        error = "[[view]] references an unknown type: " + type_name;
                        return fail(amc::ErrorCategory::config, "build", error, config_path.string());
                    }
                    amc::Hash128 target{};
                    for (const auto &entry2 : canonical)
                        if (entry2.first == static_cast<uint32_t>(amc::LoweringKind::sequence_view))
                            target = entry2.second;
                    if (target == amc::Hash128{}) {
                        error = "[[view]] needs a direct sequence-view instantiation to target";
                        return fail(amc::ErrorCategory::config, "build", error, config_path.string());
                    }
                    const std::string data_name = (*table)["data_accessor"].value_or("");
                    const std::string size_name = (*table)["size_accessor"].value_or("");
                    amc::Hash128 data_signature{}, size_signature{};
                    for (const auto &function : full.functions) {
                        if (function.name == data_name) data_signature = function.signature;
                        if (function.name == size_name) size_signature = function.signature;
                    }
                    if (data_signature == amc::Hash128{} || size_signature == amc::Hash128{}) {
                        error = "[[view]] accessors not found: " + data_name + " / " + size_name;
                        return fail(amc::ErrorCategory::config, "build", error, config_path.string());
                    }
                    full.view_adapters.push_back({source, target, data_signature, size_signature});
                    amc::LoweringResult lowering;
                    lowering.type_id = source;
                    lowering.contract_type = target;
                    lowering.kind = static_cast<uint32_t>(amc::LoweringKind::sequence_view);
                    lowering.status = static_cast<uint32_t>(amc::LoweringStatus::needs_adapter);
                    lowering.reason = static_cast<uint32_t>(amc::LoweringReason::layout);
                    full.lowerings.push_back(lowering);
                }
            }
            for (const auto &spec : exports) {
                const fs::path output = spec.output.is_absolute() ? spec.output : base / spec.output;
                fs::create_directories(output.parent_path(), ec);
                if (ec)
                    return fail(amc::ErrorCategory::io, "build", "cannot create export directory", output.string(),
                        ec.message());
                amc::AbiModule projected;
                if (!amc::project_symbols(full, spec.symbols, projected, error))
                    return fail(amc::ErrorCategory::validation, "project_symbols", error, config_path.string());
                if (!amc::write_abix(projected, output.string(), error))
                    return fail(amc::ErrorCategory::io, "write_abix", error, output.string());
                // AI-PM: emit the `.abix.meta` sidecar for symbol-server/CI indexing.
                if (!write_abix_meta(projected, output, error))
                    return fail(amc::ErrorCategory::io, "write_abix_meta", error, output.string() + ".meta");
            }
            fs::remove(temporary, ec);
            return 0;
        } catch (const toml::parse_error &error) {
            return fail(amc::ErrorCategory::config, "build", std::string(error.description()), config_path.string());
        }
    }
}

}   // namespace amc::cli
