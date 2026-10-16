#include "AMC/CLI/CLI.h"

#include <exception>

#include <fmt/format.h>
#include <fmt/os.h>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <sys/wait.h>
#  include <unistd.h>
#endif

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
std::string hash_key(amc::Hash128 value) { return fmt::format("{:016x}{:016x}", value.hi, value.lo); }

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

fs::path provider_executable(const fs::path &base_provider, const std::string &language) {
    if (language.empty() || language == "cpp") return base_provider;
    return base_provider.parent_path() / ("amc-" + language);
}

namespace {

// Minimal cross-platform child process running `provider --ipc`: a request pipe
// (parent -> child stdin) and a response pipe (child stdout -> parent).
struct ProviderProcess {
#if defined(_WIN32)
    HANDLE process = nullptr;
    HANDLE request_write = nullptr;
    HANDLE response_read = nullptr;
#else
    pid_t pid = -1;
    int request_write = -1;
    int response_read = -1;
#endif
};

#if defined(_WIN32)
bool provider_spawn(const fs::path &provider, ProviderProcess &proc, std::string &error) {
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE request_read = nullptr;
    HANDLE response_write = nullptr;
    if (!CreatePipe(&request_read, &proc.request_write, &security, 0)
        || !CreatePipe(&proc.response_read, &response_write, &security, 0)) {
        error = "cannot create provider pipes";
        return false;
    }
    // Hand only the child's pipe ends to the child.
    SetHandleInformation(proc.request_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(proc.response_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = request_read;
    startup.hStdOutput = response_write;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    std::string command = "\"" + provider.string() + "\" --ipc";
    PROCESS_INFORMATION info{};
    const BOOL created =
        CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &info);
    CloseHandle(request_read);
    CloseHandle(response_write);
    if (!created) {
        error = "cannot create provider process";
        return false;
    }
    CloseHandle(info.hThread);
    proc.process = info.hProcess;
    return true;
}

bool provider_write(const ProviderProcess &proc, const std::string &line) {
    DWORD written = 0;
    return WriteFile(proc.request_write, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) != 0
        && written == line.size();
}

long provider_read(const ProviderProcess &proc, char *buffer, size_t size) {
    DWORD count = 0;
    if (ReadFile(proc.response_read, buffer, static_cast<DWORD>(size), &count, nullptr) == 0) return 0;
    return static_cast<long>(count);
}

int provider_wait(ProviderProcess &proc) {
    if (proc.request_write != nullptr) CloseHandle(proc.request_write);
    if (proc.response_read != nullptr) CloseHandle(proc.response_read);
    DWORD code = 1;
    WaitForSingleObject(proc.process, INFINITE);
    GetExitCodeProcess(proc.process, &code);
    CloseHandle(proc.process);
    return static_cast<int>(code);
}
#else
bool provider_spawn(const fs::path &provider, ProviderProcess &proc, std::string &error) {
    int request_pipe[2] = {};
    int response_pipe[2] = {};
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
    proc.pid = child;
    proc.request_write = request_pipe[1];
    proc.response_read = response_pipe[0];
    return true;
}

bool provider_write(const ProviderProcess &proc, const std::string &line) {
    return write(proc.request_write, line.data(), line.size()) == static_cast<ssize_t>(line.size());
}

long provider_read(const ProviderProcess &proc, char *buffer, size_t size) {
    return static_cast<long>(read(proc.response_read, buffer, size));
}

int provider_wait(ProviderProcess &proc) {
    if (proc.request_write >= 0) close(proc.request_write);
    if (proc.response_read >= 0) close(proc.response_read);
    int status = 1;
    waitpid(proc.pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
#endif

}   // namespace

bool dispatch_provider(const fs::path &provider, const char *capability, const fs::path &input, const fs::path &output,
    std::string &error) {
    ProviderProcess child;
    if (!provider_spawn(provider, child, error)) return false;
    const auto send = [&](const std::string &message) { return provider_write(child, message + "\n"); };
    const auto request = std::string("{\"type\":\"ANALYZE\",\"capability\":")
                       + json_quote(capability)
                       + ",\"input\":"
                       + json_quote(input.string())
                       + ",\"output\":"
                       + json_quote(output.string())
                       + "}";
    bool ok = send("{\"type\":\"INIT\",\"protocol\":1}") && send("{\"type\":\"QUERY_CAPABILITIES\"}");
    char buffer[4'096] = {};
    const long response_size = provider_read(child, buffer, sizeof(buffer) - 1);
    if (response_size <= 0) ok = false;
    if (ok) ok = send(request) && send("{\"type\":\"DONE\"}");
    std::string responses;
    if (ok) {
        long result_size = 0;
        while ((result_size = provider_read(child, buffer, sizeof(buffer))) > 0)
            responses.append(buffer, static_cast<size_t>(result_size));
        ok = responses.find("\"type\":\"ABI_MODULE\"") != std::string::npos
          && responses.find("\"status\":0") != std::string::npos;
    }
    const int exit_code = provider_wait(child);
    if (!ok) {
        error = "provider reported failure";
        if (!responses.empty()) {
            if (responses.size() > 240) responses.resize(240);
            error += ": " + responses;
        } else {
            error += " (exit " + fmt::to_string(exit_code) + ")";
        }
    }
    return ok && exit_code == 0;
}

int generate(
    const fs::path &provider, const fs::path &input, const fs::path &destination, const std::string &extension) {
    fs::path output = destination;
    if (destination.extension() != extension) output /= ("amc_generated" + extension);
    std::error_code ec;
    if (!output.parent_path().empty()) {
        fs::create_directories(output.parent_path(), ec);
        if (ec)
            return fail(
                amc::ErrorCategory::io, "generate", "cannot create output directory", output.string(), ec.message());
    }
    std::string error;
    if (!dispatch_provider(provider, "backend", input, output, error))
        return fail(amc::ErrorCategory::provider, "backend", error, input.string());
    return 0;
}

void print_usage() {
    fmt::print(stderr,
        "usage:\n"
        "  amc build -c <file>.abic.toml [-B <build_dir>] [--cache <dir>] [--deps <files>]\n"
        "  amc generate <file>.abix -l cpp|lua|rust|zig -o <directory|file>\n"
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
        "  amc target <file.abix|binary> [--expect arch=..,os=..,pointer_bits=..,endianness=..] [--format text|json]\n"
        "  amc diff <source.abix> <target.abix> [-o report.abix]\n"
        "  amc compatibility <source.abix> <target.abix>\n"
        "  amc cache <dir> [--verify|--clear] [--format text|json]\n"
        "global options:\n"
        "  --error-format text|json   structured error envelope (default text)\n");
}

}   // namespace amc::cli
