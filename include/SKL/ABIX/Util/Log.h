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

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include <atomic>
#include <type_traits>

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

// Explicit logging state. A host or runtime object can own one LogState and
// share it with code under its control. ABIX does not require a process-global
// singleton.
struct LogState {
    std::atomic<log_sink_t> sink{nullptr};
    std::atomic<log_record_sink_t> record_sink{nullptr};
    std::atomic<LogLevel> level{LogLevel::Warning};
};

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

inline void set_log_sink(LogState &state, log_sink_t sink) noexcept {
    // Only one callback kind is active at a time.
    state.record_sink.store(nullptr, std::memory_order_release);
    state.sink.store(sink, std::memory_order_release);
}

inline void set_log_sink(log_sink_t sink) noexcept { set_log_sink(default_log_state(), sink); }

inline void set_log_record_sink(LogState &state, log_record_sink_t sink) noexcept {
    state.sink.store(nullptr, std::memory_order_release);
    state.record_sink.store(sink, std::memory_order_release);
}

inline void set_log_record_sink(log_record_sink_t sink) noexcept { set_log_record_sink(default_log_state(), sink); }

inline void set_log_level(LogState &state, LogLevel level) noexcept {
    state.level.store(normalize_log_level(level), std::memory_order_relaxed);
}

inline void set_log_level(LogLevel level) noexcept { set_log_level(default_log_state(), level); }

inline log_sink_t get_log_sink(const LogState &state) noexcept { return state.sink.load(std::memory_order_acquire); }

inline log_sink_t get_log_sink() noexcept { return get_log_sink(default_log_state()); }

inline log_record_sink_t get_log_record_sink(const LogState &state) noexcept {
    return state.record_sink.load(std::memory_order_acquire);
}

inline log_record_sink_t get_log_record_sink() noexcept { return get_log_record_sink(default_log_state()); }

inline LogLevel get_log_level(const LogState &state) noexcept { return state.level.load(std::memory_order_relaxed); }

inline LogLevel get_log_level() noexcept { return get_log_level(default_log_state()); }

namespace detail {

inline bool log_level_enabled(const LogState &state, LogLevel level) noexcept {
    const auto value = static_cast<uint8_t>(level);
    const auto threshold = static_cast<uint8_t>(get_log_level(state));
    return value <= static_cast<uint8_t>(LogLevel::Error)
        && threshold != static_cast<uint8_t>(LogLevel::Off)
        && value >= threshold;
}

// The sink and level are checked before formatting, so a disabled log pays only
// two atomic loads. The callback runs synchronously and without an ABIX lock.
inline void log_v(LogState &state, LogLevel level, const char *fmt, va_list args) noexcept {
    if (fmt == nullptr || !log_level_enabled(state, level)) return;

    const auto record_sink = state.record_sink.load(std::memory_order_acquire);
    const auto sink = record_sink ? nullptr : state.sink.load(std::memory_order_acquire);
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
inline void log(LogState &state, LogLevel level, const char *fmt, ...) noexcept {
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
