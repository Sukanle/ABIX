/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef SKL_ABIX_DLLS_PLUGIN_PATH_H
#define SKL_ABIX_DLLS_PLUGIN_PATH_H

// Locating and loading the plugin modules built from the `dlls/` sources. They
// are staged next to the test/benchmark executables by the build, so the path
// is resolved relative to the running executable rather than the process
// working directory. Shared by test/test_common.hpp and bench/bench_common.hpp
// so both agree on where a plugin lives.
//
// `load_plugin` is deliberately framework-agnostic: it only resolves and loads,
// and leaves the reporting to the caller (a Catch2 REQUIRE, or a benchmark's
// State::SkipWithError), so this header stays free of the test/benchmark
// frameworks.

#include <limits>
#include <string>
#include <vector>

#include "SKL/ABIX/DLL/Object.h"
#include "SKL/ABIX/Util/Config.h"

#if defined(SKL_ABIX_WINDOWS)
#  include <stringapiset.h>
#  include <windef.h>
#  include <winnt.h>
#elif defined(SKL_ABIX_APPLE)
#  include <mach-o/dyld.h>
#else
#  include <errno.h>
#  include <unistd.h>
#endif

inline std::string getExecutablePath() {
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

// Path of the plugin module `name` (e.g. "hotcache_dll") that sits next to the
// running executable. Advancing past the last separator (instead of truncating
// at it) keeps the trailing separator, so `substr` can be concatenated directly.
inline std::string dll_path(const char *name) {
    const std::string exepath = getExecutablePath();
    const std::size_t sep = exepath.find_last_of(SKL_ABIX_PATHSEPARATOR);
    if (sep == std::string::npos) return std::string(name) + SKL_ABIX_DLL_SUFFIX;
    return exepath.substr(0, sep + 1) + name + SKL_ABIX_DLL_SUFFIX;
}

// Loads the plugin module `name` into `lib`, which must outlive the caller's
// use of it. Returns true on success; on failure returns false and writes a
// human-readable diagnostic into `reason`. Callers surface that through their
// own framework instead of dereferencing a half-initialised handle.
inline bool load_plugin(skl::abix::dll::Object &lib, const char *name, std::string &reason) {
    const std::string path = dll_path(name);
    if (lib.load(path.c_str())) return true;
    reason = "cannot load plugin '" + path + "': the build stages plugins next to the binaries, rebuild or re-run";
    return false;
}

#endif   // SKL_ABIX_DLLS_PLUGIN_PATH_H
