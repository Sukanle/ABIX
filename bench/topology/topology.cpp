#include "topology.hpp"

#include <cstdio>
#include <cstring>
#include <thread>

namespace skl::bench {

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif
#if defined(__APPLE__)

// -----------------------------------------------------------------
// Helper: read a sysctl value by name
// -----------------------------------------------------------------
static bool sysctl_read(const char *name, uint32_t &out) {
    uint64_t tmp = 0;
    size_t len = sizeof(tmp);
    if (sysctlbyname(name, &tmp, &len, nullptr, 0) != 0) return false;
    out = static_cast<uint32_t>(tmp);
    return true;
}

CpuTopology detect_cpu_topology() {
    CpuTopology topo;

    // Basic core counts
    sysctl_read("hw.logicalcpu",  topo.logical_cpus);
    sysctl_read("hw.physicalcpu", topo.physical_cpus);

    // Performance level hierarchy (big.LITTLE detection)
    uint32_t nperflevels = 0;
    if (sysctl_read("hw.nperflevels", nperflevels) && nperflevels > 0) {
        for (uint32_t i = 0; i < nperflevels; ++i) {
            char key[64];
            std::snprintf(key, sizeof(key), "hw.perflevel%u.logicalcpu", i);
            uint32_t count = 0;
            if (sysctl_read(key, count)) {
                if (i == 0)
                    topo.performance_cpus = count;
                else
                    topo.efficiency_cpus += count;
            }
        }
        // Single level = homogeneous; clear P/E distinction
        if (nperflevels == 1) {
            topo.performance_cpus = 0;
            topo.efficiency_cpus = 0;
        }
    }

    topo.numa_nodes = 1;
    return topo;
}

#elif defined(__linux__)

// -----------------------------------------------------------------
// Helper: count online CPUs via sysconf
// -----------------------------------------------------------------
static uint32_t count_online_cpus() {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? static_cast<uint32_t>(n) : 1;
}

// -----------------------------------------------------------------
// Helper: count physical CPU packages (NUMA approximation)
// -----------------------------------------------------------------
static uint32_t count_physical_packages() {
    // Read /sys/devices/system/cpu/cpu*/topology/physical_package_id
    // and count unique values.
    std::set<uint32_t> packages;
    uint32_t max_cpus = count_online_cpus();
    for (uint32_t i = 0; i < max_cpus; ++i) {
        char path[128];
        std::snprintf(path, sizeof(path),
                      "/sys/devices/system/cpu/cpu%u/topology/physical_package_id", i);
        std::ifstream f(path);
        uint32_t id = 0;
        if (f >> id) packages.insert(id);
    }
    return std::max(static_cast<uint32_t>(packages.size()), 1u);
}

// -----------------------------------------------------------------
// Helper: count physical cores (core_id dedup)
// -----------------------------------------------------------------
static uint32_t count_physical_cores() {
    std::set<std::pair<uint32_t, uint32_t>> cores; // (package, core_id)
    uint32_t max_cpus = count_online_cpus();
    for (uint32_t i = 0; i < max_cpus; ++i) {
        char path[128];
        std::snprintf(path, sizeof(path),
                      "/sys/devices/system/cpu/cpu%u/topology/core_id", i);
        std::ifstream f(path);
        uint32_t core_id = 0;
        if (f >> core_id) {
            // Also get package ID
            char pkg_path[128];
            std::snprintf(pkg_path, sizeof(pkg_path),
                          "/sys/devices/system/cpu/cpu%u/topology/physical_package_id", i);
            std::ifstream pf(pkg_path);
            uint32_t pkg_id = 0;
            if (pf >> pkg_id) {
                cores.insert({pkg_id, core_id});
            }
        }
    }
    return std::max(static_cast<uint32_t>(cores.size()), 1u);
}

CpuTopology detect_cpu_topology() {
    CpuTopology topo;

    topo.logical_cpus  = count_online_cpus();
    topo.physical_cpus = count_physical_cores();
    topo.numa_nodes    = count_physical_packages();

    // Linux doesn't expose P/E core distinction via sysfs easily.
    // (ACPI CPPC or CPUID leaf 0x1a on Intel hybrid, but not portable.)
    // Leave performance_cpus / efficiency_cpus as 0 (unknown).
    topo.performance_cpus = 0;
    topo.efficiency_cpus  = 0;

    return topo;
}

#elif defined(_WIN32)

CpuTopology detect_cpu_topology() {
    CpuTopology topo;

    // GetActiveProcessorCount returns the number of active processors
    // in the specified group. ALL_PROCESSOR_GROUPS = 0xffff.
    topo.logical_cpus = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (topo.logical_cpus == 0) topo.logical_cpus = 1;

    // Windows doesn't provide a simple API for physical core count.
    // Use GetLogicalProcessorInformationEx for a more accurate count.
    // Fallback: assume logical == physical (no HT detection).
    topo.physical_cpus = topo.logical_cpus;

    // NUMA node count
    topo.numa_nodes = GetActiveProcessorGroupCount();
    if (topo.numa_nodes == 0) topo.numa_nodes = 1;

    topo.performance_cpus = 0;
    topo.efficiency_cpus  = 0;

    return topo;
}

#else
// Fallback for unknown platforms
CpuTopology detect_cpu_topology() {
    CpuTopology topo;
    unsigned hwc = std::thread::hardware_concurrency();
    topo.logical_cpus  = hwc > 0 ? hwc : 1;
    topo.physical_cpus = topo.logical_cpus;
    topo.numa_nodes    = 1;
    topo.performance_cpus = 0;
    topo.efficiency_cpus  = 0;
    return topo;
}
#endif

// ============================================================
// default_thread_counts — geometric progression from topology
//
// Includes P/E-core transition points when available:
//   1, 2, 4, (P-core count), (E-core transition), 8, N
// ============================================================
std::vector<uint32_t> default_thread_counts(const CpuTopology &topo) {
    std::vector<uint32_t> counts;
    uint32_t N = topo.logical_cpus;

    // Geometric progression: 1, 2, 4, ...
    for (uint32_t t = 1; t <= 8 && t <= N; t *= 2) {
        counts.push_back(t);
    }

    // P-core saturation point
    uint32_t p_cores = topo.performance_cpus;
    if (p_cores > 0 && p_cores <= N) {
        counts.push_back(p_cores);
    }

    // P+E transition point: P-core count + 1 (first E-core)
    uint32_t p_transition = p_cores + 1;
    if (p_cores > 0 && p_transition <= N) {
        counts.push_back(p_transition);
    }

    // E-core count (all E-cores, no P-core oversubscription)
    uint32_t e_cores = topo.efficiency_cpus;
    if (e_cores > 0 && e_cores <= N) {
        counts.push_back(e_cores);
    }

    // Midpoint
    uint32_t mid = N / 2;
    if (mid > 8 && mid <= N) {
        counts.push_back(mid);
    }

    // Full capacity
    if (N > 8) {
        counts.push_back(N);
    }

    // Sort and deduplicate
    std::sort(counts.begin(), counts.end());
    auto last = std::unique(counts.begin(), counts.end());
    counts.erase(last, counts.end());

    return counts;
}

// ============================================================
// to_string — human-readable summary
// ============================================================
std::string CpuTopology::to_string() const {
    std::string s;
    s += std::to_string(logical_cpus) + " logical CPUs, ";
    s += std::to_string(physical_cpus) + " physical cores";
    if (performance_cpus > 0 || efficiency_cpus > 0) {
        s += " (" + std::to_string(performance_cpus) + "P + "
           + std::to_string(efficiency_cpus) + "E)";
    }
    s += ", " + std::to_string(numa_nodes) + " NUMA node";
    if (numa_nodes > 1) s += 's';
    return s;
}

} // namespace skl::bench