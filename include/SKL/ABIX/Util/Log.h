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
#ifndef SKL_ABIX_LOG_H
#define SKL_ABIX_LOG_H

// 1 = atomic configuration storage (default); 0 = plain storage for a fixed
// sink / single-threaded configuration. The policy is a compile-time contract,
// not a per-call switch. Modules that share a LogState must use the same policy.
#ifndef ABIX_LOG_THREAD_SAFE
#  define ABIX_LOG_THREAD_SAFE 1
#endif

#if ABIX_LOG_THREAD_SAFE != 0 && ABIX_LOG_THREAD_SAFE != 1
#  error "ABIX_LOG_THREAD_SAFE must be 0 or 1"
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <type_traits>

#include "SKL/ABIX/Util/Atomic.h"

namespace skl::abix::util {

// Logging is an optional diagnostic side channel. Errors are reported through
// return values / error codes and must remain correct even when logging is off.
enum class LogLevel : uint8_t {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
    Off = 4,
};

// Legacy C callback. `message` is a NUL-terminated stack buffer that is valid
// only for the duration of this call. It carries no explicit truncation flag;
// use log_record_sink_t when the consumer needs that information.
using log_sink_t = void (*)(LogLevel level, const char *message);

// Extended C callback. `record` and `record->message` are valid only for the
// duration of this call. `length` excludes the terminating NUL; `truncated`
// is true when the formatted message did not fit in the fixed buffer.
struct LogRecord {
    LogLevel level;
    const char *message;
    uint32_t length;
    bool truncated;
};

using log_record_sink_t = void (*)(const LogRecord *record);

static_assert(sizeof(LogLevel) == 1, "LogLevel is part of the diagnostic callback ABI");
static_assert(std::is_trivially_copyable_v<LogRecord>, "LogRecord must remain a plain value for callback ABI use");

// Fixed-size formatting buffer. `LogRecord::length` is at most
// `log_buffer_size - 1`; longer output is marked truncated.
inline constexpr size_t log_buffer_size = 1'024;

namespace detail {

// ABIX's atomic helpers operate on caller-owned plain storage, avoiding the
// compiler-defined layout of std::atomic across DLL/CRT boundaries.
template<typename Sink, bool ThreadSafe>
struct SinkStorage;

template<typename Sink>
struct SinkStorage<Sink, true> {
    Sink value = nullptr;

    void set(Sink sink) noexcept { store_release(&value, sink); }
    Sink get() const noexcept { return load_acquire(&value); }
};

template<typename Sink>
struct SinkStorage<Sink, false> {
    Sink value = nullptr;

    void set(Sink sink) noexcept { value = sink; }
    Sink get() const noexcept { return value; }
};

template<bool ThreadSafe>
struct LevelStorage;

template<>
struct LevelStorage<true> {
    uint8_t value = static_cast<uint8_t>(LogLevel::Warning);

    void set(LogLevel level) noexcept { store_relaxed(&value, static_cast<uint8_t>(level)); }
    LogLevel get() const noexcept { return static_cast<LogLevel>(load_relaxed(&value)); }
};

template<>
struct LevelStorage<false> {
    uint8_t value = static_cast<uint8_t>(LogLevel::Warning);

    void set(LogLevel level) noexcept { value = static_cast<uint8_t>(level); }
    LogLevel get() const noexcept { return static_cast<LogLevel>(value); }
};

}   // namespace detail

// Explicit logging state. A host or runtime object can own one LogState and
// share it with code under its control. ABIX does not require a process-global
// singleton.
template<bool ThreadSafe>
struct LogStateT {
    detail::SinkStorage<log_sink_t, ThreadSafe> sink;
    detail::SinkStorage<log_record_sink_t, ThreadSafe> record_sink;
    detail::LevelStorage<ThreadSafe> level;

    LogStateT() noexcept = default;
    LogStateT(const LogStateT &) = delete;
    LogStateT &operator=(const LogStateT &) = delete;
};

using ThreadSafeLogState = LogStateT<true>;
using SingleThreadedLogState = LogStateT<false>;

static_assert(std::is_standard_layout_v<ThreadSafeLogState>, "LogState must stay a plain-storage object");
static_assert(std::is_standard_layout_v<SingleThreadedLogState>, "LogState must stay a plain-storage object");
static_assert(sizeof(ThreadSafeLogState) == sizeof(SingleThreadedLogState),
    "the dispatch policy must not change LogState layout");

#if ABIX_LOG_THREAD_SAFE
using LogState = ThreadSafeLogState;
#else
using LogState = SingleThreadedLogState;
#endif

// Header-only ABIX cannot promise that this state is process-global: a DSO
// that compiles ABIX gets its own inline copy. Use an explicit LogState when
// ownership must cross a DLL boundary; install a sink in each ABIX-bearing
// module when relying on the convenience default.
inline LogState &default_log_state() noexcept {
    static LogState state;
    return state;
}

constexpr LogLevel normalize_log_level(LogLevel level) noexcept {
    return static_cast<uint8_t>(level) <= static_cast<uint8_t>(LogLevel::Off) ? level : LogLevel::Off;
}

template<bool ThreadSafe>
inline void set_log_sink(LogStateT<ThreadSafe> &state, log_sink_t sink) noexcept {
    // Only one callback kind is active at a time.
    state.record_sink.set(nullptr);
    state.sink.set(sink);
}

inline void set_log_sink(log_sink_t sink) noexcept { set_log_sink(default_log_state(), sink); }

template<bool ThreadSafe>
inline void set_log_record_sink(LogStateT<ThreadSafe> &state, log_record_sink_t sink) noexcept {
    state.sink.set(nullptr);
    state.record_sink.set(sink);
}

inline void set_log_record_sink(log_record_sink_t sink) noexcept { set_log_record_sink(default_log_state(), sink); }

template<bool ThreadSafe>
inline void set_log_level(LogStateT<ThreadSafe> &state, LogLevel level) noexcept {
    state.level.set(normalize_log_level(level));
}

inline void set_log_level(LogLevel level) noexcept { set_log_level(default_log_state(), level); }

template<bool ThreadSafe>
inline log_sink_t get_log_sink(const LogStateT<ThreadSafe> &state) noexcept {
    return state.sink.get();
}

inline log_sink_t get_log_sink() noexcept { return get_log_sink(default_log_state()); }

template<bool ThreadSafe>
inline log_record_sink_t get_log_record_sink(const LogStateT<ThreadSafe> &state) noexcept {
    return state.record_sink.get();
}

inline log_record_sink_t get_log_record_sink() noexcept { return get_log_record_sink(default_log_state()); }

template<bool ThreadSafe>
inline LogLevel get_log_level(const LogStateT<ThreadSafe> &state) noexcept {
    return state.level.get();
}

inline LogLevel get_log_level() noexcept { return get_log_level(default_log_state()); }

namespace detail {

template<bool ThreadSafe>
inline bool log_level_enabled(const LogStateT<ThreadSafe> &state, LogLevel level) noexcept {
    const auto value = static_cast<uint8_t>(level);
    const auto threshold = static_cast<uint8_t>(state.level.get());
    return value <= static_cast<uint8_t>(LogLevel::Error)
        && threshold != static_cast<uint8_t>(LogLevel::Off)
        && value >= threshold;
}

// The sink and level are checked before formatting, so a disabled log pays only
// the configured reads. The callback runs synchronously and without an ABIX
// lock. External logging backends remain responsible for their own concurrency.
template<bool ThreadSafe>
inline void log_v(LogStateT<ThreadSafe> &state, LogLevel level, const char *fmt, va_list args) noexcept {
    if (fmt == nullptr || !log_level_enabled(state, level)) return;

    const auto record_sink = state.record_sink.get();
    const auto sink = record_sink ? nullptr : state.sink.get();
    if (record_sink == nullptr && sink == nullptr) return;

    char buf[log_buffer_size];
    const int written = vsnprintf(buf, sizeof(buf), fmt, args);
    if (written < 0) return;

    const bool truncated = static_cast<size_t>(written) >= log_buffer_size;
    const auto length = truncated ? static_cast<uint32_t>(log_buffer_size - 1) : static_cast<uint32_t>(written);

    if (record_sink != nullptr) {
        const LogRecord record{level, buf, length, truncated};
        record_sink(&record);
    } else {
        // The legacy callback has no length/truncated fields; the message is
        // always NUL-terminated and remains valid for the duration of the call.
        sink(level, buf);
    }
}

}   // namespace detail

// Format and dispatch one diagnostic event on the calling thread. The callback
// must not throw; because this function is noexcept, an escaping exception
// terminates the process. A callback may call back into the logging API, but
// must avoid unbounded re-entrant logging.
template<bool ThreadSafe>
inline void log(LogStateT<ThreadSafe> &state, LogLevel level, const char *fmt, ...) noexcept {
    va_list args;
    va_start(args, fmt);
    detail::log_v(state, level, fmt, args);
    va_end(args);
}

inline void log(LogLevel level, const char *fmt, ...) noexcept {
    va_list args;
    va_start(args, fmt);
    detail::log_v(default_log_state(), level, fmt, args);
    va_end(args);
}

}   // namespace skl::abix::util

// ==================== Log Macros ====================
// The variadic call is portable C++11+; __VA_ARGS__ includes the format string,
// so no GNU `##__VA_ARGS__` extension is needed.
#if defined(ABIX_DISABLE_LOGGING) || defined(ABIX_DISABLE_LOG_LEVEL_DEBUG)
#  define ABIX_LOG_DEBUG(...) ((void)0)
#  define ABIX_LOG_DEBUG_TO(state, ...) ((void)0)
#else
#  define ABIX_LOG_DEBUG(...) ::skl::abix::util::log(::skl::abix::util::LogLevel::Debug, __VA_ARGS__)
#  define ABIX_LOG_DEBUG_TO(state, ...) ::skl::abix::util::log((state), ::skl::abix::util::LogLevel::Debug, __VA_ARGS__)
#endif

#if defined(ABIX_DISABLE_LOGGING) || defined(ABIX_DISABLE_LOG_LEVEL_INFO)
#  define ABIX_LOG_INFO(...) ((void)0)
#  define ABIX_LOG_INFO_TO(state, ...) ((void)0)
#else
#  define ABIX_LOG_INFO(...) ::skl::abix::util::log(::skl::abix::util::LogLevel::Info, __VA_ARGS__)
#  define ABIX_LOG_INFO_TO(state, ...) ::skl::abix::util::log((state), ::skl::abix::util::LogLevel::Info, __VA_ARGS__)
#endif

#if defined(ABIX_DISABLE_LOGGING) || defined(ABIX_DISABLE_LOG_LEVEL_WARNING)
#  define ABIX_LOG_WARNING(...) ((void)0)
#  define ABIX_LOG_WARNING_TO(state, ...) ((void)0)
#else
#  define ABIX_LOG_WARNING(...) ::skl::abix::util::log(::skl::abix::util::LogLevel::Warning, __VA_ARGS__)
#  define ABIX_LOG_WARNING_TO(state, ...) \
      ::skl::abix::util::log((state), ::skl::abix::util::LogLevel::Warning, __VA_ARGS__)
#endif

#if defined(ABIX_DISABLE_LOGGING) || defined(ABIX_DISABLE_LOG_LEVEL_ERROR)
#  define ABIX_LOG_ERROR(...) ((void)0)
#  define ABIX_LOG_ERROR_TO(state, ...) ((void)0)
#else
#  define ABIX_LOG_ERROR(...) ::skl::abix::util::log(::skl::abix::util::LogLevel::Error, __VA_ARGS__)
#  define ABIX_LOG_ERROR_TO(state, ...) ::skl::abix::util::log((state), ::skl::abix::util::LogLevel::Error, __VA_ARGS__)
#endif

#endif   // SKL_ABIX_LOG_H
