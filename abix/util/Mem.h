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
#ifndef SKL_ABIX_UTIL_MEM_H
#define SKL_ABIX_UTIL_MEM_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ABIX/Util/Config.h"

#ifdef SKL_ABIX_WINDOWS
#  include <heapapi.h>
#  include <malloc.h>
#else
#  include <stdlib.h>
#endif

namespace skl::abix::mem {

inline void *alloc(size_t n) noexcept {
#ifdef SKL_ABIX_WINDOWS
    return HeapAlloc(GetProcessHeap(), 0, n);
#else
    return ::malloc(n);
#endif
}

inline void *zalloc(size_t n) noexcept {
    void *p = ::calloc(1, n);
    return p;
}

inline void *alloc_array(size_t count, size_t elem) noexcept {
    if (elem != 0 && count > SIZE_MAX / elem) return nullptr;
    return alloc(count * elem);
}

inline void *alloc_aligned(size_t n, size_t align) noexcept {
#ifdef SKL_ABIX_WINDOWS
    return _aligned_malloc(n, align);
#else
    void *p = nullptr;
    if (::posix_memalign(&p, align, n) != 0) return nullptr;
    return p;
#endif
}

inline void dealloc(void *p) noexcept {
    if (!p) return;
#ifdef SKL_ABIX_WINDOWS
    HeapFree(GetProcessHeap(), 0, p);
#else
    ::free(p);
#endif
}

inline void dealloc_aligned(void *p) noexcept {
    if (!p) return;
#ifdef SKL_ABIX_WINDOWS
    _aligned_free(p);
#else
    ::free(p);
#endif
}

}   // namespace skl::abix::mem

#endif   // SKL_ABIX_UTIL_MEM_H
