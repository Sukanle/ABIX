#include "AMC/CLI/CLI.h"

#include <filesystem>
#include <string>
#include <vector>

#include <fmt/format.h>

int main(int argc, char **argv) {
    using namespace amc::cli;

    // Strip the global --error-format option first so every subcommand sees a
    // clean argument vector, and so failures during the scan itself are
    // reported through the selected format.
    std::vector<std::string> filtered;
    filtered.emplace_back(argc > 0 ? argv[0] : "amc");
    bool explicit_error_format = false;
    bool format_json = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--error-format") {
            if (i + 1 >= argc) {
                fail(amc::ErrorCategory::usage, "options", "--error-format requires a value");
                return 2;
            }
            explicit_error_format = true;
            if (!amc::parse_error_format(argv[++i], g_error_format)) {
                fail(amc::ErrorCategory::usage, "options", "--error-format must be text or json");
                return 2;
            }
            continue;
        }
        const std::string error_prefix = "--error-format=";
        if (argument.rfind(error_prefix, 0) == 0) {
            explicit_error_format = true;
            if (!amc::parse_error_format(argument.substr(error_prefix.size()), g_error_format)) {
                fail(amc::ErrorCategory::usage, "options", "--error-format must be text or json");
                return 2;
            }
            continue;
        }
        if (argument == "--format" && i + 1 < argc && std::string(argv[i + 1]) == "json") format_json = true;
        if (argument == "--format=json") format_json = true;
        filtered.push_back(argument);
    }
    if (!explicit_error_format && format_json) g_error_format = amc::ErrorFormat::json;

    std::vector<char *> argv_storage;
    argv_storage.reserve(filtered.size());
    for (auto &argument : filtered)
        argv_storage.push_back(argument.data());
    const int arg_count = static_cast<int>(filtered.size());
    char **args = argv_storage.data();

    if (arg_count < 2) {
        print_usage();
        return 2;
    }
    const std::string command = args[1];
    const fs::path provider = fs::absolute(fs::path(args[0])).parent_path() / "amc-cpp";

    if (command == "--list-languages") {
        fmt::print("cpp\nlua\nrust\n");
        return 0;
    }
    if (command == "--describe-language") {
        if (arg_count != 3) return usage_error("--describe-language requires a language");
        const std::string language = args[2];
        if (language == "cpp") {
            fmt::print("cpp frontend backend protocol=jsonl-v1\n");
            return 0;
        }
        if (language == "lua") {
            fmt::print("lua backend protocol=direct contract=aue\n");
            return 0;
        }
        if (language == "rust") {
            fmt::print("rust frontend backend protocol=jsonl-v1\n");
            return 0;
        }
        return usage_error("--describe-language supports cpp, lua and rust");
    }

    if (command == "context") return cmd_context(args, arg_count);
    if (command == "metadata") return cmd_metadata(args, arg_count);
    if (command == "adapter") return cmd_adapter(args, arg_count);
    if (command == "query") return cmd_query(args, arg_count);
    if (command == "publish" || command == "fetch") return cmd_symbols(args, arg_count);
    if (command == "verify") return cmd_verify(args, arg_count, provider);
    if (command == "diff" || command == "compatibility") return cmd_diff(args, arg_count);
    if (command == "generate" || command == "backend") return cmd_generate(args, arg_count, provider);
    if (command == "frontend") return cmd_frontend(args, arg_count, provider);
    if (command == "build") return cmd_build(args, arg_count, provider);
    if (command == "inspect" || command == "validate") return cmd_inspect(args, arg_count);

    print_usage();
    return 2;
}
