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
            return usage_error(command + " expects -l cpp|lua -o <directory|file>");
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
        if (language != "cpp") return usage_error(command + " expects -l cpp|lua -o <directory|file>");
        if (command == "backend") {
            std::string error;
            if (!dispatch_provider(provider, "backend", args[2], args[6], error))
                return fail(amc::ErrorCategory::provider, "backend", error, args[2]);
            return 0;
        }
        return generate(provider, args[2], args[6]);
    }
}

int cmd_frontend(char **args, int arg_count, const fs::path &provider) {
    const std::string command = args[1];
    if (command == "frontend") {
        if (arg_count != 8
            || std::string(args[2]) != "-l"
            || std::string(args[3]) != "cpp"
            || std::string(args[4]) != "-c"
            || std::string(args[6]) != "-o")
            return usage_error("frontend expects -l cpp -c <config> -o <output>");
        std::string error;
        if (!dispatch_provider(provider, "frontend", args[5], args[7], error))
            return fail(amc::ErrorCategory::provider, "frontend", error, args[5]);
        return 0;
    }
}

}   // namespace amc::cli
