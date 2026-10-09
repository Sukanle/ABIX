#ifndef AMC_CLI_H
#define AMC_CLI_H

#include <filesystem>
#include <string>
#include <vector>

#include <toml++/toml.h>

#include "AMC/Core/Adapter.h"
#include "AMC/Core/Context.h"
#include "AMC/Core/Core.h"
#include "AMC/Core/ELF.h"
#include "AMC/Core/Error.h"
#include "AMC/Core/Lua.h"
#include "AMC/Core/Metadata.h"
#include "AMC/Core/Query.h"
#include "AMC/Core/Symbol.h"
#include "AMC/Core/Verify.h"
#include "AMC/Util/Error.h"

namespace fs = std::filesystem;

namespace amc::cli {

// The unified error envelope is shared with the other AMC executables.
using amc::util::fail;
using amc::util::g_error_format;
using amc::util::read_error_category;
using amc::util::usage_error;

std::string json_quote(const std::string &value);
bool parse_hash128(const std::string &text, amc::Hash128 &value);
std::string hash_key(amc::Hash128 value);
std::string elf_meta_document(const std::string &key, const amc::MetadataHeader &header);
std::string abi_diagnostics(const amc::AbiModule &reference, const amc::VerifyResult &result);
bool write_abix_meta(const amc::AbiModule &module, const fs::path &artifact, std::string &error);

struct ExportSpec {
    fs::path output;
    std::vector<std::string> symbols;
};

bool parse_exports(const toml::table &table, std::vector<ExportSpec> &exports, std::string &error);
bool dispatch_provider(const fs::path &provider, const char *capability, const fs::path &input, const fs::path &output,
    std::string &error);
// Resolve the `amc-<language>` provider executable that sits next to `amc`.
// `base_provider` is the path to the reference `amc-cpp` provider computed by
// the driver; the C++ provider is returned unchanged for the default language.
fs::path provider_executable(const fs::path &base_provider, const std::string &language);
// Dispatch a provider `backend` capability, defaulting the output to
// `amc_generated<extension>` when `destination` does not already carry it.
int generate(const fs::path &provider, const fs::path &input, const fs::path &destination, const std::string &extension);
void print_usage();

int cmd_context(char **args, int arg_count);
int cmd_metadata(char **args, int arg_count);
int cmd_adapter(char **args, int arg_count);
int cmd_query(char **args, int arg_count);
int cmd_symbols(char **args, int arg_count);
int cmd_verify(char **args, int arg_count, const fs::path &provider);
int cmd_diff(char **args, int arg_count);
int cmd_generate(char **args, int arg_count, const fs::path &provider);
int cmd_frontend(char **args, int arg_count, const fs::path &provider);
int cmd_build(char **args, int arg_count, const fs::path &provider);
int cmd_inspect(char **args, int arg_count);

}   // namespace amc::cli

#endif   // AMC_CLI_H
