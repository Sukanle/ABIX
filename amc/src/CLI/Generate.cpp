#include "AMC/CLI/CLI.h"

#include <exception>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <fmt/os.h>

namespace amc::cli {

int cmd_generate(char **args, int arg_count, const fs::path &provider) {
    const std::string command = args[1];
    if (command == "generate" || command == "backend") {
        if (arg_count != 7 || std::string(args[3]) != "-l" || std::string(args[5]) != "-o")
            return usage_error(command + " expects -l cpp|lua|rust -o <directory|file>");
        const std::string language = args[4];
        if (language == "lua") {
            if (command == "backend") return usage_error("backend is a C++ provider capability; use generate for lua");
            amc::AbiModule module;
            std::string error;
            if (!amc::load_module_source(args[2], module, error))
                return fail(read_error_category(error), "read_abix", error, args[2]);
            const fs::path requested = args[6];
            const bool conformance = requested.extension() == ".lua";
            const std::string contract =
                conformance ? amc::generate_lua_conformance(module, error) : amc::generate_lua_contract(module, error);
            if (!error.empty()) return fail(amc::ErrorCategory::internal, "generate_lua", error, args[2]);
            fs::path output = requested;
            if (output.extension() != ".hpp" && output.extension() != ".lua") output /= "amc_lua_contract.hpp";
            std::error_code ec;
            if (!output.parent_path().empty()) {
                fs::create_directories(output.parent_path(), ec);
                if (ec)
                    return fail(amc::ErrorCategory::io, "generate", "cannot create output directory", output.string(),
                        ec.message());
            }
            try {
                auto file = fmt::output_file(output.string());
                file.print("{}", contract);
            } catch (const std::exception &e) {
                return fail(amc::ErrorCategory::io, "generate", "cannot write output", output.string(), e.what());
            }
            return 0;
        }
        if (language == "cpp") {
            if (command == "backend") {
                std::string error;
                if (!dispatch_provider(provider, "backend", args[2], args[6], error))
                    return fail(amc::ErrorCategory::provider, "backend", error, args[2]);
                return 0;
            }
            return generate(provider, args[2], args[6], ".hpp");
        }
        if (language == "rust") {
            const fs::path rust_provider = provider_executable(provider, "rust");
            if (command == "backend") {
                std::string error;
                if (!dispatch_provider(rust_provider, "backend", args[2], args[6], error))
                    return fail(amc::ErrorCategory::provider, "backend", error, args[2]);
                return 0;
            }
            return generate(rust_provider, args[2], args[6], ".rs");
        }
        return usage_error(command + " expects -l cpp|lua|rust -o <directory|file>");
    }
}

int cmd_frontend(char **args, int arg_count, const fs::path &provider) {
    const std::string command = args[1];
    if (command == "frontend") {
        std::string language;
        fs::path config_path;
        fs::path output_path;
        for (int i = 2; i < arg_count; ++i) {
            const std::string argument = args[i];
            if (argument == "-l" && i + 1 < arg_count)
                language = args[++i];
            else if (argument == "-c" && i + 1 < arg_count)
                config_path = args[++i];
            else if (argument == "-o" && i + 1 < arg_count)
                output_path = args[++i];
            else
                return usage_error("frontend expects -l <language> -c <config> -o <output>");
        }
        if (language.empty() || config_path.empty() || output_path.empty())
            return usage_error("frontend expects -l <language> -c <config> -o <output>");
        std::string error;
        const fs::path selected = provider_executable(provider, language);
        if (!dispatch_provider(selected, "frontend", config_path, output_path, error))
            return fail(amc::ErrorCategory::provider, "frontend", error, config_path.string());
        return 0;
    }
}

}   // namespace amc::cli
