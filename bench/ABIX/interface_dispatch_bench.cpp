// ============================================================
// Interface dispatch benchmarks.
//
// Compares three ways of reaching one adapter:
//   BM_Interface_Direct        — the adapter called directly (floor)
//   BM_Interface_CachedCall    — resolved once through InterfaceRef, then a
//                                plain indirect call (the intended path)
//   BM_Interface_ResolvePerCall— a registry lookup on every call (what the
//                                cached path removes)
//
// Expected result: CachedCall ≈ Direct + one indirect branch, and far below
// ResolvePerCall, which pays a linear scan per call.
// ============================================================
#include <benchmark/benchmark.h>

#include "ABIX/Interface/Interface.h"

namespace bench_interface_dispatch {

using skl::abix::model::TypeId;
using skl::abix::poly::interface_entry;
using skl::abix::poly::InterfaceId;
using skl::abix::poly::InterfaceMethod;
using skl::abix::poly::InterfaceRef;
using skl::abix::poly::InterfaceRegistry;

struct Adder {
    int base;
};

using AddFn = int (*)(const void *, int);

int adder_add(const void *self, int value) { return static_cast<const Adder *>(self)->base + value; }

constexpr TypeId adder_id{0x51, 0x52};
constexpr InterfaceId iadd{0x61, 0x62};

struct Fixture {
    Adder object{1};
    InterfaceMethod methods[1];
    InterfaceRegistry<4> registry;
    InterfaceRef<4> ref;

    Fixture() {
        methods[0] = {
            {0x71, 0x0},
            interface_entry(&adder_add), 0, 0
        };
        (void)registry.add({iadd, adder_id, methods, 1, 1, 0});
        (void)ref.resolve(registry, &object, adder_id, iadd, 1);
    }
};

Fixture &fixture() {
    static Fixture instance;
    return instance;
}

void BM_Interface_Direct(benchmark::State &state) {
    auto &f = fixture();
    for (auto _ : state) {
        int r = adder_add(&f.object, 41);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Interface_Direct);

void BM_Interface_CachedCall(benchmark::State &state) {
    auto &f = fixture();
    const auto fn = f.ref.method<AddFn>(0);
    for (auto _ : state) {
        int r = fn(f.ref.object(), 41);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Interface_CachedCall);

void BM_Interface_ResolvePerCall(benchmark::State &state) {
    auto &f = fixture();
    for (auto _ : state) {
        InterfaceRef<4> ref;
        (void)ref.resolve(f.registry, &f.object, adder_id, iadd, 1);
        const auto fn = ref.method<AddFn>(0);
        int r = fn(ref.object(), 41);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Interface_ResolvePerCall);

}   // namespace bench_interface_dispatch
