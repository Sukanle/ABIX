#ifndef AMC_UTIL_NAMES_H
#define AMC_UTIL_NAMES_H

#include <cstdint>
#include <string>

#include "AMC/Core/Core.h"

namespace amc::util {

// Human-readable renderings of the numeric target/function enums, shared by
// `amc-dump` and `amc-mcp` so both agree on the spelling.
std::string arch_name(std::uint32_t value);
std::string os_name(std::uint32_t value);
std::string compiler_name(std::uint32_t value);
std::string target_calling_convention_name(std::uint32_t value);
std::string function_calling_convention_name(std::uint32_t value);

// Stable names for the packed primitive ABI descriptor and map operations.
const char *primitive_kind_name(std::uint32_t kind);
const char *float_format_name(std::uint32_t format);
const char *map_opcode_name(amc::MapOpcode opcode);

}   // namespace amc::util

#endif   // AMC_UTIL_NAMES_H
