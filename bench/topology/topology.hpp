#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace skl::bench {

struct CpuTopology {
    uint32_t logical_cpus = 1;       // threads / HWCs
    uint32_t physical_cpus = 1;      // physical cores
    uint32_t performance_cpus = 0;   // P-cores (0 = homogeneous)
    uint32_t efficiency_cpus = 0;    // E-cores (0 = homogeneous)
    uint32_t numa_nodes = 1;         // NUMA domains

    // Human-readable summary
    std::string to_string() const;
};

CpuTopology detect_cpu_topology();

std::vector<uint32_t> default_thread_counts(const CpuTopology &topo);

} // namespace skl::bench