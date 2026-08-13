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

// #define ABIX_DISABLE_LOGGING               // Completely disable all logging (zero overhead)
// #define ABIX_DISABLE_LOG_LEVEL_DEBUG      // Disable Debug-level logs
// #define ABIX_DISABLE_LOG_LEVEL_INFO       // Disable Info-level logs
// #define ABIX_DISABLE_LOG_LEVEL_WARNING    // Disable Warning-level logs
// #define ABIX_DISABLE_LOG_LEVEL_ERROR      // Disable Error-level logs

#ifndef ABIX_RCU_TIMEOUT_ENABLE
#  define ABIX_RCU_TIMEOUT_ENABLE 1   // Timeout master switch (default: on)
#endif

#ifndef ABIX_RCU_TIMEOUT_MS
#  define ABIX_RCU_TIMEOUT_MS 5'000   // Compile-time fallback default (ms)
#endif

#ifndef ABIX_RCU_TIMEOUT_FRAMES_DEFAULT
#  define ABIX_RCU_TIMEOUT_FRAMES_DEFAULT 0   // Compile-time fallback default (frames); 0 = disabled
#endif

// Strategy C safety lock (must be explicitly defined to enable)
// #define ABIX_ENABLE_FORCE_LEAK_POLICY

// Prevents the lazy timeout check from going stale when no new readers arrive.
// Level 0: pure lazy, zero overhead, accept starvation risk
// Level 1: lazy + abix::tick() passive injection (recommended default)
// Level 2: lazy + idle background thread (requires ABIX_ENABLE_IDLE_BACKGROUND_THREAD)
#define ABIX_LAZY_STARVATION_GUARD_OFF 0
#define ABIX_LAZY_STARVATION_GUARD_TICK 1
#define ABIX_LAZY_STARVATION_GUARD_IDLE 2

#ifndef ABIX_LAZY_STARVATION_GUARD
#  define ABIX_LAZY_STARVATION_GUARD ABIX_LAZY_STARVATION_GUARD_TICK
#endif

#endif