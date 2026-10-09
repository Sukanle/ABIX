#include "AMC/Tool/MCP.h"

namespace amc::mcp {

using namespace amc::util;

std::string g_default_module;
std::vector<IndexedModule> g_index;

const IndexedModule *find_indexed(const std::string &path) {
    for (const auto &entry : g_index)
        if (entry.path == path) return &entry;
    return nullptr;
}

std::string arg_string(const amc::Json &args, const char *key) {
    const amc::Json *value = args.find(key);
    return value != nullptr ? value->as_string() : std::string();
}

bool json_from_string(const std::string &text, amc::Json &out, std::string &error) {
    if (!amc::parse_json(text, out, error)) {
        error = "internal: " + error;
        return false;
    }
    return true;
}

bool load_module(const amc::Json &args, amc::AbiModule &module, std::string &error) {
    std::string path = arg_string(args, "module");
    if (path.empty()) path = g_default_module;
    if (path.empty() && !g_index.empty()) path = g_index.front().path;
    if (path.empty()) {
        error = "no module selected: pass 'module' or start amc-mcp with a .abix path";
        return false;
    }
    if (const IndexedModule *entry = find_indexed(path)) {
        module = entry->module;
        return true;
    }
    if (!amc::load_module_source(path, module, error)) {
        error = "cannot read module '" + path + "': " + error;
        return false;
    }
    return true;
}

}   // namespace amc::mcp
