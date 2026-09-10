// ============================================================
// Workload runner — connects workload profiles to Google Benchmark
//
// Provides a clean interface for registering EBR benchmarks
// with dynamic thread counts from topology detection.
//
// Usage:
//   #include "workloads/workload_runner.hpp"
//
//   auto topo = skl::bench::detect_cpu_topology();
//   skl::bench::register_ebr_fast_path(topo);
//   skl::bench::register_ebr_grace(topo);
//   skl::bench::register_ebr_workload(topo);
//
//   BENCHMARK_MAIN();  // or use benchmark::benchmark_main
// ============================================================
#pragma once

#include "topology/topology.hpp"

namespace skl::bench {

// ============================================================
// Register all EBR benchmark groups
// ============================================================

// [Micro] Reader fast path: enter/exit, guard, protected load
void register_ebr_fast_path(const CpuTopology &topo);

// [Mechanism] SyncPhase: pure synchronize scalability (production)
void register_ebr_sync(const CpuTopology &topo);

#ifdef SKL_ABIX_DEVELOPMENT
// [Mechanism] Component cost: retire, grace period (development-only)
void register_ebr_grace(const CpuTopology &topo);
#endif

// [Workload] Profile-based throughput benchmarks
void register_ebr_workload(const CpuTopology &topo);

// [Verification] Print actual operation counts per thread count
// to confirm that the workload is truly fixed-work.
void verify_workload_profiles(const CpuTopology &topo);

} // namespace skl::bench