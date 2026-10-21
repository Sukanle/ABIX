// Test-only provider used by the cooperative-cancellation IPC test.
//
// The capability polls `amc::lang::cancellation_requested()` at a safe point.
// A CANCEL must therefore turn the long-running projection into
// `kCapabilityCancelled` instead of waiting for the loop to finish.
#include "AMC/Lang/Lang.h"

#include <chrono>
#include <thread>

namespace {

int slow_backend(const char *, const char *) {
    for (int i = 0; i < 5'000; ++i) {
        if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 0;
}

}   // namespace

int main(int argc, char **argv) {
    static const amc::lang::Provider provider{
        "cancel",
        {
          {"backend", slow_backend},
          },
        "0.0.0",
        {"test"},
    };
    return amc::lang::run_provider(argc, argv, provider);
}
