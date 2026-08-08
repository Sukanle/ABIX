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
#ifndef SKL_ABIX_CACHE_H
#define SKL_ABIX_CACHE_H

#include "search.h"
#include "register.h"

SKL_ABIX_NAMESPACE_BEGIN
#ifndef SKL_ABIX_HOT_SLOTS
#  define SKL_ABIX_HOT_SLOTS 32
#endif
constexpr uint32_t SKL_ABIX_ADAPTIVE_PERIOD = 64;
constexpr uint32_t SKL_ABIX_ADAPTIVE_THRESHOLD = 4;

struct static_hot_cache {
    index_t slots[SKL_ABIX_HOT_SLOTS];
    uint32_t count;
    constexpr static_hot_cache()
        : slots{}
        , count(0) {}
    bool add(index_t idx) noexcept {
        if (contains(idx)) return true;
        if (count >= SKL_ABIX_HOT_SLOTS) return false;
        slots[count++] = idx;
        return true;
    }
    bool contains(index_t idx) const noexcept {
        for (uint32_t i = 0; i < count; ++i)
            if (slots[i] == idx) return true;
        return false;
    }
    void reset() noexcept { count = 0; }
};

struct adaptive_hot_cache {
    index_t slots[SKL_ABIX_HOT_SLOTS];
    uint32_t count = 0;
    uint32_t sample_counter = 0;
    uint32_t *hits = nullptr;
    uint32_t table_count = 0;
    uint32_t threshold = SKL_ABIX_ADAPTIVE_THRESHOLD;
    uint32_t period = SKL_ABIX_ADAPTIVE_PERIOD;

    void init(uint32_t n) noexcept {
        destroy();
        if (n == 0) return;
        hits = static_cast<uint32_t *>(abi_alloc(n * sizeof(uint32_t)));
        if (hits) memset(hits, 0, n * sizeof(uint32_t));
        table_count = n;
    }
    void destroy() noexcept {
        if (hits) {
            abi_free(hits);
            hits = nullptr;
        }
        count = 0;
        sample_counter = 0;
        table_count = 0;
    }
    ~adaptive_hot_cache() { destroy(); }
    bool contains(index_t idx) const noexcept {
        for (uint32_t i = 0; i < count; ++i)
            if (slots[i] == idx) return true;
        return false;
    }
    void promote(index_t idx) noexcept {
        if (contains(idx) || count >= SKL_ABIX_HOT_SLOTS) return;
        slots[count++] = idx;
    }
};

inline const entry *lookup_static_hot(
    const table &t, const static_hot_cache &c, const char *name, name_hash_t nh, sig_t sig) noexcept {
    for (uint32_t i = 0; i < c.count; ++i) {
        const entry &e = t.entries[c.slots[i]];
        if (e.name_hash == nh && e.sig == sig && strcmp(e.name, name) == 0) return &e;
    }
    return lookup_linear(t, name, nh, sig);
}

inline const entry *lookup_adaptive(
    const table &t, adaptive_hot_cache &c, const char *name, name_hash_t nh, sig_t sig) noexcept {
    for (uint32_t i = 0; i < c.count; ++i) {
        const entry &e = t.entries[c.slots[i]];
        if (e.name_hash == nh && e.sig == sig && strcmp(e.name, name) == 0) return &e;
    }
    index_t found = ~index_t{0};
    if (t.magic == SKL_ABIX_TABLE_MAGIC && name) {
        for (index_t i = 0; i < t.count; ++i) {
            const entry &e = t.entries[i];
            if (e.name_hash == nh && e.sig == sig && strcmp(e.name, name) == 0) {
                found = i;
                break;
            }
        }
    }
    if (found == ~index_t{0}) return nullptr;
    if (c.hits && c.table_count == t.count) {
        if (++c.sample_counter >= c.period) {
            c.sample_counter = 0;
            if (++c.hits[found] > c.threshold) c.promote(static_cast<index_t>(found));
        }
    }
    return &t.entries[found];
}
SKL_ABIX_NAMESPACE_END
#endif
