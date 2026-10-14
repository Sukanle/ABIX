#include "AMC/Lang/C++.h"
#include "AMC/Lang/Lang.h"
#include "AMC/Core/Core.h"

// The Clang resource directory detected at build time (`clang -print-resource-dir`).
// It is injected by CMake for the `amc-cpp` target; empty if unavailable.
#ifndef ABIX_CLANG_RESOURCE_DIR
#  define ABIX_CLANG_RESOURCE_DIR ""
#endif

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/RecordLayout.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Type.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Options/OptionUtils.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>

#include <llvm/ADT/APFloat.h>

#include <fmt/base.h>

#include <cctype>
#include <filesystem>
#include <fstream>

namespace amc::lang::cpp {
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
amc::PrimitiveAbiKind signedness_kind(
    bool is_signed, amc::PrimitiveAbiKind signed_kind, amc::PrimitiveAbiKind unsigned_kind) {
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
            abi = amc::primitive_abi(bytes,
                signedness_kind(is_signed, amc::PrimitiveAbiKind::char_signed, amc::PrimitiveAbiKind::char_unsigned));
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
            abi = amc::primitive_abi(bytes,
                signedness_kind(is_signed, amc::PrimitiveAbiKind::wchar_signed, amc::PrimitiveAbiKind::wchar_unsigned));
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
    static bool plain_identifier(const std::string &s) {
        if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
        for (const char c : s)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
        return true;
    }
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
        // Keep the source spelling before canonicalizing: a typedef such as
        // `abix_span_i32` names a concrete instantiation, and that name should
        // be the exported ABI entity even when the alias is not an explicit
        // export symbol.
        const std::string original = qt.getAsString();
        // A typedef declared inside a class is a member typedef (for example
        // `std::allocator<T>::allocator_type`). Its unqualified spelling must
        // never become a global ABI type name, or generated code would emit
        // `TypeTraits<::allocator_type>`, which is not valid at namespace scope.
        // `getAs` looks through sugar (elaborated/qualified spellings), so this
        // finds the typedef even when it is written as a member type.
        bool member_typedef = false;
        if (const auto *typedef_type = qt->getAs<clang::TypedefType>())
            member_typedef = llvm::isa<clang::CXXRecordDecl>(typedef_type->getDecl()->getDeclContext());
        qt = qt.getCanonicalType();
        auto s = qt.getAsString();
        auto it = ids.find(s);
        if (it != ids.end()) return it->second;
        if (original != s) {
            auto alias = ids.find(original);
            if (alias != ids.end()) {
                ids.emplace(s, alias->second);
                return alias->second;
            }
        }
        if (qt->isDependentType()) {
            amc::Type t;
            t.name = s;
            t.id = amc::hash_text(s, 0x54595045);
            t.size = t.align = 1;
            ids.emplace(s, t.id);
            module.types.push_back(std::move(t));
            return ids.at(s);
        }
        // A concrete template specialization: when the instantiation is complete
        // (the frontend owns instantiation), record its real layout and fields.
        // Otherwise fall back to an opaque record so a reference never crashes.
        if (qt->getAs<clang::TemplateSpecializationType>()) {
            // When the specialization was reached through a typedef alias (for
            // example `abix_span_i32`), use the alias as the exported ABI name.
            const bool alias_named = !member_typedef && original != s && plain_identifier(original);
            if (auto *record = qt->getAsCXXRecordDecl(); record && record->isCompleteDefinition()) {
                add_record(record, alias_named ? original : std::string());
                const auto found = ids.find(alias_named ? original : name(record));
                if (found != ids.end()) {
                    ids[s] = found->second;
                    return found->second;
                }
            }
            const std::string type_name = alias_named ? original : s;
            amc::Type t;
            t.name = type_name;
            t.kind = amc::TypeKind::record;
            t.id = amc::hash_text(type_name, 0x54595045);
            t.size = 1;
            t.align = 1;
            ids.emplace(s, t.id);
            if (alias_named) ids.emplace(original, t.id);
            module.types.push_back(std::move(t));
            return ids.at(s);
        }
        if (const auto *record = qt->getAsCXXRecordDecl()) {
            const auto record_key = qt.getAsString();
            // A typedef alias of an instantiation (`abix_span_i32`) names the
            // concrete ABI entity; record it under the alias so it can be
            // lowered without an explicit export symbol. Restrict this to class
            // template specializations: a nested type or a plain typedef spelled
            // unqualified inside its scope (e.g. `reader` inside `Domain`, or
            // `LayoutHash` for `Hash128`) also differs from its canonical
            // spelling but is not an alias, and renaming the record after it
            // would produce a type name that is not valid in any namespace.
            const bool alias_named = !member_typedef
                                  && original != s
                                  && plain_identifier(original)
                                  && llvm::isa<clang::ClassTemplateSpecializationDecl>(record);
            add_record(const_cast<clang::CXXRecordDecl *>(record), alias_named ? original : std::string());
            auto found = ids.find(alias_named ? original : record_key);
            if (found != ids.end()) {
                ids[s] = found->second;
                return found->second;
            }
            found = ids.find(name(record));
            if (found != ids.end()) {
                ids[s] = found->second;
                return found->second;
            }
            // Incomplete record (a forward declaration such as `struct Counter;`):
            // record it as opaque and register it so a self-referential reference
            // terminates instead of recursing forever. cv-qualifiers are dropped
            // so `DemoBuffer` and `const DemoBuffer` share one opaque type.
            const std::string opaque_name = name(record);
            auto existing = ids.find(opaque_name);
            if (existing != ids.end()) {
                ids.emplace(s, existing->second);
                return existing->second;
            }
            amc::Type t;
            t.name = opaque_name;
            t.kind = amc::TypeKind::record;
            t.id = amc::hash_text(opaque_name, 0x54595045);
            t.size = 1;
            t.align = 1;
            ids.emplace(s, t.id);
            ids.emplace(opaque_name, t.id);
            module.types.push_back(std::move(t));
            return ids.at(s);
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
        clang::QualType pointee_type;
        bool is_pointer = false;
        if (const auto *p = qt->getAs<clang::PointerType>()) {
            t.kind = amc::TypeKind::pointer;
            t.name = p->getPointeeType().getAsString() + "*";
            t.size = ctx.getTypeSize(qt) / 8;
            t.align = ctx.getTypeAlign(qt) / 8;
            is_pointer = true;
            pointee_type = p->getPointeeType();
        }
        if (const auto *a = llvm::dyn_cast<clang::ConstantArrayType>(qt.getTypePtr())) {
            t.kind = amc::TypeKind::array;
            t.array_count = uint32_t(a->getSize().getZExtValue());
        }
        ids[s] = t.id;
        module.types.push_back(t);
        // Record the pointee AFTER registering this pointer, so a self-referential
        // type (a node with a pointer to itself) terminates instead of recursing.
        if (is_pointer && !pointee_type->isFunctionType() && !pointee_type->isVoidType()) {
            const amc::Hash128 pointee_id = add_type(pointee_type);
            const uint32_t flags = pointee_type.isConstQualified() ? amc::pointee_const : 0u;
            if (!(pointee_id == amc::Hash128{})) module.pointees.push_back({t.id, pointee_id, flags});
        }
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
    void add_record(clang::CXXRecordDecl *d, const std::string &forced_name = std::string()) {
        const auto record_name = forced_name.empty() ? name(d) : forced_name;
        if (ids.count(record_name) != 0) return;
        if (d->isDependentContext() || ctx.getCanonicalTagType(d)->isDependentType()) return;
        if (!d->isCompleteDefinition()) return;
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
        ids[record_name] = t.id;
        // Also resolve references spelled the Clang way (e.g. a specialization
        // named `abix_span<int>` exported under the alias `abix_span_i32`).
        ids[name(d)] = t.id;
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
        const auto underlying_type = d->getUnderlyingType();
        // An alias of a template instantiation names a concrete ABI entity:
        // record the instantiated record's layout under the alias name, so the
        // exported name is stable and valid in every target language. This is the
        // `span<int> -> abix_span_i32` step; it does not by itself claim the type
        // satisfies any ABIX-STD contract.
        if (auto *record = underlying_type.getCanonicalType()->getAsCXXRecordDecl();
            record && record->isCompleteDefinition() && llvm::isa<clang::ClassTemplateSpecializationDecl>(record)) {
            add_record(record, alias_name);
            const auto found = ids.find(alias_name);
            if (found != ids.end()) {
                ids[underlying_type.getAsString()] = found->second;
                ids[underlying_type.getCanonicalType().getAsString()] = found->second;
                return;
            }
        }
        amc::Type t;
        t.name = alias_name;
        t.kind = amc::TypeKind::alias;
        t.id = amc::hash_text(t.name, 0x54595045);
        // Guard against incomplete underlying types (an uninstantiated template
        // must not be queried for its layout).
        if (underlying_type->isIncompleteType()) {
            t.size = t.align = 1;
        } else {
            t.size = uint32_t(ctx.getTypeSize(underlying_type) / 8);
            t.align = uint32_t(ctx.getTypeAlign(underlying_type) / 8);
        }
        ids[t.name] = t.id;
        const auto underlying = add_type(underlying_type);
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
// P0 automatic capability generation: emit C thunks that expose selected
// methods of a concrete type, then register them as ABI entities so any
// consumer backend can bind them. The producer compiles the generated source
// into its module; neither side writes an adapter by hand.
//
// Only public members are supported, and only representations the bridge can
// express: a parameter or return type that is not a known, layout-safe type is
// rejected rather than guessed at.
bool expose_thunks(
    amc::AbiModule &module, const amc::lang::Config &config, const std::filesystem::path &base, std::string &error) {
    if (config.exposed.empty()) return true;
    if (config.thunks_output.empty()) {
        error = "[[expose]] requires a [thunks] output path";
        return false;
    }
    uint32_t pointer_size = 8;
    for (const auto &type : module.types)
        if (type.kind == amc::TypeKind::pointer) {
            pointer_size = type.size;
            break;
        }
    const auto intern = [&](const std::string &name, amc::TypeKind kind, uint32_t size, uint32_t align,
                            uint32_t primitive) -> amc::Hash128 {
        for (const auto &type : module.types)
            if (type.name == name) return type.id;
        amc::Type type;
        type.name = name;
        type.kind = kind;
        type.size = size;
        type.align = align;
        type.primitive_abi = primitive;
        type.id = amc::hash_text(name, 0x54595045);
        module.types.push_back(std::move(type));
        return module.types.back().id;
    };
    const amc::Hash128 void_id =
        intern("void", amc::TypeKind::primitive, 0, 1, amc::primitive_abi(0, amc::PrimitiveAbiKind::void_));
    const auto type_name = [&](amc::Hash128 id) -> std::string {
        for (const auto &type : module.types)
            if (type.id == id) return type.name;
        return {};
    };
    // A boundary type is representable without an explicit contract when it is a
    // scalar, a pointer, an enumeration, an array, an ABIX-STD boundary type, or
    // a plain value record. STL types and template specializations are not:
    // passing them raw across a C ABI is unsafe, so they require a contract (for
    // example a StringView mapping) instead of guessing.
    const auto representable = [&](amc::Hash128 id) -> bool {
        for (const auto &type : module.types) {
            if (!(type.id == id)) continue;
            const std::string &name = type.name;
            if (name.rfind("abix_", 0) == 0) return true;   // ABIX-STD boundary type
            // STL types, template spellings and references must not cross the
            // boundary raw (they need a contract such as a StringView mapping).
            if (name.find("std::") != std::string::npos) return false;
            if (name.find('<') != std::string::npos) return false;
            if (name.find('&') != std::string::npos) return false;
            return true;
        }
        return false;
    };
    // A recorded primitive may carry a C spelling (e.g. `_Bool`); the generated
    // C++ thunk needs the C++ spelling.
    const auto cpp_spelling = [](std::string name) {
        if (name == "_Bool") return std::string("bool");
        return name;
    };
    const auto symbol_of = [](const std::string &name) {
        std::string out;
        for (const char c : name)
            out += (std::isalnum(static_cast<unsigned char>(c)) != 0) ? c : '_';
        return out;
    };

    std::string source =
        "// Generated by `amc` - C thunks exposing C++ types as ABIX capabilities.\n"
        "// Build artifact: compile it into the producing module. Do not edit.\n"
        "#include \"ABIX/Interface/Interface.h\"\n";
    for (const auto &file : config.files)
        source += "#include \"" + file + "\"\n";
    source += "\n";
    // Interface manifest glue (discovered through `abix_module_query_v1`).
    std::string manifest;
    std::vector<std::string> interface_names;

    // Automatic ABIX-STD contract mapping: when an exposed method uses an
    // ABIX-STD boundary type, record its contract (SequenceView / StringView /
    // Result) so a consumer knows the semantics, not just the layout.
    std::vector<std::pair<amc::Hash128, uint32_t>> recorded_contracts;
    const auto record_contract = [&](amc::Hash128 id) {
        const std::string name = type_name(id);
        amc::LoweringResult result;
        if (name.rfind("abix_span_", 0) == 0)
            result = amc::evaluate_sequence_view(module, id, amc::Hash128{}, "", false);
        else if (name.rfind("abix_str_", 0) == 0)
            result = amc::evaluate_string_view(module, id, amc::Hash128{}, "", false);
        else if (name.rfind("abix_result_", 0) == 0)
            result = amc::evaluate_result(module, id, amc::Hash128{}, "");
        else
            return;
        for (const auto &seen : recorded_contracts)
            if (seen.first == id && seen.second == result.kind) return;
        recorded_contracts.emplace_back(id, result.kind);
        module.lowerings.push_back(result);
    };

    // Methods may be inherited: collect a type's base classes so an exposed
    // concrete type can list methods declared on a base (e.g. MemoryHistory's
    // insert/length/clear come from History).
    const auto collect_bases = [&](amc::Hash128 root) {
        std::vector<amc::Hash128> out;
        std::vector<amc::Hash128> work{root};
        while (!work.empty()) {
            const amc::Hash128 id = work.back();
            work.pop_back();
            for (const auto &type : module.types) {
                if (!(type.id == id)) continue;
                for (uint32_t i = 0; i < type.field_count; ++i) {
                    const auto &field = module.fields[type.field_begin + i];
                    if ((field.flags & amc::field_base) == 0) continue;
                    if (std::find(out.begin(), out.end(), field.type_id) != out.end()) continue;
                    out.push_back(field.type_id);
                    work.push_back(field.type_id);
                }
            }
        }
        return out;
    };

    for (const auto &expose : config.exposed) {
        amc::Type *record = nullptr;
        for (auto &type : module.types)
            if (type.name == expose.type || type.name == "::" + expose.type) {
                record = &type;
                break;
            }
        if (record == nullptr) {
            error = "[[expose]] references an unknown type: " + expose.type;
            return false;
        }
        const amc::Hash128 type_id = record->id;
        const std::string cpp_type = record->name;
        const std::string symbol_type = symbol_of(record->name);
        if (expose.opaque) record->flags |= amc::type_opaque;
        const std::vector<amc::Hash128> bases = collect_bases(type_id);
        const auto is_method_of_type = [&](const amc::Function &function) {
            if (function.owner_type == type_id) return true;
            return std::find(bases.begin(), bases.end(), function.owner_type) != bases.end();
        };
        const amc::Hash128 self_id = intern(cpp_type + "*", amc::TypeKind::pointer, pointer_size, pointer_size, 0);

        const auto register_thunk = [&](const std::string &name, amc::Hash128 ret,
                                        const std::vector<std::pair<std::string, amc::Hash128>> &params) {
            amc::Function function;
            function.name = name;
            function.calling_convention = 1;   // cdecl
            function.return_type = ret;
            for (const auto &parameter : params) {
                amc::Parameter p;
                p.name = parameter.first;
                p.type_id = parameter.second;
                function.parameters.push_back(std::move(p));
            }
            function.signature = amc::signature_hash(function);
            module.functions.push_back(std::move(function));
            return module.functions.back().signature;
        };

        amc::Hash128 create_sig{}, destroy_sig{};
        if (expose.has_constructor) {
            std::string params, args;
            std::vector<std::pair<std::string, amc::Hash128>> signature;
            for (size_t i = 0; i < expose.constructor.size(); ++i) {
                const std::string &spelling = expose.constructor[i];
                const amc::Hash128 id = amc::find_type_id_by_spelling(module, spelling);
                if (id == amc::Hash128{}) {
                    error = "[[expose]] constructor parameter type is unknown: " + spelling;
                    return false;
                }
                if (!representable(id)) {
                    error = "[[expose]] constructor parameter type needs a contract: " + spelling;
                    return false;
                }
                if (i != 0) {
                    params += ", ";
                    args += ", ";
                }
                const std::string name = "arg" + std::to_string(i);
                params += spelling + " " + name;
                args += name;
                signature.emplace_back(name, id);
            }
            source += "extern \"C\" " + cpp_type + "* abix_" + symbol_type + "_create(" + params + ") {\n";
            source += "    return new " + cpp_type + "(" + args + ");\n}\n";
            create_sig = register_thunk("abix_" + symbol_type + "_create", self_id, signature);
        }
        if (expose.destructor) {
            source += "extern \"C\" void abix_" + symbol_type + "_destroy(" + cpp_type + "* self) {\n";
            source += "    delete self;\n}\n";
            destroy_sig = register_thunk("abix_" + symbol_type + "_destroy", void_id,
                {
                    {"self", self_id}
            });
        }
        if (!(create_sig == amc::Hash128{}) && !(destroy_sig == amc::Hash128{}))
            module.owned.push_back({type_id, create_sig, destroy_sig, 0});

        struct ExposedMethod {
            std::string symbol;
            amc::Hash128 signature;
        };
        std::vector<ExposedMethod> exposed_methods;
        for (const auto &method : expose.methods) {
            const amc::Function *found = nullptr;
            for (const auto &function : module.functions) {
                if (!is_method_of_type(function)) continue;
                if (function.name == method
                    || function.name.size() > method.size()
                           && function.name.compare(function.name.size() - method.size(), method.size(), method) == 0
                           && function.name[function.name.size() - method.size() - 1] == ':') {
                    found = &function;
                    break;
                }
            }
            if (found == nullptr) {
                error = "[[expose]] references an unknown method: " + expose.type + "::" + method;
                return false;
            }
            if (!representable(found->return_type)) {
                error = "[[expose]] "
                      + expose.type
                      + "::"
                      + method
                      + " return type needs a contract: "
                      + type_name(found->return_type);
                return false;
            }
            for (const auto &parameter : found->parameters)
                if (!representable(parameter.type_id)) {
                    error = "[[expose]] "
                          + expose.type
                          + "::"
                          + method
                          + " parameter type needs a contract: "
                          + type_name(parameter.type_id);
                    return false;
                }
            const bool returns_void = found->return_type == void_id;
            std::string return_spelling = returns_void ? "void" : cpp_spelling(type_name(found->return_type));
            std::string params = cpp_type + "* self";
            std::string args;
            std::vector<std::pair<std::string, amc::Hash128>> signature = {
                {"self", self_id}
            };
            for (size_t i = 0; i < found->parameters.size(); ++i) {
                const auto &parameter = found->parameters[i];
                const std::string name = parameter.name.empty() ? "arg" + std::to_string(i) : parameter.name;
                params += ", " + cpp_spelling(type_name(parameter.type_id)) + " " + name;
                if (i != 0) args += ", ";
                args += name;
                signature.emplace_back(name, parameter.type_id);
            }
            source +=
                "extern \"C\" " + return_spelling + " abix_" + symbol_type + "_" + method + "(" + params + ") {\n";
            source +=
                std::string("    ") + (returns_void ? "" : "return ") + "self->" + method + "(" + args + ");\n}\n";
            const std::string symbol = "abix_" + symbol_type + "_" + method;
            exposed_methods.push_back({symbol, register_thunk(symbol, found->return_type, signature)});
            // Record the ABIX-STD contracts of the boundary types in this
            // signature (span -> SequenceView, str -> StringView, result).
            record_contract(found->return_type);
            for (const auto &parameter : found->parameters)
                record_contract(parameter.type_id);
        }
        source += "\n";

        // One interface per exposed type: each method binds its thunk by a
        // stable slot, so the runtime bus can dispatch without a vtable.
        if (!exposed_methods.empty()) {
            const amc::Hash128 interface_id = amc::hash_text("abix-interface:" + cpp_type, 0x494e5446ULL);
            const auto hex = [](uint64_t v) { return std::to_string(v) + "ULL"; };
            const std::string methods_array = "abix_" + symbol_type + "_methods";
            manifest += "const skl::abix::poly::InterfaceMethod " + methods_array + "[] = {\n";
            for (size_t i = 0; i < exposed_methods.size(); ++i) {
                manifest += "    {{"
                          + hex(exposed_methods[i].signature.lo)
                          + ", "
                          + hex(exposed_methods[i].signature.hi)
                          + "}, skl::abix::poly::interface_entry(&"
                          + exposed_methods[i].symbol
                          + "), "
                          + std::to_string(i)
                          + ", 0},\n";
            }
            manifest += "};\n";
            const std::string interface_name = "abix_" + symbol_type + "_interface";
            manifest += "const skl::abix::poly::InterfaceManifestEntry "
                      + interface_name
                      + " = {{"
                      + hex(interface_id.lo)
                      + ", "
                      + hex(interface_id.hi)
                      + "}, 1, 0, "
                      + methods_array
                      + ", "
                      + std::to_string(exposed_methods.size())
                      + ", 0};\n";
            interface_names.push_back(interface_name);
        }
    }

    if (!interface_names.empty()) {
        manifest += "namespace {\nconst skl::abix::poly::InterfaceManifestEntry abix_module_interfaces[] = {\n";
        for (const auto &name : interface_names)
            manifest += "    " + name + ",\n";
        manifest += "};\nconst skl::abix::poly::ModuleManifest abix_module_manifest = {\n"
                    "    skl::abix::poly::abix_manifest_version, 1, 0, "
                  + std::to_string(interface_names.size())
                  + ", abix_module_interfaces, 0, 0, nullptr};\n}\n"
                    "SKL_ABIX_DEFINE_MODULE_QUERY(&abix_module_manifest)\n";
    }
    source += manifest;

    const std::filesystem::path path = base / config.thunks_output;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot write thunk source: " + path.string();
        return false;
    }
    out.write(source.data(), static_cast<std::streamsize>(source.size()));
    if (!out) {
        error = "cannot write thunk source: " + path.string();
        return false;
    }
    return true;
}

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
    // An exposed type and its methods must be recorded even when they are not
    // explicit export symbols: the generator needs their layout and signatures.
    for (const auto &expose : c.exposed) {
        wanted.insert(expose.type);
        for (const auto &method : expose.methods)
            wanted.insert(expose.type + "::" + method);
    }
    amc::AbiModule module;
    module.package_name = "cpp";

    // Generic instantiation protocol, stage A/B: AMC coordinates the request and
    // the C++ frontend (Clang/Sema) performs the instantiation and constraint
    // check. We force it with an anchor translation unit that requires each
    // requested instantiation to be complete, so a constraint failure surfaces
    // as a normal compiler diagnostic instead of a silent opaque record.
    std::vector<std::string> tool_files = c.files;
    std::string anchor_path;
    std::string anchor;
    if (!c.instantiations.empty()) {
        for (const auto &file : c.files)
            anchor += "#include \"" + file + "\"\n";
        for (const auto &inst : c.instantiations) {
            std::string concrete = inst.template_name + "<";
            for (size_t i = 0; i < inst.args.size(); ++i) {
                if (i != 0) concrete += ", ";
                concrete += inst.args[i];
            }
            concrete += ">";
            if (!inst.as.empty()) anchor += "using " + inst.as + " = " + concrete + ";\n";
            anchor += "static_assert(sizeof(" + concrete + ") > 0, \"abix instantiate\");\n";
        }
        anchor_path = (base / ".amc" / "instantiate.hpp").string();
        std::error_code ec;
        std::filesystem::create_directories((base / ".amc").string(), ec);
        tool_files = {anchor_path};
    }

    Factory factory(wanted, module);
    clang::tooling::ClangTool tool(*db, tool_files);
    if (!anchor_path.empty()) tool.mapVirtualFile(anchor_path, anchor);
    // Report the include closure so AMC's instantiation cache can invalidate on
    // any dependency (including headers it does not otherwise know about). The
    // depfile is written next to the artifact as `<output>.d`.
    const std::string depfile = std::string(output_path) + ".d";
    const std::string resource_dir = ABIX_CLANG_RESOURCE_DIR;
    tool.appendArgumentsAdjuster(
        [depfile, resource_dir](const clang::tooling::CommandLineArguments &cmd, llvm::StringRef) {
            clang::tooling::CommandLineArguments out;
            // Always parse as C++. Without this a bare `.h` is treated as a C header
            // and rejects `-std=c++17`. `cmd[0]` is the compiler word, so the `-x`
            // selector must come after it (before the flags and the input).
            if (!cmd.empty()) {
                out.push_back(cmd[0]);
                out.push_back("-x");
                out.push_back("c++");
                out.insert(out.end(), cmd.begin() + 1, cmd.end());
            } else {
                out = cmd;
            }
            // Point the driver at its builtin headers (<stddef.h>, ...). ClangTool
            // does not locate the resource directory on its own, so a header that
            // includes <stddef.h> fails with "file not found".
            if (!resource_dir.empty()) out.push_back("-resource-dir=" + resource_dir);
            // Report the include closure so AMC's instantiation cache can invalidate
            // on any dependency (including headers it does not otherwise know about).
            out.push_back("-MD");
            out.push_back("-MF" + depfile);
            out.push_back("-MT");
            out.push_back(depfile);
            return out;
        });
    if (tool.run(&factory) != 0) {
        fmt::print(stderr, "clang analysis failed\n");
        return EXIT_FAILURE;
    }
    if (!expose_thunks(module, c, base, e)) {
        fmt::print(stderr, "{}\n", e);
        return EXIT_FAILURE;
    }
    if (!amc::write_abix(module, output_path, e)) {
        fmt::print(stderr, "{}\n", e);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
}   // namespace amc::lang::cpp
