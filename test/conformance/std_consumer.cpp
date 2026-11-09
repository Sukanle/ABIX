// C++ consumer for the ABIX-STD v0.4.0 conformance fixture.
//
// This consumer includes the same headers as the native library, so its
// assertions are the C++ side of the C/C++/Rust/Zig layout matrix.

#include "SKL/ABIX/STD/Callback.h"
#include "SKL/ABIX/STD/Optional.h"
#include "SKL/ABIX/STD/OwnedSpan.h"
#include "SKL/ABIX/STD/OwnedString.h"
#include "SKL/ABIX/STD/Result.h"
#include "SKL/ABIX/STD/Span.h"
#include "SKL/ABIX/STD/String.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

extern "C" {
abix_optional_i32 conformance_optional(int32_t present);
int32_t conformance_optional_valid(abix_optional_i32 value);
int32_t conformance_optional_read(abix_optional_i32 value, int32_t *out);
abix_result_i32 conformance_result(int32_t value);
int32_t conformance_result_valid(abix_result_i32 value);
int32_t conformance_result_read(abix_result_i32 value, int32_t *out);
abix_str_utf8 conformance_greeting();
int32_t conformance_string_valid(abix_str_utf8 value);
int64_t conformance_string_len(abix_str_utf8 value);
abix_span_i32 conformance_numbers();
int64_t conformance_sum(abix_span_i32 values);
abix_owned_str conformance_owned_greeting();
abix_owned_span_i32 conformance_owned_numbers();
int32_t conformance_owned_live_count();
}

static_assert(sizeof(abix_optional_i32) == 12, "C++ optional_i32 layout");
static_assert(offsetof(abix_optional_i32, discriminant) == 0, "C++ optional_i32 layout");
static_assert(offsetof(abix_optional_i32, reserved) == 4, "C++ optional_i32 layout");
static_assert(offsetof(abix_optional_i32, value) == 8, "C++ optional_i32 layout");

static_assert(sizeof(abix_result_i32) == 12, "C++ result_i32 layout");
static_assert(offsetof(abix_result_i32, status) == 0, "C++ result_i32 layout");
static_assert(offsetof(abix_result_i32, reserved) == 4, "C++ result_i32 layout");
static_assert(offsetof(abix_result_i32, value) == 8, "C++ result_i32 layout");

static_assert(sizeof(abix_str_utf8) == abix_std::abix_str_size<char>(), "C++ string layout");
static_assert(offsetof(abix_str_utf8, reserved) == abix_std::abix_str_reserved_offset<char>(), "C++ string layout");

static_assert(sizeof(abix_callback) == 2 * sizeof(void *), "C++ callback layout");
static_assert(sizeof(abix_callback_owned) == 3 * sizeof(void *) + 8, "C++ owned callback layout");

static_assert(offsetof(abix_owner_handle, object) == 0, "C++ owner handle layout");
static_assert(offsetof(abix_owner_handle, mode) == sizeof(void *), "C++ owner handle layout");
static_assert(offsetof(abix_owner_handle, flags) == sizeof(void *) + 4, "C++ owner handle layout");
static_assert(sizeof(void *) != 8 || sizeof(abix_owner_handle) == 32, "C++ owner handle layout");

static_assert(offsetof(abix_owned_str, view) == 0, "C++ owned string layout");
static_assert(offsetof(abix_owned_str, owner) == sizeof(abix_str_utf8), "C++ owned string layout");
static_assert(sizeof(abix_owned_str) == sizeof(abix_str_utf8) + sizeof(abix_owner_handle), "C++ owned string layout");

static_assert(offsetof(abix_owned_span_i32, view) == 0, "C++ owned span layout");
static_assert(offsetof(abix_owned_span_i32, owner) == sizeof(abix_span_i32), "C++ owned span layout");
static_assert(
    sizeof(abix_owned_span_i32) == sizeof(abix_span_i32) + sizeof(abix_owner_handle), "C++ owned span layout");

int main() {
    const abix_optional_i32 some = conformance_optional(1);
    const abix_optional_i32 none = conformance_optional(0);
    int32_t value = 0;
    if (!conformance_optional_valid(some) || conformance_optional_read(some, &value) != 0 || value != 1) {
        return 1;
    }
    if (!conformance_optional_valid(none) || conformance_optional_read(none, &value) != 1) {
        return 2;
    }
    const abix_optional_i32 invalid_discriminant{2, 0, 7};
    const abix_optional_i32 invalid_reserved{1, 1, 7};
    if (conformance_optional_valid(invalid_discriminant)
        || conformance_optional_read(invalid_discriminant, &value) != -1) {
        return 3;
    }
    if (conformance_optional_valid(invalid_reserved) || conformance_optional_read(invalid_reserved, &value) != -1) {
        return 4;
    }

    const abix_result_i32 result = conformance_result(42);
    if (!conformance_result_valid(result) || conformance_result_read(result, &value) != 0 || value != 42) {
        return 5;
    }
    const abix_result_i32 invalid_result{0, 1, 42};
    if (conformance_result_valid(invalid_result) || conformance_result_read(invalid_result, &value) != -1) {
        return 6;
    }

    const abix_str_utf8 greeting = conformance_greeting();
    if (!conformance_string_valid(greeting) || conformance_string_len(greeting) != 5) {
        return 7;
    }
    abix_str_utf8 invalid_string = greeting;
    invalid_string.reserved = 1;
    if (conformance_string_valid(invalid_string) || conformance_string_len(invalid_string) != -1) {
        return 8;
    }

    if (conformance_sum(conformance_numbers()) != 6) {
        return 9;
    }

    abix_owned_str owned_text = conformance_owned_greeting();
    if (owned_text.view.data == nullptr || owned_text.view.len != 5 || owned_text.view.reserved != 0) {
        return 10;
    }
    if (owned_text.owner.object != owned_text.view.data
        || owned_text.owner.mode != ABIX_OWNER_OWNED
        || owned_text.owner.destroy == nullptr
        || conformance_owned_live_count() != 1) {
        return 11;
    }
    owned_text.owner.destroy(owned_text.owner.object);
    if (conformance_owned_live_count() != 0) {
        return 12;
    }

    abix_owned_span_i32 owned_numbers = conformance_owned_numbers();
    if (owned_numbers.view.data == nullptr
        || owned_numbers.view.len != 3
        || owned_numbers.view.data[0] != 4
        || owned_numbers.view.data[1] != 5
        || owned_numbers.view.data[2] != 6) {
        return 13;
    }
    if (owned_numbers.owner.object != owned_numbers.view.data
        || owned_numbers.owner.mode != ABIX_OWNER_OWNED
        || owned_numbers.owner.destroy == nullptr
        || conformance_owned_live_count() != 1) {
        return 14;
    }
    owned_numbers.owner.destroy(owned_numbers.owner.object);
    if (conformance_owned_live_count() != 0) {
        return 15;
    }

    printf("cpp std conformance ok\n");
    return 0;
}
