#include "AMC/CLI/CLI.h"

#include <string>
#include <vector>

namespace amc::cli {

int cmd_build(char **args, int arg_count, const fs::path &provider) {
    const std::string command = args[1];
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
}

}   // namespace amc::cli
