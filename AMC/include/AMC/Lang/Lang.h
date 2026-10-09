#pragma once

#include <string>
#include <vector>

// Language-provider infrastructure shared by every `amc-<language>` executable.
//
// A provider is the language-agnostic description of an AMC language tool: a
// name (e.g. "cpp") plus the capabilities it implements (e.g. "frontend",
// "backend"). The one-shot command line, the `.abic.toml` import config and the
// JSONL IPC handshake are identical for all languages, so they live here; the
// individual capability implementations stay in `amc/lang/<language>/`.
namespace amc::lang {

// Configuration parsed from the single `[[import]]` section of a `.abic.toml`.
struct Config {
    std::string language;
    std::string db;       // compile_commands.json (optional)
    std::string output;   // [[export]] output, if any
    std::vector<std::string> flags;
    std::vector<std::string> files;
    std::vector<std::string> symbols;
};

// A capability the provider exposes (e.g. "frontend", "backend").
struct Capability {
    std::string name;
    int (*run)(const char *input, const char *output);
};

// Language-agnostic description of an `amc-<language>` executable.
struct Provider {
    std::string language;
    std::vector<Capability> capabilities;
};

// Parse the `[[import]]` section of a `.abic.toml`. `expected_language` must
// match the declared language (empty accepts any). Returns false and fills `e`
// on error.
bool config_load(const std::string &path, const std::string &expected_language, Config &c, std::string &e);

// Extract a `"key":"value"` string field from one JSONL request line.
std::string json_value(const std::string &line, const char *key);

// Run an `amc-<language>` provider executable: either one-shot
// `<capability> <input> <output>` or the `--ipc` JSONL loop.
int run_provider(int argc, char **argv, const Provider &provider);

// JSONL IPC loop shared by every language provider.
int ipc(const Provider &provider);

}   // namespace amc::lang
