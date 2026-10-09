#include "AMC/CLI/CLI.h"

#include <string>
#include <vector>

#include <fmt/format.h>

namespace amc::cli {

int cmd_query(char **args, int arg_count) {
    const std::string command = args[1];
    if (command == "query") {
        fs::path input;
        fs::path other;
        std::string needle;
        std::string mode;
        std::string format = "text";
        bool include_layout = false;
        for (int i = 2; i < arg_count; ++i) {
            const std::string argument = args[i];
            if ((argument == "--type"
                    || argument == "--function"
                    || argument == "--compatible"
                    || argument == "--format")
                && i + 1 < arg_count) {
                const std::string value = args[++i];
                if (argument == "--type") {
                    mode = "type";
                    needle = value;
                } else if (argument == "--function") {
                    mode = "function";
                    needle = value;
                } else if (argument == "--compatible") {
                    mode = "compatible";
                    other = value;
                } else
                    format = value;
            } else if (argument == "--layout") {
                include_layout = true;
            } else if (argument.rfind("--format=", 0) == 0) {
                format = argument.substr(std::string("--format=").size());
            } else if (input.empty()) {
                input = argument;
            } else {
                return usage_error("query accepts a single .abix input");
            }
        }
        if (input.empty()) return usage_error("query requires a .abix input");
        if (mode.empty()) return usage_error("query requires --type, --function or --compatible");
        if (format != "text" && format != "json") return usage_error("query --format must be text or json");
        const bool json = format == "json";

        amc::AbiModule module;
        std::string error;
        if (!amc::load_module_source(input.string(), module, error))
            return fail(read_error_category(error), "read_abix", error, input.string());

        if (mode == "type") {
            const auto indices = amc::query_type_indices(module, needle);
            fmt::print("{}", json ? amc::query_types_json(module, indices, include_layout, needle)
                                  : amc::query_types_text(module, indices, include_layout));
            return indices.empty() ? 1 : 0;
        }
        if (mode == "function") {
            const auto indices = amc::query_function_indices(module, needle);
            fmt::print("{}", json ? amc::query_functions_json(module, indices, needle)
                                  : amc::query_functions_text(module, indices));
            return indices.empty() ? 1 : 0;
        }

        amc::AbiModule target;
        if (!amc::load_module_source(other.string(), target, error))
            return fail(read_error_category(error), "read_abix", error, other.string());
        amc::VerifyResult result;
        if (!amc::verify_modules(module, target, result, error))
            return fail(amc::ErrorCategory::internal, "verify", error);
        fmt::print("{}", json ? amc::compatibility_query_json(module, target, result)
                              : amc::compatibility_query_text(module, target, result));
        return result.consistent ? 0 : 1;
    }
}

int cmd_inspect(char **args, int arg_count) {
    const std::string command = args[1];
    if (command == "inspect" || command == "validate") {
        if (arg_count != 3) return usage_error(command + " expects exactly one .abix input");
        amc::AbiModule module;
        std::string error;
        if (!amc::load_module_source(args[2], module, error))
            return fail(read_error_category(error), "read_abix", error, args[2]);
        if (command == "inspect")
            fmt::print("package={} types={} fields={} functions={}\n", module.package_name, module.types.size(),
                module.fields.size(), module.functions.size());
        return 0;
    }
}

}   // namespace amc::cli
