#include "test_common.hpp"

#include "SKL/ABIX/Interface/LeaseABI.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

using skl::abix::is_success;
using skl::abix::to_error_code;
using skl::abix::poly::ModuleControl;

// Test-only Host registry. Production ABIX does not provide this: the Host
// (AMC) owns the registry and implements the same function-table contract.
struct MockHost {
    std::mutex mutex;
    bool closing = false;
    bool destroyed = false;
    uint64_t next_token = 1;
    std::unordered_map<abix_module_instance, ModuleControl *> modules;
    std::unordered_map<abix_lease, ModuleControl *> leases;
};

MockHost *host_of(abix_lease_registry *registry) noexcept { return reinterpret_cast<MockHost *>(registry); }

abix_status mock_registry_create(void *context, abix_lease_registry **out_registry) {
    if (context == nullptr || out_registry == nullptr) return ABIX_ERROR_INVALID;
    *out_registry = reinterpret_cast<abix_lease_registry *>(context);
    return ABIX_SUCCESS;
}

abix_status mock_registry_destroy(void *context, abix_lease_registry *registry) {
    if (context == nullptr || registry == nullptr) return ABIX_ERROR_INVALID;
    MockHost &host = *host_of(registry);
    std::lock_guard<std::mutex> lock(host.mutex);
    if (host.destroyed) return ABIX_ERROR_INVALID;
    host.closing = true;
    if (!host.leases.empty()) return ABIX_ERROR_BUSY;
    host.destroyed = true;
    return ABIX_SUCCESS;
}

abix_status mock_acquire(
    void *context, abix_lease_registry *registry, abix_module_instance module, abix_lease *out_lease) {
    if (context == nullptr || registry == nullptr || out_lease == nullptr) return ABIX_ERROR_INVALID;
    *out_lease = ABIX_LEASE_INVALID;
    MockHost &host = *host_of(registry);
    std::lock_guard<std::mutex> lock(host.mutex);
    if (host.destroyed || host.closing) return ABIX_ERROR_UNLOADING;
    auto module_it = host.modules.find(module);
    if (module_it == host.modules.end()) return ABIX_ERROR_NOT_FOUND;
    ModuleControl &control = *module_it->second;
    if (!skl::abix::poly::try_acquire_lease(control)) return ABIX_ERROR_UNLOADING;
    if (host.next_token == 0) {
        skl::abix::poly::release_lease(control);
        return ABIX_ERROR_INVALID;
    }
    const abix_lease token = host.next_token;
    host.next_token = token == UINT64_MAX ? 0 : token + 1;
    host.leases.emplace(token, &control);
    *out_lease = token;
    return ABIX_SUCCESS;
}

void mock_release(void *context, abix_lease_registry *registry, abix_lease lease) {
    if (context == nullptr || registry == nullptr || lease == ABIX_LEASE_INVALID) return;
    MockHost &host = *host_of(registry);
    std::lock_guard<std::mutex> lock(host.mutex);
    auto it = host.leases.find(lease);
    if (it == host.leases.end()) return;
    ModuleControl *control = it->second;
    host.leases.erase(it);
    skl::abix::poly::release_lease(*control);
}

abix_lease_api make_api(MockHost &host) {
    return abix_lease_api{
        ABIX_LEASE_API_VERSION,
        sizeof(abix_lease_api),
        &host,
        &mock_registry_create,
        &mock_registry_destroy,
        &mock_acquire,
        &mock_release,
    };
}

void make_active_control(MockHost &host, ModuleControl &control, const char *name) {
    control.name = name;
    control.id = skl::abix::poly::next_module_instance_id();
    REQUIRE(skl::abix::poly::activate_module(control));
    host.modules.emplace(control.id, &control);
}

}   // namespace

TEST_CASE("lease_abi.status_mapping_is_stable", "[lease][abi]") {
    using namespace skl::abix;

    CHECK(is_success(ABIX_SUCCESS));
    CHECK(!is_success(ABIX_ERROR_INVALID));

    CHECK(to_error_code(ABIX_SUCCESS).domain == invalid_domain);
    CHECK(to_error_code(ABIX_SUCCESS).value == 0);

    CHECK(to_error_code(ABIX_ERROR_INVALID).domain == call_domain);
    CHECK(to_error_code(ABIX_ERROR_INVALID).value == 7);
    CHECK(to_error_code(ABIX_ERROR_NOT_FOUND).domain == module_domain);
    CHECK(to_error_code(ABIX_ERROR_NOT_FOUND).value == 7);
    CHECK(to_error_code(ABIX_ERROR_STALE_HANDLE).domain == call_domain);
    CHECK(to_error_code(ABIX_ERROR_STALE_HANDLE).value == 5);
    CHECK(to_error_code(ABIX_ERROR_BUSY).domain == module_domain);
    CHECK(to_error_code(ABIX_ERROR_BUSY).value == 8);
    CHECK(to_error_code(ABIX_ERROR_UNLOADING).domain == call_domain);
    CHECK(to_error_code(ABIX_ERROR_UNLOADING).value == 9);

    const abix_status unknown = static_cast<abix_status>(99);
    CHECK(to_error_code(unknown).domain == call_domain);
    CHECK(to_error_code(unknown).value == 99);
    CHECK(!is_success(unknown));
}

TEST_CASE("lease_abi.function_table_and_registry_lifecycle", "[lease][abi]") {
    MockHost host;
    const abix_lease_api api = make_api(host);
    CHECK(api.abi_version == ABIX_LEASE_API_VERSION);
    CHECK(api.struct_size == sizeof(abix_lease_api));
    CHECK(api.acquire != nullptr);
    CHECK(api.release != nullptr);

    abix_lease_registry *registry = nullptr;
    REQUIRE(api.registry_create(api.host_context, &registry) == ABIX_SUCCESS);
    REQUIRE(registry != nullptr);

    ModuleControl control;
    make_active_control(host, control, "lease-lifecycle");
    abix_lease lease = ABIX_LEASE_INVALID;
    REQUIRE(api.acquire(api.host_context, registry, control.id, &lease) == ABIX_SUCCESS);
    CHECK(lease != ABIX_LEASE_INVALID);
    CHECK(control.leases.load() == 1);

    // A live lease blocks registry destruction.
    REQUIRE(api.registry_destroy(api.host_context, registry) == ABIX_ERROR_BUSY);
    api.release(api.host_context, registry, lease);
    CHECK(control.leases.load() == 0);
    REQUIRE(api.registry_destroy(api.host_context, registry) == ABIX_SUCCESS);
}

TEST_CASE("lease_abi.acquire_release_and_duplicate_release", "[lease][abi]") {
    MockHost host;
    const abix_lease_api api = make_api(host);
    abix_lease_registry *registry = nullptr;
    REQUIRE(api.registry_create(api.host_context, &registry) == ABIX_SUCCESS);

    ModuleControl control;
    make_active_control(host, control, "lease-token");
    abix_lease first = ABIX_LEASE_INVALID;
    REQUIRE(api.acquire(api.host_context, registry, control.id, &first) == ABIX_SUCCESS);
    CHECK(control.leases.load() == 1);

    api.release(api.host_context, registry, first);
    CHECK(control.leases.load() == 0);

    // Duplicate release is a no-op.
    api.release(api.host_context, registry, first);
    CHECK(control.leases.load() == 0);

    // Tokens are not reused within a registry lifetime.
    abix_lease second = ABIX_LEASE_INVALID;
    REQUIRE(api.acquire(api.host_context, registry, control.id, &second) == ABIX_SUCCESS);
    CHECK(second != ABIX_LEASE_INVALID);
    CHECK(second != first);
    api.release(api.host_context, registry, second);
    CHECK(control.leases.load() == 0);
}

TEST_CASE("lease_abi.draining_blocks_new_leases_and_allows_destroy", "[lease][abi]") {
    MockHost host;
    const abix_lease_api api = make_api(host);
    abix_lease_registry *registry = nullptr;
    REQUIRE(api.registry_create(api.host_context, &registry) == ABIX_SUCCESS);

    ModuleControl control;
    make_active_control(host, control, "lease-draining");
    abix_lease lease = ABIX_LEASE_INVALID;
    REQUIRE(api.acquire(api.host_context, registry, control.id, &lease) == ABIX_SUCCESS);

    REQUIRE(skl::abix::poly::begin_unload(control));
    CHECK(skl::abix::poly::module_state(control) == skl::abix::poly::ModuleState::draining);

    abix_lease rejected = ABIX_LEASE_INVALID;
    CHECK(api.acquire(api.host_context, registry, control.id, &rejected) == ABIX_ERROR_UNLOADING);
    CHECK(rejected == ABIX_LEASE_INVALID);
    CHECK_FALSE(skl::abix::poly::wait_for_idle(control));

    // Draining does not block destroying an object / releasing its lease.
    api.release(api.host_context, registry, lease);
    CHECK(control.leases.load() == 0);
    CHECK(skl::abix::poly::wait_for_idle(control));
    skl::abix::poly::mark_unloaded(control);
    CHECK(skl::abix::poly::module_state(control) == skl::abix::poly::ModuleState::unloaded);
}

TEST_CASE("lease_abi.two_registries_have_independent_tokens", "[lease][abi]") {
    MockHost host_a;
    MockHost host_b;
    const abix_lease_api api_a = make_api(host_a);
    const abix_lease_api api_b = make_api(host_b);
    abix_lease_registry *registry_a = nullptr;
    abix_lease_registry *registry_b = nullptr;
    REQUIRE(api_a.registry_create(api_a.host_context, &registry_a) == ABIX_SUCCESS);
    REQUIRE(api_b.registry_create(api_b.host_context, &registry_b) == ABIX_SUCCESS);

    ModuleControl control_a;
    make_active_control(host_a, control_a, "lease-a");
    ModuleControl control_b;
    make_active_control(host_b, control_b, "lease-b");

    abix_lease token_a = ABIX_LEASE_INVALID;
    abix_lease token_b = ABIX_LEASE_INVALID;
    REQUIRE(api_a.acquire(api_a.host_context, registry_a, control_a.id, &token_a) == ABIX_SUCCESS);
    REQUIRE(api_b.acquire(api_b.host_context, registry_b, control_b.id, &token_b) == ABIX_SUCCESS);
    CHECK(token_a == token_b);   // same numeric value, different registry namespaces

    api_a.release(api_a.host_context, registry_a, token_a);
    CHECK(control_a.leases.load() == 0);
    CHECK(control_b.leases.load() == 1);

    api_b.release(api_b.host_context, registry_b, token_b);
    CHECK(control_b.leases.load() == 0);
}

TEST_CASE("lease_abi.concurrent_duplicate_release_decrements_once", "[lease][abi]") {
    MockHost host;
    const abix_lease_api api = make_api(host);
    abix_lease_registry *registry = nullptr;
    REQUIRE(api.registry_create(api.host_context, &registry) == ABIX_SUCCESS);

    ModuleControl control;
    make_active_control(host, control, "lease-race");
    abix_lease lease = ABIX_LEASE_INVALID;
    REQUIRE(api.acquire(api.host_context, registry, control.id, &lease) == ABIX_SUCCESS);
    REQUIRE(control.leases.load() == 1);

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
        threads.emplace_back([&] { api.release(api.host_context, registry, lease); });
    for (auto &thread : threads)
        thread.join();

    CHECK(control.leases.load() == 0);
    api.release(api.host_context, registry, lease);
    CHECK(control.leases.load() == 0);
}

TEST_CASE("lease_abi.token_overflow_fails_closed", "[lease][abi]") {
    MockHost host;
    const abix_lease_api api = make_api(host);
    abix_lease_registry *registry = nullptr;
    REQUIRE(api.registry_create(api.host_context, &registry) == ABIX_SUCCESS);

    ModuleControl control;
    make_active_control(host, control, "lease-overflow");
    host.next_token = UINT64_MAX;

    abix_lease last = ABIX_LEASE_INVALID;
    REQUIRE(api.acquire(api.host_context, registry, control.id, &last) == ABIX_SUCCESS);
    CHECK(last == UINT64_MAX);
    CHECK(control.leases.load() == 1);

    abix_lease rejected = ABIX_LEASE_INVALID;
    CHECK(api.acquire(api.host_context, registry, control.id, &rejected) == ABIX_ERROR_INVALID);
    CHECK(rejected == ABIX_LEASE_INVALID);
    CHECK(control.leases.load() == 1);

    api.release(api.host_context, registry, last);
    CHECK(control.leases.load() == 0);
}
