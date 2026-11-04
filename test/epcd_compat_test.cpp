#include "test_common.hpp"

#include "SKL/ABIX/Error/Domain.h"
#include "SKL/ABIX/Error/ErrorCode.h"

#include "SKL/EPCD/Core/Domain.h"
#include "SKL/EPCD/Core/ErrorCode.h"

#include <cstddef>

// Optional compatibility test: ABIX must never include or link EPCD at runtime,
// but its error identity has to be convertible to EPCD's core type field by
// field. This translation unit is built only when EPCD headers are available.
namespace {

constexpr skl::abix::ErrorCode to_abix(skl::epcd::ErrorCode code) noexcept {
    return skl::abix::ErrorCode{code.domain, code.value, 0};
}

constexpr skl::epcd::ErrorCode to_epcd(skl::abix::ErrorCode code) noexcept {
    return skl::epcd::ErrorCode{code.domain, code.value};
}

}   // namespace

TEST_CASE("epcd.error_code_layout_is_compatible", "[error][epcd]") {
    using abix_code = skl::abix::ErrorCode;
    using epcd_code = skl::epcd::ErrorCode;

    static_assert(sizeof(abix_code) == sizeof(epcd_code), "ABIX and EPCD ErrorCode sizes differ");
    static_assert(alignof(abix_code) == alignof(epcd_code), "ABIX and EPCD ErrorCode alignment differs");
    static_assert(offsetof(abix_code, domain) == offsetof(epcd_code, domain), "domain offset differs");
    static_assert(offsetof(abix_code, value) == offsetof(epcd_code, value), "value offset differs");
    static_assert(offsetof(abix_code, reserved) == offsetof(epcd_code, value) + sizeof(epcd_code::value),
        "ABIX reserved does not occupy the EPCD padding");

    CHECK(sizeof(abix_code) == sizeof(epcd_code));
    CHECK(offsetof(abix_code, domain) == offsetof(epcd_code, domain));
    CHECK(offsetof(abix_code, value) == offsetof(epcd_code, value));
    CHECK(offsetof(abix_code, reserved) == offsetof(epcd_code, value) + sizeof(epcd_code::value));
}

TEST_CASE("epcd.domain_masks_and_allocations_match", "[error][epcd]") {
    static_assert(skl::abix::domain_owner_mask == skl::epcd::domain_owner_mask, "owner mask differs");
    static_assert(skl::abix::domain_module_mask == skl::epcd::domain_module_mask, "module mask differs");
    static_assert(skl::abix::domain_local_mask == skl::epcd::domain_local_mask, "local mask differs");
    static_assert(skl::abix::domain_owner_shift == skl::epcd::domain_owner_shift, "owner shift differs");
    static_assert(skl::abix::domain_module_shift == skl::epcd::domain_module_shift, "module shift differs");
    static_assert(skl::abix::owner_skl == skl::epcd::owner_skl, "SKL owner value differs");

    static_assert(skl::abix::call_domain == skl::epcd::make_skl_domain(0x002, 1), "call domain differs");
    static_assert(skl::abix::interface_domain == skl::epcd::make_skl_domain(0x002, 2), "interface domain differs");
    static_assert(skl::abix::module_domain == skl::epcd::make_skl_domain(0x002, 3), "module domain differs");
    static_assert(skl::abix::manifest_domain == skl::epcd::make_skl_domain(0x002, 4), "manifest domain differs");
    static_assert(skl::abix::lifecycle_domain == skl::epcd::make_skl_domain(0x002, 5), "lifecycle domain differs");

    CHECK(skl::abix::call_domain == skl::epcd::make_skl_domain(0x002, 1));
    CHECK(skl::abix::domain_module(skl::abix::call_domain) == 0x002);
}

TEST_CASE("epcd.conversion_is_field_by_field", "[error][epcd]") {
    const skl::abix::ErrorCode abix_ok = skl::abix::success();
    const skl::epcd::ErrorCode epcd_ok = to_epcd(abix_ok);
    CHECK(epcd_ok.domain == 0);
    CHECK(epcd_ok.value == 0);

    const skl::abix::ErrorCode abix_fail{skl::abix::call_domain, 7, 0};
    const skl::epcd::ErrorCode epcd_fail = to_epcd(abix_fail);
    CHECK(epcd_fail.domain == abix_fail.domain);
    CHECK(epcd_fail.value == abix_fail.value);

    const skl::abix::ErrorCode round_trip = to_abix(epcd_fail);
    CHECK(round_trip.domain == abix_fail.domain);
    CHECK(round_trip.value == abix_fail.value);
    CHECK(round_trip.reserved == 0);
    CHECK(skl::abix::is_canonical(round_trip));
}
