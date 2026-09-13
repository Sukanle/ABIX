#include "amc_context.h"
#include "amc_core.h"
#include "amc_elf.h"
#include "amc_error.h"
#include "amc_lua.h"
#include "amc_metadata.h"
#include "amc_query.h"
#include "amc_symbol_store.h"
#include "amc_verify.h"
#include <toml++/toml.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
amc::ErrorFormat g_error_format = amc::ErrorFormat::text;

int fail(amc::ErrorCategory category, const std::string &stage, const std::string &message,
    const std::string &file = {}, const std::string &detail = {}) {
    const auto error = amc::make_error(category, stage, message, file, detail);
    std::cerr
        << (g_error_format == amc::ErrorFormat::json ? amc::error_to_json(error) : amc::error_to_text(error))
        << "\n";
    return 1;
}

int usage_error(const std::string &message) {
    fail(amc::ErrorCategory::usage, "options", message);
    return 2;
}

// read_abix reports both I/O and format failures through one string; split
// them back out so the structured envelope carries an accurate category.
amc::ErrorCategory read_error_category(const std::string &message) {
    if (message.rfind("cannot open", 0) == 0) return amc::ErrorCategory::io;
    return amc::ErrorCategory::format;
}

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
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value.hi << std::setw(16) << value.lo;
    return out.str();
}

// Minimal `.abix.meta` for a region extracted from a binary: the package/abi
// hash are not carried by the manifest, so only identity + counts are emitted.
std::string elf_meta_document(const std::string &key, const amc::MetadataHeader &header) {
    std::ostringstream out;
    out
        << "[metadata]\n"
        << "build_id = \"0x"
        << key
        << "\"\n"
        << "metadata_id = \"0x"
        << hash_key(header.metadata_id)
        << "\"\n"
        << "abi_version = "
        << header.version_major
        << "\n"
        << "hash_algorithm = "
        << header.hash_algorithm
        << "\n"
        << "type_count = "
        << header.type_count
        << "\n"
        << "field_count = "
        << header.field_count
        << "\n"
        << "function_count = "
        << header.function_count
        << "\n"
        << "parameter_count = "
        << header.parameter_count
        << "\n"
        << "symbol_count = "
        << header.symbol_count
        << "\n"
        << "source = \"elf\"\n";
    return out.str();
}

// Editor-consumable ABI diagnostics: one `file:line:column: error: ...` line
// per drift, using the contract artifact's Source Origin when available.
std::string abi_diagnostics(const amc::AbiModule &reference, const amc::VerifyResult &result) {
    std::ostringstream out;
    for (const auto &change : result.changes) {
        const amc::Type *type = nullptr;
        if (!change.type.empty()) {
            const auto indices = amc::query_type_indices(reference, change.type);
            if (!indices.empty()) type = &reference.types[indices.front()];
        }
        if (type != nullptr) {
            if (const auto *source = amc::find_source_origin(reference, type->id))
                out << source->file << ":" << source->line << ":" << source->column << ": ";
        }
        out << "error: ABI " << amc::change_kind_name(change.kind);
        if (!change.type.empty()) out << " for " << change.type;
        if (!change.name.empty()) out << "." << change.name;
        if (!change.detail.empty()) out << ": " << change.detail;
        out << "\n";
    }
    return out.str();
}

// Emits the `<artifact>.meta` sidecar described by the AI-PM plan so a symbol
// server or CI can index BuildID/MetadataID without parsing the `.abix`.
bool write_abix_meta(const amc::AbiModule &module, const fs::path &artifact, std::string &error) {
    amc::MetadataOptions options;
    std::vector<uint8_t> region;
    if (!amc::build_metadata_region(module, options, region, error)) return false;
    amc::MetadataHeader header;
    if (!amc::read_metadata_header(region, header, error)) return false;
    std::ofstream meta(artifact.string() + ".meta", std::ios::binary);
    if (!meta) {
        error = "cannot open metadata output: " + artifact.string() + ".meta";
        return false;
    }
    meta << amc::metadata_meta_document(module, header);
    if (!meta) {
        error = "failed to write metadata output: " + artifact.string() + ".meta";
        return false;
    }
    return true;
}

struct ExportSpec {
    fs::path output;
    std::vector<std::string> symbols;
};

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
            error += " (exit " + std::to_string(WEXITSTATUS(status)) + ")";
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
    std::cerr
        << "usage:\n"
        << "  amc build -c <file>.abic.toml [-B <build_dir>]\n"
        << "  amc generate <file>.abix -l cpp|lua -o <directory|file>\n"
        << "  amc validate <file>.abix\n"
        << "  amc inspect <file>.abix\n"
        << "  amc context <file>.abix [--format llm|json] [--no-names] [-o <file>]\n"
        << "  amc query <file>.abix --type|--function <name> [--layout] [--format text|json]\n"
        << "  amc query <file>.abix --compatible <other.abix> [--format text|json]\n"
        << "  amc publish <file.abix|binary> [--root <dir>] [--build-id <hex>]\n"
        << "  amc fetch <binary>|--build-id <hex> [--root <dir>] [-o <file>]\n"
        << "  amc metadata <file>.abix [--format bin|meta|json] [-o <file>]\n"
        << "                            [--no-names] [--no-hash-index] [--build-id <hex>]\n"
        << "  amc metadata --verify <region> [--format text|json]\n"
        << "  amc metadata --from-elf <binary> [--format bin|json] [-o <file>]\n"
        << "  amc verify <contract.abix> <implementation.abix> [--format text|json]\n"
        << "  amc verify -c <file>.abic.toml [-B <build_dir>] [--format text|json]\n"
        << "  amc diff <source.abix> <target.abix> [-o report.abix]\n"
        << "  amc compatibility <source.abix> <target.abix>\n"
        << "global options:\n"
        << "  --error-format text|json   structured error envelope (default text)\n";
}
}   // namespace

int main(int argc, char **argv) {
    // Strip the global --error-format option first so every subcommand sees a
    // clean argument vector, and so failures during the scan itself are
    // reported through the selected format.
    std::vector<std::string> filtered;
    filtered.emplace_back(argc > 0 ? argv[0] : "amc");
    bool explicit_error_format = false;
    bool format_json = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--error-format") {
            if (i + 1 >= argc) {
                fail(amc::ErrorCategory::usage, "options", "--error-format requires a value");
                return 2;
            }
            explicit_error_format = true;
            if (!amc::parse_error_format(argv[++i], g_error_format)) {
                fail(amc::ErrorCategory::usage, "options", "--error-format must be text or json");
                return 2;
            }
            continue;
        }
        const std::string error_prefix = "--error-format=";
        if (argument.rfind(error_prefix, 0) == 0) {
            explicit_error_format = true;
            if (!amc::parse_error_format(argument.substr(error_prefix.size()), g_error_format)) {
                fail(amc::ErrorCategory::usage, "options", "--error-format must be text or json");
                return 2;
            }
            continue;
        }
        if (argument == "--format" && i + 1 < argc && std::string(argv[i + 1]) == "json") format_json = true;
        if (argument == "--format=json") format_json = true;
        filtered.push_back(argument);
    }
    if (!explicit_error_format && format_json) g_error_format = amc::ErrorFormat::json;

    std::vector<char *> argv_storage;
    argv_storage.reserve(filtered.size());
    for (auto &argument : filtered)
        argv_storage.push_back(argument.data());
    const int arg_count = static_cast<int>(filtered.size());
    char **args = argv_storage.data();

    if (arg_count < 2) {
        print_usage();
        return 2;
    }
    const std::string command = args[1];
    const fs::path provider = fs::absolute(fs::path(args[0])).parent_path() / "amc-cpp";

    if (command == "--list-languages") {
        std::cout << "cpp\nlua\n";
        return 0;
    }
    if (command == "--describe-language") {
        if (arg_count != 3) return usage_error("--describe-language requires a language");
        const std::string language = args[2];
        if (language == "cpp") {
            std::cout << "cpp frontend backend protocol=jsonl-v1\n";
            return 0;
        }
        if (language == "lua") {
            std::cout << "lua backend protocol=direct contract=aue\n";
            return 0;
        }
        return usage_error("--describe-language supports cpp and lua");
    }

    if (command == "context") {
        fs::path input;
        fs::path output;
        std::string format = "llm";
        bool include_names = true;
        for (int i = 2; i < arg_count; ++i) {
            const std::string argument = args[i];
            if ((argument == "--format" || argument == "-o") && i + 1 < arg_count) {
                const std::string value = args[++i];
                if (argument == "--format")
                    format = value;
                else
                    output = value;
            } else if (argument.rfind("--format=", 0) == 0) {
                format = argument.substr(std::string("--format=").size());
            } else if (argument == "--no-names") {
                include_names = false;
            } else if (input.empty()) {
                input = argument;
            } else {
                return usage_error("context accepts a single .abix input");
            }
        }
        if (input.empty()) return usage_error("context requires an input .abix file");
        if (format != "llm" && format != "json") return usage_error("context --format must be llm or json");

        amc::AbiModule module;
        std::string error;
        if (!amc::load_module_source(input.string(), module, error))
            return fail(read_error_category(error), "read_abix", error, input.string());

        const std::string rendered =
            format == "json" ? amc::context_to_json(module, include_names) : amc::context_to_llm(module, include_names);
        if (output.empty()) {
            std::cout << rendered;
            return 0;
        }
        std::ofstream file(output, std::ios::binary);
        if (!file) return fail(amc::ErrorCategory::io, "context", "cannot open output", output.string());
        file << rendered;
        if (!file) return fail(amc::ErrorCategory::io, "context", "failed to write output", output.string());
        return 0;
    }

    if (command == "metadata") {
        fs::path input;
        fs::path output;
        std::string format = "bin";
        bool include_names = true;
        bool include_hash_index = true;
        bool verify = false;
        bool from_elf_flag = false;
        amc::Hash128 build_id{};
        for (int i = 2; i < arg_count; ++i) {
            const std::string argument = args[i];
            if (argument == "--verify") {
                verify = true;
            } else if (argument == "--from-elf") {
                from_elf_flag = true;
            } else if (argument == "--no-names") {
                include_names = false;
            } else if (argument == "--no-hash-index") {
                include_hash_index = false;
            } else if ((argument == "--format" || argument == "-o" || argument == "--build-id") && i + 1 < arg_count) {
                const std::string value = args[++i];
                if (argument == "--format")
                    format = value;
                else if (argument == "-o")
                    output = value;
                else if (!parse_hash128(value, build_id))
                    return usage_error("--build-id expects 32 hexadecimal digits");
            } else if (argument.rfind("--format=", 0) == 0) {
                format = argument.substr(std::string("--format=").size());
            } else if (input.empty()) {
                input = argument;
            } else {
                return usage_error("metadata accepts a single input file");
            }
        }
        if (input.empty()) return usage_error("metadata requires an input file");

        if (verify) {
            if (format != "text" && format != "json" && format != "bin")
                return usage_error("metadata --verify --format must be text or json");
            std::ifstream file(input, std::ios::binary);
            if (!file) return fail(amc::ErrorCategory::io, "metadata", "cannot open input", input.string());
            std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            std::vector<uint8_t> region;
            std::string error;
            // A raw region starts with the manifest; an ELF binary embeds it
            // in the `.abix.metadata` section.
            if (from_elf_flag || amc::is_elf(bytes.data(), bytes.size())) {
                if (!amc::find_elf_section(bytes.data(), bytes.size(), ".abix.metadata", region, error))
                    return fail(amc::ErrorCategory::format, "metadata", error, input.string());
            } else {
                region = std::move(bytes);
            }
            amc::MetadataHeader header;
            if (!amc::read_metadata_header(region, header, error) || !amc::verify_metadata_region(region, error))
                return fail(amc::ErrorCategory::validation, "verify_metadata", error, input.string());
            if (format == "json") {
                std::cout << amc::metadata_header_to_json(header);
            } else {
                std::cout
                    << "metadata: valid metadata_id=0x"
                    << std::hex
                    << header.metadata_id.hi
                    << header.metadata_id.lo
                    << std::dec
                    << " types="
                    << header.type_count
                    << " fields="
                    << header.field_count
                    << " functions="
                    << header.function_count
                    << " symbols="
                    << header.symbol_count
                    << "\n";
            }
            return 0;
        }

        if (from_elf_flag) {
            if (format != "bin" && format != "json")
                return usage_error("metadata --from-elf supports --format bin or json");
            std::ifstream file(input, std::ios::binary);
            if (!file) return fail(amc::ErrorCategory::io, "metadata", "cannot open input", input.string());
            std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            std::vector<uint8_t> region;
            std::string error;
            if (!amc::find_elf_section(bytes.data(), bytes.size(), ".abix.metadata", region, error))
                return fail(amc::ErrorCategory::format, "metadata", error, input.string());
            amc::MetadataHeader header;
            if (!amc::read_metadata_header(region, header, error))
                return fail(amc::ErrorCategory::format, "read_metadata_header", error, input.string());
            if (!amc::verify_metadata_region(region, error))
                return fail(amc::ErrorCategory::validation, "verify_metadata", error, input.string());
            if (format == "json") {
                std::cout << amc::metadata_header_to_json(header);
                return 0;
            }
            if (output.empty()) {
                std::cout.write(
                    reinterpret_cast<const char *>(region.data()), static_cast<std::streamsize>(region.size()));
                return 0;
            }
            std::ofstream out(output, std::ios::binary);
            if (!out) return fail(amc::ErrorCategory::io, "metadata", "cannot open output", output.string());
            out.write(reinterpret_cast<const char *>(region.data()), static_cast<std::streamsize>(region.size()));
            if (!out) return fail(amc::ErrorCategory::io, "metadata", "failed to write output", output.string());
            return 0;
        }

        if (format != "bin" && format != "meta" && format != "json")
            return usage_error("metadata --format must be bin, meta or json");

        amc::AbiModule module;
        std::string error;
        if (!amc::load_module_source(input.string(), module, error))
            return fail(read_error_category(error), "read_abix", error, input.string());

        amc::MetadataOptions options;
        options.build_id = build_id;
        options.include_names = include_names;
        options.include_hash_index = include_hash_index;
        std::vector<uint8_t> region;
        if (!amc::build_metadata_region(module, options, region, error))
            return fail(amc::ErrorCategory::internal, "build_metadata_region", error, input.string());
        amc::MetadataHeader header;
        if (!amc::read_metadata_header(region, header, error))
            return fail(amc::ErrorCategory::internal, "read_metadata_header", error, input.string());

        if (format == "bin") {
            if (output.empty()) {
                std::cout.write(
                    reinterpret_cast<const char *>(region.data()), static_cast<std::streamsize>(region.size()));
                return 0;
            }
            std::ofstream file(output, std::ios::binary);
            if (!file) return fail(amc::ErrorCategory::io, "metadata", "cannot open output", output.string());
            file.write(reinterpret_cast<const char *>(region.data()), static_cast<std::streamsize>(region.size()));
            if (!file) return fail(amc::ErrorCategory::io, "metadata", "failed to write output", output.string());
            return 0;
        }

        const std::string rendered =
            format == "json" ? amc::metadata_header_to_json(header) : amc::metadata_meta_document(module, header);
        if (output.empty()) {
            std::cout << rendered;
            return 0;
        }
        std::ofstream file(output, std::ios::binary);
        if (!file) return fail(amc::ErrorCategory::io, "metadata", "cannot open output", output.string());
        file << rendered;
        if (!file) return fail(amc::ErrorCategory::io, "metadata", "failed to write output", output.string());
        return 0;
    }

    if (command == "query") {
        fs::path input;
        fs::path other;
        std::string needle;
        std::string mode;
        std::string format = "text";
        bool include_layout = false;
        for (int i = 2; i < arg_count; ++i) {
            const std::string argument = args[i];
            if ((argument == "--type"
                    || argument == "--function"
                    || argument == "--compatible"
                    || argument == "--format")
                && i + 1 < arg_count) {
                const std::string value = args[++i];
                if (argument == "--type") {
                    mode = "type";
                    needle = value;
                } else if (argument == "--function") {
                    mode = "function";
                    needle = value;
                } else if (argument == "--compatible") {
                    mode = "compatible";
                    other = value;
                } else
                    format = value;
            } else if (argument == "--layout") {
                include_layout = true;
            } else if (argument.rfind("--format=", 0) == 0) {
                format = argument.substr(std::string("--format=").size());
            } else if (input.empty()) {
                input = argument;
            } else {
                return usage_error("query accepts a single .abix input");
            }
        }
        if (input.empty()) return usage_error("query requires a .abix input");
        if (mode.empty()) return usage_error("query requires --type, --function or --compatible");
        if (format != "text" && format != "json") return usage_error("query --format must be text or json");
        const bool json = format == "json";

        amc::AbiModule module;
        std::string error;
        if (!amc::load_module_source(input.string(), module, error))
            return fail(read_error_category(error), "read_abix", error, input.string());

        if (mode == "type") {
            const auto indices = amc::query_type_indices(module, needle);
            std::cout << (json ? amc::query_types_json(module, indices, include_layout, needle)
                               : amc::query_types_text(module, indices, include_layout));
            return indices.empty() ? 1 : 0;
        }
        if (mode == "function") {
            const auto indices = amc::query_function_indices(module, needle);
            std::cout << (json ? amc::query_functions_json(module, indices, needle)
                               : amc::query_functions_text(module, indices));
            return indices.empty() ? 1 : 0;
        }

        amc::AbiModule target;
        if (!amc::load_module_source(other.string(), target, error))
            return fail(read_error_category(error), "read_abix", error, other.string());
        amc::VerifyResult result;
        if (!amc::verify_modules(module, target, result, error))
            return fail(amc::ErrorCategory::internal, "verify", error);
        std::cout << (json ? amc::compatibility_query_json(module, target, result)
                           : amc::compatibility_query_text(module, target, result));
        return result.consistent ? 0 : 1;
    }

    if (command == "publish" || command == "fetch") {
        fs::path input;
        fs::path output;
        fs::path root;
        std::string explicit_build_id;
        for (int i = 2; i < arg_count; ++i) {
            const std::string argument = args[i];
            if ((argument == "--root" || argument == "-o" || argument == "--build-id") && i + 1 < arg_count) {
                const std::string value = args[++i];
                if (argument == "--root")
                    root = value;
                else if (argument == "-o")
                    output = value;
                else
                    explicit_build_id = value;
            } else if (argument.rfind("--root=", 0) == 0) {
                root = argument.substr(std::string("--root=").size());
            } else if (argument.rfind("--build-id=", 0) == 0) {
                explicit_build_id = argument.substr(std::string("--build-id=").size());
            } else if (input.empty()) {
                input = argument;
            } else {
                return usage_error(command + " accepts a single input");
            }
        }
        if (root.empty()) root = amc::default_symbol_store_root();

        if (command == "publish") {
            if (input.empty()) return usage_error("publish requires an input .abix or binary");
            std::ifstream file(input, std::ios::binary);
            if (!file) return fail(amc::ErrorCategory::io, "publish", "cannot open input", input.string());
            std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            std::string error, key, meta, suffix;
            std::vector<uint8_t> artifact;
            if (amc::is_elf(bytes.data(), bytes.size())) {
                std::vector<uint8_t> build_id;
                if (!amc::read_gnu_build_id(bytes.data(), bytes.size(), build_id, error))
                    return fail(amc::ErrorCategory::format, "publish", error, input.string());
                key = amc::hex_encode(build_id.data(), build_id.size());
                if (!amc::find_elf_section(bytes.data(), bytes.size(), ".abix.metadata", artifact, error))
                    return fail(amc::ErrorCategory::format, "publish", error, input.string());
                amc::MetadataHeader header;
                if (!amc::read_metadata_header(artifact, header, error))
                    return fail(amc::ErrorCategory::format, "publish", error, input.string());
                meta = elf_meta_document(key, header);
                suffix = ".abixmeta";
            } else {
                amc::AbiModule module;
                if (!amc::load_module_source(input.string(), module, error))
                    return fail(read_error_category(error), "read_abix", error, input.string());
                amc::MetadataOptions options;
                std::vector<uint8_t> region;
                if (!amc::build_metadata_region(module, options, region, error))
                    return fail(amc::ErrorCategory::internal, "build_metadata_region", error, input.string());
                amc::MetadataHeader header;
                if (!amc::read_metadata_header(region, header, error))
                    return fail(amc::ErrorCategory::internal, "read_metadata_header", error, input.string());
                if (!explicit_build_id.empty()) {
                    if (!amc::normalize_build_id(explicit_build_id, key))
                        return usage_error("--build-id expects hexadecimal digits");
                } else if (fs::exists(input.string() + ".meta")) {
                    try {
                        const auto table = toml::parse_file(input.string() + ".meta");
                        const auto side = table["metadata"]["build_id"].value<std::string>();
                        std::string normalized;
                        if (side
                            && amc::normalize_build_id(*side, normalized)
                            && normalized.find_first_not_of('0') != std::string::npos)
                            key = normalized;
                    } catch (const toml::parse_error &) {
                        // A malformed sidecar is not fatal: fall back to MetadataID.
                    }
                }
                if (key.empty()) key = hash_key(header.metadata_id);
                artifact = bytes;
                meta = amc::metadata_meta_document(module, header);
                suffix = ".abix";
            }
            std::string stored_path;
            if (!amc::publish_symbol(root.string(), key, artifact, meta, suffix.c_str(), stored_path, error))
                return fail(amc::ErrorCategory::io, "publish", error, input.string());
            std::cout << "published key=" << key << " path=" << stored_path << "\n";
            return 0;
        }

        std::string key;
        if (!explicit_build_id.empty()) {
            if (!amc::normalize_build_id(explicit_build_id, key))
                return usage_error("--build-id expects hexadecimal digits");
        } else {
            if (input.empty()) return usage_error("fetch requires a binary or --build-id");
            std::ifstream file(input, std::ios::binary);
            if (!file) return fail(amc::ErrorCategory::io, "fetch", "cannot open input", input.string());
            std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            if (!amc::is_elf(bytes.data(), bytes.size()))
                return fail(amc::ErrorCategory::format, "fetch", "input is not an ELF binary", input.string(),
                    "pass --build-id to look up by id directly");
            std::vector<uint8_t> build_id;
            std::string error;
            if (!amc::read_gnu_build_id(bytes.data(), bytes.size(), build_id, error))
                return fail(amc::ErrorCategory::format, "fetch", error, input.string());
            key = amc::hex_encode(build_id.data(), build_id.size());
        }
        std::vector<uint8_t> artifact;
        std::string path, error;
        if (!amc::fetch_symbol(root.string(), key, artifact, path, error))
            return fail(amc::ErrorCategory::io, "fetch", error);
        if (output.empty()) {
            std::cout << path << "\n";
            return 0;
        }
        std::ofstream out(output, std::ios::binary);
        if (!out) return fail(amc::ErrorCategory::io, "fetch", "cannot open output", output.string());
        out.write(reinterpret_cast<const char *>(artifact.data()), static_cast<std::streamsize>(artifact.size()));
        if (!out) return fail(amc::ErrorCategory::io, "fetch", "failed to write output", output.string());
        std::cout << "fetched " << path << " -> " << output.string() << "\n";
        return 0;
    }

    if (command == "verify") {
        fs::path config_path;
        fs::path build_dir;
        std::vector<std::string> positionals;
        std::string format = "text";
        for (int i = 2; i < arg_count; ++i) {
            const std::string argument = args[i];
            if ((argument == "-c" || argument == "-B" || argument == "--format") && i + 1 < arg_count) {
                const std::string value = args[++i];
                if (argument == "-c")
                    config_path = value;
                else if (argument == "-B")
                    build_dir = fs::absolute(value);
                else
                    format = value;
            } else if (argument.rfind("--format=", 0) == 0) {
                format = argument.substr(std::string("--format=").size());
            } else {
                positionals.push_back(argument);
            }
        }
        if (format != "text" && format != "json" && format != "diagnostics")
            return usage_error("verify --format must be text, json or diagnostics");
        const bool json_output = format == "json";
        const bool diagnostics = format == "diagnostics";

        if (!positionals.empty()) {
            // Two-file form: amc verify <contract.abix> <implementation.abix>
            if (positionals.size() != 2)
                return usage_error("verify expects <contract.abix> <implementation.abix> or -c <file>.abic.toml");
            amc::AbiModule contract, implementation;
            std::string error;
            if (!amc::load_module_source(positionals[0], contract, error))
                return fail(read_error_category(error), "read_abix", error, positionals[0]);
            if (!amc::load_module_source(positionals[1], implementation, error))
                return fail(read_error_category(error), "read_abix", error, positionals[1]);
            amc::VerifyResult result;
            if (!amc::verify_modules(contract, implementation, result, error))
                return fail(amc::ErrorCategory::internal, "verify", error);
            result.contract_path = positionals[0];
            result.implementation_path = positionals[1];
            if (diagnostics)
                std::cout << abi_diagnostics(contract, result);
            else
                std::cout << (json_output ? amc::verify_report_to_json({result}, result.consistent)
                                          : amc::verify_report_to_text({result}));
            return result.consistent ? 0 : 1;
        }

        if (config_path.empty()) {
            print_usage();
            return 2;
        }

        try {
            const auto config = toml::parse_file(config_path.string());
            const auto *imports = config["import"].as_array();
            if (!imports || imports->size() != 1)
                return fail(
                    amc::ErrorCategory::config, "verify", "MVP requires exactly one [[import]]", config_path.string());
            std::vector<ExportSpec> exports;
            std::string error;
            if (!parse_exports(config, exports, error))
                return fail(amc::ErrorCategory::config, "verify", error, config_path.string());

            const fs::path base = build_dir.empty() ? fs::absolute(config_path).parent_path() : build_dir;
            const fs::path temporary = base / ".amc" / "verify-frontend.abix";
            std::error_code ec;
            fs::create_directories(temporary.parent_path(), ec);
            if (ec)
                return fail(
                    amc::ErrorCategory::io, "verify", "cannot create build directory", base.string(), ec.message());
            if (!dispatch_provider(provider, "frontend", config_path, temporary, error))
                return fail(amc::ErrorCategory::provider, "frontend", error, config_path.string());

            amc::AbiModule full;
            if (!amc::read_abix(temporary.string(), full, error))
                return fail(read_error_category(error), "read_abix", error, temporary.string());
            full.package_name = config["package"]["name"].value_or("cpp");
            full.package_version = config["package"]["version"].value_or("");

            std::vector<amc::VerifyResult> results;
            std::string diagnostics_buffer;
            bool consistent = true;
            for (size_t index = 0; index < exports.size(); ++index) {
                const auto &spec = exports[index];
                const fs::path reference = spec.output.is_absolute() ? spec.output : base / spec.output;
                if (!fs::exists(reference)) {
                    fs::remove(temporary, ec);
                    return fail(amc::ErrorCategory::io, "verify", "reference contract artifact not found",
                        reference.string(), "build the contract before verifying");
                }
                amc::AbiModule projected;
                if (!amc::project_symbols(full, spec.symbols, projected, error))
                    return fail(amc::ErrorCategory::validation, "project_symbols", error, config_path.string());
                const fs::path fresh = base / ".amc" / ("verify-" + std::to_string(index) + ".abix");
                if (!amc::write_abix(projected, fresh.string(), error))
                    return fail(amc::ErrorCategory::io, "write_abix", error, fresh.string());

                amc::AbiModule contract;
                if (!amc::read_abix(reference.string(), contract, error))
                    return fail(read_error_category(error), "read_abix", error, reference.string());
                amc::VerifyResult result;
                if (!amc::verify_modules(contract, projected, result, error))
                    return fail(amc::ErrorCategory::internal, "verify", error);
                result.contract_path = reference.string();
                result.implementation_path = config_path.string() + " (regenerated)";
                consistent = consistent && result.consistent;
                if (diagnostics) diagnostics_buffer += abi_diagnostics(contract, result);
                results.push_back(std::move(result));
                fs::remove(fresh, ec);
            }
            fs::remove(temporary, ec);

            if (diagnostics)
                std::cout << diagnostics_buffer;
            else
                std::cout << (json_output ? amc::verify_report_to_json(results, consistent)
                                          : amc::verify_report_to_text(results));
            return consistent ? 0 : 1;
        } catch (const toml::parse_error &error) {
            return fail(amc::ErrorCategory::config, "verify", std::string(error.description()), config_path.string());
        }
    }

    if (command == "diff" || command == "compatibility") {
        if (arg_count != 4 && !(command == "diff" && arg_count == 6 && std::string(args[4]) == "-o"))
            return usage_error(command + " expects two .abix inputs");
        amc::AbiModule source, target, report;
        std::string error;
        if (!amc::load_module_source(args[2], source, error))
            return fail(read_error_category(error), "read_abix", error, args[2]);
        if (!amc::load_module_source(args[3], target, error))
            return fail(read_error_category(error), "read_abix", error, args[3]);
        if (!amc::build_compatibility(source, target, report, error))
            return fail(amc::ErrorCategory::compatibility, "build_compatibility", error);
        bool incompatible = false;
        for (const auto &record : report.compatibility) {
            std::cout
                << amc::compatibility_name(record.kind)
                << " "
                << record.source_type.lo
                << ":"
                << record.source_type.hi
                << " -> "
                << record.target_type.lo
                << ":"
                << record.target_type.hi;
            if (record.map_index != UINT32_MAX) std::cout << " map=" << record.map_index;
            std::cout << "\n";
            incompatible = incompatible || record.kind == amc::CompatibilityKind::incompatible;
        }
        if (command == "diff" && arg_count == 6 && !amc::write_abix(report, args[5], error))
            return fail(amc::ErrorCategory::io, "write_abix", error, args[5]);
        return command == "compatibility" && incompatible ? 1 : 0;
    }

    if (command == "generate" || command == "backend") {
        if (arg_count != 7 || std::string(args[3]) != "-l" || std::string(args[5]) != "-o")
            return usage_error(command + " expects -l cpp|lua -o <directory|file>");
        const std::string language = args[4];
        if (language == "lua") {
            if (command == "backend") return usage_error("backend is a C++ provider capability; use generate for lua");
            amc::AbiModule module;
            std::string error;
            if (!amc::load_module_source(args[2], module, error))
                return fail(read_error_category(error), "read_abix", error, args[2]);
            const fs::path requested = args[6];
            const bool conformance = requested.extension() == ".lua";
            const std::string contract =
                conformance ? amc::generate_lua_conformance(module, error) : amc::generate_lua_contract(module, error);
            if (!error.empty()) return fail(amc::ErrorCategory::internal, "generate_lua", error, args[2]);
            fs::path output = requested;
            if (output.extension() != ".hpp" && output.extension() != ".lua") output /= "amc_lua_contract.hpp";
            std::error_code ec;
            if (!output.parent_path().empty()) {
                fs::create_directories(output.parent_path(), ec);
                if (ec)
                    return fail(amc::ErrorCategory::io, "generate", "cannot create output directory", output.string(),
                        ec.message());
            }
            std::ofstream file(output, std::ios::binary);
            if (!file) return fail(amc::ErrorCategory::io, "generate", "cannot open output", output.string());
            file << contract;
            if (!file) return fail(amc::ErrorCategory::io, "generate", "failed to write output", output.string());
            return 0;
        }
        if (language != "cpp") return usage_error(command + " expects -l cpp|lua -o <directory|file>");
        if (command == "backend") {
            std::string error;
            if (!dispatch_provider(provider, "backend", args[2], args[6], error))
                return fail(amc::ErrorCategory::provider, "backend", error, args[2]);
            return 0;
        }
        return generate(provider, args[2], args[6]);
    }

    if (command == "frontend") {
        if (arg_count != 8
            || std::string(args[2]) != "-l"
            || std::string(args[3]) != "cpp"
            || std::string(args[4]) != "-c"
            || std::string(args[6]) != "-o")
            return usage_error("frontend expects -l cpp -c <config> -o <output>");
        std::string error;
        if (!dispatch_provider(provider, "frontend", args[5], args[7], error))
            return fail(amc::ErrorCategory::provider, "frontend", error, args[5]);
        return 0;
    }

    if (command == "build") {
        fs::path config_path;
        fs::path build_dir;
        for (int i = 2; i < arg_count; ++i) {
            const std::string argument = args[i];
            if ((argument == "-c" || argument == "-B") && i + 1 < arg_count) {
                const fs::path value = args[++i];
                if (argument == "-c")
                    config_path = value;
                else
                    build_dir = fs::absolute(value);
            } else if (config_path.empty()) {
                config_path = argument;
            } else {
                print_usage();
                return 2;
            }
        }
        if (config_path.empty()) {
            print_usage();
            return 2;
        }
        try {
            const auto config = toml::parse_file(config_path.string());
            const auto *imports = config["import"].as_array();
            if (!imports || imports->size() != 1)
                return fail(
                    amc::ErrorCategory::config, "build", "MVP requires exactly one [[import]]", config_path.string());
            std::vector<ExportSpec> exports;
            std::string error;
            if (!parse_exports(config, exports, error))
                return fail(amc::ErrorCategory::config, "build", error, config_path.string());
            const fs::path base = build_dir.empty() ? fs::absolute(config_path).parent_path() : build_dir;
            const fs::path temporary = base / ".amc" / "frontend.abix";
            std::error_code ec;
            fs::create_directories(temporary.parent_path(), ec);
            if (ec)
                return fail(
                    amc::ErrorCategory::io, "build", "cannot create build directory", base.string(), ec.message());
            if (!dispatch_provider(provider, "frontend", config_path, temporary, error))
                return fail(amc::ErrorCategory::provider, "frontend", error, config_path.string());

            amc::AbiModule full;
            if (!amc::read_abix(temporary.string(), full, error))
                return fail(read_error_category(error), "read_abix", error, temporary.string());
            full.package_name = config["package"]["name"].value_or("cpp");
            full.package_version = config["package"]["version"].value_or("");
            for (const auto &spec : exports) {
                const fs::path output = spec.output.is_absolute() ? spec.output : base / spec.output;
                fs::create_directories(output.parent_path(), ec);
                if (ec)
                    return fail(amc::ErrorCategory::io, "build", "cannot create export directory", output.string(),
                        ec.message());
                amc::AbiModule projected;
                if (!amc::project_symbols(full, spec.symbols, projected, error))
                    return fail(amc::ErrorCategory::validation, "project_symbols", error, config_path.string());
                if (!amc::write_abix(projected, output.string(), error))
                    return fail(amc::ErrorCategory::io, "write_abix", error, output.string());
                // AI-PM: emit the `.abix.meta` sidecar for symbol-server/CI indexing.
                if (!write_abix_meta(projected, output, error))
                    return fail(amc::ErrorCategory::io, "write_abix_meta", error, output.string() + ".meta");
            }
            fs::remove(temporary, ec);
            return 0;
        } catch (const toml::parse_error &error) {
            return fail(amc::ErrorCategory::config, "build", std::string(error.description()), config_path.string());
        }
    }

    if (command == "inspect" || command == "validate") {
        if (arg_count != 3) return usage_error(command + " expects exactly one .abix input");
        amc::AbiModule module;
        std::string error;
        if (!amc::load_module_source(args[2], module, error))
            return fail(read_error_category(error), "read_abix", error, args[2]);
        if (command == "inspect")
            std::cout
                << "package="
                << module.package_name
                << " types="
                << module.types.size()
                << " fields="
                << module.fields.size()
                << " functions="
                << module.functions.size()
                << "\n";
        return 0;
    }

    print_usage();
    return 2;
}
