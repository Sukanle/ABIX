#ifndef AMC_CLI_H
#define AMC_CLI_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <toml++/toml.h>

#include "SKL/AMC/Core/Adapter.h"
#include "SKL/AMC/Core/Cache.h"
#include "SKL/AMC/Core/Context.h"
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Core/ELF.h"
#include "SKL/AMC/Core/Error.h"
#include "SKL/AMC/Core/Lua.h"
#include "SKL/AMC/Core/Metadata.h"
#include "SKL/AMC/Core/Query.h"
#include "SKL/AMC/Core/Symbol.h"
#include "SKL/AMC/Core/Verify.h"
#include "SKL/AMC/Util/Error.h"

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

// One entry of `AMC/providers.toml`. `roles` contains `frontend` and/or
// `backend`; `protocol` is `jsonl-v1` for provider processes and `direct` for
// built-in backends such as the Aue/Lua contract writer.
struct ProviderManifestEntry {
    std::string language;
    std::string executable;
    std::vector<std::string> roles;
    std::string protocol;
    std::string contract;
    std::vector<std::string> features;
};

struct ProviderManifest {
    uint32_t version = 1;
    std::vector<ProviderManifestEntry> providers;
};

// Load the provider manifest next to the `amc` executable, falling back to
// `AMC_DEFAULT_PROVIDERS_FILE` for source-tree runs. `AMC_PROVIDERS_FILE`
// overrides both. The manifest is the source of truth for
// `--list-languages` and `--describe-language`.
bool load_provider_manifest(const fs::path &executable_dir, ProviderManifest &manifest, std::string &error);
const ProviderManifestEntry *find_provider_manifest(const ProviderManifest &manifest, const std::string &language);
std::string describe_provider(const ProviderManifestEntry &provider);

bool parse_exports(const toml::table &table, std::vector<ExportSpec> &exports, std::string &error);
// Apply the `[target]` table to the module and recompute every LayoutHash with
// the Canonical Layout Encoding v1. Call once the target facts are final.
void apply_target_and_layout(const toml::table &config, amc::AbiModule &module);
bool dispatch_provider(const fs::path &provider, const char *capability, const fs::path &input, const fs::path &output,
    std::string &error);
// Resolve the `amc-<language>` provider executable that sits next to `amc`.
// `base_provider` is the path to the reference `amc-cpp` provider computed by
// the driver; the C++ provider is returned unchanged for the default language.
fs::path provider_executable(const fs::path &base_provider, const std::string &language);
// Dispatch a provider `backend` capability, defaulting the output to
// `amc_generated<extension>` when `destination` does not already carry it.
int generate(
    const fs::path &provider, const fs::path &input, const fs::path &destination, const std::string &extension);
void print_usage();

int cmd_context(char **args, int arg_count);
int cmd_metadata(char **args, int arg_count);
int cmd_adapter(char **args, int arg_count);
int cmd_query(char **args, int arg_count);
int cmd_symbols(char **args, int arg_count);
int cmd_verify(char **args, int arg_count, const fs::path &provider);
int cmd_target(char **args, int arg_count);
int cmd_diff(char **args, int arg_count);
int cmd_cache(char **args, int arg_count);
int cmd_generate(char **args, int arg_count, const fs::path &provider);
int cmd_frontend(char **args, int arg_count, const fs::path &provider);
int cmd_build(char **args, int arg_count, const fs::path &provider);
int cmd_inspect(char **args, int arg_count);

}   // namespace amc::cli

#endif   // AMC_CLI_H
