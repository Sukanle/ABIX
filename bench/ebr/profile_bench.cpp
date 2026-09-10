#include <benchmark/benchmark.h>

#include <cstdio>

#include "ebr/ebr_common.hpp"
#include "topology/topology.hpp"
#include "workloads/workload_runner.hpp"

int main(int argc, char **argv) {
    // Detect hardware topology
    auto topo = detect_cpu_topology();
    fprintf(stderr, "[ebr_profile] Topology: %s\n", topo.to_string().c_str());

    // Print rcu_domain cache-line layout diagnostics
    print_rcu_domain_layout();

    // Print thread counts
    auto threads = default_thread_counts(topo);
    fprintf(stderr, "[ebr_profile] Thread counts: ");
    for (auto t : threads)
        fprintf(stderr, "%u ", t);
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