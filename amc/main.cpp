#include "core/amc_core.h"
#include <toml++/toml.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
struct ExportSpec {
    fs::path output;
    std::vector<std::string> symbols;
};

std::string json_quote(const std::string &value) {
    std::string result = "\"";
    for (char c : value) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else result += c;
    }
    return result + '"';
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
            for (const auto &symbol : *symbols) result.symbols.push_back(symbol.value_or(""));
        exports.push_back(std::move(result));
    }
    return true;
}

int dispatch_provider(const fs::path &provider, const char *capability,
                      const fs::path &input, const fs::path &output) {
    int request_pipe[2] = {}, response_pipe[2] = {};
    if (pipe(request_pipe) != 0 || pipe(response_pipe) != 0) return 1;
    const pid_t child = fork();
    if (child < 0) return 1;
    if (child == 0) {
        dup2(request_pipe[0], STDIN_FILENO); dup2(response_pipe[1], STDOUT_FILENO);
        close(request_pipe[0]); close(request_pipe[1]); close(response_pipe[0]); close(response_pipe[1]);
        execl(provider.c_str(), provider.c_str(), "--ipc", static_cast<char *>(nullptr));
        _exit(127);
    }
    close(request_pipe[0]); close(response_pipe[1]);
    const auto send = [&](const std::string &message) {
        const auto line = message + "\n";
        return write(request_pipe[1], line.data(), line.size()) == static_cast<ssize_t>(line.size());
    };
    const auto request = std::string("{\"type\":\"ANALYZE\",\"capability\":") + json_quote(capability)
        + ",\"input\":" + json_quote(input.string()) + ",\"output\":" + json_quote(output.string()) + "}";
    bool ok = send("{\"type\":\"INIT\",\"protocol\":1}") && send("{\"type\":\"QUERY_CAPABILITIES\"}");
    char buffer[4096] = {};
    const ssize_t response_size = read(response_pipe[0], buffer, sizeof(buffer) - 1);
    if (response_size <= 0) ok = false;
    if (ok) ok = send(request) && send("{\"type\":\"DONE\"}");
    if (ok) {
        std::string responses;
        ssize_t result_size = 0;
        while ((result_size = read(response_pipe[0], buffer, sizeof(buffer))) > 0)
            responses.append(buffer, static_cast<size_t>(result_size));
        ok = responses.find("\"type\":\"ABI_MODULE\"") != std::string::npos &&
             responses.find("\"status\":0") != std::string::npos;
    }
    close(response_pipe[0]);
    close(request_pipe[1]);
    int status = 1; waitpid(child, &status, 0);
    return ok && WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

int generate(const fs::path &provider, const fs::path &input, const fs::path &destination) {
    fs::path output = destination;
    if (destination.extension() != ".hpp") output /= "amc_generated.hpp";
    std::error_code ec;
    fs::create_directories(output.parent_path(), ec);
    if (ec) {
        std::cerr << "cannot create output directory: " << ec.message() << "\n";
        return 1;
    }
    return dispatch_provider(provider, "backend", input, output);
}

void print_usage() {
    std::cerr << "usage:\n"
              << "  amc build -c <file>.abic.toml [-B <build_dir>]\n"
              << "  amc generate <file>.abix -l cpp -o <directory|file.hpp>\n"
              << "  amc validate <file>.abix\n"
              << "  amc inspect <file>.abix\n"
              << "  amc diff <source.abix> <target.abix> [-o report.abix]\n"
              << "  amc compatibility <source.abix> <target.abix>\n";
}
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 2;
    }
    const std::string command = argv[1];
    const fs::path provider = fs::absolute(fs::path(argv[0])).parent_path() / "amc-cpp";

    if (command == "--list-languages") {
        std::cout << "cpp\n";
        return 0;
    }
    if (command == "--describe-language") {
        if (argc != 3 || std::string(argv[2]) != "cpp") return 2;
        std::cout << "cpp frontend backend protocol=jsonl-v1\n";
        return 0;
    }

    if (command == "diff" || command == "compatibility") {
        if (argc != 4 && !(command == "diff" && argc == 6 && std::string(argv[4]) == "-o")) {
            print_usage(); return 2;
        }
        amc::AbiModule source, target, report;
        std::string error;
        if (!amc::read_abix(argv[2], source, error) || !amc::read_abix(argv[3], target, error) ||
            !amc::build_compatibility(source, target, report, error)) {
            std::cerr << error << "\n";
            return 1;
        }
        bool incompatible = false;
        for (const auto &record : report.compatibility) {
            std::cout << amc::compatibility_name(record.kind) << " "
                      << record.source_type.lo << ":" << record.source_type.hi << " -> "
                      << record.target_type.lo << ":" << record.target_type.hi;
            if (record.map_index != UINT32_MAX) std::cout << " map=" << record.map_index;
            std::cout << "\n";
            incompatible = incompatible || record.kind == amc::CompatibilityKind::incompatible;
        }
        if (command == "diff" && argc == 6 && !amc::write_abix(report, argv[5], error)) {
            std::cerr << error << "\n";
            return 1;
        }
        return command == "compatibility" && incompatible ? 1 : 0;
    }

    if (command == "generate" || command == "backend") {
        if (argc != 7 || std::string(argv[3]) != "-l" || std::string(argv[4]) != "cpp"
            || std::string(argv[5]) != "-o") {
            print_usage();
            return 2;
        }
        return command == "backend"
            ? dispatch_provider(provider, "backend", argv[2], argv[6])
            : generate(provider, argv[2], argv[6]);
    }

    if (command == "frontend") {
        if (argc != 8 || std::string(argv[2]) != "-l" || std::string(argv[3]) != "cpp"
            || std::string(argv[4]) != "-c" || std::string(argv[6]) != "-o") {
            print_usage();
            return 2;
        }
        return dispatch_provider(provider, "frontend", argv[5], argv[7]);
    }

    if (command == "build") {
        fs::path config_path;
        fs::path build_dir;
        for (int i = 2; i < argc; ++i) {
            const std::string argument = argv[i];
            if ((argument == "-c" || argument == "-B") && i + 1 < argc) {
                const fs::path value = argv[++i];
                if (argument == "-c") config_path = value;
                else build_dir = fs::absolute(value);
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
            if (!imports || imports->size() != 1) {
                std::cerr << "MVP requires exactly one [[import]]\n";
                return 1;
            }
            std::vector<ExportSpec> exports;
            std::string error;
            if (!parse_exports(config, exports, error)) {
                std::cerr << error << "\n";
                return 1;
            }
            const fs::path base = build_dir.empty() ? fs::absolute(config_path).parent_path() : build_dir;
            const fs::path temporary = base / ".amc" / "frontend.abix";
            std::error_code ec;
            fs::create_directories(temporary.parent_path(), ec);
            if (ec) {
                std::cerr << "cannot create build directory: " << ec.message() << "\n";
                return 1;
            }
            if (dispatch_provider(provider, "frontend", config_path, temporary) != 0) return 1;

            amc::AbiModule full;
            if (!amc::read_abix(temporary.string(), full, error)) {
                std::cerr << error << "\n";
                return 1;
            }
            full.package_name = config["package"]["name"].value_or("cpp");
            full.package_version = config["package"]["version"].value_or("");
            for (const auto &spec : exports) {
                const fs::path output = spec.output.is_absolute() ? spec.output : base / spec.output;
                fs::create_directories(output.parent_path(), ec);
                if (ec) {
                    std::cerr << "cannot create export directory: " << ec.message() << "\n";
                    return 1;
                }
                amc::AbiModule projected;
                if (!amc::project_symbols(full, spec.symbols, projected, error)
                    || !amc::write_abix(projected, output.string(), error)) {
                    std::cerr << error << "\n";
                    return 1;
                }
            }
            fs::remove(temporary, ec);
            return 0;
        } catch (const toml::parse_error &error) {
            std::cerr << error.description() << "\n";
            return 1;
        }
    }

    if (command == "inspect" || command == "validate") {
        if (argc != 3) {
            print_usage();
            return 2;
        }
        amc::AbiModule module;
        std::string error;
        if (!amc::read_abix(argv[2], module, error)) {
            std::cerr << error << "\n";
            return 1;
        }
        if (command == "inspect")
            std::cout << "package=" << module.package_name << " types=" << module.types.size()
                      << " fields=" << module.fields.size() << " functions=" << module.functions.size() << "\n";
        return 0;
    }

    print_usage();
    return 2;
}
