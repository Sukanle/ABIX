#ifndef AMC_METADATA_H
#define AMC_METADATA_H

#include "SKL/AMC/Core/Core.h"

#include <stdint.h>
#include <string>
#include <vector>

namespace amc {

// AI-PM: ABIX Metadata Region.
//
// The Metadata Region is a self-describing, offset-based (pointer-free)
// binary image of an ABI module.  It is deliberately independent of any
// container: the same bytes can be embedded in an ELF `.abix.*` section,
// shipped as a standalone `.abixmeta` file, or mmap'd by an offline parser.
//
// Layout:
//   [ manifest ]  MetadataHeader (128 bytes, little-endian, at offset 0)
//   [ desc     ]  type/field/function/parameter/symbol records
//   [ hash     ]  optional type_id -> type index (accelerates lookup)
//   [ names    ]  optional NUL-terminated string table (strippable)
//
// All cross references inside `desc` and `hash` are offsets relative to the
// region base, or indices into the sibling arrays.  There are no absolute
// pointers and therefore no relocations, so the region survives ASLR, PIE,
// mmap and cross-process transport unchanged.

// BuildID identifies a concrete binary build; MetadataID identifies the
// metadata content.  Both are 128-bit, like every other ABIX identity.
using BuildID = Hash128;
using MetadataID = Hash128;

constexpr uint32_t metadata_magic = 0x58494241u;   // "ABIX" in little-endian byte order
constexpr uint16_t metadata_version_major = 1;
constexpr uint16_t metadata_version_minor = 0;
constexpr size_t metadata_manifest_size = 128;

enum class MetadataFlag : uint32_t {
    none = 0,
    has_names = 1u << 0,
    has_hash_index = 1u << 1,
};

constexpr uint32_t metadata_flag_value(MetadataFlag flag) { return static_cast<uint32_t>(flag); }

// In-memory projection of the on-disk manifest.  The serializer writes the
// fields explicitly at fixed offsets so the wire layout never depends on
// compiler padding; the static_asserts below lock the size down.
struct MetadataHeader {
    uint32_t magic = metadata_magic;
    uint16_t version_major = metadata_version_major;
    uint16_t version_minor = metadata_version_minor;
    uint32_t flags = 0;
    BuildID build_id{};
    MetadataID metadata_id{};
    uint32_t hash_algorithm = 0;
    uint32_t hash_version = 1;
    uint32_t type_count = 0;
    uint32_t field_count = 0;
    uint32_t function_count = 0;
    uint32_t parameter_count = 0;
    uint32_t symbol_count = 0;
    uint64_t desc_offset = metadata_manifest_size;
    uint64_t desc_size = 0;
    uint64_t hash_offset = 0;
    uint64_t hash_size = 0;
    uint64_t names_offset = 0;
    uint64_t names_size = 0;
};

static_assert(sizeof(MetadataHeader) == metadata_manifest_size, "MetadataHeader wire layout changed");

// Fixed-size on-disk records.  They are plain data: every pointer-sized
// concept is spelled as a 32/64-bit offset or index.
struct MetadataTypeRecord {
    Hash128 type_id;
    Hash128 layout_hash;
    uint32_t flags;
    uint32_t kind;
    uint32_t size;
    uint32_t align;
    uint32_t field_begin;
    uint32_t field_count;
    uint32_t array_count;
    uint32_t name_offset;
};
struct MetadataFieldRecord {
    Hash128 owner_type;
    Hash128 type_id;
    uint32_t offset;
    uint32_t flags;
    uint32_t name_offset;
    uint32_t reserved;
};
struct MetadataFunctionRecord {
    Hash128 owner_type;
    Hash128 signature;
    Hash128 return_type;
    uint32_t calling_convention;
    uint32_t flags;
    uint32_t parameter_begin;
    uint32_t parameter_count;
    uint32_t name_offset;
    uint32_t reserved;
};
struct MetadataParameterRecord {
    Hash128 type_id;
    uint32_t flags;
    uint32_t name_offset;
    uint64_t reserved;
};
struct MetadataSymbolRecord {
    uint32_t name_offset;
    uint32_t kind;
    uint32_t target_index;
    uint32_t reserved;
};
struct MetadataHashRecord {
    Hash128 type_id;
    uint32_t type_index;
    uint32_t reserved;
};

// Optional ownership contract record. Ownership is a property of one ABI
// interaction (a parameter or a return value), not of a type, so each record
// points at a parameter or a function return. Records live inside the trailing
// extension directory (see below), so an old reader that stops after the symbol
// table still sees a valid region, and the manifest is unchanged.
struct MetadataOwnershipRecord {
    uint32_t target_kind;        // 0 = parameter, 1 = return value
    uint32_t target_index;       // global parameter index, or function index
    uint32_t mode;               // OwnershipMode
    uint32_t release_function;   // function table index, or UINT32_MAX
    uint32_t flags;              // reserved contract flags
    uint32_t reserved;
};

// Optional error-boundary record: where a function reports its status and where
// the result value is carried.
struct MetadataErrorRecord {
    uint32_t function_index;
    uint32_t status_source;      // StatusSource
    uint32_t status_parameter;   // local parameter index, or UINT32_MAX
    uint32_t value_parameter;    // local parameter index, or UINT32_MAX
    int64_t success_value;
    uint32_t flags;
    uint32_t reserved;
};

// Target platform identity, carried as an extension so runtime verification can
// compare the artifact's platform against the host (or a required target).
struct MetadataTargetRecord {
    uint32_t arch;
    uint32_t os;
    uint32_t target_abi;
    uint32_t compiler;
    uint32_t calling_convention;
    uint32_t pointer_bits;   // 0 = unknown
    uint32_t endianness;     // 0 unknown, 1 little, 2 big
    uint32_t flags;
};

// Extension kind 12: the Canonical Layout Encoding version used by every
// LayoutHash in the region.
struct MetadataLayoutEncodingRecord {
    uint32_t version;
    uint32_t reserved;
};

// ABIX-STD lowering result, carried as an extension (kind = 4). Types are
// referenced by their index in the type table. A lowering record lets a reader
// discover that an adapter is required; it does not by itself execute anything.
struct MetadataLoweringRecord {
    uint32_t type_index;       // concrete instantiated type
    uint32_t contract_index;   // canonical boundary type
    uint32_t kind;             // LoweringKind
    uint32_t status;           // LoweringStatus
    uint32_t reason;           // LoweringReason
    uint32_t pointer_field;
    uint32_t length_field;
    uint32_t reserved;
};

// Container accessor adapters (kind = 5 view, 6 range). Functions are referenced
// by their index in the function table; `flags` carries the contract semantics.
struct MetadataViewAdapterRecord {
    uint32_t source_index;
    uint32_t target_index;
    uint32_t data_accessor;
    uint32_t size_accessor;
    uint32_t flags;
    uint32_t reserved;
};
struct MetadataRangeAdapterRecord {
    uint32_t source_index;
    uint32_t size_accessor;
    uint32_t at_accessor;
    uint32_t flags;
};

// Non-intrusive interface contracts (kind = 7 identity, 8 methods, 9
// implementations, 10 method bindings, 11 method parameters). Interfaces are
// independent of any compiler vtable: method identity is (interface id, slot)
// and a binding names the native adapter by signature, never a vtable slot.
struct MetadataInterfaceRecord {
    Hash128 interface_id;
    uint32_t version;
    uint32_t method_begin;
    uint32_t method_count;
    uint32_t flags;
    uint32_t name_offset;
};
struct MetadataInterfaceMethodRecord {
    Hash128 interface_id;
    Hash128 signature;
    Hash128 return_type;
    uint32_t slot;
    uint32_t flags;
    uint32_t name_offset;
    uint32_t parameter_begin;
    uint32_t parameter_count;
};
struct MetadataInterfaceParameterRecord {
    uint32_t type_index;
    uint32_t flags;
};
struct MetadataInterfaceImplementationRecord {
    Hash128 interface_id;
    Hash128 concrete_type;
    uint32_t binding_begin;
    uint32_t binding_count;
    uint32_t flags;
    uint32_t reserved;
};
struct MetadataMethodBindingRecord {
    Hash128 interface_id;
    Hash128 method_signature;
    Hash128 native_function;   // adapter signature, or zero when resolved elsewhere
    uint32_t slot;
    uint32_t flags;
};

// Trailing extension directory: how optional contract tables are discovered.
//
// Layout (inside the desc section, after the symbol table):
//   [ header  ]  MetadataExtensionHeader (16 bytes)
//   [ entries ]  MetadataExtensionEntry * count (24 bytes each)
//   [ records ]  extension payloads, each addressed by an entry
//
// Rules:
//   * a region with no extensions has `desc_size == known_size`; its bytes are
//     identical to a region produced before this directory existed;
//   * `directory.version` describes the directory encoding only. Each entry has
//     its own `version` for the contract's semantics, so contracts evolve
//     independently;
//   * `kind` names the contract type (not the record shape); `byte_size` alone
//     fixes record boundaries. A reader may skip an unknown `kind` only when the
//     directory is well formed and the boundaries check out;
//   * an unknown directory version, a duplicate `kind`, or an out-of-bounds
//     entry is rejected rather than guessed at.
constexpr uint32_t metadata_extension_version = 1;
constexpr uint32_t metadata_extension_ownership = 1;
constexpr uint32_t metadata_extension_error = 2;
constexpr uint32_t metadata_extension_target = 3;
constexpr uint32_t metadata_extension_lowering = 4;
constexpr uint32_t metadata_extension_view = 5;
constexpr uint32_t metadata_extension_range = 6;
// Non-intrusive polymorphism. The interface contract family spans several
// kinds so each table keeps a fixed record shape.
constexpr uint32_t metadata_extension_interface = 7;
constexpr uint32_t metadata_extension_interface_method = 8;
constexpr uint32_t metadata_extension_implementation = 9;
constexpr uint32_t metadata_extension_method_binding = 10;
constexpr uint32_t metadata_extension_interface_parameter = 11;
// Canonical Layout Encoding version carried by the region (1 = CLE v1). It
// lets a consumer interpret the LayoutHash values without reading the .abix.
constexpr uint32_t metadata_extension_layout_encoding = 12;

// Semantics of an accessor adapter, as a contract flag (not a layout fact).
constexpr uint32_t metadata_adapter_borrowed = 1u << 0;   // view over live data
constexpr uint32_t metadata_adapter_owned = 1u << 1;      // receiver releases
constexpr uint32_t metadata_adapter_copy = 1u << 2;       // independent data

struct MetadataExtensionHeader {
    uint32_t version;
    uint32_t count;
    uint32_t flags;
    uint32_t reserved;
};
struct MetadataExtensionEntry {
    uint32_t kind;
    uint32_t version;
    uint32_t count;
    uint32_t byte_offset;   // relative to the start of the desc section
    uint32_t byte_size;
    uint32_t reserved;
};

static_assert(sizeof(MetadataLayoutEncodingRecord) == 8, "MetadataLayoutEncodingRecord wire layout changed");
static_assert(sizeof(MetadataTypeRecord) == 64, "MetadataTypeRecord wire layout changed");
static_assert(sizeof(MetadataFieldRecord) == 48, "MetadataFieldRecord wire layout changed");
static_assert(sizeof(MetadataFunctionRecord) == 72, "MetadataFunctionRecord wire layout changed");
static_assert(sizeof(MetadataParameterRecord) == 32, "MetadataParameterRecord wire layout changed");
static_assert(sizeof(MetadataSymbolRecord) == 16, "MetadataSymbolRecord wire layout changed");
static_assert(sizeof(MetadataHashRecord) == 24, "MetadataHashRecord wire layout changed");
static_assert(sizeof(MetadataOwnershipRecord) == 24, "MetadataOwnershipRecord wire layout changed");
static_assert(sizeof(MetadataErrorRecord) == 32, "MetadataErrorRecord wire layout changed");
static_assert(sizeof(MetadataTargetRecord) == 32, "MetadataTargetRecord wire layout changed");
static_assert(sizeof(MetadataLoweringRecord) == 32, "MetadataLoweringRecord wire layout changed");
static_assert(sizeof(MetadataViewAdapterRecord) == 24, "MetadataViewAdapterRecord wire layout changed");
static_assert(sizeof(MetadataRangeAdapterRecord) == 16, "MetadataRangeAdapterRecord wire layout changed");
static_assert(sizeof(MetadataInterfaceRecord) == 40, "MetadataInterfaceRecord wire layout changed");
static_assert(sizeof(MetadataInterfaceMethodRecord) == 72, "MetadataInterfaceMethodRecord wire layout changed");
static_assert(sizeof(MetadataInterfaceParameterRecord) == 8, "MetadataInterfaceParameterRecord wire layout changed");
static_assert(
    sizeof(MetadataInterfaceImplementationRecord) == 48, "MetadataInterfaceImplementationRecord wire layout changed");
static_assert(sizeof(MetadataMethodBindingRecord) == 56, "MetadataMethodBindingRecord wire layout changed");
static_assert(sizeof(MetadataExtensionHeader) == 16, "MetadataExtensionHeader wire layout changed");
static_assert(sizeof(MetadataExtensionEntry) == 24, "MetadataExtensionEntry wire layout changed");

// A decoded ownership contract, resolved against the region's function table.
struct MetadataOwnershipContract {
    uint32_t target_kind = 0;   // 0 = parameter, 1 = return value
    uint32_t target_index = 0;
    OwnershipMode mode = OwnershipMode::unspecified;
    uint32_t release_function = UINT32_MAX;   // function table index, or UINT32_MAX
};

// A decoded error-boundary contract.
struct MetadataErrorContract {
    uint32_t function_index = 0;
    StatusSource status_source = StatusSource::return_value;
    uint32_t status_parameter = UINT32_MAX;
    int64_t success_value = 0;
    uint32_t value_parameter = UINT32_MAX;
};

// A decoded ABIX-STD lowering result (types referenced by table index).
struct MetadataLoweringContract {
    uint32_t type_index = 0;
    uint32_t contract_index = 0;
    LoweringKind kind = LoweringKind::sequence_view;
    LoweringStatus status = LoweringStatus::unsupported;
    LoweringReason reason = LoweringReason::ok;
    uint32_t pointer_field = UINT32_MAX;
    uint32_t length_field = UINT32_MAX;
};

// Decoded container accessor adapters (functions referenced by table index).
struct MetadataViewAdapter {
    uint32_t source_index = 0;
    uint32_t target_index = 0;
    uint32_t data_accessor = 0;
    uint32_t size_accessor = 0;
    uint32_t semantics = metadata_adapter_borrowed;
};
struct MetadataRangeAdapter {
    uint32_t source_index = 0;
    uint32_t size_accessor = 0;
    uint32_t at_accessor = 0;
    uint32_t semantics = metadata_adapter_borrowed;
};

// Decoded non-intrusive interface contracts. Types and parameters are given as
// type-table indices, matching the region's file-local reference model.
struct MetadataInterfaceContract {
    Hash128 id{};
    uint32_t version = 1;
    uint32_t method_begin = 0;
    uint32_t method_count = 0;
};
struct MetadataInterfaceMethod {
    Hash128 interface_id{};
    Hash128 signature{};
    Hash128 return_type{};
    uint32_t slot = 0;
    uint32_t flags = 0;
    std::vector<uint32_t> parameter_types;   // type table indices
    std::vector<uint32_t> parameter_flags;
};
struct MetadataInterfaceImplementation {
    Hash128 interface_id{};
    Hash128 concrete_type{};
    uint32_t binding_begin = 0;
    uint32_t binding_count = 0;
};
struct MetadataMethodBinding {
    Hash128 interface_id{};
    Hash128 method_signature{};
    Hash128 native_function{};
    uint32_t slot = 0;
    uint32_t flags = 0;
};

// Reads the optional ownership contract records from a Metadata Region. Regions
// without them yield an empty `out` (every interaction is `unspecified`); an
// unknown contract version or a malformed record is reported as an error rather
// than being silently treated as safe.
bool read_ownership_contracts(
    const uint8_t *data, size_t size, std::vector<MetadataOwnershipContract> &out, std::string &error);
bool read_ownership_contracts(
    const std::vector<uint8_t> &region, std::vector<MetadataOwnershipContract> &out, std::string &error);

// Reads the optional error-boundary contracts. Same rules as ownership.
bool read_error_contracts(
    const uint8_t *data, size_t size, std::vector<MetadataErrorContract> &out, std::string &error);
bool read_error_contracts(
    const std::vector<uint8_t> &region, std::vector<MetadataErrorContract> &out, std::string &error);

// Reads the optional ABIX-STD lowering results. Same rules as ownership.
bool read_lowering_contracts(
    const uint8_t *data, size_t size, std::vector<MetadataLoweringContract> &out, std::string &error);
bool read_lowering_contracts(
    const std::vector<uint8_t> &region, std::vector<MetadataLoweringContract> &out, std::string &error);

// Reads the optional container accessor adapters (view / range). The `semantics`
// flag carries borrow/owned/copy; the lifetime and invalidation rules are the
// caller's contract, expressed here so a reader can report them.
bool read_view_adapters(const uint8_t *data, size_t size, std::vector<MetadataViewAdapter> &out, std::string &error);
bool read_view_adapters(const std::vector<uint8_t> &region, std::vector<MetadataViewAdapter> &out, std::string &error);
bool read_range_adapters(const uint8_t *data, size_t size, std::vector<MetadataRangeAdapter> &out, std::string &error);
bool read_range_adapters(
    const std::vector<uint8_t> &region, std::vector<MetadataRangeAdapter> &out, std::string &error);

// Reads the optional non-intrusive interface contract (identity, methods,
// implementations, method bindings). Same rules as ownership: an absent
// extension yields an empty result, an unknown version or malformed record is
// an error. Method parameters are decoded alongside their method.
bool read_interface_contracts(
    const uint8_t *data, size_t size, std::vector<MetadataInterfaceContract> &out, std::string &error);
bool read_interface_contracts(
    const std::vector<uint8_t> &region, std::vector<MetadataInterfaceContract> &out, std::string &error);
bool read_interface_methods(
    const uint8_t *data, size_t size, std::vector<MetadataInterfaceMethod> &out, std::string &error);
bool read_interface_methods(
    const std::vector<uint8_t> &region, std::vector<MetadataInterfaceMethod> &out, std::string &error);
bool read_interface_implementations(
    const uint8_t *data, size_t size, std::vector<MetadataInterfaceImplementation> &out, std::string &error);
bool read_interface_implementations(
    const std::vector<uint8_t> &region, std::vector<MetadataInterfaceImplementation> &out, std::string &error);
bool read_method_bindings(
    const uint8_t *data, size_t size, std::vector<MetadataMethodBinding> &out, std::string &error);
bool read_method_bindings(
    const std::vector<uint8_t> &region, std::vector<MetadataMethodBinding> &out, std::string &error);

// Target platform description recovered from a region. `present` is false when
// the region carries no target extension (all fields unknown).
struct TargetDescription {
    uint32_t arch = 0;
    uint32_t os = 0;
    uint32_t target_abi = 0;
    uint32_t compiler = 0;
    uint32_t calling_convention = 0;
    uint32_t pointer_bits = 0;
    uint32_t endianness = 0;
    bool present = false;
};
bool read_target_description(const uint8_t *data, size_t size, TargetDescription &out, std::string &error);
bool read_target_description(const std::vector<uint8_t> &region, TargetDescription &out, std::string &error);
// Read the Canonical Layout Encoding version from the region. `present` is
// false for a region written before the extension existed; a present value
// other than 1 is rejected as unsupported.
bool read_layout_encoding(const uint8_t *data, size_t size, uint32_t &version, bool &present, std::string &error);
bool read_layout_encoding(const std::vector<uint8_t> &region, uint32_t &version, bool &present, std::string &error);

// Target ABI verification. A field of 0 in the expectation means "do not
// check". The result separates the three outcomes the plan calls for:
//   compatible   — every checked fact matched
//   incompatible — a checked fact disagreed
//   unverifiable — evidence is missing (no target description, or the region
//                  leaves a checked fact unknown); never reported as compatible
enum class TargetVerificationStatus : uint32_t {
    compatible = 0,
    incompatible = 1,
    unverifiable = 2
};
struct TargetExpectation {
    uint32_t arch = 0;
    uint32_t os = 0;
    uint32_t target_abi = 0;
    uint32_t pointer_bits = 0;
    uint32_t endianness = 0;
    uint32_t calling_convention = 0;   // 0 = do not check
};
struct TargetVerification {
    TargetVerificationStatus status = TargetVerificationStatus::unverifiable;
    std::vector<std::string> reasons;
};
bool verify_target_abi(
    const uint8_t *data, size_t size, const TargetExpectation &expected, TargetVerification &out, std::string &error);
bool verify_target_abi(
    const std::vector<uint8_t> &region, const TargetExpectation &expected, TargetVerification &out, std::string &error);

// Structured ABI verification: each dimension is reported independently so a
// tool can say precisely what matched, what disagreed, and what could not be
// checked from the region alone. Facts that need the actual binary or toolchain
// (for example real field offsets in a compiled object) are NOT claimed here;
// they belong to the build stage and are reported as unverifiable.
struct AbiVerificationReport {
    TargetVerificationStatus platform = TargetVerificationStatus::unverifiable;
    TargetVerificationStatus layout = TargetVerificationStatus::unverifiable;
    TargetVerificationStatus calling_convention = TargetVerificationStatus::unverifiable;
    std::vector<std::string> reasons;
};
bool verify_abi(const uint8_t *data, size_t size, const TargetExpectation &expected, AbiVerificationReport &out,
    std::string &error);
bool verify_abi(const std::vector<uint8_t> &region, const TargetExpectation &expected, AbiVerificationReport &out,
    std::string &error);

struct MetadataOptions {
    BuildID build_id{};
    bool include_names = true;
    bool include_hash_index = true;
    uint16_t version_major = metadata_version_major;
    uint16_t version_minor = metadata_version_minor;
};

// Serializes `module` into a complete Metadata Region.
bool build_metadata_region(
    const AbiModule &module, const MetadataOptions &options, std::vector<uint8_t> &region, std::string &error);

// Parses and validates the manifest and section bounds.  Does not verify the
// content hash; call verify_metadata_region for that.
bool read_metadata_header(const uint8_t *data, size_t size, MetadataHeader &header, std::string &error);
bool read_metadata_header(const std::vector<uint8_t> &region, MetadataHeader &header, std::string &error);

// Recomputes MetadataID over desc+hash+names and compares it with the
// manifest.  Returns false and fills `error` on any structural or identity
// mismatch.
bool verify_metadata_region(const uint8_t *data, size_t size, std::string &error);
bool verify_metadata_region(const std::vector<uint8_t> &region, std::string &error);

// MetadataID over an arbitrary canonical content buffer (used for tests and
// for callers that already have the desc+hash+names slice).
MetadataID metadata_content_id(const uint8_t *data, size_t size);

// Decodes a Metadata Region back into the in-memory AbiModule model — the
// offline parser the plan asks for. Names are recovered from the region's
// names table when present; package/version/target are not part of the
// manifest and therefore come back empty.
bool module_from_region(const uint8_t *data, size_t size, AbiModule &module, std::string &error);
bool module_from_region(const std::vector<uint8_t> &region, AbiModule &module, std::string &error);

// Loads an AbiModule from either a standalone `.abix` artifact or an ELF /
// Mach-O binary that embeds a `.abix.metadata` region. This lets every consumer
// (CLI, MCP server) accept `foo.abix`, `libfoo.so` and `foo.dylib`
// interchangeably.
bool load_module_source(const std::string &path, AbiModule &module, std::string &error);

std::string metadata_header_to_json(const MetadataHeader &header);
std::string metadata_meta_document(const AbiModule &module, const MetadataHeader &header);

}   // namespace amc

#endif
