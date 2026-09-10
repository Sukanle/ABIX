#pragma once

#include "topology/topology.hpp"

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