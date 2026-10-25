#ifndef AMC_UTIL_JSON_H
#define AMC_UTIL_JSON_H

#include <string>

namespace amc::util {

// Minimal JSON text helpers for hand-written emitters (LLVM-style output in
// `amc-dump`). The structured `amc::Json` type remains the default elsewhere.
std::string escape_json(const std::string &value);
void json_string(std::string &output, const std::string &value);
void indent(std::string &output, unsigned depth);

}   // namespace amc::util

#endif   // AMC_UTIL_JSON_H
