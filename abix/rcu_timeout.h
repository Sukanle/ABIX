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
#ifndef SKL_ABIX_RCU_TIMEOUT_H
#define SKL_ABIX_RCU_TIMEOUT_H

#include "config.h"
#if SKL_ABIX_WINDOWS
#  include <windows.h>
#else
#  include <time.h>
#endif

#if ABIX_LAZY_STARVATION_GUARD >= ABIX_LAZY_STARVATION_GUARD_IDLE && defined(ABIX_ENABLE_IDLE_BACKGROUND_THREAD)
#  include <thread>
#  include <atomic>
#endif

SKL_ABIX_NAMESPACE_BEGIN

namespace detail {

// --- wall-clock time (always available) ---
inline uint64_t get_tick_ms() noexcept {
#if SKL_ABIX_WINDOWS
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1'000 + (uint64_t)ts.tv_nsec / 1'000'000;
#endif
}

// --- frame counter (always available, host injects via abix::tick()) ---
inline uint64_t &g_tick_counter() noexcept {
    static uint64_t counter = 0;
    return counter;
}
inline uint64_t get_tick_frames() noexcept { return g_tick_counter(); }

// ==================== Lazy Starvation Guard ====================
#if ABIX_LAZY_STARVATION_GUARD >= ABIX_LAZY_STARVATION_GUARD_TICK
inline uint64_t &g_last_check_time() noexcept {
    static uint64_t t = 0;
    return t;
}

#  if ABIX_LAZY_STARVATION_GUARD >= ABIX_LAZY_STARVATION_GUARD_IDLE
#    if defined(ABIX_ENABLE_IDLE_BACKGROUND_THREAD)
inline std::atomic<bool> &g_need_check() noexcept {
    static std::atomic<bool> flag{false};
    return flag;
}
inline void ensure_idle_thread() noexcept {
    static std::thread t([]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            g_need_check().store(true, std::memory_order_release);
        }
    });
    (void)t;
}
#    endif
#  endif

inline void try_passive_check() noexcept {
    uint64_t now = get_tick_ms();
    uint64_t last = g_last_check_time();
    if (now - last > 30'000) {
        g_last_check_time() = now;
    }
#  if ABIX_LAZY_STARVATION_GUARD >= ABIX_LAZY_STARVATION_GUARD_IDLE
#    if defined(ABIX_ENABLE_IDLE_BACKGROUND_THREAD)
    if (g_need_check().load(std::memory_order_acquire)) {
        g_need_check().store(false, std::memory_order_release);
        g_last_check_time() = now;
    }
#    endif
#  endif
}
#endif   // ABIX_LAZY_STARVATION_GUARD

}   // namespace detail

inline void tick(uint64_t timestamp) noexcept {
    detail::g_tick_counter() = timestamp;
#if ABIX_LAZY_STARVATION_GUARD >= ABIX_LAZY_STARVATION_GUARD_TICK
    detail::g_last_check_time() = timestamp;
#endif
}

SKL_ABIX_NAMESPACE_END

#endif   // SKL_ABIX_RCU_TIMEOUT_H