#include <benchmark/benchmark.h>

#include "bench_fmt.hpp"
#include "ebr/ebr_common.hpp"
#include "topology/topology.hpp"
#include "workloads/workload_runner.hpp"

int main(int argc, char **argv) {
    benchmark::MaybeReenterWithoutASLR(argc, argv);

    // Detect hardware topology
    auto topo = detect_cpu_topology();
    fmt::print(stderr, "[ebr_profile] Topology: {}\n", topo.to_string());

    // Print rcu_domain cache-line layout diagnostics
    print_rcu_domain_layout();

    // Print thread counts
    auto threads = default_thread_counts(topo);
    fmt::print(stderr, "[ebr_profile] Thread counts: ");
    for (auto t : threads)
        fmt::print(stderr, "{} ", t);
    fmt::print(stderr, "\n");

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
