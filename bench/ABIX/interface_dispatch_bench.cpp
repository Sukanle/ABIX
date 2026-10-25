// ============================================================
// Interface dispatch benchmarks — the hot path.
//
// The consumer-facing path is an `UniqueInterfaceProxy`: bind once (resolve the
// binding and verify every declared method signature), then call typed methods.
// This file measures what that path costs after it is bound, against the
// mechanisms underneath it:
//
//   BM_Interface_Direct              adapter called directly (the floor)
//   BM_Interface_CachedCall          cached InterfaceRef adapter, no proxy layer
//   BM_Interface_ProxyCall           method through a bound UniqueInterfaceProxy
//   BM_Interface_ProxyResize         a two-argument method through the proxy
//   BM_Interface_ProxyCheckedCall    call_checked (null check + status + out)
//   BM_Interface_MacroProxyCall      the same contract declared by
//                                    ABIX_DECLARE_INTERFACE (zero difference)
//   BM_Interface_VerifyPerCall       verify() on every call (what bind() does
//                                    once, for every slot)
//   BM_Interface_ResolvePerCall      registry lookup + resolve on every call
//
// Expected: Direct <= CachedCall ≈ ProxyCall ≈ MacroProxyCall <
// ProxyCheckedCall, and all of them far below VerifyPerCall/ResolvePerCall.
// A proxy is a handle, not a runtime dispatch loop: `bind()` is the only place
// that inspects the registry or the signatures.
// ============================================================
#include <benchmark/benchmark.h>

#include <stdint.h>

#include "SKL/ABIX/Interface/Interface.h"

namespace bench_interface_dispatch {

using skl::abix::model::TypeId;
using skl::abix::poly::interface_entry;
using skl::abix::poly::interface_id;
using skl::abix::poly::InterfaceId;
using skl::abix::poly::InterfaceMethod;
using skl::abix::poly::InterfaceRef;
using skl::abix::poly::InterfaceRegistry;
using skl::abix::poly::InterfaceStatus;
using skl::abix::poly::method_signature;
using skl::abix::poly::UniqueInterfaceProxy;

// ------------------------------------------------------------
// The contract: a hand-written Traits + proxy, as AMC would emit one.
// ------------------------------------------------------------

constexpr auto i32 = skl::abix::model::canonical_type_id("prim:i32");

using AreaFn = int32_t (*)(const void *);
using ResizeFn = int32_t (*)(void *, int32_t, int32_t);

struct ShapeTraits {
    static constexpr skl::mics::util::array signatures = {
        method_signature("area", i32, skl::mics::util::array{}),
        method_signature("resize", i32, skl::mics::util::array{i32, i32}),
    };
    static constexpr uint32_t version = 1;
    static constexpr InterfaceId id = interface_id("Shape", version, signatures);

    struct alignas(8) Slots {
        uint32_t area;
        uint32_t resize;
    };
    // Slots are zero-based: the first entry is slot 0.
    alignas(8) static constexpr Slots slots = skl::mics::util::make_iota<Slots, signatures.size(), 0>();
};

class Shape : public UniqueInterfaceProxy<ShapeTraits> {
    using Base = UniqueInterfaceProxy<ShapeTraits>;

public:
    using Base::Base;

    int32_t area() const noexcept { return this->template call<AreaFn>(ShapeTraits::slots.area); }
    InterfaceStatus area(int32_t &out) const noexcept {
        return this->template call_checked<AreaFn>(out, ShapeTraits::slots.area);
    }

    int32_t resize(int32_t width, int32_t height) const noexcept {
        return this->template call<ResizeFn>(ShapeTraits::slots.resize, width, height);
    }
    InterfaceStatus resize(int32_t &out, int32_t width, int32_t height) const noexcept {
        return this->template call_checked<ResizeFn>(out, ShapeTraits::slots.resize, width, height);
    }
};

// The same contract through the declaration macro. It exists to prove the
// generated proxy has exactly the same hot path as the hand-written one.
#define DISPATCH_SHAPE_METHODS(X) \
    X(area, int32_t, i32, {})     \
    X(resize, int32_t, i32, {i32, i32})

SKL_ABIX_INTERFACE_DECLARE(UNIQUE, DeclaredShape, DISPATCH_SHAPE_METHODS, 1);

// ------------------------------------------------------------
// Provider: a plain aggregate plus one adapter per method.
// ------------------------------------------------------------

struct Box {
    int32_t width;
    int32_t height;
};

int32_t box_area(const void *self) {
    const auto *box = static_cast<const Box *>(self);
    return box->width * box->height;
}

int32_t box_resize(void *self, int32_t width, int32_t height) {
    auto *box = static_cast<Box *>(self);
    box->width = width;
    box->height = height;
    return box->width * box->height;
}

constexpr TypeId box_id{0x51, 0x01};

// ------------------------------------------------------------
// Fixture: one provider object, two providers of the same binding shape
// (hand-written and macro-generated), both resolved once at startup.
// ------------------------------------------------------------

struct Fixture {
    Box object{3, 4};
    InterfaceMethod methods[2];
    InterfaceRegistry<4> registry;
    InterfaceRef<2> ref;
    Shape proxy;
    DeclaredShape declared;

    Fixture() {
        methods[0] = {ShapeTraits::signatures[0], interface_entry(&box_area), ShapeTraits::slots.area, 0};
        methods[1] = {ShapeTraits::signatures[1], interface_entry(&box_resize), ShapeTraits::slots.resize, 0};

        (void)registry.add({ShapeTraits::id, box_id, methods, 2, ShapeTraits::version, 0});
        (void)registry.add({DeclaredShapeTraits::id, box_id, methods, 2, DeclaredShapeTraits::version, 0});

        (void)ref.resolve(registry, &object, box_id, ShapeTraits::id, ShapeTraits::version);
        (void)proxy.bind(registry, &object, box_id);
        (void)declared.bind(registry, &object, box_id);
    }
};

// Returns the fixture only when every binding succeeded. The check runs once
// per benchmark, outside the timed loop, and a broken contract is reported
// rather than measured as a null function pointer.
Fixture *fixture_for(benchmark::State &state) {
    static Fixture instance;
    if (!instance.ref.valid() || !instance.proxy.valid() || !instance.declared.valid()) {
        state.SkipWithError("interface dispatch fixture failed to bind");
        return nullptr;
    }
    return &instance;
}

// ------------------------------------------------------------
// Hot-path benchmarks
// ------------------------------------------------------------

void BM_Interface_Direct(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    for (auto _ : state) {
        int32_t result = box_area(&f->object);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_Interface_Direct);

void BM_Interface_CachedCall(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    const AreaFn area = f->ref.method<AreaFn>(ShapeTraits::slots.area);
    if (area == nullptr) {
        state.SkipWithError("area adapter is not bound");
        return;
    }
    for (auto _ : state) {
        int32_t result = area(f->ref.object());
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_Interface_CachedCall);

void BM_Interface_ProxyCall(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    for (auto _ : state) {
        int32_t result = f->proxy.area();
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_Interface_ProxyCall);

void BM_Interface_ProxyResize(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    for (auto _ : state) {
        int32_t result = f->proxy.resize(2, 3);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_Interface_ProxyResize);

void BM_Interface_ProxyCheckedCall(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    int32_t out = 0;
    for (auto _ : state) {
        InterfaceStatus status = f->proxy.area(out);
        benchmark::DoNotOptimize(status);
        benchmark::DoNotOptimize(out);
    }
}
BENCHMARK(BM_Interface_ProxyCheckedCall);

void BM_Interface_MacroProxyCall(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    for (auto _ : state) {
        int32_t result = f->declared.area();
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_Interface_MacroProxyCall);

// ------------------------------------------------------------
// What binding (or not caching) costs per call
// ------------------------------------------------------------

void BM_Interface_VerifyPerCall(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    for (auto _ : state) {
        InterfaceStatus status = f->proxy.verify(ShapeTraits::slots.area);
        benchmark::DoNotOptimize(status);
    }
}
BENCHMARK(BM_Interface_VerifyPerCall);

void BM_Interface_ResolvePerCall(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    for (auto _ : state) {
        InterfaceRef<2> ref;
        InterfaceStatus status = ref.resolve(f->registry, &f->object, box_id, ShapeTraits::id, ShapeTraits::version);
        const AreaFn area = ref.method<AreaFn>(ShapeTraits::slots.area);
        int32_t result = area == nullptr ? 0 : area(ref.object());
        benchmark::DoNotOptimize(status);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_Interface_ResolvePerCall);

}   // namespace bench_interface_dispatch
