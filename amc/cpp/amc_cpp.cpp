#include "../core/amc_core.h"
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecordLayout.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <toml++/toml.h>
#include <fstream>
#include <cctype>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <unordered_map>
#include <filesystem>
#include <string_view>

using namespace clang;
using namespace clang::tooling;

namespace {
struct Config {
    std::string db, output;
    std::vector<std::string> flags, files, symbols;
};
bool config_load(const std::string &path, Config &c, std::string &e) {
    try {
        auto t = toml::parse_file(path);
        auto *imp = t["import"].as_array();
        if (!imp || imp->empty()) {
            e = "configuration needs [[import]]";
            return false;
        }
        if (imp->size() != 1) {
            e = "MVP supports exactly one [[import]]";
            return false;
        }
        auto tab = (*imp)[0].as_table();
        if (!tab || tab->get("language")->value_or(std::string{}) != "cpp") {
            e = "only cpp import is supported";
            return false;
        }
        c.db = (*tab)["compile_commands"].value_or("");
        auto files = (*tab)["files"].as_array();
        auto symbols = (*tab)["symbols"].as_array();
        auto flags = (*tab)["flags"].as_array();
        if (flags)
            for (auto &&v : *flags) c.flags.push_back(v.value_or(""));
        if (files)
            for (auto &&v : *files)
                c.files.push_back(v.value_or(""));
        if (symbols)
            for (auto &&v : *symbols)
                c.symbols.push_back(v.value_or(""));
        if (auto ex = t["export"].as_array(); ex && !ex->empty()) {
            if (auto et = (*ex)[0].as_table()) c.output = (*et)["output"].value_or("");
        }
        if ((c.db.empty() && c.flags.empty()) || c.files.empty() || c.symbols.empty()) {
            e = "cpp import requires compile_commands or flags, files and symbols";
            return false;
        }
        return true;
    } catch (const toml::parse_error &x) {
        e = x.description();
        return false;
    }
}

class Extractor : public RecursiveASTVisitor<Extractor> {
public:
    Extractor(ASTContext &c, const std::set<std::string> &wanted, amc::AbiModule &m)
        : ctx(c)
        , wanted(wanted)
        , module(m) {}
    bool VisitRecordDecl(RecordDecl *d) {
        auto *cxx = dyn_cast<CXXRecordDecl>(d);
        if (!cxx || d->isImplicit() || !d->isThisDeclarationADefinition() ||
            (!selected(d) && !has_selected_member(cxx))) return true;
        add_record(cxx);
        return true;
    }
    bool VisitEnumDecl(EnumDecl *d) {
        if (d->isCompleteDefinition() && selected(d)) add_enum(d);
        return true;
    }
    bool VisitTypedefNameDecl(TypedefNameDecl *d) {
        if (selected(d)) add_alias(d);
        return true;
    }
    bool VisitDecl(Decl *d) {
        if (auto *fn = dyn_cast<FunctionDecl>(d)) {
            if (selected(fn)) add_function(fn);
        }
        return true;
    }

private:
    ASTContext &ctx;
    const std::set<std::string> &wanted;
    amc::AbiModule &module;
    std::unordered_map<std::string, amc::Hash128> ids;
    std::string name(const NamedDecl *d) const { return d->getQualifiedNameAsString(); }
    void add_namespaces(const NamedDecl *d) {
        const auto *context = d->getDeclContext();
        std::vector<std::string> names;
        while (context && !isa<TranslationUnitDecl>(context)) {
            if (const auto *named = dyn_cast<NamespaceDecl>(context)) {
                if (!named->isAnonymousNamespace()) names.push_back(named->getQualifiedNameAsString());
            }
            context = context->getParent();
        }
        for (auto it = names.rbegin(); it != names.rend(); ++it) {
            if (ids.count(*it) != 0) continue;
            amc::Type t;
            t.name = *it;
            t.kind = amc::TypeKind::namespace_type;
            t.id = amc::hash_text(t.name, 0x54595045);
            t.size = t.align = 1;
            ids.emplace(t.name, t.id);
            module.types.push_back(std::move(t));
        }
    }
    bool selected(const NamedDecl *d) const {
        auto n = name(d);
        return wanted.count(n) || wanted.count(d->getNameAsString());
    }
    bool has_selected_member(const CXXRecordDecl *d) const {
        const auto prefix = name(d) + "::";
        for (const auto &symbol : wanted)
            if (symbol.compare(0, prefix.size(), prefix) == 0) return true;
        return false;
    }
    amc::Hash128 add_type(QualType qt) {
        qt = qt.getCanonicalType();
        auto s = qt.getAsString();
        auto it = ids.find(s);
        if (it != ids.end()) return it->second;
        if (qt->isDependentType()) {
            amc::Type t;
            t.name = s;
            t.id = amc::hash_text(s, 0x54595045);
            t.size = t.align = 1;
            ids.emplace(s, t.id);
            module.types.push_back(std::move(t));
            return ids.at(s);
        }
        // A concrete template specialization has a stable instantiated layout,
        // but walking its primary template can expose dependent AST nodes.
        // Record the specialization as an opaque ABI type here; its concrete
        // fields are still available when Clang presents a complete record.
        if (qt->getAs<TemplateSpecializationType>()) {
            amc::Type t;
            t.name = s;
            t.kind = amc::TypeKind::record;
            t.id = amc::hash_text(s, 0x54595045);
            t.size = uint32_t(ctx.getTypeSize(qt) / 8);
            t.align = uint32_t(ctx.getTypeAlign(qt) / 8);
            ids.emplace(s, t.id);
            module.types.push_back(std::move(t));
            return ids.at(s);
        }
        if (const auto *record = qt->getAsCXXRecordDecl()) {
            const auto record_key = qt.getAsString();
            add_record(const_cast<CXXRecordDecl *>(record));
            auto found = ids.find(record_key);
            if (found != ids.end()) return found->second;
            found = ids.find(name(record));
            if (found != ids.end()) return found->second;
            return add_type(ctx.getCanonicalTagType(const_cast<CXXRecordDecl *>(record)));
        }
        if (const auto *enumeration = qt->getAs<EnumType>()) {
            add_enum(enumeration->getDecl());
            auto found = ids.find(name(enumeration->getDecl()));
            if (found != ids.end()) return found->second;
            return amc::hash_text(s, 0x54595045);
        }
        amc::Type t;
        t.name = s;
        t.id = amc::hash_text(s, 0x54595045);
        t.size = uint32_t(ctx.getTypeSize(qt) / 8);
        t.align = uint32_t(ctx.getTypeAlign(qt) / 8);
        t.kind = amc::TypeKind::primitive;
        if (const auto *p = qt->getAs<PointerType>()) {
            t.kind = amc::TypeKind::pointer;
            t.name = p->getPointeeType().getAsString() + "*";
            t.size = ctx.getTypeSize(qt) / 8;
            t.align = ctx.getTypeAlign(qt) / 8;
        }
        if (const auto *a = dyn_cast<ConstantArrayType>(qt.getTypePtr())) {
            t.kind = amc::TypeKind::array;
            t.array_count = uint32_t(a->getSize().getZExtValue());
        }
        ids[s] = t.id;
        module.types.push_back(t);
        return t.id;
    }
    static uint32_t access_flags(AccessSpecifier access) {
        switch (access) {
            case AS_public: return amc::visibility_flags(amc::Visibility::public_);
            case AS_protected: return amc::visibility_flags(amc::Visibility::protected_);
            case AS_private: return amc::visibility_flags(amc::Visibility::private_);
            case AS_none: return amc::visibility_flags(amc::Visibility::none);
        }
        return 0;
    }
    static uint32_t calling_convention(FunctionDecl *d) {
        const auto *prototype = d->getType()->getAs<FunctionProtoType>();
        if (!prototype) return 0;
        switch (prototype->getCallConv()) {
            case CC_C: return 1;
            case CC_X86StdCall: return 2;
            case CC_X86FastCall: return 3;
            case CC_X86ThisCall: return 4;
            default:
                return d->hasAttr<AArch64SVEPcsAttr>() ? 5 : 0;
        }
    }
    void add_record(CXXRecordDecl *d) {
        const auto record_name = name(d);
        if (ids.count(record_name) != 0) return;
        if (d->isDependentContext() || ctx.getCanonicalTagType(d)->isDependentType()) return;
        add_namespaces(d);
        amc::Type t;
        t.name = record_name;
        t.kind = amc::TypeKind::record;
        if (d->getDescribedClassTemplate()) t.flags |= amc::type_template_primary;
        t.id = amc::hash_text(t.name, 0x54595045);
        auto &l = ctx.getASTRecordLayout(d);
        auto qt = ctx.getCanonicalTagType(d);
        t.size = uint32_t(ctx.getTypeSize(qt) / 8);
        t.align = uint32_t(ctx.getTypeAlign(qt) / 8);
        t.field_begin = uint32_t(module.fields.size());
        ids[t.name] = t.id;
        module.types.push_back(t);
        const auto record_index = module.types.size() - 1;
        for (const auto &base : d->bases()) {
            amc::Field x;
            x.owner_type = t.id;
            x.name = base.getType().getAsString();
            x.type_id = add_type(base.getType());
            x.flags = amc::field_base | access_flags(base.getAccessSpecifier());
            module.fields.push_back(std::move(x));
        }
        for (auto *f : d->fields()) {
            // Anonymous implementation fields have no stable ABI name and
            // cannot participate in name-based compatibility or Map IR.
            if (f->getName().empty()) continue;
            amc::Field x;
            x.owner_type = t.id;
            x.name = f->getNameAsString();
            x.type_id = add_type(f->getType());
            const uint64_t bit_offset = l.getFieldOffset(f->getFieldIndex());
            x.offset = uint32_t(bit_offset / 8);
            x.flags = access_flags(f->getAccess());
            if (f->isBitField()) {
                x.flags |= amc::field_bitfield;
                x.flags |= uint32_t(bit_offset % 8) << amc::field_bit_offset_shift;
                x.flags |= f->getBitWidthValue() << amc::field_bit_width_shift;
            }
            module.fields.push_back(x);
        }
        module.types[record_index].field_count =
            uint32_t(module.fields.size()) - module.types[record_index].field_begin;
        std::vector<amc::Field> layout_fields;
        for (uint32_t i = 0; i < module.types[record_index].field_count; ++i)
            layout_fields.push_back(module.fields[module.types[record_index].field_begin + i]);
        module.types[record_index].layout_hash =
            amc::layout_hash(module.types[record_index], layout_fields);
    }
    void add_enum(EnumDecl *d) {
        add_namespaces(d);
        if (ids.count(name(d)) != 0) return;
        amc::Type t;
        t.name = name(d);
        t.kind = amc::TypeKind::enumeration;
        t.id = amc::hash_text(t.name, 0x54595045);
        t.size = uint32_t(ctx.getTypeSize(d->getIntegerType()) / 8);
        t.align = uint32_t(ctx.getTypeAlign(d->getIntegerType()) / 8);
        ids[t.name] = t.id;
        module.types.push_back(t);
    }
    void add_alias(TypedefNameDecl *d) {
        add_namespaces(d);
        const auto alias_name = name(d);
        if (ids.count(alias_name) != 0) return;
        amc::Type t;
        t.name = alias_name;
        t.kind = amc::TypeKind::alias;
        t.id = amc::hash_text(t.name, 0x54595045);
        t.size = uint32_t(ctx.getTypeSize(d->getUnderlyingType()) / 8);
        t.align = uint32_t(ctx.getTypeAlign(d->getUnderlyingType()) / 8);
        ids[t.name] = t.id;
        const auto underlying = add_type(d->getUnderlyingType());
        t.field_begin = uint32_t(module.fields.size());
        t.field_count = 1;
        module.types.push_back(t);
        module.fields.push_back({t.id, "underlying", underlying, 0, 0});
    }
    void add_function(FunctionDecl *d) {
        amc::Function f;
        if (const auto *method = dyn_cast<CXXMethodDecl>(d)) {
            if (const auto *owner = method->getParent()) {
                f.owner_type = amc::hash_text(name(owner), 0x54595045);
                if (ids.count(name(owner)) == 0) add_record(const_cast<CXXRecordDecl *>(owner));
            }
        }
        f.name = name(d);
        f.calling_convention = calling_convention(d);
        f.flags = access_flags(d->getAccess());
        f.return_type = add_type(d->getReturnType());
        for (auto *p : d->parameters()) {
            amc::Parameter q;
            q.name = p->getNameAsString();
            q.type_id = add_type(p->getType());
            f.parameters.push_back(q);
        }
        f.signature = amc::signature_hash(f);
        module.functions.push_back(std::move(f));
    }
};
class Consumer : public ASTConsumer {
public:
    Consumer(const std::set<std::string> &w, amc::AbiModule &m)
        : wanted(w)
        , module(m) {}
    void HandleTranslationUnit(ASTContext &c) override {
        Extractor x(c, wanted, module);
        x.TraverseDecl(c.getTranslationUnitDecl());
    }

private:
    const std::set<std::string> &wanted;
    amc::AbiModule &module;
};
class Action : public ASTFrontendAction {
public:
    Action(const std::set<std::string> &w, amc::AbiModule &m)
        : wanted(w)
        , module(m) {}
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, StringRef) override {
        return std::make_unique<Consumer>(wanted, module);
    }

private:
    const std::set<std::string> &wanted;
    amc::AbiModule &module;
};
class Factory : public FrontendActionFactory {
public:
    Factory(const std::set<std::string> &w, amc::AbiModule &m)
        : wanted(w)
        , module(m) {}
    std::unique_ptr<FrontendAction> create() override { return std::make_unique<Action>(wanted, module); }

private:
    const std::set<std::string> &wanted;
    amc::AbiModule &module;
};
}   // namespace

static std::string cpp_name(std::string s) {
    for (size_t p = 0; (p = s.find("::", p)) != std::string::npos; s.replace(p, 2, "_")) {}
    for (char &c : s)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') c = '_';
    return s;
}

static std::string cpp_string(const std::string &value) {
    std::string result = "\"";
    for (const char c : value) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else result += c;
    }
    result += '"';
    return result;
}

static std::string hex_u64(uint64_t value) {
    std::ostringstream output;
    output << "0x" << std::hex << value << "ULL";
    return output.str();
}

static int backend(const char *input, const char *output) {
    amc::AbiModule m;
    std::string e;
    if (!amc::read_abix(input, m, e)) {
        std::cerr << e << "\n";
        return 1;
    }
    std::ofstream o(output);
    if (!o) {
        std::cerr << "cannot open output: " << output << "\n";
        return 1;
    }
    o << "#pragma once\n"
         "#include <abix/runtime_registry.h>\n"
         "#include <cstddef>\n"
         "#include <cstdint>\n"
         "#include <cstring>\n"
         "namespace amc_generated {\n"
         "using TypeId = ::skl::abix::model::TypeId;\n"
         "using LayoutInfo = ::skl::abix::runtime::LayoutInfo;\n"
         "using FieldInfo = ::skl::abix::runtime::FieldDescriptor;\n"
         "using ParameterInfo = ::skl::abix::runtime::ParameterDescriptor;\n"
         "using FunctionInfo = ::skl::abix::runtime::FunctionDescriptor;\n"
         "using SymbolInfo = ::skl::abix::runtime::SymbolDescriptor;\n"
         "using ModuleInfo = ::skl::abix::runtime::ModuleDescriptor;\n"
         "enum class MapOpcode : std::uint8_t { copy_field, convert_int, convert_float, add_default, skip_field };\n"
         "struct MapOperation { MapOpcode opcode; std::uint32_t source_field; std::uint32_t target_field; std::uint32_t source_offset; std::uint32_t target_offset; std::uint32_t byte_count; TypeId auxiliary; };\n"
         "template <typename Source, typename Target> struct MapPrivate;\n"
         "template <typename T> struct TypeTraits;\n";

    for (const auto &type : m.types) {
        const std::string id = cpp_name(type.name) + "_ABIX";
        o << "struct " << id << " {\n"
          << "  static constexpr TypeId type_id{" << hex_u64(type.id.lo) << ", "
          << hex_u64(type.id.hi) << "};\n"
          << "  static constexpr std::uint64_t type_id_lo = " << hex_u64(type.id.lo) << ";\n"
          << "  static constexpr std::uint64_t type_id_hi = " << hex_u64(type.id.hi) << ";\n"
          << "  static constexpr LayoutInfo layout{" << type.size << ", " << type.align << ", "
          << type.field_count << "};\n"
          << "  static constexpr std::size_t size = " << type.size << ";\n"
          << "  static constexpr std::size_t align = " << type.align << ";\n"
          ;
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto &field = m.fields[type.field_begin + i];
            o << "  static constexpr std::size_t " << cpp_name(field.name)
              << "_offset = " << field.offset << ";\n";
        }
        o << "};\n"
          << "template <> struct TypeTraits<" << id << "> {\n"
          << "  static constexpr TypeId type_id{" << hex_u64(type.id.lo) << ", "
          << hex_u64(type.id.hi) << "};\n"
          << "  static constexpr LayoutInfo layout{" << type.size << ", " << type.align << ", "
          << type.field_count << "};\n"
          << "  static constexpr std::size_t size = " << type.size << ";\n"
          << "  static constexpr std::size_t align = " << type.align << ";\n"
          << "};\n";
        o << "inline constexpr FieldInfo " << id << "_fields["
          << (type.field_count == 0 ? 1 : type.field_count) << "] = {\n";
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto &field = m.fields[type.field_begin + i];
            o << "  {" << cpp_string(field.name) << ", {" << hex_u64(field.type_id.lo) << ", "
              << hex_u64(field.type_id.hi) << "}, " << field.offset << ", " << field.flags << "},\n";
        }
        o << "};\n";
    }

    for (size_t i = 0; i < m.functions.size(); ++i) {
        const auto &function = m.functions[i];
        o << "inline constexpr ParameterInfo amc_function_" << i << "_parameters["
          << (function.parameters.empty() ? 1 : function.parameters.size()) << "] = {\n";
        for (const auto &parameter : function.parameters)
            o << "  {" << cpp_string(parameter.name) << ", {" << hex_u64(parameter.type_id.lo) << ", "
              << hex_u64(parameter.type_id.hi) << "}, " << parameter.flags << "},\n";
        o << "};\n"
          << "inline constexpr FunctionInfo amc_function_" << i << "{" << cpp_string(function.name)
          << ", {" << hex_u64(function.signature.lo) << ", " << hex_u64(function.signature.hi)
          << "}, {" << hex_u64(function.return_type.lo) << ", " << hex_u64(function.return_type.hi)
          << "}, amc_function_" << i << "_parameters, " << function.parameters.size() << ", "
          << function.calling_convention << ", " << function.flags << "};\n";
    }

    o << "inline constexpr ::skl::abix::runtime::TypeDescriptor amc_types["
      << (m.types.empty() ? 1 : m.types.size()) << "] = {\n";
    for (const auto &type : m.types) {
        const std::string id = cpp_name(type.name) + "_ABIX";
        o << "  {" << cpp_string(type.name) << ", {" << hex_u64(type.id.lo) << ", "
          << hex_u64(type.id.hi) << "}, {" << hex_u64(type.layout_hash.lo) << ", "
          << hex_u64(type.layout_hash.hi) << "}, " << type.flags << ", " << type.size << ", "
          << type.align << ", " << id << "_fields, " << type.field_count << "},\n";
    }
    o << "};\n"
      << "inline constexpr ::skl::abix::runtime::FunctionDescriptor amc_functions["
      << (m.functions.empty() ? 1 : m.functions.size()) << "] = {\n";
    for (size_t i = 0; i < m.functions.size(); ++i) {
        const auto &function = m.functions[i];
        o << "  {" << cpp_string(function.name) << ", {" << hex_u64(function.signature.lo) << ", "
          << hex_u64(function.signature.hi) << "}, {" << hex_u64(function.return_type.lo) << ", "
          << hex_u64(function.return_type.hi) << "}, amc_function_" << i << "_parameters, "
          << function.parameters.size() << ", " << function.calling_convention << ", "
          << function.flags << "},\n";
    }
    o << "};\n";

    o << "inline constexpr SymbolInfo amc_symbols["
      << (m.symbols.empty() ? 1 : m.symbols.size()) << "] = {\n";
    for (const auto &symbol : m.symbols)
        o << "  {" << cpp_string(symbol.name) << ", " << static_cast<uint32_t>(symbol.kind)
          << ", " << symbol.target_index << "},\n";
    o << "};\n"
      << "inline constexpr ModuleInfo amc_module{" << cpp_string(m.package_name) << ", "
      << cpp_string(m.package_version) << ", amc_types, " << m.types.size() << ", amc_functions, "
      << m.functions.size() << ", amc_symbols, " << m.symbols.size() << "};\n";
    for (size_t i = 0; i < m.maps.size(); ++i) {
        const auto &map = m.maps[i];
        o << "inline constexpr MapOperation amc_map_" << i << "_operations["
          << (map.operations.empty() ? 1 : map.operations.size()) << "] = {\n";
        for (const auto &operation : map.operations)
            o << "  {MapOpcode::" << (operation.opcode == amc::MapOpcode::copy_field ? "copy_field" :
                                      operation.opcode == amc::MapOpcode::convert_int ? "convert_int" :
                                      operation.opcode == amc::MapOpcode::convert_float ? "convert_float" :
                                      operation.opcode == amc::MapOpcode::add_default ? "add_default" : "skip_field")
              << ", " << operation.source_field << ", " << operation.target_field << ", "
              << operation.source_offset << ", " << operation.target_offset << ", " << operation.byte_count << ", {"
              << hex_u64(operation.auxiliary.lo) << ", " << hex_u64(operation.auxiliary.hi) << "}},\n";
        o << "};\n"
          << "template <> struct MapPrivate<" << cpp_name(map.source_name) << "_ABIX, "
          << cpp_name(map.target_name) << "_ABIX> {\n"
          << "  static constexpr const MapOperation *operations = amc_map_" << i << "_operations;\n"
          << "  static constexpr std::size_t operation_count = " << map.operations.size() << ";\n"
          << "  static bool apply(void *target, const void *source) noexcept {\n"
          << "    if (!target || !source) return false;\n";
        for (const auto &operation : map.operations) {
            switch (operation.opcode) {
            case amc::MapOpcode::copy_field:
                o << "    std::memcpy(static_cast<char *>(target) + " << operation.target_offset
                  << ", static_cast<const char *>(source) + " << operation.source_offset << ", "
                  << operation.byte_count << ");\n";
                break;
            case amc::MapOpcode::add_default:
                o << "    std::memset(static_cast<char *>(target) + " << operation.target_offset
                  << ", 0, " << operation.byte_count << ");\n";
                break;
            case amc::MapOpcode::skip_field:
                break;
            case amc::MapOpcode::convert_int:
            case amc::MapOpcode::convert_float:
                o << "    return false;  // conversion requires an explicit native converter\n";
                break;
            }
        }
        o << "    return true;\n"
          << "  }\n"
          << "};\n";
    }
    o << "}  // namespace amc_generated\n"
         "#ifdef AMC_GENERATED_DECLARE_NATIVE_TYPE_TRAITS\n"
         "namespace skl::abix::runtime {\n";
    for (const auto &type : m.types) {
        // Native traits are an opt-in convenience projection.  A record
        // discovered while walking a layout may be a library template primary
        // (Clang does not consistently retain that fact on every visited
        // CXXRecordDecl), for which `TypeTraits<::std::vector>` is ill-formed.
        // Only emit a specialization when the spelling denotes a concrete,
        // non-standard-library C++ type.
        if (type.kind != amc::TypeKind::record ||
            (type.flags & amc::type_template_primary) != 0 ||
            type.name.find('<') != std::string::npos ||
            type.name.rfind("std::", 0) == 0) continue;
        const std::string id = cpp_name(type.name) + "_ABIX";
        o << "template <> struct TypeTraits<::" << type.name << "> {\n"
          << "  static constexpr ::skl::abix::model::TypeId type_id = ::amc_generated::"
          << id << "::type_id;\n};\n";
    }
    o << "}  // namespace skl::abix::runtime\n"
         "#endif  // AMC_GENERATED_DECLARE_NATIVE_TYPE_TRAITS\n";
    return 0;
}

static std::string json_value(const std::string &line, const char *key) {
    const std::string needle = std::string("\"") + key + "\":\"";
    const auto begin = line.find(needle);
    if (begin == std::string::npos) return {};
    const auto start = begin + needle.size();
    const auto end = line.find('"', start);
    return end == std::string::npos ? std::string{} : line.substr(start, end - start);
}

static int run_frontend(const char *config_path, const char *output_path) {
    Config c;
    std::string e;
    if (!config_load(config_path, c, e)) { std::cerr << e << "\n"; return 1; }
    std::filesystem::path base = std::filesystem::absolute(config_path).parent_path();
    if (!c.db.empty() && std::filesystem::path(c.db).is_relative()) c.db = (base / c.db).string();
    for (auto &f : c.files) if (std::filesystem::path(f).is_relative()) f = (base / f).string();
    std::string db_error;
    std::unique_ptr<CompilationDatabase> db;
    if (!c.db.empty()) db = JSONCompilationDatabase::loadFromFile(c.db, db_error, JSONCommandLineSyntax::AutoDetect);
    else db = std::make_unique<FixedCompilationDatabase>(base.string(), c.flags);
    if (!db) { std::cerr << db_error << "\n"; return 1; }
    std::set<std::string> wanted(c.symbols.begin(), c.symbols.end());
    amc::AbiModule module;
    module.package_name = "cpp";
    Factory factory(wanted, module);
    ClangTool tool(*db, c.files);
    if (tool.run(&factory) != 0) { std::cerr << "clang analysis failed\n"; return 1; }
    if (!amc::write_abix(module, output_path, e)) { std::cerr << e << "\n"; return 1; }
    return 0;
}

static int ipc() {
    std::string line, capability, input, output;
    bool initialized = false;
    while (std::getline(std::cin, line)) {
        if (line.find("\"type\":\"INIT\"") != std::string::npos) {
            initialized = line.find("\"protocol\":1") != std::string::npos;
            std::cout << "{\"type\":\"READY\",\"protocol\":1}\n" << std::flush;
        } else if (line.find("\"type\":\"QUERY_CAPABILITIES\"") != std::string::npos) {
            if (!initialized) return 2;
            std::cout << "{\"type\":\"CAPABILITIES\",\"language\":\"cpp\",\"capabilities\":[\"frontend\",\"backend\"]}\n" << std::flush;
        } else if (line.find("\"type\":\"ANALYZE\"") != std::string::npos) {
            capability = json_value(line, "capability");
            input = json_value(line, "input"); output = json_value(line, "output");
            int result = capability == "frontend" ? run_frontend(input.c_str(), output.c_str()) :
                         capability == "backend" ? backend(input.c_str(), output.c_str()) : 2;
            if (result == 0)
                std::cout << "{\"type\":\"ABI_MODULE\",\"path\":\"" << output << "\"}\n";
            std::cout << "{\"type\":\"ANALYZE_RESULT\",\"status\":" << result << "}\n" << std::flush;
            if (result != 0) return result;
        } else if (line.find("\"type\":\"DONE\"") != std::string::npos) return 0;
    }
    return 2;
}

int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--ipc") return ipc();
    if (argc != 4) {
        std::cerr << "usage: amc-cpp frontend <config.abic.toml> <output.abix> | backend <input.abix> <output.hpp>\n";
        return 2;
    }
    if (std::string(argv[1]) == "backend") return backend(argv[2], argv[3]);
    if (std::string(argv[1]) != "frontend") {
        std::cerr << "unknown capability\n";
        return 2;
    }
    return run_frontend(argv[2], argv[3]);
}
