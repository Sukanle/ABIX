// ABIX MCP server (AI-P2).
//
// Speaks the Model Context Protocol over stdio (newline-delimited JSON-RPC
// 2.0) and exposes ABIX Metadata queries as MCP tools. The tools are thin
// wrappers over the shared `AMC/src/Core/Query.cpp` engine, so the CLI, the MCP
// server and any future consumer answer from exactly one implementation.
//
// Usage:
//   amc-mcp [<file.abix|libfoo.so>...]  index one or more artifacts
//   amc-mcp --index <path>              add an artifact to the knowledge base
//   amc-mcp --abix <file.abix>          serve one module by default
//   amc-mcp --list-tools                print the tool catalogue and exit
//
// With more than one artifact the server becomes a small ABI knowledge base:
// `abix.list_modules` and `abix.search_type` span every indexed module, and
// `abix.find_compatible` can decide compatibility across the whole index.
#include "AMC/Tool/MCP.h"

using namespace amc::mcp;

namespace amc::mcp {
namespace {

void print_usage() {
    fmt::print(stderr,
        "usage: amc-mcp [<file.abix|libfoo.so>...] [--index <path>] "
        "[--abix <file.abix>] [--list-tools]\n");
}

bool load_index(const std::vector<std::string> &paths, std::string &error) {
    for (const auto &path : paths) {
        IndexedModule entry;
        entry.path = path;
        if (!amc::load_module_source(path, entry.module, error)) {
            error = "cannot read module '" + path + "': " + error;
            return false;
        }
        g_index.push_back(std::move(entry));
    }
    return true;
}

}   // namespace
}   // namespace amc::mcp

int main(int argc, char **argv) {
    std::vector<std::string> paths;
    std::string explicit_default;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if ((argument == "--abix" || argument == "-m") && i + 1 < argc) {
            explicit_default = argv[++i];
            paths.push_back(explicit_default);
        } else if (argument == "--index" && i + 1 < argc) {
            paths.push_back(argv[++i]);
        } else if (argument == "--list-tools") {
            fmt::print("{}\n", tools_catalogue().dump_pretty());
            return 0;
        } else if (argument == "--help" || argument == "-h") {
            print_usage();
            return 0;
        } else if (!argument.empty() && argument[0] != '-') {
            paths.push_back(argument);
        } else {
            print_usage();
            return 2;
        }
    }

    std::string load_error;
    if (!load_index(paths, load_error)) {
        fmt::print(stderr, "amc-mcp: {}\n", load_error);
        return 1;
    }
    g_default_module = !explicit_default.empty() ? explicit_default : (paths.empty() ? std::string() : paths.front());

    std::ios::sync_with_stdio(false);
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        amc::Json request;
        std::string error;
        if (!amc::parse_json(line, request, error)) {
            respond(make_error(amc::Json(), -32'700, "parse error: " + error));
            continue;
        }
        handle_request(request);
    }
    return 0;
}
