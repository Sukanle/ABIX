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
#ifndef SKL_ABIX_REGISTER_H
#define SKL_ABIX_REGISTER_H

#include <stddef.h>   // IWYU pragma: keep

#include <new>   // IWYU pragma: keep

#include "abix/config.h"   // IWYU pragma: keep

#ifdef SKL_ABIX_WINDOWS
#  include <heapapi.h>
#endif

SKL_ABIX_NAMESPACE_BEGIN

#define SKL_ABIX_CCPICK_Cdecl ::skl::abix::cc::tag::Cdecl
#define SKL_ABIX_CCPICK_Stdcall ::skl::abix::cc::tag::Stdcall
#define SKL_ABIX_CCPICK2(C) SKL_ABIX_CCPICK_##C
#define SKL_ABIX_CCPICK(C) SKL_ABIX_CCPICK2(C)

#define SKL_ABIX_ENTRY_FULL(name, func, cctype, ver, flg)                             \
    {name, ::skl::abix::fn_sig<decltype(&func), SKL_ABIX_CCPICK(cctype)>::value, ver, \
        reinterpret_cast<uintptr_t>(&func), Reflect::Utils::cstr32(name), flg}
#define SKL_ABIX_ENTRY_CC(name, func, cctype) SKL_ABIX_ENTRY_FULL(name, func, cctype, 0, 0)
#define SKL_ABIX_ENTRY(name, func) SKL_ABIX_ENTRY_CC(name, func, Cdecl)
#define SKL_ABIX_VERSION(v) Reflect::Utils::version(v)

#define SKL_ABIX_DEFINE_TABLE(...)                                                 \
    static const ::skl::abix::entry _abi_entries[] = {__VA_ARGS__};                \
    extern "C" SKL_ABIX_DLL_EXPORT const ::skl::abix::table *abi_get_table(void) { \
        return skl::abix::make_table(_abi_entries);                                \
    }

#define SKL_ABIX_TABLE_GETTER "abi_get_table"

inline void *abi_alloc(size_t n) noexcept {
#ifdef SKL_ABIX_WINDOWS
    return HeapAlloc(GetProcessHeap(), 0, n);
#else
    return malloc(n);
#endif
}
inline void abi_free(void *p) noexcept {
    if (!p) return;
#ifdef SKL_ABIX_WINDOWS
    HeapFree(GetProcessHeap(), 0, p);
#else
    free(p);
#endif
}
SKL_ABIX_NAMESPACE_END
#endif
