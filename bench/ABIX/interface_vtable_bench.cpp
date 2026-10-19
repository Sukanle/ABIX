// ============================================================
// ABIX Interface vs native V-Table / dynamic_cast / RTTI.
//
// Pairs each native C++ mechanism with its ABIX Interface counterpart so the
// overhead of the non-intrusive model is visible on the same operation:
//
//   native_direct          <-> abix_direct      (direct call, the floor)
//   native_virtual         <-> abix_cached      (dispatch through a resolved slot)
//   native_dynamic_cast_ok <-> abix_bind        (discover a capability, success)
//   native_dynamic_cast_fail <-> abix_resolve_miss (discovery fails)
//   native_typeid          <-> abix_verify      (identity check before trusting a bound)
//
// The native side is called through an `ABIX_NOINLINE` accessor so the compiler
// cannot prove the dynamic type and devirtualise the call or fold the cast.
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
    virtual int calc(int value) const noexcept = 0;
};

struct Derived final : Base {
    int base = 1;
    int calc(int value) const noexcept override { return base + value; }
    int direct(int value) const noexcept { return base + value; }
};

struct Unrelated final : Base {
    int calc(int value) const noexcept override { return value; }
};

Derived g_derived;

// noinline: the callers must not learn the dynamic type, or the virtual call is
// devirtualised and dynamic_cast is folded into a constant.
ABIX_NOINLINE Base *opaque_base() noexcept { return &g_derived; }
ABIX_NOINLINE Derived &opaque_derived() noexcept { return g_derived; }

// ------------------------------------------------------------
// ABIX Interface fixture
// ------------------------------------------------------------

using skl::abix::model::TypeId;
using skl::abix::poly::interface_entry;
using skl::abix::poly::InterfaceId;
using skl::abix::poly::InterfaceMethod;
using skl::abix::poly::InterfaceMethodHash;
using skl::abix::poly::InterfaceRef;
using skl::abix::poly::InterfaceRegistry;
using skl::abix::poly::InterfaceStatus;

struct Adder {
    int base = 1;
};

using AddFn = int (*)(const void *, int);

int adder_add(const void *self, int value) { return static_cast<const Adder *>(self)->base + value; }

constexpr TypeId adder_id{0x51, 0x52};
constexpr InterfaceId iadd{0x61, 0x62};
constexpr InterfaceId unknown_iadd{0x61, 0x63};
constexpr InterfaceMethodHash add_signature{0x71, 0x72};

struct Fixture {
    Adder object;
    InterfaceMethod methods[1];
    InterfaceRegistry<4> registry;
    InterfaceRef<4> ref;

    Fixture() {
        methods[0] = {add_signature, interface_entry(&adder_add), 0, 0};
        (void)registry.add({iadd, adder_id, methods, 1, 1, 0});
        (void)ref.resolve(registry, &object, adder_id, iadd, 1);
    }
};

Fixture &fixture() {
    static Fixture instance;
    return instance;
}

// ------------------------------------------------------------
// Native: direct / virtual call
// ------------------------------------------------------------

void BM_InterfaceVtable_native_direct(benchmark::State &state) {
    for (auto _ : state) {
        int result = opaque_derived().direct(41);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_InterfaceVtable_native_direct)->Name("BM_InterfaceVtable/native_direct");

void BM_InterfaceVtable_native_virtual(benchmark::State &state) {
    Base *base = opaque_base();
    for (auto _ : state) {
        int result = base->calc(41);
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
    Fixture &f = fixture();
    for (auto _ : state) {
        int result = adder_add(&f.object, 41);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_InterfaceVtable_abix_direct)->Name("BM_InterfaceVtable/abix_direct");

void BM_InterfaceVtable_abix_cached(benchmark::State &state) {
    Fixture &f = fixture();
    const auto fn = f.ref.method<AddFn>(0);
    for (auto _ : state) {
        int result = fn(f.ref.object(), 41);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_InterfaceVtable_abix_cached)->Name("BM_InterfaceVtable/abix_cached");

void BM_InterfaceVtable_abix_bind(benchmark::State &state) {
    Fixture &f = fixture();
    for (auto _ : state) {
        InterfaceRef<4> ref;
        InterfaceStatus status = ref.resolve(f.registry, &f.object, adder_id, iadd, 1);
        benchmark::DoNotOptimize(status);
    }
}
BENCHMARK(BM_InterfaceVtable_abix_bind)->Name("BM_InterfaceVtable/abix_bind");

void BM_InterfaceVtable_abix_resolve_miss(benchmark::State &state) {
    Fixture &f = fixture();
    for (auto _ : state) {
        InterfaceRef<4> ref;
        InterfaceStatus status = ref.resolve(f.registry, &f.object, adder_id, unknown_iadd, 1);
        benchmark::DoNotOptimize(status);
    }
}
BENCHMARK(BM_InterfaceVtable_abix_resolve_miss)->Name("BM_InterfaceVtable/abix_resolve_miss");

void BM_InterfaceVtable_abix_verify(benchmark::State &state) {
    Fixture &f = fixture();
    for (auto _ : state) {
        InterfaceStatus status = f.ref.verify(0, add_signature);
        benchmark::DoNotOptimize(status);
    }
}
BENCHMARK(BM_InterfaceVtable_abix_verify)->Name("BM_InterfaceVtable/abix_verify");

}   // namespace bench_interface_vtable
