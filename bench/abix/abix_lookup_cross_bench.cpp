// ============================================================
// BM_ABIX_LookupCross — crossover benchmark for lookup strategies
//
// Compares 2 lookup strategies across table sizes and access
// patterns to find the optimal strategy per table size.
//
// Strategies:
//   Linear       — full-table linear scan (baseline)
//   HashIndex    — hash table (name_hash → index) + linear verify
//
// Access patterns:
//   uniform      — random index, uniform distribution
//   zipf_1.5     — zipfian (α=1.5), simulates real-world hot spots
//
// Table sizes:
//   16, 32, 64, 128, 256, 512, 1K, 4K, 16K
//
// Expected output:
//   A crossover table showing which strategy is fastest per
//   (table_size, access_pattern) combination.
// ============================================================
#include <benchmark/benchmark.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "abix/abix.hpp"

// ============================================================
// Access pattern generators
// ============================================================

// Uniform random — every entry equally likely
static std::vector<uint32_t> gen_uniform_queries(uint32_t table_size, uint32_t count) {
    std::vector<uint32_t> q(count);
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint32_t> dist(0, table_size - 1);
    for (uint32_t i = 0; i < count; ++i)
        q[i] = dist(rng);
    return q;
}

// Zipfian — hot entries dominate (α = 1.5)
static std::vector<uint32_t> gen_zipf_queries(uint32_t table_size, uint32_t count, double alpha = 1.5) {
    std::vector<uint32_t> q(count);
    std::mt19937 rng(42);

    std::vector<double> weights(table_size);
    double sum = 0.0;
    for (uint32_t i = 0; i < table_size; ++i) {
        weights[i] = 1.0 / std::pow(static_cast<double>(i + 1), alpha);
        sum += weights[i];
    }
    for (uint32_t i = 0; i < table_size; ++i)
        weights[i] /= sum;

    std::discrete_distribution<uint32_t> dist(weights.begin(), weights.end());
    for (uint32_t i = 0; i < count; ++i)
        q[i] = dist(rng);
    return q;
}

// ============================================================
// Test fixture — builds a table of given size
// ============================================================

struct LookupFixture {
    std::vector<skl::abix::entry> entries;
    skl::abix::table table;
    std::vector<std::string> names; // owns name strings

    explicit LookupFixture(uint32_t count) {
        entries.reserve(count);
        names.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "sym_%04u", i);
            names.emplace_back(buf);
            entries.push_back(skl::abix::entry{
                names.back().c_str(),
                skl::abix::fn_sig_v<int(int)>,
                0,
                uintptr_t(0xDEADBEEF),
                mics::utils::cstr32(names.back().c_str()),
                0});
        }

        table = skl::abix::table{count, skl::abix::SKL_ABIX_TABLE_MAGIC,
                                 skl::abix::SKL_ABIX_TABLE_FORMAT_VERSION, 0, entries.data()};
    }

    LookupFixture(const LookupFixture &) = delete;
    LookupFixture &operator=(const LookupFixture &) = delete;
};

// ============================================================
// Benchmark functions
// ============================================================

namespace {

enum class Strategy : uint8_t { Linear, HashIndex };

static const char *strategy_name(Strategy s) {
    switch (s) {
        case Strategy::Linear:      return "linear";
        case Strategy::HashIndex:   return "hash_index";
    }
    return "???";
}

// ============================================================
// Uniform random access
// ============================================================

static void run_uniform_bench(benchmark::State &state, Strategy strategy, uint32_t table_size) {
    LookupFixture fix(table_size);
    skl::abix::hash_index hash_idx;
    if (strategy == Strategy::HashIndex)
        hash_idx.build(fix.table);

    auto queries = gen_uniform_queries(table_size, 10000);
    uint32_t qi = 0;

    const skl::abix::sig_t sig = skl::abix::fn_sig_v<int(int)>;

    for (auto _ : state) {
        const char *name = fix.names[queries[qi]].c_str();
        skl::abix::name_hash_t nh = mics::utils::cstr32(name);
        qi = (qi + 1) % static_cast<uint32_t>(queries.size());

        const skl::abix::entry *found = nullptr;
        switch (strategy) {
            case Strategy::Linear:
                found = skl::abix::lookup_linear(fix.table, name, nh, sig);
                break;
            case Strategy::HashIndex:
                found = skl::abix::lookup_hash(fix.table, hash_idx, name, nh, sig);
                break;
        }
        benchmark::DoNotOptimize(found);
    }

    state.SetItemsProcessed(state.iterations());
}

// ============================================================
// Zipfian access (hot-spot heavy)
// ============================================================

static void run_zipf_bench(benchmark::State &state, Strategy strategy, uint32_t table_size) {
    LookupFixture fix(table_size);
    skl::abix::hash_index hash_idx;
    if (strategy == Strategy::HashIndex)
        hash_idx.build(fix.table);

    auto queries = gen_zipf_queries(table_size, 10000);
    uint32_t qi = 0;

    const skl::abix::sig_t sig = skl::abix::fn_sig_v<int(int)>;

    for (auto _ : state) {
        const char *name = fix.names[queries[qi]].c_str();
        skl::abix::name_hash_t nh = mics::utils::cstr32(name);
        qi = (qi + 1) % static_cast<uint32_t>(queries.size());

        const skl::abix::entry *found = nullptr;
        switch (strategy) {
            case Strategy::Linear:
                found = skl::abix::lookup_linear(fix.table, name, nh, sig);
                break;
            case Strategy::HashIndex:
                found = skl::abix::lookup_hash(fix.table, hash_idx, name, nh, sig);
                break;
        }
        benchmark::DoNotOptimize(found);
    }

    state.SetItemsProcessed(state.iterations());
}

} // anonymous namespace

// ============================================================
// Register all combinations
// ============================================================

static constexpr int kTableSizes[] = {16, 32, 64, 128, 256, 512, 1024, 4096, 16384};
static constexpr const char *kSizeLabels[] = {"16", "32", "64", "128", "256", "512", "1K", "4K", "16K"};

class ABIXLookupCrossRegisterer {
public:
    ABIXLookupCrossRegisterer() {
        for (int st = 0; st <= 1; ++st) {
            Strategy s = static_cast<Strategy>(st);
            for (int si = 0; si < 9; ++si) {
                // Uniform
                {
                    std::string name = std::string("BM_ABIX_LookupCross/uniform/")
                        + strategy_name(s) + "/" + kSizeLabels[si];
                    benchmark::RegisterBenchmark(name.c_str(),
                        [s, size = kTableSizes[si]](benchmark::State &st2) {
                            run_uniform_bench(st2, s, uint32_t(size));
                        })
                        ->Threads(1)
                        ->Unit(benchmark::kNanosecond);
                }
                // Zipf
                {
                    std::string name = std::string("BM_ABIX_LookupCross/zipf/")
                        + strategy_name(s) + "/" + kSizeLabels[si];
                    benchmark::RegisterBenchmark(name.c_str(),
                        [s, size = kTableSizes[si]](benchmark::State &st2) {
                            run_zipf_bench(st2, s, uint32_t(size));
                        })
                        ->Threads(1)
                        ->Unit(benchmark::kNanosecond);
                }
            }
        }
    }
};

static ABIXLookupCrossRegisterer g_registerer;