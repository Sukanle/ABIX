// ============================================================
// Interface lifecycle benchmarks — the cold paths.
//
// The hot path (a bound proxy's method call) is measured in
// interface_dispatch_bench.cpp; this file covers everything a consumer pays
// once when a module is loaded, discovered and bound:
//
//   BM_Interface_ValidateManifest       structural manifest validation
//   BM_Interface_ModuleRegister         register + unregister one module
//   BM_Interface_ModuleFindFirst        find an interface in a module registry
//   BM_Interface_BindInterface          acquire a lease + resolve a binding
//   BM_Interface_ProxyBind              proxy bind: resolve + verify all slots
//   BM_Interface_ProxyBindMethods       proxy bind from a module's method table
//   BM_Interface_ProxyBindMismatch      proxy bind that must reject a bad signature
//   BM_Interface_LeaseAcquireRelease    the lease refcount round-trip
//
// These are setup / discovery costs, not per-call overhead. The two bind
// variants differ in where the adapter table comes from: the registry path
// resolves a registration, the methods path uses a discovered manifest
// directly. Both verify the contract's signatures before the proxy is valid.
// ============================================================
#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>

#include "ABIX/Interface/Interface.h"

namespace bench_interface_lifecycle {

using skl::abix::model::TypeId;
using skl::abix::poly::abix_manifest_version;
using skl::abix::poly::acquire_lease;
using skl::abix::poly::activate_module;
using skl::abix::poly::bind_interface;
using skl::abix::poly::interface_entry;
using skl::abix::poly::interface_id;
using skl::abix::poly::InterfaceId;
using skl::abix::poly::InterfaceManifestEntry;
using skl::abix::poly::InterfaceMethod;
using skl::abix::poly::InterfaceMethodHash;
using skl::abix::poly::InterfaceProxy;
using skl::abix::poly::InterfaceRegistry;
using skl::abix::poly::InterfaceStatus;
using skl::abix::poly::ManifestStatus;
using skl::abix::poly::method_signature;
using skl::abix::poly::ModuleControl;
using skl::abix::poly::ModuleManifest;
using skl::abix::poly::ModuleRegistry;
using skl::abix::poly::ModuleStatus;
using skl::abix::poly::validate_manifest;

constexpr auto i32 = skl::abix::model::canonical_type_id("prim:i32");

// ------------------------------------------------------------
// The contract: four slots, so binding has a visible per-signature cost.
// ------------------------------------------------------------

using OpFn = int32_t (*)(const void *, int32_t);

struct ServiceTraits {
    static constexpr skl::mics::utils::array signatures = {
        method_signature("add", i32, skl::mics::utils::array{i32}),
        method_signature("multiply", i32, skl::mics::utils::array{i32}),
        method_signature("minimum", i32, skl::mics::utils::array{i32}),
        method_signature("maximum", i32, skl::mics::utils::array{i32}),
    };
    static constexpr uint32_t version = 1;
    static constexpr InterfaceId id = interface_id("Service", version, signatures);

    struct alignas(8) Slots {
        uint32_t add;
        uint32_t multiply;
        uint32_t minimum;
        uint32_t maximum;
    };
    alignas(8) static constexpr Slots slots = skl::mics::utils::make_iota<Slots, signatures.size(), 0>();
};

class Service : public InterfaceProxy<ServiceTraits> {
    using Base = InterfaceProxy<ServiceTraits>;

public:
    using Base::Base;
    int32_t add(int32_t value) const noexcept { return this->template call<OpFn>(ServiceTraits::slots.add, value); }
};

// ------------------------------------------------------------
// Provider
// ------------------------------------------------------------

struct Counter {
    int32_t value;
};

int32_t counter_add(const void *self, int32_t value) { return static_cast<const Counter *>(self)->value + value; }
int32_t counter_multiply(const void *self, int32_t value) { return static_cast<const Counter *>(self)->value * value; }
int32_t counter_minimum(const void *self, int32_t value) {
    const int32_t current = static_cast<const Counter *>(self)->value;
    return value < current ? value : current;
}
int32_t counter_maximum(const void *self, int32_t value) {
    const int32_t current = static_cast<const Counter *>(self)->value;
    return value > current ? value : current;
}

constexpr TypeId counter_id{0x51, 0x52};
constexpr InterfaceId extra_iid1{0x61, 0x02};
constexpr InterfaceId extra_iid2{0x61, 0x03};
constexpr InterfaceId extra_iid3{0x61, 0x04};

constexpr uint32_t kSlots = static_cast<uint32_t>(ServiceTraits::signatures.size());
constexpr uint32_t kInterfaces = 4;

// One object, one method table shared by every manifest entry, and two
// registries: the valid binding and a deliberately mismatched signature.
struct Fixture {
    Counter object{1};
    InterfaceMethod methods[kSlots];
    InterfaceMethod mismatched[kSlots];
    InterfaceManifestEntry entries[kInterfaces];
    ModuleManifest manifest;
    InterfaceRegistry<8> registry;
    InterfaceRegistry<8> mismatch_registry;

    ModuleControl module_control;
    ModuleRegistry<8, 32> modules;

    Fixture() {
        methods[0] = {ServiceTraits::signatures[0], interface_entry(&counter_add), ServiceTraits::slots.add, 0};
        methods[1] = {
            ServiceTraits::signatures[1], interface_entry(&counter_multiply), ServiceTraits::slots.multiply, 0};
        methods[2] = {ServiceTraits::signatures[2], interface_entry(&counter_minimum), ServiceTraits::slots.minimum, 0};
        methods[3] = {ServiceTraits::signatures[3], interface_entry(&counter_maximum), ServiceTraits::slots.maximum, 0};

        for (uint32_t i = 0; i < kSlots; ++i)
            mismatched[i] = methods[i];
        mismatched[ServiceTraits::slots.add].signature = InterfaceMethodHash{0xde, 0xad};

        entries[0] = {ServiceTraits::id, ServiceTraits::version, 0, methods, kSlots, 0};
        entries[1] = {extra_iid1, 1, 0, methods, kSlots, 0};
        entries[2] = {extra_iid2, 1, 0, methods, kSlots, 0};
        entries[3] = {extra_iid3, 1, 0, methods, kSlots, 0};

        manifest = {abix_manifest_version, 1, 0, kInterfaces, entries, 0, 0, nullptr};

        (void)registry.add({ServiceTraits::id, counter_id, methods, kSlots, ServiceTraits::version, 0});
        (void)mismatch_registry.add({ServiceTraits::id, counter_id, mismatched, kSlots, ServiceTraits::version, 0});
        (void)modules.register_module(module_control, manifest);
    }
};

Fixture *fixture_for(benchmark::State &state) {
    static Fixture instance;
    if (instance.registry.find(counter_id, ServiceTraits::id, ServiceTraits::version) == nullptr
        || instance.modules.find_first(ServiceTraits::id, ServiceTraits::version) == nullptr) {
        state.SkipWithError("interface lifecycle fixture failed to register");
        return nullptr;
    }
    return &instance;
}

// ------------------------------------------------------------
// Discovery and registration
// ------------------------------------------------------------

void BM_Interface_ValidateManifest(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    for (auto _ : state) {
        ManifestStatus status = validate_manifest(&f->manifest);
        benchmark::DoNotOptimize(status);
    }
}
BENCHMARK(BM_Interface_ValidateManifest);

void BM_Interface_ModuleRegister(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    ModuleRegistry<8, 32> registry;
    for (auto _ : state) {
        ModuleControl control;
        ModuleStatus added = registry.register_module(control, f->manifest);
        benchmark::DoNotOptimize(added);
        ModuleStatus removed = registry.unregister_module(control.id);
        benchmark::DoNotOptimize(removed);
    }
}
BENCHMARK(BM_Interface_ModuleRegister);

void BM_Interface_ModuleFindFirst(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    for (auto _ : state) {
        const auto *found = f->modules.find_first(ServiceTraits::id, ServiceTraits::version);
        benchmark::DoNotOptimize(found);
    }
}
BENCHMARK(BM_Interface_ModuleFindFirst);

void BM_Interface_BindInterface(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    const auto *binding = f->modules.find_first(ServiceTraits::id, ServiceTraits::version);
    if (binding == nullptr) {
        state.SkipWithError("service binding is not registered");
        return;
    }
    for (auto _ : state) {
        auto bound = bind_interface<kSlots>(*binding, &f->object);
        bool valid = bound.valid();
        benchmark::DoNotOptimize(valid);
    }
}
BENCHMARK(BM_Interface_BindInterface);

// ------------------------------------------------------------
// Proxy binding: registry path vs. a discovered method table
// ------------------------------------------------------------

void BM_Interface_ProxyBind(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    for (auto _ : state) {
        Service proxy;
        InterfaceStatus status = proxy.bind(f->registry, &f->object, counter_id);
        benchmark::DoNotOptimize(status);
    }
}
BENCHMARK(BM_Interface_ProxyBind);

void BM_Interface_ProxyBindMethods(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    for (auto _ : state) {
        Service proxy;
        InterfaceStatus status =
            proxy.bind_methods(ServiceTraits::id, ServiceTraits::version, f->methods, kSlots, &f->object);
        benchmark::DoNotOptimize(status);
    }
}
BENCHMARK(BM_Interface_ProxyBindMethods);

void BM_Interface_ProxyBindMismatch(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    // A benchmark that silently measured a successful bind would be misleading:
    // make the expected failure explicit before timing it.
    {
        Service probe;
        if (probe.bind(f->mismatch_registry, &f->object, counter_id) != InterfaceStatus::signature_mismatch) {
            state.SkipWithError("mismatched signature was not rejected");
            return;
        }
    }
    for (auto _ : state) {
        Service proxy;
        InterfaceStatus status = proxy.bind(f->mismatch_registry, &f->object, counter_id);
        benchmark::DoNotOptimize(status);
    }
}
BENCHMARK(BM_Interface_ProxyBindMismatch);

// ------------------------------------------------------------
// Lease lifecycle
// ------------------------------------------------------------

void BM_Interface_LeaseAcquireRelease(benchmark::State &state) {
    ModuleControl control;
    if (!activate_module(control)) {
        state.SkipWithError("module control could not be activated");
        return;
    }
    for (auto _ : state) {
        auto lease = acquire_lease(control);
        bool valid = lease.valid();
        benchmark::DoNotOptimize(valid);
        lease.release();
    }
}
BENCHMARK(BM_Interface_LeaseAcquireRelease);

}   // namespace bench_interface_lifecycle
