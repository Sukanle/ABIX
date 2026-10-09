#include "AMC/Tool/MCP.h"

namespace amc::mcp {

using namespace amc::util;

// --- JSON-RPC plumbing ---------------------------------------------------

amc::Json make_result(const amc::Json &id, amc::Json result) {
    amc::Json response = amc::Json::object();
    response.set("jsonrpc", "2.0");
    response.set("id", id);
    response.set("result", std::move(result));
    return response;
}

amc::Json make_error(const amc::Json &id, long long code, const std::string &message) {
    amc::Json error = amc::Json::object();
    error.set("code", code);
    error.set("message", message);
    amc::Json response = amc::Json::object();
    response.set("jsonrpc", "2.0");
    response.set("id", id);
    response.set("error", std::move(error));
    return response;
}

void respond(amc::Json response) {
    fmt::print("{}\n", response.dump());
    std::fflush(stdout);
}

amc::Json tool_call_result(amc::Json structured, bool is_error) {
    amc::Json item = amc::Json::object();
    item.set("type", "text");
    item.set("text", is_error ? structured.as_string() : structured.dump_pretty());
    amc::Json content = amc::Json::array();
    content.push_back(std::move(item));

    amc::Json result = amc::Json::object();
    result.set("content", std::move(content));
    if (!is_error) result.set("structuredContent", std::move(structured));
    result.set("isError", is_error);
    return result;
}

void handle_request(const amc::Json &request) {
    const amc::Json *id_node = request.find("id");
    const amc::Json id = id_node != nullptr ? *id_node : amc::Json();
    if (id_node == nullptr) return;   // notification: no response

    const amc::Json *method_node = request.find("method");
    const std::string method = method_node != nullptr ? method_node->as_string() : "";
    if (method.empty()) {
        respond(make_error(id, -32'600, "invalid request: missing method"));
        return;
    }

    if (method == "initialize") {
        std::string protocol = "2025-06-18";   // preferred supported version
        if (const amc::Json *params = request.find("params")) {
            if (const amc::Json *requested = params->find("protocolVersion")) {
                const std::string value = requested->as_string();
                if (value == "2024-11-05" || value == "2025-06-18") protocol = value;
            }
        }
        amc::Json capabilities = amc::Json::object();
        capabilities.set("tools", amc::Json::object());
        amc::Json server = amc::Json::object();
        server.set("name", "amc-mcp");
        server.set("version", "1.0.0");
        amc::Json result = amc::Json::object();
        result.set("protocolVersion", protocol);
        result.set("capabilities", std::move(capabilities));
        result.set("serverInfo", std::move(server));
        respond(make_result(id, std::move(result)));
        return;
    }
    if (method == "ping") {
        respond(make_result(id, amc::Json::object()));
        return;
    }
    if (method == "tools/list") {
        amc::Json result = amc::Json::object();
        result.set("tools", tools_catalogue());
        respond(make_result(id, std::move(result)));
        return;
    }
    if (method == "tools/call") {
        const amc::Json *params = request.find("params");
        const amc::Json *name_node = params != nullptr ? params->find("name") : nullptr;
        if (name_node == nullptr) {
            respond(make_error(id, -32'602, "tools/call requires params.name"));
            return;
        }
        const std::string tool = name_node->as_string();
        const amc::Json empty = amc::Json::object();
        const amc::Json *arguments_node = params->find("arguments");
        const amc::Json &arguments = arguments_node != nullptr ? *arguments_node : empty;

        std::string error;
        amc::Json structured;
        if (tool == "abix.get_module")
            structured = tool_get_module(arguments, error);
        else if (tool == "abix.list_types")
            structured = tool_list_types(arguments, error);
        else if (tool == "abix.get_type")
            structured = tool_get_type(arguments, error);
        else if (tool == "abix.get_function")
            structured = tool_get_function(arguments, error);
        else if (tool == "abix.compare_abi")
            structured = tool_compare_abi(arguments, error);
        else if (tool == "abix.find_compatible")
            structured = tool_find_compatible(arguments, error);
        else if (tool == "abix.list_functions")
            structured = tool_list_functions(arguments, error);
        else if (tool == "abix.get_layout")
            structured = tool_get_layout(arguments, error);
        else if (tool == "abix.resolve_type")
            structured = tool_resolve_type(arguments, error);
        else if (tool == "abix.compare_types")
            structured = tool_find_compatible(arguments, error);
        else if (tool == "abix.list_modules")
            structured = tool_list_modules(arguments, error);
        else if (tool == "abix.search_type")
            structured = tool_search_type(arguments, error);
        else
            error = "unknown tool '" + tool + "'";

        if (!error.empty()) {
            respond(make_result(id, tool_call_result(amc::Json(error), true)));
            return;
        }
        respond(make_result(id, tool_call_result(std::move(structured), false)));
        return;
    }

    respond(make_error(id, -32'601, "method not found: " + method));
}

}   // namespace amc::mcp
