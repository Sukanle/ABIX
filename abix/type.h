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
#ifndef SKL_ABIX_TYPE_H
#define SKL_ABIX_TYPE_H

#include <stdint.h>

#include "config.h"
#include "Reflection/utils/hash.h"

SKL_ABIX_NAMESPACE_BEGIN

using sig_t = Reflect::Utils::hash64_t;
using name_hash_t = Reflect::Utils::hash32_t;
using version_t = uint64_t;
using index_t = uint32_t;

constexpr uint32_t SKL_ABIX_TABLE_MAGIC = 0xAB1E7A81U;
constexpr uint32_t SKL_ABIX_TABLE_FORMAT_VERSION = 1U;

constexpr uint32_t SKL_ABIX_ENTRY_HOT = 0x1ULL;

struct entry {
    const char *name;
    sig_t sig;
    version_t version;
    uintptr_t fnptr;
    name_hash_t name_hash;
    uint32_t flags;
};
struct table {
    uint32_t count;
    uint32_t magic;
    uint32_t format_version;
    uint32_t reserved;
    const entry *entries;
};

template<size_t N>
inline const table *make_table(const entry (&arr)[N]) noexcept {
    static const table t{static_cast<uint32_t>(N), SKL_ABIX_TABLE_MAGIC, SKL_ABIX_TABLE_FORMAT_VERSION, 0U, arr};
    return &t;
}
SKL_ABIX_NAMESPACE_END

#endif
