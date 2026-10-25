#include "SKL/AMC/CLI/CLI.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "SKL/AMC/Core/Cache.h"

namespace amc::cli {

void apply_target_and_layout(const toml::table &config, amc::AbiModule &module) {
    if (const auto *target = config["target"].as_table()) {
        if (auto value = (*target)["arch"].value<int64_t>()) module.arch = static_cast<uint32_t>(*value);
        if (auto value = (*target)["os"].value<int64_t>()) module.os = static_cast<uint32_t>(*value);
        if (auto value = (*target)["abi"].value<int64_t>()) module.target_abi = static_cast<uint32_t>(*value);
        if (auto value = (*target)["pointer_bits"].value<int64_t>())
            module.pointer_bits = static_cast<uint32_t>(*value);
        const std::string endianness = (*target)["endianness"].value_or(std::string{});
        if (!endianness.empty()) module.endianness = endianness == "little" ? 1u : (endianness == "big" ? 2u : 0u);
    }
    amc::recompute_layout_hashes(module);
}

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

// Resolve `[[interface]]` and `[[implements]]` declarations into the
// non-intrusive interface contract. An interface is an independent, named,
// versioned contract whose identity is a content hash over its name, version
// and ordered method signatures. A concrete type implements it through an
// adapter binding per method; no language's private vtable layout is used.
//
//   [[interface]]
//   name = "IRenderer"
//   version = 1
//   methods = ["draw", "resize"]          # function names, order = slot order
//
//   [[implements]]
//   interface = "IRenderer"
//   type = "Renderer"
//   methods = ["Renderer_draw", "Renderer_resize"]
bool resolve_interfaces(const toml::table &config, amc::AbiModule &module, std::string &error) {
    const auto *interfaces = config["interface"].as_array();
    if (interfaces) {
        for (auto &&entry : *interfaces) {
            const auto *table = entry.as_table();
            if (!table) continue;
            const std::string name = (*table)["name"].value_or("");
            if (name.empty()) {
                error = "[[interface]] requires a name";
                return false;
            }
            for (const auto &existing : module.interfaces)
                if (existing.name == name) {
                    error = "duplicate [[interface]]: " + name;
                    return false;
                }
            const auto *methods = (*table)["methods"].as_array();
            if (!methods || methods->empty()) {
                error = "[[interface]] requires at least one method: " + name;
                return false;
            }
            amc::InterfaceRecord record;
            record.name = name;
            record.version = static_cast<uint32_t>((*table)["version"].value_or(int64_t{1}));
            record.method_begin = static_cast<uint32_t>(module.interface_methods.size());
            record.method_count = 0;
            for (auto &&value : *methods) {
                const std::string method_name = value.value_or("");
                const amc::Function *function = nullptr;
                for (const auto &candidate : module.functions)
                    if (candidate.name == method_name) {
                        function = &candidate;
                        break;
                    }
                if (!function) {
                    error = "[[interface]] method references an unknown function: " + method_name;
                    return false;
                }
                amc::InterfaceMethod method;
                method.interface_id = Hash128{};   // filled once the id is known
                method.name = function->name;
                method.signature = function->signature;
                method.return_type = function->return_type;
                method.parameters = function->parameters;
                method.slot = record.method_count;
                module.interface_methods.push_back(std::move(method));
                ++record.method_count;
            }
            record.id = amc::interface_hash(record, module.interface_methods);
            for (uint32_t i = 0; i < record.method_count; ++i)
                module.interface_methods[record.method_begin + i].interface_id = record.id;
            module.interfaces.push_back(record);
        }
    }
    const auto *implementations = config["implements"].as_array();
    if (implementations) {
        for (auto &&entry : *implementations) {
            const auto *table = entry.as_table();
            if (!table) continue;
            const std::string interface_name = (*table)["interface"].value_or("");
            const std::string type_name = (*table)["type"].value_or("");
            if (interface_name.empty() || type_name.empty()) {
                error = "[[implements]] requires an interface and a type";
                return false;
            }
            const amc::InterfaceRecord *contract = nullptr;
            for (const auto &candidate : module.interfaces)
                if (candidate.name == interface_name) {
                    contract = &candidate;
                    break;
                }
            if (!contract) {
                error = "[[implements]] references an unknown interface: " + interface_name;
                return false;
            }
            const amc::Hash128 concrete = amc::find_type_id_by_spelling(module, type_name);
            if (concrete == Hash128{}) {
                error = "[[implements]] references an unknown type: " + type_name;
                return false;
            }
            // Interface methods ordered by slot.
            std::vector<const amc::InterfaceMethod *> ordered;
            for (uint32_t i = 0; i < contract->method_count; ++i)
                ordered.push_back(&module.interface_methods[contract->method_begin + i]);
            std::sort(ordered.begin(), ordered.end(),
                [](const amc::InterfaceMethod *a, const amc::InterfaceMethod *b) { return a->slot < b->slot; });
            const auto *methods = (*table)["methods"].as_array();
            if (!methods || methods->size() != ordered.size()) {
                error = "[[implements]] must bind every interface method of " + interface_name;
                return false;
            }
            amc::InterfaceImplementation implementation;
            implementation.interface_id = contract->id;
            implementation.concrete_type = concrete;
            implementation.binding_begin = static_cast<uint32_t>(module.method_bindings.size());
            implementation.binding_count = 0;
            size_t position = 0;
            for (auto &&value : *methods) {
                const std::string function_name = value.value_or("");
                const amc::Function *function = nullptr;
                for (const auto &candidate : module.functions)
                    if (candidate.name == function_name) {
                        function = &candidate;
                        break;
                    }
                if (!function) {
                    error = "[[implements]] method references an unknown function: " + function_name;
                    return false;
                }
                amc::MethodBinding binding;
                binding.interface_id = contract->id;
                binding.method_signature = ordered[position]->signature;
                binding.slot = ordered[position]->slot;
                binding.native_function = function->signature;
                module.method_bindings.push_back(binding);
                ++implementation.binding_count;
                ++position;
            }
            module.implementations.push_back(implementation);
        }
    }
    return true;
}

// Parse a make-style depfile (`target: dep1 dep2 \` with line continuations
// and backslash-escaped spaces) into its dependency paths. This is the include
// closure the C++ frontend reports, and it is what makes a header change
// invalidate the cache without it being named anywhere in the config.
std::vector<std::string> parse_makefile_dependencies(const std::string &text) {
    std::vector<std::string> paths;
    const size_t colon = text.find(':');
    std::string dependencies = colon == std::string::npos ? text : text.substr(colon + 1);
    std::string token;
    bool escaped = false;
    const auto flush = [&]() {
        if (!token.empty()) {
            paths.push_back(token);
            token.clear();
        }
    };
    for (const char c : dependencies) {
        if (escaped) {
            if (c == '\n' || c == '\r') {
                // line continuation: a separator, not part of a token
                flush();
            } else {
                token.push_back(c);
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            flush();
        } else {
            token.push_back(c);
        }
    }
    flush();
    return paths;
}

bool copy_file(const fs::path &from, const fs::path &to, std::string &error) {
    std::ifstream input(from, std::ios::binary);
    if (!input) {
        error = "cannot read cached artifact: " + from.string();
        return false;
    }
    std::ofstream output(to, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot write frontend artifact: " + to.string();
        return false;
    }
    output << input.rdbuf();
    return static_cast<bool>(output);
}

}   // namespace

int cmd_build(char **args, int arg_count, const fs::path &provider) {
    const std::string command = args[1];
    if (command == "build") {
        fs::path config_path;
        fs::path build_dir;
        fs::path cache_dir;
        std::vector<std::string> cli_deps;
        for (int i = 2; i < arg_count; ++i) {
            const std::string argument = args[i];
            if ((argument == "-c" || argument == "-B" || argument == "--cache") && i + 1 < arg_count) {
                const fs::path value = args[++i];
                if (argument == "-c")
                    config_path = value;
                else if (argument == "-B")
                    build_dir = fs::absolute(value);
                else
                    cache_dir = fs::absolute(value);
            } else if (argument == "--deps" && i + 1 < arg_count) {
                std::string list = args[++i];
                size_t start = 0;
                while (start <= list.size()) {
                    const size_t comma = list.find(',', start);
                    const std::string item = list.substr(start, comma == std::string::npos ? comma : comma - start);
                    if (!item.empty()) cli_deps.push_back(item);
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
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

            // M3.4: reuse a cached frontend artifact when every input that can
            // change it is identical. The source digest covers the declared
            // files and the optional `[cache].deps` / `--deps` list, so a header
            // whose change can alter the ABI must be declared as a dependency.
            if (cache_dir.empty())
                if (const auto *cache_table = config["cache"].as_table())
                    cache_dir = fs::absolute((*cache_table)["dir"].value_or(std::string{}));
            std::vector<std::string> dep_files = cli_deps;
            if (const auto *cache_table = config["cache"].as_table())
                if (const auto *deps = (*cache_table)["deps"].as_array())
                    for (auto &&value : *deps) {
                        const std::string dep = value.value_or("");
                        if (!dep.empty()) dep_files.push_back(dep);
                    }

            uint32_t key_arch = 0, key_os = 0, key_abi = 0, key_pointer_bits = 0, key_endianness = 0;
            if (const auto *target = config["target"].as_table()) {
                if (auto value = (*target)["arch"].value<int64_t>()) key_arch = static_cast<uint32_t>(*value);
                if (auto value = (*target)["os"].value<int64_t>()) key_os = static_cast<uint32_t>(*value);
                if (auto value = (*target)["abi"].value<int64_t>()) key_abi = static_cast<uint32_t>(*value);
                if (auto value = (*target)["pointer_bits"].value<int64_t>())
                    key_pointer_bits = static_cast<uint32_t>(*value);
                const std::string endianness = (*target)["endianness"].value_or(std::string{});
                if (!endianness.empty()) key_endianness = endianness == "little" ? 1u : (endianness == "big" ? 2u : 0u);
            }

            amc::InstantiationBuildInputs cache_inputs;
            const bool use_cache = !cache_dir.empty();
            bool frontend_ran = true;
            amc::InstantiationCache cache(cache_dir.string());
            // Source files and dependencies are declared relative to the config,
            // not to the build output directory.
            const fs::path source_base = fs::absolute(config_path).parent_path();
            if (use_cache) {
                // The key identifies the project (declared inputs + flags +
                // target), not their contents: content changes are caught by
                // dependency verification, which covers the frontend's whole
                // include closure.
                std::string identity = language;
                identity.push_back('\x1f');
                if (import_table)
                    if (const auto *files = (*import_table)["files"].as_array())
                        for (auto &&value : *files) {
                            const std::string file = value.value_or("");
                            if (file.empty()) continue;
                            identity += (source_base / file).string();
                            identity.push_back('\n');
                        }
                if (import_table) {
                    const std::string db = (*import_table)["db"].value_or("");
                    if (!db.empty()) identity += (source_base / db).string();
                }
                for (const auto &dep : dep_files) {
                    identity += (source_base / dep).string();
                    identity.push_back('\n');
                }
                std::string flags_blob;
                if (import_table)
                    if (const auto *flags = (*import_table)["flags"].as_array())
                        for (auto &&value : *flags) {
                            flags_blob += value.value_or("");
                            flags_blob.push_back('\n');
                        }
                cache_inputs.request.template_name = "amc.build";
                cache_inputs.request.arguments.push_back(language);
                cache_inputs.request.arch = key_arch;
                cache_inputs.request.os = key_os;
                cache_inputs.request.target_abi = key_abi;
                cache_inputs.request.pointer_bits = key_pointer_bits;
                cache_inputs.source_digest = amc::digest_text(identity, amc::digest_domain_source);
                cache_inputs.toolchain =
                    amc::digest_text(language + "\x1f" + provider.string(), amc::digest_domain_toolchain);
                cache_inputs.build_options = amc::digest_text(flags_blob
                                                                  + "\x1f"
                                                                  + std::to_string(key_arch)
                                                                  + ":"
                                                                  + std::to_string(key_os)
                                                                  + ":"
                                                                  + std::to_string(key_abi)
                                                                  + ":"
                                                                  + std::to_string(key_pointer_bits)
                                                                  + ":"
                                                                  + std::to_string(key_endianness),
                    amc::digest_domain_options);
                const auto cached = cache.lookup(cache_inputs);
                if (cached.status == amc::CacheStatus::hit) {
                    for (const auto &artifact : cached.artifacts)
                        if (artifact.name == "frontend.abix") {
                            if (!copy_file(fs::path(cached.directory) / artifact.name, temporary, error))
                                return fail(amc::ErrorCategory::io, "cache", error, config_path.string());
                            frontend_ran = false;
                        }
                }
            }
            if (frontend_ran) {
                if (!dispatch_provider(frontend, "frontend", config_path, temporary, error))
                    return fail(amc::ErrorCategory::provider, "frontend", error, config_path.string());
            }

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
            if (!resolve_interfaces(config, full, error))
                return fail(amc::ErrorCategory::config, "build", error, config_path.string());
            apply_target_and_layout(config, full);
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
                    // Instantiation request identity: template + canonical args +
                    // target + frontend. Not the concrete ABI identity.
                    std::string request_key = (*table)["template"].value_or(std::string{}) + "\x1f";
                    if (const auto *args = (*table)["args"].as_array())
                        for (auto &&value : *args) {
                            request_key += value.value_or("");
                            request_key += ",";
                        }
                    request_key += "\x1f"
                                 + std::to_string(full.arch)
                                 + ":"
                                 + std::to_string(full.os)
                                 + ":"
                                 + std::to_string(full.target_abi)
                                 + ":"
                                 + std::to_string(full.pointer_bits);
                    full.instantiations.push_back({amc::hash_text(request_key, 0x494e5354), type_id});
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
                    amc::LoweringResult result;
                    if (selected_contract == "sequence-view" || selected_contract == "abix-std.sequence-view")
                        result = amc::evaluate_sequence_view(full, type_id, element_id, element_name, mutable_view);
                    else if (selected_contract == "string-view" || selected_contract == "abix-std.string-view")
                        result = amc::evaluate_string_view(full, type_id, element_id, element_name, mutable_view);
                    else if (selected_contract == "optional" || selected_contract == "abix-std.optional")
                        result = amc::evaluate_optional(full, type_id, element_id, element_name);
                    else if (selected_contract == "result" || selected_contract == "abix-std.result")
                        result = amc::evaluate_result(full, type_id, element_id, element_name);
                    else if (selected_contract == "callback" || selected_contract == "abix-std.callback")
                        result = amc::evaluate_callback(full, type_id);
                    else {
                        error = "unsupported ABIX-STD contract: " + selected_contract;
                        return fail(amc::ErrorCategory::config, "build", error, config_path.string());
                    }
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
            // Non-template contract types (for example a callback struct).
            if (const auto *types = config["contract-type"].as_array()) {
                for (auto &&entry : *types) {
                    const auto *table = entry.as_table();
                    if (!table) continue;
                    const std::string type_name = (*table)["type"].value_or("");
                    if (type_name.empty()) continue;
                    const std::string contract = (*table)["contract"].value_or("");
                    const amc::Hash128 source = amc::find_type_id_by_spelling(full, type_name);
                    if (source == amc::Hash128{}) {
                        error = "[[contract-type]] references an unknown type: " + type_name;
                        return fail(amc::ErrorCategory::config, "build", error, config_path.string());
                    }
                    amc::LoweringResult result;
                    if (contract == "callback" || contract == "abix-std.callback")
                        result = amc::evaluate_callback(full, source);
                    else if (contract == "opaque" || contract == "abix-std.opaque")
                        result = amc::evaluate_opaque(full, source);
                    else {
                        error = "unsupported ABIX-STD contract: " + contract;
                        return fail(amc::ErrorCategory::config, "build", error, config_path.string());
                    }
                    if (result.status == static_cast<uint32_t>(amc::LoweringStatus::unsupported)) {
                        error = "a contract type does not satisfy its ABIX-STD contract";
                        return fail(amc::ErrorCategory::validation, "build", error, config_path.string());
                    }
                    full.lowerings.push_back(result);
                }
            }
            // Non-contiguous containers exposed as an iterable range.
            if (const auto *ranges = config["range"].as_array()) {
                for (auto &&entry : *ranges) {
                    const auto *table = entry.as_table();
                    if (!table) continue;
                    const std::string type_name = (*table)["type"].value_or("");
                    if (type_name.empty()) continue;
                    const amc::Hash128 source = amc::find_type_id_by_spelling(full, type_name);
                    if (source == amc::Hash128{}) {
                        error = "[[range]] references an unknown type: " + type_name;
                        return fail(amc::ErrorCategory::config, "build", error, config_path.string());
                    }
                    const std::string size_name = (*table)["size_accessor"].value_or("");
                    const std::string at_name = (*table)["at_accessor"].value_or("");
                    amc::Hash128 size_signature{}, at_signature{};
                    for (const auto &function : full.functions) {
                        if (function.name == size_name) size_signature = function.signature;
                        if (function.name == at_name) at_signature = function.signature;
                    }
                    if (size_signature == amc::Hash128{} || at_signature == amc::Hash128{}) {
                        error = "[[range]] accessors not found: " + size_name + " / " + at_name;
                        return fail(amc::ErrorCategory::config, "build", error, config_path.string());
                    }
                    full.range_adapters.push_back({source, size_signature, at_signature});
                    amc::LoweringResult lowering;
                    lowering.type_id = source;
                    lowering.contract_type = source;
                    lowering.kind = static_cast<uint32_t>(amc::LoweringKind::range);
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
            // Populate the cache with the frontend artifact and its full
            // dependency closure, so a later build with unchanged inputs can
            // reuse it and any changed header invalidates it. The cache is
            // advisory: a store failure never fails the build.
            if (use_cache && frontend_ran) {
                std::vector<amc::CacheDependency> dependencies;
                const auto add_dependency = [&](const fs::path &path) {
                    const fs::path resolved = path.is_absolute() ? path : source_base / path;
                    std::ifstream input(resolved, std::ios::binary);
                    if (!input) return;
                    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
                    dependencies.push_back({resolved.string(),
                        amc::digest_bytes(content.data(), content.size(), amc::digest_domain_dependency)});
                };
                if (import_table)
                    if (const auto *files = (*import_table)["files"].as_array())
                        for (auto &&value : *files) {
                            const std::string file = value.value_or("");
                            if (!file.empty()) add_dependency(file);
                        }
                for (const auto &dep : dep_files)
                    add_dependency(dep);
                std::ifstream depfile(temporary.string() + ".d", std::ios::binary);
                if (depfile) {
                    std::string text((std::istreambuf_iterator<char>(depfile)), std::istreambuf_iterator<char>());
                    for (const auto &path : parse_makefile_dependencies(text))
                        add_dependency(path);
                }
                std::ifstream cached_output(temporary, std::ios::binary);
                if (cached_output) {
                    std::string content(
                        (std::istreambuf_iterator<char>(cached_output)), std::istreambuf_iterator<char>());
                    std::string store_error;
                    (void)cache.store(cache_inputs,
                        {
                            {"frontend.abix", content}
                    },
                        dependencies, store_error);
                }
            }
            fs::remove(temporary, ec);
            fs::remove(temporary.string() + ".d", ec);
            return 0;
        } catch (const toml::parse_error &error) {
            return fail(amc::ErrorCategory::config, "build", std::string(error.description()), config_path.string());
        }
    }
    return usage_error(command + ": unsupported subcommand");
}

}   // namespace amc::cli
