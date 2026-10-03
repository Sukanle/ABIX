// reload_dll_b.cpp - Hot-Reload simulation (new version)
#include "ABIX/ABIX.h"

extern "C" int get_version() { return 2; }
extern "C" int helper(int v) { return v + 200; }

// clang-format off
SKL_ABIX_DEFINE_TABLE(
    SKL_ABIX_ENTRY("get_version", get_version),
    SKL_ABIX_ENTRY("helper", helper),
)
// clang-format on
