#include "AMC/CLI/CLI.h"

#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <fmt/os.h>

namespace amc::cli {

int cmd_symbols(char **args, int arg_count) {
    const std::string command = args[1];
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
            if (amc::is_binary(bytes.data(), bytes.size())) {
                std::vector<uint8_t> build_id;
                if (!amc::read_build_id(bytes.data(), bytes.size(), build_id, error))
                    return fail(amc::ErrorCategory::format, "publish", error, input.string());
                key = amc::hex_encode(build_id.data(), build_id.size());
                if (!amc::find_binary_section(bytes.data(), bytes.size(), ".abix.metadata", artifact, error))
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
            fmt::print("published key={} path={}\n", key, stored_path);
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
            if (!amc::is_binary(bytes.data(), bytes.size()))
                return fail(amc::ErrorCategory::format, "fetch", "input is not an ELF or Mach-O binary", input.string(),
                    "pass --build-id to look up by id directly");
            std::vector<uint8_t> build_id;
            std::string error;
            if (!amc::read_build_id(bytes.data(), bytes.size(), build_id, error))
                return fail(amc::ErrorCategory::format, "fetch", error, input.string());
            key = amc::hex_encode(build_id.data(), build_id.size());
        }
        std::vector<uint8_t> artifact;
        std::string path, error;
        if (!amc::fetch_symbol(root.string(), key, artifact, path, error))
            return fail(amc::ErrorCategory::io, "fetch", error);
        if (output.empty()) {
            fmt::print("{}\n", path);
            return 0;
        }
        try {
            auto out = fmt::output_file(output.string());
            out.print("{}", fmt::string_view(reinterpret_cast<const char *>(artifact.data()), artifact.size()));
        } catch (const std::exception &e) {
            return fail(amc::ErrorCategory::io, "fetch", "cannot write output", output.string(), e.what());
        }
        fmt::print("fetched {} -> {}\n", path, output.string());
        return 0;
    }
}

}   // namespace amc::cli
