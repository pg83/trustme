#pragma once

#include "ast_ast.h"
#include "ast_types.h"
#include "ast_edition.h"
#include <set>

namespace HIR {
    class Crate;
    class TypeInterner;
}

namespace stl {
    class ObjPool;
}

namespace AST {

    class ExternCrate;

    class TestDesc {
    public:
        Span span;
        ::AST::AbsolutePath path;
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

    enum class ProcMacroTy {
        Function,
        Derive,
        Attribute,
    };

    class ProcMacroDef {
    public:
        ProcMacroTy ty;
        RcString name;
        ::AST::AbsolutePath path;
        ::std::vector<::std::string> attributes;
    };

    class Crate {
    public:
        stl::ObjPool* pool;
        HIR::TypeInterner& types;
        ::AST::AttributeList mAttrs;

        ::std::map<::std::string, ::AST::AbsolutePath> mLangItems;
        ::std::set<RcString> features;

    public:
        Module mRootModule;

        /// Loaded crates in load order
        ::std::vector<RcString> externCratesOrd;
        ::std::map<RcString, ExternCrate> externCrates;
        // Mapping filled by searching for (?visible) macros with is_pub=true
        ::std::map<RcString, const MacroRules*> exportedMacros;

        RcString extCratenameCore;
        RcString extCratenameStd;
        RcString extCratenameProcmacro;
        RcString extCratenameTest;

        // List of tests (populated in expand if --test is passed)
        bool testHarness = false;
        bool noMain = false;
        ::std::vector<TestDesc> tests;

        /// Files loaded using things like include! and include_str!
        mutable ::std::vector<::std::string> extraFiles;

        // Procedural macros!
        ::std::vector<ProcMacroDef> procMacros;

        AST::Edition edition;
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
        AST::Path preludePath;

        Crate(stl::ObjPool* pool, HIR::TypeInterner& types);

        const Module& rootModule() const {
            return mRootModule;
        }

        Module& rootModule() {
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
    class ExternCrate {
    public:
        RcString mName;
        RcString shortName;
        ::std::string filename;
        ::HIR::Crate* hir = nullptr;

        ExternCrate(stl::ObjPool* pool, HIR::TypeInterner& types, const RcString& name, const ::std::string& path);

        ExternCrate(ExternCrate&&) = default;
        ExternCrate& operator=(ExternCrate&&) = default;
        ExternCrate(const ExternCrate&) = delete;
        ExternCrate& operator=(const ExternCrate&) = delete;
    };

    extern ::std::vector<::std::string> gCrateLoadDirs;
    extern ::std::map<::std::string, ::std::string> gCrateOverrides;
    extern ::std::map<RcString, RcString> gImplicitCrates;

} // namespace AST
