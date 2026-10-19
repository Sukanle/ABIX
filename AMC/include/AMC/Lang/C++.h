#pragma once

#include "AMC/Lang/Lang.h"   // IWYU pragma: keep
#include <vector>
#include <string>

namespace amc::lang::cpp {

// Clang resource directory (e.g. /opt/homebrew/opt/llvm/lib/clang/22),
// detected at build time via `clang -print-resource-dir` and injected
// as a compile definition.  Used to locate built-in headers (stdarg.h,
// stddef.h, etc.) and the Brew LLVM's libc++ installation.
extern const char * const kClangResourceDir;

int run_frontend(const char *config_path, const char *output_path);

int backend(const char *input, const char *output);

void append_macos_sysroot(std::vector<std::string> &flags);

// Non-Apple targets: point the clang we are linked against at the resource
// directory detected at build time, so the built-in headers (stddef.h,
// stdarg.h, ...) are found without a .abic.toml naming a toolchain version.
void append_clang_resource_dir(std::vector<std::string> &flags);

}   // namespace amc::lang::cpp
