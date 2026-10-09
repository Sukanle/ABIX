#ifndef AMC_UTIL_HEX_H
#define AMC_UTIL_HEX_H

#include <cstdint>
#include <string>

#include <fmt/format.h>

#include "AMC/Core/Core.h"

namespace amc::util {

// Canonical lowercase hex rendering of a 128-bit identity (hi then lo), with
// the project-wide "0X" prefix used for BuildID / TypeID display.
inline std::string hash_hex(amc::Hash128 value) { return fmt::format("0X{:016x}{:016x}", value.hi, value.lo); }

// A fixed-width lowercase hex rendering of one 64-bit value.
inline std::string u64_hex(std::uint64_t value) { return fmt::format("{:016x}", value); }

}   // namespace amc::util

#endif   // AMC_UTIL_HEX_H
