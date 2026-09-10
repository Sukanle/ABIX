/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */
#ifndef SKL_ABIX_MAP_H
#define SKL_ABIX_MAP_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "abi_model.h"

namespace skl::abix::runtime_map {

enum class Opcode : uint8_t {
    copy_field = 0,
    convert_int = 1,
    convert_float = 2,
    default_field = 3,
    rename_field = 4,
    skip_field = 5,
    add_default = 6,
    call_converter = 7,
};

struct Operation {
    Opcode opcode;
    uint8_t reserved[3];
    uint32_t source_field_index;
    uint32_t target_field_index;
    uint64_t auxiliary;
};

using Converter = bool (*)(const void *source, void *target,
                           uint64_t auxiliary, void *context) noexcept;

enum class Status : uint32_t {
    ok = 0,
    invalid_plan,
    invalid_layout,
    invalid_operation,
    conversion_failed,
};

template<size_t Capacity = 64>
class MapPlan {
public:
    MapPlan(model::MapInfo info, const Operation *operations,
            uint32_t operation_count) noexcept
        : _info(info), _operations(operations), _count(operation_count) {}

    Status apply(const model::TypeLayout &source_layout,
                 const model::TypeLayout &target_layout,
                 const model::Field *source_fields,
                 const model::Field *target_fields,
                 void *target, const void *source,
                 Converter converter = nullptr, void *context = nullptr) const noexcept {
        if (!_operations || _count > Capacity || !target || !source)
            return Status::invalid_plan;
        if (!valid_layout(source_layout, source_fields) ||
            !valid_layout(target_layout, target_fields))
            return Status::invalid_layout;

        for (uint32_t i = 0; i < _count; ++i) {
            const Operation &operation = _operations[i];
            switch (operation.opcode) {
            case Opcode::copy_field:
            case Opcode::rename_field:
                if (!valid_range(operation.source_field_index, source_layout,
                                 source_fields, operation.auxiliary) ||
                    !valid_range(operation.target_field_index, target_layout,
                                 target_fields, operation.auxiliary) ||
                    operation.auxiliary == 0)
                    return Status::invalid_operation;
                memcpy(static_cast<char *>(target) +
                           target_fields[operation.target_field_index].offset,
                       static_cast<const char *>(source) +
                           source_fields[operation.source_field_index].offset,
                       static_cast<size_t>(operation.auxiliary));
                break;
            case Opcode::default_field:
            case Opcode::add_default:
                if (!valid_range(operation.target_field_index, target_layout,
                                 target_fields, operation.auxiliary) ||
                    operation.auxiliary == 0)
                    return Status::invalid_operation;
                memset(static_cast<char *>(target) +
                           target_fields[operation.target_field_index].offset,
                       0, static_cast<size_t>(operation.auxiliary));
                break;
            case Opcode::skip_field:
                if (!valid_field(operation.source_field_index, source_layout,
                                 source_fields))
                    return Status::invalid_operation;
                break;
            case Opcode::convert_int:
            case Opcode::convert_float:
            case Opcode::call_converter:
                if (!converter ||
                    !valid_field(operation.source_field_index, source_layout,
                                 source_fields) ||
                    !valid_field(operation.target_field_index, target_layout,
                                 target_fields) ||
                    !converter(static_cast<const char *>(source) +
                                   source_fields[operation.source_field_index].offset,
                               static_cast<char *>(target) +
                                   target_fields[operation.target_field_index].offset,
                               operation.auxiliary, context))
                    return converter ? Status::conversion_failed
                                     : Status::invalid_operation;
                break;
            default:
                return Status::invalid_operation;
            }
        }
        return Status::ok;
    }

    const model::MapInfo &info() const noexcept { return _info; }
    uint32_t operation_count() const noexcept { return _count; }

private:
    static bool valid_layout(const model::TypeLayout &layout,
                             const model::Field *fields) noexcept {
        return layout.size != 0 && layout.align != 0 &&
               (layout.align & (layout.align - 1)) == 0 &&
               (layout.field_count == 0 || fields != nullptr);
    }

    static bool valid_field(uint32_t index, const model::TypeLayout &layout,
                            const model::Field *fields) noexcept {
        return fields != nullptr && index < layout.field_count &&
               fields[index].offset < layout.size;
    }

    static bool valid_range(uint32_t index, const model::TypeLayout &layout,
                            const model::Field *fields, uint64_t bytes) noexcept {
        return valid_field(index, layout, fields) && bytes <= layout.size &&
               fields[index].offset <= layout.size - bytes;
    }

    model::MapInfo _info;
    const Operation *_operations;
    uint32_t _count;
};

} // namespace skl::abix::runtime_map

#endif // SKL_ABIX_MAP_H
