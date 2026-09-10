/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */
#ifndef SKL_ABIX_ABI_MODEL_H
#define SKL_ABIX_ABI_MODEL_H

#include <stddef.h>
#include <stdint.h>

namespace skl::abix::model {

// Hash values are data-model values. The selected algorithm is recorded by
// the containing artifact rather than being implied by this representation.
struct Hash128 {
    uint64_t lo;
    uint64_t hi;
};

constexpr bool operator==(Hash128 lhs, Hash128 rhs) noexcept {
    return lhs.lo == rhs.lo && lhs.hi == rhs.hi;
}
constexpr bool operator!=(Hash128 lhs, Hash128 rhs) noexcept { return !(lhs == rhs); }

using Hash64 = uint64_t;
using SignatureHash = Hash128;
using ABIHash = Hash128;

using TypeId = Hash128;
using LayoutHash = Hash128;

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

struct Compatibility {
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

static_assert(sizeof(Hash128) == 16, "Hash128 must have a fixed wire size");
static_assert(sizeof(TypeLayout) == 32, "TypeLayout wire layout changed");
static_assert(sizeof(TypeDesc) == 32, "TypeDesc wire layout changed");
static_assert(sizeof(Field) == 40, "Field wire layout changed");
static_assert(sizeof(Function) == 16, "Function wire layout changed");
static_assert(sizeof(Symbol) == 32, "Symbol wire layout changed");

} // namespace skl::abix::model

#endif // SKL_ABIX_ABI_MODEL_H
