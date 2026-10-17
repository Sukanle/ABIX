// ============================================================
// Probe: how should HashIndex::build be built?
//
// `HashIndex::build` (ABIX/Runtime/Search.h) runs on every load/reload for tables
// at or above SKL_ABIX_HASH_THRESHOLD:
//   capacity = next_pow2(count * 2) slots of {hash32, index32} (8 B)
//   + an O(capacity) pass writing `hash = 0; index = EMPTY` to every slot
//   + an O(count) probe-insert pass
// The shipping build, measured with this same fixture on this machine: 69 ns at 64
// entries, ~800 ns at 1K, ~5.8 us at 4K and ~30 us at 16K entries (256 KiB of
// slots) — ~10x a same-path reload at that size, paid on every load/reload.
//
// This is a plain std::chrono harness rather than a Google Benchmark one: at these
// sizes the variant differences sit inside GB's iteration-estimation noise (five
// "repetitions" came back with identical means, and the pooled variants measured
// slower than the allocating ones, which is not physically sensible).
//
// Two questions are measured separately, because their answers differ:
//
//   Pooled  — the slot buffer is already in place; what does the *fill* cost?
//     A_base : the shipping strategy (per-slot two-field init loop)
//     B_zero : zero sentinel (`index+1`, 0 == empty) + one bulk memset
//     D_gen  : per-slot generation tag, so no clearing pass at all
//   Alloc   — what does allocating the buffer per build add on top?
//     A_prod : the shipping HashIndex (new[] + init + insert, every build)
//     B_zero : malloc + memset every build
//     D_gen  : allocate once, then only bump the generation
//
// Every variant keeps the identical probe shape (capacity = next_pow2(count*2),
// linear probing, first free slot wins, `strcmp` on a hash match), and the
// equivalence check confirms they resolve identical indices with identical probe
// counts. Nothing here is wired into ABIX.
// ============================================================

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ABIX/ABIX.h"

namespace {

using skl::abix::runtime::Entry;
using skl::abix::runtime::HashSlot;
using skl::abix::runtime::index_t;
using skl::abix::runtime::name_hash_t;
using skl::abix::runtime::next_pow2;
using skl::abix::runtime::Table;

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------- fixture

struct Fixture {
    std::vector<Entry> entries;
    Table table{};

    explicit Fixture(uint32_t count) {
        entries.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            char *name = new char[16];
            std::snprintf(name, 16, "sym_%04u", i);
            entries.push_back(
                {name, skl::abix::dll::FnSigV<int(int)>, 0, 0xDEADBEEF, skl::abix::runtime::name_hash(name), 0});
        }
        table = {count, SKL_ABIX_TABLE_MAGIC, SKL_ABIX_TABLE_FORMAT_VERSION, 0, entries.data()};
    }
    ~Fixture() {
        for (auto &e : entries)
            delete[] e.name;
    }
    Fixture(const Fixture &) = delete;
    Fixture &operator=(const Fixture &) = delete;
};

// ---------------------------------------------------------------- A (mirror)

struct BaseSlot {
    uint32_t hash;
    uint32_t index;
};

struct IndexBase {
    BaseSlot *slots = nullptr;
    uint32_t capacity = 0;
    uint32_t mask = 0;
    bool owns = true;
    const Table *names = nullptr;

    ~IndexBase() {
        if (owns) std::free(slots);
    }
    void adopt(BaseSlot *buffer, uint32_t want) noexcept {
        slots = buffer;
        capacity = want;
        mask = want - 1;
        owns = false;
    }
    void build(const Table &t) {
        const uint32_t want = next_pow2(t.count * 2);
        if (slots == nullptr || capacity != want) {
            if (owns) std::free(slots);
            slots = static_cast<BaseSlot *>(std::malloc(sizeof(BaseSlot) * want));
            capacity = want;
            mask = want - 1;
        }
        for (uint32_t i = 0; i < capacity; ++i) {   // per-slot init pass
            slots[i].hash = 0;
            slots[i].index = SKL_ABIX_HASHSLOT_EMPTY;
        }
        for (index_t i = 0; i < t.count; ++i) {
            const Entry &e = t.entries[i];
            uint32_t pos = e.name_hash & mask;
            while (slots[pos].index != SKL_ABIX_HASHSLOT_EMPTY)
                pos = (pos + 1) & mask;
            slots[pos].hash = e.name_hash;
            slots[pos].index = i;
        }
    }
    index_t lookup(name_hash_t nh, const char *name, uint64_t &probes) const {
        uint32_t pos = nh & mask;
        probes = 1;
        while (slots[pos].index != SKL_ABIX_HASHSLOT_EMPTY) {
            if (slots[pos].hash == nh && std::strcmp(names->entries[slots[pos].index].name, name) == 0)
                return slots[pos].index;
            pos = (pos + 1) & mask;
            ++probes;
        }
        return SKL_ABIX_HASHSLOT_EMPTY;
    }
};

// ---------------------------------------------------------------- B

struct ZeroSlot {
    uint32_t hash;
    uint32_t index_plus1;
};

struct IndexZero {
    ZeroSlot *slots = nullptr;
    uint32_t capacity = 0;
    uint32_t mask = 0;
    bool owns = true;
    const Table *names = nullptr;

    ~IndexZero() {
        if (owns) std::free(slots);
    }
    void adopt(ZeroSlot *buffer, uint32_t want) noexcept {
        slots = buffer;
        capacity = want;
        mask = want - 1;
        owns = false;
    }
    void build(const Table &t) {
        const uint32_t want = next_pow2(t.count * 2);
        if (slots == nullptr || capacity != want) {
            if (owns) std::free(slots);
            slots = static_cast<ZeroSlot *>(std::malloc(sizeof(ZeroSlot) * want));
            capacity = want;
            mask = want - 1;
        }
        std::memset(slots, 0, sizeof(ZeroSlot) * capacity);   // one bulk pass
        for (index_t i = 0; i < t.count; ++i) {
            const Entry &e = t.entries[i];
            uint32_t pos = e.name_hash & mask;
            while (slots[pos].index_plus1 != 0)
                pos = (pos + 1) & mask;
            slots[pos].hash = e.name_hash;
            slots[pos].index_plus1 = i + 1;
        }
    }
    index_t lookup(name_hash_t nh, const char *name, uint64_t &probes) const {
        uint32_t pos = nh & mask;
        probes = 1;
        while (slots[pos].index_plus1 != 0) {
            const index_t i = slots[pos].index_plus1 - 1;
            if (slots[pos].hash == nh && std::strcmp(names->entries[i].name, name) == 0) return i;
            pos = (pos + 1) & mask;
            ++probes;
        }
        return SKL_ABIX_HASHSLOT_EMPTY;
    }
};

// ---------------------------------------------------------------- D

struct GenSlot {
    uint32_t hash;
    uint32_t index;
    uint32_t gen;
};

struct IndexGen {
    GenSlot *slots = nullptr;
    uint32_t capacity = 0;
    uint32_t mask = 0;
    uint32_t gen = 0;
    const Table *names = nullptr;

    ~IndexGen() { std::free(slots); }

    void reset(uint32_t want) {
        std::free(slots);
        slots = static_cast<GenSlot *>(std::calloc(want, sizeof(GenSlot)));
        capacity = want;
        mask = want - 1;
        gen = 0;
    }
    void clear() {
        std::free(slots);
        slots = nullptr;
        capacity = mask = gen = 0;
    }
    void build(const Table &t) {
        const uint32_t want = next_pow2(t.count * 2);
        if (slots == nullptr || capacity != want) reset(want);
        if (++gen == 0) {
            std::memset(slots, 0, sizeof(GenSlot) * capacity);
            gen = 1;
        }
        for (index_t i = 0; i < t.count; ++i) {
            const Entry &e = t.entries[i];
            uint32_t pos = e.name_hash & mask;
            while (slots[pos].gen == gen)
                pos = (pos + 1) & mask;
            slots[pos] = {e.name_hash, i, gen};
        }
    }
    index_t lookup(name_hash_t nh, const char *name, uint64_t &probes) const {
        uint32_t pos = nh & mask;
        probes = 1;
        while (slots[pos].gen == gen) {
            if (slots[pos].hash == nh && std::strcmp(names->entries[slots[pos].index].name, name) == 0)
                return slots[pos].index;
            pos = (pos + 1) & mask;
            ++probes;
        }
        return SKL_ABIX_HASHSLOT_EMPTY;
    }
};

// ---------------------------------------------------------------- measurement

// Keeps a value observable so the work that produced it cannot be elided.
inline volatile unsigned long long g_sink = 0;
inline void keep(unsigned long long v) noexcept { g_sink = v; }

double median(std::vector<double> &v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// Times `one_build` `iters` times per run, `reps` runs, and returns the median
// ns per build. A template rather than std::function so no call overhead is added
// to the 1K cases.
template<typename F>
double time_builds(F &&one_build, int iters, int reps) {
    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(reps));
    for (int r = 0; r < reps; ++r) {
        const auto t0 = Clock::now();
        for (int i = 0; i < iters; ++i)
            one_build();
        const auto t1 = Clock::now();
        samples.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / iters);
    }
    return median(samples);
}

int probes_and_mismatches(const Table &t, double &base_probes, double &zero_probes, double &gen_probes) {
    IndexBase base;
    base.names = &t;
    base.build(t);
    IndexZero zero;
    zero.names = &t;
    zero.build(t);
    IndexGen gen;
    gen.names = &t;
    gen.build(t);

    uint64_t bp = 0, zp = 0, gp = 0, mism = 0;
    for (index_t i = 0; i < t.count; ++i) {
        const Entry &e = t.entries[i];
        uint64_t p = 0;
        const index_t b = base.lookup(e.name_hash, e.name, p);
        bp += p;
        const index_t z = zero.lookup(e.name_hash, e.name, p);
        zp += p;
        const index_t g = gen.lookup(e.name_hash, e.name, p);
        gp += p;
        if (z != b || g != b) ++mism;
    }
    base_probes = static_cast<double>(bp) / t.count;
    zero_probes = static_cast<double>(zp) / t.count;
    gen_probes = static_cast<double>(gp) / t.count;
    return static_cast<int>(mism);
}

}   // namespace

// ---------------------------------------------------------------- single-process mode

// Measures one (variant, size) in this process and prints a single RESULT line.
// `cold` is the first timed round (it includes the first allocation and its page
// faults); `warm` is the median of the remaining rounds. Run through `--spawn` so
// every combination starts from a fresh allocator/page state.
int run_one(const char *variant, uint32_t size, int rounds) {
    Fixture fix(size);
    const uint32_t want = next_pow2(size * 2);
    const int iters = size >= 16'384 ? 200 : (size >= 4'096 ? 500 : 2'000);

    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(rounds));

    IndexBase base;
    IndexZero zero;
    IndexGen gen;
    const bool is_base = std::strcmp(variant, "A_alloc") == 0 || std::strcmp(variant, "A_pool") == 0;
    const bool is_zero = std::strcmp(variant, "B_alloc") == 0 || std::strcmp(variant, "B_pool") == 0;
    const bool pooled = std::strstr(variant, "_pool") != nullptr;

    if (is_base && pooled) {
        base.adopt(static_cast<BaseSlot *>(std::malloc(sizeof(BaseSlot) * want)), want);
        base.names = &fix.table;
    }
    if (is_zero && pooled) {
        zero.adopt(static_cast<ZeroSlot *>(std::malloc(sizeof(ZeroSlot) * want)), want);
        zero.names = &fix.table;
    }
    if (!is_base && !is_zero) {   // D_pool
        gen.reset(want);
        gen.names = &fix.table;
    }

    for (int r = 0; r < rounds; ++r) {
        const auto t0 = Clock::now();
        for (int i = 0; i < iters; ++i) {
            if (is_base) {
                if (pooled) {
                    base.build(fix.table);
                    keep(base.capacity);
                } else {
                    IndexBase local;
                    local.names = &fix.table;
                    local.build(fix.table);
                    keep(static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(local.slots)));
                }
            } else if (is_zero) {
                if (pooled) {
                    zero.build(fix.table);
                    keep(zero.capacity);
                } else {
                    IndexZero local;
                    local.names = &fix.table;
                    local.build(fix.table);
                    keep(static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(local.slots)));
                }
            } else {
                gen.build(fix.table);
                keep(gen.capacity);
            }
        }
        const auto t1 = Clock::now();
        samples.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / iters);
    }

    const double cold = samples[0];
    std::vector<double> rest(samples.begin() + 1, samples.end());
    const double warm = rest.empty() ? cold : median(rest);
    const double best = *std::min_element(samples.begin(), samples.end());
    std::printf("RESULT %s %u %.1f %.1f %.1f\n", variant, size, cold, warm, best);
    return 0;
}

// ---------------------------------------------------------------- spawn mode

#if defined(_WIN32)
#  define ABIX_POPEN _popen
#  define ABIX_PCLOSE _pclose
#else
#  define ABIX_POPEN popen
#  define ABIX_PCLOSE pclose
#endif

int spawn_all(const char *self) {
    struct Combo {
        const char *variant;
        uint32_t size;
        std::vector<double> colds;
        std::vector<double> warms;
    };

    const char *variants[] = {"A_alloc", "A_pool", "B_alloc", "B_pool", "D_pool"};
    const uint32_t sizes[] = {1'024, 4'096, 16'384};
    const int processes = 5;
    const int rounds = 9;

    std::vector<Combo> combos;
    for (uint32_t size : sizes)
        for (const char *v : variants)
            combos.push_back({v, size, {}, {}});

    for (Combo &c : combos) {
        for (int p = 0; p < processes; ++p) {
            std::string cmd = std::string("\"") + self + "\" --run-one " + c.variant + " " + std::to_string(c.size)
                            + " " + std::to_string(rounds);
            std::FILE *pipe = ABIX_POPEN(cmd.c_str(), "r");
            if (!pipe) {
                std::fprintf(stderr, "cannot spawn: %s\n", cmd.c_str());
                return 2;
            }
            char line[256] = {};
            while (std::fgets(line, sizeof(line), pipe)) {
                if (std::strncmp(line, "RESULT", 6) != 0) continue;
                char v[32] = {};
                unsigned sz = 0;
                double cold = 0, warm = 0, best = 0;
                if (std::sscanf(line, "RESULT %31s %u %lf %lf %lf", v, &sz, &cold, &warm, &best) == 5) {
                    c.colds.push_back(cold);
                    c.warms.push_back(warm);
                }
            }
            ABIX_PCLOSE(pipe);
        }
    }

    std::printf("one (variant, size) per process: %d processes each, first round vs steady state\n\n", processes);
    std::printf("%-6s %-8s | %14s %14s | %14s %14s | %8s\n", "size", "variant", "cold min ns", "cold med ns",
        "warm min ns", "warm med ns", "warm/cold");
    std::printf("%-6s %-8s | %14s %14s | %14s %14s | %8s\n", "", "", "", "", "", "", "");
    for (Combo &c : combos) {
        std::vector<double> cold = c.colds, warm = c.warms;
        if (cold.empty() || warm.empty()) continue;
        const double cold_min = *std::min_element(cold.begin(), cold.end());
        const double cold_med = median(cold);
        const double warm_min = *std::min_element(warm.begin(), warm.end());
        const double warm_med = median(warm);
        std::printf("%-6u %-8s | %14.1f %14.1f | %14.1f %14.1f | %8.2f\n", c.size, c.variant, cold_min, cold_med,
            warm_min, warm_med, warm_med / cold_med);
    }

    // The decision the experiment exists for: reuse vs allocate for the shipped
    // strategy (zero sentinel), per size.
    std::printf("\npooling payoff for the shipped strategy (B): alloc / pool\n");
    std::printf("%-6s | %14s %14s %8s\n", "size", "B_alloc warm", "B_pool warm", "ratio");
    for (uint32_t size : sizes) {
        double alloc_warm = 0, pool_warm = 0;
        for (Combo &c : combos) {
            if (c.size != size) continue;
            if (std::strcmp(c.variant, "B_alloc") == 0) alloc_warm = median(c.warms);
            if (std::strcmp(c.variant, "B_pool") == 0) pool_warm = median(c.warms);
        }
        std::printf("%-6u | %14.1f %14.1f %8.2f\n", size, alloc_warm, pool_warm, alloc_warm / pool_warm);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 5 && std::strcmp(argv[1], "--run-one") == 0) {
        return run_one(argv[2], static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10)),
            static_cast<int>(std::strtol(argv[4], nullptr, 10)));
    }
    if (argc >= 2 && std::strcmp(argv[1], "--spawn") == 0) return spawn_all(argv[0]);

    const uint32_t sizes[] = {1'024, 4'096, 16'384};
    const char *labels[] = {"1K", "4K", "16K"};
    const int iters[] = {2'000, 500, 200};
    const int reps = 9;

    std::printf("HashIndex::build variants (median of %d runs)\n\n", reps);
    std::printf("%-6s | %11s %11s %7s | %11s %7s | %11s %11s\n", "size", "pooled A", "pooled B", "B/A", "pooled D",
        "D/A", "alloc A", "alloc B");
    std::printf("%-6s | %11s %11s %7s | %11s %7s | %11s %11s\n", "", "base ns", "zero ns", "", "gen ns", "",
        "prod ns", "zero ns");

    std::vector<std::array<double, 3>> alloc_cost;   // per size: pooled A, alloc A, ratio
    for (int s = 0; s < 3; ++s) {
        Fixture fix(sizes[s]);
        const uint32_t want = next_pow2(sizes[s] * 2);

        // Pooled: buffer in place, only the fill strategy differs.
        IndexBase base;
        base.adopt(static_cast<BaseSlot *>(std::malloc(sizeof(BaseSlot) * want)), want);
        base.names = &fix.table;
        const double pa = time_builds([&] { base.build(fix.table); }, iters[s], reps);

        IndexZero zero;
        zero.adopt(static_cast<ZeroSlot *>(std::malloc(sizeof(ZeroSlot) * want)), want);
        zero.names = &fix.table;
        const double pb = time_builds([&] { zero.build(fix.table); }, iters[s], reps);

        IndexGen gen;
        gen.reset(want);
        gen.names = &fix.table;
        const double pd = time_builds([&] { gen.build(fix.table); }, iters[s], reps);

        // Alloc: what allocating the buffer per build adds on top.
        const double aa = time_builds(
            [&] {
                skl::abix::runtime::HashIndex idx;
                idx.build(fix.table);
                keep(idx.capacity);
                idx.destroy();
            },
            iters[s], reps);
        const double ab = time_builds(
            [&] {
                IndexZero local;
                local.names = &fix.table;
                local.build(fix.table);
                keep(static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(local.slots)));
            },
            iters[s], reps);

        std::printf("%-6s | %11.1f %11.1f %7.2f | %11.1f %7.2f | %11.1f %11.1f\n", labels[s], pa, pb, pb / pa, pd,
            pd / pa, aa, ab);

        // Allocation overhead for the *implemented* strategy (B: zero sentinel),
        // measured interleaved: the same fill with a pooled buffer vs a fresh
        // allocation per build, round by round, so both see the same allocator state.
        std::vector<double> pooled_rounds, alloc_rounds;
        for (int round = 0; round < 7; ++round) {
            pooled_rounds.push_back(time_builds([&] { zero.build(fix.table); }, iters[s], 1));
            alloc_rounds.push_back(time_builds(
                [&] {
                    IndexZero local;
                    local.names = &fix.table;
                    local.build(fix.table);
                    keep(static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(local.slots)));
                },
                iters[s], 1));
        }
        const double pooled_med = median(pooled_rounds);
        const double alloc_med = median(alloc_rounds);
        alloc_cost.push_back({pooled_med, alloc_med, alloc_med / pooled_med});
    }

    std::printf("\nallocating the slot buffer per build vs reusing it (interleaved, 7 rounds, strategy B)\n");
    std::printf("%-6s | %12s %12s %7s\n", "size", "pooled ns", "per-alloc ns", "ratio");
    for (int s = 0; s < 3; ++s) {
        std::printf("%-6s | %12.1f %12.1f %7.2f\n", labels[s], alloc_cost[s][0], alloc_cost[s][1], alloc_cost[s][2]);
    }

    double bp = 0, zp = 0, gp = 0;
    int mism = 0;
    for (int s = 0; s < 3; ++s) {
        Fixture fix(sizes[s]);
        mism += probes_and_mismatches(fix.table, bp, zp, gp);
        std::printf("\nequivalence %-4s: base_probes=%.3f zero_probes=%.3f gen_probes=%.3f mismatches=%d\n",
            labels[s], bp, zp, gp, mism);
    }
    std::printf("\nslot bytes: A base=%zu  B zero=%zu  D gen=%zu\n", sizeof(BaseSlot), sizeof(ZeroSlot),
        sizeof(GenSlot));
    return 0;
}
