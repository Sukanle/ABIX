#include "AMC/Lang/Lang.h"

#include <cstdio>
#include <iostream>

#include <fmt/format.h>
#include <toml++/toml.h>

namespace amc::lang {

bool config_load(const std::string &path, const std::string &expected_language, Config &c, std::string &e) {
    try {
        auto t = toml::parse_file(path);
        auto *imp = t["import"].as_array();
        if (!imp || imp->empty()) {
            e = "configuration needs [[import]]";
            return false;
        }
        if (imp->size() != 1) {
            e = "MVP supports exactly one [[import]]";
            return false;
        }
        auto *tab = (*imp)[0].as_table();
        c.language = tab ? tab->get("language")->value_or(std::string{}) : std::string{};
        if (!tab || (!expected_language.empty() && c.language != expected_language)) {
            e = "only " + (expected_language.empty() ? std::string("the provider") : expected_language)
                + " import is supported";
            return false;
        }
        c.db = (*tab)["compile_commands"].value_or("");
        auto *files = (*tab)["files"].as_array();
        auto *symbols = (*tab)["symbols"].as_array();
        auto *flags = (*tab)["flags"].as_array();
        if (flags)
            for (auto &&v : *flags)
                c.flags.push_back(v.value_or(""));
        if (files)
            for (auto &&v : *files)
                c.files.push_back(v.value_or(""));
        if (symbols)
            for (auto &&v : *symbols)
                c.symbols.push_back(v.value_or(""));
        if (auto *ex = t["export"].as_array(); ex && !ex->empty())
            if (auto *et = (*ex)[0].as_table()) c.output = (*et)["output"].value_or("");
        if ((c.db.empty() && c.flags.empty()) || c.files.empty() || c.symbols.empty()) {
            e = c.language + " import requires compile_commands or flags, files and symbols";
            return false;
        }
        return true;
    } catch (const toml::parse_error &x) {
        e = x.description();
        return false;
    }
}

std::string json_value(const std::string &line, const char *key) {
    const std::string needle = std::string("\"") + key + "\":\"";
    const auto begin = line.find(needle);
    if (begin == std::string::npos) return {};
    const auto start = begin + needle.size();
    const auto end = line.find('"', start);
    return end == std::string::npos ? std::string{} : line.substr(start, end - start);
}

int run_provider(int argc, char **argv, const Provider &provider) {
    if (argc == 2 && std::string(argv[1]) == "--ipc") return ipc(provider);
    if (argc != 4) {
        std::string names;
        for (const auto &capability : provider.capabilities) {
            if (!names.empty()) names += "|";
            names += capability.name;
        }
        fmt::print(
            stderr, "usage: amc-{} <{}> <input> <output>\n", provider.language, names);
        return 2;
    }
    const std::string capability = argv[1];
    for (const auto &candidate : provider.capabilities)
        if (candidate.name == capability) return candidate.run(argv[2], argv[3]);
    fmt::print(stderr, "unknown capability\n");
    return 2;
}

int ipc(const Provider &provider) {
    std::string capability_names;
    for (const auto &capability : provider.capabilities) {
        if (!capability_names.empty()) capability_names += ",";
        capability_names += "\"" + capability.name + "\"";
    }
    std::string line, capability, input, output;
    bool initialized = false;
    while (std::getline(std::cin, line)) {
        if (line.find(R"("type":"INIT")") != std::string::npos) {
            initialized = line.find(R"("protocol":1)") != std::string::npos;
            fmt::println(R"({{"type":"READY","protocol":1}})");
            std::fflush(stdout);
        } else if (line.find(R"("type":"QUERY_CAPABILITIES")") != std::string::npos) {
            if (!initialized) return 2;
            fmt::println(R"({{"type":"CAPABILITIES","language":"{}","capabilities":[{}]}})", provider.language,
                capability_names);
            std::fflush(stdout);
        } else if (line.find(R"("type":"ANALYZE")") != std::string::npos) {
            capability = json_value(line, "capability");
            input = json_value(line, "input");
            output = json_value(line, "output");
            int result = 2;
            for (const auto &candidate : provider.capabilities) {
                if (candidate.name != capability) continue;
                result = candidate.run(input.c_str(), output.c_str());
                break;
            }
            if (result == 0) fmt::println(R"({{"type":"ABI_MODULE","path":"{}"}})", output);
            fmt::println(R"({{"type":"ANALYZE_RESULT","status":{}}})", result);
            std::fflush(stdout);
            if (result != 0) return result;
        } else if (line.find(R"("type":"DONE")") != std::string::npos)
            return 0;
    }
    return 2;
}

}   // namespace amc::lang
