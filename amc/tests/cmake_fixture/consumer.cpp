#include "amc_metadata.hpp"

int amc_cmake_fixture() {
    return static_cast<int>(amc_generated::AmcTestFoo_ABIX::size);
}
