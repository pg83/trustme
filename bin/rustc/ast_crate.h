struct Settings;
#pragma once

#include "ast_ast.h"
#include "ast_types.h"
#include "ast_edition.h"

#include <set>

class HIRCrate;
class HIRTypeInterner;

namespace stl {
    class ObjPool;
}

class ASTExternCrate;
struct WireBoard;

class ASTTestDesc {
public:
    Span span;
    ASTAbsolutePath path;
    std::string name;
    bool ignore = false;
    bool isBenchmark = false;

    enum class ShouldPanic {
        No,
        Yes,
        YesWithMessage,
    } panicType = ShouldPanic::No;

    std::string expectedPanicMessage;
};

enum class ASTProcMacroTy {
    Function,
    Derive,
    Attribute,
};

class ASTProcMacroDef {
public:
    ASTProcMacroTy ty;
    RcString name;
    ASTAbsolutePath path;
    std::vector<std::string> attributes;
};

class ASTCrate {
public:
    const WireBoard& wb;
    stl::ObjPool* pool;

    stl::ObjPool* hirPool;
    HIRTypeInterner& types;
    ASTAttributeList attrs;

    std::map<std::string, ASTAbsolutePath> langItems;
    std::set<RcString> features;

public:
    ASTModule rootModule_;

    std::vector<RcString> externCratesOrd;
    std::map<RcString, ASTExternCrate> externCrates;

    std::map<RcString, const MacroRules*> exportedMacros;

    RcString extCratenameCore;
    RcString extCratenameStd;
    RcString extCratenameProcmacro;
    RcString extCratenameTest;

    bool testHarness = false;
    bool noMain = false;
    std::vector<ASTTestDesc> tests;

    mutable std::vector<std::string> extraFiles;

    std::vector<ASTProcMacroDef> procMacros;

    ASTEdition edition;
    enum class Type {
        Unknown,
        RustLib,
        RustDylib,
        CDylib,
        Executable,
        ProcMacro,
    } crateType = Type::Unknown;

    enum LoadStd {
        LOAD_STD,
        LOAD_CORE,
        LOAD_NONE,
    } loadStd = LOAD_STD;

    std::string crateNameSuffix;
    std::string crateNameSet;
    RcString crateNameReal;
    ASTPath preludePath;

    ASTCrate(const WireBoard& wb, stl::ObjPool* pool, stl::ObjPool* hirPool, HIRTypeInterner& types);

    const ASTModule& rootModule() const {
        return rootModule_;
    }

    ASTModule& rootModule() {
        return rootModule_;
    }

    void setCrateName(std::string name);

    void loadExterns(Settings& settings);

    RcString loadExternCrate(Settings& settings, Span sp, const RcString& name, const std::string& file = "");
};

class ASTExternCrate {
public:
    RcString name;
    RcString shortName;

    std::string filename;
    RcString objectFilename;
    RcString procMacroFilename;
    bool isProcMacro = false;
    HIRCrate* hir = nullptr;

    ASTExternCrate(u32& id, stl::ObjPool* pool, HIRTypeInterner& types, const RcString& name, const std::string& path);

    ASTExternCrate(ASTExternCrate&&) = default;
    ASTExternCrate& operator=(ASTExternCrate&&) = default;
    ASTExternCrate(const ASTExternCrate&) = delete;
    ASTExternCrate& operator=(const ASTExternCrate&) = delete;
};
