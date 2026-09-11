#include "../core/amc_core.h"
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecordLayout.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Options/OptionUtils.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/Path.h>
#include <toml++/toml.h>
#include <algorithm>
#include <iostream>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <unordered_map>
#include <filesystem>
#include <string_view>
#include <unistd.h>

#include <fmt/color.h>
#include <fmt/os.h>

using namespace clang;
using namespace clang::tooling;

// Clang resource directory (e.g. /opt/homebrew/opt/llvm/lib/clang/22),
// detected at build time via `clang -print-resource-dir` and injected
// as a compile definition.  Used to locate built-in headers (stdarg.h,
// stddef.h, etc.) and the Brew LLVM's libc++ installation.
#ifndef ABIX_CLANG_RESOURCE_DIR
#define ABIX_CLANG_RESOURCE_DIR ""
#endif
static const char *const kClangResourceDir = ABIX_CLANG_RESOURCE_DIR;

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
            const std::string fname = f->getNameAsString();
            // Skip if a field with the same name already exists in this type.
            // (libc++ std::string has both __long and __short inside an
            // anonymous union, each with fields named __data_, __size_, etc.)
            bool dup = false;
            for (uint32_t j = module.types[record_index].field_begin;
                 j < module.fields.size(); ++j) {
                if (module.fields[j].name == fname) { dup = true; break; }
            }
            if (dup) continue;
            amc::Field x;
            x.owner_type = t.id;
            x.name = std::move(fname);
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
    return fmt::format("0x{:x}ULL", value);
}

static int backend(const char *input, const char *output) {
    amc::AbiModule m;
    std::string e;
    if (!amc::read_abix(input, m, e)) {
        fmt::print(stderr, "{}\n", e);
        return 1;
    }
    auto o = fmt::output_file(output);
    std::string header = "#pragma once\n"
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
        const std::string id = fmt::format("{}_ABIX", cpp_name(type.name));
        header += fmt::format("struct {} {{\n", id);
        header += fmt::format("  static constexpr TypeId type_id{{{}, {}}};\n", hex_u64(type.id.lo), hex_u64(type.id.hi));
        header += fmt::format("  static constexpr std::uint64_t type_id_lo = {};\n", hex_u64(type.id.lo));
        header += fmt::format("  static constexpr std::uint64_t type_id_hi = {};\n", hex_u64(type.id.hi));
        header += fmt::format("  static constexpr LayoutInfo layout{{{}, {}, {}}};\n", type.size, type.align, type.field_count);
        header += fmt::format("  static constexpr std::size_t size = {};\n", type.size);
        header += fmt::format("  static constexpr std::size_t align = {};\n", type.align);
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto &field = m.fields[type.field_begin + i];
            header += fmt::format("  static constexpr std::size_t {}_offset = {};\n", cpp_name(field.name), field.offset);
        }
        header += fmt::format("}};\n");
        header += fmt::format("template <> struct TypeTraits<{}> {{\n", id);
        header += fmt::format("  static constexpr TypeId type_id{{{}, {}}};\n", hex_u64(type.id.lo), hex_u64(type.id.hi));
        header += fmt::format("  static constexpr LayoutInfo layout{{{}, {}, {}}};\n", type.size, type.align, type.field_count);
        header += fmt::format("  static constexpr std::size_t size = {};\n", type.size);
        header += fmt::format("  static constexpr std::size_t align = {};\n", type.align);
        header += fmt::format("}};\n");
        header += fmt::format("inline constexpr FieldInfo {}_fields[{}] = {{\n", id, type.field_count == 0 ? 1 : type.field_count);
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto &field = m.fields[type.field_begin + i];
            header += fmt::format("  {{{}, {{{}, {}}}, {}, {}}},\n",
                       cpp_string(field.name), hex_u64(field.type_id.lo), hex_u64(field.type_id.hi),
                       field.offset, field.flags);
        }
        header += fmt::format("}};\n");
    }

    for (size_t i = 0; i < m.functions.size(); ++i) {
        const auto &function = m.functions[i];
        const auto param_count = function.parameters.empty() ? 1 : function.parameters.size();
        header += fmt::format("inline constexpr ParameterInfo amc_function_{}_parameters[{}] = {{\n", i, param_count);
        for (const auto &parameter : function.parameters)
            header += fmt::format("  {{{}, {{{}, {}}}, {}}},\n",
                       cpp_string(parameter.name), hex_u64(parameter.type_id.lo),
                       hex_u64(parameter.type_id.hi), parameter.flags);
        header += fmt::format("}};\n");
        header += fmt::format("inline constexpr FunctionInfo amc_function_{}{{{}, {{{}, {}}}, {{{}, {}}},"
                   " amc_function_{}_parameters, {}, {}, {}}};\n",
                   i, cpp_string(function.name), hex_u64(function.signature.lo),
                   hex_u64(function.signature.hi), hex_u64(function.return_type.lo),
                   hex_u64(function.return_type.hi), i, function.parameters.size(),
                   function.calling_convention, function.flags);
    }

    {
        const auto type_count = m.types.empty() ? 1 : m.types.size();
        header += fmt::format("inline constexpr ::skl::abix::runtime::TypeDescriptor amc_types[{}] = {{\n",
                   type_count);
        for (const auto &type : m.types) {
            const std::string id = fmt::format("{}_ABIX", cpp_name(type.name));
            header += fmt::format("  {{{}, {{{}, {}}}, {{{}, {}}}, {}, {}, {}, {}_fields, {}}},\n",
                       cpp_string(type.name), hex_u64(type.id.lo), hex_u64(type.id.hi),
                       hex_u64(type.layout_hash.lo), hex_u64(type.layout_hash.hi),
                       type.flags, type.size, type.align, id, type.field_count);
        }
        header += fmt::format("}};\n");
    }

    {
        const auto func_count = m.functions.empty() ? 1 : m.functions.size();
        header += fmt::format("inline constexpr ::skl::abix::runtime::FunctionDescriptor amc_functions[{}] = {{\n",
                   func_count);
        for (size_t i = 0; i < m.functions.size(); ++i) {
            const auto &function = m.functions[i];
            header += fmt::format("  {{{}, {{{}, {}}}, {{{}, {}}}, amc_function_{}_parameters, {}, {}, {}}},\n",
                       cpp_string(function.name), hex_u64(function.signature.lo),
                       hex_u64(function.signature.hi), hex_u64(function.return_type.lo),
                       hex_u64(function.return_type.hi), i, function.parameters.size(),
                       function.calling_convention, function.flags);
        }
        header += fmt::format("}};\n");
    }

    {
        const auto sym_count = m.symbols.empty() ? 1 : m.symbols.size();
        header += fmt::format("inline constexpr SymbolInfo amc_symbols[{}] = {{\n", sym_count);
        for (const auto &symbol : m.symbols)
            header += fmt::format("  {{{}, {}, {}}},\n",
                       cpp_string(symbol.name), static_cast<uint32_t>(symbol.kind),
                       symbol.target_index);
        header += fmt::format("}};\n");
    }

    header += fmt::format("inline constexpr ModuleInfo amc_module{{{}, {}, amc_types, {}, amc_functions, {}, amc_symbols, {}}};\n",
               cpp_string(m.package_name), cpp_string(m.package_version),
               m.types.size(), m.functions.size(), m.symbols.size());

    for (size_t i = 0; i < m.maps.size(); ++i) {
        const auto &map = m.maps[i];
        const auto op_count = map.operations.empty() ? 1 : map.operations.size();
        header += fmt::format("inline constexpr MapOperation amc_map_{}_operations[{}] = {{\n", i, op_count);
        for (const auto &operation : map.operations) {
            const char *opname =
                operation.opcode == amc::MapOpcode::copy_field ? "copy_field" :
                operation.opcode == amc::MapOpcode::convert_int ? "convert_int" :
                operation.opcode == amc::MapOpcode::convert_float ? "convert_float" :
                operation.opcode == amc::MapOpcode::add_default ? "add_default" : "skip_field";
            header += fmt::format("  {{MapOpcode::{}, {}, {}, {}, {}, {}, {{{}, {}}}}},\n",
                       opname, operation.source_field, operation.target_field,
                       operation.source_offset, operation.target_offset, operation.byte_count,
                       hex_u64(operation.auxiliary.lo), hex_u64(operation.auxiliary.hi));
        }
        header += fmt::format("}};\n"
                   "template <> struct MapPrivate<{}_ABIX, {}_ABIX> {{\n"
                   "  static constexpr const MapOperation *operations = amc_map_{}_operations;\n"
                   "  static constexpr std::size_t operation_count = {};\n"
                   "  static bool apply(void *target, const void *source) noexcept {{\n"
                   "    if (!target || !source) return false;\n",
                   cpp_name(map.source_name), cpp_name(map.target_name), i, map.operations.size());
        for (const auto &operation : map.operations) {
            switch (operation.opcode) {
            case amc::MapOpcode::copy_field:
                header += fmt::format("    std::memcpy(static_cast<char *>(target) + {}, static_cast<const char *>(source) + {}, {});\n",
                           operation.target_offset, operation.source_offset, operation.byte_count);
                break;
            case amc::MapOpcode::add_default:
                header += fmt::format("    std::memset(static_cast<char *>(target) + {}, 0, {});\n",
                           operation.target_offset, operation.byte_count);
                break;
            case amc::MapOpcode::skip_field:
                break;
            case amc::MapOpcode::convert_int:
            case amc::MapOpcode::convert_float:
                header += fmt::format("    return false;  // conversion requires an explicit native converter\n");
                break;
            }
        }
        header += "    return true;\n"
                   "  }\n"
                   "};\n";
    }

    header += "}  // namespace amc_generated\n"
               "#ifdef AMC_GENERATED_DECLARE_NATIVE_TYPE_TRAITS\n"
               "namespace skl::abix::runtime {\n";
    for (const auto &type : m.types) {
        if (type.kind != amc::TypeKind::record ||
            (type.flags & amc::type_template_primary) != 0 ||
            type.name.find('<') != std::string::npos ||
            type.name.rfind("std::", 0) == 0) continue;
        const std::string id = fmt::format("{}_ABIX", cpp_name(type.name));
        header += fmt::format("template <> struct TypeTraits<::{}> {{\n"
                   "  static constexpr ::skl::abix::model::TypeId type_id = ::amc_generated::{}::type_id;\n"
                   "}};\n",
                   type.name, id);
    }
    header += "}  // namespace skl::abix::runtime\n"
               "#endif  // AMC_GENERATED_DECLARE_NATIVE_TYPE_TRAITS\n";
    o.print("{}", header);
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

// Derive the Homebrew LLVM installation prefix from the known resource-dir.
//   kClangResourceDir  →  /opt/homebrew/opt/llvm/lib/clang/22
//   brew_prefix        →  /opt/homebrew/opt/llvm
//
// Returns the empty string when the prefix cannot be derived.
// On non-macOS platforms this always returns empty.
static std::string brew_llvm_prefix() {
#if defined(__APPLE__)
    if (!kClangResourceDir[0]) return {};
    llvm::SmallString<128> pfx(kClangResourceDir);
    llvm::sys::path::remove_filename(pfx);
    llvm::sys::path::remove_filename(pfx);
    llvm::sys::path::remove_filename(pfx);
    llvm::SmallString<128> sanity(pfx);
    llvm::sys::path::append(sanity, "include", "c++", "v1");
    return access(sanity.c_str(), F_OK) == 0 ? pfx.c_str() : std::string{};
#else
    return {};
#endif
}

// Detect the macOS SDK path and append flags that mirror what the Clang
// driver would normally set when invoked from the command line.
//
// When loaded as a library (no config file is read), Clang does not
// automatically know its resource directory or the active SDK sysroot.
// This function fills those gaps.
//
// Include order (required by libc++ <cstddef>):
//   1. libc++ headers  (Brew LLVM: -cxx-isystem via -nostdinc++)
//   2. Clang built-ins (via -resource-dir + -isystem)
//   3. System headers  (via -isysroot)
//
// The most important design rule is that **all three sets of headers must
// come from the same LLVM distribution**.  On macOS the default Xcode SDK
// ships its own libc++ at <sdk>/usr/include/c++/v1, but that version may
// be incompatible with the Homebrew LLVM's Clang built-in headers (e.g.
// std::string internal layout differs, leading to duplicate member errors).
//
// We therefore:
//   a. Derive the Homebrew LLVM prefix from the resource-dir known at
//      build time (kClangResourceDir);
//   b. Use -nostdinc++ to suppress the default C++ standard library search
//      (which would otherwise pick up the SDK's libc++);
//   c. Use -cxx-isystem to re-add only the Homebrew LLVM's own libc++.
//
// All three values are derived automatically: the resource directory is
// detected at build time via `clang -print-resource-dir` and injected through
// the ABIX_CLANG_RESOURCE_DIR compile definition; the SDK path is looked up
// at runtime from the SDKROOT environment variable or via xcrun.
static void append_macos_sysroot(std::vector<std::string> &flags) {
#if defined(__APPLE__)
    // ----- 1. Detect SDK path -------------------------------------------
    auto detect_sdk = []() -> std::string {
        if (const char *sdk = std::getenv("SDKROOT"))
            return sdk;
        FILE *fp = popen("xcrun --sdk macosx --show-sdk-path 2>/dev/null", "r");
        if (!fp) return {};
        char buf[4096] = {0};
        std::string result;
        if (std::fgets(buf, sizeof(buf), fp))
            result = buf;
        pclose(fp);
        while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back())))
            result.pop_back();
        return result;
    };

    // ----- 2. Check for existing configuration ----------------------------
    // If the user (or the .abic.toml) already provides explicit sysroot or
    // a valid -isystem, don't override their choices.
    for (size_t i = 0; i < flags.size(); ++i) {
        const auto &f = flags[i];
        if (f == "-isysroot" || f.find("--sysroot") == 0) return;
        if (f == "-isystem" && i + 1 < flags.size()) {
            if (access(flags[i + 1].c_str(), F_OK) == 0) return;
        }
    }

    // ----- 3. Derive Homebrew LLVM prefix --------------------------------
    const std::string brew_prefix = brew_llvm_prefix();
    const bool have_brew_libcxx = !brew_prefix.empty();

    // ----- 4. macOS SDK setup --------------------------------------------
    //
    // Include order (all from the same LLVM distribution):
    //   1. libc++ headers   (Homebrew LLVM via -isystem)
    //   2. Clang built-ins  (<resource>/include via -isystem)
    //   3. System headers   (-isysroot)
    //
    // IMPORTANT: We use `-isystem` (NOT `-cxx-isystem`) for brew libc++.
    // In Clang's header search, `-isystem` paths are searched *before*
    // `-cxx-isystem` paths.  If we used `-cxx-isystem`, the resource-dir's
    // `-isystem` path would win over libc++, and `<cstddef>` would fail
    // because it finds Clang's `<stddef.h>` instead of libc++'s wrapper.
    // ---------------------------------------------------------------------
    {
        std::string sdk = detect_sdk();
        if (!sdk.empty()) {
            // (a) libc++ – from Brew LLVM (compatible with our resource-dir)
            if (have_brew_libcxx) {
                llvm::SmallString<128> brew_libcxx(brew_prefix);
                llvm::sys::path::append(brew_libcxx, "include", "c++", "v1");
                flags.push_back("-nostdinc++");
                flags.push_back("-isystem");
                flags.push_back(brew_libcxx.c_str());
            }

            // (b) Clang built-in headers (stdarg.h, stddef.h, etc.)
            if (kClangResourceDir[0]) {
                flags.push_back("-resource-dir");
                flags.push_back(kClangResourceDir);
                llvm::SmallString<128> res_inc(kClangResourceDir);
                llvm::sys::path::append(res_inc, "include");
                if (access(res_inc.c_str(), F_OK) == 0) {
                    flags.push_back("-isystem");
                    flags.push_back(res_inc.c_str());
                }
            }

            // (c) SDK sysroot – system C headers, frameworks, etc.
            flags.push_back("-isysroot");
            flags.push_back(std::move(sdk));
        }
    }

    // ----- 5. Fallback (no SDK) ------------------------------------------
    // If no SDK is available, at least set the resource directory and
    // libc++ from the Brew LLVM prefix, so basic analysis still works.
    if (!flags.empty() &&
        std::find(flags.begin(), flags.end(), "-resource-dir") == flags.end()) {
        // (a) libc++ from Brew LLVM (must use -isystem, not -cxx-isystem,
        //     because -cxx-isystem has lower priority than -isystem and
        //     would lose to the resource-dir's built-in headers below)
        if (have_brew_libcxx) {
            llvm::SmallString<128> brew_libcxx(brew_prefix);
            llvm::sys::path::append(brew_libcxx, "include", "c++", "v1");
            flags.push_back("-nostdinc++");
            flags.push_back("-isystem");
            flags.push_back(brew_libcxx.c_str());
        }

        // (b) Clang built-in headers
        if (kClangResourceDir[0]) {
            flags.push_back("-resource-dir");
            flags.push_back(kClangResourceDir);
            llvm::SmallString<128> res_inc(kClangResourceDir);
            llvm::sys::path::append(res_inc, "include");
            if (access(res_inc.c_str(), F_OK) == 0) {
                flags.push_back("-isystem");
                flags.push_back(res_inc.c_str());
            }
        }
    }
#endif
}

static int run_frontend(const char *config_path, const char *output_path) {
    Config c;
    std::string e;
    if (!config_load(config_path, c, e)) { fmt::print(stderr, "{}\n", e); return EXIT_FAILURE; }
    std::filesystem::path base = std::filesystem::absolute(config_path).parent_path();
    if (!c.db.empty() && std::filesystem::path(c.db).is_relative()) c.db = (base / c.db).string();
    for (auto &f : c.files) if (std::filesystem::path(f).is_relative()) f = (base / f).string();
    append_macos_sysroot(c.flags);
    std::string db_error;
    std::unique_ptr<CompilationDatabase> db;
    if (!c.db.empty()) db = JSONCompilationDatabase::loadFromFile(c.db, db_error, JSONCommandLineSyntax::AutoDetect);
    else db = std::make_unique<FixedCompilationDatabase>(base.string(), c.flags);
    if (!db) { fmt::print(stderr, "{}\n", db_error); return EXIT_FAILURE; }
    std::set<std::string> wanted(c.symbols.begin(), c.symbols.end());
    amc::AbiModule module;
    module.package_name = "cpp";
    Factory factory(wanted, module);
    ClangTool tool(*db, c.files);
    if (tool.run(&factory) != 0) { fmt::print(stderr, "clang analysis failed\n"); return EXIT_FAILURE; }
    if (!amc::write_abix(module, output_path, e)) { fmt::print(stderr, "{}\n", e); return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}

static int ipc() {
    std::string line, capability, input, output;
    bool initialized = false;
    while (std::getline(std::cin, line)) {
        if (line.find("\"type\":\"INIT\"") != std::string::npos) {
            initialized = line.find("\"protocol\":1") != std::string::npos;
            fmt::print("{{\"type\":\"READY\",\"protocol\":1}}\n");
            std::fflush(stdout);
        } else if (line.find("\"type\":\"QUERY_CAPABILITIES\"") != std::string::npos) {
            if (!initialized) return 2;
            fmt::print("{{\"type\":\"CAPABILITIES\",\"language\":\"cpp\",\"capabilities\":[\"frontend\",\"backend\"]}}\n");
            std::fflush(stdout);
        } else if (line.find("\"type\":\"ANALYZE\"") != std::string::npos) {
            capability = json_value(line, "capability");
            input = json_value(line, "input"); output = json_value(line, "output");
            int result = capability == "frontend" ? run_frontend(input.c_str(), output.c_str()) :
                         capability == "backend" ? backend(input.c_str(), output.c_str()) : 2;
            if (result == 0)
                fmt::print("{{\"type\":\"ABI_MODULE\",\"path\":\"{}\"}}\n", output);
            fmt::print("{{\"type\":\"ANALYZE_RESULT\",\"status\":{}}}\n", result);
            std::fflush(stdout);
            if (result != 0) return result;
        } else if (line.find("\"type\":\"DONE\"") != std::string::npos) return 0;
    }
    return 2;
}

int main(int argc, char **argv) {
    #ifdef __APPLE__
    if (!kClangResourceDir[0]) {
        fmt::print(stderr, "warning: ABIX_CLANG_RESOURCE_DIR not set at build time; "
                   "built-in headers (stdarg.h, etc.) may not be found.\n");
    }
    #endif

    if (argc == 2 && std::string(argv[1]) == "--ipc") return ipc();
    if (argc != 4) {
        fmt::print(stderr, "usage: amc-cpp frontend <config.abic.toml> <output.abix> | backend <input.abix> <output.hpp>\n");
        return 2;
    }
    if (std::string(argv[1]) == "backend") return backend(argv[2], argv[3]);
    if (std::string(argv[1]) != "frontend") {
        fmt::print(stderr, "unknown capability\n");
        return 2;
    }
    return run_frontend(argv[2], argv[3]);
}