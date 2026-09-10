// ============================================================
// EBR profile benchmark — topology-aware workload runner
//
// Uses the cross-platform topology detection and workload
// profile framework to register benchmarks dynamically.
//
// This is the "next generation" EBR benchmark entry point.
// It replaces the static BENCHMARK() macros with dynamic
// registration based on the detected CPU topology.
//
// Build:
//   cmake --build build/debug --target ebr_profile_bench
//
// Run:
//   ./build/debug/bin/ebr_profile_bench
//   ./build/debug/bin/ebr_profile_bench --benchmark_filter=BM_EBR_ReaderPhase
// ============================================================

#include <benchmark/benchmark.h>

#include <cstdio>

#include "ebr/ebr_common.hpp"
#include "topology/topology.hpp"
#include "workloads/workload_runner.hpp"

int main(int argc, char **argv) {
    using namespace skl::bench;

    // Detect hardware topology
    auto topo = detect_cpu_topology();
    fprintf(stderr, "[ebr_profile] Topology: %s\n", topo.to_string().c_str());

    // Print rcu_domain cache-line layout diagnostics
    print_rcu_domain_layout();

    // Print thread counts
    auto threads = default_thread_counts(topo);
    fprintf(stderr, "[ebr_profile] Thread counts: ");
    for (auto t : threads) fprintf(stderr, "%u ", t);
    fprintf(stderr, "\n");

    // Register all EBR benchmark groups
    register_ebr_fast_path(topo);
    register_ebr_sync(topo);
#ifdef SKL_ABIX_DEVELOPMENT
    register_ebr_grace(topo);
#endif
    register_ebr_workload(topo);

    // Verify fixed-work semantics before running
    verify_workload_profiles(topo);

    // Run Google Benchmark
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
    ::benchmark::Shutdown();

    return 0;
}