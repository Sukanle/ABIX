/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0.
 *
 * Runtime descriptor types for ABIX metadata.
 *
 * These structs are the in-memory representation of ABIX metadata used at
 * runtime. Name strings are NOT embedded here -- they live in a separate
 * .abix.names section (or external .abix file) for diagnostic use only.
 * Runtime contract validation relies exclusively on type_id (Hash128).
 *
 * See memory/ABIX_rdata_bloat.md for the design rationale.
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

// Compact field descriptor (no name, 24 bytes).
struct FieldDescriptor {
    model::TypeId type_id;   // 16
    uint32_t offset;         //  4
    uint32_t flags;          //  4
};                           // 24

// Compact type descriptor (no name, 56 bytes).
struct TypeDescriptor {
    const FieldDescriptor *fields;   //  8
    model::TypeId type_id;           // 16
    model::LayoutHash layout_hash;   // 16
    uint32_t flags;                  //  4
    uint32_t size;                   //  4
    uint32_t align;                  //  4
    uint32_t field_count;            //  4
};                                   // 56

// Compact parameter descriptor (no name, 24 bytes).
struct ParameterDescriptor {
    model::TypeId type_id;   // 16
    uint32_t flags;          //  4
    // padding               //  4 (to reach 24)
};                           // 24

// Compact function descriptor (no name, 56 bytes).
struct FunctionDescriptor {
    const ParameterDescriptor *parameters;   //  8
    model::SignatureHash signature;          // 16
    model::TypeId return_type;               // 16
    uint32_t parameter_count;                //  4
    uint32_t calling_convention;             //  4
    uint32_t flags;                          //  4
    // padding                               //  4 (to reach 56 on 64-bit)
};                                           // 56

// Compact symbol descriptor (no name, 8 bytes).
struct SymbolDescriptor {
    uint32_t kind;           //  4
    uint32_t target_index;   //  4
};                           //  8

// Module descriptor retains name and version strings.
// These are logical package identifiers, not diagnostic names.
struct ModuleDescriptor {
    const char *name;                       //  8
    const char *version;                    //  8
    const TypeDescriptor *types;            //  8
    uint32_t type_count;                    //  4
    uint32_t function_count;                //  4
    const FunctionDescriptor *functions;    //  8
    const SymbolDescriptor *symbols;        //  8
    uint32_t symbol_count;                  //  4
    // padding                              //  4
};                                          // 56

inline bool valid(const ModuleDescriptor &module) noexcept {
    if (!module.name || !module.version) return false;
    if ((module.type_count != 0 && !module.types) ||
        (module.function_count != 0 && !module.functions) ||
        (module.symbol_count != 0 && !module.symbols)) return false;
    for (uint32_t i = 0; i < module.type_count; ++i) {
        const auto &type = module.types[i];
        if (type.type_id == model::Hash128{} || type.size == 0 || type.align == 0)
            return false;
        if (type.field_count != 0 && !type.fields) return false;
        for (uint32_t j = 0; j < type.field_count; ++j) {
            if (type.fields[j].type_id == model::Hash128{}) return false;
            if (type.fields[j].offset > type.size) return false;
        }
    }
    for (uint32_t i = 0; i < module.function_count; ++i) {
        const auto &function = module.functions[i];
        if (function.return_type == model::Hash128{}) return false;
        if (function.parameter_count != 0 && !function.parameters) return false;
    }
    return true;
}

}  // namespace skl::abix::runtime

#endif  // SKL_ABIX_RUNTIME_DESCRIPTOR_H