#ifndef AMC_ADAPTER_H
#define AMC_ADAPTER_H

#include "amc_core.h"

#include <string>

namespace amc {

// Options controlling what `amc adapter` emits.
struct AdapterOptions {
    // Emit `abix::adapter<Source, Target>` specializations on the generated C++
    // projection types (`<namespace>::<symbol>_ABIX`). Requires the projection
    // header to be included before the adapter, guarded by ABIX_ADAPTER_TYPED.
    bool typed = false;
    std::string typed_namespace = "amc_generated";
    // Emit a C ABI shim (`abix_adapter_apply`) so the generated adapter can be
    // compiled into a standalone shared library.
    bool shim = false;
};

// AI-PA: generate a standalone C++ ABI adapter from a compatibility report.
//
// `report` is the output of build_compatibility(source, target, report): its
// `maps` carry the field-level mapping (copy_field / add_default / skip_field /
// convert_int / convert_float). The generated header applies each mapping from
// raw source memory to raw target memory, so it works without the C++
// projections being present. Integer and floating-point widening/narrowing are
// emitted as casts; unsignedness is not tracked by the model, so casts go
// through signed intermediates.
std::string generate_adapter(const AbiModule &source, const AbiModule &target, const AbiModule &report,
    const AdapterOptions &options, std::string &error);

// Convenience overload using the default options (no typed specializations).
inline std::string generate_adapter(
    const AbiModule &source, const AbiModule &target, const AbiModule &report, std::string &error) {
    return generate_adapter(source, target, report, AdapterOptions{}, error);
}

// C identifier for a qualified ABIX name (`a::B` -> `a_B`).
std::string adapter_symbol(std::string_view name);

}   // namespace amc

#endif
