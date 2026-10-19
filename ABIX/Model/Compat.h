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
//   * `canonical_layout_hash` — physical layout compatibility (Canonical Layout
//                               Encoding v1). The same TypeID can have different
//                               layouts on different targets.
//   * `abi_signature_hash`    — call ABI (return/parameter types plus calling
//                               convention/qualifiers). This is not method
//                               identity: `poly::method_signature` is the logical
//                               method identity and does not carry the calling
//                               convention.
constexpr uint64_t abi_signature_domain = 0x4142495349475F31ULL;   // "ABISIG_1"

// Canonical Layout Encoding v1 (CLE v1) — the versioned, name-independent byte
// encoding shared by ABIX and AMC (ABI-SPEC §11.2). For the same target ABI and
// layout facts, both implementations must produce the same bytes and the same
// hash.
struct LayoutContext {
    uint32_t arch;
    uint32_t os;
    uint32_t abi;
    uint32_t compiler;
};

constexpr uint16_t canonical_layout_version = 1;
constexpr uint64_t canonical_layout_domain = 0x4C41594F55545631ULL;   // "LAYOUTV1"

// Only ABI-relevant field flags enter the CLE: base/bitfield markers and the
// packed bit offset/width. Visibility and other source-level bits are excluded.
constexpr uint32_t canonical_layout_flags(uint32_t flags) noexcept { return flags & 0xFFFF0003u; }

struct LayoutByteSink {
    uint8_t *out;
    size_t capacity;
    size_t size;
    constexpr bool put(uint8_t byte) noexcept {
        if (size >= capacity) return false;
        out[size++] = byte;
        return true;
    }
    constexpr bool put_u16(uint16_t value) noexcept {
        return put(static_cast<uint8_t>(value >> 8)) && put(static_cast<uint8_t>(value));
    }
    constexpr bool put_u32(uint32_t value) noexcept {
        return put(static_cast<uint8_t>(value >> 24))
            && put(static_cast<uint8_t>(value >> 16))
            && put(static_cast<uint8_t>(value >> 8))
            && put(static_cast<uint8_t>(value));
    }
    constexpr bool put_u64(uint64_t value) noexcept {
        return put(static_cast<uint8_t>(value >> 56))
            && put(static_cast<uint8_t>(value >> 48))
            && put(static_cast<uint8_t>(value >> 40))
            && put(static_cast<uint8_t>(value >> 32))
            && put(static_cast<uint8_t>(value >> 24))
            && put(static_cast<uint8_t>(value >> 16))
            && put(static_cast<uint8_t>(value >> 8))
            && put(static_cast<uint8_t>(value));
    }
    constexpr bool put_hash(Hash128 value) noexcept { return put_u64(value.lo) && put_u64(value.hi); }
};

struct LayoutHashSink {
    util::CanonicalState state;
    constexpr bool put(uint8_t byte) noexcept {
        state = util::canonical_hash_byte(state, byte);
        return true;
    }
    constexpr bool put_u16(uint16_t value) noexcept {
        return put(static_cast<uint8_t>(value >> 8)) && put(static_cast<uint8_t>(value));
    }
    constexpr bool put_u32(uint32_t value) noexcept {
        return put(static_cast<uint8_t>(value >> 24))
            && put(static_cast<uint8_t>(value >> 16))
            && put(static_cast<uint8_t>(value >> 8))
            && put(static_cast<uint8_t>(value));
    }
    constexpr bool put_u64(uint64_t value) noexcept {
        return put(static_cast<uint8_t>(value >> 56))
            && put(static_cast<uint8_t>(value >> 48))
            && put(static_cast<uint8_t>(value >> 40))
            && put(static_cast<uint8_t>(value >> 32))
            && put(static_cast<uint8_t>(value >> 24))
            && put(static_cast<uint8_t>(value >> 16))
            && put(static_cast<uint8_t>(value >> 8))
            && put(static_cast<uint8_t>(value));
    }
    constexpr bool put_hash(Hash128 value) noexcept { return put_u64(value.lo) && put_u64(value.hi); }
};

template<typename Sink>
constexpr bool visit_canonical_layout(const LayoutContext &context, model::TypeId type_id,
    const model::TypeLayout &layout, const model::Field *fields, Sink &sink) noexcept {
    constexpr char magic[] = "ABIX-CLE";
    for (size_t i = 0; i < sizeof(magic) - 1; ++i)
        if (!sink.put(static_cast<uint8_t>(magic[i]))) return false;
    if (!sink.put_u16(canonical_layout_version)) return false;
    if (!sink.put_u32(context.arch)) return false;
    if (!sink.put_u32(context.os)) return false;
    if (!sink.put_u32(context.abi)) return false;
    if (!sink.put_u32(context.compiler)) return false;
    if (!sink.put_hash(type_id)) return false;
    if (!sink.put_u32(layout.size)) return false;
    if (!sink.put_u32(layout.align)) return false;
    if (!sink.put_u32(layout.field_count)) return false;
    if (layout.field_count != 0 && fields == nullptr) return false;
    for (uint32_t i = 0; i < layout.field_count; ++i) {
        if (!sink.put_hash(fields[i].type_id)) return false;
        if (!sink.put_u32(fields[i].offset)) return false;
        if (!sink.put_u32(canonical_layout_flags(fields[i].flags))) return false;
    }
    return true;
}

// Encode CLE v1 into `out`. Returns the byte count, or 0 when the buffer is too
// small. Compare these bytes (not only the hash) when cross-checking AMC.
constexpr size_t encode_canonical_layout(const LayoutContext &context, model::TypeId type_id,
    const model::TypeLayout &layout, const model::Field *fields, uint8_t *out, size_t capacity) noexcept {
    LayoutByteSink sink{out, capacity, 0};
    return visit_canonical_layout(context, type_id, layout, fields, sink) ? sink.size : 0;
}

constexpr Hash128 canonical_layout_hash(const LayoutContext &context, model::TypeId type_id,
    const model::TypeLayout &layout, const model::Field *fields = nullptr) noexcept {
    LayoutHashSink sink{util::canonical_hash_begin(canonical_layout_domain)};
    if (!visit_canonical_layout(context, type_id, layout, fields, sink)) return {};
    return Hash128{sink.state.lo, sink.state.hi};
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
