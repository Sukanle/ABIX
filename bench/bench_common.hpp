#ifndef SKL_ABIX_BENCH_COMMON_HPP
#define SKL_ABIX_BENCH_COMMON_HPP

#include <benchmark/benchmark.h>
#include <string>

#include "ABIX/ABIX.h"
#include "bench_fmt.hpp"
#include "dlls/plugin_path.h"

#if defined(_MSC_VER)
#  define ABIX_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#  define ABIX_NOINLINE __attribute__((noinline))
#else
#  define ABIX_NOINLINE
#endif

// Loads the plugin `name` into `lib` (see dlls/plugin_path.h) or marks the
// current benchmark as skipped. A benchmark that needs a plugin must bail out
// on false: with a failed load the resolved Function/handle points into a
// module that was never mapped, and using it dereferences null.
inline bool require_plugin(benchmark::State &state, skl::abix::dll::Object &lib, const char *name) {
    std::string reason;
    if (load_plugin(lib, name, reason)) return true;
    state.SkipWithError(reason);
    return false;
}

inline const char *hot_name(int i) {
    static char buf[16];
    format_into(buf, "h_{:02}", i);
    return buf;
}

inline const char *cold_name(int i) {
    static char buf[16];
    format_into(buf, "e_{:04}", i);
    return buf;
}

#endif   // SKL_ABIX_BENCH_COMMON_HPP
