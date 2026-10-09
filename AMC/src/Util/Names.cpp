#include "AMC/Util/Names.h"

namespace amc::util {

std::string arch_name(std::uint32_t value) {
    switch (value) {
        case 0:  return "x86_64";
        case 1:  return "aarch64";
        case 2:  return "riscv64";
        default: return "unknown";
    }
}

std::string os_name(std::uint32_t value) {
    switch (value) {
        case 0:  return "linux";
        case 1:  return "windows";
        case 2:  return "macos";
        default: return "unknown";
    }
}

std::string compiler_name(std::uint32_t value) {
    switch (value) {
        case 0:  return "gcc";
        case 1:  return "clang";
        case 2:  return "msvc";
        default: return "unknown";
    }
}

std::string target_calling_convention_name(std::uint32_t value) {
    switch (value) {
        case 0:  return "sysv_abi";
        case 1:  return "ms_abi";
        case 2:  return "aapcs";
        default: return "unknown";
    }
}

std::string function_calling_convention_name(std::uint32_t value) {
    switch (value) {
        case 0:  return "unspecified";
        case 1:  return "c";
        case 2:  return "stdcall";
        case 3:  return "fastcall";
        case 4:  return "thiscall";
        case 5:  return "aarch64_sve";
        default: return "unknown";
    }
}

const char *primitive_kind_name(std::uint32_t kind) {
    switch (static_cast<amc::PrimitiveAbiKind>(kind & 0xffu)) {
        case amc::PrimitiveAbiKind::none:           return "none";
        case amc::PrimitiveAbiKind::void_:          return "void";
        case amc::PrimitiveAbiKind::bool_:          return "bool";
        case amc::PrimitiveAbiKind::char_signed:    return "char_signed";
        case amc::PrimitiveAbiKind::char_unsigned:  return "char_unsigned";
        case amc::PrimitiveAbiKind::schar:          return "schar";
        case amc::PrimitiveAbiKind::uchar:          return "uchar";
        case amc::PrimitiveAbiKind::char8:          return "char8";
        case amc::PrimitiveAbiKind::char16:         return "char16";
        case amc::PrimitiveAbiKind::char32:         return "char32";
        case amc::PrimitiveAbiKind::wchar_signed:   return "wchar_signed";
        case amc::PrimitiveAbiKind::wchar_unsigned: return "wchar_unsigned";
        case amc::PrimitiveAbiKind::sint:           return "sint";
        case amc::PrimitiveAbiKind::uint:           return "uint";
        case amc::PrimitiveAbiKind::floating:       return "floating";
    }
    return "unknown";
}

const char *float_format_name(std::uint32_t format) {
    switch (static_cast<amc::FloatFormat>((format & amc::primitive_format_mask) >> amc::primitive_format_shift)) {
        case amc::FloatFormat::none:              return "none";
        case amc::FloatFormat::ieee16:            return "ieee16";
        case amc::FloatFormat::bfloat16:          return "bfloat16";
        case amc::FloatFormat::ieee32:            return "ieee32";
        case amc::FloatFormat::ieee64:            return "ieee64";
        case amc::FloatFormat::ieee128:           return "ieee128";
        case amc::FloatFormat::x87_80:            return "x87_80";
        case amc::FloatFormat::ppc_double_double: return "ppc_double_double";
    }
    return "unknown";
}

const char *map_opcode_name(amc::MapOpcode opcode) {
    switch (opcode) {
        case amc::MapOpcode::copy_field:    return "copy_field";
        case amc::MapOpcode::convert_int:   return "convert_int";
        case amc::MapOpcode::convert_float: return "convert_float";
        case amc::MapOpcode::add_default:   return "add_default";
        case amc::MapOpcode::skip_field:    return "skip_field";
    }
    return "unknown";
}

}   // namespace amc::util
