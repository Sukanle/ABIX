#pragma once

#include "AMC/Lang/Lang.h"
#include "AMC/Core/Core.h"
#include "AMC/Core/Metadata.h"
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/RecordLayout.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Options/OptionUtils.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/APFloat.h>
#include <llvm/Support/Path.h>
#include <toml++/toml.h>
#include <iostream>

#include <stdio.h>
#include <string.h>
#include <memory>
#include <set>
#include <unordered_map>
#include <filesystem>
#include <string_view>
#include <unistd.h>

#include <fmt/color.h>
#include <fmt/os.h>

namespace amc::cpp {

// Clang resource directory (e.g. /opt/homebrew/opt/llvm/lib/clang/22),
// detected at build time via `clang -print-resource-dir` and injected
// as a compile definition.  Used to locate built-in headers (stdarg.h,
// stddef.h, etc.) and the Brew LLVM's libc++ installation.
extern const char * const kClangResourceDir;

int run_frontend(const char *config_path, const char *output_path);

int backend(const char *input, const char *output);

void append_macos_sysroot(std::vector<std::string> &flags);

}   // namespace amc::cpp
