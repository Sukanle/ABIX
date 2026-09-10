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
    function
};
struct Type {
    std::string name;
    TypeKind kind = TypeKind::primitive;
    Hash128 id{};
    uint32_t size = 0, align = 0, flags = 0;
    uint32_t field_begin = 0, field_count = 0;
    uint32_t array_count = 0;
};
struct Field {
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
    std::string name;
    Hash128 signature{};
    Hash128 return_type{};
    std::vector<Parameter> parameters;
    uint32_t calling_convention = 0, flags = 0;
};
struct AbiModule {
    std::string package_name, package_version;
    uint32_t arch = 0, os = 0, compiler = 1, calling_convention = 0;
    std::vector<Type> types;
    std::vector<Field> fields;
    std::vector<Function> functions;
};

Hash128 hash_text(std::string_view text, uint64_t domain);
Hash128 layout_hash(const Type &, const std::vector<Field> &);
Hash128 signature_hash(const Function &);
bool validate(const AbiModule &, std::string &error);
bool project_symbols(const AbiModule &, const std::vector<std::string> &symbols,
                     AbiModule &, std::string &error);

bool write_abix(const AbiModule &, const std::string &path, std::string &error);
bool read_abix(const std::string &path, AbiModule &, std::string &error);
std::string type_kind_name(TypeKind);

}   // namespace amc
#endif
