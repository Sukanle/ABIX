#include "AMC/Lang/Lang.h"

#include <cstdio>
#include <iostream>
#include <string>

#include <fmt/format.h>
#include <toml++/toml.h>

#include "AMC/Core/Json.h"

#if defined(_WIN32)
#  include <io.h>
#  define AMC_DUP _dup
#  define AMC_DUP2 _dup2
#  define AMC_FILENO _fileno
#  define AMC_CLOSE _close
#else
#  include <unistd.h>
#  define AMC_DUP dup
#  define AMC_DUP2 dup2
#  define AMC_FILENO fileno
#  define AMC_CLOSE close
#endif

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
            e = "only "
              + (expected_language.empty() ? std::string("the provider") : expected_language)
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
        if (auto *owned = t["owned"].as_array()) {
            for (auto &&entry : *owned) {
                auto *table = entry.as_table();
                if (!table) continue;
                OwnedDecl decl;
                decl.type = (*table)["type"].value_or("");
                decl.create = (*table)["create"].value_or("");
                decl.destroy = (*table)["destroy"].value_or("");
                decl.semantics = (*table)["semantics"].value_or(std::string("owned"));
                if (!decl.type.empty()) c.owned.push_back(std::move(decl));
            }
        }
        if (auto *contracts = t["contract"].as_array()) {
            for (auto &&entry : *contracts) {
                auto *table = entry.as_table();
                if (!table) continue;
                ContractDecl decl;
                decl.function = (*table)["function"].value_or("");
                decl.returns = (*table)["returns"].value_or("");
                decl.release = (*table)["release"].value_or("");
                if (auto *params = (*table)["params"].as_array())
                    for (auto &&value : *params)
                        decl.params.push_back(value.value_or(""));
                if (!decl.function.empty()) c.contracts.push_back(std::move(decl));
            }
        }
        if (auto *errors = t["error"].as_array()) {
            for (auto &&entry : *errors) {
                auto *table = entry.as_table();
                if (!table) continue;
                ErrorDecl decl;
                decl.function = (*table)["function"].value_or("");
                decl.status = (*table)["status"].value_or(std::string("return"));
                decl.success = (*table)["success"].value_or(int64_t{0});
                decl.value = (*table)["value"].value_or(int64_t{-1});
                if (!decl.function.empty()) c.errors.push_back(std::move(decl));
            }
        }
        if (auto *thunks = t["thunks"].as_table()) c.thunks_output = (*thunks)["output"].value_or("");
        if (auto *expose = t["expose"].as_array()) {
            for (auto &&entry : *expose) {
                auto *table = entry.as_table();
                if (!table) continue;
                ExposeDecl decl;
                decl.type = (*table)["type"].value_or("");
                if (auto *methods = (*table)["methods"].as_array())
                    for (auto &&value : *methods) {
                        const std::string method = value.value_or("");
                        if (!method.empty()) decl.methods.push_back(method);
                    }
                if (auto *ctor = (*table)["constructor"].as_array()) {
                    decl.has_constructor = true;
                    for (auto &&value : *ctor)
                        decl.constructor.push_back(value.value_or(""));
                }
                decl.destructor = (*table)["destructor"].value_or(true);
                decl.opaque = (*table)["opaque"].value_or(true);
                if (!decl.type.empty()) c.exposed.push_back(std::move(decl));
            }
        }
        if (auto *instantiations = t["instantiate"].as_array()) {
            for (auto &&entry : *instantiations) {
                auto *table = entry.as_table();
                if (!table) continue;
                InstantiateDecl decl;
                decl.template_name = (*table)["template"].value_or("");
                decl.as = (*table)["as"].value_or("");
                if (auto *args = (*table)["args"].as_array())
                    for (auto &&value : *args)
                        decl.args.push_back(value.value_or(""));
                if (!decl.template_name.empty()) c.instantiations.push_back(std::move(decl));
            }
        }
        if (auto *adapts = t["adapt"].as_array()) {
            for (auto &&entry : *adapts) {
                auto *table = entry.as_table();
                if (!table) continue;
                AdaptDecl decl;
                decl.native = (*table)["native"].value_or("");
                decl.target = (*table)["target"].value_or("");
                decl.element = (*table)["element"].value_or("");
                if (!decl.native.empty()) c.adaptations.push_back(std::move(decl));
            }
        }
        // `compile_commands` / `flags` are optional: a source-level frontend
        // (e.g. Rust's `#[repr(C)]` extractor) needs no build flags, and the
        // C++ frontend falls back to a fixed compilation database.
        if (c.files.empty() || c.symbols.empty()) {
            e = c.language + " import requires files and symbols";
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
    const auto hex = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<unsigned>(c - 'A' + 10);
        return 0;
    };
    // The sender escapes the value with `json_escape` (backslashes doubled), so
    // the value must be unescaped here: a Windows path arrives as
    // `F:\\Code\\...` and would otherwise keep the doubled separators.
    std::string out;
    const size_t size = line.size();
    for (size_t i = begin + needle.size(); i < size; ++i) {
        const char c = line[i];
        if (c == '"') return out;   // the unescaped closing quote
        if (c != '\\') {
            out += c;
            continue;
        }
        if (++i >= size) break;
        switch (line[i]) {
            case '"':  out += '"'; break;
            case '\\': out += '\\'; break;
            case '/':  out += '/'; break;
            case 'b':  out += '\b'; break;
            case 'f':  out += '\f'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 'u':
                // `json_escape` only emits \uXXXX for bytes below 0x20, which is
                // always a single UTF-8 byte.
                if (i + 4 < size) {
                    const unsigned code =
                        (hex(line[i + 1]) << 12) | (hex(line[i + 2]) << 8) | (hex(line[i + 3]) << 4) | hex(line[i + 4]);
                    out += static_cast<char>(code & 0xFF);
                    i += 4;
                }
                break;
            default: out += line[i]; break;
        }
    }
    return out;
}

int run_provider(int argc, char **argv, const Provider &provider) {
    if (argc == 2 && std::string(argv[1]) == "--ipc") return ipc(provider);
    if (argc != 4) {
        std::string names;
        for (const auto &capability : provider.capabilities) {
            if (!names.empty()) names += "|";
            names += capability.name;
        }
        fmt::print(stderr, "usage: amc-{} <{}> <input> <output>\n", provider.language, names);
        return 2;
    }
    const std::string capability = argv[1];
    for (const auto &candidate : provider.capabilities)
        if (candidate.name == capability) return candidate.run(argv[2], argv[3]);
    fmt::print(stderr, "unknown capability\n");
    return 2;
}

namespace {

constexpr int kProtocolMajor = 1;
constexpr int kProtocolMinor = 0;

long long message_id(const amc::Json &message) {
    const auto *id = message.find("id");
    return id != nullptr ? static_cast<long long>(id->as_number(-1)) : -1;
}

std::string message_type(const amc::Json &message) {
    const auto *type = message.find("type");
    return type != nullptr ? type->as_string() : std::string{};
}

void emit(const amc::Json &message) {
    fmt::println("{}", message.dump());
    std::fflush(stdout);
}

amc::Json diagnostics_json(const std::string &text, const char *severity) {
    amc::Json diagnostics = amc::Json::array();
    if (text.empty()) return diagnostics;
    amc::Json entry = amc::Json::object();
    entry.set("severity", severity);
    entry.set("message", text);
    diagnostics.push_back(std::move(entry));
    return diagnostics;
}

void emit_error(long long id, const char *code, const char *category, const std::string &message,
    const std::string &diagnostics = {}) {
    amc::Json error = amc::Json::object();
    error.set("schema", "abix.error/1");
    error.set("code", code);
    error.set("category", category);
    error.set("message", message);
    error.set("retryable", false);

    amc::Json envelope = amc::Json::object();
    envelope.set("schema", "abix.plugin/1");
    envelope.set("id", amc::Json(id));
    envelope.set("type", "ERROR");
    envelope.set("error", std::move(error));
    if (!diagnostics.empty()) envelope.set("diagnostics", diagnostics_json(diagnostics, "error"));
    emit(envelope);
}

bool parse_protocol(const amc::Json &message, bool &legacy, std::string &error) {
    const auto *protocol = message.find("protocol");
    if (protocol == nullptr) {
        error = "missing `protocol`";
        return false;
    }
    if (protocol->is_object()) {
        const auto *major = protocol->find("major");
        if (major == nullptr || static_cast<int>(major->as_number(0)) != kProtocolMajor) {
            error = fmt::format("unsupported protocol major (expected {})", kProtocolMajor);
            return false;
        }
        legacy = false;
        return true;
    }
    if (static_cast<int>(protocol->as_number(0)) != kProtocolMajor) {
        error = fmt::format("unsupported protocol (expected {})", kProtocolMajor);
        return false;
    }
    legacy = true;
    return true;
}

amc::Json roles_json(const Provider &provider) {
    amc::Json roles = amc::Json::array();
    for (const auto &capability : provider.capabilities)
        roles.push_back(capability.name);
    return roles;
}

amc::Json features_json(const Provider &provider) {
    amc::Json features = amc::Json::array();
    for (const auto &feature : provider.features)
        features.push_back(feature);
    return features;
}

amc::Json ir_json(const Provider &provider) {
    amc::Json ir = amc::Json::object();
    ir.set("min", amc::Json(provider.ir_min));
    ir.set("max", amc::Json(provider.ir_max));
    return ir;
}

void emit_ready(const Provider &provider, long long id, bool legacy) {
    amc::Json message = amc::Json::object();
    message.set("schema", "abix.plugin/1");
    message.set("id", amc::Json(id));
    message.set("type", "READY");
    if (legacy) {
        message.set("protocol", amc::Json(kProtocolMajor));
    } else {
        amc::Json protocol = amc::Json::object();
        protocol.set("major", amc::Json(kProtocolMajor));
        protocol.set("minor", amc::Json(kProtocolMinor));
        message.set("protocol", std::move(protocol));
    }
    message.set("language", provider.language);
    message.set("roles", roles_json(provider));
    message.set("provider_version", provider.version);
    message.set("ir", ir_json(provider));
    message.set("features", features_json(provider));
    emit(message);
}

void emit_capabilities(const Provider &provider, long long id) {
    amc::Json message = amc::Json::object();
    message.set("schema", "abix.plugin/1");
    message.set("id", amc::Json(id));
    message.set("type", "CAPABILITIES");
    message.set("language", provider.language);
    message.set("capabilities", roles_json(provider));
    message.set("roles", roles_json(provider));
    message.set("provider_version", provider.version);
    message.set("ir", ir_json(provider));
    message.set("features", features_json(provider));
    emit(message);
}

void emit_describe(const Provider &provider, long long id) {
    amc::Json info = amc::Json::object();
    info.set("language", provider.language);
    info.set("roles", roles_json(provider));
    info.set("protocol", "jsonl-v1");
    info.set("provider_version", provider.version);
    info.set("ir", ir_json(provider));
    info.set("features", features_json(provider));

    amc::Json message = amc::Json::object();
    message.set("schema", "abix.plugin/1");
    message.set("id", amc::Json(id));
    message.set("type", "DESCRIBE_RESULT");
    message.set("provider", std::move(info));
    emit(message);
}

int run_capturing_stderr(
    int (*run)(const char *, const char *), const char *input, const char *output, std::string &diagnostics) {
    std::fflush(stderr);
    FILE *capture = std::tmpfile();
    if (capture == nullptr) return run(input, output);

    const int saved = AMC_DUP(AMC_FILENO(stderr));
    if (saved < 0 || AMC_DUP2(AMC_FILENO(capture), AMC_FILENO(stderr)) < 0) {
        if (saved >= 0) AMC_CLOSE(saved);
        std::fclose(capture);
        return run(input, output);
    }

    const int status = run(input, output);
    std::fflush(stderr);
    AMC_DUP2(saved, AMC_FILENO(stderr));
    AMC_CLOSE(saved);

    std::rewind(capture);
    char buffer[4'096];
    size_t count = 0;
    while ((count = std::fread(buffer, 1, sizeof(buffer), capture)) > 0)
        diagnostics.append(buffer, count);
    std::fclose(capture);
    if (!diagnostics.empty()) std::fwrite(diagnostics.data(), 1, diagnostics.size(), stderr);
    return status;
}

void emit_result(
    long long id, const std::string &capability, const std::string &output, const std::string &diagnostics = {}) {
    amc::Json artifact = amc::Json::object();
    artifact.set("kind", capability == "frontend" ? "abix" : "source");
    artifact.set("path", output);

    amc::Json artifacts = amc::Json::array();
    artifacts.push_back(std::move(artifact));

    amc::Json message = amc::Json::object();
    message.set("schema", "abix.plugin/1");
    message.set("id", amc::Json(id));
    message.set("type", "RESULT");
    message.set("status", "ok");
    message.set("capability", capability);
    message.set("artifacts", std::move(artifacts));
    message.set("diagnostics", diagnostics_json(diagnostics, "warning"));
    emit(message);
}

int run_capability(const Provider &provider, const std::string &name, const char *input, const char *output,
    std::string &diagnostics) {
    for (const auto &candidate : provider.capabilities) {
        if (candidate.name != name) continue;
        return run_capturing_stderr(candidate.run, input, output, diagnostics);
    }
    return -1;
}

}   // namespace

int ipc(const Provider &provider) {
    std::string line;
    bool initialized = false;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        amc::Json message;
        std::string parse_error;
        if (!amc::parse_json(line, message, parse_error)) {
            emit_error(-1, "PLUGIN-INVALID-JSON", "protocol", "invalid JSON: " + parse_error);
            return 2;
        }
        const long long id = message_id(message);
        if (const auto *schema = message.find("schema"); schema != nullptr && schema->as_string() != "abix.plugin/1") {
            emit_error(id, "PLUGIN-SCHEMA", "protocol", "unsupported schema: " + schema->as_string());
            return 2;
        }

        const std::string type = message_type(message);
        if (type == "INIT" || type == "HELLO") {
            bool legacy = false;
            std::string protocol_error;
            if (!parse_protocol(message, legacy, protocol_error)) {
                emit_error(id, "PLUGIN-PROTOCOL-MISMATCH", "protocol", protocol_error);
                return 2;
            }
            initialized = true;
            emit_ready(provider, id, legacy);
            continue;
        }
        if (type == "QUERY_CAPABILITIES") {
            if (!initialized) {
                emit_error(id, "PLUGIN-NOT-INITIALIZED", "protocol", "QUERY_CAPABILITIES before INIT/HELLO");
                return 2;
            }
            emit_capabilities(provider, id);
            continue;
        }
        if (type == "DESCRIBE") {
            if (!initialized) {
                emit_error(id, "PLUGIN-NOT-INITIALIZED", "protocol", "DESCRIBE before INIT/HELLO");
                return 2;
            }
            emit_describe(provider, id);
            continue;
        }
        if (type == "ANALYZE") {
            if (!initialized) {
                emit_error(id, "PLUGIN-NOT-INITIALIZED", "protocol", "ANALYZE before INIT/HELLO");
                return 2;
            }
            const auto *capability_node = message.find("capability");
            const auto *input_node = message.find("input");
            const auto *output_node = message.find("output");
            if (capability_node == nullptr || input_node == nullptr || output_node == nullptr) {
                emit_error(id, "PLUGIN-BAD-REQUEST", "protocol", "ANALYZE requires capability, input and output");
                return 2;
            }
            const std::string capability = capability_node->as_string();
            const std::string input = input_node->as_string();
            const std::string output = output_node->as_string();

            std::string diagnostics;
            const int result = run_capability(provider, capability, input.c_str(), output.c_str(), diagnostics);
            if (result < 0) {
                emit_error(id, "PLUGIN-UNKNOWN-CAPABILITY", "protocol",
                    fmt::format("capability `{}` is not advertised by {}", capability, provider.language));
                return 2;
            }
            if (result != 0) {
                emit_error(id, "PLUGIN-CAPABILITY-FAILED", "provider",
                    fmt::format("capability `{}` failed with status {}", capability, result), diagnostics);
                return result;
            }
            emit_result(id, capability, output, diagnostics);
            continue;
        }
        if (type == "PROJECT") {
            if (!initialized) {
                emit_error(id, "PLUGIN-NOT-INITIALIZED", "protocol", "PROJECT before INIT/HELLO");
                return 2;
            }
            const auto *input_node = message.find("input");
            const auto *output_node = message.find("output");
            if (input_node == nullptr || output_node == nullptr) {
                emit_error(id, "PLUGIN-BAD-REQUEST", "protocol", "PROJECT requires input and output");
                return 2;
            }
            const std::string input = input_node->as_string();
            const std::string output = output_node->as_string();

            std::string diagnostics;
            const int result = run_capability(provider, "backend", input.c_str(), output.c_str(), diagnostics);
            if (result < 0) {
                emit_error(id, "PLUGIN-UNKNOWN-CAPABILITY", "protocol",
                    fmt::format("{} does not advertise a backend capability", provider.language));
                return 2;
            }
            if (result != 0) {
                emit_error(id, "PLUGIN-CAPABILITY-FAILED", "provider",
                    fmt::format("backend projection failed with status {}", result), diagnostics);
                return result;
            }
            emit_result(id, "backend", output, diagnostics);
            continue;
        }
        if (type == "CANCEL") {
            if (!initialized) {
                emit_error(id, "PLUGIN-NOT-INITIALIZED", "protocol", "CANCEL before INIT/HELLO");
                return 2;
            }
            const auto *target = message.find("target");
            if (target == nullptr) {
                emit_error(id, "PLUGIN-BAD-REQUEST", "protocol", "CANCEL requires target");
                return 2;
            }
            amc::Json cancelled = amc::Json::object();
            cancelled.set("schema", "abix.plugin/1");
            cancelled.set("id", amc::Json(id));
            cancelled.set("type", "CANCELLED");
            cancelled.set("status", "cancelled");
            cancelled.set("target", amc::Json(static_cast<long long>(target->as_number(-1))));
            emit(cancelled);
            continue;
        }
        if (type == "DONE") {
            amc::Json done = amc::Json::object();
            done.set("schema", "abix.plugin/1");
            done.set("id", amc::Json(id));
            done.set("type", "DONE");
            emit(done);
            return 0;
        }

        emit_error(id, "PLUGIN-UNSUPPORTED-MESSAGE", "protocol", "unsupported message type: " + type);
        return 2;
    }
    return 2;
}

}   // namespace amc::lang
