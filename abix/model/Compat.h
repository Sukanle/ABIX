/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */
#ifndef SKL_ABIX_MODEL_COMPAT_H
#define SKL_ABIX_MODEL_COMPAT_H

#include <stddef.h>
#include <stdint.h>

#include "ABIX/Util/Hash.h"
#include "ABIX/Model/ABI.h"

namespace skl::abix::model {

enum class CompatResult : uint32_t {
    identical = 0,
    layout_compatible,
    map_compatible,
    incompatible,
};

constexpr Hash128 combine(Hash128 value, uint64_t part) noexcept {
    value.lo ^= part + 0X9E3779B97F4A7C15ULL + (value.lo << 6) + (value.lo >> 2);
    value.hi ^= (part ^ 0XD6E8FEB86659FD93ULL) + (value.hi << 7) + (value.hi >> 3);
    return value;
}

constexpr Hash128 type_hash(const uint8_t *canonical, size_t size) noexcept {
    return util::fnv1a(canonical, size, util::Domain::type_identity).digest;
}

constexpr Hash128 layout_hash(
    model::TypeId type_id, const model::TypeLayout &layout, const model::Field *fields = nullptr) noexcept {
    Hash128 result = {0x6c61796f75745f31ULL, 0x6c61796f75745f32ULL};
    result = combine(result, type_id.lo);
    result = combine(result, type_id.hi);
    result = combine(result, layout.size);
    result = combine(result, layout.align);
    result = combine(result, layout.field_count);
    for (uint32_t i = 0; fields && i < layout.field_count; ++i) {
        result = combine(result, fields[i].name_offset);
        result = combine(result, fields[i].type_id.lo);
        result = combine(result, fields[i].type_id.hi);
        result = combine(result, fields[i].offset);
        result = combine(result, fields[i].flags);
        result = combine(result, fields[i].bit_offset);
        result = combine(result, fields[i].bit_width);
    }
    return result;
}

constexpr Hash128 signature_hash(model::TypeId return_type, const model::TypeId *parameters, size_t parameter_count,
    uint32_t calling_convention, uint32_t qualifiers = 0) noexcept {
    Hash128 result = {0X7369676E61747572ULL, 0X7369676E61747572ULL};
    result = combine(result, return_type.lo);
    result = combine(result, return_type.hi);
    result = combine(result, parameter_count);
    for (size_t i = 0; parameters && i < parameter_count; ++i) {
        result = combine(result, parameters[i].lo);
        result = combine(result, parameters[i].hi);
    }
    result = combine(result, calling_convention);
    return combine(result, qualifiers);
}

constexpr bool same_hash(Hash128 lhs, Hash128 rhs) noexcept { return lhs.lo == rhs.lo && lhs.hi == rhs.hi; }

constexpr CompatResult compare(model::TypeId source_id, const model::TypeLayout &source, model::TypeId target_id,
    const model::TypeLayout &target, bool has_map = false) noexcept {
    if (same_hash(source_id, target_id) && same_hash(source.layout_hash, target.layout_hash))
        return CompatResult::identical;
    if (same_hash(source.layout_hash, target.layout_hash)) return CompatResult::layout_compatible;
    return has_map ? CompatResult::map_compatible : CompatResult::incompatible;
}

}   // namespace skl::abix::model

#endif   // SKL_ABIX_MODEL_COMPAT_H
