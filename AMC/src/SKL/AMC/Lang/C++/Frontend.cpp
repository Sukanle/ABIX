#include "SKL/AMC/Lang/C++.h"
#include "SKL/AMC/Lang/Lang.h"
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Core/Generated.h"

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
        // share both. The width is in bits (`prim:i32`), matching the `i8`/`u64`
        // spelling the rest of the toolchain uses.
        abi = amc::primitive_abi(bytes, is_signed ? amc::PrimitiveAbiKind::sint : amc::PrimitiveAbiKind::uint,
            amc::FloatFormat::none, is_signed);
        return std::string(is_signed ? "prim:i" : "prim:u") + std::to_string(bytes * 8);
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

// The native C++ kinds an `[[adapt]]` may declare. Each maps a canonical STL
// spelling onto an ABIX-STD boundary contract through a generated adapter.
enum class NativeKind : uint32_t {
    none = 0,
    string = 1,        // std::string        -> string-view (owned copy in the adapter)
    string_view = 2,   // std::string_view   -> string-view (borrowed)
    vector = 3,        // std::vector<T>     -> sequence-view (owned copy in the adapter)
    span = 4           // std::span<const T> -> sequence-view (borrowed)
};

// One recorded native↔ABIX-STD parameter adaptation. The function keeps the
// native parameter type (the method's real signature); `[[expose]]` substitutes
// the boundary type in the generated thunk and emits the marshalling step. This
// is a frontend working set consumed by `expose_thunks`; it is not serialized
// and never feeds ABI identity.
struct NativeAdapt {
    amc::Hash128 native_type{};     // recorded native parameter type
    amc::Hash128 boundary_type{};   // ABIX-STD boundary type (filled by expose)
    amc::Hash128 element_id{};      // element/value type for a sequence view
    uint32_t kind = 0;              // NativeKind
    std::string element;            // element spelling (sequence views)
    std::string target;             // declared boundary type name
};

NativeKind native_kind_of(const std::string &name) {
    if (name == "std::string") return NativeKind::string;
    if (name == "std::string_view") return NativeKind::string_view;
    if (name == "std::vector") return NativeKind::vector;
    if (name == "std::span") return NativeKind::span;
    return NativeKind::none;
}

// Normalize a type spelling for element matching: drop whitespace and the
// qualifiers that do not change the element's ABI identity.
std::string normalise_element(const std::string &spelling) {
    std::string out;
    std::string token;
    const auto flush = [&] {
        if (token.empty()) return;
        if (token != "const" && token != "class" && token != "struct" && token != "volatile" && token != "typename") {
            if (!out.empty()) out += ' ';
            out += token;
        }
        token.clear();
    };
    for (const char c : spelling) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == '&') {
            flush();
            continue;
        }
        token += c;
    }
    flush();
    return out;
}

// The native type name recorded for a native parameter (distinct per element,
// so `std::vector<int>` and `std::vector<float>` never collapse onto one type).
std::string native_type_name(NativeKind kind, const std::string &element) {
    switch (kind) {
        case NativeKind::string:      return "std::string";
        case NativeKind::string_view: return "std::string_view";
        case NativeKind::vector:      return "std::vector<" + element + ">";
        case NativeKind::span:        return "std::span<" + element + ">";
        default:                      return {};
    }
}

// Parse a fixed-width integer spelling (`int32_t`, `uint16_t`, ...) into its
// byte size and signedness. Returns false when the spelling is not one, so the
// matcher never guesses at an element type it cannot prove.
bool fixed_width_int(const std::string &spelling, uint32_t &size, bool &is_signed) {
    struct Entry {
        const char *name;
        uint32_t size;
        bool is_signed;
    };
    static constexpr Entry kEntries[] = {
        {  "int8_t", 1,  true},
        { "uint8_t", 1, false},
        { "int16_t", 2,  true},
        {"uint16_t", 2, false},
        { "int32_t", 4,  true},
        {"uint32_t", 4, false},
        { "int64_t", 8,  true},
        {"uint64_t", 8, false},
    };
    const std::string name = normalise_element(spelling);
    for (const auto &entry : kEntries)
        if (name == entry.name) {
            size = entry.size;
            is_signed = entry.is_signed;
            return true;
        }
    return false;
}

class Extractor : public clang::RecursiveASTVisitor<Extractor> {
public:
    Extractor(clang::ASTContext &c, const std::set<std::string> &wanted, amc::AbiModule &m,
        const std::vector<amc::lang::AdaptDecl> &a, std::vector<NativeAdapt> &out)
        : ctx(c)
        , wanted(wanted)
        , module(m)
        , adapts(a)
        , adaptations(out) {}
    bool VisitRecordDecl(clang::RecordDecl *d) {
        if (amc::lang::cancellation_requested()) return false;
        auto *cxx = llvm::dyn_cast<clang::CXXRecordDecl>(d);
        if (!cxx
            || d->isImplicit()
            || !d->isThisDeclarationADefinition()
            || (!selected(d) && !has_selected_member(cxx)))
            return true;
        add_record(cxx);
        add_inherited_methods(cxx);
        return true;
    }
    bool VisitEnumDecl(clang::EnumDecl *d) {
        if (amc::lang::cancellation_requested()) return false;
        if (d->isCompleteDefinition() && selected(d)) add_enum(d);
        return true;
    }
    bool VisitTypedefNameDecl(clang::TypedefNameDecl *d) {
        if (amc::lang::cancellation_requested()) return false;
        if (selected(d)) add_alias(d);
        return true;
    }
    bool VisitDecl(clang::Decl *d) {
        if (amc::lang::cancellation_requested()) return false;
        if (auto *fn = llvm::dyn_cast<clang::FunctionDecl>(d))
            if (selected(fn)) add_function(fn);
        return true;
    }

private:
    clang::ASTContext &ctx;
    const std::set<std::string> &wanted;
    amc::AbiModule &module;
    const std::vector<amc::lang::AdaptDecl> &adapts;
    std::vector<NativeAdapt> &adaptations;
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
        module.types[record_index].layout_hash =
            amc::canonical_layout_hash(module, module.types[record_index], layout_fields);
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
    // Record methods an exposed type inherits from a base class. The exposed
    // type's own declarations are recorded through `wanted` (`X::m`); a method
    // declared on a base `B` (`B::m`) is not, so walk the base chain and record
    // any method whose name is one of the type's requested methods. A name a
    // more-derived class already declares shadows the base one (the thunk calls
    // `self->name`, so name lookup picks the override anyway).
    void add_inherited_methods(clang::CXXRecordDecl *d) {
        const std::string prefix = name(d) + "::";
        std::vector<std::string> methods;
        for (const auto &symbol : wanted)
            if (symbol.compare(0, prefix.size(), prefix) == 0) methods.push_back(symbol.substr(prefix.size()));
        if (methods.empty() || !d->isCompleteDefinition()) return;
        std::set<std::string> shadowed;
        for (const auto *method : d->methods())
            shadowed.insert(method->getNameAsString());
        std::vector<clang::CXXRecordDecl *> work;
        for (const auto &base : d->bases())
            if (auto *base_record = base.getType()->getAsCXXRecordDecl()) work.push_back(base_record);
        while (!work.empty()) {
            clang::CXXRecordDecl *base = work.back();
            work.pop_back();
            const std::string base_name = name(base);
            for (const auto *method : base->methods()) {
                const std::string method_name = method->getNameAsString();
                if (std::find(methods.begin(), methods.end(), method_name) == methods.end()) continue;
                if (shadowed.count(method_name) != 0) continue;
                const std::string qualified = base_name + "::" + method_name;
                bool recorded = false;
                for (const auto &function : module.functions)
                    if (function.name == qualified) {
                        recorded = true;
                        break;
                    }
                if (!recorded) add_function(const_cast<clang::CXXMethodDecl *>(method));
            }
            for (const auto *method : base->methods())
                shadowed.insert(method->getNameAsString());
            for (const auto &up : base->bases())
                if (auto *up_record = up.getType()->getAsCXXRecordDecl()) work.push_back(up_record);
        }
    }

    // Record a native (STL) type by its canonical spelling, so a parameter type
    // that must not cross the boundary raw still has a stable identity for the
    // diagnosis and the adaptation record.
    amc::Hash128 register_native_type(const std::string &record_name) {
        auto found = ids.find(record_name);
        if (found != ids.end()) return found->second;
        amc::Type t;
        t.name = record_name;
        t.kind = amc::TypeKind::record;
        t.id = amc::hash_text(record_name, 0x54595045);
        t.size = t.align = 1;
        ids.emplace(record_name, t.id);
        module.types.push_back(std::move(t));
        return module.types.back().id;
    }

    // Detect whether a parameter type is a native type with a declared
    // `[[adapt]]`. The mapping is explicit (ABI-SPEC §18): an STL type is never
    // adapted unless a matching declaration exists. `element` is the sequence
    // value type for a vector/span; read-only borrowed views only, so a
    // non-const lvalue reference or a mutable `std::span<T>` is not adapted
    // (writing back through a borrowed view is not modelled).
    bool detect_native(
        clang::QualType qt, const amc::lang::AdaptDecl *&decl, NativeKind &kind, clang::QualType &element) {
        decl = nullptr;
        kind = NativeKind::none;
        const bool lvalue_ref = qt->isLValueReferenceType();
        if (lvalue_ref && !qt.getNonReferenceType().isConstQualified()) return false;
        const clang::QualType value = qt.getNonReferenceType().getUnqualifiedType();
        const auto *record = value->getAsCXXRecordDecl();
        if (record == nullptr) return false;
        const std::string qualified = record->getQualifiedNameAsString();
        NativeKind candidate = NativeKind::none;
        if (qualified == "std::basic_string")
            candidate = NativeKind::string;
        else if (qualified == "std::basic_string_view")
            candidate = NativeKind::string_view;
        else if (qualified == "std::vector")
            candidate = NativeKind::vector;
        else if (qualified == "std::span")
            candidate = NativeKind::span;
        else
            return false;
        const auto *spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record);
        if (spec == nullptr) return false;
        const auto &args = spec->getTemplateArgs();
        if (args.size() < 1 || args[0].getKind() != clang::TemplateArgument::Type) return false;
        if (candidate == NativeKind::string || candidate == NativeKind::string_view) {
            // The code unit must be C `char`; the boundary view is UTF-8 bytes.
            const clang::QualType unit = args[0].getAsType().getUnqualifiedType();
            if (!unit->isCharType() || ctx.getTypeSize(unit) != 8) return false;
            element = unit;
        } else {
            const clang::QualType value_type = args[0].getAsType();
            if (candidate == NativeKind::span && !value_type.isConstQualified()) return false;
            element = value_type.getUnqualifiedType();
        }
        for (const auto &adapt : adapts) {
            if (native_kind_of(adapt.native) != candidate) continue;
            if ((candidate == NativeKind::vector || candidate == NativeKind::span)
                && !element_matches(adapt.element, element))
                continue;
            decl = &adapt;
            kind = candidate;
            return true;
        }
        return false;
    }

    // An element spelling matches by normalized spelling (sugared or canonical),
    // or by the ABI of a fixed-width integer alias (`int32_t` matches `int`).
    bool element_matches(const std::string &declared, clang::QualType element) const {
        if (declared.empty()) return false;
        if (normalise_element(declared) == normalise_element(element.getAsString())
            || normalise_element(declared) == normalise_element(element.getCanonicalType().getAsString()))
            return true;
        uint32_t size = 0;
        bool is_signed = false;
        if (!fixed_width_int(declared, size, is_signed)) return false;
        const clang::QualType canonical = element.getCanonicalType();
        return canonical->isIntegerType()
            && ctx.getTypeSize(canonical) / 8 == size
            && canonical->isSignedIntegerType() == is_signed;
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
            const amc::lang::AdaptDecl *decl = nullptr;
            NativeKind kind = NativeKind::none;
            clang::QualType element;
            if (detect_native(p->getType(), decl, kind, element)) {
                NativeAdapt adapt;
                adapt.kind = static_cast<uint32_t>(kind);
                adapt.element = element.getAsString();
                adapt.element_id = add_type(element);
                adapt.target = decl->target;
                adapt.native_type = register_native_type(native_type_name(kind, adapt.element));
                adaptations.push_back(std::move(adapt));
                q.type_id = adaptations.back().native_type;
            } else {
                q.type_id = add_type(p->getType());
            }
            f.parameters.push_back(q);
        }
        f.signature = amc::signature_hash(f);
        module.functions.push_back(std::move(f));
    }
};
class Consumer : public clang::ASTConsumer {
public:
    Consumer(const std::set<std::string> &w, amc::AbiModule &m, const std::vector<amc::lang::AdaptDecl> &a,
        std::vector<NativeAdapt> &out)
        : wanted(w)
        , module(m)
        , adapts(a)
        , adaptations(out) {}
    void HandleTranslationUnit(clang::ASTContext &c) override {
        if (amc::lang::cancellation_requested()) return;
        Extractor x(c, wanted, module, adapts, adaptations);
        x.TraverseDecl(c.getTranslationUnitDecl());
    }

private:
    const std::set<std::string> &wanted;
    amc::AbiModule &module;
    const std::vector<amc::lang::AdaptDecl> &adapts;
    std::vector<NativeAdapt> &adaptations;
};
class Action : public clang::ASTFrontendAction {
public:
    Action(const std::set<std::string> &w, amc::AbiModule &m, const std::vector<amc::lang::AdaptDecl> &a,
        std::vector<NativeAdapt> &out)
        : wanted(w)
        , module(m)
        , adapts(a)
        , adaptations(out) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &, clang::StringRef) override {
        return std::make_unique<Consumer>(wanted, module, adapts, adaptations);
    }

private:
    const std::set<std::string> &wanted;
    amc::AbiModule &module;
    const std::vector<amc::lang::AdaptDecl> &adapts;
    std::vector<NativeAdapt> &adaptations;
};
class Factory : public clang::tooling::FrontendActionFactory {
public:
    Factory(const std::set<std::string> &w, amc::AbiModule &m, const std::vector<amc::lang::AdaptDecl> &a,
        std::vector<NativeAdapt> &out)
        : wanted(w)
        , module(m)
        , adapts(a)
        , adaptations(out) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<Action>(wanted, module, adapts, adaptations);
    }

private:
    const std::set<std::string> &wanted;
    amc::AbiModule &module;
    const std::vector<amc::lang::AdaptDecl> &adapts;
    std::vector<NativeAdapt> &adaptations;
};
// P0 automatic capability generation: emit C thunks that expose selected
// methods of a concrete type, then register them as ABI entities so any
// consumer backend can bind them. The producer compiles the generated source
// into its module; neither side writes an adapter by hand.
//
// Only public members are supported, and only representations the bridge can
// express: a parameter or return type that is not a known, layout-safe type is
// rejected rather than guessed at.
// Spelling for a generated `#include`: prefer the path relative to the config
// directory, then relative to any `-I` directory declared in the import flags,
// and only fall back to the declared string. This keeps generated thunks free
// of machine-specific absolute prefixes while still resolving at compile time.
std::string include_spelling(
    const std::string &file, const std::filesystem::path &base, const std::vector<std::string> &flags) {
    const std::filesystem::path declared(file);
    if (declared.is_relative()) return file;   // already relative to the config dir
    const auto under = [&](const std::filesystem::path &root) -> std::string {
        std::error_code ec;
        const std::filesystem::path rel = std::filesystem::relative(declared, root, ec);
        if (ec || rel.empty() || *rel.begin() == "..") return {};
        return rel.generic_string();
    };
    if (const std::string rel = under(base); !rel.empty()) return rel;
    for (const std::string &flag : flags) {
        if (flag.rfind("-I", 0) != 0 || flag.size() <= 2) continue;
        if (const std::string rel = under(flag.substr(2)); !rel.empty()) return rel;
    }
    return file;
}

bool expose_thunks(amc::AbiModule &module, const amc::lang::Config &config, std::vector<NativeAdapt> &adaptations,
    const std::filesystem::path &base, std::string &error) {
    if (config.exposed.empty()) return true;
    if (config.thunks_output.empty()) {
        error = "[[expose]] requires a [thunks] output path";
        return false;
    }
    for (const auto &adapt : config.adaptations) {
        const NativeKind kind = native_kind_of(adapt.native);
        if (kind == NativeKind::none) {
            error = "[[adapt]] native kind is not supported: " + adapt.native;
            return false;
        }
        if (adapt.target.empty()) {
            error = "[[adapt]] requires a boundary target type";
            return false;
        }
        if ((kind == NativeKind::vector || kind == NativeKind::span) && adapt.element.empty()) {
            error = "[[adapt]] requires an element spelling for " + adapt.native;
            return false;
        }
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
    // Synthesize the ABIX-STD boundary type an adapted parameter is marshalled
    // onto, when the module does not already declare it. The layout mirrors
    // `SKL/ABIX/STD/{String,Span}.h` (the generated thunk includes the real header,
    // so the compiled parameter type is the header's); a consumer projects the
    // boundary by name. An existing type of the same name is reused untouched.
    const auto ensure_pointer = [&](const std::string &name, amc::Hash128 pointee) -> amc::Hash128 {
        const amc::Hash128 id = intern(name, amc::TypeKind::pointer, pointer_size, pointer_size, 0);
        for (const auto &ref : module.pointees)
            if (ref.pointer == id) return id;
        module.pointees.push_back({id, pointee, amc::pointee_const});
        return id;
    };
    const amc::Hash128 len_id =
        intern("unsigned long", amc::TypeKind::primitive, 8, 8, amc::primitive_abi(8, amc::PrimitiveAbiKind::uint));
    const amc::Hash128 encoding_id =
        intern("unsigned int", amc::TypeKind::primitive, 4, 4, amc::primitive_abi(4, amc::PrimitiveAbiKind::uint));
    const auto ensure_boundary = [&](NativeAdapt &adapt) -> amc::Hash128 {
        for (const auto &type : module.types)
            if (type.name == adapt.target) return type.id;
        const bool string_view = adapt.kind == static_cast<uint32_t>(NativeKind::string)
                              || adapt.kind == static_cast<uint32_t>(NativeKind::string_view);
        const std::string element = adapt.element.empty() ? "char" : adapt.element;
        const amc::Hash128 data_id = ensure_pointer("const " + element + "*", adapt.element_id);
        amc::Type type;
        type.name = adapt.target;
        type.kind = amc::TypeKind::record;
        type.id = amc::hash_text(type.name, 0x54595045);
        type.field_begin = static_cast<uint32_t>(module.fields.size());
        module.fields.push_back({type.id, "data", data_id, 0, 0});
        module.fields.push_back({type.id, "len", len_id, pointer_size, 0});
        uint32_t end = pointer_size + 8;
        if (string_view) {
            module.fields.push_back({type.id, "encoding", encoding_id, pointer_size + 8, 0});
            end = pointer_size + 12;
        }
        type.field_count = static_cast<uint32_t>(module.fields.size()) - type.field_begin;
        type.align = pointer_size;
        type.size = (end + pointer_size - 1) / pointer_size * pointer_size;
        std::vector<amc::Field> layout_fields(
            module.fields.begin() + type.field_begin, module.fields.begin() + type.field_begin + type.field_count);
        type.layout_hash = amc::canonical_layout_hash(module, type, layout_fields);
        module.types.push_back(std::move(type));
        return module.types.back().id;
    };
    // The C++ expression that marshals one boundary value at the call site.
    const auto adapt_expression = [&](const NativeAdapt &adapt, const std::string &arg) -> std::string {
        switch (static_cast<NativeKind>(adapt.kind)) {
            case NativeKind::string:      return "std::string(" + arg + ".data, " + arg + ".len)";
            case NativeKind::string_view: return "std::string_view(" + arg + ".data, " + arg + ".len)";
            case NativeKind::vector:
                return "std::vector<" + adapt.element + ">(" + arg + ".data, " + arg + ".data + " + arg + ".len)";
            case NativeKind::span: return "std::span<const " + adapt.element + ">(" + arg + ".data, " + arg + ".len)";
            default:               return arg;
        }
    };
    // Adaptation lookups are by the recorded native parameter type.
    const auto find_adapt = [&](amc::Hash128 native) -> NativeAdapt * {
        for (auto &adapt : adaptations)
            if (adapt.native_type == native) return &adapt;
        return nullptr;
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
        AMC_GENERATED_FILE_WARNING("//")
        "// Generated by `amc` - C thunks exposing C++ types as ABIX capabilities.\n"
        "// Build artifact: compile it into the producing module.\n"
        "#include \"SKL/ABIX/Interface/Interface.h\"\n";
    // A declared native↔ABIX-STD adaptation needs the boundary header for the
    // spelling it puts in the thunk signature.
    bool wants_string = false, wants_span = false;
    for (const auto &adapt : config.adaptations) {
        const NativeKind kind = native_kind_of(adapt.native);
        if (kind == NativeKind::string || kind == NativeKind::string_view) wants_string = true;
        if (kind == NativeKind::vector || kind == NativeKind::span) wants_span = true;
    }
    if (wants_string) source += "#include \"SKL/ABIX/STD/String.h\"\n";
    if (wants_span) source += "#include \"SKL/ABIX/STD/Span.h\"\n";
    for (const auto &file : config.files)
        source += "#include \"" + include_spelling(file, base, config.flags) + "\"\n";
    source += "\n";
    // Interface manifest glue (discovered through `abix_module_query_v1`).
    std::string manifest;
    std::vector<std::string> interface_names;

    // Automatic ABIX-STD contract mapping: when an exposed method uses an
    // ABIX-STD boundary type, record its contract (SequenceView / StringView /
    // Result) so a consumer knows the semantics, not just the layout.
    std::vector<std::pair<amc::Hash128, uint32_t>> recorded_contracts;
    const auto record_contract = [&](amc::Hash128 id) -> amc::LoweringResult {
        const std::string name = type_name(id);
        amc::LoweringResult result;
        if (name.rfind("abix_span_", 0) == 0)
            result = amc::evaluate_sequence_view(module, id, amc::Hash128{}, "", false);
        else if (name.rfind("abix_str_", 0) == 0)
            result = amc::evaluate_string_view(module, id, amc::Hash128{}, "", false);
        else if (name.rfind("abix_result_", 0) == 0)
            result = amc::evaluate_result(module, id, amc::Hash128{}, "");
        else
            return {};
        for (const auto &seen : recorded_contracts)
            if (seen.first == id && seen.second == result.kind) return result;
        recorded_contracts.emplace_back(id, result.kind);
        module.lowerings.push_back(result);
        return result;
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
        // A base subobject is part of the exposed opaque handle: project it as an
        // opaque blob too, never reconstructing its (possibly STL-heavy) layout.
        for (auto &type : module.types)
            if (std::find(bases.begin(), bases.end(), type.id) != bases.end()) type.flags |= amc::type_opaque;
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
                    || (function.name.size() > method.size()
                        && function.name.compare(function.name.size() - method.size(), method.size(), method) == 0
                        && function.name[function.name.size() - method.size() - 1] == ':')) {
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
            for (const auto &parameter : found->parameters) {
                NativeAdapt *adapt = find_adapt(parameter.type_id);
                if (adapt != nullptr) {
                    // A declared native↔ABIX-STD adaptation: the thunk takes the
                    // boundary type and marshals it. Record the boundary contract
                    // and the adaptation as a needs_adapter lowering, once.
                    if (adapt->boundary_type == amc::Hash128{}) {
                        adapt->boundary_type = ensure_boundary(*adapt);
                        record_contract(adapt->boundary_type);
                        amc::LoweringResult lowering;
                        lowering.type_id = adapt->native_type;
                        lowering.contract_type = adapt->boundary_type;
                        const bool string_view = adapt->kind == static_cast<uint32_t>(NativeKind::string)
                                              || adapt->kind == static_cast<uint32_t>(NativeKind::string_view);
                        lowering.kind = static_cast<uint32_t>(
                            string_view ? amc::LoweringKind::string_view : amc::LoweringKind::sequence_view);
                        lowering.status = static_cast<uint32_t>(amc::LoweringStatus::needs_adapter);
                        lowering.reason = static_cast<uint32_t>(amc::LoweringReason::layout);
                        module.lowerings.push_back(lowering);
                    }
                    continue;
                }
                if (!representable(parameter.type_id)) {
                    error = "[[expose]] "
                          + expose.type
                          + "::"
                          + method
                          + " parameter type needs a contract: "
                          + type_name(parameter.type_id);
                    return false;
                }
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
                const NativeAdapt *adapt = find_adapt(parameter.type_id);
                const amc::Hash128 param_type = adapt != nullptr ? adapt->boundary_type : parameter.type_id;
                params += ", " + cpp_spelling(type_name(param_type)) + " " + name;
                if (i != 0) args += ", ";
                args += adapt != nullptr ? adapt_expression(*adapt, name) : name;
                signature.emplace_back(name, param_type);
            }
            source +=
                "extern \"C\" " + return_spelling + " abix_" + symbol_type + "_" + method + "(" + params + ") {\n";
            source +=
                std::string("    ") + (returns_void ? "" : "return ") + "self->" + method + "(" + args + ");\n}\n";
            const std::string symbol = "abix_" + symbol_type + "_" + method;
            // Capture the method facts before registering the thunk: `register_thunk`
            // pushes onto `module.functions`, which may reallocate the vector and
            // invalidate `found`. Reading through `found` afterwards would be a
            // use-after-free, so copy what the contract recording needs first.
            const amc::Hash128 return_type_id = found->return_type;
            std::vector<amc::Hash128> parameter_type_ids;
            parameter_type_ids.reserve(found->parameters.size());
            for (const auto &parameter : found->parameters)
                parameter_type_ids.push_back(parameter.type_id);
            // Record the ABIX-STD contracts of the boundary types in this
            // signature (span -> SequenceView, str -> StringView, result).
            const amc::LoweringResult return_contract = record_contract(return_type_id);
            for (const amc::Hash128 parameter_type_id : parameter_type_ids)
                record_contract(parameter_type_id);
            const amc::Hash128 thunk_signature = register_thunk(symbol, return_type_id, signature);
            const uint32_t thunk_index = static_cast<uint32_t>(module.functions.size() - 1);
            exposed_methods.push_back({symbol, thunk_signature});
            // A `abix_result_*` return binds to the error boundary: the status and
            // value are fields of the returned record, so a consumer can turn the
            // C aggregate into a typed Result without inspecting a bare integer.
            if (type_name(return_type_id).rfind("abix_result_", 0) == 0
                && return_contract.status != static_cast<uint32_t>(amc::LoweringStatus::unsupported)) {
                amc::ErrorContract contract;
                contract.function_index = thunk_index;
                contract.status_source = amc::StatusSource::return_field;
                contract.status_parameter = return_contract.pointer_field;
                contract.value_parameter = return_contract.length_field;
                contract.success_value = 0;
                module.errors.push_back(contract);
            }
        }
        source += "\n";

        // One interface per exposed type: each method binds its thunk by a
        // stable slot, so the runtime bus can dispatch without a vtable.
        if (!exposed_methods.empty()) {
            // Same identity formula as the [[interface]] builder and the ABIX
            // runtime helper: canonical name + version + ordered signatures.
            std::vector<amc::Hash128> ordered_signatures;
            ordered_signatures.reserve(exposed_methods.size());
            for (const auto &method : exposed_methods)
                ordered_signatures.push_back(method.signature);
            const amc::Hash128 interface_id = amc::interface_identity(cpp_type, 1, ordered_signatures);
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
    if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
    amc::lang::Config c;
    std::string e;
    if (!amc::lang::config_load(config_path, "cpp", c, e)) {
        fmt::print(stderr, "{}\n", e);
        return EXIT_FAILURE;
    }
    std::filesystem::path base = std::filesystem::absolute(config_path).parent_path();
    if (!c.db.empty() && std::filesystem::path(c.db).is_relative()) c.db = (base / c.db).string();
    // Clang needs absolute paths, but `c.files` is left exactly as declared:
    // `expose_thunks` emits it verbatim into the generated `#include`, so a
    // declared relative path stays relative instead of leaking a machine-
    // specific absolute prefix. Only the tool file list is resolved here.
    std::vector<std::string> tool_files;
    tool_files.reserve(c.files.size());
    for (const auto &f : c.files)
        tool_files.push_back(std::filesystem::path(f).is_relative() ? (base / f).string() : f);
#if defined(__APPLE__)
    append_macos_sysroot(c.flags);
#else
    append_clang_resource_dir(c.flags);
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
    std::string anchor_path;
    std::string anchor;
    if (!c.instantiations.empty()) {
        // The anchor lives in `base/.amc/`, so it needs the resolved paths.
        for (const auto &file : tool_files)
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

    std::vector<NativeAdapt> adaptations;
    Factory factory(wanted, module, c.adaptations, adaptations);
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
    if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
    if (!expose_thunks(module, c, adaptations, base, e)) {
        fmt::print(stderr, "{}\n", e);
        return EXIT_FAILURE;
    }
    if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
    if (!amc::write_abix(module, output_path, e)) {
        fmt::print(stderr, "{}\n", e);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
}   // namespace amc::lang::cpp
