#include "AMC/Lang/C++.h"

namespace amc::cpp {
namespace {
// Stable, language-independent name for a floating-point format. Two float
// types only share an identity when they share an actual IEEE/format semantic
// (e.g. x87 80-bit must never merge with binary128, and a target where
// `long double` is just `double` must merge with it).
const char *float_semantics_tag(const llvm::fltSemantics &semantics) {
    using llvm::APFloatBase;
    if (&semantics == &APFloatBase::IEEEhalf()) return "ieee16";
    if (&semantics == &APFloatBase::BFloat()) return "bfloat16";
    if (&semantics == &APFloatBase::IEEEsingle()) return "ieee32";
    if (&semantics == &APFloatBase::IEEEdouble()) return "ieee64";
    if (&semantics == &APFloatBase::IEEEquad()) return "ieee128";
    if (&semantics == &APFloatBase::x87DoubleExtended()) return "x87_80";
    if (&semantics == &APFloatBase::PPCDoubleDouble()) return "ppc_double_double";
    return "unknown";
}

amc::FloatFormat float_format_code(const llvm::fltSemantics &semantics) {
    using llvm::APFloatBase;
    if (&semantics == &APFloatBase::IEEEhalf()) return amc::FloatFormat::ieee16;
    if (&semantics == &APFloatBase::BFloat()) return amc::FloatFormat::bfloat16;
    if (&semantics == &APFloatBase::IEEEsingle()) return amc::FloatFormat::ieee32;
    if (&semantics == &APFloatBase::IEEEdouble()) return amc::FloatFormat::ieee64;
    if (&semantics == &APFloatBase::IEEEquad()) return amc::FloatFormat::ieee128;
    if (&semantics == &APFloatBase::x87DoubleExtended()) return amc::FloatFormat::x87_80;
    if (&semantics == &APFloatBase::PPCDoubleDouble()) return amc::FloatFormat::ppc_double_double;
    return amc::FloatFormat::none;
}

// Pick the ABI kind that matches the observed signedness of a type whose C
// spelling does not fix it (`char`, `wchar_t`).
amc::PrimitiveAbiKind signedness_kind(bool is_signed, amc::PrimitiveAbiKind signed_kind,
    amc::PrimitiveAbiKind unsigned_kind) {
    return is_signed ? signed_kind : unsigned_kind;
}

// ABI descriptor for a primitive scalar type.
//
// Identity is the *abstract C ABI kind* — the width and format are recorded as
// properties, not baked into the name. This is what makes a target like AVR
// work: there `double` is 32-bit, and the IR still says `double` (an abstract
// kind) with `size == 4`, so a Rust backend can emit `core::ffi::c_double`
// (which is `f32` on AVR) instead of a hard-coded `f64`. Two spellings that
// share an ABI (`long`/`long long` on LP64) collapse onto one kind, and the
// measured width/format disambiguates the rest (x87 80-bit vs binary128).
//
// Returns an empty string when the type is not a scalar builtin (pointer,
// array, record, ...), meaning the caller keeps the spelling-based identity.
// `abi` is filled with the packed `PrimitiveAbiKind` + width + format.
std::string primitive_abi_key(clang::ASTContext &ctx, clang::QualType qt, uint32_t &abi) {
    const auto *bt = qt->getAs<clang::BuiltinType>();
    if (bt == nullptr) return {};
    const uint32_t bytes = uint32_t(ctx.getTypeSize(qt) / 8);
    const bool is_signed = bt->isSignedInteger();
    switch (bt->getKind()) {
        case clang::BuiltinType::Void:
            abi = amc::primitive_abi(bytes, amc::PrimitiveAbiKind::void_);
            return "prim:void";
        case clang::BuiltinType::Bool:
            abi = amc::primitive_abi(bytes, amc::PrimitiveAbiKind::bool_);
            return "prim:bool";
        case clang::BuiltinType::Char_S:
        case clang::BuiltinType::Char_U:
            // Plain `char`: signedness is target-defined, so the kind encodes
            // the *actual* signedness, not the spelling.
            abi = amc::primitive_abi(bytes, signedness_kind(is_signed, amc::PrimitiveAbiKind::char_signed,
                amc::PrimitiveAbiKind::char_unsigned));
            return is_signed ? "prim:char_s" : "prim:char_u";
        case clang::BuiltinType::SChar:
            abi = amc::primitive_abi(bytes, amc::PrimitiveAbiKind::schar, amc::FloatFormat::none, true);
            return "prim:schar";
        case clang::BuiltinType::UChar:
            abi = amc::primitive_abi(bytes, amc::PrimitiveAbiKind::uchar);
            return "prim:uchar";
        case clang::BuiltinType::Char8:
            abi = amc::primitive_abi(bytes, amc::PrimitiveAbiKind::char8);
            return "prim:char8";
        case clang::BuiltinType::Char16:
            abi = amc::primitive_abi(bytes, amc::PrimitiveAbiKind::char16);
            return "prim:char16";
        case clang::BuiltinType::Char32:
            abi = amc::primitive_abi(bytes, amc::PrimitiveAbiKind::char32);
            return "prim:char32";
        case clang::BuiltinType::WChar_S:
        case clang::BuiltinType::WChar_U:
            abi = amc::primitive_abi(bytes, signedness_kind(is_signed, amc::PrimitiveAbiKind::wchar_signed,
                amc::PrimitiveAbiKind::wchar_unsigned));
            return std::string("prim:wchar") + (is_signed ? "_s" : "_u");
        default: break;
    }
    if (bt->isInteger()) {
        // Integer width *is* ABI: `int` and `long` differ, so the key carries
        // width and signedness. `long`/`long long` still collapse because they
        // share both.
        abi = amc::primitive_abi(bytes, is_signed ? amc::PrimitiveAbiKind::sint : amc::PrimitiveAbiKind::uint,
            amc::FloatFormat::none, is_signed);
        return std::string(is_signed ? "prim:i" : "prim:u") + std::to_string(bytes);
    }
    if (bt->isFloatingPoint()) {
        // Floats are identified by format, not spelling: a target where
        // `double` is 32-bit IEEE collapses it with `float` (same ABI), while
        // x87 80-bit and IEEE binary128 stay distinct despite equal size.
        const auto format = float_format_code(ctx.getFloatTypeSemantics(qt));
        abi = amc::primitive_abi(bytes, amc::PrimitiveAbiKind::floating, format);
        return std::string("prim:f") + float_semantics_tag(ctx.getFloatTypeSemantics(qt));
    }
    return {};
}

class Extractor : public clang::RecursiveASTVisitor<Extractor> {
public:
    Extractor(clang::ASTContext &c, const std::set<std::string> &wanted, amc::AbiModule &m)
        : ctx(c)
        , wanted(wanted)
        , module(m) {}
    bool VisitRecordDecl(clang::RecordDecl *d) {
        auto *cxx = llvm::dyn_cast<clang::CXXRecordDecl>(d);
        if (!cxx
            || d->isImplicit()
            || !d->isThisDeclarationADefinition()
            || (!selected(d) && !has_selected_member(cxx)))
            return true;
        add_record(cxx);
        return true;
    }
    bool VisitEnumDecl(clang::EnumDecl *d) {
        if (d->isCompleteDefinition() && selected(d)) add_enum(d);
        return true;
    }
    bool VisitTypedefNameDecl(clang::TypedefNameDecl *d) {
        if (selected(d)) add_alias(d);
        return true;
    }
    bool VisitDecl(clang::Decl *d) {
        if (auto *fn = llvm::dyn_cast<clang::FunctionDecl>(d))
            if (selected(fn)) add_function(fn);
        return true;
    }

private:
    clang::ASTContext &ctx;
    const std::set<std::string> &wanted;
    amc::AbiModule &module;
    std::unordered_map<std::string, amc::Hash128> ids;
    // Dedup keyed by ABI descriptor, so spellings that share an ABI (e.g.
    // `long` / `long long`) collapse onto one type record with one TypeID.
    std::unordered_map<std::string, amc::Hash128> primitive_ids;
    std::string name(const clang::NamedDecl *d) const { return d->getQualifiedNameAsString(); }
    void record_source(amc::Hash128 type_id, clang::SourceLocation location) {
        if (location.isInvalid()) return;
        const auto presumed = ctx.getSourceManager().getPresumedLoc(location);
        if (presumed.isInvalid()) return;
        amc::SourceOrigin origin;
        origin.type_id = type_id;
        origin.file = presumed.getFilename() != nullptr ? presumed.getFilename() : "";
        origin.line = presumed.getLine();
        origin.column = presumed.getColumn();
        module.sources.push_back(std::move(origin));
    }
    void add_namespaces(const clang::NamedDecl *d) {
        const auto *context = d->getDeclContext();
        std::vector<std::string> names;
        while (context && !llvm::isa<clang::TranslationUnitDecl>(context)) {
            if (const auto *named = llvm::dyn_cast<clang::NamespaceDecl>(context)) {
                if (!named->isAnonymousNamespace()) names.push_back(named->getQualifiedNameAsString());
            }
            context = context->getParent();
        }
        // for (auto it = names.rbegin(); it != names.rend(); ++it) {
        for (auto &e : names) {
            if (ids.count(e)) continue;
            amc::Type t;
            t.name = e;
            t.kind = amc::TypeKind::namespace_type;
            t.id = amc::hash_text(t.name, 0x54595045);
            t.size = t.align = 1;
            ids.emplace(t.name, t.id);
            module.types.push_back(std::move(t));
        }
    }
    bool selected(const clang::NamedDecl *d) const {
        auto n = name(d);
        return wanted.count(n) || wanted.count(d->getNameAsString());
    }
    bool has_selected_member(const clang::CXXRecordDecl *d) const {
        const auto prefix = name(d) + "::";
        for (const auto &symbol : wanted)
            if (symbol.compare(0, prefix.size(), prefix) == 0) return true;
        return false;
    }
    amc::Hash128 add_type(clang::QualType qt) {
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
        if (qt->getAs<clang::TemplateSpecializationType>()) {
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
            add_record(const_cast<clang::CXXRecordDecl *>(record));
            auto found = ids.find(record_key);
            if (found != ids.end()) return found->second;
            found = ids.find(name(record));
            if (found != ids.end()) return found->second;
            return add_type(ctx.getCanonicalTagType(const_cast<clang::CXXRecordDecl *>(record)));
        }
        if (const auto *enumeration = qt->getAs<clang::EnumType>()) {
            add_enum(enumeration->getDecl());
            auto found = ids.find(name(enumeration->getDecl()));
            if (found != ids.end()) return found->second;
            return amc::hash_text(s, 0x54595045);
        }
        amc::Type t;
        t.name = s;
        t.size = uint32_t(ctx.getTypeSize(qt) / 8);
        t.align = uint32_t(ctx.getTypeAlign(qt) / 8);
        t.kind = amc::TypeKind::primitive;
        // Primitives are identified by their ABI descriptor, not their spelling,
        // so that ABI-equal types (long/long long, ...) share one TypeID and the
        // abstract C kind (double, long double) is preserved for target-dependent
        // projection (e.g. AVR `double == f32`). Non-scalar types keep the
        // spelling-based identity.
        uint32_t abi = 0;
        const std::string abi_key = primitive_abi_key(ctx, qt, abi);
        t.primitive_abi = abi;
        if (!abi_key.empty()) {
            t.id = amc::hash_text(abi_key, 0x54595045);
            const auto seen = primitive_ids.find(abi_key);
            if (seen != primitive_ids.end()) {
                ids.emplace(s, seen->second);
                return seen->second;
            }
            primitive_ids.emplace(abi_key, t.id);
        } else {
            t.id = amc::hash_text(s, 0x54595045);
        }
        if (const auto *p = qt->getAs<clang::PointerType>()) {
            t.kind = amc::TypeKind::pointer;
            t.name = p->getPointeeType().getAsString() + "*";
            t.size = ctx.getTypeSize(qt) / 8;
            t.align = ctx.getTypeAlign(qt) / 8;
        }
        if (const auto *a = llvm::dyn_cast<clang::ConstantArrayType>(qt.getTypePtr())) {
            t.kind = amc::TypeKind::array;
            t.array_count = uint32_t(a->getSize().getZExtValue());
        }
        ids[s] = t.id;
        module.types.push_back(t);
        return t.id;
    }
    static uint32_t access_flags(clang::AccessSpecifier access) {
        switch (access) {
            case clang::AS_public:    return amc::visibility_flags(amc::Visibility::public_);
            case clang::AS_protected: return amc::visibility_flags(amc::Visibility::protected_);
            case clang::AS_private:   return amc::visibility_flags(amc::Visibility::private_);
            case clang::AS_none:      return amc::visibility_flags(amc::Visibility::none);
        }
        return 0;
    }
    static uint32_t calling_convention(clang::FunctionDecl *d) {
        const auto *prototype = d->getType()->getAs<clang::FunctionProtoType>();
        if (!prototype) return 0;
        switch (prototype->getCallConv()) {
            case clang::CC_C:           return 1;
            case clang::CC_X86StdCall:  return 2;
            case clang::CC_X86FastCall: return 3;
            case clang::CC_X86ThisCall: return 4;
            default:                    return d->hasAttr<clang::AArch64SVEPcsAttr>() ? 5 : 0;
        }
    }
    void add_record(clang::CXXRecordDecl *d) {
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
        record_source(t.id, d->getLocation());
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
            for (uint32_t j = module.types[record_index].field_begin; j < module.fields.size(); ++j) {
                if (module.fields[j].name == fname) {
                    dup = true;
                    break;
                }
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
        module.types[record_index].layout_hash = amc::layout_hash(module.types[record_index], layout_fields);
    }
    void add_enum(clang::EnumDecl *d) {
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
        record_source(t.id, d->getLocation());
    }
    void add_alias(clang::TypedefNameDecl *d) {
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
        record_source(t.id, d->getLocation());
        module.fields.push_back({t.id, "underlying", underlying, 0, 0});
    }
    void add_function(clang::FunctionDecl *d) {
        amc::Function f;
        if (const auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(d)) {
            if (const auto *owner = method->getParent()) {
                f.owner_type = amc::hash_text(name(owner), 0x54595045);
                if (ids.count(name(owner)) == 0) add_record(const_cast<clang::CXXRecordDecl *>(owner));
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
class Consumer : public clang::ASTConsumer {
public:
    Consumer(const std::set<std::string> &w, amc::AbiModule &m)
        : wanted(w)
        , module(m) {}
    void HandleTranslationUnit(clang::ASTContext &c) override {
        Extractor x(c, wanted, module);
        x.TraverseDecl(c.getTranslationUnitDecl());
    }

private:
    const std::set<std::string> &wanted;
    amc::AbiModule &module;
};
class Action : public clang::ASTFrontendAction {
public:
    Action(const std::set<std::string> &w, amc::AbiModule &m)
        : wanted(w)
        , module(m) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &, clang::StringRef) override {
        return std::make_unique<Consumer>(wanted, module);
    }

private:
    const std::set<std::string> &wanted;
    amc::AbiModule &module;
};
class Factory : public clang::tooling::FrontendActionFactory {
public:
    Factory(const std::set<std::string> &w, amc::AbiModule &m)
        : wanted(w)
        , module(m) {}
    std::unique_ptr<clang::FrontendAction> create() override { return std::make_unique<Action>(wanted, module); }

private:
    const std::set<std::string> &wanted;
    amc::AbiModule &module;
};
}   // namespace

int run_frontend(const char *config_path, const char *output_path) {
    amc::lang::Config c;
    std::string e;
    if (!amc::lang::config_load(config_path, "cpp", c, e)) {
        fmt::print(stderr, "{}\n", e);
        return EXIT_FAILURE;
    }
    std::filesystem::path base = std::filesystem::absolute(config_path).parent_path();
    if (!c.db.empty() && std::filesystem::path(c.db).is_relative()) c.db = (base / c.db).string();
    for (auto &f : c.files)
        if (std::filesystem::path(f).is_relative()) f = (base / f).string();
#if defined(__APPLE__)
    append_macos_sysroot(c.flags);
#endif
    std::string db_error;
    std::unique_ptr<clang::tooling::CompilationDatabase> db;
    if (!c.db.empty())
        db = clang::tooling::JSONCompilationDatabase::loadFromFile(
            c.db, db_error, clang::tooling::JSONCommandLineSyntax::AutoDetect);
    else
        db = std::make_unique<clang::tooling::FixedCompilationDatabase>(base.string(), c.flags);
    if (!db) {
        fmt::print(stderr, "{}\n", db_error);
        return EXIT_FAILURE;
    }
    std::set<std::string> wanted(c.symbols.begin(), c.symbols.end());
    amc::AbiModule module;
    module.package_name = "cpp";
    Factory factory(wanted, module);
    clang::tooling::ClangTool tool(*db, c.files);
    if (tool.run(&factory) != 0) {
        fmt::print(stderr, "clang analysis failed\n");
        return EXIT_FAILURE;
    }
    if (!amc::write_abix(module, output_path, e)) {
        fmt::print(stderr, "{}\n", e);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
}   // namespace amc::cpp
