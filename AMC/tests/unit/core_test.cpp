#include "AMC/Core/Adapter.h"
#include "AMC/Core/Context.h"
#include "AMC/Core/Core.h"
#include "AMC/Core/ELF.h"
#include "AMC/Core/Error.h"
#include "AMC/Core/Json.h"
#include "AMC/Core/Lua.h"
#include "AMC/Core/Materialize.h"
#include "AMC/Core/Metadata.h"
#include "AMC/Core/Query.h"
#include "AMC/Core/Symbol.h"
#include "AMC/Core/Verify.h"

#include "ABIX/Runtime/Registry.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "  FAIL: %s\n", message);
        ++failures;
    }
}

bool contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

amc::Type make_type(const std::string &name, uint32_t size, uint32_t align) {
    amc::Type type;
    type.name = name;
    type.kind = amc::TypeKind::record;
    type.id = amc::hash_text(name, 0x54595045);
    type.size = size;
    type.align = align;
    return type;
}

}   // namespace

int main() {
    // --- .abix round trip -------------------------------------------------
    {
        amc::AbiModule m;
        m.package_name = "test";
        m.types.push_back(make_type("Foo", 8, 8));
        m.types[0].layout_hash = amc::hash_text("Foo", 0x4c41594f);
        amc::Function f;
        f.name = "f";
        f.return_type = m.types[0].id;
        f.signature = amc::signature_hash(f);
        m.functions.push_back(f);

        std::string error;
        check(amc::write_abix(m, "amc_core_test.abix", error), "write_abix");
        amc::AbiModule loaded;
        check(amc::read_abix("amc_core_test.abix", loaded, error), "read_abix");
        check(loaded.types.size() == 1, "loaded type count");
        check(loaded.functions.size() == 1, "loaded function count");
        std::remove("amc_core_test.abix");
    }

    // --- unified error schema --------------------------------------------
    {
        const auto error = amc::make_error(amc::ErrorCategory::io, "read_abix", "cannot open input: x", "x");
        const auto json = amc::error_to_json(error);
        check(contains(json, "\"schema\":\"abix.error/1\""), "error json schema");
        check(contains(json, "\"code\":\"AMC-IO\""), "error json code");
        check(contains(json, "\"category\":\"io\""), "error json category");
        check(contains(json, "\"stage\":\"read_abix\""), "error json stage");
        check(contains(json, "\"file\":\"x\""), "error json file");
        check(contains(amc::error_to_text(error), "amc: io/read_abix: cannot open input: x"), "error text rendering");
        amc::ErrorFormat format = amc::ErrorFormat::text;
        check(amc::parse_error_format("json", format) && format == amc::ErrorFormat::json, "parse_error_format json");
        check(!amc::parse_error_format("yaml", format), "parse_error_format rejects unknown");
    }

    // --- LLM context ------------------------------------------------------
    {
        amc::AbiModule m;
        m.package_name = "demo";
        m.types.push_back(make_type("Foo", 16, 8));
        m.types[0].layout_hash = amc::hash_text("Foo", 0x4c41594f);
        amc::Field field;
        field.owner_type = m.types[0].id;
        field.name = "x";
        field.type_id = amc::hash_text("int", 0x54595045);
        field.offset = 0;
        m.fields.push_back(field);
        m.types[0].field_begin = 0;
        m.types[0].field_count = 1;

        const auto llm = amc::context_to_llm(m, true);
        check(contains(llm, "# ABIX ABI context"), "llm header");
        check(contains(llm, "package: demo"), "llm package");
        check(contains(llm, "counts: types=1 fields=1 functions=0 symbols=0"), "llm counts");
        check(contains(llm, "T0 Foo kind=record size=16 align=8"), "llm type record");
        check(contains(llm, "F0 x owner=T0"), "llm field owner index");
        check(!contains(amc::context_to_llm(m, false), "Foo"), "llm --no-names omits names");

        const auto json = amc::context_to_json(m, true);
        check(contains(json, "\"schema\":\"abix.context/1\""), "context json schema");
        check(contains(json, "\"name\":\"Foo\""), "context json type name");
        check(!contains(amc::context_to_json(m, false), "\"name\":\"Foo\""), "context json --no-names omits names");
    }

    // --- verify contract vs implementation --------------------------------
    {
        amc::AbiModule contract;
        contract.package_name = "pkg";
        contract.types.push_back(make_type("Foo", 8, 8));
        contract.types[0].layout_hash = amc::hash_text("Foo", 0x4c41594f);
        amc::Function fn;
        fn.name = "run";
        fn.owner_type = contract.types[0].id;
        fn.return_type = contract.types[0].id;
        fn.signature = amc::signature_hash(fn);
        contract.functions.push_back(fn);

        amc::AbiModule implementation = contract;
        amc::VerifyResult result;
        std::string error;
        check(amc::verify_modules(contract, implementation, result, error), "verify runs");
        check(result.consistent && result.changes.empty(), "identical modules are consistent");

        implementation.types[0].size = 16;
        implementation.types[0].layout_hash = amc::hash_text("Foo-v2", 0x4c41594f);
        check(amc::verify_modules(contract, implementation, result, error), "verify reruns");
        check(!result.consistent, "layout drift is detected");
        check(result.changes.size() == 1, "one layout change reported");
        check(result.changes[0].kind == amc::ChangeKind::layout_changed, "layout change kind");
        check(contains(amc::verify_result_to_json(result), "\"status\":\"drift\""), "drift json status");
        check(contains(amc::verify_result_to_text(result), "status=drift"), "drift text status");

        amc::AbiModule missing = contract;
        missing.types.clear();
        missing.functions.clear();
        check(amc::verify_modules(contract, missing, result, error), "verify missing type");
        check(!result.consistent, "missing type is drift");
        check(result.changes[0].kind == amc::ChangeKind::type_removed, "type_removed kind");
    }

    // --- metadata region --------------------------------------------------
    {
        amc::AbiModule m;
        m.package_name = "meta";
        m.package_version = "1.2.3";
        m.types.push_back(make_type("Foo", 16, 8));
        m.types[0].layout_hash = amc::hash_text("Foo", 0x4c41594f);
        amc::Field field;
        field.owner_type = m.types[0].id;
        field.name = "x";
        field.type_id = m.types[0].id;
        field.offset = 0;
        m.fields.push_back(field);
        m.types[0].field_begin = 0;
        m.types[0].field_count = 1;

        amc::MetadataOptions options;
        options.build_id = amc::hash_text("build", 1);
        std::vector<uint8_t> region, region_again;
        std::string error;
        check(amc::build_metadata_region(m, options, region, error), "build metadata region");
        check(amc::build_metadata_region(m, options, region_again, error), "rebuild metadata region");
        check(region == region_again, "metadata region is deterministic");

        amc::MetadataHeader header;
        check(amc::read_metadata_header(region, header, error), "read metadata header");
        check(header.type_count == 1 && header.field_count == 1, "metadata counts");
        check(header.build_id == options.build_id, "metadata build id round trip");
        check((header.flags & amc::metadata_flag_value(amc::MetadataFlag::has_names)) != 0, "metadata has names");
        check(amc::verify_metadata_region(region, error), "verify metadata region");
        check(contains(amc::metadata_meta_document(m, header), "metadata_id = \""), "meta document has metadata id");
        check(contains(amc::metadata_header_to_json(header), "\"schema\":\"abix.metadata/1\""), "metadata json schema");

        // Stripping names keeps the region valid, only diagnostic strings go.
        amc::MetadataOptions stripped = options;
        stripped.include_names = false;
        std::vector<uint8_t> bare;
        check(amc::build_metadata_region(m, stripped, bare, error), "build stripped region");
        amc::MetadataHeader bare_header;
        check(amc::read_metadata_header(bare, bare_header, error), "read stripped header");
        check((bare_header.flags & amc::metadata_flag_value(amc::MetadataFlag::has_names)) == 0,
            "stripped region reports no names");
        check(bare_header.names_size == 0, "stripped region has empty names section");
        check(amc::verify_metadata_region(bare, error), "stripped region still verifies");
        check(!(bare_header.metadata_id == header.metadata_id), "names change metadata id");

        // Tampering with the desc section must break MetadataID.
        std::vector<uint8_t> tampered = region;
        tampered[amc::metadata_manifest_size + 8] ^= 0x01;
        check(!amc::verify_metadata_region(tampered, error), "tampered region fails verification");
    }

    // --- ELF section reader ----------------------------------------------
    {
        const char text[] = "not an elf";
        const auto *bytes = reinterpret_cast<const uint8_t *>(text);
        std::vector<uint8_t> content;
        std::string error;
        check(!amc::is_elf(bytes, sizeof(text) - 1), "non-elf input is rejected");
        check(!amc::find_elf_section(bytes, sizeof(text) - 1, ".abix.metadata", content, error),
            "find_elf_section fails on non-elf input");
        check(contains(error, "not an ELF"), "non-elf error is descriptive");

        // A truncated ELF identification must not read out of bounds.
        const unsigned char header[4] = {0x7f, 'E', 'L', 'F'};
        std::vector<amc::ElfSection> sections;
        check(amc::is_elf(header, sizeof(header)), "ELF magic detected");
        check(!amc::read_elf_sections(header, sizeof(header), sections, error), "truncated ELF is rejected");
    }

    // --- Mach-O section reader --------------------------------------------
    {
        const char text[] = "not a macho";
        const auto *bytes = reinterpret_cast<const uint8_t *>(text);
        std::vector<uint8_t> content;
        std::string error;
        check(!amc::is_macho(bytes, sizeof(text) - 1), "non-macho input is rejected");
        check(!amc::find_macho_section(bytes, sizeof(text) - 1, "__abix_metadata", content, error),
            "find_macho_section fails on non-macho input");
        check(!amc::is_binary(bytes, sizeof(text) - 1), "unrecognized input is not a supported binary");

        // 64-bit Mach-O magic (little-endian) with a truncated header.
        const unsigned char header[4] = {0xcf, 0xfa, 0xed, 0xfe};
        std::vector<amc::BinarySection> sections;
        check(amc::is_macho(header, sizeof(header)), "Mach-O magic detected");
        check(amc::is_binary(header, sizeof(header)), "Mach-O is a supported binary");
        check(amc::detect_binary_format(header, sizeof(header)) == amc::BinaryFormat::macho, "Mach-O format detected");
        check(!amc::read_macho_sections(header, sizeof(header), sections, error), "truncated Mach-O is rejected");

        check(amc::native_section_name(amc::BinaryFormat::macho, ".abix.metadata") == "__abix_metadata",
            "canonical metadata name maps to the Mach-O spelling");
        check(amc::native_section_name(amc::BinaryFormat::macho, ".abix.names") == "__abix_names",
            "canonical names section maps to the Mach-O spelling");
        check(amc::native_section_name(amc::BinaryFormat::elf, ".abix.metadata") == ".abix.metadata",
            "ELF canonical section name is unchanged");

        // `find_binary_section` must report an unsupported container clearly.
        check(!amc::find_binary_section(bytes, sizeof(text) - 1, ".abix.metadata", content, error),
            "find_binary_section rejects an unsupported container");
        check(contains(error, "not a supported binary"), "unsupported container error is descriptive");
    }

    // --- query engine -----------------------------------------------------
    {
        amc::AbiModule m;
        m.package_name = "q";
        m.types.push_back(make_type("Foo", 16, 8));
        m.types.push_back(make_type("Bar", 8, 8));
        m.functions.push_back(amc::Function{});
        m.functions.back().name = "run";
        m.functions.back().owner_type = m.types[0].id;
        m.functions.back().return_type = m.types[1].id;
        m.functions.back().signature = amc::signature_hash(m.functions.back());

        check(amc::query_type_indices(m, "Foo").size() == 1, "query type exact");
        check(amc::query_type_indices(m, "oo").size() == 1, "query type substring");
        check(amc::query_type_indices(m, "Nope").empty(), "query type no match");
        check(amc::find_type_by_id(m, m.types[1].id) == &m.types[1], "find type by id");
        check(amc::query_function_indices(m, "run").size() == 1, "query function exact");
        check(amc::query_function_indices(m, "Foo::run").size() == 1, "query function qualified");
        check(amc::query_function_indices(m, "nope").empty(), "query function no match");

        const auto indices = amc::query_type_indices(m, "Foo");
        check(contains(amc::query_types_json(m, indices, true, "Foo"), "\"schema\":\"abix.query/1\""),
            "query type json schema");
        check(contains(amc::query_types_text(m, indices, true), "field x")
                  || contains(amc::query_types_text(m, indices, true), "Foo"),
            "query type text");
        check(contains(amc::query_functions_json(m, amc::query_function_indices(m, "run"), "run"), "return_type_name"),
            "query function json");
    }

    // --- symbol store -----------------------------------------------------
    {
        std::string error;
        const uint8_t raw[] = {0xde, 0xad, 0xbe, 0xef};
        check(amc::hex_encode(raw, sizeof(raw)) == "deadbeef", "hex encode");
        std::vector<uint8_t> decoded;
        check(amc::hex_decode("deadbeef", decoded) && decoded.size() == 4 && decoded[0] == 0xde, "hex decode");
        std::string normalized;
        check(amc::normalize_build_id("0xDEADBEEF", normalized) && normalized == "deadbeef", "normalize build id");
        check(!amc::normalize_build_id("xyz", normalized), "normalize rejects non-hex");

        const std::string root = "amc_symbol_test_store";
        const std::string key = "deadbeef";
        check(contains(amc::symbol_store_path(root, key, ".abix"), "de/adbeef.abix"), "symbol store path layout");

        const std::vector<uint8_t> artifact = {1, 2, 3, 4};
        std::string stored;
        check(amc::publish_symbol(root, key, artifact, "meta", ".abix", stored, error), "publish symbol");
        std::vector<uint8_t> fetched;
        std::string fetched_path;
        check(amc::fetch_symbol(root, key, fetched, fetched_path, error), "fetch symbol");
        check(fetched == artifact, "fetched artifact matches");
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    // --- JSON -------------------------------------------------------------
    {
        amc::Json value;
        std::string error;
        check(amc::parse_json("{\"a\":[1,2,{\"b\":\"x\\n\"}],\"c\":true,\"d\":null}", value, error), "parse json");
        check(value.find("c") != nullptr && value.find("c")->as_bool(false), "json bool");
        check(value.find("a") != nullptr && value.find("a")->items().size() == 3, "json array");
        check(value.find("d") != nullptr && value.find("d")->is_null(), "json null");
        const std::string dumped = value.dump();
        amc::Json round;
        check(amc::parse_json(dumped, round, error), "reparse json");
        check(round.dump() == dumped, "json round-trips");
        check(!amc::parse_json("{bad}", round, error), "reject invalid json");
        check(!amc::parse_json("{\"a\":1} trailing", round, error), "reject trailing input");

        check(amc::parse_json("// head\n{\"a\": 1, /* middle */}", round, error), "accept json comments");
        check(amc::parse_json("[1, 2,]", round, error) && round.items().size() == 2, "accept array trailing comma");
        check(amc::parse_json("{\"a\": 1,}", round, error), "accept object trailing comma");
        check(amc::parse_json("\"foo\\\nbar\"", round, error) && round.as_string() == "foobar",
            "accept LF string continuation");
        check(amc::parse_json("\"foo\\\r\nbar\"", round, error) && round.as_string() == "foobar",
            "accept CRLF string continuation");
        check(!amc::parse_json("\"foo\nbar\"", round, error), "reject raw newline in string");
        check(!amc::parse_json("{'a': 1}", round, error), "reject single quote extension");
        check(!amc::parse_json("{a: 1}", round, error), "reject unquoted key extension");
        check(!amc::parse_json("0x10", round, error), "reject hexadecimal extension");
        check(!amc::parse_json("NaN", round, error), "reject NaN extension");

        amc::Json copied(value);
        copied.set("c", false);
        check(value.find("c")->as_bool(true) && !copied.find("c")->as_bool(true), "json deep copy");
        amc::Json moved(std::move(copied));
        check(moved.find("a") != nullptr, "json move");
        value.set("a", 42);
        check(value.find("a")->as_number() == 42, "json replacement invalidates cache");
        amc::Json numbers = amc::Json::array();
        numbers.push_back(3);
        check(numbers.items().size() == 1 && numbers.items()[0].as_number() == 3, "json array mutation");

        amc::Json pooled;
        check(amc::parse_json("{\"a\":1}", pooled, error), "pooled parse");
        pooled.set("b", 2);
        check(pooled.find("b") != nullptr && pooled.dump() == "{\"a\":1,\"b\":2}", "modify pooled root");

        amc::Json built = amc::Json::object();
        built.set("x", 1);
        built.set("y", "text");
        check(built.dump() == "{\"x\":1,\"y\":\"text\"}", "json serialize object");
    }

    // --- region -> module -------------------------------------------------
    {
        amc::AbiModule m;
        m.package_name = "roundtrip";
        m.types.push_back(make_type("Foo", 16, 8));
        m.types[0].layout_hash = amc::hash_text("Foo", 0x4c41594f);
        amc::Field field;
        field.owner_type = m.types[0].id;
        field.name = "x";
        field.type_id = m.types[0].id;
        field.offset = 0;
        m.fields.push_back(field);
        m.types[0].field_begin = 0;
        m.types[0].field_count = 1;
        amc::Function function;
        function.name = "run";
        function.owner_type = m.types[0].id;
        function.return_type = m.types[0].id;
        function.signature = amc::signature_hash(function);
        function.parameters.push_back({"a", m.types[0].id, 0});
        m.functions.push_back(function);

        amc::MetadataOptions options;
        std::vector<uint8_t> region;
        std::string error;
        check(amc::build_metadata_region(m, options, region, error), "build region for decode");
        amc::AbiModule decoded;
        check(amc::module_from_region(region, decoded, error), "decode region to module");
        check(decoded.types.size() == 1 && decoded.types[0].name == "Foo", "decoded type name");
        check(decoded.types[0].id == m.types[0].id, "decoded type id");
        check(decoded.fields.size() == 1 && decoded.fields[0].name == "x" && decoded.fields[0].offset == 0,
            "decoded field");
        check(decoded.functions.size() == 1
                  && decoded.functions[0].parameters.size() == 1
                  && decoded.functions[0].parameters[0].name == "a",
            "decoded function parameters");
        check(amc::abi_hash(decoded) == amc::abi_hash(m), "decoded module canonical hash matches");

        // Names are optional: decode the stripped variant without names.
        amc::MetadataOptions stripped = options;
        stripped.include_names = false;
        std::vector<uint8_t> bare;
        check(amc::build_metadata_region(m, stripped, bare, error), "build stripped region for decode");
        amc::AbiModule bare_module;
        check(amc::module_from_region(bare, bare_module, error), "decode stripped region");
        check(bare_module.types.size() == 1 && bare_module.types[0].name.empty(),
            "stripped region decodes without names");
        check(bare_module.types[0].id == m.types[0].id && bare_module.types[0].layout_hash == m.types[0].layout_hash,
            "stripped region still carries ABI identity");
    }

    // --- Lua contract generation -----------------------------------------
    {
        amc::AbiModule m;
        m.package_name = "counter";
        m.functions.push_back(amc::Function{});
        m.functions.back().name = "counter_add";
        m.functions.back().signature = amc::hash_text("counter_add", 1);
        m.functions.back().parameters.push_back({"a", amc::Hash128{}, 0});
        m.functions.back().parameters.push_back({"b", amc::Hash128{}, 0});

        std::string error;
        const std::string contract = amc::generate_lua_contract(m, error);
        check(error.empty(), "lua generation succeeds");
        check(contains(contract, "#include \"Aue/Aue.h\""), "lua contract includes Aue");
        check(contains(contract, "aue::Contract"), "lua contract type");
        check(contains(contract, "\"add\", 2, counter_add"), "lua entry name/arity/symbol");
        check(contains(contract, "\"counter\""), "lua module name");
        check(amc::lua_function_symbol("Foo::bar") == "Foo_bar", "lua symbol sanitizes scope");
        check(amc::lua_function_symbol("weird-name") == "weird_name", "lua symbol sanitizes chars");
        check(amc::lua_entry_name("counter", "counter_add") == "add", "lua entry strips package prefix");
        check(amc::lua_entry_name("counter", "other") == "other", "lua entry keeps unprefixed name");

        const std::string conformance = amc::generate_lua_conformance(m, error);
        check(error.empty(), "lua conformance generation succeeds");
        check(contains(conformance, "assert(type(counter[\"add\"])") || contains(conformance, "\"add\""),
            "lua conformance lists the entry");
        check(contains(conformance, "counter._abi_hash"), "lua conformance checks the abi hash");
        check(contains(conformance, "\"counter\""), "lua conformance checks the module name");
    }

    // --- ABI adapter generation -------------------------------------------
    {
        amc::AbiModule v1, v2, report;
        v1.package_name = "v1";
        v2.package_name = "v2";
        const amc::Hash128 int_id = amc::hash_text("int", 0x54595045);
        const amc::Hash128 long_id = amc::hash_text("long", 0x54595045);
        const auto primitive = [](const char *name, amc::Hash128 id, uint32_t size) {
            amc::Type type;
            type.name = name;
            type.kind = amc::TypeKind::primitive;
            type.id = id;
            type.size = size;
            type.align = size;
            type.layout_hash = amc::hash_text(std::string(name) + "-layout", 9);
            return type;
        };

        v1.types.push_back(make_type("MapRecord", 8, 4));
        v1.types[0].layout_hash = amc::hash_text("v1-layout", 1);
        v1.types.push_back(primitive("int", int_id, 4));
        amc::Field value;
        value.owner_type = v1.types[0].id;
        value.name = "value";
        value.type_id = int_id;
        value.offset = 0;
        amc::Field count = value;
        count.name = "count";
        count.offset = 4;
        v1.fields = {value, count};
        v1.types[0].field_begin = 0;
        v1.types[0].field_count = 2;

        v2.types.push_back(make_type("MapRecord", 16, 8));
        v2.types[0].layout_hash = amc::hash_text("v2-layout", 2);
        v2.types.push_back(primitive("int", int_id, 4));
        v2.types.push_back(primitive("long", long_id, 8));
        amc::Field copied = value;
        copied.owner_type = v2.types[0].id;
        amc::Field added = copied;
        added.name = "added";
        added.offset = 4;
        amc::Field widened = copied;
        widened.name = "count";
        widened.type_id = long_id;
        widened.offset = 8;
        v2.fields = {copied, added, widened};
        v2.types[0].field_begin = 0;
        v2.types[0].field_count = 3;

        std::string error;
        check(amc::adapter_symbol("a::B") == "a_B", "adapter symbol scope");
        check(amc::adapter_symbol("weird-name") == "weird_name", "adapter symbol chars");

        check(amc::build_compatibility(v1, v2, report, error), "compatibility for adapter");
        check(!report.maps.empty(), "compatibility produced a mapping");
        const std::string adapter = amc::generate_adapter(v1, v2, report, error);
        check(error.empty(), "adapter generation succeeds");
        check(contains(adapter, "namespace abix_adapter"), "adapter namespace");
        check(contains(adapter, "copy_field"), "adapter copy operation");
        check(contains(adapter, "add_default"), "adapter default operation");
        check(contains(adapter, "convert_int"), "adapter integer conversion");

        amc::AdapterOptions typed_options;
        typed_options.typed = true;
        const std::string typed = amc::generate_adapter(v1, v2, report, typed_options, error);
        check(contains(typed, "::skl::abix::bridge::Adapter<"), "typed adapter specialization");
        check(contains(typed, "amc_generated::MapRecord_ABIX"), "typed adapter projection type");
        check(contains(typed, "ABIX_ADAPTER_TYPED"), "typed adapter is guarded");

        amc::AdapterOptions shim_options;
        shim_options.shim = true;
        const std::string shim = amc::generate_adapter(v1, v2, report, shim_options, error);
        check(contains(shim, "extern \"C\""), "shim exports a C ABI");
        check(contains(shim, "abix_adapter_apply"), "shim exposes abix_adapter_apply");
        check(!contains(shim, "#pragma once"), "shim is emitted as a translation unit");

        amc::AbiModule empty;
        std::string missing;
        const std::string none = amc::generate_adapter(empty, empty, empty, missing);
        check(none.empty() && !missing.empty(), "adapter without a mapping reports an error");
    }

    // --- region -> runtime projection ------------------------------------
    {
        amc::AbiModule m;
        m.package_name = "proj";
        m.package_version = "1";
        m.types.push_back(make_type("Foo", 16, 8));
        m.types[0].layout_hash = amc::hash_text("Foo", 0x4c41594f);
        amc::Field field;
        field.owner_type = m.types[0].id;
        field.name = "x";
        field.type_id = m.types[0].id;
        field.offset = 8;
        m.fields.push_back(field);
        m.types[0].field_begin = 0;
        m.types[0].field_count = 1;

        amc::MetadataOptions options;
        std::vector<uint8_t> region;
        std::string error;
        check(amc::build_metadata_region(m, options, region, error), "region for projection");

        amc::MaterializedModule materialized;
        check(materialized.build_from_region(region, error), "materialize region");
        check(materialized.descriptor.type_count == 1, "materialized type count");
        const skl::abix::Hash128 runtime_id{m.types[0].id.lo, m.types[0].id.hi};
        check(materialized.descriptor.types[0].type_id == runtime_id, "materialized type id");

        skl::abix::runtime::Registry<8> registry;
        check(registry.register_module(materialized.descriptor) == skl::abix::runtime::RegisterStatus::ok,
            "register materialized module");
        const auto *entry = registry.find_by_id(runtime_id);
        check(entry != nullptr, "materialized type is registered");
        check(entry != nullptr && entry->descriptor->field_count == 1, "materialized field count");
        check(entry != nullptr && entry->descriptor->fields[0].offset == 8, "materialized field offset");
    }

    // --- source origin ----------------------------------------------------
    {
        amc::AbiModule m;
        m.package_name = "src";
        m.types.push_back(make_type("Foo", 8, 8));
        m.types[0].layout_hash = amc::hash_text("Foo", 0x4c41594f);
        m.sources.push_back({m.types[0].id, "/tmp/foo.hpp", 12, 3});

        std::string error;
        check(amc::write_abix(m, "amc_source_test.abix", error), "write with source origin");
        amc::AbiModule loaded;
        check(amc::read_abix("amc_source_test.abix", loaded, error), "read with source origin");
        check(loaded.sources.size() == 1, "source origin round-trips");
        const amc::SourceOrigin *origin = amc::find_source_origin(loaded, m.types[0].id);
        check(origin != nullptr && origin->file == "/tmp/foo.hpp" && origin->line == 12 && origin->column == 3,
            "source origin fields");
        check(amc::abi_hash(loaded) == amc::abi_hash(m), "source origin excluded from abi hash");
        std::remove("amc_source_test.abix");
    }

    {
        // Region extension directory: discovery, boundary rules and the
        // treatment of unknown versions/kinds.
        amc::AbiModule m;
        m.package_name = "ext";
        m.package_version = "1";
        amc::Type t;
        t.name = "Foo";
        t.kind = amc::TypeKind::record;
        t.id = amc::hash_text("Foo", 0x54595045);
        t.size = 4;
        t.align = 4;
        m.types.push_back(t);
        amc::Function f;
        f.name = "f";
        f.return_type = t.id;
        m.functions.push_back(f);
        amc::OwnershipContract contract;
        contract.target = amc::OwnershipTarget::return_value;
        contract.target_index = 0;
        contract.mode = amc::OwnershipMode::borrowed;
        m.contracts.push_back(contract);

        amc::MetadataOptions options;
        std::vector<uint8_t> region;
        std::string error;
        check(amc::build_metadata_region(m, options, region, error), "build region with a contract");
        std::vector<amc::MetadataOwnershipContract> owned;
        check(amc::read_ownership_contracts(region, owned, error)
                  && owned.size() == 1
                  && owned[0].mode == amc::OwnershipMode::borrowed,
            "ownership contract discovered through the directory");

        amc::MetadataHeader header;
        check(amc::read_metadata_header(region, header, error), "read region header");
        const size_t known = static_cast<size_t>(header.type_count) * 64
                           + static_cast<size_t>(header.field_count) * 48
                           + static_cast<size_t>(header.function_count) * 72
                           + static_cast<size_t>(header.parameter_count) * 32
                           + static_cast<size_t>(header.symbol_count) * 16;
        check(header.desc_size > known, "region carries an extension directory");
        const size_t directory = static_cast<size_t>(header.desc_offset) + known;

        std::vector<amc::MetadataOwnershipContract> ignored;
        std::vector<uint8_t> bad_version = region;
        bad_version[directory] = 99;
        check(!amc::read_ownership_contracts(bad_version, ignored, error), "unknown directory version rejected");

        std::vector<uint8_t> bad_bounds = region;
        bad_bounds[directory + 16 + 12] = 0xff;   // first entry byte_offset low byte -> out of range
        bad_bounds[directory + 16 + 13] = 0xff;
        check(!amc::read_ownership_contracts(bad_bounds, ignored, error), "out-of-bounds extension entry rejected");

        std::vector<uint8_t> unknown_kind = region;
        unknown_kind[directory + 16] = 0x7f;   // first entry kind -> unknown
        check(amc::read_ownership_contracts(unknown_kind, ignored, error) && ignored.empty(),
            "unknown extension kind is skipped");
    }

    {
        // Target ABI verification: compatible / incompatible / unverifiable.
        amc::MetadataOptions options;
        std::string error;
        amc::Type t;
        t.name = "Foo";
        t.kind = amc::TypeKind::record;
        t.id = amc::hash_text("Foo", 0x54595045);
        t.size = 4;
        t.align = 4;

        amc::AbiModule m;
        m.package_name = "target";
        m.package_version = "1";
        m.arch = 1;
        m.os = 1;
        m.target_abi = 1;
        m.pointer_bits = 64;
        m.endianness = 1;
        m.types.push_back(t);
        std::vector<uint8_t> region;
        check(amc::build_metadata_region(m, options, region, error), "build region with target");
        amc::TargetDescription target;
        check(amc::read_target_description(region, target, error) && target.present && target.pointer_bits == 64,
            "read target description");

        amc::TargetExpectation expected;
        expected.arch = 1;
        expected.os = 1;
        expected.target_abi = 1;
        expected.pointer_bits = 64;
        expected.endianness = 1;
        amc::TargetVerification verdict;
        check(amc::verify_target_abi(region, expected, verdict, error)
                  && verdict.status == amc::TargetVerificationStatus::compatible,
            "target compatible");
        amc::TargetExpectation wrong = expected;
        wrong.arch = 2;
        check(amc::verify_target_abi(region, wrong, verdict, error)
                  && verdict.status == amc::TargetVerificationStatus::incompatible,
            "target incompatible");

        // No target extension -> unverifiable, never compatible.
        amc::AbiModule bare;
        bare.package_name = "bare";
        bare.package_version = "1";
        bare.types.push_back(t);
        std::vector<uint8_t> bare_region;
        check(amc::build_metadata_region(bare, options, bare_region, error), "build region without target");
        amc::TargetExpectation any;
        any.arch = 1;
        check(amc::verify_target_abi(bare_region, any, verdict, error)
                  && verdict.status == amc::TargetVerificationStatus::unverifiable,
            "missing target is unverifiable");

        // Target present but a checked fact is unknown -> unverifiable.
        amc::AbiModule partial;
        partial.package_name = "partial";
        partial.package_version = "1";
        partial.arch = 1;
        partial.types.push_back(t);
        std::vector<uint8_t> partial_region;
        check(amc::build_metadata_region(partial, options, partial_region, error), "build partial target region");
        check(amc::verify_target_abi(partial_region, expected, verdict, error)
                  && verdict.status == amc::TargetVerificationStatus::unverifiable,
            "unknown target field is unverifiable");
    }

    {
        // ABIX-STD sequence-view lowering: shape alone is not compatibility.
        amc::AbiModule m;
        m.pointer_bits = 64;
        const auto add_pointer = [&](const char *name) {
            amc::Type t;
            t.name = name;
            t.kind = amc::TypeKind::pointer;
            t.id = amc::hash_text(name, 0x54595045);
            t.size = 8;
            t.align = 8;
            m.types.push_back(t);
            return t.id;
        };
        const auto add_primitive = [&](const char *name, uint32_t width, amc::PrimitiveAbiKind kind) {
            amc::Type t;
            t.name = name;
            t.kind = amc::TypeKind::primitive;
            t.id = amc::hash_text(name, 0x54595045);
            t.size = width;
            t.align = width;
            t.primitive_abi = amc::primitive_abi(width, kind);
            m.types.push_back(t);
            return t.id;
        };
        const auto add_record = [&](const char *name,
                                    const std::vector<std::tuple<std::string, amc::Hash128, uint32_t>> &fields) {
            amc::Type t;
            t.name = name;
            t.kind = amc::TypeKind::record;
            t.id = amc::hash_text(name, 0x54595045);
            t.align = 8;
            t.field_begin = static_cast<uint32_t>(m.fields.size());
            t.field_count = static_cast<uint32_t>(fields.size());
            for (const auto &field : fields) {
                m.fields.push_back({t.id, std::get<0>(field), std::get<1>(field), std::get<2>(field), 0});
                t.size = std::max(t.size, std::get<2>(field) + 8);
            }
            m.types.push_back(t);
            return t.id;
        };
        const auto int_id = add_primitive("int", 4, amc::PrimitiveAbiKind::sint);
        const auto float_id = add_primitive("float", 4, amc::PrimitiveAbiKind::floating);
        const auto u64 = add_primitive("unsigned long", 8, amc::PrimitiveAbiKind::uint);
        const auto i64 = add_primitive("long", 8, amc::PrimitiveAbiKind::sint);
        const auto int_ptr = add_pointer("const int*");
        const auto float_ptr = add_pointer("const float*");
        // Record the pointee so element matching is by TypeID, not spelling.
        m.pointees.push_back({int_ptr, int_id, amc::pointee_const});
        m.pointees.push_back({float_ptr, float_id, amc::pointee_const});
        const auto span = add_record("span", {
                                                 {"data", int_ptr, 0},
                                                 { "len",     u64, 8}
        });
        const auto reversed = add_record("reversed", {
                                                         { "len",     u64, 0},
                                                         {"data", int_ptr, 8}
        });
        const auto wrong_element = add_record("wrong_element", {
                                                                   {"data", float_ptr, 0},
                                                                   { "len",       u64, 8}
        });
        const auto bad_length = add_record("bad_length", {
                                                             {"data", int_ptr, 0},
                                                             { "len",     i64, 8}
        });
        const auto extra_field =
            add_record("extra_field", {
                                          {"data", int_ptr,  0},
                                          { "len",     u64,  8},
                                          { "cap",     u64, 16}
        });

        const auto direct = amc::evaluate_sequence_view(m, span, int_id, "int", false);
        check(direct.status == static_cast<uint32_t>(amc::LoweringStatus::direct), "span is a direct sequence view");

        const auto element = amc::evaluate_sequence_view(m, wrong_element, int_id, "int", false);
        check(element.status == static_cast<uint32_t>(amc::LoweringStatus::unsupported)
                  && element.reason == static_cast<uint32_t>(amc::LoweringReason::element_mismatch),
            "element mismatch is rejected (by pointee TypeID)");

        const auto adapter = amc::evaluate_sequence_view(m, reversed, int_id, "int", false);
        check(adapter.status == static_cast<uint32_t>(amc::LoweringStatus::needs_adapter),
            "reversed layout needs an adapter");

        const auto length = amc::evaluate_sequence_view(m, bad_length, int_id, "int", false);
        check(length.status == static_cast<uint32_t>(amc::LoweringStatus::unsupported)
                  && length.reason == static_cast<uint32_t>(amc::LoweringReason::length_type),
            "non-unsigned length is rejected");

        const auto extra = amc::evaluate_sequence_view(m, extra_field, int_id, "int", false);
        check(extra.status == static_cast<uint32_t>(amc::LoweringStatus::unsupported)
                  && extra.reason == static_cast<uint32_t>(amc::LoweringReason::field_count),
            "an extra field is rejected");

        const auto mutable_view = amc::evaluate_sequence_view(m, span, int_id, "int", true);
        check(mutable_view.status == static_cast<uint32_t>(amc::LoweringStatus::unsupported)
                  && mutable_view.reason == static_cast<uint32_t>(amc::LoweringReason::mutability_mismatch),
            "a const pointer cannot satisfy a mutable contract");
    }

    {
        // ABIX-STD string-view: { data, length, encoding }, code unit by TypeID.
        amc::AbiModule m;
        m.pointer_bits = 64;
        const auto add_pointer = [&](const char *name) {
            amc::Type t;
            t.name = name;
            t.kind = amc::TypeKind::pointer;
            t.id = amc::hash_text(name, 0x54595045);
            t.size = 8;
            t.align = 8;
            m.types.push_back(t);
            return t.id;
        };
        const auto add_primitive = [&](const char *name, uint32_t width, amc::PrimitiveAbiKind kind) {
            amc::Type t;
            t.name = name;
            t.kind = amc::TypeKind::primitive;
            t.id = amc::hash_text(name, 0x54595045);
            t.size = width;
            t.align = width;
            t.primitive_abi = amc::primitive_abi(width, kind);
            m.types.push_back(t);
            return t.id;
        };
        const auto add_record = [&](const char *name,
                                    const std::vector<std::tuple<std::string, amc::Hash128, uint32_t>> &fields) {
            amc::Type t;
            t.name = name;
            t.kind = amc::TypeKind::record;
            t.id = amc::hash_text(name, 0x54595045);
            t.align = 8;
            t.field_begin = static_cast<uint32_t>(m.fields.size());
            t.field_count = static_cast<uint32_t>(fields.size());
            for (const auto &field : fields) {
                m.fields.push_back({t.id, std::get<0>(field), std::get<1>(field), std::get<2>(field), 0});
                t.size = std::max(t.size, std::get<2>(field) + 8);
            }
            m.types.push_back(t);
            return t.id;
        };
        const auto ch = add_primitive("char", 1, amc::PrimitiveAbiKind::char_signed);
        const auto ch_ptr = add_pointer("const char*");
        m.pointees.push_back({ch_ptr, ch, amc::pointee_const});
        const auto u64 = add_primitive("unsigned long", 8, amc::PrimitiveAbiKind::uint);
        const auto u32 = add_primitive("unsigned int", 4, amc::PrimitiveAbiKind::uint);
        const auto text = add_record("text", {
                                                 {    "data", ch_ptr,  0},
                                                 {     "len",    u64,  8},
                                                 {"encoding",    u32, 16}
        });
        const auto no_encoding = add_record("no_encoding", {
                                                               {"data", ch_ptr, 0},
                                                               { "len",    u64, 8}
        });

        const auto direct = amc::evaluate_string_view(m, text, ch, "char", false);
        check(direct.status == static_cast<uint32_t>(amc::LoweringStatus::direct), "string view is direct");

        const auto missing = amc::evaluate_string_view(m, no_encoding, ch, "char", false);
        check(missing.status == static_cast<uint32_t>(amc::LoweringStatus::unsupported)
                  && missing.reason == static_cast<uint32_t>(amc::LoweringReason::field_count),
            "string view without an encoding is rejected");

        const auto wrong = amc::evaluate_string_view(m, text, u64, "unsigned long", false);
        check(wrong.status == static_cast<uint32_t>(amc::LoweringStatus::unsupported)
                  && wrong.reason == static_cast<uint32_t>(amc::LoweringReason::element_mismatch),
            "string view code unit mismatch is rejected");
    }

    {
        // The lowering result round-trips through the metadata region extension.
        amc::AbiModule m;
        m.package_name = "lower";
        m.package_version = "1";
        m.pointer_bits = 64;
        amc::Type span;
        span.name = "span";
        span.kind = amc::TypeKind::record;
        span.id = amc::hash_text("span", 0x54595045);
        span.size = 16;
        span.align = 8;
        amc::Type rev;
        rev.name = "rev";
        rev.kind = amc::TypeKind::record;
        rev.id = amc::hash_text("rev", 0x54595045);
        rev.size = 16;
        rev.align = 8;
        m.types.push_back(span);
        m.types.push_back(rev);
        amc::LoweringResult lowering;
        lowering.type_id = rev.id;
        lowering.contract_type = span.id;
        lowering.kind = static_cast<uint32_t>(amc::LoweringKind::sequence_view);
        lowering.status = static_cast<uint32_t>(amc::LoweringStatus::needs_adapter);
        lowering.reason = static_cast<uint32_t>(amc::LoweringReason::layout);
        lowering.pointer_field = 1;
        lowering.length_field = 0;
        m.lowerings.push_back(lowering);

        amc::MetadataOptions options;
        std::vector<uint8_t> region;
        std::string error;
        check(amc::build_metadata_region(m, options, region, error), "build region with a lowering");
        std::vector<amc::MetadataLoweringContract> lowerings;
        check(amc::read_lowering_contracts(region, lowerings, error)
                  && lowerings.size() == 1
                  && lowerings[0].status == amc::LoweringStatus::needs_adapter
                  && lowerings[0].reason == amc::LoweringReason::layout
                  && lowerings[0].type_index == 1
                  && lowerings[0].contract_index == 0,
            "lowering round-trips through the region");

        // A region with no lowering keeps the same bytes as before the extension.
        amc::AbiModule bare;
        bare.package_name = "bare";
        bare.package_version = "1";
        bare.types.push_back(span);
        std::vector<uint8_t> bare_region;
        check(amc::build_metadata_region(bare, options, bare_region, error), "build region without a lowering");
        std::vector<amc::MetadataLoweringContract> none;
        check(
            amc::read_lowering_contracts(bare_region, none, error) && none.empty(), "no lowering means an empty read");
    }

    {
        // Structured ABI verification: platform / layout / calling convention.
        amc::AbiModule m;
        m.package_name = "abi";
        m.package_version = "1";
        m.arch = 1;
        m.os = 1;
        m.target_abi = 1;
        m.pointer_bits = 64;
        m.endianness = 1;
        amc::Type record;
        record.name = "Foo";
        record.kind = amc::TypeKind::record;
        record.id = amc::hash_text("Foo", 0x54595045);
        record.size = 16;
        record.align = 8;
        record.field_begin = 0;
        record.field_count = 1;
        amc::Type length;
        length.name = "unsigned long";
        length.kind = amc::TypeKind::primitive;
        length.id = amc::hash_text("unsigned long", 0x54595045);
        length.size = 8;
        length.align = 8;
        length.primitive_abi = amc::primitive_abi(8, amc::PrimitiveAbiKind::uint);
        m.types.push_back(record);
        m.types.push_back(length);
        m.fields.push_back({record.id, "len", length.id, 0, 0});
        amc::Function function;
        function.name = "f";
        function.return_type = length.id;
        function.calling_convention = 1;
        function.signature = amc::signature_hash(function);
        m.functions.push_back(function);

        amc::MetadataOptions options;
        std::vector<uint8_t> region;
        std::string error;
        check(amc::build_metadata_region(m, options, region, error), "build region for ABI verification");

        amc::TargetExpectation expected;
        expected.arch = 1;
        expected.os = 1;
        expected.target_abi = 1;
        expected.pointer_bits = 64;
        expected.endianness = 1;
        expected.calling_convention = 1;
        amc::AbiVerificationReport report;
        check(amc::verify_abi(region, expected, report, error)
                  && report.platform == amc::TargetVerificationStatus::compatible
                  && report.layout == amc::TargetVerificationStatus::compatible
                  && report.calling_convention == amc::TargetVerificationStatus::compatible,
            "ABI verification: all dimensions compatible");

        amc::TargetExpectation wrong_arch = expected;
        wrong_arch.arch = 2;
        check(amc::verify_abi(region, wrong_arch, report, error)
                  && report.platform == amc::TargetVerificationStatus::incompatible,
            "ABI verification: platform incompatible");

        amc::TargetExpectation wrong_cc = expected;
        wrong_cc.calling_convention = 2;
        check(amc::verify_abi(region, wrong_cc, report, error)
                  && report.calling_convention == amc::TargetVerificationStatus::incompatible,
            "ABI verification: calling convention incompatible");

        // A region without a target description: platform is unverifiable.
        amc::AbiModule bare;
        bare.package_name = "bare";
        bare.package_version = "1";
        bare.types.push_back(record);
        bare.types.push_back(length);
        bare.fields.push_back({record.id, "len", length.id, 0, 0});
        bare.functions.push_back(function);
        std::vector<uint8_t> bare_region;
        check(amc::build_metadata_region(bare, options, bare_region, error), "build bare region");
        check(amc::verify_abi(bare_region, expected, report, error)
                  && report.platform == amc::TargetVerificationStatus::unverifiable,
            "ABI verification: missing target is unverifiable");

        // A function without a calling convention: unverifiable, not compatible.
        amc::AbiModule no_cc = m;
        no_cc.functions[0].calling_convention = 0;
        std::vector<uint8_t> no_cc_region;
        check(amc::build_metadata_region(no_cc, options, no_cc_region, error), "build no-cc region");
        check(amc::verify_abi(no_cc_region, expected, report, error)
                  && report.calling_convention == amc::TargetVerificationStatus::unverifiable,
            "ABI verification: missing calling convention is unverifiable");
    }

    if (failures != 0) {
        std::fprintf(stderr, "amc-core-test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("amc-core-test: all checks passed\n");
    return 0;
}
