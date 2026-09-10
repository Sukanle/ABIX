/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */
#ifndef SKL_ABIX_COMPATIBILITY_H
#define SKL_ABIX_COMPATIBILITY_H

#include <stddef.h>
#include <stdint.h>

#include "abi_model.h"
#include "hash.h"

namespace skl::abix::compatibility {

enum class Result : uint32_t {
    identical = 0,
    layout_compatible,
    map_compatible,
    incompatible,
};

constexpr model::Hash128 combine(model::Hash128 value,
                                 uint64_t part) noexcept {
    value.lo ^= part + 0x9e3779b97f4a7c15ULL + (value.lo << 6) +
                (value.lo >> 2);
    value.hi ^= (part ^ 0xd6e8feb86659fd93ULL) + (value.hi << 7) +
                (value.hi >> 3);
    return value;
}

constexpr model::Hash128 type_hash(const uint8_t *canonical, size_t size) noexcept {
    return hash::fnv1a(canonical, size, hash::Domain::type_identity).digest;
}

constexpr model::Hash128 layout_hash(model::TypeId type_id,
                                     const model::TypeLayout &layout,
                                     const model::Field *fields = nullptr) noexcept {
    model::Hash128 result = {0x6c61796f75745f31ULL, 0x6c61796f75745f32ULL};
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

constexpr model::Hash128 signature_hash(model::TypeId return_type,
                                         const model::TypeId *parameters,
                                         size_t parameter_count,
                                         uint32_t calling_convention,
                                         uint32_t qualifiers = 0) noexcept {
    model::Hash128 result = {0x7369676e61747572ULL, 0x7369676e61747572ULL};
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

constexpr bool same_hash(model::Hash128 lhs, model::Hash128 rhs) noexcept {
    return lhs.lo == rhs.lo && lhs.hi == rhs.hi;
}

constexpr Result compare(model::TypeId source_id,
                         const model::TypeLayout &source,
                         model::TypeId target_id,
                         const model::TypeLayout &target,
                         bool has_map = false) noexcept {
    if (same_hash(source_id, target_id) &&
        same_hash(source.layout_hash, target.layout_hash))
        return Result::identical;
    if (same_hash(source.layout_hash, target.layout_hash))
        return Result::layout_compatible;
    return has_map ? Result::map_compatible : Result::incompatible;
}

} // namespace skl::abix::compatibility

#endif // SKL_ABIX_COMPATIBILITY_H
