#ifndef AMC_UTIL_ERROR_H
#define AMC_UTIL_ERROR_H

#include <string>

#include "AMC/Core/Error.h"

namespace amc::util {

// Process-wide error envelope selector, set once from `--error-format`.
extern amc::ErrorFormat g_error_format;

// Prints `error` through the selected envelope and returns 1 (convenience for
// `return fail(...)` paths). Callers pass the file/detail that located the
// failure when available.
int fail(amc::ErrorCategory category, const std::string &stage, const std::string &message,
    const std::string &file = {}, const std::string &detail = {});

int usage_error(const std::string &message);

// Maps a free-form load error to the right category (I/O vs format).
amc::ErrorCategory read_error_category(const std::string &message);

}   // namespace amc::util

#endif   // AMC_UTIL_ERROR_H
