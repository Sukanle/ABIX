#include "AMC/Util/Error.h"

#include <fmt/format.h>

namespace amc::util {

amc::ErrorFormat g_error_format = amc::ErrorFormat::text;

int fail(amc::ErrorCategory category, const std::string &stage, const std::string &message,
    const std::string &file, const std::string &detail) {
    const auto error = amc::make_error(category, stage, message, file, detail);
    fmt::print(stderr, "{}\n",
        g_error_format == amc::ErrorFormat::json ? amc::error_to_json(error) : amc::error_to_text(error));
    return 1;
}

int usage_error(const std::string &message) {
    fail(amc::ErrorCategory::usage, "options", message);
    return 2;
}

amc::ErrorCategory read_error_category(const std::string &message) {
    if (message.rfind("cannot open", 0) == 0) return amc::ErrorCategory::io;
    return amc::ErrorCategory::format;
}

}   // namespace amc::util
