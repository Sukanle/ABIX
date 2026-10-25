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
#include <mutex>

#include "SKL/ABIX/ABIX.h"
#include "dlls/plugin_path.h"
#include "dlls/plugin_types.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace {
struct LogSinkInitializer {
    LogSinkInitializer() {
        skl::abix::util::set_log_level(skl::abix::util::LogLevel::Debug);
        skl::abix::util::set_log_sink([](skl::abix::util::LogLevel level, const char *msg) {
            static std::mutex log_mutex;
            std::lock_guard<std::mutex> lock(log_mutex);
            const char *level_str = "UNKNOWN";
            switch (level) {
                case skl::abix::util::LogLevel::Debug:   level_str = "DEBUG"; break;
                case skl::abix::util::LogLevel::Info:    level_str = "INFO"; break;
                case skl::abix::util::LogLevel::Warning: level_str = "WARNING"; break;
                case skl::abix::util::LogLevel::Error:   level_str = "ERROR"; break;
                case skl::abix::util::LogLevel::Off:     level_str = "OFF"; break;
            }
            fprintf(stderr, "[ABIX_%s] %s\n", level_str, msg);
        });
    }
};
static LogSinkInitializer _log_sink_init;
}   // anonymous namespace

bool file_exists(const std::string &p) {
    FILE *f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

double ns() {
    using namespace std::chrono;
    return (double)duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

const char *hot_name(int i) {
    static char buf[16];
    snprintf(buf, sizeof(buf), "h_%02d", i);
    return buf;
}

const char *cold_name(int i) {
    static char buf[16];
    snprintf(buf, sizeof(buf), "e_%04d", i);
    return buf;
}

void log_info(const char *fmt, ...) {
    printf("[log] ");
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

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
