#include <array>
#include <catch2/catch_test_macros.hpp>

#include "ABIX/Model/Map.h"

TEST_CASE("dynamic map copies and initializes fields with bounds checks") {
    using namespace skl::abix;
    constexpr model::Field source_fields[] = {
        {0, {1, 0}, 0, 0, 0, 0}
    };
    constexpr model::Field target_fields[] = {
        {0, {1, 0}, 4, 0, 0, 0},
        {0, {2, 0}, 0, 0, 0, 0},
    };
    constexpr model::TypeLayout source_layout{
        8, 8, 0, 1, {1, 1}
    };
    constexpr model::TypeLayout target_layout{
        8, 8, 0, 2, {2, 2}
    };
    constexpr model::Operation operations[] = {
        { model::Opcode::copy_field, {}, 0, 0, sizeof(uint32_t)},
        {model::Opcode::add_default, {}, 1, 1, sizeof(uint32_t)},
    };
    const model::MapPlan<2> plan(
        {
            {1, 0},
            {2, 0},
            0, 0
    },
        operations, 2);
    std::array<uint8_t, 8> source{1, 2, 3, 4, 0, 0, 0, 0};
    std::array<uint8_t, 8> target{9, 9, 9, 9, 9, 9, 9, 9};

    REQUIRE(plan.apply(source_layout, target_layout, source_fields, target_fields, target.data(), source.data())
            == model::PlanStatus::ok);
    REQUIRE(target[4] == 1);
    REQUIRE(target[5] == 2);
    REQUIRE(target[6] == 3);
    REQUIRE(target[7] == 4);
    REQUIRE(target[0] == 0);
    REQUIRE(target[1] == 0);
    REQUIRE(target[2] == 0);
    REQUIRE(target[3] == 0);
}

TEST_CASE("dynamic map requires a converter for type-changing operations") {
    using namespace skl::abix;
    constexpr model::Field fields[] = {
        {0, {1, 0}, 0, 0, 0, 0}
    };
    constexpr model::TypeLayout layout{
        4, 4, 0, 1, {1, 1}
    };
    constexpr model::Operation operation{model::Opcode::convert_int, {}, 0, 0, 0};
    const model::MapPlan<1> plan(
        {
            {1, 0},
            {2, 0},
            0, 0
    },
        &operation, 1);
    uint32_t source = 7;
    uint32_t target = 0;

    REQUIRE(plan.apply(layout, layout, fields, fields, &target, &source) == model::PlanStatus::invalid_operation);
}
