// reload_dll_a.cpp - Hot-Reload simulation (old version)
#include "abix/abix.hpp"

extern "C" int get_version() { return 1; }
extern "C" int helper(int v) { return v + 100; }

SKL_ABIX_DEFINE_TABLE(SKL_ABIX_ENTRY("get_version", get_version), SKL_ABIX_ENTRY("helper", helper), )
