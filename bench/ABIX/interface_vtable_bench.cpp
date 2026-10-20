// ============================================================
// ABIX Interface vs native V-Table / dynamic_cast / RTTI.
//
// Pairs each native C++ mechanism with its ABIX Interface counterpart so the
// overhead of the non-intrusive model is visible on the same operation:
//
//   native_direct           <-> abix_direct       (direct call, the floor)
//   native_virtual          <-> abix_cached       (dispatch through a cached slot)
//   native_virtual          <-> abix_proxy        (dispatch through a bound proxy)
//   native_dynamic_cast_ok  <-> abix_bind         (discover a capability, success)
//   native_dynamic_cast_fail<-> abix_bind_miss    (discovery fails)
//   native_typeid           <-> abix_verify       (identity check before trust)
//
// The native side is called through an `ABIX_NOINLINE` accessor so the compiler
// cannot prove the dynamic type and devirtualise the call or fold the cast.
// The ABIX side uses the same contract declarations a real provider would:
// `InterfaceId` and method signatures come from `interface_id` and
// `method_signature`, never from a vtable address.
// ============================================================
#include <benchmark/benchmark.h>

#include <cstdint>
#include <typeinfo>

#include "ABIX/Interface/Interface.h"
#include "bench_common.hpp"

namespace bench_interface_vtable {

// ------------------------------------------------------------
// Native virtual hierarchy
// ------------------------------------------------------------

struct Base {
    virtual ~Base() = default;
    virtual int32_t calc(int32_t value) const noexcept = 0;
};

struct Derived final : Base {
    int32_t base = 1;
    int32_t calc(int32_t value) const noexcept override { return base + value; }
    int32_t direct(int32_t value) const noexcept { return base + value; }
};

struct Unrelated final : Base {
    int32_t calc(int32_t value) const noexcept override { return value; }
};

Derived g_derived;

// noinline: the callers must not learn the dynamic type, or the virtual call is
// devirtualised and dynamic_cast is folded into a constant.
ABIX_NOINLINE Base *opaque_base() noexcept { return &g_derived; }
ABIX_NOINLINE Derived &opaque_derived() noexcept { return g_derived; }

// ------------------------------------------------------------
// ABIX Interface contract and fixture
// ------------------------------------------------------------

using skl::abix::model::TypeId;
using skl::abix::poly::interface_entry;
using skl::abix::poly::interface_id;
using skl::abix::poly::InterfaceId;
using skl::abix::poly::InterfaceMethod;
using skl::abix::poly::InterfaceProxy;
using skl::abix::poly::InterfaceRef;
using skl::abix::poly::InterfaceRegistry;
using skl::abix::poly::InterfaceStatus;
using skl::abix::poly::method_signature;

constexpr auto i32 = skl::abix::model::canonical_type_id("prim:i32");

using AreaFn = int32_t (*)(const void *);

struct ShapeTraits {
    static constexpr skl::mics::utils::array signatures = {
        method_signature("area", i32, skl::mics::utils::array{}),
    };
    static constexpr uint32_t version = 1;
    static constexpr InterfaceId id = interface_id("Shape", version, signatures);

    struct alignas(8) Slots {
        uint32_t area;
    };
    alignas(8) static constexpr Slots slots = skl::mics::utils::make_iota<Slots, signatures.size(), 0>();
};

class Shape : public InterfaceProxy<ShapeTraits> {
    using Base = InterfaceProxy<ShapeTraits>;

public:
    using Base::Base;
    int32_t area() const noexcept { return this->template call<AreaFn>(ShapeTraits::slots.area); }
};

struct Box {
    int32_t width = 3;
    int32_t height = 4;
};

int32_t box_area(const void *self) {
    const auto *box = static_cast<const Box *>(self);
    return box->width * box->height;
}

constexpr TypeId box_id{0x51, 0x01};
constexpr TypeId unknown_box_id{0xde, 0xad};

struct Fixture {
    Box object;
    InterfaceMethod methods[1];
    InterfaceRegistry<4> registry;
    InterfaceRef<1> ref;
    Shape proxy;

    Fixture() {
        methods[0] = {ShapeTraits::signatures[0], interface_entry(&box_area), ShapeTraits::slots.area, 0};
        (void)registry.add({ShapeTraits::id, box_id, methods, 1, ShapeTraits::version, 0});
        (void)ref.resolve(registry, &object, box_id, ShapeTraits::id, ShapeTraits::version);
        (void)proxy.bind(registry, &object, box_id);
    }
};

Fixture *fixture_for(benchmark::State &state) {
    static Fixture instance;
    if (!instance.ref.valid() || !instance.proxy.valid()) {
        state.SkipWithError("interface vtable fixture failed to bind");
        return nullptr;
    }
    return &instance;
}

// ------------------------------------------------------------
// Native: direct / virtual call
// ------------------------------------------------------------

void BM_InterfaceVtable_native_direct(benchmark::State &state) {
    for (auto _ : state) {
        int32_t result = opaque_derived().direct(41);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_InterfaceVtable_native_direct)->Name("BM_InterfaceVtable/native_direct");

void BM_InterfaceVtable_native_virtual(benchmark::State &state) {
    Base *base = opaque_base();
    for (auto _ : state) {
        int32_t result = base->calc(41);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_InterfaceVtable_native_virtual)->Name("BM_InterfaceVtable/native_virtual");

// ------------------------------------------------------------
// Native: dynamic_cast + RTTI
// ------------------------------------------------------------

void BM_InterfaceVtable_native_dynamic_cast_ok(benchmark::State &state) {
    Base *base = opaque_base();
    for (auto _ : state) {
        Derived *derived = dynamic_cast<Derived *>(base);
        benchmark::DoNotOptimize(derived);
    }
}
BENCHMARK(BM_InterfaceVtable_native_dynamic_cast_ok)->Name("BM_InterfaceVtable/native_dynamic_cast_ok");

void BM_InterfaceVtable_native_dynamic_cast_fail(benchmark::State &state) {
    Base *base = opaque_base();
    for (auto _ : state) {
        Unrelated *unrelated = dynamic_cast<Unrelated *>(base);
        benchmark::DoNotOptimize(unrelated);
    }
}
BENCHMARK(BM_InterfaceVtable_native_dynamic_cast_fail)->Name("BM_InterfaceVtable/native_dynamic_cast_fail");

void BM_InterfaceVtable_native_typeid(benchmark::State &state) {
    Base *base = opaque_base();
    for (auto _ : state) {
        bool same = typeid(*base) == typeid(Derived);
        benchmark::DoNotOptimize(same);
    }
}
BENCHMARK(BM_InterfaceVtable_native_typeid)->Name("BM_InterfaceVtable/native_typeid");

// ------------------------------------------------------------
// ABIX Interface counterparts
// ------------------------------------------------------------

void BM_InterfaceVtable_abix_direct(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    for (auto _ : state) {
        int32_t result = box_area(&f->object);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_InterfaceVtable_abix_direct)->Name("BM_InterfaceVtable/abix_direct");

void BM_InterfaceVtable_abix_cached(benchmark::State &state) {
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
BENCHMARK(BM_InterfaceVtable_abix_cached)->Name("BM_InterfaceVtable/abix_cached");

void BM_InterfaceVtable_abix_proxy(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    for (auto _ : state) {
        int32_t result = f->proxy.area();
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_InterfaceVtable_abix_proxy)->Name("BM_InterfaceVtable/abix_proxy");

void BM_InterfaceVtable_abix_bind(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    for (auto _ : state) {
        Shape proxy;
        InterfaceStatus status = proxy.bind(f->registry, &f->object, box_id);
        benchmark::DoNotOptimize(status);
    }
}
BENCHMARK(BM_InterfaceVtable_abix_bind)->Name("BM_InterfaceVtable/abix_bind");

void BM_InterfaceVtable_abix_bind_miss(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    {
        Shape probe;
        if (probe.bind(f->registry, &f->object, unknown_box_id) != InterfaceStatus::interface_not_found) {
            state.SkipWithError("unknown concrete type was not rejected");
            return;
        }
    }
    for (auto _ : state) {
        Shape proxy;
        InterfaceStatus status = proxy.bind(f->registry, &f->object, unknown_box_id);
        benchmark::DoNotOptimize(status);
    }
}
BENCHMARK(BM_InterfaceVtable_abix_bind_miss)->Name("BM_InterfaceVtable/abix_bind_miss");

void BM_InterfaceVtable_abix_verify(benchmark::State &state) {
    Fixture *f = fixture_for(state);
    if (f == nullptr) return;
    for (auto _ : state) {
        InterfaceStatus status = f->proxy.verify(ShapeTraits::slots.area);
        benchmark::DoNotOptimize(status);
    }
}
BENCHMARK(BM_InterfaceVtable_abix_verify)->Name("BM_InterfaceVtable/abix_verify");

}   // namespace bench_interface_vtable
