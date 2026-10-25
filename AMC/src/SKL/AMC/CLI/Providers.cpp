#include "SKL/AMC/CLI/CLI.h"

#include <stdint.h>
#include <stdlib.h>
#include <system_error>

#include <fmt/format.h>

namespace amc::cli {
namespace {

fs::path manifest_path(const fs::path &executable_dir) {
    if (const char *override_path = std::getenv("AMC_PROVIDERS_FILE");
        override_path != nullptr && *override_path != '\0')
        return fs::path(override_path);
    const fs::path beside_executable = executable_dir / "amc-providers.toml";
    std::error_code ec;
    if (fs::is_regular_file(beside_executable, ec)) return beside_executable;
#ifdef AMC_DEFAULT_PROVIDERS_FILE
    const fs::path source_tree = AMC_DEFAULT_PROVIDERS_FILE;
    if (fs::is_regular_file(source_tree, ec)) return source_tree;
#endif
    return beside_executable;
}

bool read_string_array(const toml::table &table, const char *key, std::vector<std::string> &out, std::string &error) {
    out.clear();
    const auto *entries = table[key].as_array();
    if (entries == nullptr) return true;
    for (const auto &entry : *entries) {
        const auto value = entry.value<std::string>();
        if (!value || value->empty()) {
            error = fmt::format("`{}` must contain non-empty strings", key);
            return false;
        }
        out.push_back(*value);
    }
    return true;
}

}   // namespace

bool load_provider_manifest(const fs::path &executable_dir, ProviderManifest &manifest, std::string &error) {
    manifest = {};
    const fs::path path = manifest_path(executable_dir);
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        error = "provider manifest not found: " + path.string();
        return false;
    }

    try {
        const toml::table table = toml::parse_file(path.string());
        const auto version = table["version"].value<int64_t>();
        if (!version || *version != 1) {
            error = fmt::format("provider manifest {}: unsupported or missing version (expected 1)", path.string());
            return false;
        }
        manifest.version = static_cast<uint32_t>(*version);

        const auto *entries = table["provider"].as_array();
        if (entries == nullptr || entries->empty()) {
            error = fmt::format("provider manifest {}: at least one [[provider]] is required", path.string());
            return false;
        }

        for (const auto &entry : *entries) {
            const auto *spec = entry.as_table();
            if (spec == nullptr) {
                error = fmt::format("provider manifest {}: every [[provider]] must be a table", path.string());
                return false;
            }

            ProviderManifestEntry provider;
            const auto language = (*spec)["language"].value<std::string>();
            if (!language || language->empty()) {
                error = fmt::format("provider manifest {}: [[provider]] requires `language`", path.string());
                return false;
            }
            provider.language = *language;

            if (const auto executable = (*spec)["executable"].value<std::string>()) provider.executable = *executable;
            if (const auto protocol = (*spec)["protocol"].value<std::string>()) provider.protocol = *protocol;
            if (const auto contract = (*spec)["contract"].value<std::string>()) provider.contract = *contract;

            if (!read_string_array(*spec, "roles", provider.roles, error)
                || !read_string_array(*spec, "features", provider.features, error)) {
                error = fmt::format("provider manifest {}: {}", path.string(), error);
                return false;
            }
            if (provider.roles.empty()) {
                error = fmt::format(
                    "provider manifest {}: provider `{}` requires at least one role", path.string(), provider.language);
                return false;
            }
            for (size_t i = 0; i < provider.roles.size(); ++i) {
                const auto &role = provider.roles[i];
                if (role != "frontend" && role != "backend") {
                    error = fmt::format("provider manifest {}: provider `{}` has unknown role `{}`", path.string(),
                        provider.language, role);
                    return false;
                }
                for (size_t j = 0; j < i; ++j) {
                    if (provider.roles[j] == role) {
                        error = fmt::format("provider manifest {}: provider `{}` repeats role `{}`", path.string(),
                            provider.language, role);
                        return false;
                    }
                }
            }
            if (provider.protocol != "jsonl-v1" && provider.protocol != "direct") {
                error = fmt::format("provider manifest {}: provider `{}` has unknown protocol `{}`", path.string(),
                    provider.language, provider.protocol);
                return false;
            }
            if (provider.protocol.empty()) {
                error = fmt::format(
                    "provider manifest {}: provider `{}` requires `protocol`", path.string(), provider.language);
                return false;
            }
            if (provider.protocol == "jsonl-v1" && provider.executable.empty()) {
                error = fmt::format("provider manifest {}: provider `{}` speaks jsonl-v1 but has no `executable`",
                    path.string(), provider.language);
                return false;
            }
            if (find_provider_manifest(manifest, provider.language) != nullptr) {
                error = fmt::format("provider manifest {}: duplicate language `{}`", path.string(), provider.language);
                return false;
            }
            manifest.providers.push_back(std::move(provider));
        }
    } catch (const toml::parse_error &e) {
        error = fmt::format("provider manifest {}: {}", path.string(), e.description());
        return false;
    }

    return true;
}

const ProviderManifestEntry *find_provider_manifest(const ProviderManifest &manifest, const std::string &language) {
    for (const auto &provider : manifest.providers)
        if (provider.language == language) return &provider;
    return nullptr;
}

std::string describe_provider(const ProviderManifestEntry &provider) {
    std::string line = provider.language;
    for (const auto &role : provider.roles)
        line += " " + role;
    line += " protocol=" + provider.protocol;
    if (!provider.contract.empty()) line += " contract=" + provider.contract;
    return line;
}

}   // namespace amc::cli
