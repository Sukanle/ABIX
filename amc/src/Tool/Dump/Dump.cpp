#include "AMC/Core/Core.h"
#include "AMC/Tool/Dump.h"

#include "AMC/Util/Error.h"

#include <fmt/format.h>
#include <fmt/os.h>

#include <exception>
#include <string>
#include <vector>

using namespace amc::dump;
using namespace amc::util;

namespace {
void print_usage() {
    fmt::print(stderr,
        "usage: amc-dump <file.abix> [--json <output.json>]\n"
        "       amc-dump diff <source.abix> <target.abix> [--json <output.json>]\n"
        "global options:\n"
        "  --error-format text|json   structured error envelope (default text)\n");
}
}   // namespace

int main(int argc, char **argv) {
    std::vector<std::string> filtered;
    filtered.emplace_back(argc > 0 ? argv[0] : "amc-dump");
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--error-format") {
            if (i + 1 >= argc) return fail(amc::ErrorCategory::usage, "options", "--error-format requires a value");
            if (!amc::parse_error_format(argv[++i], g_error_format))
                return fail(amc::ErrorCategory::usage, "options", "--error-format must be text or json");
            continue;
        }
        const std::string error_prefix = "--error-format=";
        if (argument.rfind(error_prefix, 0) == 0) {
            if (!amc::parse_error_format(argument.substr(error_prefix.size()), g_error_format))
                return fail(amc::ErrorCategory::usage, "options", "--error-format must be text or json");
            continue;
        }
        filtered.push_back(argument);
    }
    std::vector<char *> argv_storage;
    for (auto &argument : filtered)
        argv_storage.push_back(argument.data());
    const int arg_count = static_cast<int>(filtered.size());
    char **args = argv_storage.data();

    const bool diff = arg_count >= 2 && std::string(args[1]) == "diff";
    const bool json = (diff && arg_count == 6 && std::string(args[4]) == "--json")
                   || (!diff && arg_count == 4 && std::string(args[2]) == "--json");
    if ((diff && arg_count != 4 && !json) || (!diff && arg_count != 2 && !json)) {
        print_usage();
        return 2;
    }

    if (diff) {
        amc::AbiModule source, target, report;
        std::string error;
        if (!amc::read_abix(args[2], source, error))
            return fail(read_error_category(error), "read_abix", error, args[2]);
        if (!amc::read_abix(args[3], target, error))
            return fail(read_error_category(error), "read_abix", error, args[3]);
        if (!amc::build_compatibility(source, target, report, error))
            return fail(amc::ErrorCategory::compatibility, "build_compatibility", error);
        std::string text;
        if (!json) {
            write_diff_text(source, target, report, text);
            fmt::print("{}", text);
            return 0;
        }
        write_diff_json(source, target, report, text);
        try {
            auto output = fmt::output_file(args[5]);
            output.print("{}", text);
        } catch (const std::exception &e) {
            return fail(amc::ErrorCategory::io, "amc-dump", "cannot write JSON output", args[5], e.what());
        }
        return 0;
    }

    amc::AbiModule module;
    std::string error;
    if (!amc::read_abix(args[1], module, error)) return fail(read_error_category(error), "read_abix", error, args[1]);
    std::string text;
    if (!json) {
        write_text(module, text);
        fmt::print("{}", text);
        return 0;
    }
    write_json(module, text);
    try {
        auto output = fmt::output_file(args[3]);
        output.print("{}", text);
    } catch (const std::exception &e) {
        return fail(amc::ErrorCategory::io, "amc-dump", "cannot write JSON output", args[3], e.what());
    }
    return 0;
}
