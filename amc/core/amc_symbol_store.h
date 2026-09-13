#ifndef AMC_SYMBOL_STORE_H
#define AMC_SYMBOL_STORE_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace amc {

// AI-PS: the ABIX Symbol Server (local cache).
//
// Artifacts are addressed by BuildID:
//   {root}/{build_id[0:2]}/{build_id[2:]}.abix      standalone artifact
//   {root}/{build_id[0:2]}/{build_id[2:]}.abixmeta  embedded Metadata Region
//   {root}/{build_id[0:2]}/{build_id[2:]}.{abix,abixmeta}.meta  sidecar
//
// For a compiled binary the BuildID is the GNU build id from
// `.note.gnu.build-id`, giving the plan's lookup chain
// `Binary -> BuildID -> .abix`.

// Reads the GNU build id from `.note.gnu.build-id`.
bool read_gnu_build_id(const uint8_t *data, size_t size, std::vector<uint8_t> &build_id, std::string &error);

std::string hex_encode(const uint8_t *data, size_t size);
bool hex_decode(std::string_view hex, std::vector<uint8_t> &bytes);

// Accepts an optional `0x` prefix and any even-length hex string; lowercases.
bool normalize_build_id(std::string_view text, std::string &normalized);

// Resolves `{root}/{key[0:2]}/{key[2:]}{suffix}`.
std::string symbol_store_path(const std::string &root, std::string_view key, const char *suffix);

// Writes `artifact` (and `meta`, when non-empty) into the store and creates the
// directory hierarchy. `stored_path` receives the artifact path.
bool publish_symbol(const std::string &root, std::string_view key, const std::vector<uint8_t> &artifact,
    const std::string &meta, const char *suffix, std::string &stored_path, std::string &error);

// Looks up `<key>.abix` first, then `<key>.abixmeta`.
bool fetch_symbol(const std::string &root, std::string_view key, std::vector<uint8_t> &artifact, std::string &path,
    std::string &error);

// `$ABIX_SYMBOL_STORE`, else `$HOME/.abix/symbols`.
std::string default_symbol_store_root();

}   // namespace amc

#endif
