#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Language-provider infrastructure shared by every `amc-<language>` executable.
//
// A provider is the language-agnostic description of an AMC language tool: a
// name (e.g. "cpp") plus the capabilities it implements (e.g. "frontend",
// "backend"). The one-shot command line, the `.abic.toml` import config and the
// JSONL IPC handshake are identical for all languages, so they live here; the
// individual capability implementations stay in `AMC/src/Lang/<language>/`.
namespace amc::lang {

// Configuration parsed from the single `[[import]]` section of a `.abic.toml`.
struct OwnedDecl {
    std::string type;        // value type name
    std::string create;      // factory function name (optional)
    std::string destroy;     // release function name (required for `owned`)
    std::string semantics;   // "owned" (default) | "borrow" | "copy"
};

// Per-interaction ownership contract for one function: the mode of its return
// value and of each positional parameter, plus the release entry point for an
// `owned` return.
struct ContractDecl {
    std::string function;
    std::string returns;               // mode string, empty = unspecified
    std::string release;               // release function name (owned returns)
    std::vector<std::string> params;   // positional modes
};

// Error boundary for one function: the status value that means success and,
// optionally, which parameter carries the result value.
struct ErrorDecl {
    std::string function;
    std::string status;   // "return" (default) | "parameter"
    int64_t success = 0;
    int64_t value = -1;   // local parameter index, or -1 when the return value is the value
};

// A request to instantiate a language generic into a concrete type. The
// frontend owns instantiation and constraint checking; AMC only coordinates and
// records the concrete ABI facts. `as` is the exported name of the concrete
// entity (also emitted as a source alias in the instantiation anchor).
struct InstantiateDecl {
    std::string template_name;       // e.g. "abix_span"
    std::vector<std::string> args;   // canonical type arguments, e.g. {"int32_t"}
    std::string as;                  // exported concrete name, e.g. "abix_span_i32"
};

// A request to expose a concrete type as a set of callable capabilities. The
// frontend generates the C thunks (`abix_<Type>_<method>`) and records them as
// ABI entities, so neither the producer nor the consumer writes an adapter.
// Only public members are supported; the type is projected to consumers as an
// opaque handle (`opaque`).
struct ExposeDecl {
    std::string type;                       // concrete type name, e.g. "Counter"
    std::vector<std::string> methods;       // member function names to expose
    std::vector<std::string> constructor;   // constructor parameter spellings (empty = default ctor)
    bool has_constructor = false;           // a `constructor` key was present
    bool destructor = true;                 // expose a destroy entry
    bool opaque = true;                     // consumers see an opaque handle
};

// A declared native↔ABIX-STD parameter adaptation. An STL type must not cross
// the boundary raw (ABI-SPEC §18 requires an explicit mapping), so a native
// parameter type is declared against an ABIX-STD boundary type; the language
// frontend knows how to marshal that native kind onto the boundary, and
// `[[expose]]` then adapts any method that takes it instead of refusing it.
// Only parameters are adapted: a return value mapped to a borrowed view would
// dangle, so returns still require their own contract.
struct AdaptDecl {
    std::string native;    // canonical native kind: std::string | std::string_view | std::vector | std::span
    std::string target;    // ABIX-STD boundary type, e.g. abix_str_utf8 / abix_span_i32
    std::string element;   // element spelling, required for std::vector / std::span
};

struct Config {
    std::string language;
    std::string db;              // compile_commands.json (optional)
    std::string output;          // [[export]] output, if any
    std::string thunks_output;   // [thunks] output: generated C thunk source
    std::vector<std::string> flags;
    std::vector<std::string> files;
    std::vector<std::string> symbols;
    std::vector<OwnedDecl> owned;                  // [[owned]] release-policy declarations
    std::vector<ContractDecl> contracts;           // [[contract]] ownership contracts
    std::vector<ErrorDecl> errors;                 // [[error]] error-boundary contracts
    std::vector<InstantiateDecl> instantiations;   // [[instantiate]] generic instantiations
    std::vector<AdaptDecl> adaptations;            // [[adapt]] native↔ABIX-STD parameter mappings
    std::vector<ExposeDecl> exposed;               // [[expose]] automatic capability generation
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
