#include "test_common.hpp"

#include "SKL/ABIX/Error/Domain.h"
#include "SKL/ABIX/Error/ErrorCode.h"
#include "SKL/ABIX/Error/Mapping.h"

#include <cstddef>
#include <limits>
#include <type_traits>

TEST_CASE("error_code.layout_is_frozen", "[error][abi]") {
    using skl::abix::ErrorCode;

    static_assert(sizeof(ErrorCode) == 16, "ErrorCode must stay 16 bytes");
    static_assert(alignof(ErrorCode) == 8, "ErrorCode must stay 8-byte aligned");
    static_assert(offsetof(ErrorCode, domain) == 0, "domain offset changed");
    static_assert(offsetof(ErrorCode, value) == 8, "value offset changed");
    static_assert(offsetof(ErrorCode, reserved) == 12, "reserved offset changed");
    static_assert(std::is_standard_layout<ErrorCode>::value, "ErrorCode must stay standard-layout");
    static_assert(std::is_trivial<ErrorCode>::value, "ErrorCode must stay trivial");
    static_assert(std::is_trivially_copyable<ErrorCode>::value, "ErrorCode must stay trivially copyable");
    static_assert(std::is_aggregate<ErrorCode>::value, "ErrorCode must stay an aggregate");

    CHECK(sizeof(ErrorCode) == 16);
    CHECK(alignof(ErrorCode) == 8);
    CHECK(offsetof(ErrorCode, domain) == 0);
    CHECK(offsetof(ErrorCode, value) == 8);
    CHECK(offsetof(ErrorCode, reserved) == 12);
}

TEST_CASE("error_code.success_is_canonical", "[error][abi]") {
    using namespace skl::abix;

    const ErrorCode ok = success();
    CHECK(ok.domain == invalid_domain);
    CHECK(ok.value == 0);
    CHECK(ok.reserved == 0);
    CHECK(is_success(ok));
    CHECK(is_canonical(ok));
}

TEST_CASE("error_code.canonical_rules", "[error][abi]") {
    using namespace skl::abix;

    SECTION("a zero value with a domain is a success but not canonical") {
        const ErrorCode code{call_domain, 0, 0};
        CHECK(is_success(code));
        CHECK(!is_canonical(code));
    }

    SECTION("a named failure is canonical") {
        const ErrorCode code{call_domain, 1, 0};
        CHECK(!is_success(code));
        CHECK(is_canonical(code));
    }

    SECTION("a failure without a domain is not canonical") {
        const ErrorCode code{invalid_domain, 1, 0};
        CHECK(!is_success(code));
        CHECK(!is_canonical(code));
    }

    SECTION("a non-zero reserved field is not canonical") {
        CHECK(!is_canonical(ErrorCode{call_domain, 1, 1}));
        CHECK(!is_canonical(ErrorCode{invalid_domain, 0, 1}));
    }
}

TEST_CASE("error_code.domain_allocation_is_stable", "[error][abi]") {
    using namespace skl::abix;

    CHECK(abix_module_id == 0x002);
    CHECK(call_domain == 0x1002000000000001ULL);
    CHECK(interface_domain == 0x1002000000000002ULL);
    CHECK(module_domain == 0x1002000000000003ULL);
    CHECK(manifest_domain == 0x1002000000000004ULL);
    CHECK(lifecycle_domain == 0x1002000000000005ULL);

    CHECK(domain_owner(call_domain) == owner_skl);
    CHECK(domain_module(call_domain) == abix_module_id);
    CHECK(domain_local(call_domain) == 1);
    CHECK(domain_local(lifecycle_domain) == 5);
    CHECK(is_skl_domain(call_domain));
    CHECK(!is_skl_domain(invalid_domain));

    // WCAS already owns module 0x001; ABIX must never collide with it.
    CHECK(make_skl_domain(0x001, 1) != call_domain);
}

TEST_CASE("error_code.maps_call_error", "[error][abi]") {
    using namespace skl::abix;
    using skl::abix::dll::CallError;

    const ErrorCode ok = to_error_code(CallError::none);
    CHECK(ok.domain == invalid_domain);
    CHECK(ok.value == 0);
    CHECK(ok.reserved == 0);
    CHECK(is_success(ok));
    CHECK(is_canonical(ok));

    const ErrorCode failure = to_error_code(CallError::not_found);
    CHECK(failure.domain == call_domain);
    CHECK(failure.value == static_cast<int32_t>(CallError::not_found));
    CHECK(failure.reserved == 0);
    CHECK(!is_success(failure));
    CHECK(is_canonical(failure));

    const ErrorCode unknown = to_error_code(static_cast<CallError>(250));
    CHECK(unknown.domain == call_domain);
    CHECK(unknown.value == 250);
    CHECK(!is_success(unknown));
    CHECK(is_canonical(unknown));
}

TEST_CASE("error_code.maps_interface_status", "[error][abi]") {
    using namespace skl::abix;
    using skl::abix::poly::InterfaceStatus;

    const ErrorCode ok = to_error_code(InterfaceStatus::ok);
    CHECK(ok.domain == invalid_domain);
    CHECK(ok.value == 0);
    CHECK(is_success(ok));
    CHECK(is_canonical(ok));

    const ErrorCode failure = to_error_code(InterfaceStatus::signature_mismatch);
    CHECK(failure.domain == interface_domain);
    CHECK(failure.value == static_cast<int32_t>(InterfaceStatus::signature_mismatch));
    CHECK(is_canonical(failure));

    const ErrorCode unknown = to_error_code(static_cast<InterfaceStatus>(0xFFFFFFFFu));
    CHECK(unknown.domain == interface_domain);
    CHECK(unknown.value == -1);
    CHECK(!is_success(unknown));
    CHECK(is_canonical(unknown));
    CHECK(static_cast<uint32_t>(unknown.value) == 0xFFFFFFFFu);

    const ErrorCode high_bit = to_error_code(static_cast<InterfaceStatus>(0x80000000u));
    CHECK(high_bit.value == std::numeric_limits<int32_t>::min());
    CHECK(static_cast<uint32_t>(high_bit.value) == 0x80000000u);
    CHECK(is_canonical(high_bit));
}

TEST_CASE("error_code.maps_module_status", "[error][abi]") {
    using namespace skl::abix;
    using skl::abix::poly::ModuleStatus;

    const ErrorCode ok = to_error_code(ModuleStatus::ok);
    CHECK(ok.domain == invalid_domain);
    CHECK(ok.value == 0);
    CHECK(is_canonical(ok));

    const ErrorCode failure = to_error_code(ModuleStatus::busy);
    CHECK(failure.domain == module_domain);
    CHECK(failure.value == static_cast<int32_t>(ModuleStatus::busy));
    CHECK(!is_success(failure));
    CHECK(is_canonical(failure));

    const ErrorCode unknown = to_error_code(static_cast<ModuleStatus>(250));
    CHECK(unknown.domain == module_domain);
    CHECK(unknown.value == 250);
    CHECK(is_canonical(unknown));
}

TEST_CASE("error_code.maps_manifest_status", "[error][abi]") {
    using namespace skl::abix;
    using skl::abix::poly::ManifestStatus;

    const ErrorCode ok = to_error_code(ManifestStatus::ok);
    CHECK(ok.domain == invalid_domain);
    CHECK(ok.value == 0);
    CHECK(is_canonical(ok));

    const ErrorCode failure = to_error_code(ManifestStatus::invalid_binding);
    CHECK(failure.domain == manifest_domain);
    CHECK(failure.value == static_cast<int32_t>(ManifestStatus::invalid_binding));
    CHECK(!is_success(failure));
    CHECK(is_canonical(failure));

    const ErrorCode unknown = to_error_code(static_cast<ManifestStatus>(250));
    CHECK(unknown.domain == manifest_domain);
    CHECK(unknown.value == 250);
    CHECK(is_canonical(unknown));
}
