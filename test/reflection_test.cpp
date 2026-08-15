#include "test_common.hpp"

TEST_CASE("14.reflection_integration", "[refl][prompt5-9]") {
    log_info("Test 14: verify ABIX integrates the static/dynamic reflection facilities of the Reflection library");

    SECTION("5.static_fp_table_validation") {
        using namespace skl::abix::refl;

        using table_type = SRefl::type_list<fn_entry_tag<fn_sig_v<int(int, int)>, URefl::cstr32("add")>,
            fn_entry_tag<fn_sig_v<double(double, double)>, URefl::cstr32("multiply")>,
            fn_entry_tag<fn_sig_v<int()>, URefl::cstr32("calc_state_alive")>>;

        static_assert(has_unique_sigs<table_type>::value, "Table entries must have unique signatures");

        static_assert(find_by_sig<table_type, fn_sig_v<int(int, int)>>::index == 0, "add should be at index 0");
        static_assert(
            find_by_sig<table_type, fn_sig_v<double(double, double)>>::index == 1, "multiply should be at index 1");
        static_assert(find_by_sig<table_type, fn_sig_v<int()>>::index == 2, "calc_state_alive should be at index 2");
        static_assert(
            find_by_sig<table_type, fn_sig_v<float(float)>>::index == -1, "Unknown signature should return -1");

        log_info(" [FP] compile-time function table validation passed: 3 unique signatures, index lookup correct");
    }

    SECTION("6.any_cross_dll_parameter") {
        dll_object lib;
        REQUIRE(lib.load(dll_path("math_dll").c_str()));

        auto add = dll_func<int(int, int)>(lib, "add");
        REQUIRE(add.valid());
        DynamicAny result = dll_func_call_any(add, 3, 7);
        int val = any_cast_val<int>(result);
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

        log_info(" [Registry] CalcState id=%zu, CalcConfig id=%zu, SharedCounter id=%zu", (size_t)calc_state_id,
            (size_t)calc_config_id, (size_t)shared_counter_id);
    }

    SECTION("8.typeinfo_struct_field_access") {
        DynamicTypeInfo ti = make_pod_type_info<TestVec3>("TestVec3");
        REQUIRE(ti.name == std::string("TestVec3"));
        REQUIRE(ti.kind == DRefl::Kind::Struct);
        REQUIRE(ti.size == sizeof(TestVec3));

        log_info(" [TypeInfo] TestVec3: name=%s, size=%zu, kind=Struct", ti.name, ti.size);

        DynamicFieldAccessor field_x = make_offset_field<TestVec3, float, offsetof(TestVec3, x)>("x");
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

    SECTION("9.static_reflection_field_info") {
        using Vec3Info = SRefl::TypeInfo<TestVec3>;

        static_assert(
            Vec3Info::_name == URefl::string_view("TestVec3 [class]"), "Static reflection class name mismatch");

        constexpr auto &x_field = Vec3Info::Registry::_x;
        constexpr auto &y_field = Vec3Info::Registry::_y;
        constexpr auto &z_field = Vec3Info::Registry::_z;
        int size = sizeof(Vec3Info::Registry::_x);

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

    log_info("ABIX successfully integrated the FP/Any/Registry/TypeInfo/StaticRefl facilities of Reflection");
}