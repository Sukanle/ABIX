#include "SKL/AMC/CLI/CLI.h"

#include <string>
#include <vector>

#include <fmt/format.h>

namespace amc::cli {

// `amc cache <dir> [--verify|--clear] [--format text|json]`
//
// Inspects the M3.4 instantiation request cache: entry count, integrity
// verification, or clearing. Building is what populates the cache; this command
// is the diagnostic and CI surface over it.
int cmd_cache(char **args, int arg_count) {
    fs::path root;
    bool verify = false;
    bool clear = false;
    bool json = false;
    for (int i = 2; i < arg_count; ++i) {
        const std::string argument = args[i];
        if (argument == "--verify") {
            verify = true;
        } else if (argument == "--clear") {
            clear = true;
        } else if (argument == "--format" && i + 1 < arg_count) {
            const std::string value = args[++i];
            if (value != "text" && value != "json") return usage_error("--format must be text or json");
            json = value == "json";
        } else if (argument.rfind("--format=", 0) == 0) {
            const std::string value = argument.substr(std::string("--format=").size());
            if (value != "text" && value != "json") return usage_error("--format must be text or json");
            json = value == "json";
        } else if (root.empty()) {
            root = argument;
        } else {
            return usage_error("cache accepts a single directory");
        }
    }
    if (root.empty()) return usage_error("cache requires a directory");

    amc::InstantiationCache cache(root.string());

    if (clear) {
        const size_t removed = cache.clear();
        if (json)
            fmt::print("{{\"schema\":\"abix.cache/1\",\"cleared\":{}}}\n", removed);
        else
            fmt::print("cache cleared: {} entries\n", removed);
        return 0;
    }

    if (verify) {
        std::vector<std::string> failures;
        const size_t checked = cache.verify_all(failures);
        if (json) {
            fmt::print("{{\"schema\":\"abix.cache/1\",\"checked\":{},\"failures\":[", checked);
            for (size_t i = 0; i < failures.size(); ++i)
                fmt::print("{}{}", i == 0 ? "" : ",", json_quote(failures[i]));
            fmt::print("]}}\n");
        } else {
            for (const auto &failure : failures)
                fmt::print("corrupt: {}\n", failure);
            fmt::print("cache verified: {} entries, {} failure(s)\n", checked, failures.size());
        }
        return failures.empty() ? 0 : 1;
    }

    const size_t entries = cache.entries();
    if (json)
        fmt::print("{{\"schema\":\"abix.cache/1\",\"root\":{},\"entries\":{}}}\n", json_quote(root.string()), entries);
    else
        fmt::print("cache {}: {} entries\n", root.string(), entries);
    return 0;
}

}   // namespace amc::cli
