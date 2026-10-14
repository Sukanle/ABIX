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

inline uint32_t primitive_abi(
    uint32_t width, PrimitiveAbiKind kind, FloatFormat format = FloatFormat::none, bool is_signed = false) {
    return static_cast<uint32_t>(kind)
         | (width << primitive_width_shift)
         | (static_cast<uint32_t>(format) << primitive_format_shift)
         | (is_signed ? primitive_signed_bit : 0);
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

// Ownership semantics of one ABI interaction (a parameter or a return value),
// not a fixed property of a type: the same type may be borrowed by one function
// and owned by another. This is a contract, not a layout fact, so it never
// feeds the layout/ABI identity.
//   unspecified — not declared (also the meaning of an absent record)
//   borrowed    — a non-owning view; the callee/caller must not release it
//   owned       — the receiver takes ownership and must release via `release`
//   copy        — independent data produced by an explicit copy
enum class OwnershipMode : uint32_t {
    unspecified = 0,
    borrowed = 1,
    owned = 2,
    copy = 3
};

// Release policy: the entry points that create and release a value type. It is
// referenced by an `owned` contract; on its own it is not an ownership label.
struct OwnedType {
    Hash128 type_id{};
    Hash128 create{};
    Hash128 destroy{};
    uint32_t flags = 0;
};

// Which ABI interaction an ownership contract describes.
enum class OwnershipTarget : uint32_t {
    parameter = 0,
    return_value = 1
};

// One ownership contract: the declared mode of a parameter or return value,
// plus the entry point that releases it when the mode is `owned`. It is a
// contract, not a layout fact, so it never feeds the layout/ABI identity.
struct OwnershipContract {
    OwnershipTarget target = OwnershipTarget::parameter;
    uint32_t target_index = 0;   // global parameter index, or function index for a return
    OwnershipMode mode = OwnershipMode::unspecified;
    Hash128 release{};   // release function signature (owned only), or zero
    uint32_t flags = 0;
};

// Where a function reports its status.
enum class StatusSource : uint32_t {
    return_value = 0,
    parameter = 1
};

// Error boundary contract: how a function reports success/failure and where the
// result value is carried. It lets a consumer turn a C status code into a typed
// result instead of inspecting a bare integer. A contract, not a layout fact.
struct ErrorContract {
    uint32_t function_index = 0;
    StatusSource status_source = StatusSource::return_value;
    uint32_t status_parameter = UINT32_MAX;   // local parameter index when status_source == parameter
    int64_t success_value = 0;                // status value that means success
    uint32_t value_parameter =
        UINT32_MAX;   // local parameter index carrying the value, or UINT32_MAX when the return value is the value
    uint32_t flags = 0;
};

// ABIX-STD lowering: the result of matching a concrete instantiated type against
// an ABIX-STD boundary contract. Instantiation and lowering are separate stages:
// a successful instantiation does not by itself satisfy a contract.
enum class LoweringKind : uint32_t {
    sequence_view = 1,
    string_view = 2,
    optional = 3,
    result = 4,
    callback = 5,
    range = 6,
    opaque = 7
};
enum class LoweringStatus : uint32_t {
    direct = 0,          // the concrete type is already the boundary entity
    needs_adapter = 1,   // compatible shape, but a thin adapter must be generated
    unsupported = 2      // cannot be lowered to the contract
};
// Why a lowering landed on its status. Recorded so tools can explain the
// decision (amc check/diff, diagnostics, MCP/Agent) instead of only "incompatible".
enum class LoweringReason : uint32_t {
    ok = 0,
    not_a_record,
    field_count,           // not exactly two fields
    missing_data,          // no pointer/contiguous-data field
    missing_length,        // no unsigned length field
    element_mismatch,      // pointee element differs from the declared element
    mutability_mismatch,   // a mutable contract needs a non-const pointer
    length_type,           // length is not an acceptable unsigned integer
    layout                 // field order/offset/size differ from the canonical view
};
struct LoweringResult {
    Hash128 type_id{};                     // the concrete instantiated type
    Hash128 contract_type{};               // canonical boundary type (equal to type_id when direct)
    uint32_t kind = 0;                     // LoweringKind
    uint32_t status = 0;                   // LoweringStatus
    uint32_t reason = 0;                   // LoweringReason
    uint32_t pointer_field = UINT32_MAX;   // field index of the data pointer within type_id
    uint32_t length_field = UINT32_MAX;    // field index of the length within type_id
};

// A container lowered to a sequence view through accessor functions, for example
// a `std::vector<T>` exposing `data()` and `size()`. The container's internal
// layout is never exposed; AMC generates a thin adapter that calls the accessors.
struct ViewAdapter {
    Hash128 source_type{};     // the container type
    Hash128 target_type{};     // canonical sequence-view type
    Hash128 data_accessor{};   // function signature: const T * (const Source *)
    Hash128 size_accessor{};   // function signature: unsigned length (const Source *)
};

// A non-contiguous container exposed as an iterable range through accessors: a
// length plus indexed access. No contiguous-memory assumption.
struct RangeAdapter {
    Hash128 source_type{};
    Hash128 size_accessor{};   // function signature: unsigned length (const Source *)
    Hash128 at_accessor{};     // function signature: T (const Source *, unsigned index)
};

// Identity of one instantiation request (template + canonical arguments + target
// + frontend), mapped to the concrete entity it produced. This is the request
// identity layer; it is not the concrete ABI identity nor the export symbol name.
struct InstantiationRecord {
    Hash128 request{};
    Hash128 type{};
};

// A pointer type's pointee, recorded so element/contract matching can use ABI
// identity (TypeID) instead of a spelling. Mutability is a separate qualifier,// not part of the base element identity:
// the contract matcher checks it. A pointer without a pointee entry is matched by spelling as a fallback.
constexpr uint32_t pointee_const = 1u << 0;
struct PointeeRef {
    Hash128 pointer{};
    Hash128 pointee{};
    uint32_t flags = 0;   // pointee_const when the pointed-to data is read-only
};

struct AbiModule {
    std::string package_name, package_version;
    uint32_t arch = 0, os = 0, target_abi = 0, compiler = 1, calling_convention = 0;
    // Target platform facts used for verification. 0 means "unknown", which is
    // reported as unverifiable rather than assumed compatible.
    uint32_t pointer_bits = 0;   // pointer width in bits
    uint32_t endianness = 0;     // 0 unknown, 1 little, 2 big
    std::vector<Type> types;
    std::vector<Field> fields;
    std::vector<Function> functions;
    std::vector<Symbol> symbols;
    std::vector<CompatibilityRecord> compatibility;
    std::vector<MapRecord> maps;
    // Ownership contract (see `OwnedType` / `OwnershipContract`). Not part of
    // ABI identity.
    std::vector<OwnedType> owned;                      // release-policy registry
    std::vector<OwnershipContract> contracts;          // per-interaction ownership modes
    std::vector<ErrorContract> errors;                 // per-function error boundary
    std::vector<LoweringResult> lowerings;             // ABIX-STD contract match results
    std::vector<PointeeRef> pointees;                  // pointer -> pointee TypeID
    std::vector<ViewAdapter> view_adapters;            // container -> sequence-view accessors
    std::vector<RangeAdapter> range_adapters;          // container -> iterable range accessors
    std::vector<InstantiationRecord> instantiations;   // instantiation request identity -> concrete type
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

// ABIX-STD lowering: match a concrete instantiated type against the
// `sequence-view` contract. Returns the result (never fails): `direct` when the
// type is already the boundary entity, `needs_adapter` when the shape is
// compatible but a thin adapter must be generated, `unsupported` otherwise.
// The element is matched by pointee TypeID when `element_id` is set (and the
// pointer has a recorded pointee), otherwise by `element_spelling`; empty means
// "do not check the element". `contract_mutable` requires a non-const pointer.
LoweringResult evaluate_sequence_view(const AbiModule &module, Hash128 type_id, Hash128 element_id,
    const std::string &element_spelling, bool contract_mutable);

// Match a concrete type against the `string-view` contract: `{ data, length,
// encoding }`, with the code unit matched by pointee TypeID.
LoweringResult evaluate_string_view(const AbiModule &module, Hash128 type_id, Hash128 element_id,
    const std::string &element_spelling, bool contract_mutable);

// P1 contracts (value layouts matched by shape + element TypeID).
LoweringResult evaluate_optional(
    const AbiModule &module, Hash128 type_id, Hash128 element_id, const std::string &element_spelling);
LoweringResult evaluate_result(
    const AbiModule &module, Hash128 type_id, Hash128 element_id, const std::string &element_spelling);
LoweringResult evaluate_callback(const AbiModule &module, Hash128 type_id);

// Mark a type as an intentionally opaque handle (Map / Set / Deque / custom
// containers): its layout is not exposed and it is used through accessors.
LoweringResult evaluate_opaque(const AbiModule &module, Hash128 type_id);

// Resolve a type spelling (for example `int`) to a TypeID by normalized name,
// or a zero Hash128 when not found. `const int` and `int` resolve together.
Hash128 find_type_id_by_spelling(const AbiModule &module, const std::string &spelling);

bool write_abix(const AbiModule &, const std::string &path, std::string &error);
bool read_abix(const std::string &path, AbiModule &, std::string &error);
std::string type_kind_name(TypeKind);
// Returns the recorded declaration site of a type, or nullptr.
const SourceOrigin *find_source_origin(const AbiModule &, Hash128 type_id);

}   // namespace amc
#endif
