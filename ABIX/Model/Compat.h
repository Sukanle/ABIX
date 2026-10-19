/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
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

// Distinct semantic digests, one canonical engine. Type identity lives in
// `canonical_type_id` (ABI.h); these functions cover the two meanings that must
// stay separate from it:
//
//   * `structural_layout_hash` — physical layout compatibility. The same TypeID can
//                              have different layouts on different targets.
//   * `abi_signature_hash`   — call ABI (return/parameter types plus calling
//                              convention/qualifiers). This is not method
//                              identity: `poly::method_signature` is the logical
//                              method identity and does not carry the calling
//                              convention.
constexpr uint64_t layout_identity_domain = 0x4C41594F55545F31ULL;   // "LAYOUT_1"
constexpr uint64_t abi_signature_domain = 0x4142495349475F31ULL;     // "ABISIG_1"

constexpr Hash128 structural_layout_hash(
    model::TypeId type_id, const model::TypeLayout &layout, const model::Field *fields = nullptr) noexcept {
    util::CanonicalState state = util::canonical_hash_begin(layout_identity_domain);
    state = util::canonical_hash_u64_be(state, type_id.lo);
    state = util::canonical_hash_u64_be(state, type_id.hi);
    state = util::canonical_hash_u32_be(state, layout.size);
    state = util::canonical_hash_u32_be(state, layout.align);
    state = util::canonical_hash_u32_be(state, layout.field_count);
    for (uint32_t i = 0; fields && i < layout.field_count; ++i) {
        state = util::canonical_hash_u32_be(state, fields[i].name_offset);
        state = util::canonical_hash_u64_be(state, fields[i].type_id.lo);
        state = util::canonical_hash_u64_be(state, fields[i].type_id.hi);
        state = util::canonical_hash_u32_be(state, fields[i].offset);
        state = util::canonical_hash_u32_be(state, fields[i].flags);
        state = util::canonical_hash_u32_be(state, fields[i].bit_offset);
        state = util::canonical_hash_u32_be(state, fields[i].bit_width);
    }
    return Hash128{state.lo, state.hi};
}

constexpr Hash128 abi_signature_hash(model::TypeId return_type, const model::TypeId *parameters, size_t parameter_count,
    uint32_t calling_convention, uint32_t qualifiers = 0) noexcept {
    util::CanonicalState state = util::canonical_hash_begin(abi_signature_domain);
    state = util::canonical_hash_u64_be(state, return_type.lo);
    state = util::canonical_hash_u64_be(state, return_type.hi);
    state = util::canonical_hash_u64_be(state, static_cast<uint64_t>(parameter_count));
    for (size_t i = 0; parameters && i < parameter_count; ++i) {
        state = util::canonical_hash_u64_be(state, parameters[i].lo);
        state = util::canonical_hash_u64_be(state, parameters[i].hi);
    }
    state = util::canonical_hash_u32_be(state, calling_convention);
    state = util::canonical_hash_u32_be(state, qualifiers);
    return Hash128{state.lo, state.hi};
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
