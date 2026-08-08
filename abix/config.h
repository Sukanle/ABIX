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
#ifndef SKL_ABIX_CONFIG_H
#define SKL_ABIX_CONFIG_H
#if defined(_WIN32) || defined(__CYGWIN__)
#  define SKL_ABIX_WINDOWS 1
#else
#  define SKL_ABIX_WINDOWS 0
#endif
#if SKL_ABIX_WINDOWS
#  define SKL_ABIX_CALL_CDECL __cdecl
#  define SKL_ABIX_CALL_STDCALL __stdcall
#else
#  define SKL_ABIX_CALL_CDECL
#  define SKL_ABIX_CALL_STDCALL
#endif

#if SKL_ABIX_WINDOWS
#  define SKL_ABIX_DLL_EXPORT __declspec(dllexport)
#else
#  define SKL_ABIX_DLL_EXPORT __attribute__((visibility("default")))
#endif

#define SKL_ABIX_NAMESPACE_BEGIN \
    namespace skl {              \
    namespace abix {
#define SKL_ABIX_NAMESPACE_END \
    }                          \
    }

#ifndef SKL_NO_DEPRECATED
#  define SKL_DEPRECATED(msg) [[deprecated(msg)]]
#endif

#define SKL_ABIX_MAGIC64 0XFDFDFDFDFDFDFDFDULL   // FD -> Failed

#include <stdint.h>

SKL_ABIX_NAMESPACE_BEGIN

enum class AbiLookupPolicy : uint8_t {
    Linear = 0,
    StaticHot = 1,
    AdaptiveHot = 2,
};

SKL_ABIX_NAMESPACE_END

#endif