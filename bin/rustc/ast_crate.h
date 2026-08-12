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

class ASTTestDesc {
public:
    Span span;
    ASTAbsolutePath path;
    ::std::string name;
    bool ignore = false;
    bool isBenchmark = false;

    enum class ShouldPanic {
        No,
        Yes,
        YesWithMessage,
    } panicType = ShouldPanic::No;

    ::std::string expectedPanicMessage;
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
    ::std::vector<::std::string> attributes;
};

class ASTCrate {
public:
    stl::ObjPool* pool;
    HIRTypeInterner& types;
    ASTAttributeList mAttrs;

    ::std::map<::std::string, ASTAbsolutePath> mLangItems;
    ::std::set<RcString> features;

public:
    ASTModule mRootModule;

    /// Loaded crates in load order
    ::std::vector<RcString> externCratesOrd;
    ::std::map<RcString, ASTExternCrate> externCrates;
    // Mapping filled by searching for (?visible) macros with is_pub=true
    ::std::map<RcString, const MacroRules*> exportedMacros;

    RcString extCratenameCore;
    RcString extCratenameStd;
    RcString extCratenameProcmacro;
    RcString extCratenameTest;

    // List of tests (populated in expand if --test is passed)
    bool testHarness = false;
    bool noMain = false;
    ::std::vector<ASTTestDesc> tests;

    /// Files loaded using things like include! and include_str!
    mutable ::std::vector<::std::string> extraFiles;

    // Procedural macros!
    ::std::vector<ASTProcMacroDef> procMacros;

    ASTEdition edition;
    enum class Type {
        Unknown,
        RustLib,
        RustDylib,
        CDylib,
        Executable,
        ProcMacro, // Procedural macro
    } crateType = Type::Unknown;

    enum LoadStd {
        LOAD_STD,
        LOAD_CORE,
        LOAD_NONE,
    } loadStd = LOAD_STD;

    ::std::string crateNameSuffix; // Suffix (from command-line)
    ::std::string crateNameSet;    // Crate name as set by the user (or auto-detected)
    RcString crateNameReal;        // user name '-' suffix
    ASTPath preludePath;

    ASTCrate(stl::ObjPool* pool, HIRTypeInterner& types);

    const ASTModule& rootModule() const {
        return mRootModule;
    }

    ASTModule& rootModule() {
        return mRootModule;
    }

    void setCrateName(std::string name);

    /// Load referenced crates
    void loadExterns();

    /// Load the named crate and returns the crate's unique name
    /// If the parameter `file` is non-empty, only that particular filename will be loaded (from any of the search paths)
    RcString loadExternCrate(Span sp, const RcString& name, const ::std::string& file = "");
};

/// Representation of an imported crate
class ASTExternCrate {
public:
    RcString mName;
    RcString shortName;
    ::std::string filename;
    HIRCrate* hir = nullptr;

    ASTExternCrate(stl::ObjPool* pool, HIRTypeInterner& types, const RcString& name, const ::std::string& path);

    ASTExternCrate(ASTExternCrate&&) = default;
    ASTExternCrate& operator=(ASTExternCrate&&) = default;
    ASTExternCrate(const ASTExternCrate&) = delete;
    ASTExternCrate& operator=(const ASTExternCrate&) = delete;
};

extern ::std::vector<::std::string> gCrateLoadDirs;
extern ::std::map<::std::string, ::std::string> gCrateOverrides;
extern ::std::map<RcString, RcString> gImplicitCrates;
