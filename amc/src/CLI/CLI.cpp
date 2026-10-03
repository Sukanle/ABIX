#include "AMC/CLI/CLI.h"

#include <exception>

#include <fmt/format.h>
#include <fmt/os.h>

#include <sys/wait.h>
#include <unistd.h>

namespace amc::cli {

std::string json_quote(const std::string &value) { return "\"" + amc::json_escape(value) + "\""; }

// Parses "0x<hi16><lo16>" or "<hi16><lo16>" into a 128-bit hash.  The text
// order matches the canonical hash_string() rendering used across AMC.
bool parse_hash128(const std::string &text, amc::Hash128 &value) {
    std::string hex = text;
    if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0) hex = hex.substr(2);
    if (hex.size() != 32) return false;
    for (char c : hex)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    value.hi = std::stoull(hex.substr(0, 16), nullptr, 16);
    value.lo = std::stoull(hex.substr(16, 16), nullptr, 16);
    return true;
}

// Renders a 128-bit value as 32 lowercase hex digits (hi then lo), matching
// the symbol-server key format derived from ELF build ids.
std::string hash_key(amc::Hash128 value) {
    return fmt::format("{:016x}{:016x}", value.hi, value.lo);
}

// Minimal `.abix.meta` for a region extracted from a binary: the package/abi
// hash are not carried by the manifest, so only identity + counts are emitted.
std::string elf_meta_document(const std::string &key, const amc::MetadataHeader &header) {
    std::string out;
    out += "[metadata]\n";
    out += "build_id = \"0X";
    out += key;
    out += "\"\n";
    out += "metadata_id = \"0X";
    out += hash_key(header.metadata_id);
    out += "\"\n";
    out += "abi_version = ";
    out += fmt::to_string(header.version_major);
    out += "\n";
    out += "hash_algorithm = ";
    out += fmt::to_string(header.hash_algorithm);
    out += "\n";
    out += "type_count = ";
    out += fmt::to_string(header.type_count);
    out += "\n";
    out += "field_count = ";
    out += fmt::to_string(header.field_count);
    out += "\n";
    out += "function_count = ";
    out += fmt::to_string(header.function_count);
    out += "\n";
    out += "parameter_count = ";
    out += fmt::to_string(header.parameter_count);
    out += "\n";
    out += "symbol_count = ";
    out += fmt::to_string(header.symbol_count);
    out += "\n";
    out += "source = \"elf\"\n";
    return out;
}

// Editor-consumable ABI diagnostics: one `file:line:column: error: ...` line
// per drift, using the contract artifact's Source Origin when available.
std::string abi_diagnostics(const amc::AbiModule &reference, const amc::VerifyResult &result) {
    std::string out;
    for (const auto &change : result.changes) {
        const amc::Type *type = nullptr;
        if (!change.type.empty()) {
            const auto indices = amc::query_type_indices(reference, change.type);
            if (!indices.empty()) type = &reference.types[indices.front()];
        }
        if (type != nullptr) {
            if (const auto *source = amc::find_source_origin(reference, type->id)) {
                out += source->file;
                out += ':';
                out += fmt::to_string(source->line);
                out += ':';
                out += fmt::to_string(source->column);
                out += ": ";
            }
        }
        out += "error: ABI ";
        out += amc::change_kind_name(change.kind);
        if (!change.type.empty()) {
            out += " for ";
            out += change.type;
        }
        if (!change.name.empty()) {
            out += ".";
            out += change.name;
        }
        if (!change.detail.empty()) {
            out += ": ";
            out += change.detail;
        }
        out += "\n";
    }
    return out;
}

// Emits the `<artifact>.meta` sidecar described by the AI-PM plan so a symbol
// server or CI can index BuildID/MetadataID without parsing the `.abix`.
bool write_abix_meta(const amc::AbiModule &module, const fs::path &artifact, std::string &error) {
    amc::MetadataOptions options;
    std::vector<uint8_t> region;
    if (!amc::build_metadata_region(module, options, region, error)) return false;
    amc::MetadataHeader header;
    if (!amc::read_metadata_header(region, header, error)) return false;
    try {
        auto meta = fmt::output_file(artifact.string() + ".meta");
        meta.print("{}", amc::metadata_meta_document(module, header));
    } catch (const std::exception &e) {
        error = "cannot write metadata output: " + artifact.string() + ".meta: " + e.what();
        return false;
    }
    return true;
}

bool parse_exports(const toml::table &table, std::vector<ExportSpec> &exports, std::string &error) {
    const auto *entries = table["export"].as_array();
    if (!entries || entries->empty()) {
        error = "configuration needs at least one [[export]]";
        return false;
    }
    for (const auto &entry : *entries) {
        const auto *spec = entry.as_table();
        const auto output = spec ? (*spec)["output"].value<std::string>() : std::nullopt;
        if (!output || output->empty()) {
            error = "[[export]] requires output";
            return false;
        }
        ExportSpec result{*output, {}};
        if (const auto *symbols = (*spec)["symbols"].as_array())
            for (const auto &symbol : *symbols)
                result.symbols.push_back(symbol.value_or(""));
        exports.push_back(std::move(result));
    }
    return true;
}

bool dispatch_provider(const fs::path &provider, const char *capability, const fs::path &input, const fs::path &output,
    std::string &error) {
    int request_pipe[2] = {}, response_pipe[2] = {};
    if (pipe(request_pipe) != 0 || pipe(response_pipe) != 0) {
        error = "cannot create provider pipes";
        return false;
    }
    const pid_t child = fork();
    if (child < 0) {
        error = "cannot fork provider process";
        return false;
    }
    if (child == 0) {
        dup2(request_pipe[0], STDIN_FILENO);
        dup2(response_pipe[1], STDOUT_FILENO);
        close(request_pipe[0]);
        close(request_pipe[1]);
        close(response_pipe[0]);
        close(response_pipe[1]);
        execl(provider.c_str(), provider.c_str(), "--ipc", static_cast<char *>(nullptr));
        _exit(127);
    }
    close(request_pipe[0]);
    close(response_pipe[1]);
    const auto send = [&](const std::string &message) {
        const auto line = message + "\n";
        return write(request_pipe[1], line.data(), line.size()) == static_cast<ssize_t>(line.size());
    };
    const auto request = std::string("{\"type\":\"ANALYZE\",\"capability\":")
                       + json_quote(capability)
                       + ",\"input\":"
                       + json_quote(input.string())
                       + ",\"output\":"
                       + json_quote(output.string())
                       + "}";
    bool ok = send("{\"type\":\"INIT\",\"protocol\":1}") && send("{\"type\":\"QUERY_CAPABILITIES\"}");
    char buffer[4'096] = {};
    const ssize_t response_size = read(response_pipe[0], buffer, sizeof(buffer) - 1);
    if (response_size <= 0) ok = false;
    if (ok) ok = send(request) && send("{\"type\":\"DONE\"}");
    std::string responses;
    if (ok) {
        ssize_t result_size = 0;
        while ((result_size = read(response_pipe[0], buffer, sizeof(buffer))) > 0)
            responses.append(buffer, static_cast<size_t>(result_size));
        ok = responses.find("\"type\":\"ABI_MODULE\"") != std::string::npos
          && responses.find("\"status\":0") != std::string::npos;
    }
    close(response_pipe[0]);
    close(request_pipe[1]);
    int status = 1;
    waitpid(child, &status, 0);
    if (!ok) {
        error = "provider reported failure";
        if (!responses.empty()) {
            if (responses.size() > 240) responses.resize(240);
            error += ": " + responses;
        } else if (WIFEXITED(status)) {
            error += " (exit " + fmt::to_string(WEXITSTATUS(status)) + ")";
        }
    }
    return ok && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int generate(const fs::path &provider, const fs::path &input, const fs::path &destination) {
    fs::path output = destination;
    if (destination.extension() != ".hpp") output /= "amc_generated.hpp";
    std::error_code ec;
    fs::create_directories(output.parent_path(), ec);
    if (ec)
        return fail(
            amc::ErrorCategory::io, "generate", "cannot create output directory", output.string(), ec.message());
    std::string error;
    if (!dispatch_provider(provider, "backend", input, output, error))
        return fail(amc::ErrorCategory::provider, "backend", error, input.string());
    return 0;
}

void print_usage() {
    fmt::print(stderr,
        "usage:\n"
        "  amc build -c <file>.abic.toml [-B <build_dir>]\n"
        "  amc generate <file>.abix -l cpp|lua -o <directory|file>\n"
        "  amc validate <file>.abix\n"
        "  amc inspect <file>.abix\n"
        "  amc context <file>.abix [--format llm|json] [--no-names] [-o <file>]\n"
        "  amc query <file>.abix --type|--function <name> [--layout] [--format text|json]\n"
        "  amc query <file>.abix --compatible <other.abix> [--format text|json]\n"
        "  amc adapter <source.abix> <target.abix> [-o <file>] [--typed] [--shim]\n"
        "  amc publish <file.abix|binary> [--root <dir>] [--build-id <hex>]\n"
        "  amc fetch <binary>|--build-id <hex> [--root <dir>] [-o <file>]\n"
        "  amc metadata <file>.abix [--format bin|meta|json] [-o <file>]\n"
        "                            [--no-names] [--no-hash-index] [--build-id <hex>]\n"
        "  amc metadata --verify <region> [--format text|json]\n"
        "  amc metadata --from-elf <binary> [--format bin|json] [-o <file>]\n"
        "  amc verify <contract.abix> <implementation.abix> [--format text|json]\n"
        "  amc verify -c <file>.abic.toml [-B <build_dir>] [--format text|json]\n"
        "  amc diff <source.abix> <target.abix> [-o report.abix]\n"
        "  amc compatibility <source.abix> <target.abix>\n"
        "global options:\n"
        "  --error-format text|json   structured error envelope (default text)\n");
}

}   // namespace amc::cli
