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
#ifndef SKL_ABIX_SEARCH_H
#define SKL_ABIX_SEARCH_H

#include <string.h>

#include "Reflection/utils/hash.h"

#include "type.h"

SKL_ABIX_NAMESPACE_BEGIN
enum class lookup_result : uint8_t {
    ok = 0,
    not_found = 1,
    sig_mismatch = 2,
    version_mismatch = 3,
    bad_table = 4,
};

inline lookup_result find_index(const table &t, const char *name, sig_t sig, version_t ver, index_t &out) noexcept {
    if (!name || t.magic != SKL_ABIX_TABLE_MAGIC) {
        out = ~index_t{0};
        return lookup_result::bad_table;
    }
    const name_hash_t nh = Reflect::Utils::cstr32(name);
    for (index_t i = 0; i < t.count; ++i) {
        const entry &e = t.entries[i];
        if (e.name_hash != nh) continue;
        if (strcmp(e.name, name) != 0) continue;
        if (ver != 0 && e.version != 0 && e.version != ver) continue;
        if (e.sig != sig) {
            out = i;
            return lookup_result::sig_mismatch;
        }
        out = i;
        return lookup_result::ok;
    }
    out = ~index_t{0};
    return lookup_result::not_found;
}

inline const entry *lookup_linear(const table &t, const char *name, name_hash_t nh, sig_t sig) noexcept {
    if (!name || t.magic != SKL_ABIX_TABLE_MAGIC) return nullptr;
    for (index_t i = 0; i < t.count; ++i) {
        const entry &e = t.entries[i];
        if (e.name_hash != nh) continue;
        if (e.sig != sig) continue;
        if (strcmp(e.name, name) == 0) return &e;
    }
    return nullptr;
}
SKL_ABIX_NAMESPACE_END
#endif
