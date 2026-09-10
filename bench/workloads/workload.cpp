#include "workload.hpp"

#include <benchmark/benchmark.h>

#include <assert.h>
#include <vector>

WorkloadSchedule WorkloadProfile::generate_schedule() const {
    // Round up to the nearest multiple of 10 to avoid truncation
    uint32_t total = reader_percent + retire_percent + sync_percent;
    (void)total;
    assert(total == 100 && "percentages must sum to 100");

    uint32_t n = operations_per_iteration;
    // auto *ops = new Operation[n]();
    std::vector<Operation> ops(n);

    // Place Sync operations first (rarest)
    if (sync_percent > 0) {
        uint32_t stride = 100 / sync_percent;
        for (uint32_t i = 0, pos = stride / 2; i < sync_percent * n / 100; ++i, pos += stride) {
            if (pos < n) ops[pos] = Operation::Sync;
        }
    }

    // Place Retire operations
    if (retire_percent > 0) {
        uint32_t stride = 100 / retire_percent;
        for (uint32_t i = 0, pos = stride / 2; i < retire_percent * n / 100; ++i, pos += stride) {
            if (pos < n && ops[pos] == Operation::Read) ops[pos] = Operation::Retire;
        }
    }

    // Fill remaining with Read
    for (uint32_t i = 0; i < n; ++i) {
        if (ops[i] == Operation::Read) ops[i] = Operation::Read;
    }

    // For the owned view, we need the data to live long enough.
    // We store the schedule in a static vector to avoid lifetime issues.
    // This is safe because schedules are generated once at startup.
    static std::vector<Operation> storage = std::move(ops);

    return WorkloadSchedule{
        .name = name,
        .data = storage.data(),
        .size = storage.size(),
    };
}