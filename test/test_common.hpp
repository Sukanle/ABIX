#ifndef SKL_ABIX_TEST_COMMON_HPP
#define SKL_ABIX_TEST_COMMON_HPP

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string>
#include <chrono>
#include <memory>
#include <functional>
#include <type_traits>

#include "abix/abix.hpp"
#include "dlls/plugin_types.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace skl::abix {

static std::string dll_path(const char *name) {
#if SKL_ABIX_WINDOWS
    return std::string(name) + ".dll";
#else
    return std::string("./plugins/") + name + "/lib" + name + ".so";
#endif
}

static bool file_exists(const std::string &p) {
    FILE *f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

static double ns() {
    using namespace std::chrono;
    return (double)duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

static const char *hot_name(int i) {
    static char buf[16];
    std::snprintf(buf, sizeof(buf), "h_%02d", i);
    return buf;
}

static const char *cold_name(int i) {
    static char buf[16];
    std::snprintf(buf, sizeof(buf), "e_%04d", i);
    return buf;
}

static void log_info(const char *fmt, ...) {
    std::printf("[log] ");
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::printf("\n");
}

}   // namespace skl::abix

using namespace skl::abix;

struct TestVec3 {
    float x;
    float y;
    float z;
};

SKL_ABIX_TYPE_TAG(TestVec3, "test::TestVec3");

SKL_RFS_CLASS(TestVec3)
SKL_RFS_PROPERTY(x)
SKL_RFS_PROPERTY(y)
SKL_RFS_PROPERTY(z)
SKL_RFS_CLASS();

SKL_RFD_CLASS(TestVec3)
SKL_RFD_PROPERTY(x)
SKL_RFD_PROPERTY(y)
SKL_RFD_PROPERTY(z)
SKL_RFD_CLASS();

template<typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

struct PlayerPublic {
    char reserved[16];
    int health;
    float x, y;
};

static_assert(offsetof(PlayerPublic, health) == 16, "PlayerPublic::health offset must be 16");
static_assert(offsetof(PlayerPublic, x) == 20, "PlayerPublic::x offset must be 20");
static_assert(offsetof(PlayerPublic, y) == 24, "PlayerPublic::y offset must be 24");

#endif   // SKL_ABIX_TEST_COMMON_HPP