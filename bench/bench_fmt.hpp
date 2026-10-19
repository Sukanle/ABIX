#pragma once

// Every benchmark prints through fmt. The format strings stay readable, the
// argument types are checked at compile time, and the printf family's conversion
// pitfalls disappear (`%zu`/`%llu` casts, `%s` with a std::string, a mismatched
// width/argument count). The helpers below cover the fixed-buffer case that
// `snprintf` used to handle.
#include <fmt/format.h>

#include <cstddef>

// Formats into `buf` and always NUL-terminates it. At most N-1 characters are
// written, so a value that does not fit is truncated on purpose instead of
// overflowing the buffer — the same bound `snprintf` gave, without the
// -Wformat-truncation warning.
template<std::size_t N, typename... Args>
void format_into(char (&buf)[N], fmt::format_string<Args...> spec, Args &&...args) {
    auto out = fmt::format_to_n(buf, N - 1, spec, static_cast<Args &&>(args)...);
    *out.out = '\0';
}

// Same contract for a buffer whose size the caller tracks (a heap allocation, for
// instance). Passing the size explicitly keeps the bound visible at the call site.
template<typename... Args>
void format_into(char *buf, std::size_t size, fmt::format_string<Args...> spec, Args &&...args) {
    auto out = fmt::format_to_n(buf, size - 1, spec, static_cast<Args &&>(args)...);
    *out.out = '\0';
}
