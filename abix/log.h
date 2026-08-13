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

#include <stdio.h>
#include <stdarg.h>

#include "config.h"

SKL_ABIX_NAMESPACE_BEGIN

enum class LogLevel : uint8_t {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
};

using log_sink_t = void (*)(LogLevel level, const char *message);

namespace detail {
inline log_sink_t &get_sink() noexcept {
    static log_sink_t sink = nullptr;
    return sink;
}
}   // namespace detail

inline void set_log_sink(log_sink_t sink) noexcept { detail::get_sink() = sink; }

inline void log(LogLevel level, const char *fmt, ...) noexcept {
    log_sink_t sink = detail::get_sink();
    if (!sink) return;
    char buf[1'024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0) return;
    sink(level, buf);
}

SKL_ABIX_NAMESPACE_END

// ==================== Log Macros ====================
#if defined(ABIX_DISABLE_LOGGING) || defined(ABIX_DISABLE_LOG_LEVEL_DEBUG)
#  define ABIX_LOG_DEBUG(fmt, ...) ((void)0)
#else
#  define ABIX_LOG_DEBUG(fmt, ...) ::skl::abix::log(::skl::abix::LogLevel::Debug, fmt, ##__VA_ARGS__)
#endif

#if defined(ABIX_DISABLE_LOGGING) || defined(ABIX_DISABLE_LOG_LEVEL_INFO)
#  define ABIX_LOG_INFO(fmt, ...) ((void)0)
#else
#  define ABIX_LOG_INFO(fmt, ...) ::skl::abix::log(::skl::abix::LogLevel::Info, fmt, ##__VA_ARGS__)
#endif

#if defined(ABIX_DISABLE_LOGGING) || defined(ABIX_DISABLE_LOG_LEVEL_WARNING)
#  define ABIX_LOG_WARNING(fmt, ...) ((void)0)
#else
#  define ABIX_LOG_WARNING(fmt, ...) ::skl::abix::log(::skl::abix::LogLevel::Warning, fmt, ##__VA_ARGS__)
#endif

#if defined(ABIX_DISABLE_LOGGING) || defined(ABIX_DISABLE_LOG_LEVEL_ERROR)
#  define ABIX_LOG_ERROR(fmt, ...) ((void)0)
#else
#  define ABIX_LOG_ERROR(fmt, ...) ::skl::abix::log(::skl::abix::LogLevel::Error, fmt, ##__VA_ARGS__)
#endif

#endif   // SKL_ABIX_LOG_H