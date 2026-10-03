#include "AMC/CLI/CLI.h"

#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <fmt/os.h>

namespace amc::cli {

namespace {
void write_bytes(const fs::path &path, const std::vector<uint8_t> &bytes, const char *stage, int &rc) {
    try {
        auto out = fmt::output_file(path.string());
        out.print("{}", fmt::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
    } catch (const std::exception &e) {
        rc = fail(amc::ErrorCategory::io, stage, "cannot write output", path.string(), e.what());
    }
}
}   // namespace

int cmd_metadata(char **args, int arg_count) {
    const std::string command = args[1];
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
            // A raw region starts with the manifest; an ELF/Mach-O binary
            // embeds it in the `.abix.metadata` section.
            if (from_elf_flag || amc::is_binary(bytes.data(), bytes.size())) {
                if (!amc::find_binary_section(bytes.data(), bytes.size(), ".abix.metadata", region, error))
                    return fail(amc::ErrorCategory::format, "metadata", error, input.string());
            } else {
                region = std::move(bytes);
            }
            amc::MetadataHeader header;
            if (!amc::read_metadata_header(region, header, error) || !amc::verify_metadata_region(region, error))
                return fail(amc::ErrorCategory::validation, "verify_metadata", error, input.string());
            if (format == "json") {
                fmt::print("{}", amc::metadata_header_to_json(header));
            } else {
                fmt::print("metadata: valid metadata_id=0X{:x}{:x} types={} fields={} functions={} symbols={}\n",
                    header.metadata_id.hi, header.metadata_id.lo, header.type_count, header.field_count,
                    header.function_count, header.symbol_count);
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
            if (!amc::find_binary_section(bytes.data(), bytes.size(), ".abix.metadata", region, error))
                return fail(amc::ErrorCategory::format, "metadata", error, input.string());
            amc::MetadataHeader header;
            if (!amc::read_metadata_header(region, header, error))
                return fail(amc::ErrorCategory::format, "read_metadata_header", error, input.string());
            if (!amc::verify_metadata_region(region, error))
                return fail(amc::ErrorCategory::validation, "verify_metadata", error, input.string());
            if (format == "json") {
                fmt::print("{}", amc::metadata_header_to_json(header));
                return 0;
            }
            if (output.empty()) {
                fmt::print("{}", fmt::string_view(reinterpret_cast<const char *>(region.data()), region.size()));
                return 0;
            }
            int rc = 0;
            write_bytes(output, region, "metadata", rc);
            return rc;
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
                fmt::print("{}", fmt::string_view(reinterpret_cast<const char *>(region.data()), region.size()));
                return 0;
            }
            int rc = 0;
            write_bytes(output, region, "metadata", rc);
            return rc;
        }

        const std::string rendered =
            format == "json" ? amc::metadata_header_to_json(header) : amc::metadata_meta_document(module, header);
        if (output.empty()) {
            fmt::print("{}", rendered);
            return 0;
        }
        try {
            auto file = fmt::output_file(output.string());
            file.print("{}", rendered);
        } catch (const std::exception &e) {
            return fail(amc::ErrorCategory::io, "metadata", "cannot write output", output.string(), e.what());
        }
        return 0;
    }
}

}   // namespace amc::cli
