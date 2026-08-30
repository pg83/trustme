#include "main_bindings.h"

#include "ast_ast.h"
#include "hir_hir.h"
#include "ast_dump.h"
#include "ast_expr.h"
#include "settings.h"
#include "ast_crate.h"
#include "parse_lex.h"
#include "expand_cfg.h"
#include "lang_items.h"
#include "wire_board.h"
#include "lint_forbid.h"
#include "memory_dump.h"
#include "output_file.h"
#include "hir_from_ast.h"
#include "mir_from_hir.h"
#include "parse_common.h"
#include "trans_target.h"
#include "expand_common.h"
#include "lint_must_use.h"
#include "target_detect.h"
#include "trans_codegen.h"
#include "mir_operations.h"
#include "lint_unsafe_code.h"
#include "parse_parseerror.h"
#include "expand_proc_macro.h"
#include "hir_main_bindings.h"
#include "mir_main_bindings.h"
#include "hir_inherent_cache.h"
#include "trans_monomorphise.h"
#include "trans_main_bindings.h"
#include "hir_typeck_expr_visit.h"
#include "resolve_main_bindings.h"
#include "hir_conv_main_bindings.h"
#include "hir_expand_main_bindings.h"
#include "hir_typeck_main_bindings.h"
#include "hir_conv_constant_evaluation.h"

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

#include <set>
#include <string>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <pthread.h>

using namespace stl;

#define NEWNODE(ty, ...) makeAstExprNode<ASTExprNode##ty>(*crate.pool __VA_OPT__(, ) __VA_ARGS__)

namespace {
    Vector<RcString> pathNodes(const char* first, const char* second = nullptr) {
        Vector<RcString> nodes(second ? 2 : 1);
        nodes.pushBack(RcString(first));
        if (second) {
            nodes.pushBack(RcString(second));
        }
        return nodes;
    }

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

        bool emitMetadataOnly = false;

        std::string infile;
        std::string outfile;
        std::string outputDir = "";
        std::string target = DEFAULT_TARGET_NAME;
        RcString crateNameQuery;

        std::string emitDepfile;

        ASTEdition edition = ASTEdition::Rust2015;
        ASTCrate::Type crateType = ASTCrate::Type::Unknown;
        std::string crateName;
        std::string crateNameSuffix;

        OptimizationLevel optLevel = OptimizationLevel::None;
        bool debugAssertions = false;
        bool debugAssertionsExplicit = false;
        bool ubChecks = false;
        bool ubChecksExplicit = false;
        Settings::FmtDebug fmtDebug = Settings::FmtDebug::Full;
        bool overflowChecks = false;
        bool overflowChecksExplicit = false;
        unsigned mirOptLevel = 0;
        bool mirOptLevelExplicit = false;
        DebugInfoLevel debugInfo = DebugInfoLevel::None;

        bool testHarness = false;

        std::string targetSaveback;
        bool printCfgs = false;

        std::vector<std::string> crateSearchDirs;
        std::vector<std::string> nativeLibSearchDirs;
        std::vector<std::string> frameworkSearchDirs;
        Vector<const char*> libraries;
        std::set<std::string> features;

        struct {
            bool pause = false;

            bool dumpAst = false;
            bool dumpHir = false;
            bool dumpMir = false;
        } debug;

        struct {
            std::string codegenType;
            std::string emitBuildCommand;
            RcString emitLinkManifest;
            bool emitCppOnly = false;
            std::string panicType;
            std::vector<std::string> linkerArgs;
        } codegen;

        ProgramParams(Settings& settings, int argc, char* argv[]);

        unsigned effectiveMirOptLevel() const;

        bool enableMirInlining() const;

        bool debugAssertionsEnabled() const;

        bool ubChecksEnabled() const;

        bool overflowChecksEnabled() const;

        void showHelp() const;
    };

    struct CompileArgs {
        int argc;
        char** argv;
        int result;
    };

    std::string CrateNameFromFile(const std::string& infile) {
        auto s = infile.find_last_of('/');
        s = (s == std::string::npos ? 0 : s + 1);
        auto s2 = infile.find_last_of('\\');
        s2 = (s2 == std::string::npos ? 0 : s2 + 1);
        s = std::max(s, s2);
        auto e = infile.find_first_of('.', s);
        if (e == std::string::npos) {
            e = infile.size();
        }

        std::string rv(infile.begin() + s, infile.begin() + e);
        for (auto& b : rv) {
            if (b == '-') {
                b = '_';
            }
        }
        return rv;
    }

    static int compile(int argc, char* argv[]) {
#if TRUSTME_SANITIZER_BUILD
        auto poolOwner = ObjPool::fromMemory();
        auto* pool = poolOwner.mutPtr();
#else
        auto* pool = ObjPool::fromMemoryRaw();
#endif
        WireBoard& wb = *pool->make<WireBoard>(pool);
        unsigned memoryDumpSequence = 0;
        wb.types = pool->make<HIRTypeInterner>(*pool, wb.id);
        wb.settings = pool->make<Settings>(pool);
        wb.settings->cfg = CfgCreateState(*pool);
        ProgramParams params(*wb.settings, argc, argv);
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
            sysE << StringView("Pausing to attach a debugger\nType any text to continue") << endL;
            std::cin >> c;
        }

        wb.inherentMethods = HIRInherentCache::create(*pool);

        {
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
            CfgSetValue(*wb.settings, "fmt_debug", params.fmtDebug == Settings::FmtDebug::Shallow ? "shallow" : params.fmtDebug == Settings::FmtDebug::None ? "none" : "full");
            CfgSetValueCb(*wb.settings, "feature", [&params](const std::string& s) {
                return params.features.count(s) != 0;
            });
        }
        {
            TargetSetCfg(wb, params.target);
        }
        if (params.printCfgs) {
            auto out = sysO;
            CfgDump(*wb.settings, out);
            return 0;
        }
        if (params.crateNameQuery != "") {
            sysO << HIRDeserialiseJustName(params.crateNameQuery.c_str()) << endL;
            return 0;
        }
        if (params.targetSaveback != "") {
            TargetExportCurSpec(wb, params.targetSaveback);
            return 0;
        }

        if (params.infile == "") {
            sysE << StringView("No input file passed") << endL;
            return 1;
        }

        if (params.testHarness) {
            CfgSetFlag(*wb.settings, "test");
        }

        ExpandInit(*wb.expandRegistry);

#if TRUSTME_SANITIZER_BUILD
        auto astPoolOwner = ObjPool::fromMemory();
        auto* astPool = astPoolOwner.mutPtr();
#else
        auto* astPool = ObjPool::fromMemoryRaw();
#endif
        wb.astPool = astPool;

        {
            ASTCrate* cratePtr = [&]() {
                return ParseCrate(wb, wb.astPool, params.infile, params.edition);
            }();
            ASTCrate& crate = *cratePtr;
            wb.astCrate = cratePtr;
            crate.testHarness = params.testHarness;
            crate.crateNameSuffix = params.crateNameSuffix;

            if (params.lastStage == ProgramParams::STAGE_PARSE) {
                return 0;
            }
            memoryDump(memoryDumpSequence, "Parsed");

            {
                for (const auto& ld : params.crateSearchDirs) {
                    wb.settings->crateLoadDirs.push_back(ld);
                }
                crate.loadExterns(*wb.settings);
                if (params.testHarness) {
                    auto testCrateName = RcString::newInterned("test");
                    wb.settings->implicitCrates.insert(std::make_pair(testCrateName, crate.loadExternCrate(*wb.settings, Span(), testCrateName)));
                }
            }
            {
                auto crateType = params.crateType;
                if (crateType == ASTCrate::Type::Unknown) {
                    crateType = crate.crateType;
                }
                if (crateType == ASTCrate::Type::Unknown) {
                    crateType = ASTCrate::Type::Executable;
                }
                crate.crateType = crateType;

                crate.setCrateName(params.crateName != "" ? params.crateName : CrateNameFromFile(params.infile));
                crate.crateType = ASTCrate::Type::Unknown;
            }

            {
                Expand(wb, crate);

                if (params.testHarness) {
                    ExpandTestHarness(crate);
                }
            }
            {
                LintCheckForbid(wb, crate);
            }
            auto crateType = params.crateType;
            if (crateType == ASTCrate::Type::Unknown) {
                crateType = crate.crateType;
            }
            if (crateType == ASTCrate::Type::Unknown) {
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
                        params.outfile = FMT(params.outputDir << StringView("lib") << crate.crateNameSet << StringView(".rlib"));
                        break;
                    case ASTCrate::Type::Executable:
                        params.outfile = FMT(params.outputDir << crate.crateNameSet);
                        break;
                    case ASTCrate::Type::ProcMacro:
                        params.outfile = FMT(params.outputDir << StringView("lib") << crate.crateNameSet << StringView("-plugin"));
                        break;
                    default:
                        params.outfile = FMT(params.outputDir << crate.crateNameSet << StringView(".o"));
                        break;
                }
                DEBUG(StringView("params.outfile = ") << params.outfile);
            }

            if (params.debug.dumpAst) {
                {
                    DumpRust(FMT(params.outfile << StringView("_1_ast.rs")).c_str(), crate);
                }
            }

            if (params.lastStage == ProgramParams::STAGE_EXPAND) {
                return 0;
            }
            memoryDump(memoryDumpSequence, "Expanded");

            {
                if (crate.crateType == ASTCrate::Type::Executable || params.testHarness || crate.crateType == ASTCrate::Type::ProcMacro) {
                    bool allocatorCrateLoaded = false;
                    RcString allocCrateName;
                    bool panicRuntimeLoaded = false;
                    RcString panicCrateName;
                    bool panicRuntimeNeeded = false;
                    for (const auto& ec : crate.externCrates) {
                        DEBUG(StringView("Looking at lang items from ") << ec.first << StringView(" : ") << FMT_CB(ss, for (const auto& item : ec.second.hir->langItems) ss << item << ',';));
                        if (ec.second.hir->langItems.count("trustme-allocator")) {
                            if (allocatorCrateLoaded) {
                                ERROR(Span(), E0000, StringView("Multiple allocator crates loaded - ") << allocCrateName << StringView(" and ") << ec.first);
                            }
                            allocCrateName = ec.first;
                            allocatorCrateLoaded = true;
                        }
                        if (ec.second.hir->langItems.count("trustme-panic_runtime")) {
                            if (panicRuntimeLoaded) {
                                WARNING(Span(), W0000, StringView("Multiple panic_runtime crates loaded - ") << panicCrateName << StringView(" and ") << ec.first);
                            } else {
                                panicCrateName = ec.first;
                                panicRuntimeLoaded = true;
                            }
                        }
                        if (ec.second.hir->langItems.count("trustme-needs_panic_runtime")) {
                            panicRuntimeNeeded = true;
                        }
                    }
                    allocatorCrateLoaded = true;
                    if (!allocatorCrateLoaded) {
                        crate.loadExternCrate(*wb.settings, Span(), "alloc_system");
                    }

                    if (panicRuntimeNeeded /*&& !panic_runtime_loaded*/) {
                        auto panicCrate = "panic_" + params.codegen.panicType;
                        crate.loadExternCrate(*wb.settings, Span(), panicCrate.c_str());
                    }

                    if (!crate.noMain) {
                        crate.langItems.insert(std::make_pair(std::string("trustme-main"), ASTAbsolutePath("", pathNodes("main"))));
                    }
                }
            }
            if (params.emitDepfile != "") {
                struct PathEnumerator {
                    std::vector<std::string> out;

                    void visitModule(ASTModule& mod) {
                        if (mod.fileInfo.path != "!" && mod.fileInfo.path.back() != '/') {
                            out.push_back(mod.fileInfo.path);
                        }
                        // TODO: Should we check anon modules?

                        for (auto& i : mod.items) {
                            if (i->data.is_Module()) {
                                this->visitModule(i->data.as_Module());
                            }
                        }
                    }
                };

                PathEnumerator pe;
                pe.visitModule(crate.rootModule_);

                OutputFile of{params.emitDepfile};
                // TODO: Escape spaces and colons in these paths
                of << params.outfile << StringView(": ") << params.infile;
                for (const auto& modPath : pe.out) {
                    of << StringView(" ") << modPath;
                }
                of << endL;

                of << params.outfile << StringView(":");
                for (const auto& ec : crate.externCrates) {
                    of << StringView(" ") << ec.second.filename;
                }
            }

            {
                ResolveUse(wb, crate);
            }
            {
                ResolveIndex(crate);
            }
            {
                ResolveAbsolutise(wb, crate);
            }
            memoryDump(memoryDumpSequence, "Resolved");

            if (params.debug.dumpAst) {
                {
                    DumpRust(FMT(params.outfile << StringView("_1_ast.rs")).c_str(), crate);
                }
            }

            if (params.lastStage == ProgramParams::STAGE_RESOLVE) {
                return 0;
            }

            HIRCrate* hirCrate = [&]() {
                return LowerHIRFromAST(wb, pool, crate);
            }();
            wb.crate = hirCrate;
            wb.langItems = LangItems::create(*pool, *hirCrate);
            memoryDump(memoryDumpSequence, "HIR Gen");

            {
                wb.astCrate = nullptr;
                wb.astPool = nullptr;
#if !TRUSTME_SANITIZER_BUILD
                delete astPool;
#endif
                astPool = nullptr;
            }
            memoryDump(memoryDumpSequence, "AST Dropped");
            if (params.debug.dumpHir) {
                {
                    OutputFile os(FMT(params.outfile << StringView("_2_hir.rs")));
                    HIRDump(os, *hirCrate);
                }
            }
            memoryDump(memoryDumpSequence, "HIR");

            {
                ConvertHIRExpandAliases(wb, *hirCrate);
            }
            {
                ConvertHIRValidateReceivers(wb, *hirCrate);
            }
            {
                ConvertHIRBind(wb, *hirCrate);
            }
            {
                ConvertHIRIndexInherentMethods(wb, *hirCrate);
            }
            {
                ConvertHIRResolveUFCSOuter(wb, *hirCrate);
            }

            // - TODO: Move this later on, but that requires fixing some of the resolve logic around trait impl lookup
            {
                ConvertHIRExpandAliasesSelf(*hirCrate);
            }
            {
                ConvertHIRMarkings(wb, *hirCrate);
            }
            {
                ConvertHIRResolveUFCSSortImpls(wb, *hirCrate);
            }
            {
                ConvertHIRResolveUFCS(wb, *hirCrate);
            }
            if (params.debug.dumpHir) {
                {
                    OutputFile os(FMT(params.outfile << StringView("_2_hir.rs")));
                    HIRDump(os, *hirCrate);
                }
            }
            // TODO: Expand vtables here?

            if (params.lastStage == ProgramParams::STAGE_HIR) {
                return 0;
            }

            {
                ConvertHIRConstantEvaluate(wb, *hirCrate);
            }
            if (params.debug.dumpHir) {
                {
                    OutputFile os(FMT(params.outfile << StringView("_2_hir.rs")));
                    HIRDump(os, *hirCrate);
                }
            }

            {
                TypecheckModuleLevel(wb, *hirCrate);
            }
            {
                TypecheckExpressions(wb, *hirCrate);
            }
            {
                LintUnusedMustUse(wb, *hirCrate);
                LintUnsafeCode(wb, *hirCrate);
            }
            {
                HIRExpandAnnotateUsage(wb, *hirCrate);
            }
            {
                HIRExpandStaticBorrowConstantsMark(wb, *hirCrate);
            }
            {
                HIRExpandClosures(wb, *hirCrate);
            }
            {
                HIRExpandStaticBorrowConstants(wb, *hirCrate);
            }

            //  TODO: How early can this be done?

            //  TODO: Would prefer to have this done before consteval, as consteval might reference a vtable
            {
                HIRExpandVTables(wb, *hirCrate);
            }
            {
                HIRExpandUfcsEverything(wb, *hirCrate);
            }
            {
                HIRExpandReborrows(wb, *hirCrate);
            }
            {
                HIRExpandErasedType(wb, *hirCrate);
            }
            if (params.debug.dumpHir) {
                {
                    OutputFile os(FMT(params.outfile << StringView("_2_hir.rs")));
                    HIRDump(os, *hirCrate);
                }
            }
            if (params.lastStage == ProgramParams::STAGE_TYPECK) {
                return 0;
            }
            memoryDump(memoryDumpSequence, "Typecheck");

            {
                HIRGenerateMIR(wb, *hirCrate);
            }
            if (params.debug.dumpMir) {
                {
                    OutputFile os(FMT(params.outfile << StringView("_3_mir.rs")));
                    MIRDump(os, *hirCrate);
                }
            }
            memoryDump(memoryDumpSequence, "MIR Gen");

            {
                MIRCleanupCrate(wb, *hirCrate);
            }
            {
                MIROptimiseCrate(wb, *hirCrate, mirOptLevel, enableMirInlining);
            }
            if (params.debug.dumpMir) {
                {
                    OutputFile os(FMT(params.outfile << StringView("_3_mir.rs")));
                    MIRDump(os, *hirCrate);
                }
            }
            if (params.lastStage == ProgramParams::STAGE_MIR) {
                return 0;
            }
            memoryDump(memoryDumpSequence, "MIR Opt");

            // TODO: Pass to mark items that are..

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

            if (params.codegen.emitLinkManifest != "") {
                OutputFile manifest(params.codegen.emitLinkManifest.c_str());
                for (const auto& path : params.nativeLibSearchDirs) {
                    manifest << StringView("search\t") << path << StringView("\n");
                }
                for (const auto& path : hirCrate->linkPaths) {
                    manifest << StringView("search\t") << path << StringView("\n");
                }
                for (const auto& lib : hirCrate->extLibs) {
                    manifest << StringView("lib\t") << lib.name << StringView("\n");
                }
                for (const auto& arg : params.codegen.linkerArgs) {
                    manifest << StringView("arg\t") << arg << StringView("\n");
                }
                for (const auto& crateName : hirCrate->extCratesOrdered) {
                    const auto& ext = hirCrate->extCrates.at(crateName);
                    if (ext.objectPath == "" || ext.isProcMacro) {
                        continue;
                    }
                    if (ext.data->langItems.count("trustme-panic_runtime") && strncmp(crateName.c_str(), transOpt.panicCrate.c_str(), transOpt.panicCrate.size()) != 0) {
                        continue;
                    }
                    manifest << StringView("object\t") << ext.objectPath << StringView("\n");
                }
                manifest.close();
            }

            if (params.testHarness) {
                crateType = ASTCrate::Type::Executable;
            }

            // TODO: For 1.29 executables/dylibs, add oom/panic shims
            if (crateType == ASTCrate::Type::ProcMacro) {
                {
                    HIRCrate crateForSer(pool, *wb.types);
                    crateForSer.crateName = hirCrate->crateName;
                    crateForSer.edition = hirCrate->edition;
                    for (const auto& i : hirCrate->rootModule.macroItems) {
                        DEBUG(i.first << StringView(": ") << i.second->ent.tagStr());
                        if (const auto* e = i.second->ent.opt_ProcMacro()) {
                            crateForSer.rootModule.macroItems.insert(std::make_pair(i.first, crateForSer.pool->make<HIRVisEnt<HIRMacroItem>>(HIRVisEnt<HIRMacroItem>{i.second->publicity, *e})));
                        }
                    }
                    crateForSer.exportedMacroNames = hirCrate->exportedMacroNames;
                    HIRSerialise(params.outfile + ".rlib", crateForSer);
                }
            }

            if (params.emitMetadataOnly) {
                if (crateType == ASTCrate::Type::RustLib) {
                    HIRSerialise(params.outfile, *hirCrate);
                } else {
                    {
                        OutputFile marker(params.outfile);
                    }
                }
                return 0;
            }

            TransList items = [&]() {
                switch (crateType) {
                    case ASTCrate::Type::Unknown:
                        sysE << StringView("BUG? Unknown crate type") << endL;
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
                BUG(Span(), StringView("Invalid crate_type value"));
            }();
            {
                // TODO: Drop glue generation?
                TransAutoImpls(wb, *hirCrate, items);
            }
            {
                TransMonomorphiseList(wb, *hirCrate, items, mirOptLevel);
            }
            {
                MIROptimiseCrateInlining(wb, *hirCrate, items, false, mirOptLevel, enableMirInlining);
            }
            {
                MIRCleanupCrate(wb, *hirCrate);
            }
            memoryDump(memoryDumpSequence, "Trans");

            std::string hirFile;
            switch (crateType) {
                case ASTCrate::Type::RustLib:
                    hirFile = params.outfile;
                    {
                        HIRSerialise(hirFile, *hirCrate);
                    }
                    break;
                case ASTCrate::Type::RustDylib:
                    hirFile = params.outfile + ".rlib";
                    {
                        HIRSerialise(hirFile, *hirCrate);
                    }
                    break;
                default:
                    break;
            }

            {
                MIROptimiseCrateInlining(wb, *hirCrate, items, true, mirOptLevel, enableMirInlining);
            }
            {
                TransEnumerateCleanup(wb, *hirCrate, items);
            }
            switch (crateType) {
                case ASTCrate::Type::Unknown:
                    UNREACHABLE();
                case ASTCrate::Type::RustLib: {
                    TransCodegen(wb, params.outfile, CodegenOutput::StaticLibrary, transOpt, hirCrate, std::move(items), hirFile);
                } break;
                case ASTCrate::Type::RustDylib:
                case ASTCrate::Type::CDylib: {
                    TransCodegen(wb, params.outfile, CodegenOutput::DynamicLibrary, transOpt, hirCrate, std::move(items), hirFile);
                } break;
                case ASTCrate::Type::ProcMacro: {
                    {
                        TransCodegen(wb, params.outfile, CodegenOutput::Executable, transOpt, hirCrate, std::move(items), hirFile);
                    }
                    break;
                }
                case ASTCrate::Type::Executable: {
                    TransCodegen(wb, params.outfile, CodegenOutput::Executable, transOpt, hirCrate, std::move(items), "");
                } break;
            }
        }

        return 0;
    }

    void* compileOnThread(void* raw) {
        auto& args = *static_cast<CompileArgs*>(raw);
        try {
            args.result = compile(args.argc, args.argv);
        } catch (const std::exception& e) {
            sysE << StringView("error: ") << e.what() << endL;
            ::exit(1);
        }
        return nullptr;
    }
}

void ExpandTestHarness(ASTCrate& crate) {
    ASSERT_BUG(Span(), crate.extCratenameTest != "", StringView("Crate `test` not loaded"));
    ASSERT_BUG(Span(), crate.extCratenameStd != "", StringView("Crate `std` not loaded"));
    auto cTest = crate.extCratenameTest;

    auto mainFn = ASTFunction{Span(), mkType(*crate.pool, ASTTypeTags::Unit(), Span()), {}};
    {
        auto callNode = NEWNODE(CallPath, ASTPath(cTest, {ASTPathNode("test_main_static")}), ::makeVec1(NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(NamedValue, ASTPath("", {ASTPathNode("test#"), ASTPathNode("TESTS")})))));
        mainFn.setCode(mv$(callNode));
    }

    std::vector<ASTExprNode*> testNodes;

    for (const auto& test : crate.tests) {
        ASTExprNodeStructLiteral::tValues descVals;
        descVals.push_back({{}, "name", NEWNODE(CallPath, ASTPath(cTest, {ASTPathNode("StaticTestName")}), ::makeVec1(NEWNODE(String, test.name)))});
        descVals.push_back({{}, "ignore", NEWNODE(Bool, test.ignore)});
        {
            ASTExprNode* shouldPanicVal = nullptr;
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
            testFcnNode = NEWNODE(Closure, ASTExprNodeClosure::argsT(), mkType(*crate.pool, Span()), NEWNODE(CallPath, ASTPath(cTest, {ASTPathNode("assert_test_result")}), ::makeVec1(NEWNODE(CallPath, ASTPath(test.path), decltype(ASTExprNodeCallPath::args)()))), false, false, false);
        }
        auto testTypeVarName = test.isBenchmark ? "StaticBenchFn" : "StaticTestFn";
        descandfnVals.push_back({{}, RcString::newInterned("testfn"), NEWNODE(CallPath, ASTPath(cTest, {ASTPathNode(testTypeVarName)}), ::makeVec1(std::move(testFcnNode)))});

        testNodes.push_back(NEWNODE(StructLiteral, ASTPath(cTest, {ASTPathNode("TestDescAndFn")}), nullptr, mv$(descandfnVals)));
        {
            testNodes.back() = NEWNODE(UniOp, ASTExprNodeUniOp::REF, mv$(testNodes.back()));
        }
    }
    auto testsArray = makeAstExprNode<ASTExprNodeArray>(*crate.pool, mv$(testNodes));

    size_t testCount = static_cast<ASTExprNodeArray&>(*testsArray).values.size();
    auto listItemTy = mkType(*crate.pool, Span(), ASTPath(cTest, {ASTPathNode("TestDescAndFn")}));
    {
        listItemTy = mkType(*crate.pool, ASTTypeTags::Reference(), Span(), ASTLifetimeRef::newStatic(), false, mv$(listItemTy));
    }
    auto testsList = ASTStatic{ASTStatic::Class::STATIC, mkType(*crate.pool, ASTTypeTags::SizedArray(), Span(), mv$(listItemTy), makeAstExprNode<ASTExprNodeInteger>(*crate.pool, U128(testCount), CORETYPE_UINT)), testsArray};

    auto newmod = ASTModule{ASTAbsolutePath("", pathNodes("test#"))};
    auto visPrivate = ASTVisibility::makeRestricted(ASTVisibility::Ty::Private, newmod.path());
    // - TODO: These need to be loaded too.

    newmod.addItem(Span(), visPrivate, "main", mv$(mainFn), {});
    newmod.addItem(Span(), visPrivate, "TESTS", mv$(testsList), {});

    crate.rootModule_.addItem(Span(), visPrivate, "test#", mv$(newmod), {});
    crate.langItems["trustme-main"] = ASTAbsolutePath("", pathNodes("test#", "main"));
}

#undef NEWNODE

#ifndef __has_feature
    #define __has_feature(x) 0
#endif

#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)
    #define TRUSTME_SANITIZER_BUILD 1
#else
    #define TRUSTME_SANITIZER_BUILD 0
#endif

int main(int argc, char* argv[]) {
    size_t stackSize = 1024u * 1024 * 1024;
    if (const char* text = std::getenv("TRUSTME_MIN_STACK")) {
        char* end = nullptr;
        const auto value = std::strtoull(text, &end, 10);
        if (*end == '\0' && value > 0) {
            stackSize = static_cast<size_t>(value);
        }
    }

    pthread_attr_t attr;
    CompileArgs args{argc, argv, 1};
    pthread_t thread;
    if (pthread_attr_init(&attr) != 0 || pthread_attr_setstacksize(&attr, stackSize) != 0 || pthread_create(&thread, &attr, compileOnThread, &args) != 0) {
        return compile(argc, argv);
    }
    pthread_join(thread, nullptr);
    pthread_attr_destroy(&attr);
    return args.result;
}

ProgramParams::ProgramParams(Settings& settings, int argc, char* argv[]) {
    auto addLibrarySearchDir = [this](const char* value) {
        std::string spec(value);
        auto equals = spec.find('=');
        std::string kind;
        std::string path;
        if (equals == std::string::npos) {
            path = std::move(spec);
        } else {
            kind = spec.substr(0, equals);
            path = spec.substr(equals + 1);
        }
        if (path.empty()) {
            sysE << StringView("Option -L requires a non-empty path") << endL;
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
            sysE << StringView("Unknown -L search path kind '") << kind << StringView("'") << endL;
            exit(1);
        }
    };

    if (const auto* a = getenv("TRUSTME_LIBDIR")) {
        addLibrarySearchDir(a);
    }

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (arg[0] != '-' || arg[1] == '\0') {
            if (this->infile == "") {
                this->infile = arg;
            } else {
                sysE << StringView("Unexpected free argument") << endL;
                exit(1);
            }
        } else if (arg[1] != '-') {
            arg++;

            switch (*arg) {
                case 'L':
                    if (arg[1] == '\0') {
                        if (i == argc - 1) {
                            sysE << StringView("Option ") << arg << StringView(" requires an argument") << endL;
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
                            sysE << StringView("Option ") << arg << StringView(" requires an argument") << endL;
                            exit(1);
                        }
                        this->libraries.pushBack(argv[++i]);
                    } else {
                        this->libraries.pushBack(arg + 1);
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
                            sysE << StringView("Option -") << flag << StringView(" requires an argument") << endL;
                            exit(1);
                        }
                        lintName = argv[++i];
                    } else {
                        lintName = arg + 1;
                    }
                    if (lintName[0] == '\0') {
                        sysE << StringView("Option -") << flag << StringView(" requires an argument") << endL;
                        exit(1);
                    }
                    const auto level = flag == 'A' ? CfgLintLevel::Allow : flag == 'W' ? CfgLintLevel::Warn : flag == 'D' ? CfgLintLevel::Deny : CfgLintLevel::Forbid;
                    CfgSetLintLevel(settings, lintName, level);
                    continue;
                }
                case 'C': {
                    std::string optname;
                    std::string optval;
                    if (arg[1] == '\0') {
                        if (i == argc - 1) {
                            sysE << StringView("Option ") << arg << StringView(" requires an argument") << endL;
                            exit(1);
                        }
                        optname = argv[++i];
                    } else {
                        optname = arg + 1;
                    }
                    auto eqPos = optname.find('=');
                    if (eqPos != std::string::npos) {
                        optval = optname.substr(eqPos + 1);
                        optname.resize(eqPos);
                    }
                    auto getOptval = [&]() {
                        if (eqPos == std::string::npos) {
                            sysE << StringView("Flag -C ") << optname << StringView(" requires an argument") << endL;
                            exit(1);
                        }
                    };

                    if (optname == "emit-cpp-only") {
                        this->codegen.emitCppOnly = true;
                    } else if (optname == "emit-build-command") {
                        getOptval();
                        this->codegen.emitBuildCommand = optval;
                    } else if (optname == "emit-link-manifest") {
                        getOptval();
                        this->codegen.emitLinkManifest = RcString::newInterned(optval);
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
                            sysE << StringView("invalid value for -C ") << optname << StringView(": '") << optval << StringView("'") << endL;
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
                            sysE << StringView("optimization level needs to be between 0-3, s or z (instead was '") << optval << StringView("')") << endL;
                            exit(1);
                        }
                    } else if (optname == "debug-assertions" || optname == "debug_assertions") {
                        if (eqPos == std::string::npos || optval == "y" || optval == "yes" || optval == "on" || optval == "true") {
                            this->debugAssertions = true;
                        } else if (optval == "n" || optval == "no" || optval == "off" || optval == "false") {
                            this->debugAssertions = false;
                        } else {
                            sysE << StringView("invalid value for -C debug-assertions: '") << optval << StringView("'") << endL;
                            exit(1);
                        }
                        this->debugAssertionsExplicit = true;
                    } else if (optname == "target-feature") {
                        getOptval();
                        size_t start = 0;
                        while (start <= optval.size()) {
                            const auto end = optval.find(',', start);
                            const auto feature = optval.substr(start, end == std::string::npos ? std::string::npos : end - start);
                            if (feature != "-crt-static") {
                                sysE << StringView("unsupported value for -C target-feature: '") << feature << StringView("' (trustme only supports -crt-static)") << endL;
                                exit(1);
                            }
                            if (end == std::string::npos) {
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
                            sysE << StringView("invalid value for -C debuginfo: '") << optval << StringView("'") << endL;
                            exit(1);
                        }
                    } else {
                        sysE << StringView("Unknown codegen option: '") << optname << StringView("'") << endL;
                        exit(1);
                    }
                }
                    continue;
                case 'Z': {
                    std::string optname;
                    std::string optval;
                    if (arg[1] == '\0') {
                        if (i == argc - 1) {
                            sysE << StringView("Option ") << arg << StringView(" requires an argument") << endL;
                            exit(1);
                        }
                        optname = argv[++i];
                    } else {
                        optname = arg + 1;
                    }
                    auto eqPos = optname.find('=');
                    if (eqPos != std::string::npos) {
                        optval = optname.substr(eqPos + 1);
                        optname.resize(eqPos);
                    }
                    auto getOptval = [&]() {
                        if (eqPos == std::string::npos) {
                            sysE << StringView("Flag -Z ") << optname << StringView(" requires an argument") << endL;
                            exit(1);
                        }
                    };
                    auto noOptval = [&]() {
                        if (eqPos != std::string::npos) {
                            sysE << StringView("Flag -Z ") << optname << StringView(" doesn't take an argument") << endL;
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
                            sysE << StringView("Invalid number for -Z mir-opt-level: '") << optval << StringView("'") << endL;
                            exit(1);
                        }
                        unsigned value = 0;
                        for (const char c : optval) {
                            if (c < '0' || c > '9') {
                                sysE << StringView("Invalid number for -Z mir-opt-level: '") << optval << StringView("'") << endL;
                                exit(1);
                            }
                            const unsigned digit = c - '0';
                            if (value > (UINT_MAX - digit) / 10) {
                                sysE << StringView("Number for -Z mir-opt-level is too large: '") << optval << StringView("'") << endL;
                                exit(1);
                            }
                            value = value * 10 + digit;
                        }
                        this->mirOptLevel = value;
                        this->mirOptLevelExplicit = true;
                    } else if (optname == "ub-checks" || optname == "ub_checks") {
                        if (eqPos == std::string::npos || optval == "y" || optval == "yes" || optval == "on" || optval == "true") {
                            this->ubChecks = true;
                        } else if (optval == "n" || optval == "no" || optval == "off" || optval == "false") {
                            this->ubChecks = false;
                        } else {
                            sysE << StringView("invalid value for -Z ub-checks: '") << optval << StringView("'") << endL;
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
                            sysE << StringView("invalid value for -Z fmt-debug: '") << optval << StringView("' (expected 'full', 'shallow', or 'none')") << endL;
                            exit(1);
                        }
                    } else if (optname == "link-directives") {
                        getOptval();
                        if (optval == "yes") {
                            settings.linkDirectives = true;
                        } else if (optval == "no") {
                            settings.linkDirectives = false;
                        } else {
                            sysE << StringView("invalid value for -Z link-directives: '") << optval << StringView("' (expected 'yes' or 'no')") << endL;
                            exit(1);
                        }
                    } else if (optname == "next-solver") {
                        if (!(eqPos == std::string::npos || optval == "globally" || optval == "coherence")) {
                            sysE << StringView("Invalid value for -Z next-solver: '") << optval << StringView("' (the legacy trait solver has been removed)") << endL;
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
                            sysE << StringView("Unknown argument to -Z stop-after - '") << optval << StringView("'") << endL;
                            exit(1);
                        }
                    } else if (optname == "pause-after-start") {
                        this->debug.pause = true;
                    } else if (optname == "unpretty") {
                        getOptval();
                        auto form = optval.substr(0, optval.find(','));
                        if (form == "normal" || form == "identified") {
                            this->lastStage = STAGE_PARSE;
                        } else if (form == "expanded" || form == "ast-tree") {
                            this->lastStage = STAGE_EXPAND;
                        } else if ((form == "hir" || form == "hir-tree") && optval.find("typed") == std::string::npos) {
                            this->lastStage = STAGE_HIR;
                        } else {
                        }
                    } else if (optname == "print-cfgs") {
                        noOptval();
                        this->printCfgs = true;
                    } else if (optname == "check-cfg-all-expected") {
                        noOptval();
                    } else {
                    }
                }
                    continue;

                default:
                    break;
            }

            for (; *arg; arg++) {
                switch (*arg) {
                    case 'o':
                        if (i == argc - 1) {
                            sysE << StringView("Option -") << *arg << StringView(" requires an argument") << endL;
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
                        sysE << StringView("Unknown option: '-") << *arg << StringView("'") << endL;
                        exit(1);
                }
            }
        } else {
            auto checkWithArg = [&](const char* name) -> const char* {
                if (strcmp(arg + 2, name) == 0) {
                    if (i == argc - 1) {
                        sysE << StringView("Flag ") << arg << StringView(" requires an argument") << endL;
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
            } else if (const char* outDir = checkWithArg("out-dir")) {
                this->outputDir = outDir;
                if (this->outputDir != "" && this->outputDir.back() != '/') {
                    this->outputDir += '/';
                }
            } else if (const char* metadata = checkWithArg("crate-name-of")) {
                this->crateNameQuery = metadata;
            } else if (strcmp(arg, "--crate") == 0) {
                if (i == argc - 1) {
                    sysE << StringView("Option ") << arg << StringView(" requires an argument") << endL;
                    exit(1);
                }
                const char* desc = argv[++i];
                const char* pos = std::strchr(desc, '=');
                if (!pos || pos == desc || !pos[1]) {
                    sysE << StringView("Option --crate requires <unique-name>=<metadata-path>") << endL;
                    exit(1);
                }
                auto name = RcString::newInterned(desc, pos - desc);
                settings.crateOverride(name).metadataPath = pos + 1;
            } else if (strcmp(arg, "--crate-object") == 0) {
                if (i == argc - 1) {
                    sysE << StringView("Option ") << arg << StringView(" requires an argument") << endL;
                    exit(1);
                }
                const char* desc = argv[++i];
                const char* pos = std::strchr(desc, '=');
                if (!pos || pos == desc || !pos[1]) {
                    sysE << StringView("Option --crate-object requires <unique-name>=<object-path>") << endL;
                    exit(1);
                }
                auto name = RcString::newInterned(desc, pos - desc);
                settings.crateOverride(name).objectPath = pos + 1;
            } else if (strcmp(arg, "--proc-macro") == 0) {
                if (i == argc - 1) {
                    sysE << StringView("Option ") << arg << StringView(" requires an argument") << endL;
                    exit(1);
                }
                const char* desc = argv[++i];
                const char* pos = std::strchr(desc, '=');
                if (!pos || pos == desc || !pos[1]) {
                    sysE << StringView("Option --proc-macro requires <unique-name>=<executable-path>") << endL;
                    exit(1);
                }
                auto name = RcString::newInterned(desc, pos - desc);
                settings.crateOverride(name).procMacroPath = pos + 1;
            } else if (strcmp(arg, "--crate-alias") == 0) {
                if (i == argc - 1) {
                    sysE << StringView("Option ") << arg << StringView(" requires an argument") << endL;
                    exit(1);
                }
                const char* desc = argv[++i];
                const char* pos = std::strchr(desc, '=');
                if (!pos || pos == desc || !pos[1]) {
                    sysE << StringView("Option --crate-alias requires <source-name>=<unique-name>") << endL;
                    exit(1);
                }
                auto name = RcString::newInterned(desc, pos - desc);
                settings.crateOverride(name).target = pos + 1;
            } else if (strcmp(arg, "--extern") == 0) {
                if (i == argc - 1) {
                    sysE << StringView("Option ") << arg << StringView(" requires an argument") << endL;
                    exit(1);
                }
                const char* desc = argv[++i];
                const char* pos = std::strchr(desc, '=');
                auto name = RcString::newInterned(desc, pos ? pos - desc : strlen(desc));
                auto& spec = settings.crateOverride(name);
                spec.isExtern = true;
                spec.target = pos ? RcString::newInterned(pos + 1) : RcString{};
            } else if (const auto* nameStr = checkWithArg("crate-tag")) {
                this->crateNameSuffix = nameStr;
            } else if (const auto* nameStr = checkWithArg("crate-name")) {
                this->crateName = nameStr;
            } else if (const char* typeStr = checkWithArg("crate-type")) {
                if (strcmp(typeStr, "lib") == 0 || strcmp(typeStr, "rlib") == 0) {
                    this->crateType = ASTCrate::Type::RustLib;
                } else if (strcmp(typeStr, "dylib") == 0) {
                    this->crateType = ASTCrate::Type::RustDylib;
                } else if (strcmp(typeStr, "cdylib") == 0) {
                    this->crateType = ASTCrate::Type::CDylib;
                } else if (strcmp(typeStr, "bin") == 0) {
                    this->crateType = ASTCrate::Type::Executable;
                } else if (strcmp(typeStr, "proc-macro") == 0) {
                    this->crateType = ASTCrate::Type::ProcMacro;
                } else {
                    sysE << StringView("Unknown value for --crate-type: ") << typeStr << endL;
                    exit(1);
                }
            } else if (const char* cfgSpec = checkWithArg("cfg")) {
                std::string name;
                std::string value;
                bool has_value = false;
                CfgParseOption(cfgSpec, name, has_value, value);
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
                std::string error;
                if (!CfgSetCheckSpec(settings, checkCfgSpec, error)) {
                    sysE << StringView("invalid `--check-cfg` argument: `") << checkCfgSpec << StringView("`: ") << error << endL;
                    exit(1);
                }
            } else if (const char* envSpec = checkWithArg("env-set")) {
                const char* separator = std::strchr(envSpec, '=');
                if (separator == nullptr || separator == envSpec) {
                    sysE << StringView("--env-set takes an argument of the form NAME=VALUE") << endL;
                    exit(1);
                }
                const std::string name(envSpec, separator);
                if (::setenv(name.c_str(), separator + 1, 1) != 0) {
                    sysE << StringView("failed to set compile-time environment variable '") << name << StringView("'") << endL;
                    exit(1);
                }
            } else if (const char* forceWarn = checkWithArg("force-warn")) {
                if (forceWarn[0] == '\0') {
                    sysE << StringView("Flag --force-warn requires an argument") << endL;
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
                    sysE << StringView("unknown lint level: `") << lintCap << StringView("`") << endL;
                    exit(1);
                }
                CfgSetLintCap(settings, level);
            } else if (const char* emit = checkWithArg("emit")) {
                if (std::strcmp(emit, "metadata") == 0) {
                    this->emitMetadataOnly = true;
                } else {
                    sysE << StringView("Ignoring `--emit ") << emit << StringView("` for compatability with rustc") << endL;
                }
            } else if (const char* targetName = checkWithArg("target")) {
                this->target = targetName;
            } else if (strcmp(arg, "--dump-target-spec") == 0) {
                if (i == argc - 1) {
                    sysE << StringView("Flag ") << arg << StringView(" requires an argument") << endL;
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
                    sysE << StringView("Unknown value for ") << arg << StringView(" - '") << editionStr << StringView("'") << endL;
                    exit(1);
                }
            } else {
                sysE << StringView("Unknown option '") << arg << StringView("'") << endL;
                exit(1);
            }
        }
    }

    if (const auto* a = getenv("TRUSTME_DUMP")) {
        while (a[0]) {
            const char* end = strchr(a, ':');

            std::string_view s;
            if (end) {
                s = std::string_view{a, end};
                a = end + 1;
            } else {
                end = a + strlen(a);
                s = std::string_view{a, end};
                a = end;
            }

            if (s == "") {
            } else if (s == "ast") {
                this->debug.dumpAst = true;
            } else if (s == "hir") {
                this->debug.dumpHir = true;
            } else if (s == "mir") {
                this->debug.dumpMir = true;
            } else {
                sysE << StringView("Unknown option in $TRUSTME_DUMP '") << s << StringView("'") << endL;
            }
        }
    }
}

void ProgramParams::showHelp() const {
    sysO << StringView(
        "USAGE: rustc <sourcefile>\n"
        "\n"
        "OPTIONS:\n"
        "-L [kind=]<dir>    : Search for crates or native libraries in this directory\n"
        "-o <filename>      : Write compiler output (library or executable) to this file\n"
        "-O                 : Enable optimisation\n"
        "-g                 : Emit debugging information\n"
        "--out-dir <dir>    : Specify the output directory (alternative to `-o`)\n"
        "--crate <unique>=<rlib>\n"
        "                   : Make an exact crate metadata artifact available\n"
        "--crate-name-of <rlib>\n"
        "                   : Print the exact crate name stored in metadata\n"
        "--crate-alias <source>=<unique>\n"
        "                   : Resolve a source name through the crate table\n"
        "--crate-object <unique>=<object>\n"
        "                   : Supply the exact object used by standalone linking\n"
        "--proc-macro <unique>=<executable>\n"
        "                   : Supply the exact proc-macro host executable\n"
        "--extern <alias>=<unique>\n"
        "                   : Bind a source crate name to an available unique crate\n"
        "--crate-tag <str>  : Specify a suffix for symbols and output files\n"
        "--crate-name <str> : Override/set the crate name\n"
        "--crate-type <ty>  : Override/set the crate type (rlib, dylib, cdylib, bin, proc-macro)\n"
        "--cfg flag         : Set a boolean #[cfg]/cfg! flag\n"
        "--cfg flag=\"val\"   : Set a string #[cfg]/cfg! flag\n"
        "--target <name>    : Compile code for the given target\n"
        "--test             : Generate a unit test executable\n"
        "-C <option>        : Code-generation options\n"
        "-Z <option>        : Debugging/experimental options\n"
    );
}

auto ProgramParams::effectiveMirOptLevel() const -> unsigned {
    return mirOptLevelExplicit ? mirOptLevel : (optLevel == OptimizationLevel::None ? 1 : 2);
}

auto ProgramParams::enableMirInlining() const -> bool {
    const auto level = effectiveMirOptLevel();
    return level >= 3 || (level == 2 && optLevel != OptimizationLevel::None && optLevel != OptimizationLevel::Less);
}

auto ProgramParams::debugAssertionsEnabled() const -> bool {
    return debugAssertionsExplicit ? debugAssertions : optLevel == OptimizationLevel::None;
}

auto ProgramParams::ubChecksEnabled() const -> bool {
    return ubChecksExplicit ? ubChecks : debugAssertionsEnabled();
}

auto ProgramParams::overflowChecksEnabled() const -> bool {
    return overflowChecksExplicit ? overflowChecks : debugAssertionsEnabled();
}
