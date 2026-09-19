#ifndef AMC_MATERIALIZE_H
#define AMC_MATERIALIZE_H

#include "amc_core.h"

#include "abix/runtime_descriptor.h"

#include <cstdint>
#include <string>
#include <vector>

namespace amc {

// AI-PM: runtime projection (materialization).
//
// The Metadata Region is the compact, pointer-free ABI image. This turns it
// back into the pointer-rich `ModuleDescriptor` the runtime registry consumes,
// so generated code can register a region-loaded module without any
// compile-time descriptor arrays. The struct owns all backing storage and must
// not be copied or moved after build(): `descriptor` points into its vectors.
struct MaterializedModule {
    std::vector<skl::abix::runtime::FieldDescriptor> fields;
    std::vector<skl::abix::runtime::TypeDescriptor> types;
    std::vector<skl::abix::runtime::ParameterDescriptor> parameters;
    std::vector<skl::abix::runtime::FunctionDescriptor> functions;
    std::vector<skl::abix::runtime::SymbolDescriptor> symbols;
    std::vector<skl::abix::model::TypeDesc> canonical_types;
    std::vector<skl::abix::model::TypeLayout> canonical_layouts;
    std::string name;
    std::string version;
    skl::abix::runtime::ModuleDescriptor descriptor{};

    MaterializedModule() = default;
    MaterializedModule(const MaterializedModule &) = delete;
    MaterializedModule &operator=(const MaterializedModule &) = delete;

    bool build(const AbiModule &module, std::string &error);
    bool build_from_region(const uint8_t *data, size_t size, std::string &error);
    bool build_from_region(const std::vector<uint8_t> &region, std::string &error);
};

}   // namespace amc

#endif
