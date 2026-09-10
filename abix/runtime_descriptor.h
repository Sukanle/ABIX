/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef SKL_ABIX_RUNTIME_DESCRIPTOR_H
#define SKL_ABIX_RUNTIME_DESCRIPTOR_H

#include <stdint.h>

#include "abi_model.h"

namespace skl::abix::runtime {

struct LayoutInfo {
    uint32_t size;
    uint32_t align;
    uint32_t field_count;
};

struct FieldDescriptor {
    const char *name;
    model::TypeId type_id;
    uint32_t offset;
    uint32_t flags;
};

struct TypeDescriptor {
    const char *name;
    model::TypeId type_id;
    model::LayoutHash layout_hash;
    uint32_t flags;
    uint32_t size;
    uint32_t align;
    const FieldDescriptor *fields;
    uint32_t field_count;
};

struct ParameterDescriptor {
    const char *name;
    model::TypeId type_id;
    uint32_t flags;
};

struct FunctionDescriptor {
    const char *name;
    model::SignatureHash signature;
    model::TypeId return_type;
    const ParameterDescriptor *parameters;
    uint32_t parameter_count;
    uint32_t calling_convention;
    uint32_t flags;
};

struct SymbolDescriptor {
    const char *name;
    uint32_t kind;
    uint32_t target_index;
};

struct ModuleDescriptor {
    const char *name;
    const char *version;
    const TypeDescriptor *types;
    uint32_t type_count;
    const FunctionDescriptor *functions;
    uint32_t function_count;
    const SymbolDescriptor *symbols;
    uint32_t symbol_count;
};

inline bool valid(const ModuleDescriptor &module) noexcept {
    if (!module.name || !module.version) return false;
    if ((module.type_count != 0 && !module.types) ||
        (module.function_count != 0 && !module.functions) ||
        (module.symbol_count != 0 && !module.symbols)) return false;
    for (uint32_t i = 0; i < module.type_count; ++i) {
        const auto &type = module.types[i];
        if (!type.name || type.type_id == model::Hash128{} || type.size == 0 || type.align == 0)
            return false;
        if (type.field_count != 0 && !type.fields) return false;
        for (uint32_t j = 0; j < type.field_count; ++j) {
            if (!type.fields[j].name || type.fields[j].type_id == model::Hash128{}) return false;
            if (type.fields[j].offset > type.size) return false;
        }
    }
    for (uint32_t i = 0; i < module.function_count; ++i) {
        const auto &function = module.functions[i];
        if (!function.name || function.return_type == model::Hash128{}) return false;
        if (function.parameter_count != 0 && !function.parameters) return false;
    }
    return true;
}

}  // namespace skl::abix::runtime

#endif  // SKL_ABIX_RUNTIME_DESCRIPTOR_H
