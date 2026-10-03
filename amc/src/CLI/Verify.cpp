#include "AMC/CLI/CLI.h"

#include <string>
#include <vector>

#include <fmt/format.h>

namespace amc::cli {

int cmd_verify(char **args, int arg_count, const fs::path &provider) {
    const std::string command = args[1];
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
                fmt::print("{}", abi_diagnostics(contract, result));
            else
                fmt::print("{}", json_output ? amc::verify_report_to_json({result}, result.consistent)
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
                const fs::path fresh = base / ".amc" / fmt::format("verify-{}.abix", index);
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
                fmt::print("{}", diagnostics_buffer);
            else
                fmt::print("{}", json_output ? amc::verify_report_to_json(results, consistent)
                                             : amc::verify_report_to_text(results));
            return consistent ? 0 : 1;
        } catch (const toml::parse_error &error) {
            return fail(amc::ErrorCategory::config, "verify", std::string(error.description()), config_path.string());
        }
    }
}

}   // namespace amc::cli
