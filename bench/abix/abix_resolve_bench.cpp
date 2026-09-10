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

#include <cstdio>
#include <cstring>
#include <vector>

#include "abix/abix.hpp"

// ============================================================
// Helpers
// ============================================================

struct ResolveFixture {
    std::vector<skl::abix::entry> entries;
    skl::abix::table table;
    skl::abix::hash_index index;
    const char *target_name{nullptr};
    skl::abix::sig_t sig{0};
    skl::abix::index_t out_idx{~skl::abix::index_t{0}};

    explicit ResolveFixture(uint32_t count) {
        entries.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            char *name = new char[16];
            std::snprintf(name, 16, "sym_%04u", i);
            entries.push_back(skl::abix::entry{
                name,
                skl::abix::fn_sig_v<int(int)>,
                0,
                uintptr_t(0xDEADBEEF),
                Reflect::Utils::cstr32(name),
                0});
        }
        table = skl::abix::table{count, skl::abix::SKL_ABIX_TABLE_MAGIC,
                                 skl::abix::SKL_ABIX_TABLE_FORMAT_VERSION, 0, entries.data()};
        if (count >= skl::abix::HASH_THRESHOLD)
            index.build(table);
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

static constexpr int kTableSizes[] = {16, 64, 256, 1024, 4096, 16384};
static constexpr const char *kSizeLabels[] = {"16", "64", "256", "1K", "4K", "16K"};

namespace {

void run_resolve_bench(benchmark::State &state, bool use_rcu, uint32_t table_size) {
    ResolveFixture fix(table_size);
    auto &domain = skl::abix::rcu_domain::instance();

    // Warmup: register this thread with RCU
    domain.enter();
    domain.exit();

    for (auto _ : state) {
        if (use_rcu) domain.enter();

        auto result = skl::abix::find_index(fix.table, fix.index, fix.target_name,
                                             fix.sig, 0, fix.out_idx);

        if (use_rcu) domain.exit();

        benchmark::DoNotOptimize(result);
        benchmark::DoNotOptimize(fix.out_idx);
    }

    state.SetItemsProcessed(state.iterations());
}

} // anonymous namespace

// ============================================================
// Register: BM_ABIX_Resolve/rcu_off|rcu_on/{entries}
// ============================================================

class ABIXResolveRegisterer {
public:
    ABIXResolveRegisterer() {
        for (int rcu = 0; rcu <= 1; ++rcu) {
            for (int si = 0; si < 6; ++si) {
                std::string name = std::string("BM_ABIX_Resolve/")
                    + (rcu ? "rcu_on" : "rcu_off") + "/"
                    + kSizeLabels[si];

                benchmark::RegisterBenchmark(name.c_str(),
                    [rcu, size = kTableSizes[si]](benchmark::State &st) {
                        run_resolve_bench(st, rcu != 0, uint32_t(size));
                    })
                    ->Threads(1)
                    ->Unit(benchmark::kNanosecond);
            }
        }
    }
};

static ABIXResolveRegisterer g_registerer;