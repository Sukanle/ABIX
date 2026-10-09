#ifndef AMC_CORE_H
#define AMC_CORE_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace amc {

struct Hash128 {
    uint64_t lo = 0, hi = 0;
};
bool operator==(Hash128 a, Hash128 b);

enum class TypeKind : uint32_t {
    primitive,
    enumeration,
    record,
    pointer,
    array,
    function,
    namespace_type,
    alias
};

// A template primary has metadata useful for dependency closure, but it is not
// a concrete native C++ type that can receive a TypeTraits specialization.
constexpr uint32_t type_template_primary = 1u << 0;

// Runtime ABI kind for primitive types, independent of the source spelling.
// It captures how the type behaves at the binary boundary: signed/unsigned
// integers by width, floating-point by IEEE/format semantics, and the distinct
// ABI kinds for bool/char/wchar and friends. Two spellings that share an ABI
// (`long`/`long long`, a C `double` and a Rust `f64`) map to the same code.
// Character kinds are split by signedness because C/C++ `char` and `wchar_t`
// have an implementation-defined signedness that is part of the ABI: a
// signed vs unsigned `char` on the same target are different types, and the
// same spelling can differ across targets (aarch64/riscv `char` is unsigned,
// x86/macOS `char` is signed). `char8_t`/`char16_t`/`char32_t` are always
// unsigned and need no split.
enum class PrimitiveAbiKind : uint32_t {
    none = 0,
    void_,
    bool_,
    char_signed,
    char_unsigned,
    schar,
    uchar,
    char8,
    char16,
    char32,
    wchar_signed,
    wchar_unsigned,
    sint,
    uint,
    floating
};

constexpr uint32_t primitive_width_shift = 8;
constexpr uint32_t primitive_width_mask = 0xffu << primitive_width_shift;
constexpr uint32_t primitive_format_shift = 16;
constexpr uint32_t primitive_format_mask = 0xffu << primitive_format_shift;
constexpr uint32_t primitive_signed_bit = 1u << 24;

// Floating-point format codes stored in `primitive_format`.
enum class FloatFormat : uint32_t {
    none = 0,
    ieee16,
    bfloat16,
    ieee32,
    ieee64,
    ieee128,
    x87_80,
    ppc_double_double
};

inline uint32_t primitive_abi(uint32_t width, PrimitiveAbiKind kind, FloatFormat format = FloatFormat::none,
    bool is_signed = false) {
    return static_cast<uint32_t>(kind) | (width << primitive_width_shift)
           | (static_cast<uint32_t>(format) << primitive_format_shift) | (is_signed ? primitive_signed_bit : 0);
}

// Field flags are part of the AMC semantic ABI, not Clang implementation details.
constexpr uint32_t field_bitfield = 1u << 0;
constexpr uint32_t field_base = 1u << 1;
constexpr uint32_t field_visibility_shift = 8;
constexpr uint32_t field_visibility_mask = 3u << field_visibility_shift;
constexpr uint32_t field_bit_offset_shift = 16;
constexpr uint32_t field_bit_width_shift = 24;

enum class Visibility : uint32_t {
    public_ = 0,
    protected_ = 1,
    private_ = 2,
    none = 3
};
constexpr uint32_t visibility_flags(Visibility value) { return static_cast<uint32_t>(value) << field_visibility_shift; }
struct Type {
    std::string name;
    TypeKind kind = TypeKind::primitive;
    Hash128 id{};
    Hash128 layout_hash{};
    uint32_t size = 0, align = 0, flags = 0;
    uint32_t field_begin = 0, field_count = 0;
    uint32_t array_count = 0;
    // Primitive ABI kind for `TypeKind::primitive` (0 = none); see
    // `PrimitiveAbiKind` / `primitive_abi`. Kept out of `flags` so it stays a
    // first-class ABI fact rather than a bitmask.
    uint32_t primitive_abi = 0;
};
struct Field {
    Hash128 owner_type{};
    std::string name;
    Hash128 type_id{};
    uint32_t offset = 0, flags = 0;
};
struct Parameter {
    std::string name;
    Hash128 type_id{};
    uint32_t flags = 0;
};
struct Function {
    Hash128 owner_type{};   // zero for a namespace/free function
    std::string name;
    Hash128 signature{};
    Hash128 return_type{};
    std::vector<Parameter> parameters;
    uint32_t calling_convention = 0, flags = 0;
};
enum class SymbolKind : uint32_t {
    type,
    field,
    function
};
struct Symbol {
    std::string name;
    SymbolKind kind = SymbolKind::type;
    uint32_t target_index = 0;
};
enum class HashKind : uint32_t {
    artifact,
    type_id,
    layout,
    signature
};
enum class HashTargetKind : uint32_t {
    module,
    type,
    function
};
struct HashRecord {
    HashKind kind = HashKind::artifact;
    HashTargetKind target_kind = HashTargetKind::module;
    uint32_t target_index = 0;
    uint32_t flags = 0;
    Hash128 value{};
};
struct HashDescriptor {
    uint16_t algorithm = 0;
    uint16_t version = 1;
    uint32_t canonical_version = 1;
    uint32_t flags = 0;
};
enum class CompatibilityKind : uint32_t {
    identical,
    layout_compatible,
    map_compatible,
    incompatible
};
struct CompatibilityRecord {
    Hash128 source_type{}, target_type{};
    CompatibilityKind kind = CompatibilityKind::incompatible;
    uint32_t map_index = UINT32_MAX;
    uint32_t flags = 0;
};
enum class MapOpcode : uint8_t {
    copy_field,
    convert_int,
    convert_float,
    add_default,
    skip_field
};
struct MapOperation {
    MapOpcode opcode = MapOpcode::skip_field;
    uint32_t source_field = UINT32_MAX, target_field = UINT32_MAX;
    uint32_t source_offset = 0, target_offset = 0, byte_count = 0;
    Hash128 auxiliary{};
};
struct MapRecord {
    Hash128 source_type{}, target_type{};
    std::string source_name, target_name;
    uint32_t operation_begin = 0, operation_count = 0, flags = 0;
    std::vector<MapOperation> operations;
};
struct SourceOrigin {
    Hash128 type_id{};
    std::string file;
    uint32_t line = 0, column = 0;
};
struct AbiModule {
    std::string package_name, package_version;
    uint32_t arch = 0, os = 0, target_abi = 0, compiler = 1, calling_convention = 0;
    std::vector<Type> types;
    std::vector<Field> fields;
    std::vector<Function> functions;
    std::vector<Symbol> symbols;
    std::vector<CompatibilityRecord> compatibility;
    std::vector<MapRecord> maps;
    // Optional, debug-only: where each type was declared. Not part of ABI
    // identity (excluded from the artifact hash) — it exists so tools can
    // implement go-to-definition TypeID -> source declaration.
    std::vector<SourceOrigin> sources;
};

Hash128 hash_text(std::string_view text, uint64_t domain);
Hash128 layout_hash(const Type &, const std::vector<Field> &);
Hash128 signature_hash(const Function &);
std::vector<uint8_t> canonical_bytes(const AbiModule &);
Hash128 abi_hash(const AbiModule &);
std::vector<HashRecord> hash_table(const AbiModule &);
bool build_compatibility(const AbiModule &, const AbiModule &, AbiModule &, std::string &error);
const char *compatibility_name(CompatibilityKind);
bool validate(const AbiModule &, std::string &error);
bool project_symbols(const AbiModule &, const std::vector<std::string> &symbols, AbiModule &, std::string &error);

bool write_abix(const AbiModule &, const std::string &path, std::string &error);
bool read_abix(const std::string &path, AbiModule &, std::string &error);
std::string type_kind_name(TypeKind);
// Returns the recorded declaration site of a type, or nullptr.
const SourceOrigin *find_source_origin(const AbiModule &, Hash128 type_id);

}   // namespace amc
#endif
