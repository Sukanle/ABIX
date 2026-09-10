// ============================================================
// Workload profile framework for EBR benchmarks
//
// Separates "what to measure" (workload) from "how to scale"
// (topology) and "how to measure" (runner).
//
// A WorkloadProfile describes a mixture of EBR operations:
//   Read    — enter/exit (reader)
//   Retire  — retire an object (writer)
//   Sync    — synchronize (writer)
//
// The schedule is a deterministic sequence of operations,
// generated at compile time or setup time, with zero RNG
// overhead during measurement.
// ============================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "topology/topology.hpp"

// atomic unit of work in a benchmark iteration
enum class Operation : uint8_t {
    Read,     // enter() + exit()
    Retire,   // retire an object
    Sync,     // synchronize()
};

// deterministic operation sequence
struct WorkloadSchedule {
    std::string name;
    const Operation *data = nullptr;
    size_t size = 0;
};

// configurable workload parameters
struct WorkloadProfile {
    const char *name;
    uint32_t reader_percent;
    uint32_t retire_percent;
    uint32_t sync_percent;
    uint32_t operations_per_iteration = 100;

    // Generate a deterministic schedule from this profile.
    // The schedule is balanced so that the actual operation
    // ratios match the profile as closely as possible.
    WorkloadSchedule generate_schedule() const;
};

// Predefined workload profiles
constexpr WorkloadProfile ReadHeavy{
    .name = "read_heavy",
    .reader_percent = 90,
    .retire_percent = 9,
    .sync_percent = 1,
};

constexpr WorkloadProfile Balanced{
    .name = "balanced",
    .reader_percent = 70,
    .retire_percent = 20,
    .sync_percent = 10,
};

constexpr WorkloadProfile WriteHeavy{
    .name = "write_heavy",
    .reader_percent = 40,
    .retire_percent = 40,
    .sync_percent = 20,
};

// complete configuration for a benchmark run
struct WorkloadConfig {
    std::string name;
    WorkloadProfile profile;
    WorkloadSchedule schedule;
    uint32_t threads;
    uint32_t working_set;   // number of retired objects per iteration
    bool use_affinity = false;
};
