#include "SKL/AMC/Core/Cache.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#if defined(_WIN32)
#  include <process.h>
#else
#  include <unistd.h>
#endif

namespace amc {
namespace {

namespace fs = std::filesystem;

// Process id, so two processes writing the same key never share a temporary
// directory name (the atomic counter is only unique within one process).
long current_process_id() noexcept {
#if defined(_WIN32)
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

constexpr const char *kManifestName = "manifest.json";
constexpr const char *kManifestSchema = "abix.instantiation-cache/1";
constexpr uint64_t kRequestDomain = 0x494e5354ULL;    // "INST"
constexpr uint64_t kTemplateDomain = 0x54454d50ULL;   // "TEMP"

std::string hex_u64(uint64_t value) {
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
    return buffer;
}

std::string hash_hex_plain(Hash128 value) { return hex_u64(value.hi) + hex_u64(value.lo); }

bool parse_hex_u64(const std::string &text, size_t offset, uint64_t &value) {
    if (text.size() < offset + 16) return false;
    uint64_t result = 0;
    for (size_t i = 0; i < 16; ++i) {
        const char c = text[offset + i];
        uint32_t digit = 0;
        if (c >= '0' && c <= '9')
            digit = static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = static_cast<uint32_t>(c - 'A' + 10);
        else
            return false;
        result = (result << 4) | digit;
    }
    value = result;
    return true;
}

bool parse_hash_hex(const std::string &text, Hash128 &value) {
    if (text.size() != 32) return false;
    return parse_hex_u64(text, 0, value.hi) && parse_hex_u64(text, 16, value.lo);
}

// Mirror of Core's spelling normalisation: drop cv/elaborated qualifiers,
// whitespace and pointer/reference decoration so `const int *` and `int`-based
// spellings canonicalise consistently.
std::string normalise_spelling(const std::string &spelling) {
    static const char * const qualifiers[] = {"const", "volatile", "struct", "class", "enum", "union"};
    std::string out;
    std::string token;
    const auto flush = [&]() {
        if (token.empty()) return;
        bool qualifier = false;
        for (const char *q : qualifiers)
            if (token == q) {
                qualifier = true;
                break;
            }
        if (!qualifier) out += token;
        token.clear();
    };
    for (const char c : spelling) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == '*' || c == '&') {
            flush();
            continue;
        }
        token += c;
    }
    flush();
    return out;
}

bool valid_artifact_name(const std::string &name) {
    if (name.empty() || name == "." || name == "..") return false;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return false;
    return true;
}

bool read_file(const fs::path &path, std::string &out) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    out.assign((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

bool write_file(const fs::path &path, const std::string &content, std::string &error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot write: " + path.string();
        return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    if (!output) {
        error = "cannot write: " + path.string();
        return false;
    }
    return true;
}

uint64_t next_sequence() {
    static std::atomic<uint64_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

struct Manifest {
    uint32_t schema_version = instantiation_schema_version;
    uint32_t protocol_version = instantiation_protocol_version;
    Hash128 build_key{};
    InstantiationRequest request;
    Hash128 template_signature{};
    Hash128 request_id{};
    Hash128 source_digest{};
    Hash128 toolchain{};
    Hash128 build_options{};
    std::vector<CacheArtifact> artifacts;
    std::vector<CacheDependency> dependencies;
};

std::string encode_hex(const std::string &value) {
    static const char *digits = "0123456789abcdef";
    std::string out;
    out.reserve(value.size() * 2);
    for (const unsigned char c : value) {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 0xf]);
    }
    return out;
}

bool decode_hex(const std::string &value, std::string &out) {
    if (value.size() % 2 != 0) return false;
    out.clear();
    out.reserve(value.size() / 2);
    for (size_t i = 0; i < value.size(); i += 2) {
        auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hi = digit(value[i]);
        const int lo = digit(value[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return true;
}

bool parse_u32_text(const std::string &text, uint32_t &value) {
    if (text.empty()) return false;
    uint64_t result = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        result = result * 10 + static_cast<uint64_t>(c - '0');
        if (result > UINT32_MAX) return false;
    }
    value = static_cast<uint32_t>(result);
    return true;
}

// A deterministic, self-contained manifest. The bundled JSON printer/parser is
// not round-trip safe here, so the cache owns its format: one `key=value` per
// line, with free-form strings hex-encoded.
std::string manifest_text(const Manifest &manifest) {
    std::string out = kManifestSchema;
    out += "\n";
    out += "schema_version=" + std::to_string(manifest.schema_version) + "\n";
    out += "protocol_version=" + std::to_string(manifest.protocol_version) + "\n";
    out += "build_key=" + hash_hex_plain(manifest.build_key) + "\n";
    out += "source_digest=" + hash_hex_plain(manifest.source_digest) + "\n";
    out += "toolchain=" + hash_hex_plain(manifest.toolchain) + "\n";
    out += "build_options=" + hash_hex_plain(manifest.build_options) + "\n";
    out += "template=" + encode_hex(manifest.request.template_name) + "\n";
    for (const auto &argument : manifest.request.arguments)
        out += "arg=" + encode_hex(argument) + "\n";
    out += "arch=" + std::to_string(manifest.request.arch) + "\n";
    out += "os=" + std::to_string(manifest.request.os) + "\n";
    out += "target_abi=" + std::to_string(manifest.request.target_abi) + "\n";
    out += "pointer_bits=" + std::to_string(manifest.request.pointer_bits) + "\n";
    out += "contract=" + encode_hex(manifest.request.contract) + "\n";
    for (const auto &artifact : manifest.artifacts)
        out += "artifact=" + encode_hex(artifact.name) + ":" + hash_hex_plain(artifact.digest) + "\n";
    for (const auto &dependency : manifest.dependencies)
        out += "dependency=" + encode_hex(dependency.path) + ":" + hash_hex_plain(dependency.digest) + "\n";
    return out;
}

bool manifest_parse(const std::string &text, Manifest &manifest, std::string &error) {
    size_t line_begin = 0;
    bool first = true;
    bool have_template = false;
    while (line_begin <= text.size()) {
        const size_t line_end = text.find('\n', line_begin);
        const std::string line =
            text.substr(line_begin, line_end == std::string::npos ? std::string::npos : line_end - line_begin);
        if (first) {
            if (line != kManifestSchema) {
                error = "manifest schema is missing or unknown";
                return false;
            }
            first = false;
        } else if (!line.empty()) {
            const size_t separator = line.find('=');
            if (separator == std::string::npos) {
                error = "manifest line is malformed";
                return false;
            }
            const std::string key = line.substr(0, separator);
            const std::string value = line.substr(separator + 1);
            if (key == "schema_version") {
                if (!parse_u32_text(value, manifest.schema_version)) return false;
            } else if (key == "protocol_version") {
                if (!parse_u32_text(value, manifest.protocol_version)) return false;
            } else if (key == "build_key") {
                if (!parse_hash_hex(value, manifest.build_key)) return false;
            } else if (key == "source_digest") {
                if (!parse_hash_hex(value, manifest.source_digest)) return false;
            } else if (key == "toolchain") {
                if (!parse_hash_hex(value, manifest.toolchain)) return false;
            } else if (key == "build_options") {
                if (!parse_hash_hex(value, manifest.build_options)) return false;
            } else if (key == "template") {
                if (!decode_hex(value, manifest.request.template_name)) return false;
                have_template = true;
            } else if (key == "arg") {
                std::string argument;
                if (!decode_hex(value, argument)) return false;
                manifest.request.arguments.push_back(std::move(argument));
            } else if (key == "arch") {
                if (!parse_u32_text(value, manifest.request.arch)) return false;
            } else if (key == "os") {
                if (!parse_u32_text(value, manifest.request.os)) return false;
            } else if (key == "target_abi") {
                if (!parse_u32_text(value, manifest.request.target_abi)) return false;
            } else if (key == "pointer_bits") {
                if (!parse_u32_text(value, manifest.request.pointer_bits)) return false;
            } else if (key == "contract") {
                if (!decode_hex(value, manifest.request.contract)) return false;
            } else if (key == "artifact") {
                const size_t colon = value.find(':');
                if (colon == std::string::npos) {
                    error = "manifest artifact is malformed";
                    return false;
                }
                CacheArtifact artifact;
                if (!decode_hex(value.substr(0, colon), artifact.name)
                    || !valid_artifact_name(artifact.name)
                    || !parse_hash_hex(value.substr(colon + 1), artifact.digest)) {
                    error = "manifest artifact is malformed";
                    return false;
                }
                manifest.artifacts.push_back(std::move(artifact));
            } else if (key == "dependency") {
                const size_t colon = value.find(':');
                if (colon == std::string::npos) {
                    error = "manifest dependency is malformed";
                    return false;
                }
                CacheDependency dependency;
                if (!decode_hex(value.substr(0, colon), dependency.path)
                    || dependency.path.empty()
                    || !parse_hash_hex(value.substr(colon + 1), dependency.digest)) {
                    error = "manifest dependency is malformed";
                    return false;
                }
                manifest.dependencies.push_back(std::move(dependency));
            }
        }
        if (line_end == std::string::npos) break;
        line_begin = line_end + 1;
    }
    if (!have_template) {
        error = "manifest request is malformed";
        return false;
    }
    // Replay fields are derived, never trusted from disk.
    manifest.template_signature = instantiation_template_signature(manifest.request);
    manifest.request_id = instantiation_request_id(manifest.request);
    return true;
}

// A dependency is still reusable while its current content hashes to the
// recorded digest. A missing file counts as changed.
bool dependency_intact(const CacheDependency &dependency) {
    std::string content;
    if (!read_file(fs::path(dependency.path), content)) return false;
    return digest_bytes(content.data(), content.size(), digest_domain_dependency) == dependency.digest;
}

bool same_dependencies(const std::vector<CacheDependency> &a, const std::vector<CacheDependency> &b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].path != b[i].path || !(a[i].digest == b[i].digest)) return false;
    return true;
}

bool load_manifest(const fs::path &entry, Manifest &manifest, std::string &error) {
    std::string text;
    if (!read_file(entry / kManifestName, text)) {
        error = "manifest is missing";
        return false;
    }
    return manifest_parse(text, manifest, error);
}

// Verify one committed entry against its manifest: schema/protocol versions and
// every artifact digest. Returns false with a reason on any mismatch.
bool verify_entry_records(const fs::path &entry, const Manifest &manifest, std::string &error) {
    if (manifest.schema_version != instantiation_schema_version) {
        error = "artifact schema version mismatch";
        return false;
    }
    if (manifest.protocol_version != instantiation_protocol_version) {
        error = "instantiation protocol version mismatch";
        return false;
    }
    for (const auto &artifact : manifest.artifacts) {
        std::string content;
        if (!read_file(entry / artifact.name, content)) {
            error = "artifact is missing: " + artifact.name;
            return false;
        }
        if (!(digest_bytes(content.data(), content.size(), digest_domain_artifact) == artifact.digest)) {
            error = "artifact digest mismatch: " + artifact.name;
            return false;
        }
    }
    return true;
}

}   // namespace

std::string normalize_instantiation_argument(const std::string &spelling) { return normalise_spelling(spelling); }

Hash128 instantiation_request_id(const InstantiationRequest &request) {
    std::string key = "ABIX-INSTANTIATION-REQUEST\x1f" + request.template_name;
    for (const auto &argument : request.arguments)
        key += "\x1f" + normalize_instantiation_argument(argument);
    key += "\x1f@";
    key += std::to_string(request.arch);
    key += ":";
    key += std::to_string(request.os);
    key += ":";
    key += std::to_string(request.target_abi);
    key += ":";
    key += std::to_string(request.pointer_bits);
    key += "\x1f" + request.contract;
    return hash_text(key, kRequestDomain);
}

Hash128 instantiation_template_signature(const InstantiationRequest &request) {
    std::string key = "ABIX-INSTANTIATION-TEMPLATE\x1f" + request.template_name;
    for (const auto &argument : request.arguments)
        key += "\x1f" + normalize_instantiation_argument(argument);
    key += "\x1f" + request.contract;
    return hash_text(key, kTemplateDomain);
}

Hash128 instantiation_build_key(const InstantiationBuildInputs &inputs) {
    const Hash128 request_id = instantiation_request_id(inputs.request);
    std::string key = "ABIX-INSTANTIATION-BUILDKEY\x1f";
    key += std::to_string(request_id.lo) + ":" + std::to_string(request_id.hi) + "\x1f";
    key += std::to_string(inputs.source_digest.lo) + ":" + std::to_string(inputs.source_digest.hi) + "\x1f";
    key += std::to_string(inputs.toolchain.lo) + ":" + std::to_string(inputs.toolchain.hi) + "\x1f";
    key += std::to_string(inputs.build_options.lo) + ":" + std::to_string(inputs.build_options.hi) + "\x1f";
    key += std::to_string(inputs.protocol_version) + ":" + std::to_string(inputs.schema_version);
    return hash_text(key, digest_domain_build_key);
}

Hash128 digest_bytes(const void *data, size_t size, uint64_t domain) {
    return hash_text(std::string_view(static_cast<const char *>(data), size), domain);
}

Hash128 digest_text(const std::string &text, uint64_t domain) { return hash_text(text, domain); }

const char *cache_status_name(CacheStatus status) {
    switch (status) {
        case CacheStatus::hit:              return "hit";
        case CacheStatus::miss:             return "miss";
        case CacheStatus::corrupted:        return "corrupted";
        case CacheStatus::invalid_manifest: return "invalid_manifest";
        case CacheStatus::schema_mismatch:  return "schema_mismatch";
    }
    return "unknown";
}

InstantiationCache::InstantiationCache(std::string root)
    : _root(std::move(root)) {}

std::string InstantiationCache::entry_directory(const Hash128 &build_key) const {
    return (fs::path(_root) / hash_hex_plain(build_key)).string();
}

bool InstantiationCache::store(const InstantiationBuildInputs &inputs,
    const std::vector<std::pair<std::string, std::string>> &artifacts, std::string &error) {
    return store(inputs, artifacts, {}, error);
}

bool InstantiationCache::store(const InstantiationBuildInputs &inputs,
    const std::vector<std::pair<std::string, std::string>> &artifacts, const std::vector<CacheDependency> &dependencies,
    std::string &error) {
    error.clear();
    for (const auto &artifact : artifacts)
        if (!valid_artifact_name(artifact.first)) {
            error = "unsafe artifact name: " + artifact.first;
            return false;
        }
    Manifest manifest;
    manifest.schema_version = inputs.schema_version;
    manifest.protocol_version = inputs.protocol_version;
    manifest.build_key = instantiation_build_key(inputs);
    manifest.request = inputs.request;
    manifest.template_signature = instantiation_template_signature(inputs.request);
    manifest.request_id = instantiation_request_id(inputs.request);
    manifest.source_digest = inputs.source_digest;
    manifest.toolchain = inputs.toolchain;
    manifest.build_options = inputs.build_options;
    manifest.dependencies = dependencies;

    std::error_code ec;
    fs::create_directories(_root, ec);
    if (ec) {
        error = "cannot create cache root: " + ec.message();
        return false;
    }
    const fs::path entry = entry_directory(manifest.build_key);

    // A concurrent writer may already have committed a valid entry. Trust it and
    // become a no-op; a corrupt entry is replaced below.
    if (fs::exists(entry, ec)) {
        Manifest existing;
        // Only a no-op when the entry is valid AND records the same dependency
        // closure. The key is project-level (it does not include file
        // contents), so a source change produces the same key but a different
        // closure, which must replace the stale entry rather than be ignored.
        if (load_manifest(entry, existing, error)
            && verify_entry_records(entry, existing, error)
            && existing.build_key == manifest.build_key
            && same_dependencies(existing.dependencies, manifest.dependencies))
            return true;
        fs::remove_all(entry, ec);
    }

    // Write to a unique temporary directory, then commit with a rename.
    const std::string suffix = hash_hex_plain(manifest.build_key)
                             + "-"
                             + std::to_string(current_process_id())
                             + "-"
                             + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                             + "-"
                             + std::to_string(next_sequence());
    const fs::path temporary = fs::path(_root) / (".tmp-" + suffix);
    fs::remove_all(temporary, ec);
    fs::create_directories(temporary, ec);
    if (ec) {
        error = "cannot create cache entry: " + ec.message();
        return false;
    }
    for (const auto &artifact : artifacts) {
        CacheArtifact record;
        record.name = artifact.first;
        record.digest = digest_bytes(artifact.second.data(), artifact.second.size(), digest_domain_artifact);
        if (!write_file(temporary / record.name, artifact.second, error)) {
            fs::remove_all(temporary, ec);
            return false;
        }
        manifest.artifacts.push_back(std::move(record));
    }
    const std::string manifest_body = manifest_text(manifest);
    if (!write_file(temporary / kManifestName, manifest_body, error)) {
        fs::remove_all(temporary, ec);
        return false;
    }
    // Verify before publishing: a failed write never becomes visible.
    if (!verify_entry_records(temporary, manifest, error)) {
        fs::remove_all(temporary, ec);
        return false;
    }
    fs::rename(temporary, entry, ec);
    if (ec) {
        // Another writer may have won the race between the check and the rename.
        Manifest winner;
        if (load_manifest(entry, winner, error)
            && verify_entry_records(entry, winner, error)
            && winner.build_key == manifest.build_key
            && same_dependencies(winner.dependencies, manifest.dependencies)) {
            fs::remove_all(temporary, ec);
            return true;
        }
        fs::remove_all(entry, ec);
        fs::rename(temporary, entry, ec);
        if (ec) {
            fs::remove_all(temporary, ec);
            error = "cannot commit cache entry: " + ec.message();
            return false;
        }
    }
    return true;
}

CacheLookup InstantiationCache::lookup(const InstantiationBuildInputs &inputs) const {
    CacheLookup result;
    result.build_key = instantiation_build_key(inputs);
    result.directory = entry_directory(result.build_key);
    const fs::path entry = result.directory;
    std::error_code ec;
    if (!fs::exists(entry, ec)) {
        result.status = CacheStatus::miss;
        result.reason = cache_reason_not_found;
    } else {
        Manifest manifest;
        std::string error;
        if (!load_manifest(entry, manifest, error)) {
            result.status = CacheStatus::invalid_manifest;
            result.reason = cache_reason_invalid_manifest;
        } else if (manifest.schema_version != instantiation_schema_version
                   || manifest.protocol_version != instantiation_protocol_version) {
            result.status = CacheStatus::schema_mismatch;
            result.reason = manifest.schema_version != instantiation_schema_version
                              ? cache_reason_schema_version_changed
                              : cache_reason_protocol_version_changed;
        } else if (!verify_entry_records(entry, manifest, error)) {
            result.status = CacheStatus::corrupted;
            result.reason = cache_reason_corrupted_artifact;
        } else {
            // The entry is only reusable while every recorded dependency still
            // hashes the same. This is what makes header changes invalidate
            // without the cache key having to list them.
            bool dependencies_ok = true;
            for (const auto &dependency : manifest.dependencies)
                if (!dependency_intact(dependency)) {
                    dependencies_ok = false;
                    break;
                }
            if (!dependencies_ok) {
                result.status = CacheStatus::miss;
                result.reason = cache_reason_dependency_changed;
                return result;
            }
            result.status = CacheStatus::hit;
            result.reason = cache_reason_hit;
            result.artifacts = manifest.artifacts;
            result.dependencies = manifest.dependencies;
            return result;
        }
    }
    // Diagnose a miss against any same-template entry so incremental builds can
    // report why they rebuilt.
    const Hash128 desired_template = instantiation_template_signature(inputs.request);
    const Hash128 desired_request = instantiation_request_id(inputs.request);
    for (const auto &directory : fs::directory_iterator(fs::path(_root), ec)) {
        if (!directory.is_directory(ec)) continue;
        const std::string name = directory.path().filename().string();
        if (name.rfind(".tmp-", 0) == 0) continue;
        Manifest other;
        std::string ignored;
        if (!load_manifest(directory.path(), other, ignored)) continue;
        if (!(other.template_signature == desired_template)) continue;
        if (!(other.request_id == desired_request)) {
            result.reason = cache_reason_target_changed;
            break;
        }
        if (other.protocol_version != inputs.protocol_version) {
            result.reason = cache_reason_protocol_version_changed;
        } else if (other.schema_version != inputs.schema_version) {
            result.reason = cache_reason_schema_version_changed;
        } else if (!(other.source_digest == inputs.source_digest)) {
            result.reason = cache_reason_source_digest_changed;
        } else if (!(other.toolchain == inputs.toolchain)) {
            result.reason = cache_reason_toolchain_changed;
        } else if (!(other.build_options == inputs.build_options)) {
            result.reason = cache_reason_build_options_changed;
        } else {
            continue;
        }
        break;
    }
    return result;
}

bool InstantiationCache::verify(const Hash128 &build_key, std::string &error) const {
    Manifest manifest;
    const fs::path entry = entry_directory(build_key);
    if (!load_manifest(entry, manifest, error)) return false;
    return verify_entry_records(entry, manifest, error);
}

size_t InstantiationCache::verify_all(std::vector<std::string> &failures) const {
    std::error_code ec;
    size_t checked = 0;
    for (const auto &directory : fs::directory_iterator(fs::path(_root), ec)) {
        if (!directory.is_directory(ec)) continue;
        const std::string name = directory.path().filename().string();
        if (name.rfind(".tmp-", 0) == 0) continue;
        if (!fs::exists(directory.path() / kManifestName, ec)) continue;
        ++checked;
        Manifest manifest;
        std::string error;
        if (!load_manifest(directory.path(), manifest, error)
            || !verify_entry_records(directory.path(), manifest, error))
            failures.push_back(name + ": " + error);
    }
    return checked;
}

size_t InstantiationCache::entries() const {
    std::error_code ec;
    size_t count = 0;
    for (const auto &directory : fs::directory_iterator(fs::path(_root), ec)) {
        if (!directory.is_directory(ec)) continue;
        const std::string name = directory.path().filename().string();
        if (name.rfind(".tmp-", 0) == 0) continue;
        if (fs::exists(directory.path() / kManifestName, ec)) ++count;
    }
    return count;
}

size_t InstantiationCache::clear() const {
    std::error_code ec;
    std::vector<fs::path> remove;
    for (const auto &directory : fs::directory_iterator(fs::path(_root), ec)) {
        if (directory.is_directory(ec)) remove.push_back(directory.path());
    }
    for (const auto &path : remove)
        fs::remove_all(path, ec);
    return remove.size();
}

}   // namespace amc
