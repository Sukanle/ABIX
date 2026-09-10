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

#include "abix/abix.hpp"
#include "dlls/plugin_types.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#if defined(SKL_ABIX_WINDOWS)
#  include <winnt.h>
#  include <windef.h>
#  include <stringapiset.h>
#elif defined(SKL_ABIX_APPLE)
#  include <mach-o/dyld.h>
#else
#  include <unistd.h>
#  include <errno.h>
#endif

std::string getExecutablePath() {
#ifdef SKL_ABIX_WINDOWS
    DWORD max_size = 128;
    while (true) {
        std::vector<TCHAR> buffer(max_size);
        DWORD len = GetModuleFileName(nullptr, buffer.data(), max_size);
        if (len == 0) return "";
        if (len < max_size) {
#  ifdef UNICODE
            std::vector<char> utf8(len);
            int len = WideCharToMultiByte(CP_UTF8, 0, buffer.data(), len, nullptr, 0, nullptr, nullptr);
            WideCharToMultiByte(CP_UTF8, 0, buffer.data(), len, utf8.data(), len, nullptr, nullptr);
            return std::string(utf8.data());
#  else
            return std::string(buffer.data());
#  endif
        }
        if (max_size > std::numeric_limits<DWORD>::max() / 2) return "";
        max_size *= 2;
    }
#elif defined(SKL_ABIX_APPLE)
    uint32_t max_size = 0;
    _NSGetExecutablePath(nullptr, &max_size);
    if (max_size == 0) return "";
    std::vector<char> buffer(max_size);
    _NSGetExecutablePath(buffer.data(), &max_size);
    return std::string(buffer.data());
#else
    size_t size = 256;
    while (true) {
        std::vector<char> buffer(size);
        ssize_t len = readlink("/proc/self/exe", buffer.data(), size);
        if (len == -1) {
            if (errno == ENAMETOOLONG) {
                if (size > std::numeric_limits<size_t>::max() / 2) return "";
                size *= 2;
                continue;
            }
            return "";
        }
        return std::string(buffer.data(), static_cast<size_t>(len));
    }
#endif
}

std::string dll_path(const char *name) {
    auto exepath = getExecutablePath();
    auto exedir = exepath.substr(0, exepath.find_last_of(SKL_ABIX_PATHSEPARATOR));
    return exedir + SKL_ABIX_PATHSEPARATOR + std::string(name) + SKL_ABIX_DLL_SUFFIX;
}

namespace {
struct LogSinkInitializer {
    LogSinkInitializer() {
        skl::abix::set_log_sink([](skl::abix::LogLevel level, const char *msg) {
            static std::mutex log_mutex;
            std::lock_guard<std::mutex> lock(log_mutex);
            const char *level_str = "UNKNOWN";
            switch (level) {
                case skl::abix::LogLevel::Debug:   level_str = "DEBUG"; break;
                case skl::abix::LogLevel::Info:    level_str = "INFO"; break;
                case skl::abix::LogLevel::Warning: level_str = "WARNING"; break;
                case skl::abix::LogLevel::Error:   level_str = "ERROR"; break;
            }
            std::fprintf(stderr, "[ABIX_%s] %s\n", level_str, msg);
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
    std::snprintf(buf, sizeof(buf), "h_%02d", i);
    return buf;
}

const char *cold_name(int i) {
    static char buf[16];
    std::snprintf(buf, sizeof(buf), "e_%04d", i);
    return buf;
}

void log_info(const char *fmt, ...) {
    std::printf("[log] ");
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::printf("\n");
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