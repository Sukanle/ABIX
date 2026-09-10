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
#include <unordered_map>
#include <filesystem>

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
        if (!cxx || d->isImplicit() || !d->isThisDeclarationADefinition() || !selected(d)) return true;
        add_record(cxx);
        return true;
    }
    bool VisitEnumDecl(EnumDecl *d) {
        if (d->isCompleteDefinition() && selected(d)) add_enum(d);
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
    bool selected(const NamedDecl *d) const {
        auto n = name(d);
        return wanted.count(n) || wanted.count(d->getNameAsString());
    }
    amc::Hash128 add_type(QualType qt) {
        qt = qt.getCanonicalType();
        auto s = qt.getAsString();
        auto it = ids.find(s);
        if (it != ids.end()) return it->second;
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
    void add_record(CXXRecordDecl *d) {
        amc::Type t;
        t.name = name(d);
        t.kind = amc::TypeKind::record;
        t.id = amc::hash_text(t.name, 0x54595045);
        auto &l = ctx.getASTRecordLayout(d);
        auto qt = ctx.getCanonicalTagType(d);
        t.size = uint32_t(ctx.getTypeSize(qt) / 8);
        t.align = uint32_t(ctx.getTypeAlign(qt) / 8);
        t.field_begin = uint32_t(module.fields.size());
        ids[t.name] = t.id;
        module.types.push_back(t);
        const auto record_index = module.types.size() - 1;
        for (auto *f : d->fields()) {
            if (f->isBitField()) continue;
            amc::Field x;
            x.name = f->getNameAsString();
            x.type_id = add_type(f->getType());
            x.offset = uint32_t(l.getFieldOffset(f->getFieldIndex()) / 8);
            module.fields.push_back(x);
        }
        module.types[record_index].field_count =
            uint32_t(module.fields.size()) - module.types[record_index].field_begin;
    }
    void add_enum(EnumDecl *d) {
        amc::Type t;
        t.name = name(d);
        t.kind = amc::TypeKind::enumeration;
        t.id = amc::hash_text(t.name, 0x54595045);
        t.size = uint32_t(ctx.getTypeSize(d->getIntegerType()) / 8);
        t.align = uint32_t(ctx.getTypeAlign(d->getIntegerType()) / 8);
        ids[t.name] = t.id;
        module.types.push_back(t);
    }
    void add_function(FunctionDecl *d) {
        amc::Function f;
        f.name = name(d);
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
    o << "#pragma once\n#include <cstddef>\n#include <cstdint>\nnamespace amc_generated {\n";
    for (auto &t : m.types) {
        o
            << "struct "
            << cpp_name(t.name)
            << "_ABIX {\n static constexpr std::uint64_t type_id_lo = 0x"
            << std::hex
            << t.id.lo
            << "ULL;\n static constexpr std::uint64_t type_id_hi = 0x"
            << t.id.hi
            << "ULL;\n static constexpr std::size_t size = "
            << std::dec
            << t.size
            << ";\n static constexpr std::size_t align = "
            << t.align
            << ";\n";
        for (uint32_t i = 0; i < t.field_count; ++i)
            o
                << " static constexpr std::size_t "
                << cpp_name(m.fields[t.field_begin + i].name)
                << "_offset = "
                << m.fields[t.field_begin + i].offset
                << ";\n";
        o << "};\n";
    }
    o << "}\n";
    return 0;
}
int main(int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "usage: amc-cpp frontend <config.abic.toml> <output.abix> | backend <input.abix> <output.hpp>\n";
        return 2;
    }
    if (std::string(argv[1]) == "backend") return backend(argv[2], argv[3]);
    if (std::string(argv[1]) != "frontend") {
        std::cerr << "unknown capability\n";
        return 2;
    }
    Config c;
    std::string e;
    if (!config_load(argv[2], c, e)) {
        std::cerr << e << "\n";
        return 1;
    }
    std::filesystem::path base = std::filesystem::absolute(argv[2]).parent_path();
    if (!c.db.empty() && std::filesystem::path(c.db).is_relative()) c.db = (base / c.db).string();
    for (auto &f : c.files)
        if (std::filesystem::path(f).is_relative()) f = (base / f).string();
    std::string db_error;
    std::unique_ptr<CompilationDatabase> db;
    if (!c.db.empty()) {
        db = JSONCompilationDatabase::loadFromFile(c.db, db_error, JSONCommandLineSyntax::AutoDetect);
        if (!db) {
            std::cerr << db_error << "\n";
            return 1;
        }
    } else {
        db = std::make_unique<FixedCompilationDatabase>(base.string(), c.flags);
    }
    std::set<std::string> w(c.symbols.begin(), c.symbols.end());
    amc::AbiModule m;
    m.package_name = "cpp";
    Factory f(w, m);
    ClangTool tool(*db, c.files);
    if (tool.run(&f) != 0) {
        std::cerr << "clang analysis failed\n";
        return 1;
    }
    if (!amc::write_abix(m, argv[3], e)) {
        std::cerr << e << "\n";
        return 1;
    }
    return 0;
}
