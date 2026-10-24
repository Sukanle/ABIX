#ifndef SKL_ABIX_TEST_ERROR_ABI_PROTOTYPE_H
#define SKL_ABIX_TEST_ERROR_ABI_PROTOTYPE_H

// Test-only Error ABI prototype. This header is deliberately outside the
// public ABIX headers: it exists to validate fixed-layout error identity,
// domain mapping and unknown-code behavior before the v0.3.0 specification.
// Do not include it from SKL/ABIX/ABIX.h and do not install it.

#include "SKL/ABIX/DLL/Object.h"
#include "SKL/ABIX/Interface/Contract.h"

#include <cstdint>
#include <type_traits>

namespace skl::abix::error_abi::prototype {

using dll::CallError;
using poly::InterfaceStatus;

struct ErrorCode final {
    std::uint64_t domain;
    std::uint32_t code;
    std::uint32_t flags;
};

inline constexpr std::uint64_t domain_call_error = 1;
inline constexpr std::uint64_t domain_interface_status = 2;

inline constexpr std::uint32_t code_unknown = UINT32_MAX;
inline constexpr std::uint32_t flag_unknown = 1u << 0;

static_assert(sizeof(ErrorCode) == 16, "ErrorCode is a 16-byte fixed-layout record");
static_assert(alignof(ErrorCode) == 8, "ErrorCode is 8-byte aligned");
static_assert(std::is_trivial_v<ErrorCode>, "ErrorCode must stay trivial");
static_assert(std::is_standard_layout_v<ErrorCode>, "ErrorCode must stay standard-layout");

namespace detail {
constexpr bool known(CallError error) noexcept {
    switch (error) {
        case CallError::none:
        case CallError::not_loaded:
        case CallError::not_found:
        case CallError::sig_mismatch:
        case CallError::version_mismatch:
        case CallError::stale_handle:
        case CallError::table_changed:
        case CallError::invalid:
        case CallError::load_failed:
        case CallError::unloading:
        case CallError::lock_timeout:     return true;
    }
    return false;
}

constexpr bool known(InterfaceStatus status) noexcept {
    switch (status) {
        case InterfaceStatus::ok:
        case InterfaceStatus::invalid_table:
        case InterfaceStatus::duplicate_implementation:
        case InterfaceStatus::capacity_exceeded:
        case InterfaceStatus::interface_not_found:
        case InterfaceStatus::version_mismatch:
        case InterfaceStatus::method_not_found:
        case InterfaceStatus::method_capacity_exceeded:
        case InterfaceStatus::signature_mismatch:       return true;
    }
    return false;
}

constexpr ErrorCode make(std::uint64_t domain, std::uint32_t code, bool is_known) noexcept {
    return ErrorCode{domain, code, is_known ? 0u : flag_unknown};
}
}   // namespace detail

constexpr ErrorCode to_error_code(CallError error) noexcept {
    return detail::make(domain_call_error, static_cast<std::uint32_t>(error), detail::known(error));
}

constexpr ErrorCode to_error_code(InterfaceStatus status) noexcept {
    return detail::make(domain_interface_status, static_cast<std::uint32_t>(status), detail::known(status));
}

constexpr bool is_success(ErrorCode code) noexcept {
    if ((code.flags & flag_unknown) != 0) return false;
    if (code.domain == domain_call_error) return code.code == static_cast<std::uint32_t>(CallError::none);
    if (code.domain == domain_interface_status) return code.code == static_cast<std::uint32_t>(InterfaceStatus::ok);
    return false;
}

}   // namespace skl::abix::error_abi::prototype

#endif   // SKL_ABIX_TEST_ERROR_ABI_PROTOTYPE_H
