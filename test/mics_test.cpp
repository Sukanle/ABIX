#include "test_common.hpp"

TEST_CASE("14.mics_integration", "[refl][prompt5-9]") {
    log_info("Test 14: verify ABIX integrates the static/dynamic mics facilities of the mics library");

    SECTION("5.static_fp_table_validation") {

        using table_type =
            SRefl::type_list<skl::abix::refl::fn_entry_tag<skl::abix::fn_sig_v<int(int, int)>, URefl::cstr32("add")>,
                skl::abix::refl::fn_entry_tag<skl::abix::fn_sig_v<double(double, double)>, URefl::cstr32("multiply")>,
                skl::abix::refl::fn_entry_tag<skl::abix::fn_sig_v<int()>, URefl::cstr32("calc_state_alive")>>;

        static_assert(skl::abix::refl::has_unique_sigs<table_type>::value, "Table entries must have unique signatures");

        static_assert(skl::abix::refl::find_by_sig<table_type, skl::abix::fn_sig_v<int(int, int)>>::index == 0,
            "add should be at index 0");
        static_assert(skl::abix::refl::find_by_sig<table_type, skl::abix::fn_sig_v<double(double, double)>>::index == 1,
            "multiply should be at index 1");
        static_assert(skl::abix::refl::find_by_sig<table_type, skl::abix::fn_sig_v<int()>>::index == 2,
            "calc_state_alive should be at index 2");
        static_assert(skl::abix::refl::find_by_sig<table_type, skl::abix::fn_sig_v<float(float)>>::index == -1,
            "Unknown signature should return -1");

        log_info(" [FP] compile-time function table validation passed: 3 unique signatures, index lookup correct");
    }

    SECTION("6.any_cross_dll_parameter") {
        skl::abix::dll_object lib;
        REQUIRE(lib.load(dll_path("math_dll").c_str()));

        auto add = skl::abix::dll_func<int(int, int)>(lib, "add");
        REQUIRE(add.valid());
        skl::abix::DynamicAny result = dll_func_call_any(add, 3, 7);
        int val = skl::abix::any_cast_val<int>(result);
        REQUIRE(val == 10);

        log_info(" [Any] dll_func_call_any(3,7) -> Any -> any_cast_val<int> = %d", val);
    }

    SECTION("7.registry_runtime_lookup") {
        auto calc_state_id = DRefl::type_id_of<CalcState>();
        auto calc_config_id = DRefl::type_id_of<CalcConfig>();
        auto shared_counter_id = DRefl::type_id_of<SharedCounter>();

        REQUIRE(calc_state_id != calc_config_id);
        REQUIRE(calc_config_id != shared_counter_id);
        REQUIRE(calc_state_id != shared_counter_id);

        log_info(" [Registry] CalcState id=(%llu,%llu), CalcConfig id=(%llu,%llu), SharedCounter id=(%llu,%llu)",
            static_cast<unsigned long long>(calc_state_id.lo),
            static_cast<unsigned long long>(calc_state_id.hi),
            static_cast<unsigned long long>(calc_config_id.lo),
            static_cast<unsigned long long>(calc_config_id.hi),
            static_cast<unsigned long long>(shared_counter_id.lo),
            static_cast<unsigned long long>(shared_counter_id.hi));
    }

    SECTION("8.typeinfo_struct_field_access") {
        skl::abix::DynamicTypeInfo ti = skl::abix::make_pod_type_info<TestVec3>("TestVec3");
        REQUIRE(ti.name == std::string("TestVec3"));
        REQUIRE(ti.kind == DRefl::Kind::Struct);
        REQUIRE(ti.size == sizeof(TestVec3));

        log_info(" [TypeInfo] TestVec3: name=%s, size=%zu, kind=Struct", ti.name, ti.size);

        skl::abix::DynamicFieldAccessor field_x =
            skl::abix::make_offset_field<TestVec3, float, offsetof(TestVec3, x)>("x");
        REQUIRE(field_x.info.name == std::string("x"));
        REQUIRE(field_x.info.offset == offsetof(TestVec3, x));

        TestVec3 v = {1.0f, 2.0f, 3.0f};
        float *px = static_cast<float *>(field_x.getter(&v));
        REQUIRE(*px == 1.0f);
        float new_val = 10.0f;
        field_x.setter(&v, &new_val);
        REQUIRE(v.x == 10.0f);

        log_info(" [TypeInfo] TestVec3::x field accessor: name=%s, offset=%u, getter/setter validated",
            field_x.info.name, field_x.info.offset);
    }

    SECTION("9.static_mics_field_info") {
        using Vec3Info = SRefl::TypeInfo<TestVec3>;

        static_assert(
            Vec3Info::_name == URefl::string_view("TestVec3 [class]"), "Static mics class name mismatch");

        constexpr auto &x_field = Vec3Info::Registry::_x;
        constexpr auto &y_field = Vec3Info::Registry::_y;
        constexpr auto &z_field = Vec3Info::Registry::_z;

        static_assert(x_field.getName() == URefl::string_view("x"), "Field name should be 'x'");
        static_assert(y_field.getName() == URefl::string_view("y"), "Field name should be 'y'");
        static_assert(z_field.getName() == URefl::string_view("z"), "Field name should be 'z'");

        static_assert(
            std::is_same_v<remove_cvref_t<decltype(x_field)>::traits::type, float>, "x field type should be float");
        static_assert(
            std::is_same_v<remove_cvref_t<decltype(y_field)>::traits::type, float>, "y field type should be float");
        static_assert(
            std::is_same_v<remove_cvref_t<decltype(z_field)>::traits::type, float>, "z field type should be float");

        static_assert(x_field.is_member(), "x should be a member variable");
        static_assert(!x_field.is_function(), "x should not be a function");
        static_assert(x_field.is_variable(), "x should be a variable");

        log_info(" [StaticRefl] TestVec3: x(Float), y(Float), z(Float) - compile-time field info validated");
    }

    log_info("ABIX successfully integrated the FP/Any/Registry/TypeInfo/StaticRefl facilities of mics");
}

TEST_CASE("runtime TypeId uses the full 128-bit identity") {
    using namespace mics::rt;
    static_assert(sizeof(TypeId) == 16, "runtime TypeId must remain Hash128");
    const auto state = DRefl::type_id_of<CalcState>();
    const auto config = DRefl::type_id_of<CalcConfig>();
    REQUIRE(state != INVALID_TYPE_ID);
    REQUIRE(config != INVALID_TYPE_ID);
    REQUIRE(state != config);
    REQUIRE(state.lo != 0);
    REQUIRE(state.hi != 0);
}
