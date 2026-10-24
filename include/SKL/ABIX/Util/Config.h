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
#ifndef SKL_ABIX_UTIL_CONFIG_H
#define SKL_ABIX_UTIL_CONFIG_H

#if defined(_WIN32) || defined(__CYGWIN__)
#  define SKL_ABIX_WINDOWS
#  define SKL_ABIX_DLL_SUFFIX ".dll"
#  define SKL_ABIX_PATHSEPARATOR '\\'
#elif defined(__APPLE__)
#  define SKL_ABIX_APPLE
#  define SKL_ABIX_DLL_SUFFIX ".dylib"
#  define SKL_ABIX_PATHSEPARATOR '/'
#else
#  define SKL_ABIX_DLL_SUFFIX ".so"
#  define SKL_ABIX_PATHSEPARATOR '/'
#endif

#if defined(SKL_ABIX_WINDOWS)
#  define SKL_ABIX_CALL_CDECL __cdecl
#  define SKL_ABIX_CALL_STDCALL __stdcall
#else
#  define SKL_ABIX_CALL_CDECL
#  define SKL_ABIX_CALL_STDCALL
#endif

// ABIX's runtime is header-only: there is no ABIX shared library whose public
// symbols need import/export decoration. What does need an export annotation is
// the provider side of the module ABI: the fixed C entry points
// (`abi_get_table`, `abix_module_query_v1`) and provider handles that a consumer
// resolves through `dlsym` / `GetProcAddress`.
//
// ABIX uses one macro for those definitions. On Windows every use in this
// codebase is a definition inside a provider DLL, so `dllexport` is
// unconditional; consumers never link these symbols directly. On ELF/Mach-O the
// attribute keeps the entry visible even when the provider builds with
// `-fvisibility=hidden` (as the plugin targets do).
#if defined(SKL_ABIX_WINDOWS)
#  define SKL_ABIX_DLL_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#  define SKL_ABIX_DLL_EXPORT __attribute__((visibility("default")))
#else
#  define SKL_ABIX_DLL_EXPORT
#endif

#ifndef SKL_NO_DEPRECATED
#  define SKL_DEPRECATED(msg) [[deprecated(msg)]]
#endif

#define SKL_ABIX_MAGIC64 0XFDFDFDFDFDFDFDFDULL   // FD -> Failed

#include <stdint.h>   // IWYU pragma: keep

// #define ABIX_DISABLE_LOGGING               // Completely disable all logging (zero overhead)
// #define ABIX_DISABLE_LOG_LEVEL_DEBUG      // Disable Debug-level logs
// #define ABIX_DISABLE_LOG_LEVEL_INFO       // Disable Info-level logs
// #define ABIX_DISABLE_LOG_LEVEL_WARNING    // Disable Warning-level logs
// #define ABIX_DISABLE_LOG_LEVEL_ERROR      // Disable Error-level logs

#endif
