#ifndef SKL_ABIX_BENCH_COMMON_HPP
#define SKL_ABIX_BENCH_COMMON_HPP

#include <string>
#include <cstdio>

#include "abix/abix.hpp"

#if defined(_MSC_VER)
#  define ABIX_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#  define ABIX_NOINLINE __attribute__((noinline))
#else
#  define ABIX_NOINLINE
#endif

inline std::string dll_path(const char *name) { return std::string(name) + SKL_ABIX_DLL_SUFFIX; }

inline const char *hot_name(int i) {
    static char buf[16];
    std::snprintf(buf, sizeof(buf), "h_%02d", i);
    return buf;
}

inline const char *cold_name(int i) {
    static char buf[16];
    std::snprintf(buf, sizeof(buf), "e_%04d", i);
    return buf;
}

#endif   // SKL_ABIX_BENCH_COMMON_HPP