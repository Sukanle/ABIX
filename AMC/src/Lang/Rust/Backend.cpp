#include "AMC/Lang/Rust.h"

#include "AMC/Core/Metadata.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <fmt/os.h>

namespace amc::rust {
namespace {

// Thin wrapper over `fmt::memory_buffer` so the emit helpers read like a file
// stream while the projection is built in memory.
struct Output {
    fmt::memory_buffer &buffer;
    template <typename... Args>
    void print(fmt::format_string<Args...> format, Args &&...args) {
        fmt::format_to(std::back_inserter(buffer), format, std::forward<Args>(args)...);
    }
};

// True when `s` can be pasted as a Rust identifier without rewriting.
bool is_rust_identifier(const std::string &s) {
    if (s.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (const char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

bool is_rust_keyword(const std::string &s) {
    static const char * const keywords[] = {"as", "break", "const", "continue", "crate", "dyn", "else", "enum",
        "extern", "false", "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod", "move", "mut", "pub",
        "ref", "return", "self", "Self", "static", "struct", "super", "trait", "true", "type", "unsafe", "use",
        "where", "while", "async", "await", "box", "abstract", "become", "do", "final", "macro", "override",
        "priv", "try", "typeof", "unsized", "virtual", "yield"};
    for (const char *k : keywords)
        if (s == k) return true;
    return false;
}

// Map an arbitrary ABIX identifier to a legal Rust identifier. `::` becomes `_`
// and any other non-identifier character becomes `_`; a name that is empty or
// starts with a digit is prefixed.
std::string rust_ident(const std::string &name) {
    if (is_rust_identifier(name) && !is_rust_keyword(name)) return name;
    std::string out;
    out.reserve(name.size() + 1);
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            out += c;
        else
            out += '_';
    }
    if (out.empty()) out = "abix";
    if (std::isdigit(static_cast<unsigned char>(out[0]))) out.insert(out.begin(), '_');
    if (is_rust_keyword(out)) out += '_';
    return out;
}

// Field identifier: keywords are escaped as raw identifiers so the ABI-visible
// spelling is preserved as closely as possible.
std::string rust_field_ident(const std::string &name) {
    const std::string ident = rust_ident(name);
    if (ident == "self" || ident == "Self" || ident == "super" || ident == "crate") return ident + "_";
    if (is_rust_keyword(ident)) return "r#" + ident;
    return ident;
}

std::string rust_string(const std::string &value) {
    std::string out = "\"";
    for (const char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    out += '"';
    return out;
}

std::string hex_u64(uint64_t value) { return fmt::format("0x{:x}", value); }

// Identifies a type by TypeID so references can be resolved without a hash map.
class Projector {
public:
    explicit Projector(const amc::AbiModule &module)
        : m(module) {
        names.reserve(m.types.size());
        std::unordered_map<std::string, uint32_t> used;
        for (const auto &type : m.types) {
            std::string candidate = rust_ident(type.name);
            const auto count = used[candidate]++;
            if (count != 0) candidate += fmt::format("_{}", count);
            names.push_back(std::move(candidate));
        }
    }

    int index_of(amc::Hash128 id) const {
        for (size_t i = 0; i < m.types.size(); ++i)
            if (m.types[i].id == id) return static_cast<int>(i);
        return -1;
    }

    const std::string &name_of(amc::Hash128 id) const {
        const int index = index_of(id);
        return index < 0 ? empty : names[static_cast<size_t>(index)];
    }

    // Rust type usable in a field/parameter position for `id`.
    std::string field_type(amc::Hash128 id) const {
        const int index = index_of(id);
        if (index < 0) return "core::ffi::c_void";
        const auto &type = m.types[static_cast<size_t>(index)];
        if (type.kind == amc::TypeKind::primitive) return primitive_rust_type(type.primitive_abi);
        if (type.kind == amc::TypeKind::pointer) return "*mut core::ffi::c_void";
        return names[static_cast<size_t>(index)];
    }

    const amc::AbiModule &m;

private:
    std::vector<std::string> names;
    std::string empty;
};

// Underlying Rust integer for a `#[repr(C)]` enumeration of a given width.
std::string enum_underlying(uint32_t size) {
    switch (size) {
        case 1:  return "i8";
        case 2:  return "i16";
        case 8:  return "i64";
        case 16: return "i128";
        case 4:
        default: return "core::ffi::c_int";
    }
}

void emit_constants(Output &o, const Projector &p, const amc::Type &type) {
    const std::string &name = p.name_of(type.id);
    o.print("impl {} {{\n", name);
    o.print("    pub const ABIX_TYPE_ID_LO: u64 = {};\n", hex_u64(type.id.lo));
    o.print("    pub const ABIX_TYPE_ID_HI: u64 = {};\n", hex_u64(type.id.hi));
    o.print("    pub const ABIX_SIZE: usize = {};\n", type.size);
    o.print("    pub const ABIX_ALIGN: usize = {};\n", type.align);
    o.print("    pub const ABIX_FIELD_COUNT: u32 = {};\n", type.field_count);
    for (uint32_t i = 0; i < type.field_count; ++i) {
        const auto &field = p.m.fields[type.field_begin + i];
        if (field.name.empty()) continue;
        o.print("    pub const ABIX_{}_OFFSET: usize = {};\n", rust_ident(field.name), field.offset);
    }
    o.print("}}\n");
}

void emit_record(Output &o, const Projector &p, const amc::Type &type) {
    const std::string &name = p.name_of(type.id);
    // Natural alignment implied by the fields as projected. A larger recorded
    // alignment comes from an explicit `#[repr(C, align(N))]` and must be
    // re-emitted, otherwise the projected type would be under-aligned.
    uint32_t natural = 1;
    for (uint32_t i = 0; i < type.field_count; ++i) {
        const auto &field = p.m.fields[type.field_begin + i];
        const int index = p.index_of(field.type_id);
        if (index >= 0) natural = std::max(natural, p.m.types[static_cast<size_t>(index)].align);
    }
    if (type.align > natural)
        o.print("#[repr(C, align({}))]\n", type.align);
    else
        o.print("#[repr(C)]\n");
    o.print("pub struct {} {{\n", name);
    uint32_t base_index = 0;
    for (uint32_t i = 0; i < type.field_count; ++i) {
        const auto &field = p.m.fields[type.field_begin + i];
        const bool is_base = (field.flags & amc::field_base) != 0;
        if (is_base) {
            o.print("    pub __base{}: {},\n", base_index++, p.field_type(field.type_id));
            continue;
        }
        if (field.name.empty()) continue;
        o.print("    pub {}: {},\n", rust_field_ident(field.name), p.field_type(field.type_id));
    }
    o.print("}}\n");
    emit_constants(o, p, type);
}

void emit_enumeration(Output &o, const Projector &p, const amc::Type &type) {
    // ABIX does not retain enumerator values, so the enumeration projects to its
    // underlying integer type; layout compatibility is still enforced below.
    o.print("pub type {} = {};\n", p.name_of(type.id), enum_underlying(type.size));
}

void emit_alias(Output &o, const Projector &p, const amc::Type &type) {
    std::string underlying = "core::ffi::c_void";
    for (uint32_t i = 0; i < type.field_count; ++i) {
        const auto &field = p.m.fields[type.field_begin + i];
        if (field.name == "underlying") {
            underlying = p.field_type(field.type_id);
            break;
        }
    }
    o.print("pub type {} = {};\n", p.name_of(type.id), underlying);
}

void emit_opaque(Output &o, const Projector &p, const amc::Type &type) {
    // Arrays (the element type is not part of the ABI record) and other
    // unmodelled aggregates project to an explicitly sized, explicitly aligned
    // byte blob so size and alignment stay exact.
    o.print("#[repr(C, align({}))]\n", type.align == 0 ? 1 : type.align);
    o.print("pub struct {}([u8; {}]);\n", p.name_of(type.id), type.size);
    emit_constants(o, p, type);
}

void emit_type(Output &o, const Projector &p, const amc::Type &type) {
    switch (type.kind) {
        case amc::TypeKind::record: emit_record(o, p, type); break;
        case amc::TypeKind::enumeration: emit_enumeration(o, p, type); break;
        case amc::TypeKind::alias: emit_alias(o, p, type); break;
        case amc::TypeKind::pointer: o.print("pub type {} = *mut core::ffi::c_void;\n", p.name_of(type.id)); break;
        case amc::TypeKind::array: emit_opaque(o, p, type); break;
        case amc::TypeKind::namespace_type: break;
        case amc::TypeKind::primitive: break;
        case amc::TypeKind::function: break;
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
            std::string parameter_name =
                parameter.name.empty() ? fmt::format("arg{}", i) : rust_field_ident(parameter.name);
            params += fmt::format("{}: {}", parameter_name, p.field_type(parameter.type_id));
        }
        const int return_index = p.index_of(function->return_type);
        const bool returns_void = return_index >= 0
            && p.m.types[static_cast<size_t>(return_index)].kind == amc::TypeKind::primitive
            && (p.m.types[static_cast<size_t>(return_index)].primitive_abi & 0xffu)
                == static_cast<uint32_t>(amc::PrimitiveAbiKind::void_);
        if (returns_void)
            o.print("unsafe extern \"C\" {{ pub fn {}({}) -> (); }}\n", rust_ident(function->name), params);
        else
            o.print("unsafe extern \"C\" {{ pub fn {}({}) -> {}; }}\n", rust_ident(function->name), params,
                p.field_type(function->return_type));
    }
}

void emit_abi_checks(Output &o, const Projector &p) {
    bool any = false;
    for (const auto &type : p.m.types) {
        if (type.kind != amc::TypeKind::record || type.name.empty()) continue;
        if (!is_rust_identifier(p.name_of(type.id))) continue;
        if (!any) {
            o.print("\n// Compile-time ABI assertions: a layout drift is a compile error.\n");
            any = true;
        }
        const std::string &name = p.name_of(type.id);
        o.print("const _: () = assert!(core::mem::size_of::<{}>() == {});\n", name, type.size);
        o.print("const _: () = assert!(core::mem::align_of::<{}>() == {});\n", name,
            type.align == 0 ? 1 : type.align);
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto &field = p.m.fields[type.field_begin + i];
            if (field.name.empty() || (field.flags & amc::field_base) != 0) continue;
            if ((field.flags & amc::field_bitfield) != 0) continue;
            o.print("const _: () = assert!(core::mem::offset_of!({}, {}) == {});\n", name,
                rust_field_ident(field.name), field.offset);
        }
    }
}

bool emit_metadata_region(Output &o, const amc::AbiModule &m, std::string &error) {
    amc::MetadataOptions options;
    std::vector<uint8_t> region;
    if (!amc::build_metadata_region(m, options, region, error)) return false;
    o.print("\n// ABIX Metadata Region: manifest + desc + hash + names (offset-based).\n");
    o.print("pub const AMC_METADATA_REGION: [u8; {}] = [\n", region.size());
    for (size_t i = 0; i < region.size(); ++i) {
        if (i % 12 == 0) o.print("    ");
        o.print("0x{:02x},", region[i]);
        if (i % 12 == 11 || i + 1 == region.size()) o.print("\n");
    }
    o.print("];\n");
    o.print("pub const AMC_METADATA_REGION_SIZE: usize = {};\n", region.size());
    return true;
}

}   // namespace

std::string primitive_rust_type(uint32_t primitive_abi) {
    const uint32_t kind = primitive_abi & 0xffu;
    const uint32_t width = (primitive_abi & amc::primitive_width_mask) >> amc::primitive_width_shift;
    switch (static_cast<amc::PrimitiveAbiKind>(kind)) {
        case amc::PrimitiveAbiKind::void_: return "()";
        case amc::PrimitiveAbiKind::bool_: return "bool";
        case amc::PrimitiveAbiKind::char_signed:
        case amc::PrimitiveAbiKind::char_unsigned: return "core::ffi::c_char";
        case amc::PrimitiveAbiKind::schar: return "core::ffi::c_schar";
        case amc::PrimitiveAbiKind::uchar: return "core::ffi::c_uchar";
        case amc::PrimitiveAbiKind::char8: return "u8";
        case amc::PrimitiveAbiKind::char16: return "u16";
        case amc::PrimitiveAbiKind::char32: return "u32";
        case amc::PrimitiveAbiKind::wchar_signed: return width == 2 ? "i16" : "i32";
        case amc::PrimitiveAbiKind::wchar_unsigned: return width == 2 ? "u16" : "u32";
        case amc::PrimitiveAbiKind::sint:
            switch (width) {
                case 1:  return "i8";
                case 2:  return "i16";
                case 8:  return "core::ffi::c_longlong";
                case 16: return "i128";
                case 4:
                default: return "core::ffi::c_int";
            }
        case amc::PrimitiveAbiKind::uint:
            switch (width) {
                case 1:  return "u8";
                case 2:  return "u16";
                case 8:  return "core::ffi::c_ulonglong";
                case 16: return "u128";
                case 4:
                default: return "core::ffi::c_uint";
            }
        case amc::PrimitiveAbiKind::floating: {
            switch (static_cast<amc::FloatFormat>(
                (primitive_abi & amc::primitive_format_mask) >> amc::primitive_format_shift)) {
                case amc::FloatFormat::ieee32: return "core::ffi::c_float";
                case amc::FloatFormat::ieee64: return "core::ffi::c_double";
                case amc::FloatFormat::ieee16: return "u16";
                case amc::FloatFormat::bfloat16: return "u16";
                case amc::FloatFormat::ieee128: return "u128";
                case amc::FloatFormat::x87_80: return "u128";
                case amc::FloatFormat::ppc_double_double: return "u128";
                case amc::FloatFormat::none:
                default: return "core::ffi::c_double";
            }
        }
        case amc::PrimitiveAbiKind::none:
        default: return "core::ffi::c_void";
    }
}

std::string generate_projection(const amc::AbiModule &module) {
    Projector projector(module);
    fmt::memory_buffer buffer;
    Output o{buffer};
    o.print("// Generated by `amc generate -l rust`. Do not edit.\n");
    o.print("// ABI: {} {} (arch={}, os={}, abi={})\n", module.package_name, module.package_version, module.arch,
        module.os, module.target_abi);
    o.print("#![allow(non_camel_case_types, non_snake_case, non_upper_case_globals, dead_code, unused_imports)]\n");
    o.print("use core::ffi;\n\n");
    o.print("pub const ABIX_PACKAGE_NAME: &str = {};\n", rust_string(module.package_name));
    o.print("pub const ABIX_PACKAGE_VERSION: &str = {};\n", rust_string(module.package_version));

    for (const auto &type : module.types) {
        o.print("\n");
        emit_type(o, projector, type);
    }
    emit_functions(o, projector);
    emit_abi_checks(o, projector);
    std::string error;
    if (!emit_metadata_region(o, module, error)) fmt::print(stderr, "{}\n", error);
    return fmt::to_string(buffer);
}

int backend(const char *input, const char *output) {
    amc::AbiModule module;
    std::string error;
    if (!amc::read_abix(input, module, error)) {
        fmt::print(stderr, "{}\n", error);
        return 1;
    }
    try {
        auto file = fmt::output_file(output);
        file.print("{}", generate_projection(module));
    } catch (const std::exception &e) {
        fmt::print(stderr, "cannot write {}: {}\n", output, e.what());
        return 1;
    }
    return 0;
}

}   // namespace amc::rust
