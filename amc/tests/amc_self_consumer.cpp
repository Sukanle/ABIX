#include "amc/core/amc_core.h"

#define AMC_GENERATED_DECLARE_NATIVE_TYPE_TRAITS
#include "amc_core.hpp"

int main() {
    skl::abix::runtime::RuntimeRegistry<128> registry;
    if (registry.register_module(amc_generated::amc_module) !=
        skl::abix::runtime::RuntimeRegisterStatus::ok)
        return 1;
    return registry.type_of<amc::AbiModule>() &&
                   registry.type_of<amc::MapOperation>() &&
                   registry.type_of<amc::CompatibilityRecord>()
               ? 0
               : 2;
}
