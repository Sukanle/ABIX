#include "test_common.hpp"

#include "SKL/ABIX/STD/Callback.h"
#include "SKL/ABIX/STD/Optional.h"
#include "SKL/ABIX/STD/Owner.h"
#include "SKL/ABIX/STD/Result.h"
#include "SKL/ABIX/STD/Span.h"
#include "SKL/ABIX/STD/String.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace {

constexpr std::size_t align_up(std::size_t value, std::size_t align) noexcept {
    return (value + align - 1) / align * align;
}

template<class T>
constexpr std::size_t optional_offset() noexcept {
    return align_up(sizeof(std::uint8_t), alignof(T));
}

template<class T>
constexpr std::size_t optional_align() noexcept {
    return alignof(T);
}

template<class T>
constexpr std::size_t optional_size() noexcept {
    return align_up(optional_offset<T>() + sizeof(T), optional_align<T>());
}

template<class T>
constexpr std::size_t result_offset() noexcept {
    return align_up(sizeof(std::int32_t), alignof(T));
}

template<class T>
constexpr std::size_t result_align() noexcept {
    return sizeof(std::int32_t) > alignof(T) ? sizeof(std::int32_t) : alignof(T);
}

template<class T>
constexpr std::size_t result_size() noexcept {
    return align_up(result_offset<T>() + sizeof(T), result_align<T>());
}

constexpr std::size_t string_len_offset() noexcept { return align_up(sizeof(void *), alignof(std::uint64_t)); }

constexpr std::size_t string_encoding_offset() noexcept {
    return align_up(string_len_offset() + sizeof(std::uint64_t), alignof(std::uint32_t));
}

constexpr std::size_t string_align() noexcept {
    constexpr std::size_t pointer = alignof(void *);
    constexpr std::size_t wide = alignof(std::uint64_t);
    return pointer > wide ? (pointer > 4 ? pointer : 4) : (wide > 4 ? wide : 4);
}

constexpr std::size_t string_size() noexcept {
    return align_up(string_encoding_offset() + sizeof(std::uint32_t), string_align());
}

}   // namespace

TEST_CASE("std_contract.optional_layout_audit", "[std][contract]") {
    using optional_i32 = abix_std::abix_optional<std::int32_t>;
    using optional_i64 = abix_std::abix_optional<std::int64_t>;
    using optional_ptr = abix_std::abix_optional<void *>;

    static_assert(offsetof(optional_i32, has_value) == 0);
    static_assert(offsetof(optional_i32, value) == optional_offset<std::int32_t>());
    static_assert(sizeof(optional_i32) == optional_size<std::int32_t>());
    static_assert(alignof(optional_i32) == optional_align<std::int32_t>());

    static_assert(offsetof(optional_i64, value) == optional_offset<std::int64_t>());
    static_assert(sizeof(optional_i64) == optional_size<std::int64_t>());

    static_assert(offsetof(optional_ptr, value) == optional_offset<void *>());
    static_assert(sizeof(optional_ptr) == optional_size<void *>());

    CHECK(sizeof(optional_i32) == 8);
    CHECK(offsetof(optional_i32, value) == 4);
}

TEST_CASE("std_contract.result_layout_audit", "[std][contract]") {
    using result_i32 = abix_std::abix_result<std::int32_t>;
    using result_i64 = abix_std::abix_result<std::int64_t>;
    using result_ptr = abix_std::abix_result<void *>;

    static_assert(offsetof(result_i32, status) == 0);
    static_assert(offsetof(result_i32, value) == result_offset<std::int32_t>());
    static_assert(sizeof(result_i32) == result_size<std::int32_t>());
    static_assert(alignof(result_i32) == result_align<std::int32_t>());

    static_assert(offsetof(result_i64, value) == result_offset<std::int64_t>());
    static_assert(sizeof(result_i64) == result_size<std::int64_t>());

    static_assert(offsetof(result_ptr, value) == result_offset<void *>());
    static_assert(sizeof(result_ptr) == result_size<void *>());

    CHECK(sizeof(result_i32) == 8);
    CHECK(offsetof(result_i32, value) == 4);
}

TEST_CASE("std_contract.string_and_span_layout_audit", "[std][contract]") {
    using string_utf8 = abix_std::abix_str<char>;
    using span_i32 = abix_std::abix_span<std::int32_t>;

    static_assert(offsetof(string_utf8, data) == 0);
    static_assert(offsetof(string_utf8, len) == string_len_offset());
    static_assert(offsetof(string_utf8, encoding) == string_encoding_offset());
    static_assert(sizeof(string_utf8) == string_size());

    static_assert(offsetof(span_i32, data) == 0);
    static_assert(offsetof(span_i32, len) == string_len_offset());
    static_assert(sizeof(span_i32) == align_up(string_len_offset() + sizeof(std::uint64_t), string_align()));

    CHECK(sizeof(string_utf8) == string_size());
    CHECK(sizeof(span_i32) == 16);
}

TEST_CASE("std_contract.callback_layout_audit", "[std][contract]") {
    static_assert(offsetof(abix_callback, invoke) == 0);
    static_assert(offsetof(abix_callback, context) == sizeof(void *));
    static_assert(sizeof(abix_callback) == 2 * sizeof(void *));

    static_assert(offsetof(abix_callback_owned, invoke) == 0);
    static_assert(offsetof(abix_callback_owned, context) == sizeof(void *));
    static_assert(offsetof(abix_callback_owned, release) == 2 * sizeof(void *));
    static_assert(offsetof(abix_callback_owned, flags) == 3 * sizeof(void *));
    static_assert(offsetof(abix_callback_owned, reserved) == 3 * sizeof(void *) + 4);
    static_assert(sizeof(abix_callback_owned) == 3 * sizeof(void *) + 8);

    abix_callback_owned owned{};
    CHECK(owned.invoke == nullptr);
    CHECK(owned.context == nullptr);
    CHECK(owned.release == nullptr);
    CHECK(owned.flags == 0);
    CHECK(owned.reserved == 0);
}

TEST_CASE("std_contract.owner_projection_is_owner_handle", "[std][contract]") {
    using projected = abix_std::Owner<int>;
    using canonical = skl::abix::owner::handle;

    static_assert(std::is_same<projected, canonical>::value, "Owner<T> must not introduce a new ABI type");
    static_assert(sizeof(projected) == sizeof(canonical), "Owner<T> must not change the layout");
    static_assert(alignof(projected) == alignof(canonical), "Owner<T> must not change the alignment");

    CHECK(sizeof(projected) == sizeof(canonical));
    CHECK(alignof(projected) == alignof(canonical));
}

TEST_CASE("std_contract.result_status_roundtrip", "[std][contract]") {
    using result_i32 = abix_std::abix_result<std::int32_t>;

    const std::int32_t statuses[] = {
        0,
        1,
        -1,
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max(),
    };
    for (std::int32_t status : statuses) {
        const result_i32 value{status, 7};
        CHECK(value.status == status);
        CHECK(value.value == 7);
    }

    // An unknown status is carried through; the [[error]] contract decides its meaning.
    const result_i32 unknown{123'456'789, -42};
    CHECK(unknown.status == 123'456'789);
    CHECK(unknown.value == -42);
}

TEST_CASE("std_contract.optional_discriminant_contract", "[std][contract]") {
    using optional_i32 = abix_std::abix_optional<std::int32_t>;
    using discriminant = decltype(optional_i32::has_value);

    static_assert(
        std::is_same<discriminant, std::uint8_t>::value, "abix_optional discriminant must stay a single byte");
    static_assert(sizeof(optional_i32{}.has_value) == 1);

    const optional_i32 absent{0, 0};
    const optional_i32 present{1, 42};
    CHECK(absent.has_value == 0);
    CHECK(present.has_value == 1);
    CHECK(present.value == 42);
}
