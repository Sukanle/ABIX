#include "AMC/CLI/CLI.h"

#include <exception>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <fmt/os.h>

namespace amc::cli {

int cmd_context(char **args, int arg_count) {
    const std::string command = args[1];
    if (command == "context") {
        fs::path input;
        fs::path output;
        std::string format = "llm";
        bool include_names = true;
        for (int i = 2; i < arg_count; ++i) {
            const std::string argument = args[i];
            if ((argument == "--format" || argument == "-o") && i + 1 < arg_count) {
                const std::string value = args[++i];
                if (argument == "--format")
                    format = value;
                else
                    output = value;
            } else if (argument.rfind("--format=", 0) == 0) {
                format = argument.substr(std::string("--format=").size());
            } else if (argument == "--no-names") {
                include_names = false;
            } else if (input.empty()) {
                input = argument;
            } else {
                return usage_error("context accepts a single .abix input");
            }
        }
        if (input.empty()) return usage_error("context requires an input .abix file");
        if (format != "llm" && format != "json") return usage_error("context --format must be llm or json");

        amc::AbiModule module;
        std::string error;
        if (!amc::load_module_source(input.string(), module, error))
            return fail(read_error_category(error), "read_abix", error, input.string());

        const std::string rendered =
            format == "json" ? amc::context_to_json(module, include_names) : amc::context_to_llm(module, include_names);
        if (output.empty()) {
            fmt::print("{}", rendered);
            return 0;
        }
        try {
            auto file = fmt::output_file(output.string());
            file.print("{}", rendered);
        } catch (const std::exception &e) {
            return fail(amc::ErrorCategory::io, "context", "cannot write output", output.string(), e.what());
        }
        return 0;
    }
}

}   // namespace amc::cli
