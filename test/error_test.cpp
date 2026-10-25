#include "test_common.hpp"
#include "error_abi_prototype.h"

#include <string>
#include <thread>

TEST_CASE("error.call_error_strings_are_stable", "[error][dll]") {
    using skl::abix::dll::CallError;
    using skl::abix::dll::to_string;

    CHECK(std::string(to_string(CallError::none)) == "none");
    CHECK(std::string(to_string(CallError::not_loaded)) == "not_loaded");
    CHECK(std::string(to_string(CallError::not_found)) == "not_found");
    CHECK(std::string(to_string(CallError::sig_mismatch)) == "sig_mismatch");
    CHECK(std::string(to_string(CallError::version_mismatch)) == "version_mismatch");
    CHECK(std::string(to_string(CallError::stale_handle)) == "stale_handle");
    CHECK(std::string(to_string(CallError::table_changed)) == "table_changed");
    CHECK(std::string(to_string(CallError::invalid)) == "invalid");
    CHECK(std::string(to_string(CallError::load_failed)) == "load_failed");
    CHECK(std::string(to_string(CallError::unloading)) == "unloading");
    CHECK(std::string(to_string(CallError::lock_timeout)) == "lock_timeout");
    CHECK(std::string(to_string(static_cast<CallError>(255))) == "unknown");
}

TEST_CASE("error.last_error_is_thread_local", "[error][dll]") {
    using skl::abix::dll::CallError;
    using skl::abix::dll::last_error;

    auto &main_error = last_error();
    const CallError previous = main_error;
    main_error = CallError::not_found;

    CallError worker_initial = CallError::invalid;
    CallError worker_after_set = CallError::invalid;
    std::thread worker([&] {
        worker_initial = last_error();
        last_error() = CallError::sig_mismatch;
        worker_after_set = last_error();
    });
    worker.join();

    CHECK(worker_initial == CallError::none);
    CHECK(worker_after_set == CallError::sig_mismatch);
    CHECK(last_error() == CallError::not_found);

    main_error = previous;
}

TEST_CASE("error.status_strings_are_stable", "[error][interface]") {
    using skl::abix::poly::InterfaceStatus;
    using skl::abix::poly::ManifestStatus;
    using skl::abix::poly::ModuleStatus;

    CHECK(std::string(skl::abix::poly::to_string(InterfaceStatus::ok)) == "ok");
    CHECK(std::string(skl::abix::poly::to_string(InterfaceStatus::interface_not_found)) == "interface_not_found");
    CHECK(std::string(skl::abix::poly::to_string(InterfaceStatus::signature_mismatch)) == "signature_mismatch");
    CHECK(std::string(skl::abix::poly::to_string(static_cast<InterfaceStatus>(255))) == "unknown");

    CHECK(std::string(skl::abix::poly::to_string(ManifestStatus::ok)) == "ok");
    CHECK(std::string(skl::abix::poly::to_string(ManifestStatus::invalid_binding)) == "invalid_binding");
    CHECK(std::string(skl::abix::poly::to_string(ManifestStatus::too_many_methods)) == "too_many_methods");
    CHECK(std::string(skl::abix::poly::to_string(static_cast<ManifestStatus>(255))) == "unknown");

    CHECK(std::string(skl::abix::poly::to_string(ModuleStatus::ok)) == "ok");
    CHECK(std::string(skl::abix::poly::to_string(ModuleStatus::duplicate_interface)) == "duplicate_interface");
    CHECK(std::string(skl::abix::poly::to_string(ModuleStatus::busy)) == "busy");
    CHECK(std::string(skl::abix::poly::to_string(static_cast<ModuleStatus>(255))) == "unknown");
}

TEST_CASE("error.error_abi_prototype_layout_and_mapping", "[error][abi]") {
    using namespace skl::abix::error_abi::prototype;

    static_assert(sizeof(ErrorCode) == 16);
    static_assert(alignof(ErrorCode) == 8);
    static_assert(std::is_trivial_v<ErrorCode>);
    static_assert(std::is_standard_layout_v<ErrorCode>);

    const ErrorCode call_ok = to_error_code(skl::abix::dll::CallError::none);
    CHECK(call_ok.domain == domain_call_error);
    CHECK(call_ok.code == 0);
    CHECK(call_ok.flags == 0);
    CHECK(is_success(call_ok));

    const ErrorCode call_error = to_error_code(skl::abix::dll::CallError::not_found);
    CHECK(call_error.domain == domain_call_error);
    CHECK(call_error.code == static_cast<uint32_t>(skl::abix::dll::CallError::not_found));
    CHECK(!is_success(call_error));

    const ErrorCode interface_ok = to_error_code(skl::abix::poly::InterfaceStatus::ok);
    CHECK(interface_ok.domain == domain_interface_status);
    CHECK(interface_ok.code == 0);
    CHECK(is_success(interface_ok));

    const ErrorCode interface_error = to_error_code(skl::abix::poly::InterfaceStatus::signature_mismatch);
    CHECK(interface_error.domain == domain_interface_status);
    CHECK(interface_error.code == static_cast<uint32_t>(skl::abix::poly::InterfaceStatus::signature_mismatch));
    CHECK(!is_success(interface_error));

    const ErrorCode unknown_call = to_error_code(static_cast<skl::abix::dll::CallError>(255));
    CHECK(unknown_call.domain == domain_call_error);
    CHECK(unknown_call.code == 255);
    CHECK((unknown_call.flags & flag_unknown) != 0);
    CHECK(!is_success(unknown_call));

    const ErrorCode unknown_interface = to_error_code(static_cast<skl::abix::poly::InterfaceStatus>(255));
    CHECK(unknown_interface.domain == domain_interface_status);
    CHECK(unknown_interface.code == 255);
    CHECK((unknown_interface.flags & flag_unknown) != 0);
    CHECK(!is_success(unknown_interface));

    const ErrorCode unknown_domain{99, 0, 0};
    CHECK(!is_success(unknown_domain));

    const ErrorCode unknown_code{domain_call_error, code_unknown, 0};
    CHECK(!is_success(unknown_code));
}
