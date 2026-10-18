// ============================================================
// BM_ABIX_Resolve — measure ABIX symbol lookup cost with and
// without RCU read-side protection, across varying table sizes.
//
// Answers:
//   - How much does RCU add to a resolve (constant overhead)?
//   - How does resolve cost scale with table size?
//   - Is RCU overhead negligible compared to lookup cost?
//
// Expected result:
//   RCU overhead ≈ constant ~4 ns
//   Lookup cost  ≈ O(table_size)  (linear scan)
// ============================================================
#include <benchmark/benchmark.h>

#include <cstring>
#include <vector>

#include "ABIX/ABIX.h"
#include "bench_fmt.hpp"

// Named namespace: the file is also #included by bench_all.cpp, where all
// included sources share one translation unit.
namespace bench_abix_resolve {

// ============================================================
// Helpers
// ============================================================

struct ResolveFixture {
    std::vector<skl::abix::runtime::Entry> entries;
    skl::abix::runtime::Table table;
    skl::abix::runtime::HashIndex index;
    const char *target_name{nullptr};
    skl::abix::runtime::sig_t sig{0};
    skl::abix::runtime::index_t out_idx{SKL_ABIX_HASHSLOT_EMPTY};

    explicit ResolveFixture(uint32_t count) {
        entries.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            char *name = new char[16];
            format_into(name, 16, "sym_{:04}", i);
            entries.push_back(
                {name, skl::abix::dll::FnSigV<int(int)>, 0, 0xDEADBEEF, skl::abix::runtime::name_hash(name), 0});
        }
        table = {count, SKL_ABIX_TABLE_MAGIC, SKL_ABIX_TABLE_FORMAT_VERSION, 0, entries.data()};
        if (count >= SKL_ABIX_HASH_THRESHOLD) index.build(table);
        // Always search for the LAST entry (worst-case linear scan)
        target_name = entries.back().name;
        sig = entries.back().sig;
    }

    ~ResolveFixture() {
        index.destroy();
        for (auto &e : entries) {
            delete[] e.name;
        }
    }

    ResolveFixture(const ResolveFixture &) = delete;
    ResolveFixture &operator=(const ResolveFixture &) = delete;
};

// ============================================================
// Benchmark registration
// ============================================================

static constexpr int kTableSizes[] = {16, 64, 256, 1'024, 4'096, 16'384};
static constexpr const char *kSizeLabels[] = {"16", "64", "256", "1K", "4K", "16K"};

void run_resolve_bench(benchmark::State &state, bool use_rcu, uint32_t table_size) {
    ResolveFixture fix(table_size);
    auto &domain = skl::abix::rcu::Domain::instance();

    // Warmup: register this thread with RCU
    domain.enter();
    domain.exit();

    for (auto _ : state) {
        if (use_rcu) domain.enter();

        auto result = skl::abix::runtime::find_index(fix.table, fix.index, fix.target_name, fix.sig, 0, fix.out_idx);

        if (use_rcu) domain.exit();

        benchmark::DoNotOptimize(result);
        benchmark::DoNotOptimize(fix.out_idx);
    }

    state.SetItemsProcessed(state.iterations());
}

// HashIndex build cost and memory. `capacity = next_pow2(count * 2)` slots
// of `{hash, index}` (8 bytes each), an O(capacity) EMPTY-init pass, then an
// O(count) probe-insert pass. This runs on every load/reload for tables at or
// above SKL_ABIX_HASH_THRESHOLD, so it is part of the module-load latency.
void run_hashindex_build_bench(benchmark::State &state, uint32_t table_size) {
    if (table_size < SKL_ABIX_HASH_THRESHOLD) {
        state.SkipWithError("below SKL_ABIX_HASH_THRESHOLD: no index is built");
        return;
    }
    ResolveFixture fix(table_size);
    fix.index.destroy();   // the fixture built one; each iteration starts clean

    skl::abix::runtime::HashIndex probe;
    probe.build(fix.table);
    state.counters["capacity"] = static_cast<double>(probe.capacity);
    state.counters["slot_KiB"] =
        static_cast<double>(probe.capacity) * sizeof(skl::abix::runtime::HashSlot) / 1024.0;
    probe.destroy();

    for (auto _ : state) {
        skl::abix::runtime::HashIndex idx;
        idx.build(fix.table);
        benchmark::DoNotOptimize(idx.capacity);
        idx.destroy();
    }
    state.counters["entries"] = static_cast<double>(table_size);
}

// ============================================================
// Register: BM_ABIX_Resolve/rcu_off|rcu_on/{entries}
// ============================================================

class ABIXResolveRegisterer {
public:
    ABIXResolveRegisterer() {
        for (int rcu = 0; rcu <= 1; ++rcu) {
            for (int si = 0; si < 6; ++si) {
                std::string name =
                    std::string("BM_ABIX_Resolve/") + (rcu ? "rcu_on" : "rcu_off") + "/" + kSizeLabels[si];

                benchmark::RegisterBenchmark(
                    name.c_str(), [rcu, size = kTableSizes[si]](
                                      benchmark::State &st) { run_resolve_bench(st, rcu != 0, uint32_t(size)); })
                    ->Threads(1)
                    ->Unit(benchmark::kNanosecond);
            }
        }
    }
};

static ABIXResolveRegisterer g_registerer;

// BM_HashIndex_Build/{entries}
class ABIXHashIndexRegisterer {
public:
    ABIXHashIndexRegisterer() {
        for (int si = 0; si < 6; ++si) {
            std::string name = std::string("BM_HashIndex_Build/") + kSizeLabels[si];
            benchmark::RegisterBenchmark(name.c_str(),
                [size = kTableSizes[si]](benchmark::State &st) {
                    run_hashindex_build_bench(st, uint32_t(size));
                })
                ->Unit(benchmark::kNanosecond);
        }
    }
};

static ABIXHashIndexRegisterer g_hashindex_registerer;

}   // namespace bench_abix_resolve
