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
 *
 * An upper bound on how long a grace-period wait may take.
 *
 * `Deadline` says how long to wait, never what to do when the wait runs out: a
 * timeout means "the grace period was not proven", and acting on it is the
 * caller's decision (see `Domain::synchronize_until`). Callers that measured a
 * frame budget should convert it to milliseconds themselves; ABIX takes no frame
 * or wall-clock input from the host.
 */
#ifndef SKL_ABIX_RCU_DEADLINE_H
#define SKL_ABIX_RCU_DEADLINE_H

#include <stdint.h>

#include "SKL/ABIX/Util/Config.h"

#ifdef SKL_ABIX_WINDOWS
#  include <profileapi.h>
#else
#  include <time.h>
#endif

namespace skl::abix::rcu {

namespace detail {

#ifdef SKL_ABIX_WINDOWS
// QPC ticks per second. Fixed for the lifetime of the process, so it is queried
// once; a zero frequency is documented not to happen, but is mapped to 1 so a
// caller can never divide by zero.
inline uint64_t qpc_frequency() noexcept {
    static const uint64_t frequency = []() noexcept -> uint64_t {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return (f.QuadPart > 0) ? static_cast<uint64_t>(f.QuadPart) : 1ULL;
    }();
    return frequency;
}
#endif

// Monotonic clock in platform units: QPC ticks on Windows, nanoseconds elsewhere.
// Never goes backwards, never wraps in practice, and is only read by a bounded wait
// (every 256 polls), so it stays off the unrestricted path.
inline uint64_t monotonic_now() noexcept {
#ifdef SKL_ABIX_WINDOWS
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<uint64_t>(counter.QuadPart);
#else
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + static_cast<uint64_t>(ts.tv_nsec);
#endif
}

// The same clock in nanoseconds, for diagnostics that report durations. The
// distinction matters only on Windows, where QPC ticks are not nanoseconds; the
// split division keeps both intermediates inside 64 bits for any realistic uptime
// and frequency.
inline uint64_t monotonic_now_ns() noexcept {
#ifdef SKL_ABIX_WINDOWS
    const uint64_t ticks = monotonic_now();
    const uint64_t frequency = qpc_frequency();
    const uint64_t whole_seconds = ticks / frequency;
    const uint64_t remainder = ticks % frequency;
    return whole_seconds * 1'000'000'000ULL + remainder * 1'000'000'000ULL / frequency;
#else
    return monotonic_now();
#endif
}

// Converts milliseconds to the clock's units. The QPC frequency is fixed for the
// lifetime of the process, so it is queried once and kept.
inline uint64_t duration_units(uint64_t ms) noexcept {
#ifdef SKL_ABIX_WINDOWS
    // ticks per ms; the frequency is documented to be nonzero and fixed
    const uint64_t per_ms = qpc_frequency() / 1'000ULL;
    return ms * (per_ms != 0 ? per_ms : 1ULL);
#else
    return ms * 1'000'000ULL;
#endif
}

}   // namespace detail

class Deadline {
public:
    // No bound: wait for as long as the grace period takes. This is the behaviour
    // every entry point had before bounded waits existed.
    static constexpr Deadline never() noexcept { return Deadline{}; }

    // Expire `ms` milliseconds from now. `after_ms(0)` is already expired, which
    // makes it a non-blocking probe: it reports whether a grace period could be
    // proven without waiting.
    static Deadline after_ms(uint64_t ms) noexcept {
        if (ms == 0) return Deadline{1};   // 0 is the "never" sentinel
        return Deadline{detail::monotonic_now() + detail::duration_units(ms)};
    }

    // Whether this deadline is bounded at all (i.e. not `never()`).
    constexpr bool bounded() const noexcept { return _at != 0; }

    // A `never()` deadline is never expired; a bounded one is once the clock reaches
    // it. Reads the clock, so callers poll it rather than checking it in a hot loop.
    bool expired() const noexcept { return _at != 0 && detail::monotonic_now() >= _at; }

private:
    constexpr Deadline() noexcept
        : _at(0) {}
    explicit constexpr Deadline(uint64_t at) noexcept
        : _at(at) {}

    uint64_t _at;   // clock reading to expire at; 0 == never
};

}   // namespace skl::abix::rcu

#endif   // SKL_ABIX_RCU_DEADLINE_H
