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
#ifndef SKL_ABIX_ERROR_MAPPING_H
#define SKL_ABIX_ERROR_MAPPING_H

#include "SKL/ABIX/Error/ErrorCode.h"
#include "SKL/ABIX/DLL/Object.h"
#include "SKL/ABIX/Interface/Contract.h"
#include "SKL/ABIX/Interface/Module.h"
#include "SKL/ABIX/Interface/ModuleRegistry.h"

namespace skl::abix {

namespace error_detail {

// Preserve the low 32 bits of an enum value in the signed `value` field without
// relying on an implementation-defined unsigned-to-signed conversion. Unknown
// values therefore keep their numeric identity instead of being folded into a
// sentinel.
[[nodiscard]] constexpr int32_t as_error_value(uint32_t value) noexcept {
    return value <= 0x7FFFFFFFU ? static_cast<int32_t>(value) : -static_cast<int32_t>(0xFFFFFFFFU - value) - 1;
}

}   // namespace error_detail

// A successful subsystem status maps to the canonical success (no domain), not
// to a domain-qualified zero. Failure values keep the enum's numeric value and
// gain the subsystem's domain.
[[nodiscard]] constexpr ErrorCode to_error_code(dll::CallError error) noexcept {
    return error == dll::CallError::none
             ? success()
             : ErrorCode{call_domain, error_detail::as_error_value(static_cast<uint32_t>(error)), 0};
}

[[nodiscard]] constexpr ErrorCode to_error_code(poly::InterfaceStatus status) noexcept {
    return status == poly::InterfaceStatus::ok
             ? success()
             : ErrorCode{interface_domain, error_detail::as_error_value(static_cast<uint32_t>(status)), 0};
}

[[nodiscard]] constexpr ErrorCode to_error_code(poly::ModuleStatus status) noexcept {
    return status == poly::ModuleStatus::ok
             ? success()
             : ErrorCode{module_domain, error_detail::as_error_value(static_cast<uint32_t>(status)), 0};
}

[[nodiscard]] constexpr ErrorCode to_error_code(poly::ManifestStatus status) noexcept {
    return status == poly::ManifestStatus::ok
             ? success()
             : ErrorCode{manifest_domain, error_detail::as_error_value(static_cast<uint32_t>(status)), 0};
}

static_assert(is_success(to_error_code(dll::CallError::none)), "CallError::none must map to success");
static_assert(!is_success(to_error_code(dll::CallError::not_found)), "CallError failures must not map to success");
static_assert(to_error_code(dll::CallError::not_found).domain == call_domain, "CallError must use the call domain");
static_assert(to_error_code(dll::CallError::not_found).value == 2, "CallError values must be preserved");
static_assert(is_success(to_error_code(poly::InterfaceStatus::ok)), "InterfaceStatus::ok must map to success");
static_assert(to_error_code(poly::InterfaceStatus::signature_mismatch).domain == interface_domain,
    "InterfaceStatus must use the interface domain");
static_assert(is_success(to_error_code(poly::ModuleStatus::ok)), "ModuleStatus::ok must map to success");
static_assert(
    to_error_code(poly::ModuleStatus::busy).domain == module_domain, "ModuleStatus must use the module domain");
static_assert(is_success(to_error_code(poly::ManifestStatus::ok)), "ManifestStatus::ok must map to success");
static_assert(to_error_code(poly::ManifestStatus::invalid_binding).domain == manifest_domain,
    "ManifestStatus must use the manifest domain");

}   // namespace skl::abix

#endif   // SKL_ABIX_ERROR_MAPPING_H
