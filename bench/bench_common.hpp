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

namespace skl::abix {

#if SKL_ABIX_WINDOWS
inline std::string dll_path(const char *name) { return std::string(name) + ".dll"; }
#else
inline std::string dll_path(const char *name) { return std::string("./plugins/") + name + "/lib" + name + ".so"; }
#endif

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

inline const int HOT_IDS[] = {1'000, 1'001, 1'002, 1'003, 1'004, 1'005, 1'006, 1'007, 1'008, 1'009, 1'010, 1'011, 1'012,
    1'013, 1'014, 1'015, 1'016, 1'017, 1'018, 1'019};

}   // namespace skl::abix

#endif   // SKL_ABIX_BENCH_COMMON_HPP