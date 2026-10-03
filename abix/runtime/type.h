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
#ifndef SKL_ABIX_RUNTIME_TYPE_H
#define SKL_ABIX_RUNTIME_TYPE_H

#include <stdint.h>

#include "mics/utils/hash.h"

namespace skl::abix::runtime {

using sig_t = mics::utils::hash64_t;
using name_hash_t = mics::utils::hash32_t;
using version_t = uint64_t;
using index_t = uint32_t;

#define SKL_ABIX_TABLE_MAGIC 0XAB1E7A81U
#define SKL_ABIX_TABLE_FORMAT_VERSION 0X1U

#define SKL_ABIX_ENTRY_HOT 0X1U

struct Entry {
    const char *name;
    sig_t sig;
    version_t version;
    uintptr_t fnptr;
    name_hash_t name_hash;
    uint32_t flags;
};
struct Table {
    uint32_t count;
    uint32_t magic;
    uint32_t format_version;
    uint32_t reserved;
    const Entry *entries;
};

template<size_t N>
inline const Table *make_table(const Entry (&arr)[N]) noexcept {
    static const Table t{static_cast<uint32_t>(N), SKL_ABIX_TABLE_MAGIC, SKL_ABIX_TABLE_FORMAT_VERSION, 0U, arr};
    return &t;
}

#define SKL_ABIX_DEFINE_TABLE(...)                                                          \
    static const ::skl::abix::runtime::Entry _abi_entries[] = {__VA_ARGS__};                \
    extern "C" SKL_ABIX_DLL_EXPORT const ::skl::abix::runtime::Table *abi_get_table(void) { \
        return skl::abix::runtime::make_table(_abi_entries);                                \
    }

}   // namespace skl::abix::runtime

#endif
