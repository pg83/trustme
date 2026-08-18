#include "main_bindings.h"

#include <pthread.h>

#include "ast_ast.h"
#include "hir_hir.h" // ABI_RUST
#include "version.h"
#include "ast_dump.h"
#include "ast_expr.h"
#include "settings.h"
#include "ast_crate.h"
#include "parse_lex.h"
#include "expand_cfg.h"
#include "wire_board.h"
#include "debug_inner.h"
#include "memory_dump.h"
#include "parse_common.h" // For edition checks
#include "trans_target.h"
#include "target_detect.h" // tools/common/target_detect.h
#include "parse_parseerror.h"
#include "hir_main_bindings.h"
#include "mir_main_bindings.h"
#include "lang_items.h"
#include "hir_inherent_cache.h"
#include "trans_main_bindings.h"
#include "resolve_main_bindings.h"
#include "hir_conv_main_bindings.h"
#include "hir_expand_main_bindings.h"
#include "lint_forbid.h"
#include "lint_must_use.h"
#include "lint_unsafe_code.h"
#include "hir_typeck_main_bindings.h"

#include <std/mem/obj_pool.h>

#include <climits>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>

#define NEWNODE(ty, ...) ASTExprNodeP(new ASTExprNode##ty(__VA_ARGS__))

void ExpandTestHarness(ASTCrate& crate) {
    ASSERT_BUG(Span(), crate.extCratenameTest != "", "Crate `test` not loaded");
    ASSERT_BUG(Span(), crate.extCratenameStd != "", "Crate `std` not loaded");
    auto cTest = crate.extCratenameTest;
    // Create the following module:
    // ```
    // mod `#test` {
    //   extern crate std;
    //   extern crate test;
    //   }
    //   static TESTS: [test::TestDescAndFn; _] = [
    //     test::TestDescAndFn { desc: test::TestDesc { name: "foo", ignore: false, should_panic: test::ShouldPanic::No }, testfn: ::path::to::foo },
    //     ];
    // }
    // ```

    // ---- main function ----
    auto mainFn = ASTFunction{Span(), mkType(*crate.pool, ASTTypeTags::Unit(), Span()), {}};
    {
        auto callNode = NEWNODE(CallPath, ASTPath(cTest, {ASTPathNode("test_main_static")}), ::makeVec1(NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(NamedValue, ASTPath("", {ASTPathNode("test#"), ASTPathNode("TESTS")})))));
        mainFn.setCode(mv$(callNode));
    }

    // ---- test list ----
    ::std::vector<ASTExprNodeP> testNodes;

    for (const auto& test : crate.tests) {
        ASTExprNodeStructLiteral::tValues descVals;
        // `name: "foo",`
        descVals.push_back({{}, "name", NEWNODE(CallPath, ASTPath(cTest, {ASTPathNode("StaticTestName")}), ::makeVec1(NEWNODE(String, test.name)))});
        // `ignore: false,`
        descVals.push_back({{}, "ignore", NEWNODE(Bool, test.ignore)});
        // `should_panic: ShouldPanic::No,`
        {
            ASTExprNodeP shouldPanicVal;
            switch (test.panicType) {
                case ASTTestDesc::ShouldPanic::No:
                    shouldPanicVal = NEWNODE(NamedValue, ASTPath(cTest, {ASTPathNode("ShouldPanic"), ASTPathNode("No")}));
                    break;
                case ASTTestDesc::ShouldPanic::Yes:
                    shouldPanicVal = NEWNODE(NamedValue, ASTPath(cTest, {ASTPathNode("ShouldPanic"), ASTPathNode("Yes")}));
                    break;
                case ASTTestDesc::ShouldPanic::YesWithMessage:
                    shouldPanicVal = NEWNODE(CallPath, ASTPath(cTest, {ASTPathNode("ShouldPanic"), ASTPathNode("YesWithMessage")}), makeVec1(NEWNODE(String, test.expectedPanicMessage)));
                    break;
            }
            descVals.push_back({{}, "should_panic", mv$(shouldPanicVal)});
        }
        {
            // TODO: Get this from attributes
            descVals.push_back({{}, "compile_fail", NEWNODE(Bool, false)});
            descVals.push_back({{}, "no_run", NEWNODE(Bool, false)});
            descVals.push_back({{}, "test_type", NEWNODE(NamedValue, ASTPath(cTest, {ASTPathNode("TestType"), ASTPathNode("UnitTest")}))});
        }
        {
            descVals.push_back({{}, "ignore_message", NEWNODE(NamedValue, ASTPath(crate.extCratenameStd, {ASTPathNode("option"), ASTPathNode("Option"), ASTPathNode("None")}))});
            auto sp = test.span.getTopFileSpan();
            descVals.push_back({{}, "source_file", NEWNODE(String, sp.filename.c_str())});
            descVals.push_back({{}, "start_line", NEWNODE(Integer, U128(sp.startLine), CORETYPE_UINT)});
            descVals.push_back({{}, "start_col", NEWNODE(Integer, U128(sp.startOfs), CORETYPE_UINT)});
            descVals.push_back({{}, "end_line", NEWNODE(Integer, U128(sp.endLine), CORETYPE_UINT)});
            descVals.push_back({{}, "end_col", NEWNODE(Integer, U128(sp.endOfs), CORETYPE_UINT)});
        }
        auto descExpr = NEWNODE(StructLiteral, ASTPath(cTest, {ASTPathNode("TestDesc")}), nullptr, mv$(descVals));

        ASTExprNodeStructLiteral::tValues descandfnVals;
        descandfnVals.push_back({{}, RcString::newInterned("desc"), mv$(descExpr)});

        auto testFcnNode = NEWNODE(NamedValue, ASTPath(test.path));
        {
            // Convert `fn()` into `fn()->Result<(),String>`
            // Use `|| ::test::assert_test_result( fcn() )`
            testFcnNode = NEWNODE(Closure, {}, mkType(*crate.pool, Span()), NEWNODE(CallPath, ASTPath(cTest, {ASTPathNode("assert_test_result")}), ::makeVec1(NEWNODE(CallPath, ASTPath(test.path), {}))), false, false, false);
        }
        auto testTypeVarName = test.isBenchmark ? "StaticBenchFn" : "StaticTestFn";
        descandfnVals.push_back({{}, RcString::newInterned("testfn"), NEWNODE(CallPath, ASTPath(cTest, {ASTPathNode(testTypeVarName)}), ::makeVec1(std::move(testFcnNode)))});

        testNodes.push_back(NEWNODE(StructLiteral, ASTPath(cTest, {ASTPathNode("TestDescAndFn")}), nullptr, mv$(descandfnVals)));
        // NOTE: 1.39+ needs &TestDescAndFn here
        {
            testNodes.back() = NEWNODE(UniOp, ASTExprNodeUniOp::REF, mv$(testNodes.back()));
        }
    }
    auto* testsArray = new ASTExprNodeArray(mv$(testNodes));

    size_t testCount = testsArray->values.size();
    auto listItemTy = mkType(*crate.pool, Span(), ASTPath(cTest, {ASTPathNode("TestDescAndFn")}));
    // NOTE: 1.39+ needs &TestDescAndFn here
    {
        listItemTy = mkType(*crate.pool, ASTTypeTags::Reference(), Span(), ASTLifetimeRef::newStatic(), false, mv$(listItemTy));
    }
    auto testsList = ASTStatic{ASTStatic::Class::STATIC, mkType(*crate.pool, ASTTypeTags::SizedArray(), Span(), mv$(listItemTy), ::std::shared_ptr<ASTExprNode>(new ASTExprNodeInteger(U128(testCount), CORETYPE_UINT))), ASTExpr(mv$(testsArray))};

    // ---- module ----
    auto newmod = ASTModule{ASTAbsolutePath("", {"test#"})};
    auto visPrivate = ASTVisibility::makeRestricted(ASTVisibility::Ty::Private, newmod.path());
    // - TODO: These need to be loaded too.

    newmod.addItem(Span(), visPrivate, "main", mv$(mainFn), {});
    newmod.addItem(Span(), visPrivate, "TESTS", mv$(testsList), {});

    crate.rootModule_.addItem(Span(), visPrivate, "test#", mv$(newmod), {});
    crate.langItems["trustme-main"] = ASTAbsolutePath("", {"test#", "main"});
}

#undef NEWNODE

#ifndef __has_feature
    #define __has_feature(x) 0
#endif

#if __has_feature(addressSanitizer) || __has_feature(undefinedBehaviorSanitizer)
    #define TRUSTME_SANITIZER_BUILD 1
#else
    #define TRUSTME_SANITIZER_BUILD 0
#endif

struct ProgramParams {
    enum eLastStage {
        STAGE_PARSE,
        STAGE_EXPAND,
        STAGE_RESOLVE,
        STAGE_TYPECK,
        STAGE_BORROWCK,
        STAGE_HIR,
        STAGE_MIR,
        STAGE_ALL,
    } lastStage = STAGE_ALL;

    /// `--emit=metadata`: analyse the crate, but do not codegen or link it.
    bool emitMetadataOnly = false;

    ::std::string infile;
    ::std::string outfile;
    ::std::string outputDir = "";
    ::std::string target = DEFAULT_TARGET_NAME;

    ::std::string emitDepfile;

    ASTEdition edition = ASTEdition::Rust2015;
    ASTCrate::Type crateType = ASTCrate::Type::Unknown;
    ::std::string crateName;
    ::std::string crateNameSuffix;

    OptimizationLevel optLevel = OptimizationLevel::None;
    bool debugAssertions = false;
    bool debugAssertionsExplicit = false;
    // `-Zub-checks`: whether the library's UB checks are compiled in. Follows
    // debug assertions unless it is given.
    bool ubChecks = false;
    bool ubChecksExplicit = false;
    Settings::FmtDebug fmtDebug = Settings::FmtDebug::Full;
    bool overflowChecks = false;
    bool overflowChecksExplicit = false;
    // rustc defaults MIR optimisation to 1 at -O0 and to 2 otherwise.
    // Keep the explicit bit separate so `-Zmir-opt-level=0` is distinguishable
    // from the implicit default.
    unsigned mirOptLevel = 0;
    bool mirOptLevelExplicit = false;
    DebugInfoLevel debugInfo = DebugInfoLevel::None;

    bool testHarness = false;

    // NOTE: If populated, nothing happens except for loading the target
    ::std::string targetSaveback;
    // NOTE: if true, no parse/compilation performed (target is loaded though)
    bool printCfgs = false;

    TraitSolverConfig traitSolver;

    ::std::vector<::std::string> crateSearchDirs;
    ::std::vector<::std::string> nativeLibSearchDirs;
    ::std::vector<::std::string> frameworkSearchDirs;
    ::std::vector<const char*> libraries;
    ::std::map<::std::string, ::std::string> crateOverrides; // --extern name=path

    ::std::set<::std::string> features;

    struct {
        /// Debugger aid: pause just after startup so a debugger can attach.
        bool pause = false;

        bool dumpAst = false;
        bool dumpHir = false;
        bool dumpMir = false;
    } debug;

    struct {
        ::std::string codegenType;
        ::std::string emitBuildCommand;
        // Emit the generated C++ source and stop, without invoking the C
        // compiler (for profiling the trustme front/middle-end in isolation).
        bool emitCppOnly = false;
        ::std::string panicType;
        ::std::vector<::std::string> linkerArgs;
    } codegen;

    ProgramParams(Settings& settings, int argc, char* argv[]);

    unsigned effectiveMirOptLevel() const {
        return mirOptLevelExplicit ? mirOptLevel : (optLevel == OptimizationLevel::None ? 1 : 2);
    }

    bool enableMirInlining() const {
        const auto level = effectiveMirOptLevel();
        return level >= 3 || (level == 2 && optLevel != OptimizationLevel::None && optLevel != OptimizationLevel::Less);
    }

    bool debugAssertionsEnabled() const {
        return debugAssertionsExplicit ? debugAssertions : optLevel == OptimizationLevel::None;
    }

    bool ubChecksEnabled() const {
        return ubChecksExplicit ? ubChecks : debugAssertionsEnabled();
    }

    bool overflowChecksEnabled() const {
        return overflowChecksExplicit ? overflowChecks : debugAssertionsEnabled();
    }

    void showHelp() const;
};

template <typename Rv, typename Fcn>
Rv CompilePhase(const char* name, Fcn f) {
    DebugTimedPhase timedPhase(name);
    return f();
}

template <typename Fcn>
void CompilePhaseV(const char* name, Fcn f) {
    DebugTimedPhase timedPhase(name);
    f();
}

void initDebugList() {
    debugInitPhases(
        "TRUSTME_DEBUG",
        {"Target Load",
         "Parse",
         "LoadCrates",
         "Expand",
         "Dump Expanded",
         "Implicit Crates",

         "Resolve Use",
         "Resolve Index",
         "Resolve Absolute",

         "HIR Lower",

         "Resolve Type Aliases",
         "Resolve Bind",
         "Resolve UFCS Outer",
         "Resolve UFCS paths",
         "Resolve HIR Self Type",
         "Resolve HIR Markings",
         "Sort Impls",
         "Constant Evaluate",

         "Typecheck Outer",
         "Typecheck Expressions",

         "Lint",

         "Expand HIR Annotate",
         "Expand HIR Static Borrow Mark",
         "Expand HIR Closures",
         "Expand HIR Static Borrow",
         "Expand HIR Calls",
         "Expand HIR VTables",
         "Expand HIR Reborrows",
         "Expand HIR ErasedType",

         "Dump HIR",
         "Lower MIR",
         "Dump MIR",
         "Constant Evaluate Full",
         "MIR Cleanup",
         "MIR Optimise",

         "HIR Serialise",
         "Trans Enumerate",
         "Trans Auto Impls",
         "Trans Monomorph",
         "MIR Optimise Inline",
         "MIR Cleanup 2",
         "MIR Optimise Inline PostSave",
         "Trans Enumerate Cleanup",
         "Trans Codegen"}
    );
}

/// main!
namespace {
    /// The crate name rustc derives from the input file when none is given: the
    /// stem, with `-` written as `_`.
    ::std::string CrateNameFromFile(const ::std::string& infile) {
        auto s = infile.find_last_of('/');
        s = (s == ::std::string::npos ? 0 : s + 1);
        auto s2 = infile.find_last_of('\\');
        s2 = (s2 == ::std::string::npos ? 0 : s2 + 1);
        s = ::std::max(s, s2);
        auto e = infile.find_first_of('.', s);
        if (e == ::std::string::npos) {
            e = infile.size();
        }

        ::std::string rv(infile.begin() + s, infile.begin() + e);
        for (auto& b : rv) {
            if (b == '-') {
                b = '_';
            }
        }
        return rv;
    }
}

static int compile(int argc, char* argv[]) {
    initDebugList();
#if TRUSTME_SANITIZER_BUILD
    // Keep teardown out of production, but make sanitizer builds destroy every
    // pooled object so ASan/LSan can distinguish real leaks from arena lifetime.
    auto poolOwner = stl::ObjPool::fromMemory();
    auto* pool = poolOwner.mutPtr();
#else
    auto* pool = stl::ObjPool::fromMemoryRaw();
#endif
    WireBoard& wb = *pool->make<WireBoard>(pool);
    wb.types = pool->make<HIRTypeInterner>(*pool);
    wb.settings = pool->make<Settings>();
    wb.settings->cfg = CfgCreateState(*pool);
    ProgramParams params(*wb.settings, argc, argv);
    wb.settings->solver = params.traitSolver;
    wb.settings->overflowChecks = params.overflowChecksEnabled();
    wb.settings->ubChecks = params.ubChecksEnabled();
    wb.settings->fmtDebug = params.fmtDebug;
    const auto mirOptLevel = params.effectiveMirOptLevel();
    const auto enableMirInlining = params.enableMirInlining();
    if (params.codegen.panicType.empty()) {
        params.codegen.panicType = "unwind";
    }

    if (params.debug.pause) {
        char c;
        ::std::cerr << "Pausing to attach a debugger\nType any text to continue" << std::endl;
        ::std::cin >> c;
    }

    wb.inherentMethods = HIRInherentCache::create(*pool);

    // Set up cfg values
    CompilePhaseV("Setup", [&]() {
        CfgSetValue(*wb.settings, "rust_compiler", "trustme");
        CfgSetValue(*wb.settings, "panic", params.codegen.panicType);
        if (params.debugAssertionsEnabled()) {
            CfgSetFlag(*wb.settings, "debug_assertions");
        }
        if (params.overflowChecksEnabled()) {
            CfgSetFlag(*wb.settings, "overflow_checks");
        }
        if (params.ubChecksEnabled()) {
            CfgSetFlag(*wb.settings, "ub_checks");
        }
        CfgSetValue(*wb.settings, "fmt_debug",
            params.fmtDebug == Settings::FmtDebug::Shallow ? "shallow"
                : params.fmtDebug == Settings::FmtDebug::None ? "none"
                                                             : "full");
        CfgSetValueCb(*wb.settings, "feature", [&params](const ::std::string& s) {
            return params.features.count(s) != 0;
        });
    });
    CompilePhaseV("Target Load", [&]() {
        TargetSetCfg(wb, params.target);
    });

    if (params.printCfgs) {
        CfgDump(*wb.settings, std::cout);
        return 0;
    }
    if (params.targetSaveback != "") {
        TargetExportCurSpec(wb, params.targetSaveback);
        return 0;
    }

    if (params.infile == "") {
        ::std::cerr << "No input file passed" << ::std::endl;
        return 1;
    }

    if (params.testHarness) {
        CfgSetFlag(*wb.settings, "test");
    }

    ExpandInit();

    try {
        // Parse the crate into AST
        ASTCrate* cratePtr = CompilePhase<ASTCrate*>("Parse", [&]() {
            return ParseCrate(wb, pool, params.infile, params.edition);
        });
        ASTCrate& crate = *cratePtr;
        wb.astCrate = cratePtr;
        crate.testHarness = params.testHarness;
        crate.crateNameSuffix = params.crateNameSuffix;

        if (params.lastStage == ProgramParams::STAGE_PARSE) {
            return 0;
        }
        memoryDump("Parsed");

        // Load external crates.
        CompilePhaseV("LoadCrates", [&]() {
            // Hacky!
            wb.settings->crateOverrides = params.crateOverrides;
            for (const auto& ld : params.crateSearchDirs) {
                wb.settings->crateLoadDirs.push_back(ld);
            }
            crate.loadExterns(*wb.settings);
            if (params.testHarness) {
                auto testCrateName = RcString::newInterned("test");
                wb.settings->implicitCrates.insert(std::make_pair(testCrateName, crate.loadExternCrate(*wb.settings, Span(), testCrateName)));
            }
        });

        {
            // Extract the crate type and name from the crate attributes
            auto crateType = params.crateType;
            if (crateType == ASTCrate::Type::Unknown) {
                crateType = crate.crateType;
            }
            if (crateType == ASTCrate::Type::Unknown) {
                // Assume to be executable
                crateType = ASTCrate::Type::Executable;
            }
            crate.crateType = crateType;

            // `module_path!` is expanded below and needs the name already, so
            // the file-stem fallback is applied here rather than only once the
            // expansion is done. A `#![crate_name]` attribute is seen during
            // that expansion and overrides this.
            crate.setCrateName(params.crateName != "" ? params.crateName : CrateNameFromFile(params.infile));
            crate.crateType = ASTCrate::Type::Unknown;
        }

        // Iterate all items in the AST, applying syntax extensions
        CompilePhaseV("Expand", [&]() {
            Expand(wb, crate);

            if (params.testHarness) {
                ExpandTestHarness(crate);
            }
        });
        // Once `cfg` has removed what it removes, the lint attributes that are
        // left have to agree with each other.
        CompilePhaseV("Lint Forbid", [&]() {
            LintCheckForbid(wb, crate);
        });

        // Extract the crate type and name from the crate attributes
        auto crateType = params.crateType;
        if (crateType == ASTCrate::Type::Unknown) {
            crateType = crate.crateType;
        }
        if (crateType == ASTCrate::Type::Unknown) {
            // Assume to be executable
            crateType = ASTCrate::Type::Executable;
        }
        crate.crateType = crateType;

        if (crate.crateType == ASTCrate::Type::ProcMacro) {
            ExpandProcMacroHarness(wb, crate);
        }

        auto crateName = params.crateName;
        if (crateName == "") {
            crateName = crate.crateNameSet;
        }
        if (crateName == "") {
            crateName = CrateNameFromFile(params.infile);
        }
        if (params.testHarness) {
            crateName += "$test";
        }
        crate.setCrateName(crateName);

        if (params.outfile == "") {
            switch (crate.crateType) {
                case ASTCrate::Type::RustLib:
                    params.outfile = FMT(params.outputDir << "lib" << crate.crateNameSet << ".rlib");
                    break;
                case ASTCrate::Type::Executable:
                    params.outfile = FMT(params.outputDir << crate.crateNameSet);
                    break;
                case ASTCrate::Type::ProcMacro:
                    params.outfile = FMT(params.outputDir << "lib" << crate.crateNameSet << "-plugin");
                    break;
                default:
                    params.outfile = FMT(params.outputDir << crate.crateNameSet << ".o");
                    break;
            }
            DEBUG("params.outfile = " << params.outfile);
        }

        if (params.debug.dumpAst) {
            CompilePhaseV("Dump Expanded", [&]() {
                DumpRust(FMT(params.outfile << "_1_ast.rs").c_str(), crate);
            });
        }

        if (params.lastStage == ProgramParams::STAGE_EXPAND) {
            return 0;
        }
        memoryDump("Expanded");

        // Allocator and panic strategies
        CompilePhaseV("Implicit Crates", [&]() {
            if (crate.crateType == ASTCrate::Type::Executable || params.testHarness || crate.crateType == ASTCrate::Type::ProcMacro) {
                bool allocatorCrateLoaded = false;
                RcString allocCrateName;
                bool panicRuntimeLoaded = false;
                RcString panicCrateName;
                bool panicRuntimeNeeded = false;
                for (const auto& ec : crate.externCrates) {
                    ::std::ostringstream ss;
                    for (const auto& e : ec.second.hir->langItems) {
                        ss << e << ",";
                    }
                    DEBUG("Looking at lang items from " << ec.first << " : " << ss.str());
                    if (ec.second.hir->langItems.count("trustme-allocator")) {
                        if (allocatorCrateLoaded) {
                            ERROR(Span(), E0000, "Multiple allocator crates loaded - " << allocCrateName << " and " << ec.first);
                        }
                        allocCrateName = ec.first;
                        allocatorCrateLoaded = true;
                    }
                    if (ec.second.hir->langItems.count("trustme-panic_runtime")) {
                        if (panicRuntimeLoaded) {
                            WARNING(Span(), W0000, "Multiple panic_runtime crates loaded - " << panicCrateName << " and " << ec.first);
                        } else {
                            panicCrateName = ec.first;
                            panicRuntimeLoaded = true;
                        }
                    }
                    if (ec.second.hir->langItems.count("trustme-needs_panic_runtime")) {
                        panicRuntimeNeeded = true;
                    }
                }
                // The default (system) allocator is provided by liballoc.
                allocatorCrateLoaded = true;
                if (!allocatorCrateLoaded) {
                    crate.loadExternCrate(*wb.settings, Span(), "alloc_system");
                }

                if (panicRuntimeNeeded /*&& !panic_runtime_loaded*/) {
                    auto panicCrate = "panic_" + params.codegen.panicType;
                    crate.loadExternCrate(*wb.settings, Span(), panicCrate.c_str());
                }

                // - `trustme-main` lang item default
                if (!crate.noMain) {
                    crate.langItems.insert(::std::make_pair(::std::string("trustme-main"), ASTAbsolutePath("", {"main"})));
                }
            }
        });

        /// Emit the dependency files
        if (params.emitDepfile != "") {
            // - Iterate all loaded files for modules
            struct PathEnumerator {
                ::std::vector<::std::string> out;

                void visitModule(ASTModule& mod) {
                    if (mod.fileInfo.path != "!" && mod.fileInfo.path.back() != '/') {
                        out.push_back(mod.fileInfo.path);
                    }
                    // TODO: Should we check anon modules?
                    //}
                    for (auto& i : mod.items) {
                        if (i->data.is_Module()) {
                            this->visitModule(i->data.as_Module());
                        }
                    }
                }
            };

            PathEnumerator pe;
            pe.visitModule(crate.rootModule_);

            ::std::ofstream of{params.emitDepfile};
            // TODO: Escape spaces and colons in these paths
            of << params.outfile << ": " << params.infile;
            for (const auto& modPath : pe.out) {
                of << " " << modPath;
            }
            of << ::std::endl;

            of << params.outfile << ":";
            // - Iterate all loaded crates files
            for (const auto& ec : crate.externCrates) {
                of << " " << ec.second.filename;
            }
            // - Iterate all extra files (include! and friends)
        }

        // Resolve names to be absolute names (include references to the relevant struct/global/function)
        // - This does name checking on types and free functions.
        // - Resolves all identifiers/paths to references
        CompilePhaseV("Resolve Use", [&]() {
            ResolveUse(wb, crate); // - Absolutise and resolve use statements
        });
        CompilePhaseV("Resolve Index", [&]() {
            ResolveIndex(crate); // - Build up a per-module index of avalable names (faster and simpler later resolve)
        });
        CompilePhaseV("Resolve Absolute", [&]() {
            ResolveAbsolutise(wb, crate); // - Convert all paths to Absolute or UFCS, and resolve variables
        });
        memoryDump("Resolved");

        if (params.debug.dumpAst) {
            CompilePhaseV("Temp output - Resolved", [&]() {
                DumpRust(FMT(params.outfile << "_1_ast.rs").c_str(), crate);
            });
        }

        if (params.lastStage == ProgramParams::STAGE_RESOLVE) {
            return 0;
        }

        // --------------------------------------
        // HIR Section
        // --------------------------------------
        // Construct the HIR beside the AST in the compilation object pool.
        HIRCrate* hirCrate = CompilePhase<HIRCrate*>("HIR Lower", [&]() {
            return LowerHIRFromAST(wb, pool, crate);
        });
        wb.crate = hirCrate;
        wb.langItems = LangItems::create(*pool, *hirCrate);
        memoryDump("HIR Gen");
        if (params.debug.dumpHir) {
            CompilePhaseV("Dump HIR", [&]() {
                ::std::ofstream os(FMT(params.outfile << "_2_hir.rs"));
                HIRDump(os, *hirCrate);
            });
        }
        memoryDump("HIR");

        // Replace type aliases (`type`) into the actual type
        // - Does simple replacements
        // - Done before bind so type alises can be used in patterns?
        CompilePhaseV("Resolve Type Aliases", [&]() {
            ConvertHIRExpandAliases(*hirCrate);
        });
        CompilePhaseV("Validate Receivers", [&]() {
            ConvertHIRValidateReceivers(wb, *hirCrate);
        });
        // Set up bindings and other useful information.
        CompilePhaseV("Resolve Bind", [&]() {
            ConvertHIRBind(wb, *hirCrate);
        });

        // Determine what trait to use for <T>::Foo in outer scope
        // - Also inserts defaults in trait impls
        CompilePhaseV("Resolve UFCS Outer", [&]() {
            ConvertHIRResolveUFCSOuter(wb, *hirCrate);
        });
        // Expand `Self` into the true type
        // - TODO: Move this later on, but that requires fixing some of the resolve logic around trait impl lookup
        CompilePhaseV("Resolve HIR Self Type", [&]() {
            ConvertHIRExpandAliasesSelf(*hirCrate);
        });
        // Enumerate marker impls on types and other useful metadata
        CompilePhaseV("Resolve HIR Markings", [&]() {
            ConvertHIRMarkings(*hirCrate);
        });
        CompilePhaseV("Sort Impls", [&]() {
            ConvertHIRResolveUFCSSortImpls(wb, *hirCrate);
        });
        // Determine what trait to use for <T>::Foo (and does some associated type expansion)
        CompilePhaseV("Resolve UFCS paths", [&]() {
            ConvertHIRResolveUFCS(wb, *hirCrate);
        });
        if (params.debug.dumpHir) {
            CompilePhaseV("Dump HIR", [&]() {
                ::std::ofstream os(FMT(params.outfile << "_2_hir.rs"));
                HIRDump(os, *hirCrate);
            });
        }
        // TODO: Expand vtables here?
        // - Some parts of constant evaluate require it
        // Basic constant evalulation (intergers/floats only)
        if (params.lastStage == ProgramParams::STAGE_HIR) {
            return 0;
        }

        CompilePhaseV("Constant Evaluate", [&]() {
            ConvertHIRConstantEvaluate(wb, *hirCrate);
        });

        if (params.debug.dumpHir) {
            // DUMP after initial consteval
            CompilePhaseV("Dump HIR", [&]() {
                ::std::ofstream os(FMT(params.outfile << "_2_hir.rs"));
                HIRDump(os, *hirCrate);
            });
        }

        // === Type checking ===
        // - This can recurse and call the MIR lower to evaluate constants

        // Check outer items first (types of constants/functions/statics/impls/...)
        // - Doesn't do any expressions except those in types
        CompilePhaseV("Typecheck Outer", [&]() {
            TypecheckModuleLevel(wb, *hirCrate);
        });
        // Check the rest of the expressions (including function bodies)
        CompilePhaseV("Typecheck Expressions", [&]() {
            TypecheckExpressions(wb, *hirCrate);
        });
        // Lints that need resolved types, but must see the code as written.
        CompilePhaseV("Lint", [&]() {
            LintUnusedMustUse(wb, *hirCrate);
            LintUnsafeCode(wb, *hirCrate);
        });

        // === HIR Expansion ===
        // Annotate how each node's result is used
        CompilePhaseV("Expand HIR Annotate", [&]() {
            HIRExpandAnnotateUsage(wb, *hirCrate);
        });
        CompilePhaseV("Expand HIR Static Borrow Mark", [&]() {
            HIRExpandStaticBorrowConstantsMark(wb, *hirCrate);
        });
        // - Now that all types are known, closures can be desugared
        CompilePhaseV("Expand HIR Closures", [&]() {
            HIRExpandClosures(wb, *hirCrate);
        });
        CompilePhaseV("Expand HIR Static Borrow", [&]() {
            HIRExpandStaticBorrowConstants(wb, *hirCrate);
        });
        // - Construct VTables for all traits and impls.
        //  TODO: How early can this be done?
        //  > Requires consteval completed for types to be fully valid?
        //  TODO: Would prefer to have this done before consteval, as consteval might reference a vtable
        CompilePhaseV("Expand HIR VTables", [&]() {
            HIRExpandVTables(wb, *hirCrate);
        });
        // - And calls can be turned into UFCS
        CompilePhaseV("Expand HIR Calls", [&]() {
            HIRExpandUfcsEverything(wb, *hirCrate);
        });
        CompilePhaseV("Expand HIR Reborrows", [&]() {
            HIRExpandReborrows(wb, *hirCrate);
        });
        CompilePhaseV("Expand HIR ErasedType", [&]() {
            HIRExpandErasedType(wb, *hirCrate);
        });
        if (params.debug.dumpHir) {
            // DUMP after typecheck (before validation)
            CompilePhaseV("Dump HIR", [&]() {
                ::std::ofstream os(FMT(params.outfile << "_2_hir.rs"));
                HIRDump(os, *hirCrate);
            });
        }
        if (params.lastStage == ProgramParams::STAGE_TYPECK) {
            return 0;
        }
        memoryDump("Typecheck");

        // Lower expressions into MIR
        CompilePhaseV("Lower MIR", [&]() {
            HIRGenerateMIR(wb, *hirCrate);
        });

        if (params.debug.dumpMir) {
            // DUMP after generation
            CompilePhaseV("Dump MIR", [&]() {
                ::std::ofstream os(FMT(params.outfile << "_3_mir.rs"));
                MIRDump(os, *hirCrate);
            });
        }
        memoryDump("MIR Gen");

        // LowerMIR validates every function before returning. The next validation is
        // performed after MIR_Cleanup has actually changed the crate.

        // - Expand constants in HIR and virtualise calls
        CompilePhaseV("MIR Cleanup", [&]() {
            MIRCleanupCrate(wb, *hirCrate);
        });
        // Optimise the MIR
        CompilePhaseV("MIR Optimise", [&]() {
            MIROptimiseCrate(wb, *hirCrate, mirOptLevel, enableMirInlining);
        });

        if (params.debug.dumpMir) {
            // DUMP: After optimisation
            CompilePhaseV("Dump MIR", [&]() {
                ::std::ofstream os(FMT(params.outfile << "_3_mir.rs"));
                MIRDump(os, *hirCrate);
            });
        }
        if (params.lastStage == ProgramParams::STAGE_MIR) {
            return 0;
        }
        memoryDump("MIR Opt");

        // TODO: Pass to mark items that are..
        // - Signature Exportable (public)
        // - MIR Exportable (public generic, #[inline], or used by a either of those)
        // - Require codegen (public or used by an exported function)
        TransOptions transOpt;
        transOpt.mode = params.codegen.codegenType == "" ? "c" : params.codegen.codegenType;
        transOpt.buildCommandFile = params.codegen.emitBuildCommand;
        transOpt.emitCppOnly = params.codegen.emitCppOnly;
        transOpt.linkerArgs = params.codegen.linkerArgs;
        transOpt.optLevel = params.optLevel;
        transOpt.panicCrate = "panic_" + params.codegen.panicType;
        transOpt.librarySearchDirs = params.nativeLibSearchDirs;
        transOpt.frameworkSearchDirs = params.frameworkSearchDirs;
        for (const char* libname : params.libraries) {
            hirCrate->extLibs.push_back(HIRExternLibrary{libname});
        }
        transOpt.debugInfo = params.debugInfo;

        // Generate code for non-generic public items (if requested)
        if (params.testHarness) {
            // If the test harness is enabled, override crate type to "Executable"
            crateType = ASTCrate::Type::Executable;
        }

        // TODO: For 1.29 executables/dylibs, add oom/panic shims
        if (crateType == ASTCrate::Type::ProcMacro) {
            // - Save a very basic HIR dump, making sure that there's no lang items in it (e.g. `trustme-main`)
            CompilePhaseV("HIR Serialise", [&]() {
                HIRCrate crateForSer(pool, *wb.types);
                crateForSer.crateName = hirCrate->crateName;
                crateForSer.edition = hirCrate->edition;
                for (const auto& i : hirCrate->rootModule.macroItems) {
                    DEBUG(i.first << ": " << i.second->ent.tagStr());
                    if (const auto* e = i.second->ent.opt_ProcMacro()) {
                        crateForSer.rootModule.macroItems.insert(std::make_pair(i.first, crateForSer.pool->make<HIRVisEnt<HIRMacroItem>>(HIRVisEnt<HIRMacroItem>{i.second->publicity, *e})));
                    }
                }
                crateForSer.exportedMacroNames = hirCrate->exportedMacroNames;
                HIRSerialise(params.outfile + ".hir", crateForSer);
            });
        }

        // `--emit=metadata` stops here: the crate has been analysed, and what
        // is left only builds it.
        if (params.emitMetadataOnly) {
            // Nothing reads what this writes: a crate that depends on this one
            // is given the rlib the ordinary build produces. Only the file has
            // to exist.
            { ::std::ofstream marker(params.outfile); }
            return 0;
        }

        // Enumerate items to be passed to codegen
        TransList items = CompilePhase<TransList>("Trans Enumerate", [&]() {
            switch (crateType) {
                case ASTCrate::Type::Unknown:
                    ::std::cerr << "BUG? Unknown crate type" << ::std::endl;
                    exit(1);
                    break;
                case ASTCrate::Type::RustLib:
                case ASTCrate::Type::RustDylib:
                case ASTCrate::Type::CDylib:
                    return TransEnumeratePublic(wb, *hirCrate);
                case ASTCrate::Type::ProcMacro:
                case ASTCrate::Type::Executable:
                    return TransEnumerateMain(wb, *hirCrate);
            }
            throw ::std::runtime_error("Invalid crate_type value");
        });
        // - Generate automatic impls (mainly Clone for 1.29)
        CompilePhaseV("Trans Auto Impls", [&]() {
            // TODO: Drop glue generation?
            TransAutoImpls(wb, *hirCrate, items);
        });
        // - Generate monomorphised versions of all functions
        CompilePhaseV("Trans Monomorph", [&]() {
            TransMonomorphiseList(wb, *hirCrate, items, mirOptLevel);
        });
        // - Do post-monomorph inlining
        CompilePhaseV("MIR Optimise Inline", [&]() {
            MIROptimiseCrateInlining(wb, *hirCrate, items, false, mirOptLevel, enableMirInlining);
        });

        // - Expand constants in HIR (using ones that were monomorphised above)
        CompilePhaseV("MIR Cleanup 2", [&]() {
            MIRCleanupSetPostMonomorph();
            MIRCleanupCrate(wb, *hirCrate);
        });

        memoryDump("Trans");

        std::string hirFile;
        switch (crateType) {
            case ASTCrate::Type::RustLib:
                // Save a loadable HIR dump
                hirFile = params.outfile + ".hir";
                CompilePhaseV("HIR Serialise", [&]() {
                    HIRSerialise(hirFile, *hirCrate);
                });
                break;
            case ASTCrate::Type::RustDylib:
                // Save a loadable HIR dump
                CompilePhaseV("HIR Serialise", [&]() {
                    HIRSerialise(hirFile, *hirCrate);
                });
                break;
            default:
                break;
        }

        // - Do post-monomorph inlining
        CompilePhaseV("MIR Optimise Inline PostSave", [&]() {
            MIROptimiseCrateInlining(wb, *hirCrate, items, true, mirOptLevel, enableMirInlining);
        });
        // - Clean up ununused functions
        CompilePhaseV("Trans Enumerate Cleanup", [&]() {
            TransEnumerateCleanup(wb, *hirCrate, items);
        });

        switch (crateType) {
            case ASTCrate::Type::Unknown:
                throw "";
            case ASTCrate::Type::RustLib:
                // Generate a linkable .o
                CompilePhaseV("Trans Codegen", [&]() {
                    TransCodegen(wb, params.outfile, CodegenOutput::StaticLibrary, transOpt, hirCrate, std::move(items), hirFile);
                });
                break;
            case ASTCrate::Type::RustDylib:
            case ASTCrate::Type::CDylib:
                // Generate a shared library
                CompilePhaseV("Trans Codegen", [&]() {
                    TransCodegen(wb, params.outfile, CodegenOutput::DynamicLibrary, transOpt, hirCrate, std::move(items), hirFile);
                });
                break;
            case ASTCrate::Type::ProcMacro: {
                // Needs: An executable (the actual macro handler), metadata (for `extern crate foo;`)
                // - Metadata was done before enumerate
                CompilePhaseV("Trans Codegen", [&]() {
                    TransCodegen(wb, params.outfile, CodegenOutput::Executable, transOpt, hirCrate, std::move(items), hirFile);
                });
                break;
            }
            case ASTCrate::Type::Executable:
                CompilePhaseV("Trans Codegen", [&]() {
                    TransCodegen(wb, params.outfile, CodegenOutput::Executable, transOpt, hirCrate, std::move(items), "");
                });
                break;
        }
    } catch (unsigned int) {
    }
    //catch(const CompileError::Base& e)
    //{
    //    ::std::cerr << "Parser Error: " << e.what() << ::std::endl;
    //}
    //catch(const ::std::exception& e)
    //{
    //    ::std::cerr << "Misc Error: " << e.what() << ::std::endl;
    //}
    //catch(const char* e)
    //{
    //    ::std::cerr << "Internal Compiler Error: " << e << ::std::endl;
    //}

    return 0;
}

namespace {
    struct CompileArgs {
        int argc;
        char** argv;
        int result;
    };

    void* compileOnThread(void* raw) {
        auto& args = *static_cast<CompileArgs*>(raw);
        args.result = compile(args.argc, args.argv);
        return nullptr;
    }
}

/// The compiler recurses over the syntax tree, so its stack depth follows how
/// deeply the input nests -- generated code can nest very deeply. Run the work on
/// a thread with a stack far larger than the usual 8MB default, which is what
/// rustc does for the same reason. `TRUSTME_MIN_STACK` overrides the size, as
/// `RUST_MIN_STACK` does there.
int main(int argc, char* argv[]) {
    size_t stackSize = 1024u * 1024 * 1024;
    if (const char* text = ::std::getenv("TRUSTME_MIN_STACK")) {
        char* end = nullptr;
        const auto value = ::std::strtoull(text, &end, 10);
        if (*end == '\0' && value > 0) {
            stackSize = static_cast<size_t>(value);
        }
    }

    pthread_attr_t attr;
    CompileArgs args{argc, argv, 1};
    pthread_t thread;
    if (pthread_attr_init(&attr) != 0 || pthread_attr_setstacksize(&attr, stackSize) != 0
        || pthread_create(&thread, &attr, compileOnThread, &args) != 0) {
        // No thread to be had: the work still has to happen, just with whatever
        // stack this one has.
        return compile(argc, argv);
    }
    pthread_join(thread, nullptr);
    pthread_attr_destroy(&attr);
    return args.result;
}

ProgramParams::ProgramParams(Settings& settings, int argc, char* argv[]) {
    auto addLibrarySearchDir = [this](const char* value) {
        ::std::string spec(value);
        auto equals = spec.find('=');
        ::std::string kind;
        ::std::string path;
        if (equals == ::std::string::npos) {
            path = std::move(spec);
        } else {
            kind = spec.substr(0, equals);
            path = spec.substr(equals + 1);
        }
        if (path.empty()) {
            ::std::cerr << "Option -L requires a non-empty path" << ::std::endl;
            exit(1);
        }

        if (kind.empty() || kind == "all") {
            this->crateSearchDirs.push_back(path);
            this->nativeLibSearchDirs.push_back(std::move(path));
        } else if (kind == "crate" || kind == "dependency") {
            this->crateSearchDirs.push_back(std::move(path));
        } else if (kind == "native") {
            this->nativeLibSearchDirs.push_back(std::move(path));
        } else if (kind == "framework") {
            this->frameworkSearchDirs.push_back(std::move(path));
        } else {
            ::std::cerr << "Unknown -L search path kind '" << kind << "'" << ::std::endl;
            exit(1);
        }
    };

    if (const auto* a = getenv("TRUSTME_LIBDIR")) {
        addLibrarySearchDir(a);
    }

    // Parse the rustc-compatible command-line subset supported by this driver.
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        // The following imitates rustc's version output (which the crate `rustc_version` tries to parse)
        // Report the emulated rustc release together with the native compiler version.
        if (strcmp(arg, "-vV") == 0) {
            const char* rustcTarget = RUSTC_TARGET_VERSION;

            ::std::cout << "rustc " << rustcTarget << ".100 (trustme " << VersionGetString() << ")" << ::std::endl;
            ::std::cout << "binary: rustc" << ::std::endl;
            ::std::cout << "commit-hash: " << gsVersionGitHash << ::std::endl;
            ::std::cout << "commit-date: UNKNOWN" << ::std::endl;
            ::std::cout << "build-date: " << gsVersionBuildTime << ::std::endl;
            ::std::cout << "host: UNKNOWN" << ::std::endl;
            ::std::cout << "release: " << rustcTarget << ".100" << ::std::endl;

            exit(0);
        }

        if (arg[0] != '-' || arg[1] == '\0') {
            if (this->infile == "") {
                this->infile = arg;
            } else {
                ::std::cerr << "Unexpected free argument" << ::std::endl;
                exit(1);
            }
        } else if (arg[1] != '-') {
            arg++; // eat '-'

            switch (*arg) {
                case 'L':
                    if (arg[1] == '\0') {
                        if (i == argc - 1) {
                            ::std::cerr << "Option " << arg << " requires an argument" << ::std::endl;
                            exit(1);
                        }
                        addLibrarySearchDir(argv[++i]);
                    } else {
                        addLibrarySearchDir(arg + 1);
                    }
                    continue;
                case 'l':
                    if (arg[1] == '\0') {
                        if (i == argc - 1) {
                            ::std::cerr << "Option " << arg << " requires an argument" << ::std::endl;
                            exit(1);
                        }
                        this->libraries.push_back(argv[++i]);
                    } else {
                        this->libraries.push_back(arg + 1);
                    }
                    continue;
                case 'A':
                case 'W':
                case 'D':
                case 'F': {
                    const auto flag = *arg;
                    const char* lintName;
                    if (arg[1] == '\0') {
                        if (i == argc - 1) {
                            ::std::cerr << "Option -" << flag << " requires an argument" << ::std::endl;
                            exit(1);
                        }
                        lintName = argv[++i];
                    } else {
                        lintName = arg + 1;
                    }
                    if (lintName[0] == '\0') {
                        ::std::cerr << "Option -" << flag << " requires an argument" << ::std::endl;
                        exit(1);
                    }
                    const auto level = flag == 'A' ? CfgLintLevel::Allow : flag == 'W' ? CfgLintLevel::Warn : flag == 'D' ? CfgLintLevel::Deny : CfgLintLevel::Forbid;
                    CfgSetLintLevel(settings, lintName, level);
                    continue;
                }
                case 'C': {
                    ::std::string optname;
                    ::std::string optval;
                    if (arg[1] == '\0') {
                        if (i == argc - 1) {
                            ::std::cerr << "Option " << arg << " requires an argument" << ::std::endl;
                            exit(1);
                        }
                        optname = argv[++i];
                    } else {
                        optname = arg + 1;
                    }
                    auto eqPos = optname.find('=');
                    if (eqPos != ::std::string::npos) {
                        optval = optname.substr(eqPos + 1);
                        optname.resize(eqPos);
                    }
                    auto getOptval = [&]() {
                        if (eqPos == ::std::string::npos) {
                            ::std::cerr << "Flag -C " << optname << " requires an argument" << ::std::endl;
                            exit(1);
                        }
                    };
                    //        ::std::cerr << "Flag -C " << optname << " doesn't take an argument" << ::std::endl;
                    //    }

                    if (optname == "emit-cpp-only") {
                        this->codegen.emitCppOnly = true;
                    } else if (optname == "emit-build-command") {
                        getOptval();
                        this->codegen.emitBuildCommand = optval;
                    } else if (optname == "codegen-type") {
                        getOptval();
                        this->codegen.codegenType = optval;
                    } else if (optname == "emit-depfile") {
                        getOptval();
                        this->emitDepfile = optval;
                    } else if (optname == "panic") {
                        getOptval();
                        this->codegen.panicType = optval;
                    } else if (optname == "link-arg") {
                        getOptval();
                        this->codegen.linkerArgs.push_back(optval);
                    } else if (optname == "link-args") {
                        getOptval();
                        size_t start = 0;
                        while (start < optval.size()) {
                            while (start < optval.size() && (optval[start] == ' ' || optval[start] == '\t')) {
                                start += 1;
                            }
                            auto end = start;
                            while (end < optval.size() && optval[end] != ' ' && optval[end] != '\t') {
                                end += 1;
                            }
                            if (start != end) {
                                this->codegen.linkerArgs.push_back(optval.substr(start, end - start));
                            }
                            start = end;
                        }
                    } else if (optname == "overflow-checks" || optname == "overflow_checks") {
                        getOptval();
                        if (optval == "n" || optval == "no" || optval == "off" || optval == "false") {
                            this->overflowChecks = false;
                        } else if (optval == "y" || optval == "yes" || optval == "on" || optval == "true") {
                            this->overflowChecks = true;
                        } else {
                            ::std::cerr << "invalid value for -C " << optname << ": '" << optval << "'" << ::std::endl;
                            exit(1);
                        }
                        this->overflowChecksExplicit = true;
                    } else if (optname == "opt-level") {
                        getOptval();
                        if (optval == "0") {
                            this->optLevel = OptimizationLevel::None;
                        } else if (optval == "1") {
                            this->optLevel = OptimizationLevel::Less;
                        } else if (optval == "2") {
                            this->optLevel = OptimizationLevel::More;
                        } else if (optval == "3") {
                            this->optLevel = OptimizationLevel::Aggressive;
                        } else if (optval == "s") {
                            this->optLevel = OptimizationLevel::Size;
                        } else if (optval == "z") {
                            this->optLevel = OptimizationLevel::SizeMin;
                        } else {
                            ::std::cerr << "optimization level needs to be between 0-3, s or z (instead was '" << optval << "')" << ::std::endl;
                            exit(1);
                        }
                    } else if (optname == "debug-assertions" || optname == "debug_assertions") {
                        if (eqPos == ::std::string::npos || optval == "y" || optval == "yes" || optval == "on" || optval == "true") {
                            this->debugAssertions = true;
                        } else if (optval == "n" || optval == "no" || optval == "off" || optval == "false") {
                            this->debugAssertions = false;
                        } else {
                            ::std::cerr << "invalid value for -C debug-assertions: '" << optval << "'" << ::std::endl;
                            exit(1);
                        }
                        this->debugAssertionsExplicit = true;
                    } else if (optname == "target-feature") {
                        getOptval();
                        size_t start = 0;
                        while (start <= optval.size()) {
                            const auto end = optval.find(',', start);
                            const auto feature = optval.substr(start, end == ::std::string::npos ? ::std::string::npos : end - start);
                            if (feature != "-crt-static") {
                                ::std::cerr << "unsupported value for -C target-feature: '" << feature
                                            << "' (trustme only supports -crt-static)" << ::std::endl;
                                exit(1);
                            }
                            if (end == ::std::string::npos) {
                                break;
                            }
                            start = end + 1;
                        }
                    } else if (optname == "debuginfo") {
                        getOptval();
                        if (optval == "0" || optval == "none") {
                            this->debugInfo = DebugInfoLevel::None;
                        } else if (optval == "line-directives-only") {
                            this->debugInfo = DebugInfoLevel::LineDirectivesOnly;
                        } else if (optval == "line-tables-only") {
                            this->debugInfo = DebugInfoLevel::LineTablesOnly;
                        } else if (optval == "1" || optval == "limited") {
                            this->debugInfo = DebugInfoLevel::Limited;
                        } else if (optval == "2" || optval == "full") {
                            this->debugInfo = DebugInfoLevel::Full;
                        } else {
                            ::std::cerr << "invalid value for -C debuginfo: '" << optval << "'" << ::std::endl;
                            exit(1);
                        }
                    } else {
                        ::std::cerr << "Unknown codegen option: '" << optname << "'" << ::std::endl;
                        exit(1);
                    }
                }
                    continue;
                case 'Z': {
                    ::std::string optname;
                    ::std::string optval;
                    if (arg[1] == '\0') {
                        if (i == argc - 1) {
                            ::std::cerr << "Option " << arg << " requires an argument" << ::std::endl;
                            exit(1);
                        }
                        optname = argv[++i];
                    } else {
                        optname = arg + 1;
                    }
                    auto eqPos = optname.find('=');
                    if (eqPos != ::std::string::npos) {
                        optval = optname.substr(eqPos + 1);
                        optname.resize(eqPos);
                    }
                    auto getOptval = [&]() {
                        if (eqPos == ::std::string::npos) {
                            ::std::cerr << "Flag -Z " << optname << " requires an argument" << ::std::endl;
                            exit(1);
                        }
                    };
                    auto noOptval = [&]() {
                        if (eqPos != ::std::string::npos) {
                            ::std::cerr << "Flag -Z " << optname << " doesn't take an argument" << ::std::endl;
                            exit(1);
                        }
                    };

                    if (optname == "disable-mir-opt") {
                        noOptval();
                        this->mirOptLevel = 0;
                        this->mirOptLevelExplicit = true;
                    } else if (optname == "mir-opt-level") {
                        getOptval();
                        if (optval.empty()) {
                            ::std::cerr << "Invalid number for -Z mir-opt-level: '" << optval << "'" << ::std::endl;
                            exit(1);
                        }
                        unsigned value = 0;
                        for (const char c : optval) {
                            if (c < '0' || c > '9') {
                                ::std::cerr << "Invalid number for -Z mir-opt-level: '" << optval << "'" << ::std::endl;
                                exit(1);
                            }
                            const unsigned digit = c - '0';
                            if (value > (UINT_MAX - digit) / 10) {
                                ::std::cerr << "Number for -Z mir-opt-level is too large: '" << optval << "'" << ::std::endl;
                                exit(1);
                            }
                            value = value * 10 + digit;
                        }
                        this->mirOptLevel = value;
                        this->mirOptLevelExplicit = true;
                    } else if (optname == "ub-checks" || optname == "ub_checks") {
                        if (eqPos == ::std::string::npos || optval == "y" || optval == "yes" || optval == "on" || optval == "true") {
                            this->ubChecks = true;
                        } else if (optval == "n" || optval == "no" || optval == "off" || optval == "false") {
                            this->ubChecks = false;
                        } else {
                            ::std::cerr << "invalid value for -Z ub-checks: '" << optval << "'" << ::std::endl;
                            exit(1);
                        }
                        this->ubChecksExplicit = true;
                    } else if (optname == "fmt-debug") {
                        getOptval();
                        if (optval == "full") {
                            this->fmtDebug = Settings::FmtDebug::Full;
                        } else if (optval == "shallow") {
                            this->fmtDebug = Settings::FmtDebug::Shallow;
                        } else if (optval == "none") {
                            this->fmtDebug = Settings::FmtDebug::None;
                        } else {
                            ::std::cerr << "invalid value for -Z fmt-debug: '" << optval << "' (expected 'full', 'shallow', or 'none')" << ::std::endl;
                            exit(1);
                        }
                    } else if (optname == "next-solver") {
                        if (eqPos == ::std::string::npos || optval == "globally") {
                            this->traitSolver.coherence = true;
                            this->traitSolver.globally = true;
                        } else if (optval == "coherence") {
                            this->traitSolver.coherence = true;
                            this->traitSolver.globally = false;
                        } else if (optval == "no") {
                            this->traitSolver.coherence = false;
                            this->traitSolver.globally = false;
                        } else {
                            ::std::cerr << "Invalid value for -Z next-solver: '" << optval << "' (expected 'no', 'coherence', or 'globally')" << ::std::endl;
                            exit(1);
                        }
                    } else if (optname == "dump-ast") {
                        noOptval();
                        this->debug.dumpAst = true;
                    } else if (optname == "dump-hir") {
                        noOptval();
                        this->debug.dumpHir = true;
                    } else if (optname == "dump-mir") {
                        noOptval();
                        this->debug.dumpMir = true;
                    } else if (optname == "parse-crate-root-only") {
                        // rustc's spelling of "stop once the crate root parses".
                        noOptval();
                        this->lastStage = STAGE_PARSE;
                    } else if (optname == "stop-after") {
                        getOptval();
                        if (optval == "parse") {
                            this->lastStage = STAGE_PARSE;
                        } else if (optval == "expand") {
                            this->lastStage = STAGE_EXPAND;
                        } else if (optval == "resolve") {
                            this->lastStage = STAGE_RESOLVE;
                        } else if (optval == "typeck") {
                            this->lastStage = STAGE_TYPECK;
                        } else if (optval == "hir") {
                            this->lastStage = STAGE_HIR;
                        } else if (optval == "mir") {
                            this->lastStage = STAGE_MIR;
                        } else {
                            ::std::cerr << "Unknown argument to -Z stop-after - '" << optval << "'" << ::std::endl;
                            exit(1);
                        }
                    } else if (optname == "pause-after-start") {
                        this->debug.pause = true;
                    } else if (optname == "unpretty") {
                        // A pretty-printer selects how far the compiler runs
                        // before printing.  `normal` prints the parsed source
                        // and `expanded`/`ast-tree` the expanded crate, so
                        // neither reaches name resolution -- code that only
                        // makes sense before it is still a valid input.
                        getOptval();
                        auto form = optval.substr(0, optval.find(','));
                        if (form == "normal" || form == "identified") {
                            this->lastStage = STAGE_PARSE;
                        } else if (form == "expanded" || form == "ast-tree") {
                            this->lastStage = STAGE_EXPAND;
                        } else if ((form == "hir" || form == "hir-tree") && optval.find("typed") == ::std::string::npos) {
                            // The HIR is printed once names are resolved; only
                            // the `typed` variant needs the types as well.
                            this->lastStage = STAGE_HIR;
                        } else {
                            // `hir`, `thir` and `mir` forms need the passes
                            // that build them, so they run the whole compiler.
                        }
                    } else if (optname == "print-cfgs") {
                        noOptval();
                        this->printCfgs = true;
                    } else if (optname == "check-cfg-all-expected") {
                        // This only controls how many expected cfg values rustc
                        // prints in diagnostics.  trustme emits a compact
                        // diagnostic and has no corresponding display limit.
                        noOptval();
                    } else {
                        // Unstable rustc switches are routinely attached to
                        // upstream tests to select a rustc pass, diagnostic,
                        // or pretty-printer.  They are not language input and
                        // an unsupported one must not make an otherwise valid
                        // crate unusable by this compiler.
                    }
                }
                    continue;

                default:
                    // Fall through to the for loop below
                    break;
            }

            for (; *arg; arg++) {
                switch (*arg) {
                    // "-o <file>" : Set output file
                    case 'o':
                        if (i == argc - 1) {
                            ::std::cerr << "Option -" << *arg << " requires an argument" << ::std::endl;
                            exit(1);
                        }
                        this->outfile = argv[++i];
                        break;
                    case 'O':
                        this->optLevel = OptimizationLevel::Aggressive;
                        break;
                    case 'g':
                        this->debugInfo = DebugInfoLevel::Full;
                        break;
                    default:
                        ::std::cerr << "Unknown option: '-" << *arg << "'" << ::std::endl;
                        exit(1);
                }
            }
        } else {
            auto checkWithArg = [&](const char* name) -> const char* {
                if (strcmp(arg + 2, name) == 0) {
                    if (i == argc - 1) {
                        ::std::cerr << "Flag " << arg << " requires an argument" << ::std::endl;
                        exit(1);
                    }
                    return argv[++i];
                }
                if (strncmp(arg + 2, name, strlen(name)) == 0 && arg[2 + strlen(name)] == '=') {
                    return arg + 2 + strlen(name) + 1;
                }
                return nullptr;
            };

            if (strcmp(arg, "--help") == 0) {
                this->showHelp();
                exit(0);
            } else if (strcmp(arg, "--version") == 0) {
                const char* rustcTarget = RUSTC_TARGET_VERSION;
                // NOTE: Starts the version with "rustc 1.29.100" so build scripts don't get confused
                ::std::cout << "rustc " << rustcTarget << ".100 (trustme " << VersionGetString() << ")" << ::std::endl;
                ::std::cout << "release: " << rustcTarget << ".100" << ::std::endl; // `autoconfig` looks for this line
                ::std::cout << "- Build time: " << gsVersionBuildTime << ::std::endl;
                ::std::cout << "- Commit: " << gsVersionGitHash << (gbVersionGitDirty ? " (dirty tree)" : "") << ::std::endl;
                exit(0);
            }
            // --out-dir <dir>  >> Set the output directory for automatically-named files
            else if (const char* outDir = checkWithArg("out-dir")) {
                this->outputDir = outDir;
                if (this->outputDir != "" && this->outputDir.back() != '/') {
                    this->outputDir += '/';
                }
            }
            // --extern <name>=<path>   >> Override the file to load for `extern crate <name>;`
            else if (strcmp(arg, "--extern") == 0) {
                if (i == argc - 1) {
                    ::std::cerr << "Option " << arg << " requires an argument" << ::std::endl;
                    exit(1);
                }
                const char* desc = argv[++i];
                const char* pos = ::std::strchr(desc, '=');
                // `--extern name` with no path names a crate to load from the
                // search directories, the sysroot included; only `name=path`
                // says where it is.
                auto name = pos ? ::std::string(desc, pos) : ::std::string(desc);
                auto path = pos ? ::std::string(pos + 1) : ::std::string();
                this->crateOverrides.insert(::std::make_pair(mv$(name), mv$(path)));
            }
            // --crate-tag <name>  >> Specify a version/identifier suffix for the crate
            else if (const auto* nameStr = checkWithArg("crate-tag")) {
                this->crateNameSuffix = nameStr;
            }
            // --crate-name <name>  >> Specify the crate name (overrides `#![crate_name="<name>"]`)
            else if (const auto* nameStr = checkWithArg("crate-name")) {
                this->crateName = nameStr;
            }
            // `--crate-type <name>`    - Specify the crate type (overrides `#![crate_type="<name>"]`)
            else if (const char* typeStr = checkWithArg("crate-type")) {
                if (strcmp(typeStr, "lib") == 0 || strcmp(typeStr, "rlib") == 0) {
                    this->crateType = ASTCrate::Type::RustLib;
                } else if (strcmp(typeStr, "dylib") == 0) {
                    this->crateType = ASTCrate::Type::RustDylib;
                } else if (strcmp(typeStr, "bin") == 0) {
                    this->crateType = ASTCrate::Type::Executable;
                } else if (strcmp(typeStr, "proc-macro") == 0) {
                    this->crateType = ASTCrate::Type::ProcMacro;
                } else {
                    ::std::cerr << "Unknown value for --crate-type: " << typeStr << ::std::endl;
                    exit(1);
                }
            }
            // `--cfg <flag>` / `--cfg=<flag>`
            // `--cfg <var>=<value>` / `--cfg=<var>=<value>`
            else if (const char* cfgSpec = checkWithArg("cfg")) {
                ::std::string name;
                ::std::string value;
                ::std::string error;
                bool has_value = false;
                if (!CfgParseOption(cfgSpec, name, has_value, value, error)) {
                    ::std::cerr << "invalid `--cfg` argument: `" << cfgSpec << "`: " << error << ::std::endl;
                    exit(1);
                }
                if (has_value) {
                    if (name == "feature") {
                        this->features.insert(value);
                    } else {
                        CfgSetValue(settings, mv$(name), mv$(value));
                    }
                } else {
                    CfgSetFlag(settings, mv$(name));
                }
            } else if (const char* checkCfgSpec = checkWithArg("check-cfg")) {
                ::std::string error;
                if (!CfgSetCheckSpec(settings, checkCfgSpec, error)) {
                    ::std::cerr << "invalid `--check-cfg` argument: `" << checkCfgSpec << "`: " << error << ::std::endl;
                    exit(1);
                }
            } else if (const char* envSpec = checkWithArg("env-set")) {
                const char* separator = ::std::strchr(envSpec, '=');
                if (separator == nullptr || separator == envSpec) {
                    ::std::cerr << "--env-set takes an argument of the form NAME=VALUE" << ::std::endl;
                    exit(1);
                }
                const ::std::string name(envSpec, separator);
                if (::setenv(name.c_str(), separator + 1, 1) != 0) {
                    ::std::cerr << "failed to set compile-time environment variable '" << name << "'" << ::std::endl;
                    exit(1);
                }
            } else if (const char* forceWarn = checkWithArg("force-warn")) {
                if (forceWarn[0] == '\0') {
                    ::std::cerr << "Flag --force-warn requires an argument" << ::std::endl;
                    exit(1);
                }
                CfgSetLintLevel(settings, forceWarn, CfgLintLevel::ForceWarn);
            } else if (const char* lintCap = checkWithArg("cap-lints")) {
                CfgLintLevel level;
                if (strcmp(lintCap, "allow") == 0) {
                    level = CfgLintLevel::Allow;
                } else if (strcmp(lintCap, "warn") == 0) {
                    level = CfgLintLevel::Warn;
                } else if (strcmp(lintCap, "deny") == 0) {
                    level = CfgLintLevel::Deny;
                } else if (strcmp(lintCap, "forbid") == 0) {
                    level = CfgLintLevel::Forbid;
                } else {
                    ::std::cerr << "unknown lint level: `" << lintCap << "`" << ::std::endl;
                    exit(1);
                }
                CfgSetLintCap(settings, level);
            } else if (const char* emit = checkWithArg("emit")) {
                // `--emit=metadata` asks for the crate to be analysed but not
                // built: nothing is codegenned and nothing is linked, so a
                // crate that only declares an external symbol is still valid.
                if (::std::strcmp(emit, "metadata") == 0) {
                    this->emitMetadataOnly = true;
                } else {
                    ::std::cerr << "Ignoring `--emit " << emit << "` for compatability with rustc" << std::endl;
                }
            }
            // `--target <triple>`  - Override the default compiler target
            else if (const char* targetName = checkWithArg("target")) {
                this->target = targetName;
            } else if (strcmp(arg, "--dump-target-spec") == 0) {
                if (i == argc - 1) {
                    ::std::cerr << "Flag " << arg << " requires an argument" << ::std::endl;
                    exit(1);
                }
                this->targetSaveback = argv[++i];
            } else if (strcmp(arg, "--test") == 0) {
                this->testHarness = true;
            } else if (const char* editionStr = checkWithArg("edition")) {
                if (strcmp(editionStr, "2015") == 0) {
                    this->edition = ASTEdition::Rust2015;
                } else if (strcmp(editionStr, "2018") == 0) {
                    this->edition = ASTEdition::Rust2018;
                } else if (strcmp(editionStr, "2021") == 0) {
                    this->edition = ASTEdition::Rust2021;
                } else if (strcmp(editionStr, "2024") == 0) {
                    this->edition = ASTEdition::Rust2024;
                } else {
                    ::std::cerr << "Unknown value for " << arg << " - '" << editionStr << "'" << ::std::endl;
                    exit(1);
                }
            } else {
                ::std::cerr << "Unknown option '" << arg << "'" << ::std::endl;
                exit(1);
            }
        }
    }

    if (const auto* a = getenv("TRUSTME_DUMP")) {
        while (a[0]) {
            const char* end = strchr(a, ':');

            ::std::string_view s;
            if (end) {
                s = ::std::string_view{a, end};
                a = end + 1;
            } else {
                end = a + strlen(a);
                s = ::std::string_view{a, end};
                a = end;
            }

            if (s == "") {
                // Ignore
            } else if (s == "ast") {
                this->debug.dumpAst = true;
            } else if (s == "hir") {
                this->debug.dumpHir = true;
            } else if (s == "mir") {
                this->debug.dumpMir = true;
            } else {
                ::std::cerr << "Unknown option in $TRUSTME_DUMP '" << s << "'" << ::std::endl;
                // - No terminate, just warn
            }
        }
    }
}

void ProgramParams::showHelp() const {
    ::std::cout << "USAGE: rustc <sourcefile>\n"
                   "\n"
                   "OPTIONS:\n"
                   "-L [kind=]<dir>    : Search for crates or native libraries in this directory\n"
                   "-o <filename>      : Write compiler output (library or executable) to this file\n"
                   "-O                 : Enable optimisation\n"
                   "-g                 : Emit debugging information\n"
                   "--out-dir <dir>    : Specify the output directory (alternative to `-o`)\n"
                   "--extern <crate>=<path>\n"
                   "                   : Specify the path for a given crate (instead of searching for it)\n"
                   "--crate-tag <str>  : Specify a suffix for symbols and output files\n"
                   "--crate-name <str> : Override/set the crate name\n"
                   "--crate-type <ty>  : Override/set the crate type (rlib, bin, proc-macro)\n"
                   "--cfg flag         : Set a boolean #[cfg]/cfg! flag\n"
                   "--cfg flag=\"val\"   : Set a string #[cfg]/cfg! flag\n"
                   "--target <name>    : Compile code for the given target\n"
                   "--test             : Generate a unit test executable\n"
                   "-C <option>        : Code-generation options\n"
                   "-Z <option>        : Debugging/experimental options\n";
}
