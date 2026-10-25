#include "SKL/AMC/Lang/Zig.h"

#include "SKL/AMC/Core/Generated.h"
#include "SKL/AMC/Core/Metadata.h"

#include <algorithm>
#include <cctype>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <fmt/os.h>

namespace amc::lang::zig {
namespace {

// Thin wrapper over `fmt::memory_buffer` so the emit helpers read like a file
// stream while the projection is built in memory.
struct Output {
    fmt::memory_buffer &buffer;
    template<typename... Args>
    void print(fmt::format_string<Args...> format, Args &&...args) {
        fmt::format_to(std::back_inserter(buffer), format, std::forward<Args>(args)...);
    }
};

bool is_zig_identifier(const std::string &s) {
    if (s.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (const char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

bool is_zig_keyword(const std::string &s) {
    static const char * const keywords[] = {"addrspace", "align", "allowzero", "and", "anyframe", "anytype", "asm",
        "async", "await", "break", "callconv", "catch", "comptime", "const", "continue", "defer", "else", "enum",
        "errdefer", "error", "export", "extern", "fn", "for", "if", "inline", "linksection", "noalias", "noinline",
        "nosuspend", "opaque", "or", "orelse", "packed", "pub", "resume", "return", "struct", "suspend", "switch",
        "test", "threadlocal", "try", "union", "unreachable", "usingnamespace", "var", "volatile", "while"};
    for (const char *k : keywords)
        if (s == k) return true;
    return false;
}

// A legal Zig identifier, or a quoted `@"..."` form when the ABI name is not
// directly usable (namespaced names, keywords, ...).
std::string zig_ident(const std::string &name) {
    if (is_zig_identifier(name) && !is_zig_keyword(name)) return name;
    std::string out = "@\"";
    for (const char c : name) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

std::string zig_string(const std::string &value) {
    std::string out = "\"";
    for (const char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    out += '"';
    return out;
}

// Reduce a type spelling to its bare name so a pointer's pointee can be matched
// against the value types in the module (same approach as the Rust backend).
std::string normalise_type_spelling(const std::string &spelling) {
    static const char * const qualifiers[] = {"const", "volatile", "struct", "class", "enum", "union"};
    std::string out;
    std::string token;
    const auto flush = [&]() {
        if (token.empty()) return;
        bool qualifier = false;
        for (const char *q : qualifiers)
            if (token == q) {
                qualifier = true;
                break;
            }
        if (!qualifier) out += token;
        token.clear();
    };
    for (const char c : spelling) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == '*' || c == '&') {
            flush();
            continue;
        }
        token += c;
    }
    flush();
    return out;
}

bool spelling_has_qualifier(const std::string &spelling, const std::string &qualifier) {
    std::string token;
    for (size_t i = 0; i <= spelling.size(); ++i) {
        const char c = i < spelling.size() ? spelling[i] : ' ';
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (token == qualifier) return true;
            token.clear();
        } else {
            token += c;
        }
    }
    return false;
}

class Projector {
public:
    explicit Projector(const amc::AbiModule &module)
        : m(module) {
        names.reserve(m.types.size());
        std::unordered_map<std::string, uint32_t> used;
        for (const auto &type : m.types) {
            std::string candidate = type.name;
            const auto count = used[candidate]++;
            if (count != 0) candidate += fmt::format("_{}", count);
            names.push_back(std::move(candidate));
        }
        for (size_t i = 0; i < m.types.size(); ++i)
            if (m.types[i].kind != amc::TypeKind::pointer)
                by_spelling.emplace(normalise_type_spelling(m.types[i].name), static_cast<int>(i));
    }

    int index_of(amc::Hash128 id) const {
        for (size_t i = 0; i < m.types.size(); ++i)
            if (m.types[i].id == id) return static_cast<int>(i);
        return -1;
    }

    const std::string &raw_name(amc::Hash128 id) const {
        const int index = index_of(id);
        return index < 0 ? empty : names[static_cast<size_t>(index)];
    }

    std::string decl_name(amc::Hash128 id) const { return zig_ident(raw_name(id)); }

    std::string field_type(amc::Hash128 id) const {
        const int index = index_of(id);
        if (index < 0) return "anyopaque";
        const auto &type = m.types[static_cast<size_t>(index)];
        if (type.kind == amc::TypeKind::primitive) return primitive_zig_type(type.primitive_abi);
        if (type.kind == amc::TypeKind::pointer) return pointer_zig_type(type);
        return decl_name(id);
    }

    std::string pointer_zig_type(const amc::Type &type) const {
        const std::string &n = type.name;
        bool is_const = false;
        std::string pointee;
        if (!n.empty() && n.back() == '*') {
            pointee = n.substr(0, n.size() - 1);
            is_const = spelling_has_qualifier(pointee, "const");
        } else if (n.rfind("*const", 0) == 0) {
            is_const = true;
            pointee = n.substr(6);
        } else if (n.rfind("*mut", 0) == 0) {
            pointee = n.substr(4);
        } else if (!n.empty() && n[0] == '&') {
            pointee = n.rfind("&mut", 0) == 0 ? n.substr(4) : n.substr(1);
            is_const = n.rfind("&mut", 0) != 0;
        } else {
            return "?*anyopaque";
        }
        if (pointee.find('*') != std::string::npos || pointee.find('&') != std::string::npos)
            return is_const ? "?*const anyopaque" : "?*anyopaque";
        const std::string base = resolve_spelling(pointee);
        // Zig 0.16 rejects `[*c]anyopaque`; opaque pointees use a nullable pointer.
        if (base.empty() || base == "void" || base == "anyopaque")
            return is_const ? "?*const anyopaque" : "?*anyopaque";
        return (is_const ? "[*c]const " : "[*c]") + base;
    }

    const amc::AbiModule &m;

private:
    std::string resolve_spelling(const std::string &spelling) const {
        const auto found = by_spelling.find(normalise_type_spelling(spelling));
        if (found == by_spelling.end()) return {};
        return field_type(m.types[static_cast<size_t>(found->second)].id);
    }

    std::vector<std::string> names;
    std::unordered_map<std::string, int> by_spelling;
    std::string empty;
};

std::string enum_underlying(uint32_t size) {
    switch (size) {
        case 1:  return "i8";
        case 2:  return "i16";
        case 8:  return "i64";
        case 16: return "i128";
        case 4:
        default: return "c_int";
    }
}

void emit_constants(Output &o, const Projector &p, const amc::Type &type) {
    const std::string name = p.decl_name(type.id);
    o.print("pub const {}_ABIX_TYPE_ID_LO: u64 = 0x{:x};\n", p.raw_name(type.id), type.id.lo);
    o.print("pub const {}_ABIX_TYPE_ID_HI: u64 = 0x{:x};\n", p.raw_name(type.id), type.id.hi);
    o.print("pub const {}_ABIX_SIZE: usize = {};\n", p.raw_name(type.id), type.size);
    o.print("pub const {}_ABIX_ALIGN: usize = {};\n", p.raw_name(type.id), type.align);
    for (uint32_t i = 0; i < type.field_count; ++i) {
        const auto &field = p.m.fields[type.field_begin + i];
        if (field.name.empty()) continue;
        o.print("pub const {}_ABIX_{}_OFFSET: usize = {};\n", p.raw_name(type.id), field.name, field.offset);
    }
}

void emit_record(Output &o, const Projector &p, const amc::Type &type) {
    if (type.field_count == 0) {
        o.print("pub const {} = extern struct {{\n", p.decl_name(type.id));
        o.print("    bytes: [{}]u8 align({}),\n", type.size == 0 ? 1 : type.size, type.align == 0 ? 1 : type.align);
        o.print("}};\n");
        emit_constants(o, p, type);
        return;
    }
    uint32_t natural = 1;
    for (uint32_t i = 0; i < type.field_count; ++i) {
        const int index = p.index_of(p.m.fields[type.field_begin + i].type_id);
        if (index >= 0) natural = std::max(natural, p.m.types[static_cast<size_t>(index)].align);
    }
    o.print("pub const {} = extern struct {{\n", p.decl_name(type.id));
    uint32_t base_index = 0;
    for (uint32_t i = 0; i < type.field_count; ++i) {
        const auto &field = p.m.fields[type.field_begin + i];
        if ((field.flags & amc::field_base) != 0) {
            o.print("    __base{}: {},\n", base_index++, p.field_type(field.type_id));
            continue;
        }
        if (field.name.empty()) continue;
        o.print("    {}: {},\n", zig_ident(field.name), p.field_type(field.type_id));
    }
    if (type.align > natural)
        o.print("}} align({});\n", type.align);
    else
        o.print("}};\n");
    emit_constants(o, p, type);
}

void emit_enumeration(Output &o, const Projector &p, const amc::Type &type) {
    o.print("pub const {} = {};\n", p.decl_name(type.id), enum_underlying(type.size));
}

void emit_alias(Output &o, const Projector &p, const amc::Type &type) {
    std::string underlying = "anyopaque";
    for (uint32_t i = 0; i < type.field_count; ++i) {
        const auto &field = p.m.fields[type.field_begin + i];
        if (field.name == "underlying") {
            underlying = p.field_type(field.type_id);
            break;
        }
    }
    o.print("pub const {} = {};\n", p.decl_name(type.id), underlying);
}

void emit_opaque(Output &o, const Projector &p, const amc::Type &type) {
    o.print("pub const {} = extern struct {{\n", p.decl_name(type.id));
    o.print("    bytes: [{}]u8 align({}),\n", type.size, type.align == 0 ? 1 : type.align);
    o.print("}};\n");
    emit_constants(o, p, type);
}

void emit_type(Output &o, const Projector &p, const amc::Type &type) {
    switch (type.kind) {
        case amc::TypeKind::record:      emit_record(o, p, type); break;
        case amc::TypeKind::enumeration: emit_enumeration(o, p, type); break;
        case amc::TypeKind::alias:       emit_alias(o, p, type); break;
        case amc::TypeKind::pointer:
            o.print("pub const {} = {};\n", p.decl_name(type.id), p.pointer_zig_type(type));
            break;
        case amc::TypeKind::array:          emit_opaque(o, p, type); break;
        case amc::TypeKind::namespace_type: break;
        case amc::TypeKind::primitive:      break;
        case amc::TypeKind::function:       break;
    }
}

void emit_functions(Output &o, const Projector &p) {
    std::vector<const amc::Function *> free_functions;
    for (const auto &function : p.m.functions)
        if (function.owner_type.lo == 0 && function.owner_type.hi == 0) free_functions.push_back(&function);
    if (free_functions.empty()) return;
    o.print("\n// Free functions declared with the C calling convention.\n");
    for (const auto *function : free_functions) {
        std::string params;
        for (size_t i = 0; i < function->parameters.size(); ++i) {
            if (i != 0) params += ", ";
            const auto &parameter = function->parameters[i];
            std::string parameter_name = parameter.name.empty() ? fmt::format("arg{}", i) : zig_ident(parameter.name);
            params += fmt::format("{}: {}", parameter_name, p.field_type(parameter.type_id));
        }
        const int return_index = p.index_of(function->return_type);
        const bool returns_void = return_index >= 0
                               && p.m.types[static_cast<size_t>(return_index)].kind == amc::TypeKind::primitive
                               && (p.m.types[static_cast<size_t>(return_index)].primitive_abi & 0xffu)
                                      == static_cast<uint32_t>(amc::PrimitiveAbiKind::void_);
        if (returns_void)
            o.print("pub extern fn {}({}) void;\n", zig_ident(function->name), params);
        else
            o.print(
                "pub extern fn {}({}) {};\n", zig_ident(function->name), params, p.field_type(function->return_type));
    }
}

void emit_layout_checks(Output &o, const Projector &p) {
    bool any = false;
    for (const auto &type : p.m.types) {
        if (type.kind != amc::TypeKind::record || type.name.empty()) continue;
        if (!any) {
            o.print("\n// Compile-time ABI assertions: a layout drift is a compile error.\n");
            o.print("comptime {{\n");
            any = true;
        }
        const std::string name = p.decl_name(type.id);
        o.print("    if (@sizeOf({}) != {}) @compileError(\"{} size mismatch\");\n", name, type.size, type.name);
        o.print("    if (@alignOf({}) != {}) @compileError(\"{} align mismatch\");\n", name,
            type.align == 0 ? 1 : type.align, type.name);
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto &field = p.m.fields[type.field_begin + i];
            if (field.name.empty() || (field.flags & amc::field_base) != 0) continue;
            if ((field.flags & amc::field_bitfield) != 0) continue;
            o.print("    if (@offsetOf({}, {}) != {}) @compileError(\"{}::{} offset mismatch\");\n", name,
                zig_string(field.name), field.offset, type.name, field.name);
        }
    }
    if (any) o.print("}}\n");
}

bool emit_metadata_region(Output &o, const amc::AbiModule &m, std::string &error) {
    amc::MetadataOptions options;
    std::vector<uint8_t> region;
    if (!amc::build_metadata_region(m, options, region, error)) return false;
    o.print("\n// ABIX Metadata Region: manifest + desc + hash + names (offset-based).\n");
    o.print("pub const AMC_METADATA_REGION: [{}]u8 = .{{\n", region.size());
    for (size_t i = 0; i < region.size(); ++i) {
        if (i % 12 == 0) o.print("    ");
        o.print("0x{:02x},", region[i]);
        if (i % 12 == 11 || i + 1 == region.size()) o.print("\n");
    }
    o.print("}};\n");
    o.print("pub const AMC_METADATA_REGION_SIZE: usize = {};\n", region.size());
    return true;
}

}   // namespace

std::string primitive_zig_type(uint32_t primitive_abi) {
    const uint32_t kind = primitive_abi & 0xffu;
    const uint32_t width = (primitive_abi & amc::primitive_width_mask) >> amc::primitive_width_shift;
    switch (static_cast<amc::PrimitiveAbiKind>(kind)) {
        case amc::PrimitiveAbiKind::void_:          return "void";
        case amc::PrimitiveAbiKind::bool_:          return "bool";
        case amc::PrimitiveAbiKind::char_signed:
        case amc::PrimitiveAbiKind::char_unsigned:  return "c_char";
        case amc::PrimitiveAbiKind::schar:          return "i8";
        case amc::PrimitiveAbiKind::uchar:          return "u8";
        case amc::PrimitiveAbiKind::char8:          return "u8";
        case amc::PrimitiveAbiKind::char16:         return "u16";
        case amc::PrimitiveAbiKind::char32:         return "u32";
        case amc::PrimitiveAbiKind::wchar_signed:   return width == 2 ? "i16" : "i32";
        case amc::PrimitiveAbiKind::wchar_unsigned: return width == 2 ? "u16" : "u32";
        case amc::PrimitiveAbiKind::sint:
            switch (width) {
                case 1:  return "i8";
                case 2:  return "i16";
                case 8:  return "c_longlong";
                case 16: return "i128";
                case 4:
                default: return "c_int";
            }
        case amc::PrimitiveAbiKind::uint:
            switch (width) {
                case 1:  return "u8";
                case 2:  return "u16";
                case 8:  return "c_ulonglong";
                case 16: return "u128";
                case 4:
                default: return "c_uint";
            }
        case amc::PrimitiveAbiKind::floating: {
            switch (static_cast<amc::FloatFormat>(
                (primitive_abi & amc::primitive_format_mask) >> amc::primitive_format_shift)) {
                case amc::FloatFormat::ieee32:            return "f32";
                case amc::FloatFormat::ieee64:            return "f64";
                case amc::FloatFormat::ieee16:            return "u16";
                case amc::FloatFormat::bf16:              return "u16";
                case amc::FloatFormat::ieee128:           return "u128";
                case amc::FloatFormat::x87_80:            return "u128";
                case amc::FloatFormat::ppc_double_double: return "u128";
                case amc::FloatFormat::none:
                default:                                  return "f64";
            }
        }
        case amc::PrimitiveAbiKind::none:
        default:                          return "anyopaque";
    }
}

std::string generate_projection(const amc::AbiModule &module) {
    Projector projector(module);
    fmt::memory_buffer buffer;
    Output o{buffer};
    o.print(AMC_GENERATED_FILE_WARNING("//"));
    o.print("// ABI: {} {} (arch={}, os={}, abi={})\n", module.package_name, module.package_version, module.arch,
        module.os, module.target_abi);
    o.print("pub const ABIX_PACKAGE_NAME = {};\n", zig_string(module.package_name));
    o.print("pub const ABIX_PACKAGE_VERSION = {};\n", zig_string(module.package_version));
    o.print("pub const ABIX_LIBRARY_NAME = {};\n", zig_string(module.package_name));

    for (const auto &type : module.types) {
        if (amc::lang::cancellation_requested()) return {};
        o.print("\n");
        emit_type(o, projector, type);
    }
    emit_functions(o, projector);
    emit_layout_checks(o, projector);
    std::string error;
    if (!emit_metadata_region(o, module, error)) fmt::print(stderr, "{}\n", error);
    return fmt::to_string(buffer);
}

int backend(const char *input, const char *output) {
    if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
    amc::AbiModule module;
    std::string error;
    if (!amc::read_abix(input, module, error)) {
        fmt::print(stderr, "{}\n", error);
        return 1;
    }
    if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
    try {
        const std::string projection = generate_projection(module);
        if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
        auto file = fmt::output_file(output);
        file.print("{}", projection);
    } catch (const std::exception &e) {
        fmt::print(stderr, "cannot write {}: {}\n", output, e.what());
        return 1;
    }
    return 0;
}

}   // namespace amc::lang::zig
