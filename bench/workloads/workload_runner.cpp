#include "workload_runner.hpp"
#include "workload.hpp"

#include "abix/abix.hpp"
#include <benchmark/benchmark.h>
#include <string>
#include <vector>

static void register_simple(
    const std::string &name, const std::vector<uint32_t> &thread_counts, void (*bench_fn)(benchmark::State &)) {
    for (auto tc : thread_counts) {
        std::string full = name + "/" + std::to_string(tc) + "T";
        auto *bm = benchmark::RegisterBenchmark(full.c_str(), bench_fn);
        if (bm) bm->Threads(tc);
    }
}

#ifdef SKL_ABIX_DEVELOPMENT
static void register_range(const std::string &name, const std::vector<int64_t> &args,
    const std::vector<uint32_t> &thread_counts, void (*bench_fn)(benchmark::State &)) {
    for (auto tc : thread_counts) {
        for (auto arg : args) {
            std::string full = name + "/" + std::to_string(arg) + "/" + std::to_string(tc) + "T";
            auto *bm = benchmark::RegisterBenchmark(full.c_str(), bench_fn);
            if (bm) {
                bm->Arg(arg);
                bm->Threads(tc);
            }
        }
    }
}

// Register a benchmark only on specific thread counts (not all).
static void register_range_select(const std::string &name, const std::vector<int64_t> &args,
    const std::vector<uint32_t> &thread_counts, const std::vector<uint32_t> &selected,
    void (*bench_fn)(benchmark::State &)) {
    for (auto tc : thread_counts) {
        bool run = false;
        for (auto s : selected) {
            if (tc == s) {
                run = true;
                break;
            }
        }
        if (!run) continue;
        for (auto arg : args) {
            std::string full = name + "/" + std::to_string(arg) + "/" + std::to_string(tc) + "T";
            auto *bm = benchmark::RegisterBenchmark(full.c_str(), bench_fn);
            if (bm) {
                bm->Arg(arg);
                bm->Threads(tc);
            }
        }
    }
}
#endif

static void BM_EBR_EnterExit(benchmark::State &state) {
    auto &domain = skl::abix::rcu_domain::instance();
    for (auto _ : state) {
        domain.enter();
        domain.exit();
    }
}

static void BM_EBR_ProtectedLoad(benchmark::State &state) {
    auto &domain = skl::abix::rcu_domain::instance();
    int payload = 42;
    void *ptr = &payload;
    for (auto _ : state) {
        domain.enter();
        void *p = skl::abix::atomic::load_acquire(&ptr);
        benchmark::DoNotOptimize(p);
        domain.exit();
    }
}

#ifdef SKL_ABIX_DEVELOPMENT
static void BM_EBR_ReaderPhase(benchmark::State &state) {
    auto &domain = skl::abix::rcu_domain::instance();
    for (auto _ : state) {
        domain.enter();
        domain.exit();
    }
}

static void BM_EBR_Retire(benchmark::State &state) {
    auto &domain = skl::abix::rcu_domain::instance();
    int batch = state.range(0);
    for (auto _ : state) {
        for (int i = 0; i < batch; ++i) {
            int *p = new int(42);
            benchmark::DoNotOptimize(p);
            domain.retire(p, [](void *obj) noexcept { delete static_cast<int *>(obj); });
        }
    }
    domain.synchronize();
}
#endif

static void BM_EBR_SyncPhase(benchmark::State &state) {
    auto &domain = skl::abix::rcu_domain::instance();
    domain.synchronize();
    for (auto _ : state) {
        domain.synchronize();
    }
    domain.synchronize();
}

#ifdef SKL_ABIX_DEVELOPMENT
static void BM_EBR_GracePhase(benchmark::State &state) {
    auto &domain = skl::abix::rcu_domain::instance();
    int retire_count = state.range(0);
    domain.synchronize();
    for (auto _ : state) {
        for (int i = 0; i < retire_count; ++i) {
            int *p = new int(42);
            benchmark::DoNotOptimize(p);
            domain.retire(p, [](void *obj) noexcept { delete static_cast<int *>(obj); });
        }
        domain.synchronize();
    }
    domain.synchronize();
}
#endif

static uint64_t execute_schedule(const WorkloadSchedule &schedule, skl::abix::rcu_domain &domain, bool is_writer) {
    uint64_t ops = 0;
    for (size_t i = 0; i < schedule.size; ++i) {
        switch (schedule.data[i]) {
            case Operation::Read:
                domain.enter();
                domain.exit();
                ops++;
                break;
            case Operation::Retire:
                if (is_writer) {
                    int *p = new int(42);
                    benchmark::DoNotOptimize(p);
                    domain.retire(p, [](void *obj) noexcept { delete static_cast<int *>(obj); });
                    ops++;
                }
                break;
            case Operation::Sync:
                if (is_writer) {
                    domain.synchronize();
                    ops++;
                }
                break;
        }
    }
    return ops;
}

static void register_workload_profile(const std::string &group_name, const WorkloadProfile &profile,
    const std::vector<uint32_t> &thread_counts, bool synthetic_mixed) {
    auto schedule = profile.generate_schedule();

    // Fixed total operations — independent of thread count.
    // Each thread does total_ops / num_threads per iteration.
    constexpr uint64_t kTotalOps = 10'000'000;

    // Pre-compute ops per schedule iteration per thread role
    uint64_t writer_ops_per_sched = schedule.size;
    uint64_t reader_ops_per_sched = 0;
    for (size_t i = 0; i < schedule.size; ++i) {
        if (schedule.data[i] == Operation::Read) reader_ops_per_sched++;
    }

    for (auto tc : thread_counts) {
        // Each thread's fixed share of the total work
        uint64_t thread_ops = kTotalOps / tc;

        uint64_t writer_repeats = thread_ops / writer_ops_per_sched;
        uint64_t reader_repeats = reader_ops_per_sched > 0 ? thread_ops / reader_ops_per_sched : 0;

        std::string full = group_name + "/" + profile.name + "/" + std::to_string(tc) + "T";

        auto *bm = benchmark::RegisterBenchmark(full.c_str(),
            [schedule, writer_repeats, reader_repeats, thread_ops, synthetic_mixed](benchmark::State &state) {
                auto &domain = skl::abix::rcu_domain::instance();

                // Synthetic mixed: all threads execute the full schedule.
                // Role-based: only thread 0 (writer) does Retire/Sync.
                bool is_writer = synthetic_mixed ? true : (state.thread_index() == 0);
                uint64_t repeats = is_writer ? writer_repeats : reader_repeats;

                domain.synchronize();
                for (auto _ : state) {
                    for (uint64_t r = 0; r < repeats; ++r) {
                        execute_schedule(schedule, domain, is_writer);
                    }
                }
                // Each thread reports its fixed share.
                // Google Benchmark sums across threads, giving total = kTotalOps.
                state.SetItemsProcessed(state.iterations() * thread_ops);
                domain.synchronize();
            });

        if (bm) bm->Threads(tc);
    }
}

void register_ebr_fast_path(const CpuTopology &topo) {
    (void)topo;
    // Single-threaded only (ns-level precision, no multi-thread noise)
    benchmark::RegisterBenchmark("BM_EBR_EnterExit", BM_EBR_EnterExit);
    benchmark::RegisterBenchmark("BM_EBR_ProtectedLoad", BM_EBR_ProtectedLoad);

#ifdef SKL_ABIX_DEVELOPMENT
    auto threads = default_thread_counts(topo);
    // Multi-threaded reader scalability
    register_simple("BM_EBR_ReaderPhase", threads, BM_EBR_ReaderPhase);
#endif
}

void register_ebr_sync(const CpuTopology &topo) {
    auto threads = default_thread_counts(topo);
    register_simple("BM_EBR_SyncPhase", threads, BM_EBR_SyncPhase);
}

#ifdef SKL_ABIX_DEVELOPMENT
void register_ebr_grace(const CpuTopology &topo) {
    auto threads = default_thread_counts(topo);
    uint32_t N = topo.logical_cpus;

    // Retire — pure retire cost
    register_range("BM_EBR_Retire", {1, 4, 8}, threads, BM_EBR_Retire);

    // SyncPhase — pure synchronize scalability
    register_simple("BM_EBR_SyncPhase", threads, BM_EBR_SyncPhase);

    // GracePhase — retire + synchronize (large object counts)
    register_range("BM_EBR_GracePhase", {1'000, 10'000, 100'000}, threads, BM_EBR_GracePhase);

    // 1M — only on 1T and N (heavy, avoid over-testing)
    register_range_select("BM_EBR_GracePhase", {1'000'000}, threads, {1, N}, BM_EBR_GracePhase);
}
#endif   // SKL_ABIX_DEVELOPMENT

#ifdef SKL_ABIX_DEVELOPMENT

static void register_threshold_sweep(const std::string &group_name, const WorkloadProfile &profile,
    const std::vector<uint32_t> &thread_counts, const std::vector<uint32_t> &thresholds) {
    auto schedule = profile.generate_schedule();

    constexpr uint64_t kTotalOps = 10'000'000;

    for (auto tc : thread_counts) {
        uint64_t thread_ops = kTotalOps / tc;
        uint64_t writer_ops = schedule.size;
        uint64_t repeats = thread_ops / writer_ops;

        for (auto threshold : thresholds) {
            std::string full =
                group_name + "/" + profile.name + "/" + std::to_string(tc) + "T/thr" + std::to_string(threshold);

            auto *bm = benchmark::RegisterBenchmark(
                full.c_str(), [schedule, repeats, thread_ops, threshold](benchmark::State &state) {
                    auto &domain = skl::abix::rcu_domain::instance();
                    uint64_t retired_since_sync = 0;

                    domain.synchronize();
                    for (auto _ : state) {
                        for (uint64_t r = 0; r < repeats; ++r) {
                            for (size_t i = 0; i < schedule.size; ++i) {
                                switch (schedule.data[i]) {
                                    case Operation::Read:
                                        domain.enter();
                                        domain.exit();
                                        break;
                                    case Operation::Retire: {
                                        int *p = new int(42);
                                        benchmark::DoNotOptimize(p);
                                        domain.retire(p, [](void *obj) noexcept { delete static_cast<int *>(obj); });
                                        retired_since_sync++;
                                        if (retired_since_sync >= threshold) {
                                            domain.synchronize();
                                            retired_since_sync = 0;
                                        }
                                    } break;
                                    case Operation::Sync: break;
                                }
                            }
                        }
                    }
                    if (retired_since_sync > 0) {
                        domain.synchronize();
                    }
                    state.SetItemsProcessed(state.iterations() * thread_ops);
                });

            if (bm) bm->Threads(tc);
        }
    }
}
#endif   // SKL_ABIX_DEVELOPMENT

void register_ebr_workload(const CpuTopology &topo) {
    auto threads = default_thread_counts(topo);

    const WorkloadProfile profiles[] = {
        ReadHeavy,
        Balanced,
        WriteHeavy,
    };

    // --- Synthetic Mixed: all threads execute the full schedule ---
    // Every thread does Read + Retire + Sync, preserving the exact
    // operation ratio across all thread counts. True fixed-work scaling.
    for (const auto &profile : profiles) {
        register_workload_profile("BM_EBR_Workload/synthetic_mixed", profile, threads,
            /* synthetic_mixed = */ true);
    }

    // --- Role-Based: only thread 0 does Retire/Sync ---
    // Reader threads only do Read. Writer (thread 0) does Retire + Sync.
    // Models real-world RCU/EBR usage with N readers + 1 writer.
    for (const auto &profile : profiles) {
        register_workload_profile("BM_EBR_Workload/role_based", profile, threads,
            /* synthetic_mixed = */ false);
    }

#ifdef SKL_ABIX_DEVELOPMENT
    // --- Threshold Sweep: synthetic_mixed only ---
    // Explore how retire batching threshold affects throughput.
    // Runs synthetic_mixed for all three profiles with thresholds
    // from 1 (sync after every retire) to 256 (batch 256 retires per sync).
    const std::vector<uint32_t> thresholds = {1, 2, 4, 8, 16, 32, 64, 128, 256};
    for (const auto &profile : profiles) {
        register_threshold_sweep("BM_EBR_WorkloadThreshold/synthetic_mixed", profile, threads, thresholds);
    }
#endif
}

void verify_workload_profiles(const CpuTopology &topo) {
    auto threads = default_thread_counts(topo);
    constexpr uint64_t kTotalOps = 10'000'000;

    const WorkloadProfile profiles[] = {
        ReadHeavy,
        Balanced,
        WriteHeavy,
    };

    std::fprintf(stderr,
        "\n==========================================================\n"
        " Synthetic Mixed — All Threads Execute Full Schedule\n"
        " Total ops should be IDENTICAL across all thread counts.\n"
        "==========================================================\n");
    std::fprintf(
        stderr, " %-15s %-4s  %-12s  %-12s  %-12s  %-12s\n", "Workload", "T", "TotalOps", "Reads", "Retires", "Syncs");
    std::fprintf(
        stderr, " %-15s %-4s  %-12s  %-12s  %-12s  %-12s\n", "--------", "--", "--------", "-----", "-------", "-----");

    bool synthetic_ok = true;
    for (const auto &profile : profiles) {
        auto schedule = profile.generate_schedule();
        uint64_t writer_ops = schedule.size;
        uint64_t reader_ops = 0;
        uint64_t retires = 0;
        uint64_t syncs = 0;
        for (size_t i = 0; i < schedule.size; ++i) {
            switch (schedule.data[i]) {
                case Operation::Read:   reader_ops++; break;
                case Operation::Retire: retires++; break;
                case Operation::Sync:   syncs++; break;
            }
        }

        // Reference: 1T counts
        uint64_t ref_total = (kTotalOps / 1) / writer_ops * writer_ops;
        uint64_t ref_reads = ref_total / writer_ops * reader_ops;
        uint64_t ref_retires = ref_total / writer_ops * retires;
        uint64_t ref_syncs = ref_total / writer_ops * syncs;

        for (auto tc : threads) {
            uint64_t thread_ops = kTotalOps / tc;
            uint64_t reps = thread_ops / writer_ops;
            uint64_t total = reps * writer_ops * tc;
            uint64_t reads = reps * reader_ops * tc;
            uint64_t ret = reps * retires * tc;
            uint64_t syn = reps * syncs * tc;

            bool match = (total == ref_total) && (reads == ref_reads) && (ret == ref_retires) && (syn == ref_syncs);
            if (!match) synthetic_ok = false;

            std::fprintf(stderr, " %-15s %-4u  %-12llu  %-12llu  %-12llu  %-12llu", profile.name, tc,
                (unsigned long long)total, (unsigned long long)reads, (unsigned long long)ret, (unsigned long long)syn);
            if (!match) std::fprintf(stderr, "  ← MISMATCH");
            std::fprintf(stderr, "\n");
        }
    }
    std::fprintf(stderr, synthetic_ok ? "\n ✓ All counts match — synthetic mixed is truly fixed-work.\n"
                                      : "\n ⚠  MISMATCH detected — synthetic mixed is NOT fixed-work.\n");

    std::fprintf(stderr,
        "\n==========================================================\n"
        " Role-Based — Only Thread 0 (Writer) Does Retire/Sync\n"
        " Total ops will differ: readers skip Retire/Sync.\n"
        " This models real-world RCU/EBR usage.\n"
        "==========================================================\n");
    std::fprintf(
        stderr, " %-15s %-4s  %-12s  %-12s  %-12s  %-12s\n", "Workload", "T", "TotalOps", "Reads", "Retires", "Syncs");
    std::fprintf(
        stderr, " %-15s %-4s  %-12s  %-12s  %-12s  %-12s\n", "--------", "--", "--------", "-----", "-------", "-----");

    for (const auto &profile : profiles) {
        auto schedule = profile.generate_schedule();
        uint64_t writer_ops = schedule.size;
        uint64_t reader_ops = 0;
        uint64_t retires = 0;
        uint64_t syncs = 0;
        for (size_t i = 0; i < schedule.size; ++i) {
            switch (schedule.data[i]) {
                case Operation::Read:   reader_ops++; break;
                case Operation::Retire: retires++; break;
                case Operation::Sync:   syncs++; break;
            }
        }

        // Reference: 1T counts (all by writer)
        uint64_t ref_total = (kTotalOps / 1) / writer_ops * writer_ops;
        uint64_t ref_reads = ref_total / writer_ops * reader_ops;
        uint64_t ref_retires = ref_total / writer_ops * retires;
        uint64_t ref_syncs = ref_total / writer_ops * syncs;

        for (auto tc : threads) {
            uint64_t thread_ops = kTotalOps / tc;
            uint64_t writer_reps = thread_ops / writer_ops;
            uint64_t reader_reps = reader_ops > 0 ? thread_ops / reader_ops : 0;

            // Writer (thread 0): all operations
            uint64_t w_total = writer_reps * writer_ops;
            uint64_t w_reads = writer_reps * reader_ops;
            // Readers (threads 1..tc-1): only Read operations
            uint64_t num_readers = tc - 1;
            uint64_t r_total = reader_reps * reader_ops;

            uint64_t total = w_total + (r_total * num_readers);
            uint64_t reads = w_reads + (reader_reps * reader_ops * num_readers);
            uint64_t ret = writer_reps * retires;
            uint64_t syn = writer_reps * syncs;

            bool match = (total == ref_total) && (reads == ref_reads) && (ret == ref_retires) && (syn == ref_syncs);

            std::fprintf(stderr, " %-15s %-4u  %-12llu  %-12llu  %-12llu  %-12llu", profile.name, tc,
                (unsigned long long)total, (unsigned long long)reads, (unsigned long long)ret, (unsigned long long)syn);
            if (!match) std::fprintf(stderr, "  ← MISMATCH with 1T");
            std::fprintf(stderr, "\n");
        }
    }
    fprintf(stderr,
        "\n ⚠  Role-based: Retire/Sync counts decrease as T increases.\n"
        "    This is EXPECTED behavior — it's a different workload.\n"
        "    Synthetic mixed is the correct choice for fixed-work scaling.\n\n");
}