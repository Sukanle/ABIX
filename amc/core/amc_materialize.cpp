#include "amc_materialize.h"
#include "amc_metadata.h"

namespace amc {
namespace {

skl::abix::model::Hash128 to_model(Hash128 value) { return {value.lo, value.hi}; }

}   // namespace

bool MaterializedModule::build(const AbiModule &module, std::string &error) {
    error.clear();
    name = module.package_name.empty() ? std::string("abix_module") : module.package_name;
    version = module.package_version;

    fields.clear();
    fields.reserve(module.fields.size());
    for (const auto &field : module.fields) {
        const skl::abix::runtime::FieldDescriptor runtime_field{to_model(field.type_id), field.offset, field.flags};
        fields.push_back(runtime_field);
    }

    parameters.clear();
    std::vector<uint32_t> parameter_begin(module.functions.size(), 0);
    for (size_t i = 0; i < module.functions.size(); ++i) {
        parameter_begin[i] = static_cast<uint32_t>(parameters.size());
        for (const auto &parameter : module.functions[i].parameters) {
            const skl::abix::runtime::ParameterDescriptor runtime_parameter{
                to_model(parameter.type_id), parameter.flags};
            parameters.push_back(runtime_parameter);
        }
    }

    types.clear();
    types.reserve(module.types.size());
    for (const auto &type : module.types) {
        const skl::abix::runtime::FieldDescriptor *field_base =
            type.field_count == 0 ? nullptr : fields.data() + type.field_begin;
        const skl::abix::runtime::TypeDescriptor runtime_type{field_base, to_model(type.id), to_model(type.layout_hash),
            type.flags, type.size, type.align, type.field_count};
        types.push_back(runtime_type);
    }

    functions.clear();
    functions.reserve(module.functions.size());
    for (size_t i = 0; i < module.functions.size(); ++i) {
        const auto &function = module.functions[i];
        const skl::abix::runtime::ParameterDescriptor *parameter_base =
            function.parameters.empty() ? nullptr : parameters.data() + parameter_begin[i];
        const skl::abix::runtime::FunctionDescriptor runtime_function{parameter_base, to_model(function.signature),
            to_model(function.return_type), static_cast<uint32_t>(function.parameters.size()),
            function.calling_convention, function.flags};
        functions.push_back(runtime_function);
    }

    symbols.clear();
    symbols.reserve(module.symbols.size());
    for (const auto &symbol : module.symbols) {
        const skl::abix::runtime::SymbolDescriptor runtime_symbol{
            static_cast<uint32_t>(symbol.kind), symbol.target_index};
        symbols.push_back(runtime_symbol);
    }

    canonical_types.clear();
    canonical_layouts.clear();
    canonical_types.reserve(module.types.size());
    canonical_layouts.reserve(module.types.size());
    for (size_t i = 0; i < module.types.size(); ++i) {
        const auto &type = module.types[i];
        const skl::abix::model::TypeDesc canonical_type{to_model(type.id), type.flags, 0, static_cast<uint32_t>(i)};
        const skl::abix::model::TypeLayout canonical_layout{
            type.size, type.align, type.field_begin, type.field_count, to_model(type.layout_hash)};
        canonical_types.push_back(canonical_type);
        canonical_layouts.push_back(canonical_layout);
    }

    descriptor.name = name.c_str();
    descriptor.version = version.c_str();
    descriptor.types = types.empty() ? nullptr : types.data();
    descriptor.canonical_types = canonical_types.empty() ? nullptr : canonical_types.data();
    descriptor.canonical_layouts = canonical_layouts.empty() ? nullptr : canonical_layouts.data();
    descriptor.type_count = static_cast<uint32_t>(types.size());
    descriptor.function_count = static_cast<uint32_t>(functions.size());
    descriptor.functions = functions.empty() ? nullptr : functions.data();
    descriptor.symbols = symbols.empty() ? nullptr : symbols.data();
    descriptor.symbol_count = static_cast<uint32_t>(symbols.size());

    if (!skl::abix::runtime::valid(descriptor)) {
        error = "materialized module is not a valid runtime descriptor";
        return false;
    }
    return true;
}

bool MaterializedModule::build_from_region(const uint8_t *data, size_t size, std::string &error) {
    AbiModule module;
    if (!module_from_region(data, size, module, error)) return false;
    return build(module, error);
}

bool MaterializedModule::build_from_region(const std::vector<uint8_t> &region, std::string &error) {
    return build_from_region(region.data(), region.size(), error);
}

}   // namespace amc
