/* C consumer for the ABIX-STD v0.4.0 conformance fixture.
 *
 * The C headers do not instantiate the C++ templates, so this consumer spells
 * the concrete ABI entities explicitly. That is the point: the C view must
 * match the C++ view field by field.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#if defined(_WIN32)
#  define CONFORMANCE_IMPORT __declspec(dllimport)
#else
#  define CONFORMANCE_IMPORT
#endif

#define ALIGN_UP(value, align) (((value) + (align) - 1) / (align) * (align))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct {
    uint32_t discriminant;
    uint32_t reserved;
    int32_t value;
} conformance_optional_i32;

typedef struct {
    int32_t status;
    uint32_t reserved;
    int32_t value;
} conformance_result_i32;

typedef struct {
    const char *data;
    uint64_t len;
    uint32_t encoding;
    uint32_t reserved;
} conformance_str_utf8;

typedef struct {
    const int32_t *data;
    uint64_t len;
} conformance_span_i32;

typedef int32_t (*conformance_callback_fn)(void *context);
typedef struct {
    conformance_callback_fn invoke;
    void *context;
} conformance_callback;

typedef void (*conformance_callback_release_fn)(void *context);
typedef struct {
    conformance_callback_fn invoke;
    void *context;
    conformance_callback_release_fn release;
    uint32_t flags;
    uint32_t reserved;
} conformance_callback_owned;

#define STR_LEN_OFFSET ALIGN_UP(sizeof(void *), _Alignof(uint64_t))
#define STR_ENCODING_OFFSET ALIGN_UP(STR_LEN_OFFSET + sizeof(uint64_t), _Alignof(uint32_t))
#define STR_RESERVED_OFFSET (STR_ENCODING_OFFSET + sizeof(uint32_t))
#define STR_ALIGN MAX(MAX(sizeof(void *), _Alignof(uint64_t)), _Alignof(uint32_t))
#define STR_SIZE ALIGN_UP(STR_RESERVED_OFFSET + sizeof(uint32_t), STR_ALIGN)

_Static_assert(sizeof(conformance_optional_i32) == 12, "C optional_i32 layout");
_Static_assert(offsetof(conformance_optional_i32, discriminant) == 0, "C optional_i32 layout");
_Static_assert(offsetof(conformance_optional_i32, reserved) == 4, "C optional_i32 layout");
_Static_assert(offsetof(conformance_optional_i32, value) == 8, "C optional_i32 layout");

_Static_assert(sizeof(conformance_result_i32) == 12, "C result_i32 layout");
_Static_assert(offsetof(conformance_result_i32, status) == 0, "C result_i32 layout");
_Static_assert(offsetof(conformance_result_i32, reserved) == 4, "C result_i32 layout");
_Static_assert(offsetof(conformance_result_i32, value) == 8, "C result_i32 layout");

_Static_assert(offsetof(conformance_str_utf8, len) == STR_LEN_OFFSET, "C string layout");
_Static_assert(offsetof(conformance_str_utf8, encoding) == STR_ENCODING_OFFSET, "C string layout");
_Static_assert(offsetof(conformance_str_utf8, reserved) == STR_RESERVED_OFFSET, "C string layout");
_Static_assert(sizeof(conformance_str_utf8) == STR_SIZE, "C string layout");

_Static_assert(sizeof(conformance_callback) == 2 * sizeof(void *), "C callback layout");
_Static_assert(sizeof(conformance_callback_owned) == 3 * sizeof(void *) + 8, "C owned callback layout");

CONFORMANCE_IMPORT conformance_optional_i32 conformance_optional(int32_t present);
CONFORMANCE_IMPORT int32_t conformance_optional_valid(conformance_optional_i32 value);
CONFORMANCE_IMPORT int32_t conformance_optional_read(conformance_optional_i32 value, int32_t *out);
CONFORMANCE_IMPORT conformance_result_i32 conformance_result(int32_t value);
CONFORMANCE_IMPORT int32_t conformance_result_valid(conformance_result_i32 value);
CONFORMANCE_IMPORT int32_t conformance_result_read(conformance_result_i32 value, int32_t *out);
CONFORMANCE_IMPORT conformance_str_utf8 conformance_greeting(void);
CONFORMANCE_IMPORT int32_t conformance_string_valid(conformance_str_utf8 value);
CONFORMANCE_IMPORT int64_t conformance_string_len(conformance_str_utf8 value);
CONFORMANCE_IMPORT conformance_span_i32 conformance_numbers(void);
CONFORMANCE_IMPORT int64_t conformance_sum(conformance_span_i32 values);

int main(void) {
    conformance_optional_i32 some = conformance_optional(1);
    conformance_optional_i32 none = conformance_optional(0);
    int32_t value = 0;
    if (!conformance_optional_valid(some) || conformance_optional_read(some, &value) != 0 || value != 1) {
        return 1;
    }
    if (!conformance_optional_valid(none) || conformance_optional_read(none, &value) != 1) {
        return 2;
    }
    conformance_optional_i32 invalid_discriminant = {2, 0, 7};
    conformance_optional_i32 invalid_reserved = {1, 1, 7};
    if (conformance_optional_valid(invalid_discriminant)
        || conformance_optional_read(invalid_discriminant, &value) != -1) {
        return 3;
    }
    if (conformance_optional_valid(invalid_reserved) || conformance_optional_read(invalid_reserved, &value) != -1) {
        return 4;
    }

    conformance_result_i32 result = conformance_result(42);
    if (!conformance_result_valid(result) || conformance_result_read(result, &value) != 0 || value != 42) {
        return 5;
    }
    conformance_result_i32 invalid_result = {0, 1, 42};
    if (conformance_result_valid(invalid_result) || conformance_result_read(invalid_result, &value) != -1) {
        return 6;
    }

    conformance_str_utf8 greeting = conformance_greeting();
    if (!conformance_string_valid(greeting) || conformance_string_len(greeting) != 5) {
        return 7;
    }
    conformance_str_utf8 invalid_string = greeting;
    invalid_string.reserved = 1;
    if (conformance_string_valid(invalid_string) || conformance_string_len(invalid_string) != -1) {
        return 8;
    }

    if (conformance_sum(conformance_numbers()) != 6) {
        return 9;
    }

    printf("c std conformance ok\n");
    return 0;
}
