#include "AMC/Tool/MCP.h"

namespace amc::mcp {

using namespace amc::util;

// --- tools ---------------------------------------------------------------

amc::Json tool_get_module(const amc::Json &args, std::string &error) {
    amc::AbiModule module;
    if (!load_module(args, module, error)) return amc::Json();
    amc::Json result = amc::Json::object();
    result.set("package", module.package_name);
    result.set("package_version", module.package_version);
    amc::Json target = amc::Json::object();
    target.set("arch", static_cast<long long>(module.arch));
    target.set("os", static_cast<long long>(module.os));
    target.set("abi", static_cast<long long>(module.target_abi));
    target.set("compiler", static_cast<long long>(module.compiler));
    target.set("calling_convention", static_cast<long long>(module.calling_convention));
    target.set("arch_name", arch_name(module.arch));
    target.set("os_name", os_name(module.os));
    target.set("compiler_name", compiler_name(module.compiler));
    target.set("calling_convention_name", target_calling_convention_name(module.calling_convention));
    result.set("target", std::move(target));
    result.set("abi_hash", hash_hex(amc::abi_hash(module)));
    amc::Json counts = amc::Json::object();
    counts.set("types", static_cast<long long>(module.types.size()));
    counts.set("fields", static_cast<long long>(module.fields.size()));
    counts.set("functions", static_cast<long long>(module.functions.size()));
    counts.set("symbols", static_cast<long long>(module.symbols.size()));
    result.set("counts", std::move(counts));
    return result;
}

amc::Json tool_list_types(const amc::Json &args, std::string &error) {
    amc::AbiModule module;
    if (!load_module(args, module, error)) return amc::Json();
    amc::Json types = amc::Json::array();
    for (size_t i = 0; i < module.types.size(); ++i) {
        const auto &type = module.types[i];
        amc::Json item = amc::Json::object();
        item.set("index", static_cast<long long>(i));
        item.set("name", type.name);
        item.set("kind", amc::type_kind_name(type.kind));
        item.set("id", hash_hex(type.id));
        item.set("layout_hash", hash_hex(type.layout_hash));
        item.set("size", static_cast<long long>(type.size));
        item.set("align", static_cast<long long>(type.align));
        types.push_back(std::move(item));
    }
    amc::Json result = amc::Json::object();
    result.set("package", module.package_name);
    result.set("type_count", static_cast<long long>(module.types.size()));
    result.set("types", std::move(types));
    return result;
}

amc::Json tool_get_type(const amc::Json &args, std::string &error) {
    amc::AbiModule module;
    if (!load_module(args, module, error)) return amc::Json();
    std::string needle = arg_string(args, "name");
    if (needle.empty()) needle = arg_string(args, "id");
    if (needle.empty()) {
        error = "get_type requires 'name' or 'id'";
        return amc::Json();
    }
    const amc::Json *layout = args.find("layout");
    const bool include_layout = layout != nullptr && layout->as_bool(false);
    const auto indices = amc::query_type_indices(module, needle);
    if (indices.empty()) {
        error = "no type matches '" + needle + "'";
        return amc::Json();
    }
    amc::Json result;
    if (!json_from_string(amc::query_types_json(module, indices, include_layout, needle), result, error))
        return amc::Json();
    return result;
}

amc::Json tool_get_function(const amc::Json &args, std::string &error) {
    amc::AbiModule module;
    if (!load_module(args, module, error)) return amc::Json();
    const std::string needle = arg_string(args, "name");
    if (needle.empty()) {
        error = "get_function requires 'name'";
        return amc::Json();
    }
    const auto indices = amc::query_function_indices(module, needle);
    if (indices.empty()) {
        error = "no function matches '" + needle + "'";
        return amc::Json();
    }
    amc::Json result;
    if (!json_from_string(amc::query_functions_json(module, indices, needle), result, error)) return amc::Json();
    return result;
}

amc::Json tool_compare_abi(const amc::Json &args, std::string &error) {
    amc::AbiModule module, other;
    if (!load_module(args, module, error)) return amc::Json();
    const std::string other_path = arg_string(args, "other");
    if (other_path.empty()) {
        error = "compare_abi requires 'other'";
        return amc::Json();
    }
    std::string read_error;
    if (!amc::read_abix(other_path, other, read_error)) {
        error = "cannot read '" + other_path + "': " + read_error;
        return amc::Json();
    }
    amc::VerifyResult verification;
    if (!amc::verify_modules(module, other, verification, error)) return amc::Json();
    amc::Json result;
    if (!json_from_string(amc::compatibility_query_json(module, other, verification), result, error))
        return amc::Json();
    return result;
}

amc::Json type_summary(const amc::Type &type) {
    amc::Json summary = amc::Json::object();
    summary.set("name", type.name);
    summary.set("id", hash_hex(type.id));
    summary.set("layout_hash", hash_hex(type.layout_hash));
    summary.set("size", static_cast<long long>(type.size));
    return summary;
}

// Two modes:
//  * with `other`      — compare one type across two artifacts (unchanged).
//  * without `other`   — knowledge-base mode: compare the type in the default
//                        module against every other indexed module and report
//                        which ones stay layout-compatible.
amc::Json tool_find_compatible(const amc::Json &args, std::string &error) {
    amc::AbiModule module;
    if (!load_module(args, module, error)) return amc::Json();
    const std::string needle = arg_string(args, "name");
    const std::string other_path = arg_string(args, "other");
    if (needle.empty()) {
        error = "find_compatible requires 'name'";
        return amc::Json();
    }

    if (other_path.empty()) {
        if (g_index.empty()) {
            error = "find_compatible requires 'other' when no modules are indexed";
            return amc::Json();
        }
        const std::string source_path =
            arg_string(args, "module").empty() ? g_default_module : arg_string(args, "module");
        const auto source_indices = amc::query_type_indices(module, needle);
        amc::Json result = amc::Json::object();
        result.set("name", needle);
        if (source_indices.empty()) {
            result.set("found", false);
            result.set("compatible", false);
            result.set("reason", "type not found in the source module");
            return result;
        }
        const auto &source = module.types[source_indices.front()];
        amc::Json candidates = amc::Json::array();
        bool all_compatible = true;
        for (const auto &entry : g_index) {
            if (entry.path == source_path) continue;
            const auto indices = amc::query_type_indices(entry.module, needle);
            if (indices.empty()) continue;
            const auto &candidate = entry.module.types[indices.front()];
            const bool same_id = source.id == candidate.id;
            const bool same_layout = source.layout_hash == candidate.layout_hash;
            amc::Json item = type_summary(candidate);
            item.set("module", entry.path);
            item.set("compatible", same_id && same_layout);
            item.set("same_type_id", same_id);
            item.set("same_layout_hash", same_layout);
            if (!(same_id && same_layout)) all_compatible = false;
            candidates.push_back(std::move(item));
        }
        const auto candidate_count = static_cast<long long>(candidates.items().size());
        result.set("found", true);
        result.set("candidate_count", candidate_count);
        result.set("compatible", all_compatible && candidate_count > 0);
        if (candidate_count == 0) result.set("reason", "no other indexed module declares '" + needle + "'");
        result.set("source", type_summary(source));
        result.set("candidates", std::move(candidates));
        return result;
    }

    amc::AbiModule other;
    std::string read_error;
    if (!amc::read_abix(other_path, other, read_error)) {
        error = "cannot read '" + other_path + "': " + read_error;
        return amc::Json();
    }
    const auto source_indices = amc::query_type_indices(module, needle);
    const auto target_indices = amc::query_type_indices(other, needle);
    amc::Json result = amc::Json::object();
    result.set("name", needle);
    if (source_indices.empty() || target_indices.empty()) {
        result.set("found", false);
        result.set("compatible", false);
        result.set("reason", source_indices.empty() ? "type not found in module" : "type not found in other");
        return result;
    }
    const auto &source = module.types[source_indices.front()];
    const auto &target = other.types[target_indices.front()];
    const bool same_id = source.id == target.id;
    const bool same_layout = source.layout_hash == target.layout_hash;
    result.set("found", true);
    result.set("compatible", same_id && same_layout);
    result.set("same_type_id", same_id);
    result.set("same_layout_hash", same_layout);
    result.set("source", type_summary(source));
    result.set("target", type_summary(target));
    return result;
}

amc::Json tool_list_functions(const amc::Json &args, std::string &error) {
    amc::AbiModule module;
    if (!load_module(args, module, error)) return amc::Json();
    amc::Json functions = amc::Json::array();
    for (size_t i = 0; i < module.functions.size(); ++i) {
        const auto &function = module.functions[i];
        const amc::Type *owner = amc::find_type_by_id(module, function.owner_type);
        const amc::Type *result_type = amc::find_type_by_id(module, function.return_type);
        amc::Json item = amc::Json::object();
        item.set("index", static_cast<long long>(i));
        item.set("name", function.name);
        item.set("owner_name", owner != nullptr ? owner->name : std::string());
        item.set("signature", hash_hex(function.signature));
        item.set("return_type_name", result_type != nullptr ? result_type->name : std::string());
        item.set("calling_convention", static_cast<long long>(function.calling_convention));
        item.set("calling_convention_name", function_calling_convention_name(function.calling_convention));
        item.set("flags", static_cast<long long>(function.flags));
        item.set("parameter_count", static_cast<long long>(function.parameters.size()));
        functions.push_back(std::move(item));
    }
    amc::Json result = amc::Json::object();
    result.set("package", module.package_name);
    result.set("function_count", static_cast<long long>(module.functions.size()));
    result.set("functions", std::move(functions));
    return result;
}

amc::Json tool_get_layout(const amc::Json &args, std::string &error) {
    amc::AbiModule module;
    if (!load_module(args, module, error)) return amc::Json();
    std::string needle = arg_string(args, "name");
    if (needle.empty()) needle = arg_string(args, "id");
    if (needle.empty()) {
        error = "get_layout requires 'name' or 'id'";
        return amc::Json();
    }
    const auto indices = amc::query_type_indices(module, needle);
    if (indices.empty()) {
        error = "no type matches '" + needle + "'";
        return amc::Json();
    }
    const auto &type = module.types[indices.front()];
    amc::Json result = amc::Json::object();
    result.set("name", type.name);
    result.set("id", hash_hex(type.id));
    result.set("layout_hash", hash_hex(type.layout_hash));
    result.set("size", static_cast<long long>(type.size));
    result.set("align", static_cast<long long>(type.align));
    result.set("flags", static_cast<long long>(type.flags));
    amc::Json fields = amc::Json::array();
    for (uint32_t f = 0; f < type.field_count; ++f) {
        const auto &field = module.fields[type.field_begin + f];
        const amc::Type *field_type = amc::find_type_by_id(module, field.type_id);
        amc::Json item = amc::Json::object();
        item.set("index", static_cast<long long>(type.field_begin + f));
        item.set("name", field.name);
        item.set("type_id", hash_hex(field.type_id));
        item.set("type_name", field_type != nullptr ? field_type->name : std::string());
        item.set("offset", static_cast<long long>(field.offset));
        item.set("flags", static_cast<long long>(field.flags));
        fields.push_back(std::move(item));
    }
    result.set("field_count", static_cast<long long>(type.field_count));
    result.set("fields", std::move(fields));
    return result;
}

amc::Json tool_resolve_type(const amc::Json &args, std::string &error) {
    amc::AbiModule module;
    if (!load_module(args, module, error)) return amc::Json();
    std::string needle = arg_string(args, "name");
    if (needle.empty()) needle = arg_string(args, "id");
    if (needle.empty()) {
        error = "resolve_type requires 'name' or 'id'";
        return amc::Json();
    }
    const auto indices = amc::query_type_indices(module, needle);
    amc::Json matches = amc::Json::array();
    for (const auto index : indices) {
        const auto &type = module.types[index];
        amc::Json item = amc::Json::object();
        item.set("index", static_cast<long long>(index));
        item.set("name", type.name);
        item.set("id", hash_hex(type.id));
        matches.push_back(std::move(item));
    }
    amc::Json result = amc::Json::object();
    result.set("needle", needle);
    result.set("match_count", static_cast<long long>(indices.size()));
    result.set("matches", std::move(matches));
    return result;
}

amc::Json tool_list_modules(const amc::Json &args, std::string &error) {
    (void)args;
    if (g_index.empty()) {
        error = "no indexed modules: start amc-mcp with one or more .abix paths";
        return amc::Json();
    }
    amc::Json modules = amc::Json::array();
    for (const auto &entry : g_index) {
        const auto &module = entry.module;
        amc::Json item = amc::Json::object();
        item.set("path", entry.path);
        item.set("package", module.package_name);
        item.set("package_version", module.package_version);
        item.set("abi_hash", hash_hex(amc::abi_hash(module)));
        amc::Json counts = amc::Json::object();
        counts.set("types", static_cast<long long>(module.types.size()));
        counts.set("functions", static_cast<long long>(module.functions.size()));
        item.set("counts", std::move(counts));
        modules.push_back(std::move(item));
    }
    amc::Json result = amc::Json::object();
    result.set("module_count", static_cast<long long>(g_index.size()));
    result.set("modules", std::move(modules));
    return result;
}

// Cross-module type search for the knowledge base. Matches are grouped by type
// name, and each group records whether every occurrence in the index shares one
// TypeID and LayoutHash. This answers "find every module that declares Foo, and
// are they the same ABI?" without a per-pair query.
amc::Json tool_search_type(const amc::Json &args, std::string &error) {
    if (g_index.empty()) {
        error = "no indexed modules: start amc-mcp with one or more .abix paths";
        return amc::Json();
    }
    std::string needle = arg_string(args, "name");
    if (needle.empty()) needle = arg_string(args, "id");
    if (needle.empty()) {
        error = "search_type requires 'name' or 'id'";
        return amc::Json();
    }

    struct Group {
        std::string name;
        bool have_reference = false;
        bool consistent = true;
        amc::Hash128 reference_id{}, reference_layout{};
        amc::Json matches = amc::Json::array();
    };
    std::vector<Group> groups;
    for (const auto &entry : g_index) {
        const auto indices = amc::query_type_indices(entry.module, needle);
        for (const auto index : indices) {
            const auto &type = entry.module.types[index];
            Group *group = nullptr;
            for (auto &candidate : groups) {
                if (candidate.name == type.name) {
                    group = &candidate;
                    break;
                }
            }
            if (group == nullptr) {
                groups.push_back(Group{});
                group = &groups.back();
                group->name = type.name;
            }
            amc::Json item = amc::Json::object();
            item.set("module", entry.path);
            item.set("package", entry.module.package_name);
            item.set("id", hash_hex(type.id));
            item.set("layout_hash", hash_hex(type.layout_hash));
            item.set("size", static_cast<long long>(type.size));
            item.set("align", static_cast<long long>(type.align));
            if (!group->have_reference) {
                group->have_reference = true;
                group->reference_id = type.id;
                group->reference_layout = type.layout_hash;
            } else if (!(type.id == group->reference_id && type.layout_hash == group->reference_layout)) {
                group->consistent = false;
            }
            group->matches.push_back(std::move(item));
        }
    }

    amc::Json groups_json = amc::Json::array();
    long long total = 0;
    for (auto &group : groups) {
        const auto count = static_cast<long long>(group.matches.items().size());
        total += count;
        amc::Json item = amc::Json::object();
        item.set("name", group.name);
        item.set("match_count", count);
        item.set("consistent", group.consistent);
        item.set("matches", std::move(group.matches));
        groups_json.push_back(std::move(item));
    }

    amc::Json result = amc::Json::object();
    result.set("needle", needle);
    result.set("match_count", total);
    result.set("group_count", static_cast<long long>(groups.size()));
    result.set("groups", std::move(groups_json));
    return result;
}

// --- tool catalogue ------------------------------------------------------

amc::Json property(const char *type, const char *description) {
    amc::Json value = amc::Json::object();
    value.set("type", type);
    value.set("description", description);
    return value;
}

// JSON Schema "anyOf": at least one of "name" or "id" must be present.
amc::Json name_or_id_any_of() {
    amc::Json by_name = amc::Json::object();
    {
        amc::Json required = amc::Json::array();
        required.push_back(amc::Json("name"));
        by_name.set("required", std::move(required));
    }
    amc::Json by_id = amc::Json::object();
    {
        amc::Json required = amc::Json::array();
        required.push_back(amc::Json("id"));
        by_id.set("required", std::move(required));
    }
    amc::Json any_of = amc::Json::array();
    any_of.push_back(std::move(by_name));
    any_of.push_back(std::move(by_id));
    return any_of;
}

amc::Json make_tool(const char *name, const char *description, amc::Json properties, std::vector<std::string> required,
    amc::Json any_of = amc::Json()) {
    amc::Json schema = amc::Json::object();
    schema.set("type", "object");
    schema.set("properties", std::move(properties));
    amc::Json required_json = amc::Json::array();
    for (auto &entry : required)
        required_json.push_back(amc::Json(std::move(entry)));
    schema.set("required", std::move(required_json));
    if (any_of.is_array()) schema.set("anyOf", std::move(any_of));

    amc::Json tool = amc::Json::object();
    tool.set("name", name);
    tool.set("description", description);
    tool.set("inputSchema", std::move(schema));
    return tool;
}

amc::Json tools_catalogue() {
    const auto module_prop = property("string", "Path to the .abix artifact (defaults to the server module)");
    amc::Json tools = amc::Json::array();

    {
        amc::Json props = amc::Json::object();
        props.set("module", module_prop);
        tools.push_back(make_tool(
            "abix.get_module", "Module identity: package, target, ABI hash and record counts.", std::move(props), {}));
    }
    {
        amc::Json props = amc::Json::object();
        props.set("module", module_prop);
        tools.push_back(make_tool("abix.list_types",
            "List every ABI type with its TypeID, LayoutHash, size and alignment.", std::move(props), {}));
    }
    {
        amc::Json props = amc::Json::object();
        props.set("name", property("string", "Type name, Owner::Type or 0x TypeID"));
        props.set("id", property("string", "TypeID as 0x<32 hex>"));
        props.set("layout", property("boolean", "Include the physical field layout"));
        props.set("module", module_prop);
        tools.push_back(
            make_tool("abix.get_type", "Resolve one type by name or TypeID; optionally include field offsets.",
                std::move(props), {}, name_or_id_any_of()));
    }
    {
        amc::Json props = amc::Json::object();
        props.set("name", property("string", "Function name or Owner::name"));
        props.set("module", module_prop);
        tools.push_back(make_tool("abix.get_function", "Resolve one function's signature, return type and parameters.",
            std::move(props), {"name"}));
    }
    {
        amc::Json props = amc::Json::object();
        props.set("other", property("string", "Path of the artifact to compare against"));
        props.set("module", module_prop);
        tools.push_back(make_tool("abix.compare_abi", "Strictly compare two artifacts; any ABI drift is reported.",
            std::move(props), {"other"}));
    }
    {
        amc::Json props = amc::Json::object();
        props.set("name", property("string", "Type name or 0x TypeID"));
        props.set("other", property("string", "Candidate artifact; omit to compare across every indexed module"));
        props.set("module", module_prop);
        tools.push_back(make_tool("abix.find_compatible",
            "Decide whether a type is ABI-compatible across two artifacts, or across the whole index.",
            std::move(props), {"name"}));
    }
    {
        amc::Json props = amc::Json::object();
        tools.push_back(make_tool("abix.list_modules",
            "List every module in the knowledge base with its package and record counts.", std::move(props), {}));
    }
    {
        amc::Json props = amc::Json::object();
        props.set("name", property("string", "Type name, Owner::Type or 0x TypeID"));
        props.set("id", property("string", "TypeID as 0x<32 hex>"));
        tools.push_back(make_tool("abix.search_type",
            "Find matching types across the indexed modules, grouped by name, flagging ABI disagreement.",
            std::move(props), {}, name_or_id_any_of()));
    }
    {
        amc::Json props = amc::Json::object();
        props.set("module", module_prop);
        tools.push_back(make_tool("abix.list_functions",
            "List every exported function with signature and parameter count.", std::move(props), {}));
    }
    {
        amc::Json props = amc::Json::object();
        props.set("name", property("string", "Type name or 0x TypeID"));
        props.set("id", property("string", "TypeID as 0x<32 hex>"));
        props.set("module", module_prop);
        tools.push_back(
            make_tool("abix.get_layout", "Memory layout of one type: size, align, LayoutHash and field offsets.",
                std::move(props), {}, name_or_id_any_of()));
    }
    {
        amc::Json props = amc::Json::object();
        props.set("name", property("string", "Type name or Owner::Type"));
        props.set("id", property("string", "TypeID as 0x<32 hex>"));
        props.set("module", module_prop);
        tools.push_back(make_tool("abix.resolve_type", "Resolve a name or partial TypeID to matching type ids.",
            std::move(props), {}, name_or_id_any_of()));
    }
    {
        amc::Json props = amc::Json::object();
        props.set("name", property("string", "Type name or 0x TypeID"));
        props.set("other", property("string", "Path of the artifact holding the candidate type"));
        props.set("module", module_prop);
        tools.push_back(make_tool("abix.compare_types", "Compare one type across two artifacts (TypeID + LayoutHash).",
            std::move(props), {"name", "other"}));
    }
    return tools;
}

}   // namespace amc::mcp
