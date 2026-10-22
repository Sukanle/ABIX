/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */
#ifndef SKL_ABIX_UTIL_HASH_H
#define SKL_ABIX_UTIL_HASH_H

#include <stddef.h>
#include <stdint.h>

namespace skl::abix {

// Hash values are data-model values. Identity is defined by the canonical input
// and the semantic API, not by this representation alone.
struct Hash128 {
    uint64_t lo;
    uint64_t hi;
};

constexpr bool operator==(Hash128 lhs, Hash128 rhs) noexcept { return lhs.lo == rhs.lo && lhs.hi == rhs.hi; }
constexpr bool operator!=(Hash128 lhs, Hash128 rhs) noexcept { return !(lhs == rhs); }

static_assert(sizeof(Hash128) == 16, "Hash128 must have a fixed wire size");

using Hash64 = uint64_t;

namespace util {
// FNV-1a 64 constants. `fnv1a_offset_basis` is the canonical `lo` seed shared
// with AMC's `hash_text`; the `hi` lane uses the FNV prime + domain and XXH64's
// PRIME64_2 as its multiplier.
constexpr uint64_t fnv1a_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr uint64_t fnv1a_prime = 1'099'511'628'211ULL;
constexpr uint64_t canonical_hi_prime = 14'029'467'366'897'019'727ULL;

// The canonical hash engine. Every identity and digest in ABIX feeds the same
// state machine so that the same canonical bytes always produce the same
// Hash128. Semantic APIs (`canonical_type_id`, `method_signature`,
// `interface_id`, `layout_hash`, `abi_signature_hash`) differ in their domain
// and canonical input, never in the algorithm.
struct CanonicalState {
    uint64_t lo;
    uint64_t hi;
};

constexpr CanonicalState canonical_hash_begin(uint64_t domain) noexcept {
    return {fnv1a_offset_basis ^ domain, fnv1a_prime + domain};
}

constexpr CanonicalState canonical_hash_byte(CanonicalState state, uint8_t byte) noexcept {
    state.lo = (state.lo ^ byte) * fnv1a_prime;
    state.hi = (state.hi ^ (byte + 31)) * canonical_hi_prime;
    return state;
}

constexpr CanonicalState canonical_hash_bytes(CanonicalState state, const char *data, size_t size) noexcept {
    for (size_t i = 0; i < size; ++i)
        state = canonical_hash_byte(state, static_cast<uint8_t>(data[i]));
    return state;
}

// Fixed-width big-endian integer encoding. Used by structural digests so the
// canonical input never depends on host endianness or on decimal formatting.
constexpr CanonicalState canonical_hash_u32_be(CanonicalState state, uint32_t value) noexcept {
    for (int shift = 24; shift >= 0; shift -= 8)
        state = canonical_hash_byte(state, static_cast<uint8_t>(value >> shift));
    return state;
}

constexpr CanonicalState canonical_hash_u64_be(CanonicalState state, uint64_t value) noexcept {
    for (int shift = 56; shift >= 0; shift -= 8)
        state = canonical_hash_byte(state, static_cast<uint8_t>(value >> shift));
    return state;
}

constexpr Hash128 canonical_hash(const char *data, size_t size, uint64_t domain) noexcept {
    const CanonicalState state = canonical_hash_bytes(canonical_hash_begin(domain), data, size);
    return {state.lo, state.hi};
}
}   // namespace util

}   // namespace skl::abix

#endif   // SKL_ABIX_UTIL_HASH_H
