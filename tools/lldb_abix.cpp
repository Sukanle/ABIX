// ABIX LLDB C++ plugin (AI-P2).
//
// Registers a native `abix` command built on the shared `libabix-*` libraries,
// so the debugger and the AMC CLI answer from exactly one `.abix`/Metadata
// implementation. Loaded with:
//
//   (lldb) plugin load /path/to/libabix_lldb.so
//   (lldb) abix info
//   (lldb) abix type Foo
//   (lldb) abix function Foo::bar
//   (lldb) abix verify other.abix
//   (lldb) abix check Foo other.abix
//   (lldb) abix source Foo
//   (lldb) abix cast 0x12345678 Foo
//
// Unlike the Python shim (tools/lldb_abix.py) this plugin does not spawn the
// `amc` executable; it links libabix-metadata / libabix-abi / libabix-tools
// directly, so no ABIX_AMC executable is required. Queries still read the
// embedded `.abix.metadata` region from the current target binary, so no
// `.abix` sidecar is needed either.

#include <lldb/API/SBCommandInterpreter.h>
#include <lldb/API/SBCommandReturnObject.h>
#include <lldb/API/SBDebugger.h>
#include <lldb/API/SBError.h>
#include <lldb/API/SBFileSpec.h>
#include <lldb/API/SBFrame.h>
#include <lldb/API/SBProcess.h>
#include <lldb/API/SBTarget.h>
#include <lldb/API/SBThread.h>
#include <lldb/API/SBValue.h>

#include "amc_core.h"
#include "amc_elf.h"
#include "amc_metadata.h"
#include "amc_query.h"
#include "amc_symbol_store.h"
#include "amc_verify.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

// --- small helpers -------------------------------------------------------

std::string hash_hex(amc::Hash128 value) {
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "0x%016llx%016llx", static_cast<unsigned long long>(value.hi),
        static_cast<unsigned long long>(value.lo));
    return buffer;
}

bool read_file(const std::string &path, std::vector<uint8_t> &data) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    data.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

// The absolute path of the current target executable, or empty.
std::string executable_path(lldb::SBDebugger &debugger) {
    lldb::SBTarget target = debugger.GetSelectedTarget();
    if (!target.IsValid()) return {};
    lldb::SBFileSpec executable = target.GetExecutable();
    if (!executable.IsValid()) return {};
    char buffer[4'096] = {0};
    executable.GetPath(buffer, sizeof(buffer));
    return buffer;
}

// Splits the LLDB argv-style array into tokens, dropping a leading "abix"
// (LLDB passes the command name as the first element for some call paths).
std::vector<std::string> tokens(char **command) {
    std::vector<std::string> out;
    for (char **arg = command; arg != nullptr && *arg != nullptr; ++arg)
        out.emplace_back(*arg);
    if (!out.empty() && out.front() == "abix") out.erase(out.begin());
    return out;
}

bool load_target_module(const std::string &path, amc::AbiModule &module, lldb::SBCommandReturnObject &result) {
    std::string error;
    if (!amc::load_module_source(path, module, error)) {
        result.SetError(("abix: cannot read '" + path + "': " + error).c_str());
        return false;
    }
    return true;
}

const amc::Type *type_by_id(const amc::AbiModule &module, amc::Hash128 id) { return amc::find_type_by_id(module, id); }

std::string type_label(const amc::AbiModule &module, amc::Hash128 id) {
    const amc::Type *type = type_by_id(module, id);
    return type != nullptr ? type->name : hash_hex(id);
}

// Primitive ABI decoding: AMC normalizes int32_t -> int, uint64_t -> unsigned
// long, etc. Values are little-endian on every target AMC emits today.
struct PrimitiveEncoding {
    unsigned width;
    char kind;   // 'i' int, 'u' uint, 'f' float, 'p' pointer
};

const std::vector<std::pair<std::string, PrimitiveEncoding>> &primitive_table() {
    static const std::vector<std::pair<std::string, PrimitiveEncoding>> table = {
        {              "bool", {1, 'u'}},
        {              "char", {1, 'i'}},
        {       "signed char", {1, 'i'}},
        {     "unsigned char", {1, 'u'}},
        {             "short", {2, 'i'}},
        {    "unsigned short", {2, 'u'}},
        {               "int", {4, 'i'}},
        {      "unsigned int", {4, 'u'}},
        {          "unsigned", {4, 'u'}},
        {              "long", {8, 'i'}},
        {     "unsigned long", {8, 'u'}},
        {         "long long", {8, 'i'}},
        {"unsigned long long", {8, 'u'}},
        {             "float", {4, 'f'}},
        {            "double", {8, 'f'}},
    };
    return table;
}

bool field_encoding(const std::string &name, PrimitiveEncoding &encoding) {
    for (const auto &entry : primitive_table()) {
        if (entry.first == name) {
            encoding = entry.second;
            return true;
        }
    }
    if (!name.empty() && name.back() == '*') {
        encoding = {8, 'p'};
        return true;
    }
    return false;
}

std::string decode_field(char kind, const uint8_t *raw, unsigned width) {
    char buffer[64];
    if (kind == 'i') {
        long long value = 0;
        std::memcpy(&value, raw, width);
        std::snprintf(buffer, sizeof(buffer), "%lld", value);
    } else if (kind == 'u') {
        unsigned long long value = 0;
        std::memcpy(&value, raw, width);
        std::snprintf(buffer, sizeof(buffer), "%llu", value);
    } else if (kind == 'f') {
        double value = 0;
        if (width == 4) {
            float single = 0;
            std::memcpy(&single, raw, 4);
            value = single;
        } else {
            std::memcpy(&value, raw, 8);
        }
        std::snprintf(buffer, sizeof(buffer), "%g", value);
    } else {
        unsigned long long value = 0;
        std::memcpy(&value, raw, 8);
        std::snprintf(buffer, sizeof(buffer), "0x%llx", value);
    }
    return buffer;
}

// --- abix info -----------------------------------------------------------

void run_info(lldb::SBDebugger &debugger, lldb::SBCommandReturnObject &result) {
    const std::string path = executable_path(debugger);
    if (path.empty()) {
        result.SetError("abix info: no target binary loaded");
        return;
    }
    std::vector<uint8_t> image;
    if (!read_file(path, image)) {
        result.SetError(("abix info: cannot read '" + path + "'").c_str());
        return;
    }
    if (!amc::is_binary(image.data(), image.size())) {
        result.SetError("abix info: target is not an ELF or Mach-O binary");
        return;
    }
    std::vector<uint8_t> region;
    std::string error;
    if (!amc::find_binary_section(image.data(), image.size(), ".abix.metadata", region, error)) {
        result.SetError(("abix info: no embedded metadata region (" + error + ")").c_str());
        return;
    }
    amc::MetadataHeader header;
    if (!amc::read_metadata_header(region.data(), region.size(), header, error)) {
        result.SetError(("abix info: " + error).c_str());
        return;
    }
    result.Printf("abix info: %s\n", path.c_str());
    result.Printf("  version=%u.%u flags=0x%x\n", header.version_major, header.version_minor, header.flags);
    result.Printf("  build_id=%s\n", hash_hex(header.build_id).c_str());
    result.Printf("  metadata_id=%s\n", hash_hex(header.metadata_id).c_str());
    result.Printf("  types=%u fields=%u functions=%u parameters=%u symbols=%u\n", header.type_count, header.field_count,
        header.function_count, header.parameter_count, header.symbol_count);
    result.Printf("  region= .abix.metadata (%zu bytes)\n", region.size());
}

// --- abix type -----------------------------------------------------------

void run_type(lldb::SBDebugger &debugger, const std::string &needle, bool layout, lldb::SBCommandReturnObject &result) {
    const std::string path = executable_path(debugger);
    if (path.empty()) {
        result.SetError("abix type: no target binary loaded");
        return;
    }
    amc::AbiModule module;
    if (!load_target_module(path, module, result)) return;
    const auto indices = amc::query_type_indices(module, needle);
    if (indices.empty()) {
        result.SetError(("abix type: no type matches '" + needle + "'").c_str());
        return;
    }
    for (const auto index : indices) {
        const auto &type = module.types[index];
        result.Printf("[%zu] %s kind=%s size=%u align=%u\n", index, type.name.c_str(),
            amc::type_kind_name(type.kind).c_str(), type.size, type.align);
        result.Printf("      id=%s layout=%s flags=0x%x fields=%u\n", hash_hex(type.id).c_str(),
            hash_hex(type.layout_hash).c_str(), type.flags, type.field_count);
        if (const amc::SourceOrigin *source = amc::find_source_origin(module, type.id))
            result.Printf("      source=%s:%u:%u\n", source->file.c_str(), source->line, source->column);
        if (!layout) continue;
        for (uint32_t f = 0; f < type.field_count; ++f) {
            const auto &field = module.fields[type.field_begin + f];
            result.Printf(
                "      +%-3u %-12s %s\n", field.offset, field.name.c_str(), type_label(module, field.type_id).c_str());
        }
    }
}

// --- abix function -------------------------------------------------------

void run_function(lldb::SBDebugger &debugger, const std::string &needle, lldb::SBCommandReturnObject &result) {
    const std::string path = executable_path(debugger);
    if (path.empty()) {
        result.SetError("abix function: no target binary loaded");
        return;
    }
    amc::AbiModule module;
    if (!load_target_module(path, module, result)) return;
    const auto indices = amc::query_function_indices(module, needle);
    if (indices.empty()) {
        result.SetError(("abix function: no function matches '" + needle + "'").c_str());
        return;
    }
    for (const auto index : indices) {
        const auto &function = module.functions[index];
        result.Printf("[%zu] %s owner=%s\n", index, function.name.c_str(),
            function.owner_type == amc::Hash128{} ? "-" : type_label(module, function.owner_type).c_str());
        result.Printf("      signature=%s returns=%s cc=%u flags=0x%x params=%zu\n",
            hash_hex(function.signature).c_str(), type_label(module, function.return_type).c_str(),
            function.calling_convention, function.flags, function.parameters.size());
        for (size_t p = 0; p < function.parameters.size(); ++p) {
            const auto &parameter = function.parameters[p];
            result.Printf(
                "      param %zu %-12s %s\n", p, parameter.name.c_str(), type_label(module, parameter.type_id).c_str());
        }
    }
}

// --- abix verify / check -------------------------------------------------

void print_changes(const amc::VerifyResult &verification, lldb::SBCommandReturnObject &result) {
    result.Printf(
        "  compatible=%s changes=%zu\n", verification.consistent ? "true" : "false", verification.changes.size());
    for (const auto &change : verification.changes) {
        std::string line = "  - ";
        line += amc::change_kind_name(change.kind);
        if (!change.type.empty()) line += " type=" + change.type;
        if (!change.name.empty()) line += " name=" + change.name;
        if (!change.detail.empty()) line += " (" + change.detail + ")";
        result.Printf("%s\n", line.c_str());
    }
}

void run_verify(lldb::SBDebugger &debugger, const std::string &other_path, lldb::SBCommandReturnObject &result) {
    const std::string path = executable_path(debugger);
    if (path.empty()) {
        result.SetError("abix verify: no target binary loaded");
        return;
    }
    amc::AbiModule module;
    if (!load_target_module(path, module, result)) return;
    amc::AbiModule other;
    if (!load_target_module(other_path, other, result)) return;
    amc::VerifyResult verification;
    std::string error;
    if (!amc::verify_modules(module, other, verification, error)) {
        result.SetError(("abix verify: " + error).c_str());
        return;
    }
    result.Printf("abix verify: %s -> %s\n", path.c_str(), other_path.c_str());
    print_changes(verification, result);
    if (!verification.consistent) result.SetStatus(lldb::eReturnStatusFailed);
}

void run_check(lldb::SBDebugger &debugger, const std::string &name, const std::string &other_path,
    lldb::SBCommandReturnObject &result) {
    const std::string path = executable_path(debugger);
    if (path.empty()) {
        result.SetError("abix check: no target binary loaded");
        return;
    }
    amc::AbiModule module;
    if (!load_target_module(path, module, result)) return;
    amc::AbiModule other;
    if (!load_target_module(other_path, other, result)) return;
    amc::VerifyResult verification;
    std::string error;
    if (!amc::verify_modules(module, other, verification, error)) {
        result.SetError(("abix check: " + error).c_str());
        return;
    }
    amc::VerifyResult filtered;
    filtered.package = verification.package;
    for (const auto &change : verification.changes)
        if (change.type == name) filtered.changes.push_back(change);
    filtered.consistent = filtered.changes.empty();
    result.Printf("abix check: %s\n", name.c_str());
    print_changes(filtered, result);
    if (!filtered.consistent) result.SetStatus(lldb::eReturnStatusFailed);
}

// --- abix source ---------------------------------------------------------

void run_source(lldb::SBDebugger &debugger, const std::string &name, lldb::SBCommandReturnObject &result) {
    const std::string path = executable_path(debugger);
    if (path.empty()) {
        result.SetError("abix source: no target binary loaded");
        return;
    }
    amc::AbiModule module;
    std::string error;
    if (!amc::load_module_source(path, module, error)) {
        result.SetError(("abix source: " + error).c_str());
        return;
    }
    const auto indices = amc::query_type_indices(module, name);
    if (!indices.empty()) {
        const auto &type = module.types[indices.front()];
        if (const amc::SourceOrigin *source = amc::find_source_origin(module, type.id)) {
            result.Printf("%s:%u:%u\n", source->file.c_str(), source->line, source->column);
            return;
        }
    }

    // The embedded region is intentionally source-free (release image). Fall
    // back to the plan's chain: BuildID -> symbol server -> debug `.abix`,
    // which carries the optional Source Origin section.
    std::vector<uint8_t> image;
    if (read_file(path, image) && amc::is_binary(image.data(), image.size())) {
        std::vector<uint8_t> build_id;
        if (amc::read_build_id(image.data(), image.size(), build_id, error)) {
            const std::string key = amc::hex_encode(build_id.data(), build_id.size());
            std::vector<uint8_t> artifact;
            std::string stored_path;
            if (amc::fetch_symbol(amc::default_symbol_store_root(), key, artifact, stored_path, error)
                && stored_path.size() > 5
                && stored_path.compare(stored_path.size() - 5, 5, ".abix") == 0) {
                amc::AbiModule fetched;
                if (amc::load_module_source(stored_path, fetched, error)) {
                    const auto fetched_indices = amc::query_type_indices(fetched, name);
                    if (!fetched_indices.empty()) {
                        const auto &type = fetched.types[fetched_indices.front()];
                        if (const amc::SourceOrigin *source = amc::find_source_origin(fetched, type.id)) {
                            result.Printf("%s:%u:%u\n", source->file.c_str(), source->line, source->column);
                            return;
                        }
                    }
                }
            }
        }
    }
    result.SetError(("abix source: no source origin for '"
                     + name
                     + "' (embedded region is source-free; publish the debug .abix to the symbol server)")
            .c_str());
}

// --- abix cast -----------------------------------------------------------

void run_cast(lldb::SBDebugger &debugger, const std::string &expression, const std::string &type_name,
    lldb::SBCommandReturnObject &result) {
    const std::string path = executable_path(debugger);
    if (path.empty()) {
        result.SetError("abix cast: no target binary loaded");
        return;
    }
    lldb::SBTarget target = debugger.GetSelectedTarget();
    lldb::SBProcess process = target.GetProcess();
    if (!process.IsValid()) {
        result.SetError("abix cast: no running process (run the target first)");
        return;
    }
    lldb::SBThread thread = process.GetSelectedThread();
    lldb::SBFrame frame = thread.IsValid() ? thread.GetSelectedFrame() : lldb::SBFrame();
    if (!frame.IsValid()) {
        result.SetError("abix cast: no selected frame");
        return;
    }
    lldb::SBValue value = frame.EvaluateExpression(expression.c_str());
    if (!value.IsValid() || !value.GetError().Success()) {
        result.SetError(("abix cast: cannot evaluate '" + expression + "'").c_str());
        return;
    }
    const uint64_t address = value.GetValueAsUnsigned(0);

    amc::AbiModule module;
    if (!load_target_module(path, module, result)) return;
    const auto indices = amc::query_type_indices(module, type_name);
    if (indices.empty()) {
        result.SetError(("abix cast: no type matches '" + type_name + "'").c_str());
        return;
    }
    const auto &type = module.types[indices.front()];
    lldb::SBError error;
    std::vector<uint8_t> data(type.size);
    const size_t read = process.ReadMemory(address, data.data(), data.size(), error);
    if (read != data.size() || !error.Success()) {
        char message[128];
        std::snprintf(message, sizeof(message), "abix cast: cannot read %u bytes at 0x%llx", type.size,
            static_cast<unsigned long long>(address));
        result.SetError(message);
        return;
    }
    result.Printf(
        "abix cast: %s @ 0x%llx (%u bytes)\n", type.name.c_str(), static_cast<unsigned long long>(address), type.size);
    if (type.field_count == 0) {
        result.Printf("  raw %s\n", amc::hex_encode(data.data(), data.size()).c_str());
        return;
    }
    for (uint32_t f = 0; f < type.field_count; ++f) {
        const auto &field = module.fields[type.field_begin + f];
        const std::string field_type = type_label(module, field.type_id);
        PrimitiveEncoding encoding{};
        if (!field_encoding(field_type, encoding) || field.offset + encoding.width > data.size()) {
            result.Printf("  +%-3u %-12s %-12s <opaque>\n", field.offset, field.name.c_str(), field_type.c_str());
            continue;
        }
        result.Printf("  +%-3u %-12s %-12s %s\n", field.offset, field.name.c_str(), field_type.c_str(),
            decode_field(encoding.kind, data.data() + field.offset, encoding.width).c_str());
    }
}

// --- command dispatch ----------------------------------------------------

class AbixCommand : public lldb::SBCommandPluginInterface {
public:
    bool DoExecute(lldb::SBDebugger debugger, char **command, lldb::SBCommandReturnObject &result) override {
        const std::vector<std::string> args = tokens(command);
        const std::string sub = args.empty() ? "info" : args.front();

        if (sub == "info") {
            run_info(debugger, result);
            return true;
        }
        if (sub == "type" && args.size() >= 2) {
            bool layout = false;
            for (size_t i = 2; i < args.size(); ++i)
                if (args[i] == "--layout") layout = true;
            run_type(debugger, args[1], layout, result);
            return true;
        }
        if (sub == "function" && args.size() >= 2) {
            run_function(debugger, args[1], result);
            return true;
        }
        if (sub == "verify" && args.size() >= 2) {
            run_verify(debugger, args[1], result);
            return true;
        }
        if (sub == "check" && args.size() >= 3) {
            run_check(debugger, args[1], args[2], result);
            return true;
        }
        if (sub == "source" && args.size() >= 2) {
            run_source(debugger, args[1], result);
            return true;
        }
        if (sub == "cast" && args.size() >= 3) {
            run_cast(debugger, args[1], args[2], result);
            return true;
        }
        result.SetError(
            "usage: abix info | type <name> [--layout] | function <name> | "
            "verify <other> | check <name> <other> | source <name> | "
            "cast <address-expression> <type>");
        return false;
    }
};

}   // namespace

// LLDB's native plugin entry point. The symbol must be exactly
// `lldb::PluginInitialize(lldb::SBDebugger)`; `plugin load` resolves it.
namespace lldb {
bool PluginInitialize(lldb::SBDebugger debugger) {
    static AbixCommand command;
    debugger.GetCommandInterpreter().AddCommand("abix", &command,
        "ABIX ABI queries against the embedded ABIX Metadata Region",
        "abix <info|type|function|verify|check|source|cast> [args]");
    return true;
}
}   // namespace lldb
