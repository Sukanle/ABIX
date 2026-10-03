#include "AMC/Lang/C++.h"

#ifndef ABIX_CLANG_RESOURCE_DIR
#  define ABIX_CLANG_RESOURCE_DIR ""
#endif

namespace amc::cpp {

const char * const kClangResourceDir = ABIX_CLANG_RESOURCE_DIR;

static std::string cpp_name(std::string s) {
    for (size_t p = 0; (p = s.find("::", p)) != std::string::npos; s.replace(p, 2, "_")) {}
    for (char &c : s)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') c = '_';
    return s;
}

static bool is_cpp_identifier(const std::string &s) {
    if (s.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (const char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

// A name that can be pasted into a generated `sizeof`/`offsetof` check: a C++
// identifier, optionally qualified with `::`. This deliberately rejects the
// mangled names AMC gives to implicit/unnamed types (`struct Foo *`, anonymous
// enums) so the ABI check header never emits uncompilable code.
static bool is_cpp_qualified_name(const std::string &s) {
    if (s.empty() || s == "void") return false;
    size_t i = 0;
    while (i < s.size()) {
        if (!(std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_')) return false;
        while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_'))
            ++i;
        if (i == s.size()) return true;
        if (s.compare(i, 2, "::") != 0) return false;
        i += 2;
        if (i == s.size()) return false;   // trailing `::`
    }
    return true;
}

static std::string cpp_string(const std::string &value) {
    std::string result = "\"";
    for (const char c : value) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n')
            result += "\\n";
        else if (c == '\r')
            result += "\\r";
        else if (c == '\t')
            result += "\\t";
        else
            result += c;
    }
    result += '"';
    return result;
}

static std::string hex_u64(uint64_t value) { return fmt::format("0x{:x}ULL", value); }

int backend(const char *input, const char *output) {
    amc::AbiModule m;
    std::string e;
    if (!amc::read_abix(input, m, e)) {
        fmt::print(stderr, "{}\n", e);
        return 1;
    }
    auto o = fmt::output_file(output);
    o.print(
        "#pragma once\n"
        "#include <ABIX/Runtime/Registry.h>\n"
        "#include <stddef.h>\n"
        "#include <stdint.h>\n"
        "#include <string.h>\n"
        "namespace amc_generated {{\n"
        "using TypeId = ::skl::abix::model::TypeId;\n"
        "using LayoutInfo = ::skl::abix::metadata::LayoutInfo;\n"
        "using FieldInfo = ::skl::abix::metadata::FieldDescriptor;\n"
        "using ParameterInfo = ::skl::abix::metadata::ParameterDescriptor;\n"
        "using FunctionInfo = ::skl::abix::metadata::FunctionDescriptor;\n"
        "using SymbolInfo = ::skl::abix::metadata::SymbolDescriptor;\n"
        "using ModuleInfo = ::skl::abix::metadata::ModuleDescriptor;\n"
        "enum class MapOpcode : uint8_t {{ \n"
        "    copy_field,\n"
        "    convert_int,\n"
        "    convert_float,\n"
        "    add_default,\n"
        "    skip_field,\n"
        "}};\n"
        "struct MapOperation {{\n"
        "    MapOpcode opcode;\n"
        "    uint32_t source_field;\n"
        "    uint32_t target_field;\n"
        "    uint32_t source_offset;\n"
        "    uint32_t target_offset;\n"
        "    uint32_t byte_count;\n"
        "    TypeId auxiliary;\n"
        "}};\n"
        "template <typename Source, typename Target> struct MapPrivate;\n"
        "template <typename T> struct TypeTraits;\n");

    for (const auto &type : m.types) {
        const std::string id = fmt::format("{}_ABIX", cpp_name(type.name));
        o.print("struct {} {{\n", id);
        o.print("    static constexpr TypeId type_id {{{}, {}}};\n", hex_u64(type.id.lo), hex_u64(type.id.hi));
        o.print("    static constexpr uint64_t type_id_lo = {};\n", hex_u64(type.id.lo));
        o.print("    static constexpr uint64_t type_id_hi = {};\n", hex_u64(type.id.hi));
        o.print("    static constexpr LayoutInfo layout {{{}, {}, {}}};\n", type.size, type.align, type.field_count);
        o.print("    static constexpr size_t size = {};\n", type.size);
        o.print("    static constexpr size_t align = {};\n", type.align);
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto &field = m.fields[type.field_begin + i];
            o.print("    static constexpr size_t {}_offset = {};\n", cpp_name(field.name), field.offset);
        }
        o.print("}};\n");
        o.print("template <> struct TypeTraits<{}> {{\n", id);
        o.print("    static constexpr TypeId type_id {{{}, {}}};\n", hex_u64(type.id.lo), hex_u64(type.id.hi));
        o.print("    static constexpr LayoutInfo layout {{{}, {}, {}}};\n", type.size, type.align, type.field_count);
        o.print("    static constexpr size_t size = {};\n", type.size);
        o.print("    static constexpr size_t align = {};\n", type.align);
        o.print("}};\n");
        o.print("inline constexpr FieldInfo {}_fields[{}] = {{\n", id, type.field_count == 0 ? 1 : type.field_count);
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto &field = m.fields[type.field_begin + i];
            o.print("    {{{{{}, {}}}, {}, {}}},\n", hex_u64(field.type_id.lo), hex_u64(field.type_id.hi), field.offset,
                field.flags);
        }
        o.print("}};\n");
    }

    for (size_t i = 0; i < m.functions.size(); ++i) {
        const auto &function = m.functions[i];
        const auto param_count = function.parameters.empty() ? 1 : function.parameters.size();
        o.print("inline constexpr ParameterInfo amc_function_{}_parameters[{}] = {{\n", i, param_count);
        for (const auto &parameter : function.parameters)
            o.print("    {{{{{}, {}}}, {}}},\n", hex_u64(parameter.type_id.lo), hex_u64(parameter.type_id.hi),
                parameter.flags);
        o.print("}};\n");
        o.print(
            "inline constexpr FunctionInfo amc_function_{} {{amc_function_{}_parameters, {{{}, {}}},"
            " {{{}, {}}}, {}, {}, {}}};\n",
            i, i, hex_u64(function.signature.lo), hex_u64(function.signature.hi), hex_u64(function.return_type.lo),
            hex_u64(function.return_type.hi), function.parameters.size(), function.calling_convention, function.flags);
    }

    {
        const auto type_count = m.types.empty() ? 1 : m.types.size();
        o.print("inline constexpr ::skl::abix::metadata::TypeDescriptor amc_types[{}] = {{\n", type_count);
        for (const auto &type : m.types) {
            const std::string id = fmt::format("{}_ABIX", cpp_name(type.name));
            o.print("    {{{}_fields, {{{}, {}}}, {{{}, {}}}, {}, {}, {}, {}}},\n", id, hex_u64(type.id.lo),
                hex_u64(type.id.hi), hex_u64(type.layout_hash.lo), hex_u64(type.layout_hash.hi), type.flags, type.size,
                type.align, type.field_count);
        }
        o.print("}};\n");
    }

    // Canonical TypeDesc / TypeLayout arrays — the one ABI truth.
    // These are imported directly by RuntimeRegistry instead of being
    // reconstructed from the runtime projection (amc_types[]).
    {
        const auto type_count = m.types.empty() ? 1 : m.types.size();
        o.print("inline constexpr ::skl::abix::model::TypeDesc amc_canonical_types[{}] = {{\n", type_count);
        for (size_t i = 0; i < m.types.size(); ++i) {
            const auto &type = m.types[i];
            // name_offset = 0 (names live in .abix.names section for
            // diagnostic use only — stripped from the runtime registry).
            o.print("    {{{{{}, {}}}, {}, {}, {}}},\n", hex_u64(type.id.lo), hex_u64(type.id.hi), type.flags, 0U,
                static_cast<uint32_t>(i));
        }
        if (m.types.empty()) {
            o.print("    {{{{{}, {}}}, {}, {}, {}}},\n", "0ULL", "0ULL", 0U, 0U, 0U);
        }
        o.print("}};\n");

        o.print("inline constexpr ::skl::abix::model::TypeLayout amc_canonical_layouts[{}] = {{\n", type_count);
        for (const auto &type : m.types) {
            o.print("    {{{}, {}, {}, {}, {{{}, {}}}}},\n", type.size, type.align, type.field_begin, type.field_count,
                hex_u64(type.layout_hash.lo), hex_u64(type.layout_hash.hi));
        }
        if (m.types.empty()) {
            o.print("    {{{}, {}, {}, {}, {{{}, {}}}}},\n", 0U, 0U, 0U, 0U, "0ULL", "0ULL");
        }
        o.print("}};\n");
    }

    {
        const auto func_count = m.functions.empty() ? 1 : m.functions.size();
        o.print("inline constexpr ::skl::abix::metadata::FunctionDescriptor amc_functions[{}] = {{\n", func_count);
        for (size_t i = 0; i < m.functions.size(); ++i) {
            const auto &function = m.functions[i];
            o.print("    {{amc_function_{}_parameters, {{{}, {}}}, {{{}, {}}}, {}, {}, {}}},\n", i,
                hex_u64(function.signature.lo), hex_u64(function.signature.hi), hex_u64(function.return_type.lo),
                hex_u64(function.return_type.hi), function.parameters.size(), function.calling_convention,
                function.flags);
        }
        o.print("}};\n");
    }

    {
        const auto sym_count = m.symbols.empty() ? 1 : m.symbols.size();
        o.print("inline constexpr SymbolInfo amc_symbols[{}] = {{\n", sym_count);
        for (const auto &symbol : m.symbols)
            o.print("    {{{}, {}}},\n", static_cast<uint32_t>(symbol.kind), symbol.target_index);
        o.print("}};\n");
    }

    // Struct layout: name, version, types, canonical_types, canonical_layouts,
    // type_count, function_count, functions, symbols, symbol_count.
    o.print(
        "inline constexpr ModuleInfo amc_module{{{}, {}, amc_types, amc_canonical_types, amc_canonical_layouts, {}, "
        "{}, amc_functions, amc_symbols, {}}};\n",
        cpp_string(m.package_name), cpp_string(m.package_version), m.types.size(), m.functions.size(),
        m.symbols.size());

    // .abix.names section: type/field/function names for diagnostic use only.
    // This section is NOT referenced by any runtime code and can be safely
    // stripped via: strip --remove-section=.abix.names <binary>
    // External tools (amc-dump, ABIX symbol server) read this section from
    // the binary or from a companion .abix file archived at build time.
    //
    // `used` is required: without it the compiler is free to drop this
    // unreferenced variable entirely, which would silently make the names
    // un-strippable because they were never emitted.
    //
    // Mach-O section names are limited to 16 bytes and require an explicit
    // segment, so macOS uses "__DATA,__abix_names" instead of the ELF spelling.
    if (!m.types.empty()) {
        o.print(
            "#if defined(__APPLE__)\n"
            "__attribute__((used, section(\"__DATA,__abix_names\")))\n"
            "#elif defined(__GNUC__) || defined(__clang__)\n"
            "__attribute__((used, section(\".abix.names\")))\n"
            "#endif\n"
            "inline constexpr const char *amc_type_names[] = {{\n");
        for (const auto &type : m.types)
            o.print("    {},\n", cpp_string(type.name));
        o.print("}};\n");
    }

    // ABIX Metadata Region (AI-PM): the self-describing, pointer-free image of
    // this module (manifest + desc + hash + names). It lives in its own section
    // so an offline tool can locate the whole region through the container's
    // section table (ELF or Mach-O) without loading the program. The C++
    // descriptors above are its runtime projection and remain the hot path.
    {
        amc::MetadataOptions metadata_options;
        std::vector<uint8_t> region;
        std::string metadata_error;
        if (!amc::build_metadata_region(m, metadata_options, region, metadata_error)) {
            fmt::print(stderr, "{}\n", metadata_error);
            return 1;
        }
        o.print(
            "\n// ABIX Metadata Region: manifest + desc + hash + names (offset-based).\n"
            "#if defined(__APPLE__)\n"
            "__attribute__((used, section(\"__DATA,__abix_metadata\"), aligned(8)))\n"
            "#elif defined(__GNUC__) || defined(__clang__)\n"
            "__attribute__((used, section(\".abix.metadata\"), aligned(8)))\n"
            "#endif\n"
            "inline constexpr unsigned char amc_metadata_region[{}] = {{\n",
            region.size());
        for (size_t i = 0; i < region.size(); ++i) {
            if (i % 12 == 0) o.print("    ");
            o.print("0x{:02x},", region[i]);
            if (i % 12 == 11 || i + 1 == region.size()) o.print("\n");
        }
        o.print("}};\n");
        o.print("inline constexpr size_t amc_metadata_region_size = {};\n", region.size());
    }

    for (size_t i = 0; i < m.maps.size(); ++i) {
        const auto &map = m.maps[i];
        const auto op_count = map.operations.empty() ? 1 : map.operations.size();
        o.print("inline constexpr MapOperation amc_map_{}_operations[{}] = {{\n", i, op_count);
        for (const auto &operation : map.operations) {
            const char *opname = operation.opcode == amc::MapOpcode::copy_field    ? "copy_field"
                               : operation.opcode == amc::MapOpcode::convert_int   ? "convert_int"
                               : operation.opcode == amc::MapOpcode::convert_float ? "convert_float"
                               : operation.opcode == amc::MapOpcode::add_default   ? "add_default"
                                                                                   : "skip_field";
            o.print("    {{MapOpcode::{}, {}, {}, {}, {}, {}, {{{}, {}}}}},\n", opname, operation.source_field,
                operation.target_field, operation.source_offset, operation.target_offset, operation.byte_count,
                hex_u64(operation.auxiliary.lo), hex_u64(operation.auxiliary.hi));
        }
        o.print(
            "}};\n"
            "template <> struct MapPrivate<{}_ABIX, {}_ABIX> {{\n"
            "  static constexpr const MapOperation *operations = amc_map_{}_operations;\n"
            "  static constexpr size_t operation_count = {};\n"
            "  static bool apply(void *target, const void *source) noexcept {{\n"
            "    if (!target || !source) return false;\n",
            cpp_name(map.source_name), cpp_name(map.target_name), i, map.operations.size());
        for (const auto &operation : map.operations) {
            switch (operation.opcode) {
                case amc::MapOpcode::copy_field:
                    o.print(
                        "    memcpy(static_cast<char *>(target) + {}, static_cast<const char *>(source) + {}, "
                        "{});\n",
                        operation.target_offset, operation.source_offset, operation.byte_count);
                    break;
                case amc::MapOpcode::add_default:
                    o.print("    memset(static_cast<char *>(target) + {}, 0, {});\n", operation.target_offset,
                        operation.byte_count);
                    break;
                case amc::MapOpcode::skip_field: break;
                case amc::MapOpcode::convert_int:
                case amc::MapOpcode::convert_float:
                    o.print("    return false;  // conversion requires an explicit native converter\n");
                    break;
            }
        }
        o.print(
            "    return true;\n"
            "}}\n"
            "}};\n");
    }

    o.print(
        "}}  // namespace amc_generated\n"
        "#ifdef AMC_GENERATED_DECLARE_NATIVE_TYPE_TRAITS\n"
        "namespace skl::abix::runtime {{\n");
    for (const auto &type : m.types) {
        if (type.kind != amc::TypeKind::record
            || (type.flags & amc::type_template_primary) != 0
            || type.name.find('<') != std::string::npos
            || type.name.rfind("std::", 0) == 0)
            continue;
        const std::string id = fmt::format("{}_ABIX", cpp_name(type.name));
        o.print(
            "template <> struct TypeTraits<::{}> {{\n"
            "  static constexpr ::skl::abix::model::TypeId type_id = ::amc_generated::{}::type_id;\n"
            "}};\n",
            type.name, id);
    }
    o.print(
        "}}  // namespace skl::abix::runtime\n"
        "#endif  // AMC_GENERATED_DECLARE_NATIVE_TYPE_TRAITS\n");

    // ABI check header (AI-P2 clangd integration): compile-time ABI assertions
    // that the editor surfaces as ordinary diagnostics while a source file is
    // edited. clangd exposes no stable out-of-tree plugin API, so instead of a
    // plugin this header turns "the struct changed but the contract did not"
    // into a static_assert failure at the point of inclusion. It is a separate
    // file: a translation unit includes it *after* the native declarations it
    // names, so it never affects consumers that only want the projection.
    {
        std::string check_path(output);
        const auto slash = check_path.find_last_of('/');
        check_path = slash == std::string::npos ? std::string("amc_abi_check.hpp")
                                                : check_path.substr(0, slash + 1) + "amc_abi_check.hpp";
        auto check = fmt::output_file(check_path);
        // Field width lookup for the per-field `sizeof` assertion below. AMC
        // already recorded the field's type size, so the check is derived from
        // the artifact rather than recomputed here.
        const auto field_type_size = [&](amc::Hash128 field_type) -> uint32_t {
            for (const auto &candidate : m.types)
                if (candidate.id == field_type) return candidate.size;
            return 0;
        };

        check.print(
            "#pragma once\n"
            "// Generated by `amc generate -l cpp` (AI-P2 clangd integration).\n"
            "// Include this header AFTER the native C++ declarations it names. Every\n"
            "// static_assert below is a live ABI diagnostic: clangd and the compiler\n"
            "// report it while editing, before the mismatch reaches a running binary.\n"
            "// A mismatch means the native layout drifted from the ABIX contract.\n"
            "//\n"
            "// Coverage: overall size/alignment, each field's offset, and each\n"
            "// field's width. The width assertion catches a field whose type changed\n"
            "// to another type of a different size, which can move neither the\n"
            "// following offsets nor the total size. Bit-fields are skipped because\n"
            "// `offsetof`/`sizeof` are not defined for them.\n"
            "#include \"amc_generated.hpp\"\n"
            "#include <cstddef>\n\n");
        for (const auto &type : m.types) {
            if (!is_cpp_qualified_name(type.name)) continue;
            const std::string id = fmt::format("{}_ABIX", cpp_name(type.name));
            check.print(
                "static_assert(sizeof({0}) == ::amc_generated::{1}::size, "
                "\"ABIX ABI check: size mismatch for {0}\");\n",
                type.name, id);
            check.print(
                "static_assert(alignof({0}) == ::amc_generated::{1}::align, "
                "\"ABIX ABI check: align mismatch for {0}\");\n",
                type.name, id);
            for (uint32_t i = 0; i < type.field_count; ++i) {
                const auto &field = m.fields[type.field_begin + i];
                if (!is_cpp_identifier(field.name)) continue;
                if ((field.flags & amc::field_bitfield) != 0) continue;
                check.print(
                    "static_assert(offsetof({0}, {2}) == ::amc_generated::{1}::{2}_offset, "
                    "\"ABIX ABI check: offset mismatch for {0}::{2}\");\n",
                    type.name, id, field.name);
                const uint32_t width = field_type_size(field.type_id);
                if (width == 0) continue;
                check.print(
                    "static_assert(sizeof(static_cast<{0} *>(nullptr)->{2}) == {3}, "
                    "\"ABIX ABI check: field width mismatch for {0}::{2}\");\n",
                    type.name, id, field.name, width);
            }
        }
    }
    return 0;
}

#if defined(__APPLE__)
// Derive the Homebrew LLVM installation prefix from the known resource-dir.
//   kClangResourceDir  →  /opt/homebrew/opt/llvm/lib/clang/22
//   brew_prefix        →  /opt/homebrew/opt/llvm
//
// Returns the empty string when the prefix cannot be derived.
// On non-macOS platforms this always returns empty.
static std::string brew_llvm_prefix() {
    if (!kClangResourceDir[0]) return {};
    llvm::SmallString<128> pfx(kClangResourceDir);
    llvm::sys::path::remove_filename(pfx);
    llvm::sys::path::remove_filename(pfx);
    llvm::sys::path::remove_filename(pfx);
    llvm::SmallString<128> sanity(pfx);
    llvm::sys::path::append(sanity, "include", "c++", "v1");
    return access(sanity.c_str(), F_OK) == 0 ? pfx.c_str() : std::string{};
}
#endif

#if defined(__APPLE__)
// Detect the macOS SDK path and append flags that mirror what the Clang
// driver would normally set when invoked from the command line.
//
// When loaded as a library (no config file is read), Clang does not
// automatically know its resource directory or the active SDK sysroot.
// This function fills those gaps.
//
// Include order (required by libc++ <cstddef>):
//   1. libc++ headers  (Brew LLVM: -cxx-isystem via -nostdinc++)
//   2. Clang built-ins (via -resource-dir + -isystem)
//   3. System headers  (via -isysroot)
//
// The most important design rule is that **all three sets of headers must
// come from the same LLVM distribution**.  On macOS the default Xcode SDK
// ships its own libc++ at <sdk>/usr/include/c++/v1, but that version may
// be incompatible with the Homebrew LLVM's Clang built-in headers (e.g.
// std::string internal layout differs, leading to duplicate member errors).
//
// We therefore:
//   a. Derive the Homebrew LLVM prefix from the resource-dir known at
//      build time (kClangResourceDir);
//   b. Use -nostdinc++ to suppress the default C++ standard library search
//      (which would otherwise pick up the SDK's libc++);
//   c. Use -cxx-isystem to re-add only the Homebrew LLVM's own libc++.
//
// All three values are derived automatically: the resource directory is
// detected at build time via `clang -print-resource-dir` and injected through
// the ABIX_CLANG_RESOURCE_DIR compile definition; the SDK path is looked up
// at runtime from the SDKROOT environment variable or via xcrun.
void append_macos_sysroot(std::vector<std::string> &flags) {
    // ----- 1. Detect SDK path -------------------------------------------
    auto detect_sdk = []() -> std::string {
        if (const char *sdk = std::getenv("SDKROOT")) return sdk;
        FILE *fp = popen("xcrun --sdk macosx --show-sdk-path 2>/dev/null", "r");
        if (!fp) return {};
        char buf[4'096] = {0};
        std::string result;
        if (std::fgets(buf, sizeof(buf), fp)) result = buf;
        pclose(fp);
        while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back())))
            result.pop_back();
        return result;
    };

    // ----- 2. Check for existing configuration ----------------------------
    // If the user (or the .abic.toml) already provides explicit sysroot or
    // a valid -isystem, don't override their choices.
    for (size_t i = 0; i < flags.size(); ++i) {
        const auto &f = flags[i];
        if (f == "-isysroot" || f.find("--sysroot") == 0) return;
        if (f == "-isystem" && i + 1 < flags.size()) {
            if (access(flags[i + 1].c_str(), F_OK) == 0) return;
        }
    }

    // ----- 3. Derive Homebrew LLVM prefix --------------------------------
    const std::string brew_prefix = brew_llvm_prefix();
    const bool have_brew_libcxx = !brew_prefix.empty();

    // ----- 4. macOS SDK setup --------------------------------------------
    //
    // Include order (all from the same LLVM distribution):
    //   1. libc++ headers   (Homebrew LLVM via -isystem)
    //   2. Clang built-ins  (<resource>/include via -isystem)
    //   3. System headers   (-isysroot)
    //
    // IMPORTANT: We use `-isystem` (NOT `-cxx-isystem`) for brew libc++.
    // In Clang's header search, `-isystem` paths are searched *before*
    // `-cxx-isystem` paths.  If we used `-cxx-isystem`, the resource-dir's
    // `-isystem` path would win over libc++, and `<cstddef>` would fail
    // because it finds Clang's `<stddef.h>` instead of libc++'s wrapper.
    // ---------------------------------------------------------------------
    {
        std::string sdk = detect_sdk();
        if (!sdk.empty()) {
            // (a) libc++ – from Brew LLVM (compatible with our resource-dir)
            if (have_brew_libcxx) {
                llvm::SmallString<128> brew_libcxx(brew_prefix);
                llvm::sys::path::append(brew_libcxx, "include", "c++", "v1");
                flags.push_back("-nostdinc++");
                flags.push_back("-isystem");
                flags.push_back(brew_libcxx.c_str());
            }

            // (b) Clang built-in headers (stdarg.h, stddef.h, etc.)
            if (kClangResourceDir[0]) {
                flags.push_back("-resource-dir");
                flags.push_back(kClangResourceDir);
                llvm::SmallString<128> res_inc(kClangResourceDir);
                llvm::sys::path::append(res_inc, "include");
                if (access(res_inc.c_str(), F_OK) == 0) {
                    flags.push_back("-isystem");
                    flags.push_back(res_inc.c_str());
                }
            }

            // (c) SDK sysroot – system C headers, frameworks, etc.
            flags.push_back("-isysroot");
            flags.push_back(std::move(sdk));
        }
    }

    // ----- 5. Fallback (no SDK) ------------------------------------------
    // If no SDK is available, at least set the resource directory and
    // libc++ from the Brew LLVM prefix, so basic analysis still works.
    if (!flags.empty() && std::find(flags.begin(), flags.end(), "-resource-dir") == flags.end()) {
        // (a) libc++ from Brew LLVM (must use -isystem, not -cxx-isystem,
        //     because -cxx-isystem has lower priority than -isystem and
        //     would lose to the resource-dir's built-in headers below)
        if (have_brew_libcxx) {
            llvm::SmallString<128> brew_libcxx(brew_prefix);
            llvm::sys::path::append(brew_libcxx, "include", "c++", "v1");
            flags.push_back("-nostdinc++");
            flags.push_back("-isystem");
            flags.push_back(brew_libcxx.c_str());
        }

        // (b) Clang built-in headers
        if (kClangResourceDir[0]) {
            flags.push_back("-resource-dir");
            flags.push_back(kClangResourceDir);
            llvm::SmallString<128> res_inc(kClangResourceDir);
            llvm::sys::path::append(res_inc, "include");
            if (access(res_inc.c_str(), F_OK) == 0) {
                flags.push_back("-isystem");
                flags.push_back(res_inc.c_str());
            }
        }
    }
}
#endif

}   // namespace amc::cpp
