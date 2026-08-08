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
#include "abix/abix.hpp"
#include <cstdio>
#include <cstring>

namespace {

char s_last_msg[256];
int s_last_level = -1;
int s_last_len = 0;

void set_msg(const char *msg) {
    std::snprintf(s_last_msg, sizeof(s_last_msg), "%s", msg ? msg : "");
    s_last_msg[sizeof(s_last_msg) - 1] = '\0';
    s_last_len = msg ? static_cast<int>(std::strlen(msg)) : 0;
}

}   // namespace

extern "C" int print(const char *msg) {
    set_msg(msg);
    return s_last_len;
}
extern "C" const char *get_last_msg() { return s_last_msg; }
extern "C" int get_last_level() { return s_last_level; }
extern "C" const char *get_build_tag() {
#if defined(__clang__)
    return "clang";
#elif defined(__GNUC__)
    return "gcc";
#elif defined(_MSC_VER)
    return "msvc";
#else
    return "unknown";
#endif
}
extern "C" int get_int_sum(int a, int b) { return a + b; }
extern "C" double get_double_mul(double a, double b) { return a * b; }

extern "C" void log_v1(const char *msg) {
    set_msg(msg);
    s_last_level = 0;
}
extern "C" void log_v2(const char *msg, int level) {
    set_msg(msg);
    s_last_level = level;
}

// clang-format off
SKL_ABIX_DEFINE_TABLE(
    SKL_ABIX_ENTRY("get_build_tag", get_build_tag),
    SKL_ABIX_ENTRY("get_double_mul", get_double_mul),
    SKL_ABIX_ENTRY("get_int_sum", get_int_sum),
    SKL_ABIX_ENTRY("get_last_level", get_last_level),
    SKL_ABIX_ENTRY("get_last_msg", get_last_msg),
    SKL_ABIX_ENTRY_FULL("log", log_v1, Cdecl, SKL_ABIX_VERSION("1.0"), 0),
    SKL_ABIX_ENTRY_FULL("log", log_v2, Cdecl, SKL_ABIX_VERSION("2.0"), 0),
    SKL_ABIX_ENTRY("print", print),
)
// clang-format on
