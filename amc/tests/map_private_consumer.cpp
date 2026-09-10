#include <array>
#include <cstdint>

#include "abix/map.h"
#include "amc_map.hpp"

int main() {
    using Tag = amc_generated::amc_map_MapRecord_ABIX;
    std::array<uint8_t, 4> source{{1, 2, 3, 4}};
    std::array<uint8_t, 8> static_target{{9, 9, 9, 9, 9, 9, 9, 9}};
    std::array<uint8_t, 8> dynamic_target = static_target;

    if (!amc_generated::MapPrivate<Tag, Tag>::apply(static_target.data(), source.data()))
        return 1;

    constexpr skl::abix::model::Field source_fields[] = {{0, {1, 0}, 0, 0, 0, 0}};
    constexpr skl::abix::model::Field target_fields[] = {
        {0, {1, 0}, 0, 0, 0, 0}, {0, {1, 0}, 4, 0, 0, 0}};
    constexpr skl::abix::model::TypeLayout source_layout{4, 4, 0, 1, {1, 1}};
    constexpr skl::abix::model::TypeLayout target_layout{8, 4, 0, 2, {2, 2}};
    constexpr skl::abix::runtime_map::Operation operations[] = {
        {skl::abix::runtime_map::Opcode::copy_field, {}, 0, 0, 4},
        {skl::abix::runtime_map::Opcode::add_default, {}, 1, 1, 4}};
    const skl::abix::runtime_map::MapPlan<2> dynamic_plan({{1, 0}, {2, 0}, 0, 0}, operations, 2);
    if (dynamic_plan.apply(source_layout, target_layout, source_fields, target_fields,
                           dynamic_target.data(), source.data()) != skl::abix::runtime_map::Status::ok)
        return 2;
    return static_target == dynamic_target ? 0 : 3;
}
