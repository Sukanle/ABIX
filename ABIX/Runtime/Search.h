/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef SKL_ABIX_RUNTIME_SEARCH_H
#define SKL_ABIX_RUNTIME_SEARCH_H

#include <string.h>

#include <new>

#include "mics/utils/hash.h"

#include "ABIX/Runtime/Type.h"

namespace skl::abix::runtime {

enum class LookupResult : uint8_t {
    ok = 0,
    not_found = 1,
    sig_mismatch = 2,
    version_mismatch = 3,
    bad_Table = 4,
};

// ============================================================
// Hash index — runtime-only, open-addressing with linear probing
// ============================================================

#define SKL_ABIX_HASHSLOT_EMPTY ~skl::abix::runtime::index_t{0}
#define SKL_ABIX_HASH_THRESHOLD 64

struct HashSlot {
    name_hash_t hash;
    index_t index;
};

inline constexpr uint32_t next_pow2(uint32_t v) noexcept {
    if (v == 0) return 1;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

struct HashIndex {
    HashSlot *slots;
    uint32_t capacity;
    uint32_t mask;

    HashIndex() noexcept
        : slots(nullptr)
        , capacity(0)
        , mask(0) {}

    bool valid() const noexcept { return slots != nullptr; }

    void build(const Table &t) noexcept {
        capacity = next_pow2(t.count * 2);
        mask = capacity - 1;
        slots = new (std::nothrow) HashSlot[capacity];
        if (!slots) return;

        for (uint32_t i = 0; i < capacity; ++i) {
            slots[i].hash = 0;
            slots[i].index = SKL_ABIX_HASHSLOT_EMPTY;
        }

        for (index_t i = 0; i < t.count; ++i) {
            const Entry &e = t.entries[i];
            uint32_t pos = e.name_hash & mask;
            while (slots[pos].index != SKL_ABIX_HASHSLOT_EMPTY)
                pos = (pos + 1) & mask;
            slots[pos].hash = e.name_hash;
            slots[pos].index = i;
        }
    }

    void destroy() noexcept {
        if (slots) {
            delete[] slots;
            slots = nullptr;
        }
        capacity = 0;
        mask = 0;
    }
};

// ============================================================
// Linear scan (baseline)
// ============================================================

inline LookupResult find_linear(const Table &t, const char *name, sig_t sig, version_t ver, index_t &out) noexcept {
    if (!name || t.magic != SKL_ABIX_TABLE_MAGIC) {
        out = ~index_t{0};
        return LookupResult::bad_Table;
    }
    const name_hash_t nh = mics::utils::cstr32(name);
    for (index_t i = 0; i < t.count; ++i) {
        const Entry &e = t.entries[i];
        if (e.name_hash != nh) continue;
        if (strcmp(e.name, name) != 0) continue;
        if (ver != 0 && e.version != 0 && e.version != ver) continue;
        if (e.sig != sig) {
            out = i;
            return LookupResult::sig_mismatch;
        }
        out = i;
        return LookupResult::ok;
    }
    out = ~index_t{0};
    return LookupResult::not_found;
}

inline const Entry *lookup_linear(const Table &t, const char *name, name_hash_t nh, sig_t sig) noexcept {
    if (!name || t.magic != SKL_ABIX_TABLE_MAGIC) return nullptr;
    for (index_t i = 0; i < t.count; ++i) {
        const Entry &e = t.entries[i];
        if (e.name_hash != nh) continue;
        if (e.sig != sig) continue;
        if (strcmp(e.name, name) == 0) return &e;
    }
    return nullptr;
}

// ============================================================
// Hash index lookup
// ============================================================

inline LookupResult find_hash(
    const Table &t, const HashIndex &idx, const char *name, sig_t sig, version_t ver, index_t &out) noexcept {
    if (!name || t.magic != SKL_ABIX_TABLE_MAGIC || !idx.valid()) {
        out = ~index_t{0};
        return LookupResult::bad_Table;
    }
    const name_hash_t nh = mics::utils::cstr32(name);
    uint32_t pos = nh & idx.mask;
    while (idx.slots[pos].index != SKL_ABIX_HASHSLOT_EMPTY) {
        if (idx.slots[pos].hash == nh) {
            index_t i = idx.slots[pos].index;
            const Entry &e = t.entries[i];
            if (strcmp(e.name, name) != 0) {
                pos = (pos + 1) & idx.mask;
                continue;
            }
            if (ver != 0 && e.version != 0 && e.version != ver) {
                out = i;
                return LookupResult::version_mismatch;
            }
            if (e.sig != sig) {
                out = i;
                return LookupResult::sig_mismatch;
            }
            out = i;
            return LookupResult::ok;
        }
        pos = (pos + 1) & idx.mask;
    }
    out = ~index_t{0};
    return LookupResult::not_found;
}

inline const Entry *lookup_hash(
    const Table &t, const HashIndex &idx, const char *name, name_hash_t nh, sig_t sig) noexcept {
    if (!name || t.magic != SKL_ABIX_TABLE_MAGIC || !idx.valid()) return nullptr;
    uint32_t pos = nh & idx.mask;
    while (idx.slots[pos].index != SKL_ABIX_HASHSLOT_EMPTY) {
        if (idx.slots[pos].hash == nh) {
            const Entry &e = t.entries[idx.slots[pos].index];
            if (e.sig == sig && strcmp(e.name, name) == 0) return &e;
        }
        pos = (pos + 1) & idx.mask;
    }
    return nullptr;
}

// ============================================================
// Auto-select: Linear for small Tables, HashIndex for large Tables
// ============================================================

inline LookupResult find_index(
    const Table &t, const HashIndex &idx, const char *name, sig_t sig, version_t ver, index_t &out) noexcept {
    if (idx.valid()) return find_hash(t, idx, name, sig, ver, out);
    return find_linear(t, name, sig, ver, out);
}

}   // namespace skl::abix::runtime
#endif
