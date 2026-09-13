#ifndef AMC_METADATA_H
#define AMC_METADATA_H

#include "amc_core.h"

#include <cstdint>
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

static_assert(sizeof(MetadataTypeRecord) == 64, "MetadataTypeRecord wire layout changed");
static_assert(sizeof(MetadataFieldRecord) == 48, "MetadataFieldRecord wire layout changed");
static_assert(sizeof(MetadataFunctionRecord) == 72, "MetadataFunctionRecord wire layout changed");
static_assert(sizeof(MetadataParameterRecord) == 32, "MetadataParameterRecord wire layout changed");
static_assert(sizeof(MetadataSymbolRecord) == 16, "MetadataSymbolRecord wire layout changed");
static_assert(sizeof(MetadataHashRecord) == 24, "MetadataHashRecord wire layout changed");

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

// Loads an AbiModule from either a standalone `.abix` artifact or an ELF
// binary that embeds a `.abix.metadata` region. This lets every consumer
// (CLI, MCP server) accept `foo.abix` and `libfoo.so` interchangeably.
bool load_module_source(const std::string &path, AbiModule &module, std::string &error);

std::string metadata_header_to_json(const MetadataHeader &header);
std::string metadata_meta_document(const AbiModule &module, const MetadataHeader &header);

}   // namespace amc

#endif
