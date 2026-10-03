// ABIX MCP server (AI-P2) — shared declarations.
//
// Cross-file state and entry points for the stdio JSON-RPC server. The
// implementation is split across mcp_state.cpp (knowledge-base state),
// mcp_tools.cpp (tool catalogue) and mcp_server.cpp (JSON-RPC plumbing).
#ifndef AMC_MCP_MCP_H
#define AMC_MCP_MCP_H

#include "AMC/Core/Core.h"
#include "AMC/Core/Json.h"
#include "AMC/Core/Metadata.h"
#include "AMC/Core/Query.h"
#include "AMC/Core/Verify.h"

#include "AMC/Util/Hex.h"
#include "AMC/Util/Names.h"

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include <fmt/format.h>

namespace amc::mcp {

// An artifact loaded once at startup. A knowledge base is just a list of these,
// so cross-module queries stay a matter of iterating the index rather than
// re-parsing files.
struct IndexedModule {
    std::string path;
    amc::AbiModule module;
};

extern std::string g_default_module;
extern std::vector<IndexedModule> g_index;

const IndexedModule *find_indexed(const std::string &path);

std::string arg_string(const amc::Json &args, const char *key);

bool json_from_string(const std::string &text, amc::Json &out, std::string &error);

bool load_module(const amc::Json &args, amc::AbiModule &module, std::string &error);

// --- tools ---------------------------------------------------------------

amc::Json tool_get_module(const amc::Json &args, std::string &error);
amc::Json tool_list_types(const amc::Json &args, std::string &error);
amc::Json tool_get_type(const amc::Json &args, std::string &error);
amc::Json tool_get_function(const amc::Json &args, std::string &error);
amc::Json tool_compare_abi(const amc::Json &args, std::string &error);
amc::Json tool_find_compatible(const amc::Json &args, std::string &error);
amc::Json tool_list_functions(const amc::Json &args, std::string &error);
amc::Json tool_get_layout(const amc::Json &args, std::string &error);
amc::Json tool_resolve_type(const amc::Json &args, std::string &error);
amc::Json tool_list_modules(const amc::Json &args, std::string &error);
amc::Json tool_search_type(const amc::Json &args, std::string &error);

amc::Json type_summary(const amc::Type &type);

// --- tool catalogue ------------------------------------------------------

amc::Json property(const char *type, const char *description);
amc::Json name_or_id_any_of();
amc::Json make_tool(const char *name, const char *description, amc::Json properties,
    std::vector<std::string> required, amc::Json any_of);
amc::Json tools_catalogue();

// --- JSON-RPC plumbing ---------------------------------------------------

amc::Json make_result(const amc::Json &id, amc::Json result);
amc::Json make_error(const amc::Json &id, long long code, const std::string &message);
void respond(amc::Json response);
amc::Json tool_call_result(amc::Json structured, bool is_error);
void handle_request(const amc::Json &request);

}   // namespace amc::mcp

#endif   // AMC_MCP_MCP_H
