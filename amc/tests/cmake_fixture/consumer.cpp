#include "amc_metadata.hpp"
#include "abix/runtime_registry.h"

int amc_cmake_fixture() {
    skl::abix::runtime::RuntimeRegistry<16> registry;
    const auto status = registry.register_module(amc_generated::amc_module);
    return status == skl::abix::runtime::RuntimeRegisterStatus::ok
               ? static_cast<int>(amc_generated::AmcTestFoo_ABIX::size)
               : -1;
}
