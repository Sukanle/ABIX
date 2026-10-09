#include "AMC/CLI/CLI.h"

#include <string>
#include <vector>

#include <fmt/format.h>

namespace amc::cli {

int cmd_diff(char **args, int arg_count) {
    const std::string command = args[1];
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
            fmt::print("{} {}:{} -> {}:{}", amc::compatibility_name(record.kind), record.source_type.lo,
                record.source_type.hi, record.target_type.lo, record.target_type.hi);
            if (record.map_index != UINT32_MAX) fmt::print(" map={}", record.map_index);
            fmt::print("\n");
            incompatible = incompatible || record.kind == amc::CompatibilityKind::incompatible;
        }
        if (command == "diff" && arg_count == 6 && !amc::write_abix(report, args[5], error))
            return fail(amc::ErrorCategory::io, "write_abix", error, args[5]);
        return command == "compatibility" && incompatible ? 1 : 0;
    }
}

}   // namespace amc::cli
