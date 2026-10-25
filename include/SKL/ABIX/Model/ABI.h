/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */
#ifndef SKL_ABIX_MODEL_ABI_H
#define SKL_ABIX_MODEL_ABI_H

#include <float.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#include <limits>

#include "SKL/ABIX/Util/Hash.h"

namespace skl::abix::model {

using SignatureHash = Hash128;
using ABIHash = Hash128;

using TypeId = Hash128;
using LayoutHash = Hash128;

// AMC's canonical TypeID: `hash_text(canonical_name, "TYPE")`. Unlike
// `type_hash` in Compat.h (ABIX's internal FNV scheme), this reproduces the
// identity AMC records in artifacts. Primitives use AMC's ABI key
// (`prim:i32`, `prim:u64`, `prim:fieee32`, ...), not the C++ spelling; user-defined
// types use the canonical spelling AMC records. Keep the name exact.
constexpr TypeId canonical_type_id(const char *canonical_name) noexcept {
    size_t size = 0;
    while (canonical_name != nullptr && canonical_name[size] != '\0')
        ++size;
    return util::canonical_hash(canonical_name, size, 0X54595045ULL);
}

struct TypeLayout {
    uint32_t size;
    uint32_t align;
    uint32_t field_begin;
    uint32_t field_count;
    LayoutHash layout_hash;
};

struct TypeDesc {
    TypeId id;
    uint32_t flags;
    uint32_t name_offset;
    uint32_t layout_index;
};

// Historical spelling retained for source compatibility.  A type's stable
// identity is its TypeDesc; the independently-versioned physical layout is
// represented by TypeLayout.  Keeping this as an alias also makes generated
// TypeTraits<TypeDesc> usable by callers that still spell the model as
// TypeInfo.
using TypeInfo = TypeDesc;

struct Field {
    uint32_t name_offset;
    TypeId type_id;
    uint32_t offset;
    uint32_t flags;
    uint16_t bit_offset;
    uint16_t bit_width;
};

struct Function {
    uint32_t name_offset;
    uint32_t signature_offset;
    uint32_t parameter_begin;
    uint16_t parameter_count;
    uint8_t calling_convention;
    uint8_t flags;
};

struct Symbol {
    uint32_t name_offset;
    uint32_t value_index;
    TypeId type_id;
    uint8_t kind;
    uint8_t visibility;
    uint16_t reserved;
};

struct MapInfo {
    TypeId source;
    TypeId target;
    uint32_t rule;
    uint32_t flags;
};

struct Compat {
    TypeId source;
    TypeId target;
    uint32_t result;
    uint32_t flags;
};

struct ABIIdentity {
    uint32_t arch;
    uint32_t operating_system;
    uint32_t compiler;
    uint32_t calling_convention;
    uint32_t flags;
};

struct Target {
    uint32_t arch;
    uint32_t operating_system;
    uint32_t abi;
};

static_assert(sizeof(TypeLayout) == 32, "TypeLayout wire layout changed");
static_assert(sizeof(TypeDesc) == 32, "TypeDesc wire layout changed");
static_assert(sizeof(Field) == 40, "Field wire layout changed");
static_assert(sizeof(Function) == 16, "Function wire layout changed");
static_assert(sizeof(Symbol) == 32, "Symbol wire layout changed");

// Predefined TypeIDs for the canonical ABIX primitive descriptors. These are
// identities, not language type aliases: `f16`, `bf16` and `f128` are available
// even when the host compiler has no corresponding C++ arithmetic type.
template<typename T>
constexpr bool signedness_kind() {
    return std::numeric_limits<T>::is_signed;
}

// void / bool.

inline constexpr auto v0 = skl::abix::model::canonical_type_id("prim:void");
inline constexpr auto b8 = skl::abix::model::canonical_type_id("prim:bool");

// Signed and unsigned integers. `isize` / `usize` follow the host pointer
// width, matching the target data model used by AMC.

inline constexpr auto i8 = skl::abix::model::canonical_type_id("prim:i8");
inline constexpr auto i16 = skl::abix::model::canonical_type_id("prim:i16");
inline constexpr auto i32 = skl::abix::model::canonical_type_id("prim:i32");
inline constexpr auto i64 = skl::abix::model::canonical_type_id("prim:i64");
inline constexpr auto i128 = skl::abix::model::canonical_type_id("prim:i128");
inline constexpr auto isize = sizeof(void *) == 4 ? i32 : i64;

inline constexpr auto u8 = skl::abix::model::canonical_type_id("prim:u8");
inline constexpr auto u16 = skl::abix::model::canonical_type_id("prim:u16");
inline constexpr auto u32 = skl::abix::model::canonical_type_id("prim:u32");
inline constexpr auto u64 = skl::abix::model::canonical_type_id("prim:u64");
inline constexpr auto u128 = skl::abix::model::canonical_type_id("prim:u128");
inline constexpr auto usize = sizeof(void *) == 4 ? u32 : u64;

// `wint_t` has no independent ABI identity: it is a typedef to an integer with
// the same width and signedness. Resolve it to that integer's descriptor.
constexpr TypeId integer_type_id(size_t size, bool is_signed) noexcept {
    switch (size) {
        case 1:  return is_signed ? i8 : u8;
        case 2:  return is_signed ? i16 : u16;
        case 4:  return is_signed ? i32 : u32;
        case 8:  return is_signed ? i64 : u64;
        default: return is_signed ? i32 : u32;
    }
}

inline constexpr auto wint = integer_type_id(sizeof(wint_t), signedness_kind<wint_t>());

// Floating-point formats. The key is the format, never the C/C++ spelling.

inline constexpr auto f32 = skl::abix::model::canonical_type_id("prim:fieee32");
inline constexpr auto f64 = skl::abix::model::canonical_type_id("prim:fieee64");
inline constexpr auto f128 = skl::abix::model::canonical_type_id("prim:fieee128");

inline constexpr auto f16 = skl::abix::model::canonical_type_id("prim:fieee16");
inline constexpr auto bf16 = skl::abix::model::canonical_type_id("prim:fbf16");

inline constexpr auto f128x87 = skl::abix::model::canonical_type_id("prim:fx87_80");
inline constexpr auto f128ppc = skl::abix::model::canonical_type_id("prim:fppc_double_double");

// The C/C++ `long double` format. LDBL_MANT_DIG distinguishes every layout the
// ABIX frontends encode: binary64 (53), x87 80-bit (64), PowerPC double-double
// (106) and IEEE binary128 (113).
#if LDBL_MANT_DIG == 53
inline constexpr auto ldouble = f64;
#elif LDBL_MANT_DIG == 64
inline constexpr auto ldouble = f128x87;
#elif LDBL_MANT_DIG == 106
inline constexpr auto ldouble = f128ppc;
#elif LDBL_MANT_DIG == 113
inline constexpr auto ldouble = f128;
#else
#  warning "ABIX: Unsupported long double ABI"
#endif

// Character types. Plain `char` and `wchar_t` carry their target-defined
// signedness; the other character kinds are distinct ABI kinds.
inline constexpr auto nchar =
    skl::abix::model::canonical_type_id(signedness_kind<char>() ? "prim:char_s" : "prim:char_u");
inline constexpr auto schar = skl::abix::model::canonical_type_id("prim:schar");
inline constexpr auto uchar = skl::abix::model::canonical_type_id("prim:uchar");
inline constexpr auto wchar =
    skl::abix::model::canonical_type_id(signedness_kind<wchar_t>() ? "prim:wchar_s" : "prim:wchar_u");

inline constexpr auto c8 = skl::abix::model::canonical_type_id("prim:char8");
inline constexpr auto c16 = skl::abix::model::canonical_type_id("prim:char16");
inline constexpr auto c32 = skl::abix::model::canonical_type_id("prim:char32");
}   // namespace skl::abix::model

#endif   // SKL_ABIX_ABI_MODEL_H
