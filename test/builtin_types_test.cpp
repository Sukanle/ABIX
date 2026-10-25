#include "test_common.hpp"

#include <float.h>
#include <limits>
#include <wchar.h>

TEST_CASE("model.predefined_builtin_type_ids", "[model][types]") {
    using namespace skl::abix::model;

    CHECK(v0 == canonical_type_id("prim:void"));
    CHECK(b8 == canonical_type_id("prim:bool"));

    CHECK(i8 == canonical_type_id("prim:i8"));
    CHECK(i16 == canonical_type_id("prim:i16"));
    CHECK(i32 == canonical_type_id("prim:i32"));
    CHECK(i64 == canonical_type_id("prim:i64"));
    CHECK(i128 == canonical_type_id("prim:i128"));
    CHECK(isize == (sizeof(void *) == 4 ? i32 : i64));

    CHECK(u8 == canonical_type_id("prim:u8"));
    CHECK(u16 == canonical_type_id("prim:u16"));
    CHECK(u32 == canonical_type_id("prim:u32"));
    CHECK(u64 == canonical_type_id("prim:u64"));
    CHECK(u128 == canonical_type_id("prim:u128"));
    CHECK(usize == (sizeof(void *) == 4 ? u32 : u64));

    const bool wint_signed = std::numeric_limits<wint_t>::is_signed;
    TypeId expected_wint = wint_signed ? i64 : u64;
    if (sizeof(wint_t) == 1)
        expected_wint = wint_signed ? i8 : u8;
    else if (sizeof(wint_t) == 2)
        expected_wint = wint_signed ? i16 : u16;
    else if (sizeof(wint_t) == 4)
        expected_wint = wint_signed ? i32 : u32;
    CHECK(wint == expected_wint);

    CHECK(f32 == canonical_type_id("prim:fieee32"));
    CHECK(f64 == canonical_type_id("prim:fieee64"));
    CHECK(f128 == canonical_type_id("prim:fieee128"));
    CHECK(f128x87 == canonical_type_id("prim:fx87_80"));
    CHECK(f128ppc == canonical_type_id("prim:fppc_double_double"));

    CHECK(f16 == canonical_type_id("prim:fieee16"));
    CHECK(bf16 == canonical_type_id("prim:fbf16"));

#if LDBL_MANT_DIG == 53
    CHECK(ldouble == f64);
#elif LDBL_MANT_DIG == 64
    CHECK(ldouble == f128x87);
#elif LDBL_MANT_DIG == 106
    CHECK(ldouble == f128ppc);
#elif LDBL_MANT_DIG == 113
    CHECK(ldouble == f128);
#else
#  error "unsupported long double ABI"
#endif

    CHECK(nchar == canonical_type_id(std::numeric_limits<char>::is_signed ? "prim:char_s" : "prim:char_u"));
    CHECK(schar == canonical_type_id("prim:schar"));
    CHECK(uchar == canonical_type_id("prim:uchar"));
    CHECK(wchar == canonical_type_id(std::numeric_limits<wchar_t>::is_signed ? "prim:wchar_s" : "prim:wchar_u"));

    CHECK(c8 == canonical_type_id("prim:char8"));
    CHECK(c16 == canonical_type_id("prim:char16"));
    CHECK(c32 == canonical_type_id("prim:char32"));
}
