#include "AMC/Core/Adapter.h"
#include "AMC/Core/Cache.h"
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
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#if !defined(_WIN32)
#  include <sys/wait.h>
#  include <unistd.h>
#endif

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

// When KEEP_ARTIFACTS=1 is set (the CMake option SKL_ABIX_KEEP_TEST_ARTIFACTS),
// the temporary cache/scratch directories are retained for inspection.
bool keep_artifacts() {
    const char *value = std::getenv("KEEP_ARTIFACTS");
    return value != nullptr && std::string(value) == "1";
}

void cleanup_dir(const std::filesystem::path &root) {
    if (keep_artifacts()) {
        std::printf("  [KEEP] retained artifacts: %s\n", root.string().c_str());
        return;
    }
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
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

    // --- PE reader --------------------------------------------------------
    {
        // Minimal PE32+ with one `.rdata` section and a CodeView (RSDS) debug
        // directory, exercising is_pe / read_pe_sections / read_pe_debug_id.
        std::vector<uint8_t> pe(0x300, 0);
        const auto put16 = [&](size_t o, uint16_t v) {
            pe[o] = static_cast<uint8_t>(v);
            pe[o + 1] = static_cast<uint8_t>(v >> 8);
        };
        const auto put32 = [&](size_t o, uint32_t v) {
            for (int i = 0; i < 4; ++i)
                pe[o + i] = static_cast<uint8_t>(v >> (8 * i));
        };
        const auto put64 = [&](size_t o, uint64_t v) {
            for (int i = 0; i < 8; ++i)
                pe[o + i] = static_cast<uint8_t>(v >> (8 * i));
        };
        pe[0] = 'M';
        pe[1] = 'Z';
        put32(0x3C, 0x40);
        pe[0x40] = 'P';
        pe[0x41] = 'E';
        const size_t pe_coff = 0x44;
        put16(pe_coff + 0, 0x8664);   // amd64
        put16(pe_coff + 2, 1);        // one section
        put16(pe_coff + 16, 240);     // PE32+ optional header
        const size_t pe_optional = pe_coff + 20;
        put16(pe_optional + 0, 0x20B);
        put64(pe_optional + 24, 0x140000000ull);
        const size_t pe_ddir = pe_optional + 112;
        put32(pe_ddir + 6 * 8, 0x1000);       // debug directory RVA
        put32(pe_ddir + 6 * 8 + 4, 28);       // debug directory size
        const size_t pe_sect = pe_optional + 240;
        const char pe_name[] = ".rdata";
        for (int i = 0; i < 6; ++i)
            pe[pe_sect + i] = static_cast<uint8_t>(pe_name[i]);
        put32(pe_sect + 8, 0x100);
        put32(pe_sect + 12, 0x1000);
        put32(pe_sect + 16, 0x200);
        put32(pe_sect + 20, 0x200);
        put32(0x200 + 12, 2);      // IMAGE_DEBUG_TYPE_CODEVIEW
        put32(0x200 + 24, 0x220);  // PointerToRawData -> RSDS record
        const char rsds[] = "RSDS";
        for (int i = 0; i < 4; ++i)
            pe[0x220 + i] = static_cast<uint8_t>(rsds[i]);
        for (int i = 0; i < 16; ++i)
            pe[0x224 + i] = static_cast<uint8_t>(0x10 + i);   // GUID
        put32(0x234, 7);   // Age

        std::string pe_error;
        check(amc::is_pe(pe.data(), pe.size()), "PE image detected");
        check(amc::detect_binary_format(pe.data(), pe.size()) == amc::BinaryFormat::pe, "PE format detected");
        std::vector<amc::BinarySection> pe_sections;
        check(amc::read_pe_sections(pe.data(), pe.size(), pe_sections, pe_error), "PE sections read");
        check(amc::native_section_name(amc::BinaryFormat::pe, ".abix.metadata") == ".abix",
            "PE metadata section maps to .abix");
        std::vector<uint8_t> pe_build_id;
        check(amc::read_pe_debug_id(pe.data(), pe.size(), pe_build_id, pe_error), "PE CodeView debug id read");
        check(pe_build_id.size() == 20 && pe_build_id[0] == 0x10 && pe_build_id[16] == 7,
            "PE debug id is the RSDS GUID + Age");
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
        const std::string expected_tail = (std::filesystem::path("de") / "adbeef.abix").string();
        check(contains(amc::symbol_store_path(root, key, ".abix"), expected_tail), "symbol store path layout");

        const std::vector<uint8_t> artifact = {1, 2, 3, 4};
        std::string stored;
        check(amc::publish_symbol(root, key, artifact, "meta", ".abix", stored, error), "publish symbol");
        std::vector<uint8_t> fetched;
        std::string fetched_path;
        check(amc::fetch_symbol(root, key, fetched, fetched_path, error), "fetch symbol");
        check(fetched == artifact, "fetched artifact matches");
        cleanup_dir(root);
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

        // Regression: Json(unsigned) must serialize as decimal. The library's
        // JSON_LHEX node printed `0x..`, which its own parser (hex disabled)
        // could not read back.
        amc::Json unsigned_value = amc::Json::object();
        unsigned_value.set("count", static_cast<unsigned>(64));
        const std::string unsigned_dump = unsigned_value.dump();
        check(unsigned_dump == "{\"count\":64}", "json unsigned serializes as decimal");
        amc::Json unsigned_round;
        check(amc::parse_json(unsigned_dump, unsigned_round, error)
                  && unsigned_round.find("count") != nullptr
                  && unsigned_round.find("count")->as_number() == 64,
            "json unsigned round-trips");

        // Regression: a find() result must stay valid across later find() calls
        // on the same object, including keys that are prefixes of one another.
        amc::Json many = amc::Json::object();
        many.set("schema", std::string("S"));
        many.set("schema_version", std::string("SV"));
        many.set("request", std::string("R"));
        many.set("request_id", std::string("RI"));
        const amc::Json *schema = many.find("schema");
        const amc::Json *schema_version = many.find("schema_version");
        const amc::Json *request = many.find("request");
        const amc::Json *request_id = many.find("request_id");
        check(schema != nullptr && schema_version != nullptr && request != nullptr && request_id != nullptr,
            "json find finds every key");
        check(schema->as_string() == "S"
                  && schema_version->as_string() == "SV"
                  && request->as_string() == "R"
                  && request_id->as_string() == "RI",
            "json find results are stable across later finds");
        check(many.find("missing") == nullptr, "json find returns null for an absent key");
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

    {
        // Non-intrusive interface contract: interface identity, method identity
        // and implementation binding round-trip through .abix and the metadata
        // region without exposing any language's private vtable.
        auto make_primitive = [](const std::string &name, uint32_t size) {
            amc::Type type;
            type.name = name;
            type.kind = amc::TypeKind::primitive;
            type.id = amc::hash_text(name, 0x54595045);
            type.size = size;
            type.align = size;
            type.primitive_abi =
                amc::primitive_abi(size * 8, amc::PrimitiveAbiKind::sint, amc::FloatFormat::none, true);
            return type;
        };
        amc::AbiModule m;
        m.package_name = "iface";
        m.package_version = "1";
        m.pointer_bits = 64;
        m.types.push_back(make_primitive("int32_t", 4));
        amc::Type renderer = make_type("Renderer", 8, 8);
        m.types.push_back(renderer);
        const amc::Hash128 i32 = m.types[0].id;
        auto make_function = [&](const std::string &name, amc::Hash128 owner) {
            amc::Function function;
            function.name = name;
            function.owner_type = owner;
            function.return_type = i32;
            function.parameters = {
                { "width", i32, 0},
                {"height", i32, 0}
            };
            function.signature = amc::signature_hash(function);
            return function;
        };
        m.functions.push_back(make_function("draw", amc::Hash128{}));
        m.functions.push_back(make_function("resize", amc::Hash128{}));
        m.functions.push_back(make_function("Renderer_draw", renderer.id));
        m.functions.push_back(make_function("Renderer_resize", renderer.id));

        amc::InterfaceRecord iface;
        iface.name = "IRenderer";
        iface.version = 1;
        iface.method_begin = 0;
        iface.method_count = 2;
        for (uint32_t i = 0; i < 2; ++i) {
            amc::InterfaceMethod method;
            method.name = m.functions[i].name;
            method.signature = m.functions[i].signature;
            method.return_type = m.functions[i].return_type;
            method.parameters = m.functions[i].parameters;
            method.slot = i;
            m.interface_methods.push_back(method);
        }
        iface.id = amc::interface_hash(iface, m.interface_methods);
        for (auto &method : m.interface_methods)
            method.interface_id = iface.id;
        m.interfaces.push_back(iface);

        amc::InterfaceImplementation impl;
        impl.interface_id = iface.id;
        impl.concrete_type = renderer.id;
        impl.binding_begin = 0;
        impl.binding_count = 2;
        for (uint32_t i = 0; i < 2; ++i) {
            amc::MethodBinding binding;
            binding.interface_id = iface.id;
            binding.slot = m.interface_methods[i].slot;
            binding.method_signature = m.interface_methods[i].signature;
            binding.native_function = m.functions[2 + i].signature;
            m.method_bindings.push_back(binding);
        }
        m.implementations.push_back(impl);

        std::string error;
        check(amc::validate(m, error), "interface module validates");
        // Identity depends on name+version+signatures, so a rename is a new
        // interface even though the shape is identical.
        amc::InterfaceRecord renamed = iface;
        renamed.name = "IOther";
        check(!(amc::interface_hash(renamed, m.interface_methods) == iface.id), "interface identity includes the name");
        amc::InterfaceRecord bumped = iface;
        bumped.version = 2;
        check(
            !(amc::interface_hash(bumped, m.interface_methods) == iface.id), "interface identity includes the version");

        check(amc::write_abix(m, "amc_iface_test.abix", error), "write_abix interface");
        amc::AbiModule loaded;
        check(amc::read_abix("amc_iface_test.abix", loaded, error), "read_abix interface");
        check(loaded.interfaces.size() == 1
                  && loaded.interfaces[0].id == iface.id
                  && loaded.interface_methods.size() == 2
                  && loaded.implementations.size() == 1
                  && loaded.method_bindings.size() == 2
                  && loaded.method_bindings[0].native_function == m.functions[2].signature,
            "interface round-trips through .abix");

        amc::MetadataOptions options;
        std::vector<uint8_t> region;
        check(amc::build_metadata_region(m, options, region, error), "build region with an interface");
        std::vector<amc::MetadataInterfaceContract> contracts;
        std::vector<amc::MetadataInterfaceMethod> methods;
        std::vector<amc::MetadataInterfaceImplementation> impls;
        std::vector<amc::MetadataMethodBinding> bindings;
        check(amc::read_interface_contracts(region, contracts, error)
                  && contracts.size() == 1
                  && contracts[0].id == iface.id
                  && contracts[0].method_count == 2,
            "interface identity round-trips through the region");
        check(amc::read_interface_methods(region, methods, error)
                  && methods.size() == 2
                  && methods[0].parameter_types.size() == 2
                  && methods[0].return_type == i32,
            "interface methods round-trip through the region");
        check(amc::read_interface_implementations(region, impls, error)
                  && impls.size() == 1
                  && impls[0].concrete_type == renderer.id,
            "interface implementation round-trips through the region");
        check(amc::read_method_bindings(region, bindings, error)
                  && bindings.size() == 2
                  && bindings[0].native_function == m.functions[2].signature,
            "method bindings round-trip through the region");

        // A region with no interface reads empty.
        amc::AbiModule bare;
        bare.package_name = "bare";
        bare.package_version = "1";
        bare.types.push_back(m.types[0]);
        std::vector<uint8_t> bare_region;
        check(amc::build_metadata_region(bare, options, bare_region, error), "build bare interface region");
        std::vector<amc::MetadataInterfaceContract> none;
        check(amc::read_interface_contracts(bare_region, none, error) && none.empty(),
            "no interface means an empty region read");

        // A signature mismatch between a binding and its interface method is
        // rejected rather than tolerated.
        amc::AbiModule bad = m;
        bad.method_bindings[1].method_signature = m.functions[2].signature;
        check(!amc::validate(bad, error), "binding signature mismatch is rejected");

        // An implementation that does not bind every method is rejected.
        amc::AbiModule partial = m;
        partial.implementations[0].binding_count = 1;
        check(!amc::validate(partial, error), "incomplete implementation is rejected");

        // Projection preserves the contract when its types survive.
        amc::AbiModule projected_all;
        check(amc::project_symbols(m, {}, projected_all, error)
                  && projected_all.interfaces.size() == 1
                  && projected_all.interface_methods.size() == 2
                  && projected_all.method_bindings.size() == 2,
            "empty projection keeps the interface contract");
        std::remove("amc_iface_test.abix");
    }

    {
        // `void` is a first-class primitive with no size; a function may return
        // it. The size/align requirement is skipped for it, and it survives both
        // serialization paths.
        amc::AbiModule m;
        m.package_name = "void_test";
        m.package_version = "1";
        amc::Type void_type;
        void_type.name = "void";
        void_type.kind = amc::TypeKind::primitive;
        void_type.id = amc::hash_text("void", 0x54595045);
        void_type.size = 0;
        void_type.align = 1;
        void_type.primitive_abi = amc::primitive_abi(0, amc::PrimitiveAbiKind::void_);
        m.types.push_back(void_type);
        amc::Function ping;
        ping.name = "ping";
        ping.return_type = void_type.id;
        ping.signature = amc::signature_hash(ping);
        m.functions.push_back(ping);

        std::string error;
        check(amc::validate(m, error), "a void return type validates");
        check(amc::write_abix(m, "amc_void_test.abix", error), "write a void module");
        amc::AbiModule loaded;
        check(amc::read_abix("amc_void_test.abix", loaded, error), "read a void module");
        check(loaded.types.size() == 1
                  && loaded.functions.size() == 1
                  && loaded.types[0].size == 0
                  && loaded.functions[0].return_type == void_type.id,
            "void round-trips through .abix");

        amc::MetadataOptions options;
        std::vector<uint8_t> region;
        check(amc::build_metadata_region(m, options, region, error), "build a region with void");
        amc::AbiModule from_region;
        check(amc::module_from_region(region, from_region, error)
                  && from_region.types.size() == 1
                  && from_region.types[0].size == 0,
            "void round-trips through the metadata region");
        std::remove("amc_void_test.abix");
    }

    {
        // M3.4: instantiation request cache. Identity is layered, and a build
        // key covers every input that can change the reusable product.
        auto make_request = [](const std::string &tmpl, std::vector<std::string> args) {
            amc::InstantiationRequest request;
            request.template_name = tmpl;
            request.arguments = std::move(args);
            request.arch = 1;
            request.os = 1;
            request.target_abi = 1;
            request.pointer_bits = 64;
            return request;
        };
        auto make_inputs = [&](const amc::InstantiationRequest &request) {
            amc::InstantiationBuildInputs inputs;
            inputs.request = request;
            inputs.source_digest = amc::digest_text("source-v1", amc::digest_domain_source);
            inputs.toolchain = amc::digest_text("clang-19", amc::digest_domain_toolchain);
            inputs.build_options = amc::digest_text("-O2", amc::digest_domain_options);
            return inputs;
        };

        const auto request = make_request("abix_span", {"int32_t"});
        const auto same_shape = make_request("abix_span", {"int32_t"});
        const auto reordered = make_request("abix_span", {"float", "int32_t"});
        const auto other_template = make_request("abix_array", {"int32_t"});
        auto retargeted = request;
        retargeted.pointer_bits = 32;
        auto qualified = make_request("abix_span", {"const int32_t *"});

        check(amc::instantiation_request_id(request) == amc::instantiation_request_id(same_shape),
            "same request has one identity");
        check(!(amc::instantiation_request_id(request) == amc::instantiation_request_id(other_template))
                  && !(amc::instantiation_request_id(request) == amc::instantiation_request_id(reordered)),
            "template or argument order changes the request identity");
        check(!(amc::instantiation_request_id(request) == amc::instantiation_request_id(retargeted)),
            "target changes the request identity");
        // A spelling that canonicalises to the same argument is one request.
        check(amc::instantiation_request_id(request) == amc::instantiation_request_id(qualified),
            "equivalent normalised arguments share the request identity");
        // Same template, different target: same template signature, distinct request.
        check(amc::instantiation_template_signature(request) == amc::instantiation_template_signature(retargeted)
                  && !(amc::instantiation_request_id(request) == amc::instantiation_request_id(retargeted)),
            "template signature is target-agnostic");

        // Build key covers every reuse-decision input.
        const auto base = make_inputs(request);
        check(amc::instantiation_build_key(base) == amc::instantiation_build_key(make_inputs(same_shape)),
            "identical build inputs share a build key");
        auto source_changed = base;
        source_changed.source_digest = amc::digest_text("source-v2", amc::digest_domain_source);
        auto toolchain_changed = base;
        toolchain_changed.toolchain = amc::digest_text("clang-20", amc::digest_domain_toolchain);
        auto options_changed = base;
        options_changed.build_options = amc::digest_text("-O3", amc::digest_domain_options);
        auto protocol_changed = base;
        protocol_changed.protocol_version = 2;
        auto schema_changed = base;
        schema_changed.schema_version = 2;
        check(!(amc::instantiation_build_key(base) == amc::instantiation_build_key(source_changed))
                  && !(amc::instantiation_build_key(base) == amc::instantiation_build_key(toolchain_changed))
                  && !(amc::instantiation_build_key(base) == amc::instantiation_build_key(options_changed))
                  && !(amc::instantiation_build_key(base) == amc::instantiation_build_key(protocol_changed))
                  && !(amc::instantiation_build_key(base) == amc::instantiation_build_key(schema_changed)),
            "build key changes with source, toolchain, options, protocol or schema");

        // A scratch cache directory.
        const auto root =
            std::filesystem::temp_directory_path()
            / ("amc_cache_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        amc::InstantiationCache cache(root.string());
        std::string error;
        const std::vector<std::pair<std::string, std::string>> artifacts = {
            {"module.abix", "ARTIFACT-A"}
        };

        check(cache.lookup(base).status == amc::CacheStatus::miss
                  && cache.lookup(base).reason == std::string(amc::cache_reason_not_found),
            "empty cache misses with not_found");
        check(cache.store(base, artifacts, error), "store an instantiation product");
        const auto hit = cache.lookup(base);
        check(hit.status == amc::CacheStatus::hit
                  && hit.reason == std::string(amc::cache_reason_hit)
                  && hit.artifacts.size() == 1
                  && hit.artifacts[0].name == "module.abix",
            "repeated build hits the cache");
        check(cache.verify(hit.build_key, error), "committed entry verifies");
        check(cache.entries() == 1, "one committed entry");

        // Only export alias changes: not part of request identity, so it is the
        // same request and hits. (The alias lives in the exported symbol layer.)
        check(cache.lookup(make_inputs(make_request("abix_span", {"int32_t"}))).status == amc::CacheStatus::hit,
            "an alias-only change is not a new instantiation request");

        // Each input change yields a miss with a precise reason.
        auto reason_for = [&](const amc::InstantiationBuildInputs &inputs) {
            return cache.lookup(inputs).status == amc::CacheStatus::miss ? cache.lookup(inputs).reason : std::string();
        };
        check(reason_for(source_changed) == std::string(amc::cache_reason_source_digest_changed),
            "dependency change invalidates with source_digest_changed");
        check(reason_for(toolchain_changed) == std::string(amc::cache_reason_toolchain_changed),
            "toolchain change invalidates with toolchain_changed");
        check(reason_for(options_changed) == std::string(amc::cache_reason_build_options_changed),
            "build option change invalidates with build_options_changed");
        check(reason_for(make_inputs(retargeted)) == std::string(amc::cache_reason_target_changed),
            "target change invalidates with target_changed");
        check(reason_for(protocol_changed) == std::string(amc::cache_reason_protocol_version_changed),
            "protocol change invalidates with protocol_version_changed");
        check(reason_for(schema_changed) == std::string(amc::cache_reason_schema_version_changed),
            "schema change invalidates with schema_version_changed");

        // A corrupted artifact is rejected, then repaired by the next store.
        const auto entry = std::filesystem::path(hit.directory);
        {
            std::ofstream corrupt(entry / "module.abix", std::ios::binary | std::ios::trunc);
            corrupt << "TAMPERED";
        }
        check(cache.lookup(base).status == amc::CacheStatus::corrupted
                  && cache.lookup(base).reason == std::string(amc::cache_reason_corrupted_artifact),
            "a corrupted artifact is rejected");
        check(!cache.verify(hit.build_key, error), "corrupted entry fails verification");
        check(cache.store(base, artifacts, error) && cache.lookup(base).status == amc::CacheStatus::hit,
            "a corrupt entry is replaced by a re-store");

        // A malformed manifest is reported rather than trusted.
        {
            std::ofstream corrupt(entry / "manifest.json", std::ios::binary | std::ios::trunc);
            corrupt << "{ not json";
        }
        check(cache.lookup(base).status == amc::CacheStatus::invalid_manifest, "a malformed manifest is reported");
        check(cache.store(base, artifacts, error), "re-store replaces a malformed manifest");

        // Unsafe artifact names never touch the filesystem.
        check(!cache.store(base,
                  {
                      {"../escape", "x"}
        },
                  error),
            "an unsafe artifact name is refused");

        check(cache.clear() >= 1 && cache.entries() == 0, "cache can be cleared");
        cleanup_dir(root);

        // Concurrent writers of a fresh key must leave one complete, verifiable
        // entry, never a partial or mixed product.
        {
            const auto concurrent_root = std::filesystem::path(root.string() + "_concurrent");
            amc::InstantiationCache concurrent(concurrent_root.string());
            std::string local_error;
            std::vector<std::thread> writers;
            for (int i = 0; i < 8; ++i) {
                writers.emplace_back([&, i] {
                    const std::string content = "WRITER-" + std::to_string(i);
                    std::string thread_error;
                    amc::InstantiationCache per_thread(concurrent_root.string());
                    (void)per_thread.store(base,
                        {
                            {"module.abix", content}
                    },
                        thread_error);
                });
            }
            for (auto &writer : writers)
                writer.join();
            check(concurrent.verify(amc::instantiation_build_key(base), local_error),
                "concurrent writers leave a verifiable entry");
            const auto after = concurrent.lookup(base);
            check(after.status == amc::CacheStatus::hit && after.artifacts.size() == 1 && concurrent.entries() == 1,
                "concurrent writers leave one consistent entry");
            cleanup_dir(concurrent_root);
        }

        // A recorded dependency (the frontend's include closure) invalidates the
        // entry when it changes, without the cache key listing it.
        {
            const auto dep_root = std::filesystem::path(root.string() + "_deps");
            std::filesystem::create_directories(dep_root);
            const auto dep_file = dep_root / "dep.hpp";
            { std::ofstream(dep_file) << "v1"; }
            amc::InstantiationCache dependency_cache(dep_root.string());
            const auto dependency_for = [&](const std::string &content) {
                return std::vector<amc::CacheDependency>{
                    {dep_file.string(),
                     amc::digest_bytes(content.data(), content.size(), amc::digest_domain_dependency)}
                };
            };
            std::string dependency_error;
            check(dependency_cache.store(base,
                      {
                          {"module.abix", "C"}
            },
                      dependency_for("v1"), dependency_error),
                "store with a dependency closure");
            check(dependency_cache.lookup(base).status == amc::CacheStatus::hit, "unchanged dependency hits");
            { std::ofstream(dep_file, std::ios::app) << "more"; }
            check(dependency_cache.lookup(base).status == amc::CacheStatus::miss
                      && dependency_cache.lookup(base).reason == std::string(amc::cache_reason_dependency_changed),
                "a changed dependency invalidates the entry");
            check(dependency_cache.store(base,
                      {
                          {"module.abix", "C2"}
            },
                      dependency_for("v1more"), dependency_error)
                      && dependency_cache.lookup(base).status == amc::CacheStatus::hit,
                "re-store refreshes the dependency closure");
            cleanup_dir(dep_root);
        }

#if !defined(_WIN32)
        // Cross-process writers of a fresh key: temporary directories must be
        // process-unique, and the committed entry must remain one complete,
        // verifiable product rather than a mix.
        {
            const auto process_root = std::filesystem::path(root.string() + "_proc");
            std::vector<pid_t> children;
            for (int i = 0; i < 4; ++i) {
                const pid_t pid = fork();
                if (pid == 0) {
                    amc::InstantiationCache child_cache(process_root.string());
                    std::string child_error;
                    const std::string content = "PROC-" + std::to_string(i);
                    (void)child_cache.store(base,
                        {
                            {"module.abix", content}
                    },
                        child_error);
                    _exit(0);
                }
                if (pid > 0) children.push_back(pid);
            }
            for (const pid_t pid : children) {
                int status = 0;
                waitpid(pid, &status, 0);
                check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "cache child exits cleanly");
            }
            amc::InstantiationCache process_cache(process_root.string());
            check(process_cache.entries() == 1, "cross-process writers leave one entry");
            check(process_cache.verify(amc::instantiation_build_key(base), error), "cross-process entry verifies");
            const auto process_hit = process_cache.lookup(base);
            check(process_hit.status == amc::CacheStatus::hit && process_hit.artifacts.size() == 1,
                "cross-process entry is a consistent hit");
            cleanup_dir(process_root);
        }
#endif
    }

    if (failures != 0) {
        std::fprintf(stderr, "amc-core-test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("amc-core-test: all checks passed\n");
    return 0;
}
