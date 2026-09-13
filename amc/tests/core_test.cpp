#include "../core/amc_context.h"
#include "../core/amc_core.h"
#include "../core/amc_elf.h"
#include "../core/amc_error.h"
#include "../core/amc_json.h"
#include "../core/amc_lua.h"
#include "../core/amc_materialize.h"
#include "../core/amc_metadata.h"
#include "../core/amc_query.h"
#include "../core/amc_symbol_store.h"
#include "../core/amc_verify.h"

#include "abix/runtime_registry.h"

#include <cstdio>
#include <filesystem>
#include <string>
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
        check(contains(contract, "#include \"aue/aue.h\""), "lua contract includes aue");
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
        const skl::abix::model::Hash128 runtime_id{m.types[0].id.lo, m.types[0].id.hi};
        check(materialized.descriptor.types[0].type_id == runtime_id, "materialized type id");

        skl::abix::runtime::RuntimeRegistry<8> registry;
        check(registry.register_module(materialized.descriptor) == skl::abix::runtime::RuntimeRegisterStatus::ok,
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

    if (failures != 0) {
        std::fprintf(stderr, "amc-core-test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("amc-core-test: all checks passed\n");
    return 0;
}
