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
#ifndef SKL_ABIX_DLL_EXPORT_H
#define SKL_ABIX_DLL_EXPORT_H

#include <stddef.h>
#include <stdlib.h>

#include <new>   // IWYU pragma: keep

#include "ABIX/Util/Config.h"
#include "mics/utils/hash.h"

namespace skl::abix {

namespace cc {
using tag = mics::utils::cc::tag;
}

#define SKL_ABIX_CCPICK_Cdecl ::skl::abix::cc::tag::Cdecl
#define SKL_ABIX_CCPICK_Stdcall ::skl::abix::cc::tag::Stdcall
#define SKL_ABIX_CCPICK2(C) SKL_ABIX_CCPICK_##C
#define SKL_ABIX_CCPICK(C) SKL_ABIX_CCPICK2(C)

#define SKL_ABIX_ENTRY_FULL(name, func, cctype, ver, flg)                             \
    {name, ::skl::abix::dll::FnSig<decltype(&func), SKL_ABIX_CCPICK(cctype)>::value, ver, \
        reinterpret_cast<uintptr_t>(&func), mics::utils::cstr32(name), flg}
#define SKL_ABIX_ENTRY_CC(name, func, cctype) SKL_ABIX_ENTRY_FULL(name, func, cctype, 0, 0)
#define SKL_ABIX_ENTRY(name, func) SKL_ABIX_ENTRY_CC(name, func, Cdecl)
#define SKL_ABIX_VERSION(v) mics::utils::version(v)

#define SKL_ABIX_TABLE_GETTER "abi_get_table"

}   // namespace skl::abix

#endif
