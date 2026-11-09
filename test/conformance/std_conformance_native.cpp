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

// Native side of the ABIX-STD conformance fixture. The same shared library is
// consumed by a C, a C++, a Rust and a Zig consumer; each consumer declares the
// v0.4.0 explicit layout itself instead of reusing a language-native optional,
// result or string type.

#include "SKL/ABIX/Interface/OwnerABI.h"
#include "SKL/ABIX/STD/Optional.h"
#include "SKL/ABIX/STD/OwnedSpan.h"
#include "SKL/ABIX/STD/OwnedString.h"
#include "SKL/ABIX/STD/Result.h"
#include "SKL/ABIX/STD/Span.h"
#include "SKL/ABIX/STD/String.h"

#include <stdint.h>

#if defined(_WIN32)
#  define CONFORMANCE_API __declspec(dllexport)
#else
#  define CONFORMANCE_API __attribute__((visibility("default")))
#endif

namespace {

const int32_t kNumbers[] = {1, 2, 3};
char kGreeting[] = "hello";
int32_t g_owned_live = 0;

void destroy_owned_chars(void *object) noexcept {
    delete[] static_cast<char *>(object);
    --g_owned_live;
}

void destroy_owned_i32(void *object) noexcept {
    delete[] static_cast<int32_t *>(object);
    --g_owned_live;
}

bool optional_header_valid(const abix_optional_i32 &value) noexcept {
    return value.reserved == 0 && (value.discriminant == 0 || value.discriminant == 1);
}

bool result_header_valid(const abix_result_i32 &value) noexcept { return value.reserved == 0; }

bool string_header_valid(const abix_str_utf8 &value) noexcept {
    if (value.reserved != 0) {
        return false;
    }
    if (value.encoding > static_cast<uint32_t>(abix_std::Encoding::utf32)) {
        return false;
    }
    return value.data != nullptr || value.len == 0;
}

}   // namespace

extern "C" {

CONFORMANCE_API abix_optional_i32 conformance_optional(int32_t present) { return {present ? 1u : 0u, 0, present}; }

CONFORMANCE_API int32_t conformance_optional_valid(abix_optional_i32 value) {
    return optional_header_valid(value) ? 1 : 0;
}

// 0 = Some with `out` written, 1 = None, -1 = invalid header.
CONFORMANCE_API int32_t conformance_optional_read(abix_optional_i32 value, int32_t *out) {
    if (!optional_header_valid(value)) {
        return -1;
    }
    if (value.discriminant == 0) {
        return 1;
    }
    if (out != nullptr) {
        *out = value.value;
    }
    return 0;
}

CONFORMANCE_API abix_result_i32 conformance_result(int32_t value) { return {0, 0, value}; }

CONFORMANCE_API int32_t conformance_result_valid(abix_result_i32 value) { return result_header_valid(value) ? 1 : 0; }

CONFORMANCE_API int32_t conformance_result_read(abix_result_i32 value, int32_t *out) {
    if (!result_header_valid(value)) {
        return -1;
    }
    if (value.status != 0) {
        return value.status;
    }
    if (out != nullptr) {
        *out = value.value;
    }
    return 0;
}

CONFORMANCE_API abix_str_utf8 conformance_greeting() {
    return {kGreeting, 5, static_cast<uint32_t>(abix_std::Encoding::utf8), 0};
}

CONFORMANCE_API int32_t conformance_string_valid(abix_str_utf8 value) { return string_header_valid(value) ? 1 : 0; }

CONFORMANCE_API int64_t conformance_string_len(abix_str_utf8 value) {
    return string_header_valid(value) ? static_cast<int64_t>(value.len) : -1;
}

CONFORMANCE_API abix_span_i32 conformance_numbers() { return {kNumbers, sizeof(kNumbers) / sizeof(kNumbers[0])}; }

CONFORMANCE_API int64_t conformance_sum(abix_span_i32 values) {
    if (values.data == nullptr && values.len != 0) {
        return -1;
    }
    int64_t sum = 0;
    for (uint64_t index = 0; index < values.len; ++index) {
        sum += values.data[index];
    }
    return sum;
}

CONFORMANCE_API abix_owned_str conformance_owned_greeting() {
    char *text = new char[6]{'o', 'w', 'n', 'e', 'd', '\0'};
    ++g_owned_live;
    return {
        {text, 5, static_cast<uint32_t>(abix_std::Encoding::utf8), 0},
        {text, ABIX_OWNER_OWNED, 0, &destroy_owned_chars, 0},
    };
}

CONFORMANCE_API abix_owned_span_i32 conformance_owned_numbers() {
    int32_t *values = new int32_t[3]{4, 5, 6};
    ++g_owned_live;
    return {
        {values, 3},
        {values, ABIX_OWNER_OWNED, 0, &destroy_owned_i32, 0},
    };
}

CONFORMANCE_API int32_t conformance_owned_live_count() { return g_owned_live; }
}
