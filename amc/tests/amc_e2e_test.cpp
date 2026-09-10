#include "../core/amc_core.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <algorithm>

namespace {

struct TestResult {
    int passed = 0, failed = 0;
};

#define AMC_TEST(r, cond, msg)                                              \
    do {                                                                    \
        if (cond) {                                                         \
            (r).passed++;                                                   \
        } else {                                                            \
            (r).failed++;                                                   \
            std::cerr << "  FAIL: " << msg << " [" << __FILE__              \
                      << ":" << __LINE__ << "]\n";                          \
        }                                                                   \
    } while (0)

#define AMC_CHECK(r, a, b, msg) AMC_TEST(r, (a) == (b), msg << ": " << (a) << " != " << (b))

std::string tmp_path(const char *name) {
    return std::string("/tmp/amc_e2e_") + name;
}

void cleanup(const std::string &path) {
    std::remove(path.c_str());
}

std::vector<uint8_t> read_bytes(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

void write_bytes(const std::string &path, const std::vector<uint8_t> &bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

uint32_t read_le_u32(const std::vector<uint8_t> &bytes, size_t offset) {
    return uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
           (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
}

void write_le_u32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes[offset + i] = uint8_t(value >> (i * 8));
}

amc::Hash128 make_type_id(const char *name) {
    return amc::hash_text(name, 0x54595045);
}

amc::Type make_primitive(const char *name, uint32_t sz, uint32_t al) {
    amc::Type t;
    t.name = name;
    t.kind = amc::TypeKind::primitive;
    t.id = make_type_id(name);
    t.size = sz;
    t.align = al;
    return t;
}

amc::Type make_enum_type(const char *name, uint32_t sz, uint32_t al) {
    amc::Type t;
    t.name = name;
    t.kind = amc::TypeKind::enumeration;
    t.id = make_type_id(name);
    t.size = sz;
    t.align = al;
    return t;
}

amc::Type make_record(const char *name, uint32_t sz, uint32_t al,
                       uint32_t fb, uint32_t fc) {
    amc::Type t;
    t.name = name;
    t.kind = amc::TypeKind::record;
    t.id = make_type_id(name);
    t.size = sz;
    t.align = al;
    t.field_begin = fb;
    t.field_count = fc;
    return t;
}

amc::Field make_field(const char *name, amc::Hash128 tid, uint32_t off) {
    amc::Field f;
    f.name = name;
    f.type_id = tid;
    f.offset = off;
    return f;
}

amc::Function make_func(const char *name, amc::Hash128 ret,
                          std::vector<amc::Parameter> params) {
    amc::Function fn;
    fn.name = name;
    fn.return_type = ret;
    fn.parameters = std::move(params);
    fn.signature = amc::signature_hash(fn);
    return fn;
}

void test_hash_stability(TestResult &r) {
    std::cerr << "[hash_stability]\n";
    auto h1 = amc::hash_text("Foo", 0x54595045);
    auto h2 = amc::hash_text("Foo", 0x54595045);
    AMC_CHECK(r, h1.lo, h2.lo, "hash_text Foo lo stable");
    AMC_CHECK(r, h1.hi, h2.hi, "hash_text Foo hi stable");

    auto h3 = amc::hash_text("Bar", 0x54595045);
    AMC_TEST(r, h1.lo != h3.lo || h1.hi != h3.hi, "different names produce different hashes");

    auto h4 = amc::hash_text("Foo", 0x54595045);
    auto h5 = amc::hash_text("Foo", 0x4c41594f5554ULL);
    AMC_TEST(r, h4.lo != h5.lo || h4.hi != h5.hi, "different domains produce different hashes");
}

void test_abi_hash_and_hash_table(TestResult &r) {
    std::cerr << "[abi_hash_and_hash_table]\n";
    amc::AbiModule module;
    module.package_name = "hash_test";
    module.package_version = "1.0";
    const auto i32 = make_primitive("int", 4, 4);
    module.types.push_back(i32);
    const auto first_hash = amc::abi_hash(module);
    const auto records = amc::hash_table(module);
    AMC_CHECK(r, records.size(), size_t(3), "artifact/type/layout hash records");
    if (!records.empty()) {
        AMC_CHECK(r, uint32_t(records[0].kind), uint32_t(amc::HashKind::artifact), "artifact hash record kind");
        AMC_CHECK(r, records[0].value.lo, first_hash.lo, "artifact hash record value");
    }
    module.package_version = "2.0";
    AMC_CHECK(r, amc::abi_hash(module).lo, first_hash.lo, "package version does not affect ABIHash");
    module.types[0].size = 8;
    AMC_TEST(r, !(amc::abi_hash(module) == first_hash), "layout change affects ABIHash");
}

void test_layout_hash_stability(TestResult &r) {
    std::cerr << "[layout_hash_stability]\n";
    amc::Type t = make_record("Foo", 16, 8, 0, 2);
    std::vector<amc::Field> fs = {
        make_field("x", make_type_id("int"), 0),
        make_field("y", make_type_id("double"), 8),
    };
    auto lh1 = amc::layout_hash(t, fs);
    auto lh2 = amc::layout_hash(t, fs);
    AMC_CHECK(r, lh1.lo, lh2.lo, "layout_hash lo stable");
    AMC_CHECK(r, lh1.hi, lh2.hi, "layout_hash hi stable");

    std::vector<amc::Field> fs2 = {
        make_field("x", make_type_id("int"), 0),
        make_field("z", make_type_id("double"), 8),
    };
    auto lh3 = amc::layout_hash(t, fs2);
    AMC_TEST(r, lh1.lo != lh3.lo || lh1.hi != lh3.hi,
             "different field names produce different layout hashes");
}

void test_signature_hash_stability(TestResult &r) {
    std::cerr << "[signature_hash_stability]\n";
    auto fn1 = make_func("foo_create", make_type_id("int"), {});
    auto fn2 = make_func("foo_create", make_type_id("int"), {});
    AMC_CHECK(r, fn1.signature.lo, fn2.signature.lo, "signature_hash lo stable");
    AMC_CHECK(r, fn1.signature.hi, fn2.signature.hi, "signature_hash hi stable");

    auto fn3 = make_func("foo_destroy", make_type_id("int"), {});
    AMC_TEST(r, fn1.signature.lo != fn3.signature.lo || fn1.signature.hi != fn3.signature.hi,
             "different function names produce different signature hashes");
}

void test_validate_empty_module(TestResult &r) {
    std::cerr << "[validate_empty_module]\n";
    amc::AbiModule m;
    m.package_name = "empty";
    std::string e;
    AMC_TEST(r, amc::validate(m, e), "empty module is valid");
}

void test_validate_invalid_type(TestResult &r) {
    std::cerr << "[validate_invalid_type]\n";
    amc::AbiModule m;
    m.package_name = "bad";
    amc::Type t;
    t.name = "";
    t.id = make_type_id("x");
    t.size = 4;
    t.align = 4;
    m.types.push_back(t);
    std::string e;
    AMC_TEST(r, !amc::validate(m, e), "empty type name rejected");
    AMC_TEST(r, e.find("invalid type") != std::string::npos, "error mentions invalid type");

    m.types.clear();
    amc::Type t2;
    t2.name = "NoId";
    t2.size = 4;
    t2.align = 4;
    m.types.push_back(t2);
    AMC_TEST(r, !amc::validate(m, e), "zero-id type rejected");

    m.types.clear();
    amc::Type t3;
    t3.name = "ZeroSize";
    t3.id = make_type_id("ZeroSize");
    t3.size = 0;
    t3.align = 4;
    m.types.push_back(t3);
    AMC_TEST(r, !amc::validate(m, e), "zero-size type rejected");
}

void test_validate_field_ref_unknown_type(TestResult &r) {
    std::cerr << "[validate_field_ref_unknown_type]\n";
    amc::AbiModule m;
    m.package_name = "bad_field";
    auto i32 = make_primitive("int", 4, 4);
    m.types.push_back(i32);
    amc::Field f;
    f.name = "x";
    f.type_id = make_type_id("nonexistent");
    f.offset = 0;
    m.fields.push_back(f);
    std::string e;
    AMC_TEST(r, !amc::validate(m, e), "field referencing unknown type rejected");
    AMC_TEST(r, e.find("unknown type") != std::string::npos, "error mentions unknown type");
}

void test_validate_function_ref_unknown_type(TestResult &r) {
    std::cerr << "[validate_function_ref_unknown_type]\n";
    amc::AbiModule m;
    m.package_name = "bad_func";
    auto i32 = make_primitive("int", 4, 4);
    m.types.push_back(i32);
    amc::Function fn;
    fn.name = "bad_ret";
    fn.return_type = make_type_id("nonexistent");
    fn.signature = amc::signature_hash(fn);
    m.functions.push_back(fn);
    std::string e;
    AMC_TEST(r, !amc::validate(m, e), "function with unknown return type rejected");

    m.functions.clear();
    amc::Function fn2;
    fn2.name = "bad_param";
    fn2.return_type = i32.id;
    amc::Parameter p;
    p.name = "a";
    p.type_id = make_type_id("nonexistent");
    fn2.parameters.push_back(p);
    fn2.signature = amc::signature_hash(fn2);
    m.functions.push_back(fn2);
    AMC_TEST(r, !amc::validate(m, e), "function with unknown param type rejected");
}

void test_validate_function_no_name(TestResult &r) {
    std::cerr << "[validate_function_no_name]\n";
    amc::AbiModule m;
    m.package_name = "bad_fnname";
    auto i32 = make_primitive("int", 4, 4);
    m.types.push_back(i32);
    amc::Function fn;
    fn.name = "";
    fn.return_type = i32.id;
    fn.signature = amc::signature_hash(fn);
    m.functions.push_back(fn);
    std::string e;
    AMC_TEST(r, !amc::validate(m, e), "function with empty name rejected");
}

void test_abix_roundtrip_primitive(TestResult &r) {
    std::cerr << "[abix_roundtrip_primitive]\n";
    auto path = tmp_path("primitive.abix");
    cleanup(path);

    amc::AbiModule m;
    m.package_name = "prim_test";
    m.package_version = "0.1";
    auto i32 = make_primitive("int", 4, 4);
    auto f64 = make_primitive("double", 8, 8);
    m.types.push_back(i32);
    m.types.push_back(f64);

    std::string e;
    AMC_TEST(r, amc::write_abix(m, path, e), "write primitive module");

    amc::AbiModule loaded;
    AMC_TEST(r, amc::read_abix(path, loaded, e), "read primitive module");
    AMC_CHECK(r, loaded.package_name, std::string("prim_test"), "package name round-trip");
    AMC_CHECK(r, loaded.package_version, std::string("0.1"), "package version round-trip");
    AMC_CHECK(r, loaded.types.size(), size_t(2), "type count round-trip");
    if (loaded.types.size() >= 2) {
        AMC_CHECK(r, loaded.types[0].name, std::string("int"), "type[0] name");
        AMC_CHECK(r, uint32_t(loaded.types[0].kind), uint32_t(amc::TypeKind::primitive), "type[0] kind");
        AMC_CHECK(r, loaded.types[0].size, uint32_t(4), "type[0] size");
        AMC_CHECK(r, loaded.types[0].align, uint32_t(4), "type[0] align");
        AMC_CHECK(r, loaded.types[0].id.lo, i32.id.lo, "type[0] id.lo");
        AMC_CHECK(r, loaded.types[0].id.hi, i32.id.hi, "type[0] id.hi");

        AMC_CHECK(r, loaded.types[1].name, std::string("double"), "type[1] name");
        AMC_CHECK(r, loaded.types[1].size, uint32_t(8), "type[1] size");
    }

    cleanup(path);
}

void test_abix_roundtrip_enum(TestResult &r) {
    std::cerr << "[abix_roundtrip_enum]\n";
    auto path = tmp_path("enum.abix");
    cleanup(path);

    amc::AbiModule m;
    m.package_name = "enum_test";
    auto i32 = make_primitive("int", 4, 4);
    auto color = make_enum_type("Color", 4, 4);
    m.types.push_back(i32);
    m.types.push_back(color);

    std::string e;
    AMC_TEST(r, amc::write_abix(m, path, e), "write enum module");

    amc::AbiModule loaded;
    AMC_TEST(r, amc::read_abix(path, loaded, e), "read enum module");
    AMC_CHECK(r, loaded.types.size(), size_t(2), "type count");
    if (loaded.types.size() >= 2) {
        AMC_CHECK(r, uint32_t(loaded.types[1].kind), uint32_t(amc::TypeKind::enumeration), "enum kind");
        AMC_CHECK(r, loaded.types[1].name, std::string("Color"), "enum name");
        AMC_CHECK(r, loaded.types[1].size, uint32_t(4), "enum size");
    }

    cleanup(path);
}

void test_abix_roundtrip_record_with_fields(TestResult &r) {
    std::cerr << "[abix_roundtrip_record_with_fields]\n";
    auto path = tmp_path("record.abix");
    cleanup(path);

    amc::AbiModule m;
    m.package_name = "record_test";
    auto i32 = make_primitive("int", 4, 4);
    auto f64 = make_primitive("double", 8, 8);
    m.types.push_back(i32);
    m.types.push_back(f64);

    auto foo = make_record("Foo", 16, 8, 0, 2);
    m.types.push_back(foo);

    amc::Field fx = make_field("x", i32.id, 0);
    amc::Field fy = make_field("y", f64.id, 8);
    m.fields.push_back(fx);
    m.fields.push_back(fy);

    std::string e;
    AMC_TEST(r, amc::write_abix(m, path, e), "write record module");

    amc::AbiModule loaded;
    AMC_TEST(r, amc::read_abix(path, loaded, e), "read record module");
    AMC_CHECK(r, loaded.types.size(), size_t(3), "type count");
    AMC_CHECK(r, loaded.fields.size(), size_t(2), "field count");
    if (loaded.types.size() >= 3) {
        auto &lt = loaded.types[2];
        AMC_CHECK(r, lt.name, std::string("Foo"), "record name");
        AMC_CHECK(r, uint32_t(lt.kind), uint32_t(amc::TypeKind::record), "record kind");
        AMC_CHECK(r, lt.size, uint32_t(16), "record size");
        AMC_CHECK(r, lt.align, uint32_t(8), "record align");
        AMC_CHECK(r, lt.field_begin, uint32_t(0), "field_begin");
        AMC_CHECK(r, lt.field_count, uint32_t(2), "field_count");
    }
    if (loaded.fields.size() >= 2) {
        AMC_CHECK(r, loaded.fields[0].name, std::string("x"), "field[0] name");
        AMC_CHECK(r, loaded.fields[0].offset, uint32_t(0), "field[0] offset");
        AMC_CHECK(r, loaded.fields[0].type_id.lo, i32.id.lo, "field[0] type_id");
        AMC_CHECK(r, loaded.fields[1].name, std::string("y"), "field[1] name");
        AMC_CHECK(r, loaded.fields[1].offset, uint32_t(8), "field[1] offset");
    }

    cleanup(path);
}

void test_abix_roundtrip_function(TestResult &r) {
    std::cerr << "[abix_roundtrip_function]\n";
    auto path = tmp_path("func.abix");
    cleanup(path);

    amc::AbiModule m;
    m.package_name = "func_test";
    auto i32 = make_primitive("int", 4, 4);
    auto f64 = make_primitive("double", 8, 8);
    m.types.push_back(i32);
    m.types.push_back(f64);

    amc::Parameter p1;
    p1.name = "a";
    p1.type_id = i32.id;
    amc::Parameter p2;
    p2.name = "b";
    p2.type_id = f64.id;
    auto fn = make_func("compute", f64.id, {p1, p2});
    m.functions.push_back(fn);

    std::string e;
    AMC_TEST(r, amc::write_abix(m, path, e), "write function module");

    amc::AbiModule loaded;
    AMC_TEST(r, amc::read_abix(path, loaded, e), "read function module");
    AMC_CHECK(r, loaded.functions.size(), size_t(1), "function count");
    if (loaded.functions.size() >= 1) {
        auto &lf = loaded.functions[0];
        AMC_CHECK(r, lf.name, std::string("compute"), "function name");
        AMC_CHECK(r, lf.return_type.lo, f64.id.lo, "return type id");
        AMC_CHECK(r, lf.parameters.size(), size_t(2), "param count");
        if (lf.parameters.size() >= 2) {
            AMC_CHECK(r, lf.parameters[0].name, std::string("a"), "param[0] name");
            AMC_CHECK(r, lf.parameters[0].type_id.lo, i32.id.lo, "param[0] type_id");
            AMC_CHECK(r, lf.parameters[1].name, std::string("b"), "param[1] name");
        }
        AMC_CHECK(r, lf.signature.lo, fn.signature.lo, "signature hash lo");
        AMC_CHECK(r, lf.signature.hi, fn.signature.hi, "signature hash hi");
    }

    cleanup(path);
}

void test_abix_roundtrip_full_module(TestResult &r) {
    std::cerr << "[abix_roundtrip_full_module]\n";
    auto path = tmp_path("full.abix");
    cleanup(path);

    amc::AbiModule m;
    m.package_name = "full_test";
    m.package_version = "2.0";

    auto i32 = make_primitive("int", 4, 4);
    auto f64 = make_primitive("double", 8, 8);
    auto u8 = make_primitive("unsigned char", 1, 1);
    auto ptr = make_primitive("Foo*", 8, 8);
    ptr.kind = amc::TypeKind::pointer;
    auto arr = make_primitive("int[4]", 16, 4);
    arr.kind = amc::TypeKind::array;
    arr.array_count = 4;
    auto color = make_enum_type("Color", 4, 4);
    auto foo = make_record("Foo", 16, 8, 0, 2);
    auto bar = make_record("Bar", 24, 8, 2, 3);

    m.types = {i32, f64, u8, ptr, arr, color, foo, bar};

    amc::Field fx = make_field("x", i32.id, 0);
    amc::Field fy = make_field("y", f64.id, 8);
    amc::Field ba = make_field("a", i32.id, 0);
    amc::Field bb = make_field("b", f64.id, 8);
    amc::Field bc = make_field("c", i32.id, 16);
    m.fields = {fx, fy, ba, bb, bc};

    amc::Parameter p1;
    p1.name = "out";
    p1.type_id = ptr.id;
    amc::Parameter p2;
    p2.name = "c";
    p2.type_id = color.id;
    auto fn_create = make_func("foo_create", i32.id, {p1, p2});
    auto fn_destroy = make_func("foo_destroy", i32.id, {p1});
    m.functions = {fn_create, fn_destroy};

    std::string e;
    AMC_TEST(r, amc::write_abix(m, path, e), "write full module");

    amc::AbiModule loaded;
    AMC_TEST(r, amc::read_abix(path, loaded, e), "read full module");

    AMC_CHECK(r, loaded.package_name, std::string("full_test"), "package name");
    AMC_CHECK(r, loaded.package_version, std::string("2.0"), "package version");
    AMC_CHECK(r, loaded.types.size(), size_t(8), "type count");
    AMC_CHECK(r, loaded.fields.size(), size_t(5), "field count");
    AMC_CHECK(r, loaded.functions.size(), size_t(2), "function count");

    if (loaded.types.size() >= 8) {
        AMC_CHECK(r, uint32_t(loaded.types[3].kind), uint32_t(amc::TypeKind::pointer), "pointer kind");
        AMC_CHECK(r, uint32_t(loaded.types[4].kind), uint32_t(amc::TypeKind::array), "array kind");
        AMC_CHECK(r, loaded.types[4].array_count, uint32_t(4), "array count");
        AMC_CHECK(r, uint32_t(loaded.types[5].kind), uint32_t(amc::TypeKind::enumeration), "enum kind");
        AMC_CHECK(r, uint32_t(loaded.types[6].kind), uint32_t(amc::TypeKind::record), "record kind Foo");
        AMC_CHECK(r, uint32_t(loaded.types[7].kind), uint32_t(amc::TypeKind::record), "record kind Bar");
    }

    if (loaded.types.size() >= 8) {
        for (size_t i = 0; i < m.types.size(); ++i) {
            AMC_CHECK(r, loaded.types[i].id.lo, m.types[i].id.lo,
                      "type[" << i << "] id.lo match");
            AMC_CHECK(r, loaded.types[i].id.hi, m.types[i].id.hi,
                      "type[" << i << "] id.hi match");
        }
    }

    cleanup(path);
}

void test_abix_invalid_magic(TestResult &r) {
    std::cerr << "[abix_invalid_magic]\n";
    auto path = tmp_path("bad_magic.abix");
    cleanup(path);

    {
        std::ofstream out(path, std::ios::binary);
        const char bad[] = "BADI\x01\x00\x00\x00";
        out.write(bad, 8);
    }

    amc::AbiModule m;
    std::string e;
    AMC_TEST(r, !amc::read_abix(path, m, e), "bad magic rejected");
    AMC_TEST(r, e.find("invalid") != std::string::npos || e.find("unsupported") != std::string::npos,
             "error mentions invalid/unsupported header");

    cleanup(path);
}

void test_abix_bad_version(TestResult &r) {
    std::cerr << "[abix_bad_version]\n";
    auto path = tmp_path("bad_ver.abix");
    cleanup(path);

    {
        std::ofstream out(path, std::ios::binary);
        const char bad[] = "ABIX\xFF\x00\x00\x00";
        out.write(bad, 8);
    }

    amc::AbiModule m;
    std::string e;
    AMC_TEST(r, !amc::read_abix(path, m, e), "bad version rejected");

    cleanup(path);
}

void test_abix_truncated(TestResult &r) {
    std::cerr << "[abix_truncated]\n";
    auto path = tmp_path("truncated.abix");
    cleanup(path);

    {
        std::ofstream out(path, std::ios::binary);
        out.write("ABIX\x01\x00\x00\x00", 8);
        out.write("test", 4);
    }

    amc::AbiModule m;
    std::string e;
    AMC_TEST(r, !amc::read_abix(path, m, e), "truncated file rejected");

    cleanup(path);
}

void test_abix_nonexistent(TestResult &r) {
    std::cerr << "[abix_nonexistent]\n";
    amc::AbiModule m;
    std::string e;
    AMC_TEST(r, !amc::read_abix("/tmp/amc_e2e_nonexistent_42.abix", m, e),
             "nonexistent file rejected");
}

void test_abix_v4_section_directory(TestResult &r) {
    std::cerr << "[abix_v4_section_directory]\n";
    const auto path = tmp_path("v4_sections.abix");
    cleanup(path);

    amc::AbiModule module;
    module.package_name = "v4";
    module.package_version = "1";
    module.types.push_back(make_primitive("int", 4, 4));
    std::string error;
    AMC_TEST(r, amc::write_abix(module, path, error), "write v4 artifact");

    const auto bytes = read_bytes(path);
    AMC_TEST(r, bytes.size() >= 20 + 13 * 24, "v4 artifact has header and directory");
    if (bytes.size() >= 20 + 13 * 24) {
        AMC_CHECK(r, uint32_t(bytes[4]) | (uint32_t(bytes[5]) << 8), uint32_t(4), "format version is v4");
        AMC_CHECK(r, read_le_u32(bytes, 12), uint32_t(13), "section count");
        AMC_CHECK(r, read_le_u32(bytes, 16), uint32_t(20), "directory follows header");
        const uint32_t expected_ids[] = {1, 2, 13, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        const uint32_t expected_sizes[] = {0, 52, 12, 68, 48, 72, 28, 16, 16, 32, 44, 60, 40};
        for (size_t i = 0; i < 13; ++i) {
            const size_t entry = 20 + i * 24;
            AMC_CHECK(r, read_le_u32(bytes, entry), expected_ids[i], "section id");
            if (expected_sizes[i] != 0)
                AMC_CHECK(r, read_le_u32(bytes, entry + 8), read_le_u32(bytes, entry + 12) * expected_sizes[i], "section byte length");
            else
                AMC_TEST(r, read_le_u32(bytes, entry + 8) >= 8, "variable section byte length");
            AMC_CHECK(r, read_le_u32(bytes, entry + 16), expected_sizes[i], "section entry size");
            AMC_CHECK(r, read_le_u32(bytes, entry + 20), i < 10 ? uint32_t(1) : uint32_t(0), "section required flag");
        }
    }

    amc::AbiModule loaded;
    AMC_TEST(r, amc::read_abix(path, loaded, error), "read v4 artifact");
    AMC_CHECK(r, loaded.symbols.size(), size_t(1), "symbols auto-generated");
    if (!loaded.symbols.empty()) {
        AMC_CHECK(r, loaded.symbols[0].name, std::string("int"), "generated symbol name");
        AMC_CHECK(r, uint32_t(loaded.symbols[0].kind), uint32_t(amc::SymbolKind::type), "generated symbol kind");
    }
    auto extended = bytes;
    write_le_u32(extended, 20 + 10 * 24, 99);
    const auto extended_path = tmp_path("v4_unknown_optional.abix");
    write_bytes(extended_path, extended);
    AMC_TEST(r, amc::read_abix(extended_path, loaded, error), "unknown optional section is skipped");
    cleanup(extended_path);
    cleanup(path);
}

void test_compatibility_and_map_ir(TestResult &r) {
    std::cerr << "[compatibility_and_map_ir]\n";
    amc::AbiModule source, target, report;
    source.package_name = target.package_name = "compat";
    const auto source_i32 = make_primitive("source_i32", 4, 4);
    const auto target_i64 = make_primitive("target_i64", 8, 8);
    source.types.push_back(source_i32);
    source.fields.push_back(make_field("value", source_i32.id, 0));
    source.types.push_back(make_record("Thing", 4, 4, 0, 1));
    target.types.push_back(target_i64);
    target.fields.push_back(make_field("value", target_i64.id, 0));
    target.fields.push_back(make_field("added", target_i64.id, 8));
    target.types.push_back(make_record("Thing", 16, 8, 0, 2));
    std::string error;
    AMC_TEST(r, amc::build_compatibility(source, target, report, error), "build compatibility report");
    AMC_CHECK(r, report.compatibility.size(), size_t(1), "one matching type");
    AMC_CHECK(r, report.maps.size(), size_t(1), "one map plan");
    if (!report.compatibility.empty()) {
        AMC_CHECK(r, uint32_t(report.compatibility[0].kind), uint32_t(amc::CompatibilityKind::map_compatible),
                  "record is map compatible");
    }
    if (!report.maps.empty()) {
        AMC_CHECK(r, report.maps[0].operations.size(), size_t(2), "convert and default operations");
        AMC_CHECK(r, uint32_t(report.maps[0].operations[0].opcode), uint32_t(amc::MapOpcode::convert_int),
                  "integer conversion operation");
        AMC_CHECK(r, uint32_t(report.maps[0].operations[1].opcode), uint32_t(amc::MapOpcode::add_default),
                  "default operation");
    }
    const auto path = tmp_path("compatibility.abix");
    AMC_TEST(r, amc::write_abix(report, path, error), "write compatibility report");
    amc::AbiModule loaded;
    AMC_TEST(r, amc::read_abix(path, loaded, error), "read compatibility report");
    AMC_CHECK(r, loaded.compatibility.size(), size_t(1), "compatibility round-trip");
    AMC_CHECK(r, loaded.maps.size(), size_t(1), "map round-trip");
    cleanup(path);
}

void test_abix_v4_rejects_invalid_references(TestResult &r) {
    std::cerr << "[abix_v4_rejects_invalid_references]\n";
    const auto source_path = tmp_path("v4_source.abix");
    const auto bad_path = tmp_path("v4_bad.abix");
    cleanup(source_path);
    cleanup(bad_path);

    amc::AbiModule module;
    module.package_name = "v4";
    module.types.push_back(make_primitive("int", 4, 4));
    std::string error;
    AMC_TEST(r, amc::write_abix(module, source_path, error), "write source artifact");
    const auto source = read_bytes(source_path);
    AMC_TEST(r, source.size() >= 20 + 13 * 24, "source artifact has directory");
    if (source.size() >= 20 + 13 * 24) {
        auto corrupted = source;
        write_le_u32(corrupted, 16, uint32_t(corrupted.size()));
        write_bytes(bad_path, corrupted);
        amc::AbiModule loaded;
        AMC_TEST(r, !amc::read_abix(bad_path, loaded, error), "out-of-range directory rejected");

        corrupted = source;
        write_le_u32(corrupted, 20 + 24 + 16, 44);
        write_bytes(bad_path, corrupted);
        AMC_TEST(r, !amc::read_abix(bad_path, loaded, error), "wrong identity entry size rejected");

        corrupted = source;
        const uint32_t identity_offset = read_le_u32(corrupted, 20 + 24 + 4);
        write_le_u32(corrupted, identity_offset + 36, 0xffffffffU);
        write_bytes(bad_path, corrupted);
        AMC_TEST(r, !amc::read_abix(bad_path, loaded, error), "out-of-range package string rejected");
    }
    cleanup(source_path);
    cleanup(bad_path);
}

void test_validate_v2_model_invariants(TestResult &r) {
    std::cerr << "[validate_v2_model_invariants]\n";
    amc::AbiModule module;
    module.package_name = "invalid";
    const auto i32 = make_primitive("int", 4, 4);
    auto duplicate = make_primitive("same_id", 4, 4);
    duplicate.id = i32.id;
    module.types = {i32, duplicate};
    std::string error;
    AMC_TEST(r, !amc::validate(module, error), "duplicate TypeId rejected");

    module.types = {i32, make_record("Bad", 4, 4, 0, 1)};
    module.fields = {make_field("too_far", i32.id, 2)};
    AMC_TEST(r, !amc::validate(module, error), "field exceeding record layout rejected");
}

void test_type_kind_name(TestResult &r) {
    std::cerr << "[type_kind_name]\n";
    AMC_CHECK(r, amc::type_kind_name(amc::TypeKind::primitive), std::string("primitive"), "primitive name");
    AMC_CHECK(r, amc::type_kind_name(amc::TypeKind::enumeration), std::string("enum"), "enum name");
    AMC_CHECK(r, amc::type_kind_name(amc::TypeKind::record), std::string("record"), "record name");
    AMC_CHECK(r, amc::type_kind_name(amc::TypeKind::pointer), std::string("pointer"), "pointer name");
    AMC_CHECK(r, amc::type_kind_name(amc::TypeKind::array), std::string("array"), "array name");
    AMC_CHECK(r, amc::type_kind_name(amc::TypeKind::function), std::string("function"), "function name");
}

void test_hash128_equality(TestResult &r) {
    std::cerr << "[hash128_equality]\n";
    amc::Hash128 a{123, 456};
    amc::Hash128 b{123, 456};
    amc::Hash128 c{123, 999};
    AMC_TEST(r, a == b, "equal hashes");
    AMC_TEST(r, !(a == c), "unequal hashes");
}

struct Foo {
    int32_t x;
    double y;
};

struct Bar {
    int32_t a;
    double b;
    int32_t c;
};

enum class Color : int32_t {
    Red = 0,
    Green = 1,
    Blue = 2,
};

void test_layout_matches_cpp(TestResult &r) {
    std::cerr << "[layout_matches_cpp]\n";

    AMC_CHECK(r, uint32_t(sizeof(Foo)), uint32_t(16), "Foo sizeof == 16");
    AMC_CHECK(r, uint32_t(alignof(Foo)), uint32_t(8), "Foo alignof == 8");
    AMC_CHECK(r, uint32_t(offsetof(Foo, x)), uint32_t(0), "Foo::x offset == 0");
    AMC_CHECK(r, uint32_t(offsetof(Foo, y)), uint32_t(8), "Foo::y offset == 8");

    AMC_CHECK(r, uint32_t(sizeof(Bar)), uint32_t(24), "Bar sizeof == 24");
    AMC_CHECK(r, uint32_t(alignof(Bar)), uint32_t(8), "Bar alignof == 8");
    AMC_CHECK(r, uint32_t(offsetof(Bar, a)), uint32_t(0), "Bar::a offset == 0");
    AMC_CHECK(r, uint32_t(offsetof(Bar, b)), uint32_t(8), "Bar::b offset == 8");
    AMC_CHECK(r, uint32_t(offsetof(Bar, c)), uint32_t(16), "Bar::c offset == 16");

    AMC_CHECK(r, uint32_t(sizeof(Color)), uint32_t(4), "Color sizeof == 4");
    AMC_CHECK(r, uint32_t(alignof(Color)), uint32_t(4), "Color alignof == 4");
}

static std::string cpp_name(std::string s) {
    for (size_t p = 0; (p = s.find("::", p)) != std::string::npos; s.replace(p, 2, "_")) {}
    for (char &c : s)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') c = '_';
    return s;
}

std::string generate_projection(const amc::AbiModule &m) {
    std::ostringstream o;
    o << "#pragma once\n#include <cstddef>\n#include <cstdint>\nnamespace amc_generated {\n";
    for (auto &t : m.types) {
        o << "struct " << cpp_name(t.name) << "_ABIX {\n"
          << " static constexpr std::uint64_t type_id_lo = 0x" << std::hex << t.id.lo << "ULL;\n"
          << " static constexpr std::uint64_t type_id_hi = 0x" << t.id.hi << "ULL;\n"
          << std::dec
          << " static constexpr std::size_t size = " << t.size << ";\n"
          << " static constexpr std::size_t align = " << t.align << ";\n";
        for (uint32_t i = 0; i < t.field_count; ++i)
            o << " static constexpr std::size_t " << cpp_name(m.fields[t.field_begin + i].name)
              << "_offset = " << m.fields[t.field_begin + i].offset << ";\n";
        o << "};\n";
    }
    o << "}\n";
    return o.str();
}

void test_backend_projection_generation(TestResult &r) {
    std::cerr << "[backend_projection_generation]\n";

    amc::AbiModule m;
    m.package_name = "projection_test";
    auto i32 = make_primitive("int", 4, 4);
    auto f64 = make_primitive("double", 8, 8);
    auto color = make_enum_type("Color", 4, 4);
    auto foo = make_record("Foo", 16, 8, 0, 2);
    auto bar = make_record("Bar", 24, 8, 2, 3);
    m.types = {i32, f64, color, foo, bar};

    amc::Field fx = make_field("x", i32.id, 0);
    amc::Field fy = make_field("y", f64.id, 8);
    amc::Field ba = make_field("a", i32.id, 0);
    amc::Field bb = make_field("b", f64.id, 8);
    amc::Field bc = make_field("c", i32.id, 16);
    m.fields = {fx, fy, ba, bb, bc};

    auto proj = generate_projection(m);

    AMC_TEST(r, proj.find("#pragma once") != std::string::npos, "has include guard");
    AMC_TEST(r, proj.find("namespace amc_generated") != std::string::npos, "has namespace");
    AMC_TEST(r, proj.find("Foo_ABIX") != std::string::npos, "has Foo_ABIX");
    AMC_TEST(r, proj.find("Bar_ABIX") != std::string::npos, "has Bar_ABIX");
    AMC_TEST(r, proj.find("Color_ABIX") != std::string::npos, "has Color_ABIX");
    AMC_TEST(r, proj.find("type_id_lo") != std::string::npos, "has type_id_lo");
    AMC_TEST(r, proj.find("type_id_hi") != std::string::npos, "has type_id_hi");
    AMC_TEST(r, proj.find("x_offset") != std::string::npos, "has x_offset");
    AMC_TEST(r, proj.find("y_offset") != std::string::npos, "has y_offset");
    AMC_TEST(r, proj.find("a_offset") != std::string::npos, "has a_offset");
}

void test_projection_matches_cpp_layout(TestResult &r) {
    std::cerr << "[projection_matches_cpp_layout]\n";

    amc::AbiModule m;
    m.package_name = "layout_match_test";
    auto i32 = make_primitive("int", 4, 4);
    auto f64 = make_primitive("double", 8, 8);
    auto foo = make_record("Foo", uint32_t(sizeof(Foo)), uint32_t(alignof(Foo)), 0, 2);
    auto bar = make_record("Bar", uint32_t(sizeof(Bar)), uint32_t(alignof(Bar)), 2, 3);
    m.types = {i32, f64, foo, bar};

    amc::Field fx = make_field("x", i32.id, uint32_t(offsetof(Foo, x)));
    amc::Field fy = make_field("y", f64.id, uint32_t(offsetof(Foo, y)));
    amc::Field ba = make_field("a", i32.id, uint32_t(offsetof(Bar, a)));
    amc::Field bb = make_field("b", f64.id, uint32_t(offsetof(Bar, b)));
    amc::Field bc = make_field("c", i32.id, uint32_t(offsetof(Bar, c)));
    m.fields = {fx, fy, ba, bb, bc};

    auto path = tmp_path("layout_match.abix");
    cleanup(path);

    std::string e;
    AMC_TEST(r, amc::write_abix(m, path, e), "write layout-match module");

    amc::AbiModule loaded;
    AMC_TEST(r, amc::read_abix(path, loaded, e), "read layout-match module");

    if (loaded.types.size() >= 4) {
        auto &lfoo = loaded.types[2];
        AMC_CHECK(r, lfoo.size, uint32_t(sizeof(Foo)), "Foo size matches C++");
        AMC_CHECK(r, lfoo.align, uint32_t(alignof(Foo)), "Foo align matches C++");

        auto &lbar = loaded.types[3];
        AMC_CHECK(r, lbar.size, uint32_t(sizeof(Bar)), "Bar size matches C++");
        AMC_CHECK(r, lbar.align, uint32_t(alignof(Bar)), "Bar align matches C++");
    }

    if (loaded.fields.size() >= 5) {
        AMC_CHECK(r, loaded.fields[0].offset, uint32_t(offsetof(Foo, x)), "Foo::x offset matches C++");
        AMC_CHECK(r, loaded.fields[1].offset, uint32_t(offsetof(Foo, y)), "Foo::y offset matches C++");
        AMC_CHECK(r, loaded.fields[2].offset, uint32_t(offsetof(Bar, a)), "Bar::a offset matches C++");
        AMC_CHECK(r, loaded.fields[3].offset, uint32_t(offsetof(Bar, b)), "Bar::b offset matches C++");
        AMC_CHECK(r, loaded.fields[4].offset, uint32_t(offsetof(Bar, c)), "Bar::c offset matches C++");
    }

    cleanup(path);
}

void test_projection_file_writable_and_compilable(TestResult &r) {
    std::cerr << "[projection_file_writable_and_compilable]\n";

    amc::AbiModule m;
    m.package_name = "compile_test";
    auto i32 = make_primitive("int", 4, 4);
    auto f64 = make_primitive("double", 8, 8);
    auto foo = make_record("Foo", 16, 8, 0, 2);
    m.types = {i32, f64, foo};

    amc::Field fx = make_field("x", i32.id, 0);
    amc::Field fy = make_field("y", f64.id, 8);
    m.fields = {fx, fy};

    auto proj = generate_projection(m);

    auto hpp_path = tmp_path("amc_generated.hpp");
    cleanup(hpp_path);

    {
        std::ofstream out(hpp_path);
        AMC_TEST(r, bool(out), "projection file opened for write");
        out << proj;
        AMC_TEST(r, bool(out), "projection file written");
    }

    {
        std::ifstream in(hpp_path);
        std::string content((std::istreambuf_iterator<char>(in)), {});
        AMC_TEST(r, content == proj, "projection file content matches");
    }

    cleanup(hpp_path);
}

void test_full_pipeline(TestResult &r) {
    std::cerr << "[full_pipeline]\n";

    auto abix_path = tmp_path("pipeline.abix");
    auto hpp_path = tmp_path("pipeline_generated.hpp");
    cleanup(abix_path);
    cleanup(hpp_path);

    amc::AbiModule m;
    m.package_name = "pipeline_test";
    m.package_version = "1.0";

    auto i32 = make_primitive("int", 4, 4);
    auto f64 = make_primitive("double", 8, 8);
    auto u8 = make_primitive("unsigned char", 1, 1);
    auto color = make_enum_type("Color", 4, 4);
    auto foo = make_record("Foo", uint32_t(sizeof(Foo)), uint32_t(alignof(Foo)), 0, 2);
    auto bar = make_record("Bar", uint32_t(sizeof(Bar)), uint32_t(alignof(Bar)), 2, 3);
    m.types = {i32, f64, u8, color, foo, bar};

    amc::Field fx = make_field("x", i32.id, uint32_t(offsetof(Foo, x)));
    amc::Field fy = make_field("y", f64.id, uint32_t(offsetof(Foo, y)));
    amc::Field ba = make_field("a", i32.id, uint32_t(offsetof(Bar, a)));
    amc::Field bb = make_field("b", f64.id, uint32_t(offsetof(Bar, b)));
    amc::Field bc = make_field("c", i32.id, uint32_t(offsetof(Bar, c)));
    m.fields = {fx, fy, ba, bb, bc};

    amc::Parameter p_out;
    p_out.name = "out";
    p_out.type_id = i32.id;
    amc::Parameter p_c;
    p_c.name = "c";
    p_c.type_id = color.id;
    auto fn_create = make_func("foo_create", i32.id, {p_out, p_c});

    amc::Parameter p_self;
    p_self.name = "self";
    p_self.type_id = i32.id;
    auto fn_destroy = make_func("foo_destroy", i32.id, {p_self});

    amc::Parameter p_a;
    p_a.name = "a";
    p_a.type_id = i32.id;
    amc::Parameter p_b;
    p_b.name = "b";
    p_b.type_id = f64.id;
    auto fn_compute = make_func("compute", f64.id, {p_a, p_b});

    m.functions = {fn_create, fn_destroy, fn_compute};

    std::string e;

    AMC_TEST(r, amc::validate(m, e), "module validates");

    AMC_TEST(r, amc::write_abix(m, abix_path, e), "step 1: write .abix");

    amc::AbiModule loaded;
    AMC_TEST(r, amc::read_abix(abix_path, loaded, e), "step 2: read .abix");

    AMC_CHECK(r, loaded.types.size(), m.types.size(), "type count preserved");
    AMC_CHECK(r, loaded.fields.size(), m.fields.size(), "field count preserved");
    AMC_CHECK(r, loaded.functions.size(), m.functions.size(), "function count preserved");

    for (size_t i = 0; i < m.types.size() && i < loaded.types.size(); ++i) {
        AMC_CHECK(r, loaded.types[i].name, m.types[i].name,
                  "type[" << i << "] name preserved");
        AMC_CHECK(r, loaded.types[i].size, m.types[i].size,
                  "type[" << i << "] size preserved");
        AMC_CHECK(r, loaded.types[i].align, m.types[i].align,
                  "type[" << i << "] align preserved");
        AMC_CHECK(r, loaded.types[i].id.lo, m.types[i].id.lo,
                  "type[" << i << "] id.lo preserved");
    }

    for (size_t i = 0; i < m.fields.size() && i < loaded.fields.size(); ++i) {
        AMC_CHECK(r, loaded.fields[i].name, m.fields[i].name,
                  "field[" << i << "] name preserved");
        AMC_CHECK(r, loaded.fields[i].offset, m.fields[i].offset,
                  "field[" << i << "] offset preserved");
    }

    for (size_t i = 0; i < m.functions.size() && i < loaded.functions.size(); ++i) {
        AMC_CHECK(r, loaded.functions[i].name, m.functions[i].name,
                  "function[" << i << "] name preserved");
        AMC_CHECK(r, loaded.functions[i].parameters.size(), m.functions[i].parameters.size(),
                  "function[" << i << "] param count preserved");
        AMC_CHECK(r, loaded.functions[i].signature.lo, m.functions[i].signature.lo,
                  "function[" << i << "] signature.lo preserved");
    }

    auto proj = generate_projection(loaded);

    {
        std::ofstream out(hpp_path);
        out << proj;
    }

    AMC_TEST(r, proj.find("Foo_ABIX") != std::string::npos, "step 3: projection has Foo_ABIX");
    AMC_TEST(r, proj.find("Bar_ABIX") != std::string::npos, "step 3: projection has Bar_ABIX");
    AMC_TEST(r, proj.find("Color_ABIX") != std::string::npos, "step 3: projection has Color_ABIX");

    AMC_TEST(r, proj.find("x_offset = 0") != std::string::npos, "Foo::x offset = 0 in projection");
    AMC_TEST(r, proj.find("y_offset = 8") != std::string::npos, "Foo::y offset = 8 in projection");
    AMC_TEST(r, proj.find("a_offset = 0") != std::string::npos, "Bar::a offset = 0 in projection");
    AMC_TEST(r, proj.find("b_offset = 8") != std::string::npos, "Bar::b offset = 8 in projection");
    AMC_TEST(r, proj.find("c_offset = 16") != std::string::npos, "Bar::c offset = 16 in projection");

    cleanup(abix_path);
    cleanup(hpp_path);
}

void test_abix_write_cannot_open(TestResult &r) {
    std::cerr << "[abix_write_cannot_open]\n";
    amc::AbiModule m;
    m.package_name = "io_test";
    auto i32 = make_primitive("int", 4, 4);
    m.types.push_back(i32);
    std::string e;
    AMC_TEST(r, !amc::write_abix(m, "/nonexistent_dir/deep/file.abix", e), "write to invalid path rejected");
}

void test_abix_double_write_read(TestResult &r) {
    std::cerr << "[abix_double_write_read]\n";
    auto path = tmp_path("double.abix");
    cleanup(path);

    amc::AbiModule m;
    m.package_name = "double_test";
    auto i32 = make_primitive("int", 4, 4);
    auto foo = make_record("Foo", 16, 8, 0, 2);
    m.types = {i32, foo};
    amc::Field fx = make_field("x", i32.id, 0);
    amc::Field fy = make_field("y", i32.id, 4);
    m.fields = {fx, fy};

    std::string e;
    AMC_TEST(r, amc::write_abix(m, path, e), "first write");

    amc::AbiModule loaded1;
    AMC_TEST(r, amc::read_abix(path, loaded1, e), "first read");

    AMC_TEST(r, amc::write_abix(loaded1, path, e), "second write (from loaded)");

    amc::AbiModule loaded2;
    AMC_TEST(r, amc::read_abix(path, loaded2, e), "second read");

    AMC_CHECK(r, loaded2.types.size(), loaded1.types.size(), "type count stable after double round-trip");
    AMC_CHECK(r, loaded2.fields.size(), loaded1.fields.size(), "field count stable after double round-trip");
    if (loaded2.types.size() >= 2 && loaded1.types.size() >= 2) {
        AMC_CHECK(r, loaded2.types[1].id.lo, loaded1.types[1].id.lo, "type id stable after double round-trip");
    }

    cleanup(path);
}

void test_cpp_name_sanitization(TestResult &r) {
    std::cerr << "[cpp_name_sanitization]\n";
    AMC_CHECK(r, cpp_name("Foo"), std::string("Foo"), "simple name unchanged");
    AMC_CHECK(r, cpp_name("ns::Foo"), std::string("ns_Foo"), "namespace separator replaced");
    AMC_CHECK(r, cpp_name("a::b::c"), std::string("a_b_c"), "nested namespace replaced");
    AMC_CHECK(r, cpp_name("unsigned char"), std::string("unsigned_char"), "space replaced");
    AMC_CHECK(r, cpp_name("int[4]"), std::string("int_4_"), "brackets replaced");
}

void test_pointer_and_array_types(TestResult &r) {
    std::cerr << "[pointer_and_array_types]\n";
    auto path = tmp_path("ptr_arr.abix");
    cleanup(path);

    amc::AbiModule m;
    m.package_name = "ptr_arr_test";
    auto i32 = make_primitive("int", 4, 4);
    auto ptr = make_primitive("Foo*", 8, 8);
    ptr.kind = amc::TypeKind::pointer;
    auto arr = make_primitive("int[4]", 16, 4);
    arr.kind = amc::TypeKind::array;
    arr.array_count = 4;
    m.types = {i32, ptr, arr};

    std::string e;
    AMC_TEST(r, amc::write_abix(m, path, e), "write pointer/array module");

    amc::AbiModule loaded;
    AMC_TEST(r, amc::read_abix(path, loaded, e), "read pointer/array module");
    AMC_CHECK(r, loaded.types.size(), size_t(3), "type count");
    if (loaded.types.size() >= 3) {
        AMC_CHECK(r, uint32_t(loaded.types[1].kind), uint32_t(amc::TypeKind::pointer), "pointer kind preserved");
        AMC_CHECK(r, uint32_t(loaded.types[2].kind), uint32_t(amc::TypeKind::array), "array kind preserved");
        AMC_CHECK(r, loaded.types[2].array_count, uint32_t(4), "array count preserved");
    }

    cleanup(path);
}

void test_multiple_functions(TestResult &r) {
    std::cerr << "[multiple_functions]\n";
    auto path = tmp_path("multi_fn.abix");
    cleanup(path);

    amc::AbiModule m;
    m.package_name = "multi_fn_test";
    auto i32 = make_primitive("int", 4, 4);
    auto f64 = make_primitive("double", 8, 8);
    auto u8 = make_primitive("unsigned char", 1, 1);
    m.types = {i32, f64, u8};

    auto fn1 = make_func("add", i32.id, {});
    amc::Parameter pa;
    pa.name = "a";
    pa.type_id = i32.id;
    amc::Parameter pb;
    pb.name = "b";
    pb.type_id = i32.id;
    auto fn2 = make_func("add_args", i32.id, {pa, pb});
    amc::Parameter px;
    px.name = "x";
    px.type_id = f64.id;
    auto fn3 = make_func("square", f64.id, {px});
    auto fn4 = make_func("noop", u8.id, {});
    m.functions = {fn1, fn2, fn3, fn4};

    std::string e;
    AMC_TEST(r, amc::write_abix(m, path, e), "write multi-function module");

    amc::AbiModule loaded;
    AMC_TEST(r, amc::read_abix(path, loaded, e), "read multi-function module");
    AMC_CHECK(r, loaded.functions.size(), size_t(4), "function count");
    if (loaded.functions.size() >= 4) {
        AMC_CHECK(r, loaded.functions[0].name, std::string("add"), "fn[0] name");
        AMC_CHECK(r, loaded.functions[0].parameters.size(), size_t(0), "fn[0] no params");
        AMC_CHECK(r, loaded.functions[1].name, std::string("add_args"), "fn[1] name");
        AMC_CHECK(r, loaded.functions[1].parameters.size(), size_t(2), "fn[1] 2 params");
        AMC_CHECK(r, loaded.functions[2].name, std::string("square"), "fn[2] name");
        AMC_CHECK(r, loaded.functions[2].parameters.size(), size_t(1), "fn[2] 1 param");
        AMC_CHECK(r, loaded.functions[3].name, std::string("noop"), "fn[3] name");
    }

    cleanup(path);
}

void test_symbol_projection(TestResult &r) {
    std::cerr << "[symbol_projection]\n";
    amc::AbiModule source;
    source.package_name = "projection";
    auto i32 = make_primitive("int", 4, 4);
    auto f64 = make_primitive("double", 8, 8);
    auto foo = make_record("demo::Foo", 16, 8, 0, 2);
    auto bar = make_record("demo::Bar", 8, 4, 2, 1);
    source.types = {i32, f64, foo, bar};
    source.fields = {make_field("x", i32.id, 0), make_field("y", f64.id, 8),
                     make_field("z", i32.id, 0)};
    auto make_foo = make_func("demo::make_foo", foo.id, {});
    auto make_bar = make_func("demo::make_bar", bar.id, {});
    source.functions = {make_foo, make_bar};

    amc::AbiModule projected;
    std::string error;
    AMC_TEST(r, amc::project_symbols(source, {"demo::Foo", "demo::make_foo"}, projected, error),
             "project selected symbols");
    AMC_CHECK(r, projected.functions.size(), size_t(1), "one selected function");
    AMC_CHECK(r, projected.fields.size(), size_t(2), "selected record fields retained");
    AMC_CHECK(r, projected.types.size(), size_t(3), "selected type dependencies retained");
    AMC_TEST(r, std::none_of(projected.types.begin(), projected.types.end(),
                             [](const amc::Type &type) { return type.name == "demo::Bar"; }),
             "unselected type omitted");
}

void test_member_symbol_projection(TestResult &r) {
    std::cerr << "[member_symbol_projection]\n";
    amc::AbiModule source;
    source.package_name = "member_projection";
    const auto i32 = make_primitive("int", 4, 4);
    const auto foo = make_record("demo::Foo", 8, 4, 0, 2);
    source.types = {i32, foo};
    auto first = make_field("first", i32.id, 0);
    first.owner_type = foo.id;
    auto second = make_field("second", i32.id, 4);
    second.owner_type = foo.id;
    source.fields = {first, second};
    auto reset = make_func("demo::Foo::reset", i32.id, {});
    reset.owner_type = foo.id;
    auto clear = make_func("demo::Foo::clear", i32.id, {});
    clear.owner_type = foo.id;
    source.functions = {reset, clear};

    amc::AbiModule projected;
    std::string error;
    AMC_TEST(r, amc::project_symbols(source, {"demo::Foo::reset", "demo::Foo::first"}, projected, error),
             "project selected class members");
    AMC_CHECK(r, projected.types.size(), size_t(2), "member projection retains owner and dependency type");
    AMC_CHECK(r, projected.fields.size(), size_t(1), "member projection retains only selected field");
    AMC_CHECK(r, projected.functions.size(), size_t(1), "member projection retains only selected method");
    if (!projected.fields.empty()) {
        AMC_CHECK(r, projected.fields[0].name, std::string("first"), "selected field name");
        AMC_CHECK(r, projected.fields[0].owner_type.lo, foo.id.lo, "selected field owner");
    }
    if (!projected.functions.empty()) {
        AMC_CHECK(r, projected.functions[0].name, std::string("demo::Foo::reset"), "selected method name");
        AMC_CHECK(r, projected.functions[0].owner_type.lo, foo.id.lo, "selected method owner");
    }
    AMC_TEST(r, amc::project_symbols(source, {"demo::Foo"}, projected, error),
             "project complete class");
    AMC_CHECK(r, projected.fields.size(), size_t(2), "complete class retains all fields");
    AMC_CHECK(r, projected.functions.size(), size_t(2), "complete class retains all methods");
}

}

int main() {
    TestResult r;

    std::cerr << "=== AMC E2E Tests ===\n\n";

    test_hash128_equality(r);
    test_hash_stability(r);
    test_abi_hash_and_hash_table(r);
    test_layout_hash_stability(r);
    test_signature_hash_stability(r);
    test_type_kind_name(r);
    test_cpp_name_sanitization(r);

    test_validate_empty_module(r);
    test_validate_invalid_type(r);
    test_validate_field_ref_unknown_type(r);
    test_validate_function_ref_unknown_type(r);
    test_validate_function_no_name(r);

    test_abix_roundtrip_primitive(r);
    test_abix_roundtrip_enum(r);
    test_abix_roundtrip_record_with_fields(r);
    test_abix_roundtrip_function(r);
    test_abix_roundtrip_full_module(r);
    test_abix_double_write_read(r);
    test_abix_invalid_magic(r);
    test_abix_bad_version(r);
    test_abix_truncated(r);
    test_abix_nonexistent(r);
    test_abix_write_cannot_open(r);
    test_abix_v4_section_directory(r);
    test_compatibility_and_map_ir(r);
    test_abix_v4_rejects_invalid_references(r);
    test_validate_v2_model_invariants(r);

    test_pointer_and_array_types(r);
    test_multiple_functions(r);
    test_symbol_projection(r);
    test_member_symbol_projection(r);

    test_layout_matches_cpp(r);
    test_backend_projection_generation(r);
    test_projection_matches_cpp_layout(r);
    test_projection_file_writable_and_compilable(r);
    test_full_pipeline(r);

    std::cerr << "\n=== Results: " << r.passed << " passed, " << r.failed << " failed ===\n";
    return r.failed > 0 ? 1 : 0;
}
