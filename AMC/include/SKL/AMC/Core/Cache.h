#ifndef AMC_CACHE_H
#define AMC_CACHE_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "SKL/AMC/Core/Core.h"

namespace amc {

// M3.4: the instantiation request cache.
//
// The cache reuses an instantiation product across builds only when every input
// that can change the result is identical. Identity stays layered (plan §18):
//
//   * Request Identity  — what was asked for: canonical template + arguments +
//     target + contract. Used for request deduplication and lookup.
//   * ABI Identity      — the concrete ABI that was produced (TypeID /
//     LayoutHash). Used for compatibility; it is *not* request identity.
//   * Symbol Identity   — the exported name. Never part of request identity, so
//     an alias rename cannot masquerade as a new template instantiation.
//
// A read hit means "same request and same build inputs" and nothing more: a
// cache hit is not an ABI compatibility proof, so consumers still run their ABI
// checks on the reused product.

// One canonical instantiation request.
struct InstantiationRequest {
    std::string template_name;            // canonical template identity
    std::vector<std::string> arguments;   // canonical type arguments, in order
    uint32_t arch = 0;
    uint32_t os = 0;
    uint32_t target_abi = 0;
    uint32_t pointer_bits = 0;
    std::string contract;   // normalized ABIX-STD contract (or empty)
};

// Canonical, spelling-normalized request identity. Two requests with the same
// id describe the same instantiation; a different template, argument order,
// target or contract is a different request.
Hash128 instantiation_request_id(const InstantiationRequest &request);

// Target-agnostic template signature (template + ordered arguments + contract).
// Two requests with the same signature but different targets are the same
// template asked for a different target — the reason a cache lookup reports
// `target_changed` rather than `not_found`.
Hash128 instantiation_template_signature(const InstantiationRequest &request);

// Normalize a type spelling for request identity (strip cv/elaborated
// qualifiers and collapse whitespace) without resolving it to a TypeID. Callers
// that already have a canonical identity (for example a TypeID hex) may pass it
// through unchanged.
std::string normalize_instantiation_argument(const std::string &spelling);

// Inputs that decide whether a cached product can be reused for this build.
struct InstantiationBuildInputs {
    InstantiationRequest request;
    Hash128 source_digest{};         // template + dependency source content
    Hash128 toolchain{};             // compiler/frontend fingerprint
    Hash128 build_options{};         // flags and other build options
    uint32_t protocol_version = 1;   // AMC instantiation protocol version
    uint32_t schema_version = 1;     // artifact serialization schema version
};

// Stable build key over every reuse-decision input.
Hash128 instantiation_build_key(const InstantiationBuildInputs &inputs);

// The protocol / schema versions this build understands.
constexpr uint32_t instantiation_protocol_version = 1;
constexpr uint32_t instantiation_schema_version = 1;

// Domain-separated digests so callers derive digests through the same hash
// rules as the rest of ABIX rather than inventing a cache-local scheme.
Hash128 digest_bytes(const void *data, size_t size, uint64_t domain);
Hash128 digest_text(const std::string &text, uint64_t domain);
constexpr uint64_t digest_domain_source = 0x534f5552ULL;       // "SOUR"
constexpr uint64_t digest_domain_toolchain = 0x544f4f4cULL;    // "TOOL"
constexpr uint64_t digest_domain_options = 0x4f505453ULL;      // "OPTS"
constexpr uint64_t digest_domain_artifact = 0x41525449ULL;     // "ARTI"
constexpr uint64_t digest_domain_dependency = 0x44455053ULL;   // "DEPS"
constexpr uint64_t digest_domain_build_key = 0x4255494cULL;    // "BUIL"

// One cached artifact: a logical name and the digest of its bytes.
struct CacheArtifact {
    std::string name;
    Hash128 digest{};
};

// A file the cached product depends on (from the frontend's include closure or
// an explicit declaration). The entry stores each dependency's digest, so a
// lookup can re-hash the current file and refuse the reuse when it changed —
// even for a header the cache key itself does not mention.
struct CacheDependency {
    std::string path;
    Hash128 digest{};
};

enum class CacheStatus : uint32_t {
    hit = 0,
    miss,               // no entry for this build key
    corrupted,          // an entry exists but an artifact digest failed
    invalid_manifest,   // an entry exists but the manifest is malformed
    schema_mismatch,    // an entry exists for a different schema/protocol version
};

const char *cache_status_name(CacheStatus status);

// Diagnostics strings recorded in CacheLookup::reason.
constexpr const char *cache_reason_hit = "hit";
constexpr const char *cache_reason_not_found = "not_found";
constexpr const char *cache_reason_source_digest_changed = "source_digest_changed";
constexpr const char *cache_reason_toolchain_changed = "toolchain_changed";
constexpr const char *cache_reason_build_options_changed = "build_options_changed";
constexpr const char *cache_reason_target_changed = "target_changed";
constexpr const char *cache_reason_protocol_version_changed = "protocol_version_changed";
constexpr const char *cache_reason_schema_version_changed = "schema_version_changed";
constexpr const char *cache_reason_corrupted_artifact = "corrupted_artifact";
constexpr const char *cache_reason_invalid_manifest = "invalid_manifest";
constexpr const char *cache_reason_dependency_changed = "dependency_changed";

struct CacheLookup {
    CacheStatus status = CacheStatus::miss;
    std::string reason;   // e.g. "source_digest_changed"
    Hash128 build_key{};
    std::string directory;                       // entry directory (may not exist on miss)
    std::vector<CacheArtifact> artifacts;        // populated on a verified hit
    std::vector<CacheDependency> dependencies;   // populated on a verified hit
};

// A content-addressed, immutable instantiation cache rooted at one directory.
// Entries are written to a unique temporary directory, verified, then committed
// by rename, so a failed or partial write never becomes a visible entry. A
// lookup verifies the manifest and every artifact digest, so a corrupted entry
// is reported rather than trusted.
class InstantiationCache {
public:
    explicit InstantiationCache(std::string root);

    const std::string &root() const { return _root; }

    // Store `artifacts` (name -> bytes) under the build key. Returns true on
    // success, or when an equivalent entry already exists (a concurrent writer
    // won the race). A pre-existing corrupt entry is replaced only after the new
    // one verifies. Failed writes leave no partial entry behind.
    bool store(const InstantiationBuildInputs &inputs,
        const std::vector<std::pair<std::string, std::string>> &artifacts, std::string &error);

    // Store with a dependency list: the entry is reusable only while every
    // dependency still hashes to the recorded digest.
    bool store(const InstantiationBuildInputs &inputs,
        const std::vector<std::pair<std::string, std::string>> &artifacts,
        const std::vector<CacheDependency> &dependencies, std::string &error);

    // Look up the entry for `inputs`, verifying the manifest, artifacts and
    // every recorded dependency. On a miss the `reason` explains which input
    // differs from any same-template entry, so incremental builds can report
    // why they rebuilt.
    CacheLookup lookup(const InstantiationBuildInputs &inputs) const;

    // Verify a committed entry (manifest schema/versions + artifact digests).
    bool verify(const Hash128 &build_key, std::string &error) const;

    // Verify every committed entry. Appends one human-readable failure per bad
    // entry and returns the number of entries checked.
    size_t verify_all(std::vector<std::string> &failures) const;

    // Number of committed entries (directories with a valid manifest). A
    // malformed entry counts as present so it can be reported and repaired.
    size_t entries() const;

    // Remove every committed entry, returning the number removed.
    size_t clear() const;

    std::string entry_directory(const Hash128 &build_key) const;

private:
    std::string _root;
};

}   // namespace amc

#endif   // AMC_CACHE_H
