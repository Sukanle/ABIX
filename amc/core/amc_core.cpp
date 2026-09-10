#include "amc_core.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <unordered_set>

namespace amc {
bool operator==(Hash128 a, Hash128 b) { return a.lo == b.lo && a.hi == b.hi; }
Hash128 hash_text(std::string_view s, uint64_t domain) {
    uint64_t a = 1'469'598'103'934'665'603ULL ^ domain, b = 1'099'511'628'211ULL + domain;
    for (unsigned char c : s) {
        a = (a ^ c) * 1'099'511'628'211ULL;
        b = (b ^ (c + 31)) * 14'029'467'366'897'019'727ULL;
    }
    return {a, b};
}
static void put(std::vector<uint8_t> &b, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(uint8_t(v >> (i * 8)));
}
static void put64(std::vector<uint8_t> &b, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        b.push_back(uint8_t(v >> (i * 8)));
}
static uint32_t get(const std::vector<uint8_t> &b, size_t &p, std::string &e) {
    if (p + 4 > b.size()) {
        e = "truncated u32";
        return 0;
    }
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= uint32_t(b[p++]) << (i * 8);
    return v;
}
static uint64_t get64(const std::vector<uint8_t> &b, size_t &p, std::string &e) {
    if (p + 8 > b.size()) {
        e = "truncated u64";
        return 0;
    }
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= uint64_t(b[p++]) << (i * 8);
    return v;
}
static void putstr(std::vector<uint8_t> &b, std::string_view s) {
    put(b, uint32_t(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}
static std::string getstr(const std::vector<uint8_t> &b, size_t &p, std::string &e) {
    auto n = get(b, p, e);
    if (!e.empty() || p + n > b.size()) {
        if (e.empty()) e = "truncated string";
        return {};
    }
    std::string s(reinterpret_cast<const char *>(b.data() + p), n);
    p += n;
    return s;
}
Hash128 layout_hash(const Type &t, const std::vector<Field> &fs) {
    std::string x = t.name + ":" + std::to_string(t.size) + ":" + std::to_string(t.align);
    for (auto &f : fs)
        x += f.name + std::to_string(f.offset) + std::to_string(f.type_id.lo);
    return hash_text(x, 0x4c41594f5554ULL);
}
Hash128 signature_hash(const Function &f) {
    std::string x = f.name + std::to_string(f.return_type.lo) + std::to_string(f.return_type.hi);
    for (auto &p : f.parameters)
        x += std::to_string(p.type_id.lo) + std::to_string(p.type_id.hi);
    return hash_text(x, 0x5349474e41545552ULL);
}
bool validate(const AbiModule &m, std::string &e) {
    for (auto &t : m.types) {
        if (t.name.empty() || t.size == 0 || t.align == 0) {
            e = "invalid type: " + t.name;
            return false;
        }
        if (t.id == Hash128{}) {
            e = "type has no id: " + t.name;
            return false;
        }
    }
    for (auto &f : m.fields) {
        auto it = std::find_if(m.types.begin(), m.types.end(), [&](auto &t) { return t.id == f.type_id; });
        if (it == m.types.end()) {
            e = "field references unknown type: " + f.name;
            return false;
        }
    }
    for (auto &fn : m.functions) {
        if (fn.name.empty()) {
            e = "function has no name";
            return false;
        }
        auto ret = std::find_if(m.types.begin(), m.types.end(), [&](auto &t) { return t.id == fn.return_type; });
        if (ret == m.types.end()) {
            e = "function references unknown return type: " + fn.name;
            return false;
        }
        for (auto &p : fn.parameters) {
            auto it = std::find_if(m.types.begin(), m.types.end(), [&](auto &t) { return t.id == p.type_id; });
            if (it == m.types.end()) {
                e = "parameter references unknown type";
                return false;
            }
        }
    }
    return true;
}
bool project_symbols(const AbiModule &source, const std::vector<std::string> &symbols,
                     AbiModule &result, std::string &e) {
    if (!validate(source, e)) return false;

    auto matches = [](const std::string &name, const std::string &symbol) {
        if (name == symbol) return true;
        const auto pos = name.rfind("::");
        return pos != std::string::npos && name.substr(pos + 2) == symbol;
    };
    const bool all = symbols.empty();
    std::unordered_set<std::string> type_ids;
    auto id_key = [](Hash128 id) {
        return std::to_string(id.lo) + ":" + std::to_string(id.hi);
    };
    auto add_id = [&](Hash128 id) {
        type_ids.insert(id_key(id));
    };
    auto has_id = [&](Hash128 id) {
        return type_ids.count(id_key(id)) != 0;
    };
    auto selected = [&](const std::string &name) {
        return all || std::any_of(symbols.begin(), symbols.end(),
                                  [&](const std::string &s) { return matches(name, s); });
    };

    result = source;
    result.types.clear();
    result.fields.clear();
    result.functions.clear();
    for (const auto &type : source.types)
        if (selected(type.name)) add_id(type.id);
    for (const auto &function : source.functions) {
        if (!selected(function.name)) continue;
        result.functions.push_back(function);
        add_id(function.return_type);
        for (const auto &parameter : function.parameters) add_id(parameter.type_id);
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto &type : source.types) {
            if (!has_id(type.id)) continue;
            for (uint32_t i = 0; i < type.field_count; ++i) {
                const auto &field = source.fields[type.field_begin + i];
                if (!has_id(field.type_id)) {
                    add_id(field.type_id);
                    changed = true;
                }
            }
        }
    }
    for (const auto &type : source.types) {
        if (!has_id(type.id)) continue;
        Type projected = type;
        projected.field_begin = static_cast<uint32_t>(result.fields.size());
        projected.field_count = 0;
        for (uint32_t i = 0; i < type.field_count; ++i) {
            result.fields.push_back(source.fields[type.field_begin + i]);
            ++projected.field_count;
        }
        result.types.push_back(std::move(projected));
    }
    if (!all && result.types.empty() && result.functions.empty()) {
        e = "export symbols did not match any ABI declaration";
        return false;
    }
    return validate(result, e);
}
std::string type_kind_name(TypeKind k) {
    switch (k) {
        case TypeKind::primitive:   return "primitive";
        case TypeKind::enumeration: return "enum";
        case TypeKind::record:      return "record";
        case TypeKind::pointer:     return "pointer";
        case TypeKind::array:       return "array";
        case TypeKind::function:    return "function";
    }
    return "unknown";
}

bool write_abix(const AbiModule &m, const std::string &path, std::string &e) {
    if (!validate(m, e)) return false;
    std::vector<uint8_t> b = {'A', 'B', 'I', 'X', 1, 0, 0, 0};
    putstr(b, m.package_name);
    putstr(b, m.package_version);
    put(b, uint32_t(m.types.size()));
    put(b, uint32_t(m.fields.size()));
    put(b, uint32_t(m.functions.size()));
    for (auto &t : m.types) {
        putstr(b, t.name);
        put(b, uint32_t(t.kind));
        put64(b, t.id.lo);
        put64(b, t.id.hi);
        put(b, t.size);
        put(b, t.align);
        put(b, t.flags);
        put(b, t.array_count);
        put(b, t.field_begin);
        put(b, t.field_count);
    }
    for (auto &f : m.fields) {
        putstr(b, f.name);
        put64(b, f.type_id.lo);
        put64(b, f.type_id.hi);
        put(b, f.offset);
        put(b, f.flags);
    }
    for (auto &fn : m.functions) {
        putstr(b, fn.name);
        put64(b, fn.signature.lo);
        put64(b, fn.signature.hi);
        put64(b, fn.return_type.lo);
        put64(b, fn.return_type.hi);
        put(b, fn.calling_convention);
        put(b, fn.flags);
        put(b, uint32_t(fn.parameters.size()));
        for (auto &p : fn.parameters) {
            putstr(b, p.name);
            put64(b, p.type_id.lo);
            put64(b, p.type_id.hi);
            put(b, p.flags);
        }
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        e = "cannot open output: " + path;
        return false;
    }
    out.write(reinterpret_cast<const char *>(b.data()), b.size());
    return bool(out);
}
bool read_abix(const std::string &path, AbiModule &m, std::string &e) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        e = "cannot open input: " + path;
        return false;
    }
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), {});
    if (b.size() < 8 || std::memcmp(b.data(), "ABIX", 4) != 0 || b[4] != 1) {
        e = "invalid or unsupported ABIX header";
        return false;
    }
    size_t p = 8;
    m = {};
    m.package_name = getstr(b, p, e);
    m.package_version = getstr(b, p, e);
    auto nt = get(b, p, e), nf = get(b, p, e), nfn = get(b, p, e);
    if (!e.empty()) return false;
    for (uint32_t i = 0; i < nt; ++i) {
        Type t;
        t.name = getstr(b, p, e);
        t.kind = TypeKind(get(b, p, e));
        t.id = {get64(b, p, e), get64(b, p, e)};
        t.size = get(b, p, e);
        t.align = get(b, p, e);
        t.flags = get(b, p, e);
        t.array_count = get(b, p, e);
        t.field_begin = get(b, p, e);
        t.field_count = get(b, p, e);
        m.types.push_back(std::move(t));
    }
    for (uint32_t i = 0; i < nf; ++i) {
        Field f;
        f.name = getstr(b, p, e);
        f.type_id = {get64(b, p, e), get64(b, p, e)};
        f.offset = get(b, p, e);
        f.flags = get(b, p, e);
        m.fields.push_back(std::move(f));
    }
    for (uint32_t i = 0; i < nfn; ++i) {
        Function fn;
        fn.name = getstr(b, p, e);
        fn.signature = {get64(b, p, e), get64(b, p, e)};
        fn.return_type = {get64(b, p, e), get64(b, p, e)};
        fn.calling_convention = get(b, p, e);
        fn.flags = get(b, p, e);
        auto np = get(b, p, e);
        for (uint32_t j = 0; j < np; ++j) {
            Parameter q;
            q.name = getstr(b, p, e);
            q.type_id = {get64(b, p, e), get64(b, p, e)};
            q.flags = get(b, p, e);
            fn.parameters.push_back(std::move(q));
        }
        m.functions.push_back(std::move(fn));
    }
    return e.empty() && validate(m, e);
}
}   // namespace amc
