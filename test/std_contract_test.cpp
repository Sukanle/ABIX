#include "test_common.hpp"

#include "SKL/ABIX/STD/Callback.h"
#include "SKL/ABIX/STD/Layout.h"
#include "SKL/ABIX/STD/Optional.h"
#include "SKL/ABIX/STD/OwnedSpan.h"
#include "SKL/ABIX/STD/OwnedString.h"
#include "SKL/ABIX/STD/Owner.h"
#include "SKL/ABIX/STD/Result.h"
#include "SKL/ABIX/STD/Span.h"
#include "SKL/ABIX/STD/String.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace {

using abix_std::detail::align_up;

template<class T>
constexpr std::size_t optional_offset() noexcept {
    return abix_std::detail::align_up(8, alignof(T));
}

template<class T>
constexpr std::size_t optional_align() noexcept {
    return abix_std::detail::max_align(4, alignof(T));
}

template<class T>
constexpr std::size_t optional_size() noexcept {
    return align_up(optional_offset<T>() + sizeof(T), optional_align<T>());
}

template<class T>
constexpr std::size_t result_offset() noexcept {
    return abix_std::detail::align_up(8, alignof(T));
}

template<class T>
constexpr std::size_t result_align() noexcept {
    return abix_std::detail::max_align(4, alignof(T));
}

template<class T>
constexpr std::size_t result_size() noexcept {
    return align_up(result_offset<T>() + sizeof(T), result_align<T>());
}

constexpr std::size_t string_len_offset() noexcept { return align_up(sizeof(void *), alignof(std::uint64_t)); }

constexpr std::size_t string_encoding_offset() noexcept {
    return align_up(string_len_offset() + sizeof(std::uint64_t), alignof(std::uint32_t));
}

constexpr std::size_t string_reserved_offset() noexcept { return string_encoding_offset() + sizeof(std::uint32_t); }

constexpr std::size_t string_align() noexcept {
    const std::size_t pointer = alignof(void *);
    const std::size_t wide = alignof(std::uint64_t);
    return abix_std::detail::max_align(abix_std::detail::max_align(pointer, wide), alignof(std::uint32_t));
}

constexpr std::size_t string_size() noexcept {
    return align_up(string_reserved_offset() + sizeof(std::uint32_t), string_align());
}

template<class T>
constexpr bool optional_header_valid(const abix_std::abix_optional<T> &value) noexcept {
    return value.reserved == 0 && value.discriminant <= 1;
}

template<class T>
constexpr bool result_header_valid(const abix_std::abix_result<T> &value) noexcept {
    return value.reserved == 0;
}

constexpr bool string_header_valid(const abix_str_utf8 &value) noexcept { return value.reserved == 0; }

// The v0.3.x optional bytes: one-byte discriminant, then an int32 payload at
// offset 4 (with implicit padding). Keeping the exact layout here makes the
// old/new reader boundary executable rather than a comment.
struct old_optional_i32 {
    std::uint8_t has_value;
    std::int32_t value;
};

// The v0.3.x result bytes: status then an int32 payload at offset 4.
struct old_result_i32 {
    std::int32_t status;
    std::int32_t value;
};

}   // namespace

TEST_CASE("std_contract.optional_layout_audit", "[std][contract]") {
    using optional_i32 = abix_std::abix_optional<std::int32_t>;
    using optional_i64 = abix_std::abix_optional<std::int64_t>;
    using optional_ptr = abix_std::abix_optional<void *>;

    static_assert(offsetof(optional_i32, discriminant) == 0);
    static_assert(offsetof(optional_i32, reserved) == 4);
    static_assert(offsetof(optional_i32, value) == optional_offset<std::int32_t>());
    static_assert(sizeof(optional_i32) == optional_size<std::int32_t>());
    static_assert(alignof(optional_i32) == optional_align<std::int32_t>());

    static_assert(offsetof(optional_i64, value) == optional_offset<std::int64_t>());
    static_assert(sizeof(optional_i64) == optional_size<std::int64_t>());
    static_assert(alignof(optional_i64) == optional_align<std::int64_t>());

    static_assert(offsetof(optional_ptr, value) == optional_offset<void *>());
    static_assert(sizeof(optional_ptr) == optional_size<void *>());
    static_assert(alignof(optional_ptr) == optional_align<void *>());

    CHECK(sizeof(optional_i32) == 12);
    CHECK(offsetof(optional_i32, value) == 8);
    CHECK(alignof(optional_i32) == 4);

    // The formula is driven by the target ABI, not by a hard-coded width.
    CHECK(offsetof(optional_i64, value) == align_up(8, alignof(std::int64_t)));
    CHECK(sizeof(optional_i64) == optional_size<std::int64_t>());
    CHECK(offsetof(optional_ptr, value) == align_up(8, alignof(void *)));
    CHECK(sizeof(optional_ptr) == optional_size<void *>());
}

TEST_CASE("std_contract.result_layout_audit", "[std][contract]") {
    using result_i32 = abix_std::abix_result<std::int32_t>;
    using result_i64 = abix_std::abix_result<std::int64_t>;
    using result_ptr = abix_std::abix_result<void *>;

    static_assert(offsetof(result_i32, status) == 0);
    static_assert(offsetof(result_i32, reserved) == 4);
    static_assert(offsetof(result_i32, value) == result_offset<std::int32_t>());
    static_assert(sizeof(result_i32) == result_size<std::int32_t>());
    static_assert(alignof(result_i32) == result_align<std::int32_t>());

    static_assert(offsetof(result_i64, value) == result_offset<std::int64_t>());
    static_assert(sizeof(result_i64) == result_size<std::int64_t>());
    static_assert(alignof(result_i64) == result_align<std::int64_t>());

    static_assert(offsetof(result_ptr, value) == result_offset<void *>());
    static_assert(sizeof(result_ptr) == result_size<void *>());
    static_assert(alignof(result_ptr) == result_align<void *>());

    CHECK(sizeof(result_i32) == 12);
    CHECK(offsetof(result_i32, value) == 8);
    CHECK(alignof(result_i32) == 4);

    CHECK(offsetof(result_i64, value) == align_up(8, alignof(std::int64_t)));
    CHECK(sizeof(result_i64) == result_size<std::int64_t>());
    CHECK(offsetof(result_ptr, value) == align_up(8, alignof(void *)));
    CHECK(sizeof(result_ptr) == result_size<void *>());
}

TEST_CASE("std_contract.string_and_span_layout_audit", "[std][contract]") {
    using string_utf8 = abix_std::abix_str<char>;
    using span_i32 = abix_std::abix_span<std::int32_t>;

    static_assert(offsetof(string_utf8, data) == 0);
    static_assert(offsetof(string_utf8, len) == string_len_offset());
    static_assert(offsetof(string_utf8, encoding) == string_encoding_offset());
    static_assert(offsetof(string_utf8, reserved) == string_reserved_offset());
    static_assert(sizeof(string_utf8) == string_size());
    static_assert(alignof(string_utf8) == string_align());

    static_assert(offsetof(span_i32, data) == 0);
    static_assert(offsetof(span_i32, len) == string_len_offset());
    static_assert(sizeof(span_i32) == align_up(string_len_offset() + sizeof(std::uint64_t), string_align()));

    CHECK(sizeof(string_utf8) == string_size());
    CHECK(offsetof(string_utf8, reserved) == string_encoding_offset() + 4);
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
    static_assert(std::is_same<canonical, abix_owner_handle>::value, "owner::handle must be the C ABI type");
    static_assert(sizeof(projected) == sizeof(canonical), "Owner<T> must not change the layout");
    static_assert(alignof(projected) == alignof(canonical), "Owner<T> must not change the alignment");

    CHECK(sizeof(projected) == sizeof(canonical));
    CHECK(alignof(projected) == alignof(canonical));
}

TEST_CASE("std_contract.owner_handle_layout_audit", "[std][contract]") {
    static_assert(offsetof(abix_owner_handle, object) == 0);
    static_assert(offsetof(abix_owner_handle, mode) == sizeof(void *));
    static_assert(offsetof(abix_owner_handle, flags) == sizeof(void *) + 4);
    static_assert(offsetof(abix_owner_handle, destroy) >= sizeof(void *) + 8);
    static_assert(offsetof(abix_owner_handle, module_instance) > offsetof(abix_owner_handle, destroy));
    static_assert(sizeof(void *) != 8 || offsetof(abix_owner_handle, destroy) == 16);
    static_assert(sizeof(void *) != 8 || offsetof(abix_owner_handle, module_instance) == 24);
    static_assert(sizeof(void *) != 8 || sizeof(abix_owner_handle) == 32);
    static_assert(sizeof(void *) != 8 || alignof(abix_owner_handle) == 8);

    if (sizeof(void *) == 8) {
        CHECK(sizeof(abix_owner_handle) == 32);
    } else {
        CHECK(sizeof(abix_owner_handle) == sizeof(void *) + 8 + sizeof(void *) + sizeof(uint64_t));
    }

    CHECK(offsetof(abix_owner_handle, object) == 0);
    CHECK(offsetof(abix_owner_handle, mode) == sizeof(void *));
    CHECK(offsetof(abix_owner_handle, flags) == sizeof(void *) + 4);
    CHECK(abix_owner_handle{}.flags == 0);

    int object = 0;
    const auto borrowed = skl::abix::owner::make_borrowed(&object);
    const auto owned = skl::abix::owner::make_owned(&object, [](void *) {});
    const auto copied = skl::abix::owner::make_copied(&object, [](void *) {});
    CHECK(borrowed.mode == ABIX_OWNER_BORROWED);
    CHECK(owned.mode == ABIX_OWNER_OWNED);
    CHECK(copied.mode == ABIX_OWNER_COPIED);
    CHECK(borrowed.destroy == nullptr);
    CHECK(owned.destroy != nullptr);
    CHECK(copied.destroy != nullptr);
    CHECK(skl::abix::owner::valid(owned));
}

TEST_CASE("std_contract.owned_view_layout_audit", "[std][contract]") {
    using owned_str = abix_owned_str;
    using owned_span = abix_std::abix_owned_span<std::int32_t>;

    static_assert(offsetof(owned_str, view) == 0);
    static_assert(offsetof(owned_str, owner) == sizeof(abix_str_utf8));
    static_assert(sizeof(owned_str) == sizeof(abix_str_utf8) + sizeof(abix_owner_handle));
    static_assert(std::is_same<abix_owned_str_utf8, abix_owned_str>::value);

    static_assert(offsetof(owned_span, view) == 0);
    static_assert(offsetof(owned_span, owner) == sizeof(abix_span_i32));
    static_assert(sizeof(owned_span) == sizeof(abix_span_i32) + sizeof(abix_owner_handle));
    static_assert(std::is_same<abix_owned_span_i32, owned_span>::value);

    CHECK(sizeof(owned_str) == sizeof(abix_str_utf8) + sizeof(abix_owner_handle));
    CHECK(sizeof(owned_span) == sizeof(abix_span_i32) + sizeof(abix_owner_handle));

    // The view and the owner refer to the same allocation; the owner is the only
    // release path.
    char buffer[] = "owned";
    owned_str value{
        {buffer, 5, 0, 0},
        {buffer, ABIX_OWNER_OWNED, 0, nullptr, 0}
    };
    CHECK(value.view.data == value.owner.object);
    CHECK(value.view.len == 5);
    CHECK(value.owner.mode == ABIX_OWNER_OWNED);
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
        const result_i32 value{status, 0, 7};
        CHECK(value.status == status);
        CHECK(value.reserved == 0);
        CHECK(value.value == 7);
        CHECK(result_header_valid(value));
    }

    // An unknown status is carried through; the [[error]] contract decides its meaning.
    const result_i32 unknown{123'456'789, 0, -42};
    CHECK(unknown.status == 123'456'789);
    CHECK(unknown.value == -42);
    CHECK(result_header_valid(unknown));
}

TEST_CASE("std_contract.optional_discriminant_contract", "[std][contract]") {
    using optional_i32 = abix_std::abix_optional<std::int32_t>;
    using discriminant = decltype(optional_i32::discriminant);

    static_assert(std::is_same<discriminant, std::uint32_t>::value, "abix_optional discriminant must stay 32 bits");
    static_assert(sizeof(optional_i32{}.discriminant) == 4);

    const optional_i32 absent{0, 0, 0};
    const optional_i32 present{1, 0, 42};
    CHECK(absent.discriminant == 0);
    CHECK(present.discriminant == 1);
    CHECK(present.value == 42);
    CHECK(optional_header_valid(absent));
    CHECK(optional_header_valid(present));
}

TEST_CASE("std_contract.optional_invalid_header_is_rejected", "[std][contract]") {
    using optional_i32 = abix_std::abix_optional<std::int32_t>;

    // A consumer rejects every discriminant other than 0 or 1. The ABI stores
    // the value as-is; the runtime does not silently normalize it.
    const optional_i32 invalid_discriminant{2, 0, 7};
    CHECK(invalid_discriminant.discriminant == 2);
    CHECK_FALSE(optional_header_valid(invalid_discriminant));

    // A non-zero reserved word is a layout error, not an ignorable field.
    const optional_i32 invalid_reserved{1, 1, 7};
    CHECK(invalid_reserved.reserved == 1);
    CHECK_FALSE(optional_header_valid(invalid_reserved));

    // None must never expose the payload: the bytes may be anything.
    const optional_i32 none_with_garbage{0, 0, 99};
    CHECK(optional_header_valid(none_with_garbage));
    CHECK(none_with_garbage.discriminant == 0);
}

TEST_CASE("std_contract.result_invalid_reserved_is_rejected", "[std][contract]") {
    using result_i32 = abix_std::abix_result<std::int32_t>;

    const result_i32 invalid_reserved{0, 1, 7};
    CHECK(invalid_reserved.reserved == 1);
    CHECK_FALSE(result_header_valid(invalid_reserved));

    const result_i32 unknown_status{99, 0, 7};
    CHECK(result_header_valid(unknown_status));
}

TEST_CASE("std_contract.string_reserved_is_rejected", "[std][contract]") {
    const char text[] = "hi";
    const abix_str_utf8 valid{text, 2, static_cast<std::uint32_t>(abix_std::Encoding::utf8), 0};
    CHECK(string_header_valid(valid));

    const abix_str_utf8 invalid{text, 2, static_cast<std::uint32_t>(abix_std::Encoding::utf8), 1};
    CHECK_FALSE(string_header_valid(invalid));
}

TEST_CASE("std_contract.v0_3_optional_bytes_are_not_a_v0_4_value", "[std][contract]") {
    static_assert(sizeof(old_optional_i32) == 8);

    const old_optional_i32 old{1, 42};
    abix_std::abix_optional<std::int32_t> current{};
    std::memcpy(&current, &old, sizeof(old));

    // The old payload sits in the new reserved word, so a conforming reader
    // fails closed instead of reading a partially overlapped value.
    CHECK(current.reserved == 42);
    CHECK_FALSE(optional_header_valid(current));
}

TEST_CASE("std_contract.v0_3_result_bytes_are_not_a_v0_4_value", "[std][contract]") {
    static_assert(sizeof(old_result_i32) == 8);

    const old_result_i32 old{7, 42};
    abix_std::abix_result<std::int32_t> current{};
    std::memcpy(&current, &old, sizeof(old));

    CHECK(current.status == 7);
    CHECK(current.reserved == 42);
    CHECK_FALSE(result_header_valid(current));

    // The old record is also 8 bytes while the v0.4 record is 12: a reader
    // must not read the payload from a short v0.3 record at all.
    CHECK(sizeof(old_result_i32) < sizeof(current));
}

TEST_CASE("std_contract.callback_owned_incomplete_entry_is_kept_for_validation", "[std][contract]") {
    abix_callback_owned incomplete{};
    incomplete.invoke = [](void *) -> std::int32_t { return 0; };

    CHECK(incomplete.invoke != nullptr);
    CHECK(incomplete.context == nullptr);
    CHECK(incomplete.release == nullptr);

    // An owned callback is valid only when invoke and release are both set.
    // The consumer must reject an incomplete entry instead of invoking it.
}
