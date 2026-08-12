#include "main_bindings.h"

#include "ast_dump.h"
#include "ast_ast.h"
#include "ast_expr.h"
#include "ast_crate.h"
#include "main_bindings.h"
#include "hir_hir.h" // ABI_RUST
#include <iostream>
#include <iomanip>
#include <string>
#include <set>
#include <climits>
#include "version.h"
#include "parse_lex.h"
#include "parse_parseerror.h"
#include "parse_common.h" // For edition checks
#include <cstring>
#include "resolve_main_bindings.h"
#include "hir_main_bindings.h"
#include "hir_conv_main_bindings.h"
#include "hir_typeck_main_bindings.h"
#include "hir_expand_main_bindings.h"
#include "mir_main_bindings.h"
#include "trans_main_bindings.h"
#include "trans_target.h"
#include "trait_solver_mode.h"
#include "expand_cfg.h"
#include "target_detect.h" // tools/common/target_detect.h
#include "debug_inner.h"
#include "memory_dump.h"
#include <std/mem/obj_pool.h>

#define NEWNODE(ty, ...) ::AST::ExprNodeP(new ::AST::ExprNode##ty(__VA_ARGS__))

void ExpandTestHarness(::AST::Crate& crate) {
    ASSERT_BUG(Span(), crate.m_ext_cratename_test != "", "Crate `test` not loaded");
    ASSERT_BUG(Span(), crate.m_ext_cratename_std != "", "Crate `std` not loaded");
    auto c_test = crate.m_ext_cratename_test;
    // Create the following module:
    // ```
    // mod `#test` {
    //   extern crate std;
    //   extern crate test;
    //   fn main() {
    //     self::test::test_main_static(&::`#test`::TESTS);
    //   }
    //   static TESTS: [test::TestDescAndFn; _] = [
    //     test::TestDescAndFn { desc: test::TestDesc { name: "foo", ignore: false, should_panic: test::ShouldPanic::No }, testfn: ::path::to::foo },
    //     ];
    // }
    // ```

    // ---- main function ----
    auto main_fn = ::AST::Function{Span(), TypeRef(TypeRef::TagUnit(), Span()), {}};
    {
        auto call_node = NEWNODE(CallPath, ::AST::Path(c_test, {::AST::PathNode("test_main_static")}), ::make_vec1(NEWNODE(UniOp, ::AST::ExprNodeUniOp::REF, NEWNODE(NamedValue, ::AST::Path("", {::AST::PathNode("test#"), ::AST::PathNode("TESTS")})))));
        main_fn.set_code(mv$(call_node));
    }

    // ---- test list ----
    ::std::vector<::AST::ExprNodeP> test_nodes;

    for (const auto& test : crate.m_tests) {
        ::AST::ExprNodeStructLiteral::t_values desc_vals;
        // `name: "foo",`
        desc_vals.push_back({{}, "name", NEWNODE(CallPath, ::AST::Path(c_test, {::AST::PathNode("StaticTestName")}), ::make_vec1(NEWNODE(String, test.name)))});
        // `ignore: false,`
        desc_vals.push_back({{}, "ignore", NEWNODE(Bool, test.ignore)});
        // `should_panic: ShouldPanic::No,`
        {
            ::AST::ExprNodeP should_panic_val;
            switch (test.panic_type) {
                case ::AST::TestDesc::ShouldPanic::No:
                    should_panic_val = NEWNODE(NamedValue, ::AST::Path(c_test, {::AST::PathNode("ShouldPanic"), ::AST::PathNode("No")}));
                    break;
                case ::AST::TestDesc::ShouldPanic::Yes:
                    should_panic_val = NEWNODE(NamedValue, ::AST::Path(c_test, {::AST::PathNode("ShouldPanic"), ::AST::PathNode("Yes")}));
                    break;
                case ::AST::TestDesc::ShouldPanic::YesWithMessage:
                    should_panic_val = NEWNODE(CallPath, ::AST::Path(c_test, {::AST::PathNode("ShouldPanic"), ::AST::PathNode("YesWithMessage")}), make_vec1(NEWNODE(String, test.expected_panic_message)));
                    break;
            }
            desc_vals.push_back({{}, "should_panic", mv$(should_panic_val)});
        }
        {
            // TODO: Get this from attributes
            desc_vals.push_back({{}, "compile_fail", NEWNODE(Bool, false)});
            desc_vals.push_back({{}, "no_run", NEWNODE(Bool, false)});
            desc_vals.push_back({{}, "test_type", NEWNODE(NamedValue, ::AST::Path(c_test, {AST::PathNode("TestType"), AST::PathNode("UnitTest")}))});
        }
        {
            desc_vals.push_back({{}, "ignore_message", NEWNODE(NamedValue, ::AST::Path(crate.m_ext_cratename_std, {AST::PathNode("option"), AST::PathNode("Option"), AST::PathNode("None")}))});
            auto sp = test.span.get_top_file_span();
            desc_vals.push_back({{}, "source_file", NEWNODE(String, sp.filename.c_str())});
            desc_vals.push_back({{}, "start_line", NEWNODE(Integer, U128(sp.start_line), CORETYPE_UINT)});
            desc_vals.push_back({{}, "start_col", NEWNODE(Integer, U128(sp.start_ofs), CORETYPE_UINT)});
            desc_vals.push_back({{}, "end_line", NEWNODE(Integer, U128(sp.end_line), CORETYPE_UINT)});
            desc_vals.push_back({{}, "end_col", NEWNODE(Integer, U128(sp.end_ofs), CORETYPE_UINT)});
        }
        auto desc_expr = NEWNODE(StructLiteral, ::AST::Path(c_test, {::AST::PathNode("TestDesc")}), nullptr, mv$(desc_vals));

        ::AST::ExprNodeStructLiteral::t_values descandfn_vals;
        descandfn_vals.push_back({{}, RcString::new_interned("desc"), mv$(desc_expr)});

        auto test_fcn_node = NEWNODE(NamedValue, AST::Path(test.path));
        {
            // Convert `fn()` into `fn()->Result<(),String>`
            // Use `|| ::test::assert_test_result( fcn() )`
            test_fcn_node = NEWNODE(Closure, {}, TypeRef(Span()), NEWNODE(CallPath, ::AST::Path(c_test, {::AST::PathNode("assert_test_result")}), ::make_vec1(NEWNODE(CallPath, AST::Path(test.path), {}))), false, false);
        }
        auto test_type_var_name = test.is_benchmark ? "StaticBenchFn" : "StaticTestFn";
        descandfn_vals.push_back({{}, RcString::new_interned("testfn"), NEWNODE(CallPath, ::AST::Path(c_test, {::AST::PathNode(test_type_var_name)}), ::make_vec1(std::move(test_fcn_node)))});

        test_nodes.push_back(NEWNODE(StructLiteral, ::AST::Path(c_test, {::AST::PathNode("TestDescAndFn")}), nullptr, mv$(descandfn_vals)));
        // NOTE: 1.39+ needs &TestDescAndFn here
        {
            test_nodes.back() = NEWNODE(UniOp, ::AST::ExprNodeUniOp::REF, mv$(test_nodes.back()));
        }
    }
    auto* tests_array = new ::AST::ExprNodeArray(mv$(test_nodes));

    size_t test_count = tests_array->m_values.size();
    auto list_item_ty = TypeRef(Span(), ::AST::Path(c_test, {::AST::PathNode("TestDescAndFn")}));
    // NOTE: 1.39+ needs &TestDescAndFn here
    {
        list_item_ty = TypeRef(TypeRef::TagReference(), Span(), AST::LifetimeRef::new_static(), false, mv$(list_item_ty));
    }
    auto tests_list = ::AST::Static{::AST::Static::Class::STATIC, TypeRef(TypeRef::TagSizedArray(), Span(), mv$(list_item_ty), ::std::shared_ptr<::AST::ExprNode>(new ::AST::ExprNodeInteger(U128(test_count), CORETYPE_UINT))), ::AST::Expr(mv$(tests_array))};

    // ---- module ----
    auto newmod = ::AST::Module{::AST::AbsolutePath("", {"test#"})};
    auto vis_private = AST::Visibility::make_restricted(AST::Visibility::Ty::Private, newmod.path());
    // - TODO: These need to be loaded too.
    //  > They don't actually need to exist here, just be loaded (and use absolute paths)
    //newmod.add_ext_crate(Span(), false, "std", "std", {});
    //newmod.add_ext_crate(Span(), false, "test", "test", {});

    newmod.add_item(Span(), vis_private, "main", mv$(main_fn), {});
    newmod.add_item(Span(), vis_private, "TESTS", mv$(tests_list), {});

    crate.m_root_module.add_item(Span(), vis_private, "test#", mv$(newmod), {});
    crate.m_lang_items["mrustc-main"] = ::AST::AbsolutePath("", {"test#", "main"});
}

#undef NEWNODE



#ifndef __has_feature
    #define __has_feature(x) 0
#endif

#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)
    #define MRUSTC_SANITIZER_BUILD 1
#else
    #define MRUSTC_SANITIZER_BUILD 0
#endif


struct ProgramParams {
    enum eLastStage {
        STAGE_PARSE,
        STAGE_EXPAND,
        STAGE_RESOLVE,
        STAGE_TYPECK,
        STAGE_BORROWCK,
        STAGE_MIR,
        STAGE_ALL,
    } last_stage = STAGE_ALL;

    ::std::string infile;
    ::std::string outfile;
    ::std::string output_dir = "";
    ::std::string target = DEFAULT_TARGET_NAME;

    ::std::string emit_depfile;

    AST::Edition edition = AST::Edition::Rust2015;
    ::AST::Crate::Type crate_type = ::AST::Crate::Type::Unknown;
    ::std::string crate_name;
    ::std::string crate_name_suffix;

    OptimizationLevel opt_level = OptimizationLevel::None;
    bool debug_assertions = false;
    bool debug_assertions_explicit = false;
    // rustc defaults MIR optimisation to 1 at -O0 and to 2 otherwise.
    // Keep the explicit bit separate so `-Zmir-opt-level=0` is distinguishable
    // from the implicit default.
    unsigned mir_opt_level = 0;
    bool mir_opt_level_explicit = false;
    DebugInfoLevel debug_info = DebugInfoLevel::None;

    bool test_harness = false;

    // NOTE: If populated, nothing happens except for loading the target
    ::std::string target_saveback;
    // NOTE: if true, no parse/compilation performed (target is loaded though)
    bool print_cfgs = false;

    //
    bool run_borrowcheck = false;

    TraitSolverConfig trait_solver;

    ::std::vector<const char*> lib_search_dirs;
    ::std::vector<const char*> libraries;
    ::std::map<::std::string, ::std::string> crate_overrides; // --extern name=path

    ::std::set<::std::string> features;

    struct {
        /// Debugger aid: pause just after startup so a debugger can attach.
        bool pause = false;

        bool full_validate = false;
        bool full_validate_early = false;

        bool dump_ast = false;
        bool dump_hir = false;
        bool dump_mir = false;
    } debug;

    struct {
        ::std::string codegen_type;
        ::std::string emit_build_command;
        ::std::string panic_type;
        ::std::vector<::std::string> linker_args;
    } codegen;

    ProgramParams(int argc, char* argv[]);

    unsigned effective_mir_opt_level() const {
        return mir_opt_level_explicit ? mir_opt_level : (opt_level == OptimizationLevel::None ? 1 : 2);
    }
    bool enable_mir_inlining() const {
        const auto level = effective_mir_opt_level();
        return level >= 3 || (level == 2 && opt_level != OptimizationLevel::None && opt_level != OptimizationLevel::Less);
    }
    bool debug_assertions_enabled() const {
        return debug_assertions_explicit ? debug_assertions : opt_level == OptimizationLevel::None;
    }

    void show_help() const;
};

template <typename Rv, typename Fcn>
Rv CompilePhase(const char* name, Fcn f) {
    DebugTimedPhase timed_phase(name);
    return f();
}

template <typename Fcn>
void CompilePhaseV(const char* name, Fcn f) {
    DebugTimedPhase timed_phase(name);
    f();
}

void init_debug_list() {
    debug_init_phases(
        "MRUSTC_DEBUG",
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

         "Lifetime Elision",
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

         "Expand HIR Annotate",
         "Expand HIR Static Borrow Mark",
         "Expand HIR Lifetimes",
         "Expand HIR Closures",
         "Expand HIR Static Borrow",
         "Expand HIR Calls",
         "Expand HIR VTables",
         "Expand HIR Reborrows",
         "Expand HIR ErasedType",
         "Typecheck Expressions (validate)",
         "Expand HIR Lifetimes (validate)",

         "Dump HIR",
         "Lower MIR",
         "MIR Validate Full Early",
         "Dump MIR",
         "Constant Evaluate Full",
         "MIR Cleanup",
         "MIR Borrowcheck",
         "MIR Optimise",
         "MIR Validate PO",
         "MIR Validate Full",

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
int main(int argc, char* argv[]) {
    init_debug_list();
    ProgramParams params(argc, argv);
    gTraitSolverConfig = params.trait_solver;
    const auto mir_opt_level = params.effective_mir_opt_level();
    const auto enable_mir_inlining = params.enable_mir_inlining();
    if (params.codegen.panic_type.empty()) {
        params.codegen.panic_type = "unwind";
    }

    if (params.debug.pause) {
        char c;
        ::std::cerr << "Pausing to attach a debugger\nType any text to continue" << std::endl;
        ::std::cin >> c;
    }

    // Set up cfg values
    CompilePhaseV("Setup", [&]() {
        CfgSetValue("rust_compiler", "mrustc");
        CfgSetValue("panic", params.codegen.panic_type);
        if (params.debug_assertions_enabled()) {
            CfgSetFlag("debug_assertions");
        }
        CfgSetValueCb("feature", [&params](const ::std::string& s) {
            return params.features.count(s) != 0;
        });
    });
    CompilePhaseV("Target Load", [&]() {
        TargetSetCfg(params.target);
    });

    if (params.print_cfgs) {
        CfgDump(std::cout);
        return 0;
    }
    if (params.target_saveback != "") {
        TargetExportCurSpec(params.target_saveback);
        return 0;
    }

    if (params.infile == "") {
        ::std::cerr << "No input file passed" << ::std::endl;
        return 1;
    }

    if (params.test_harness) {
        CfgSetFlag("test");
    }

    ExpandInit();
#if MRUSTC_SANITIZER_BUILD
    // Keep teardown out of production, but make sanitizer builds destroy every
    // pooled object so ASan/LSan can distinguish real leaks from arena lifetime.
    auto pool_owner = stl::ObjPool::fromMemory();
    auto* pool = pool_owner.mutPtr();
#else
    auto* pool = stl::ObjPool::fromMemoryRaw();
#endif
    auto* types = pool->make<HIR::TypeInterner>(*pool);

    try {
        // Parse the crate into AST
        AST::Crate* crate_ptr = CompilePhase<AST::Crate*>("Parse", [&]() {
            return ParseCrate(pool, *types, params.infile, params.edition);
        });
        AST::Crate& crate = *crate_ptr;
        crate.m_test_harness = params.test_harness;
        crate.m_crate_name_suffix = params.crate_name_suffix;
        //crate.m_crate_name = params.crate_name;

        if (params.last_stage == ProgramParams::STAGE_PARSE) {
            return 0;
        }
        memory_dump("Parsed");

        // Load external crates.
        CompilePhaseV("LoadCrates", [&]() {
            // Hacky!
            AST::g_crate_overrides = params.crate_overrides;
            for (const auto& ld : params.lib_search_dirs) {
                AST::g_crate_load_dirs.push_back(ld);
            }
            crate.load_externs();
            if (params.test_harness) {
                auto test_crate_name = RcString::new_interned("test");
                AST::g_implicit_crates.insert(std::make_pair(test_crate_name, crate.load_extern_crate(Span(), test_crate_name)));
            }
        });

        if (params.crate_name != "") {
            // Extract the crate type and name from the crate attributes
            auto crate_type = params.crate_type;
            if (crate_type == ::AST::Crate::Type::Unknown) {
                crate_type = crate.m_crate_type;
            }
            if (crate_type == ::AST::Crate::Type::Unknown) {
                // Assume to be executable
                crate_type = ::AST::Crate::Type::Executable;
            }
            crate.m_crate_type = crate_type;

            crate.set_crate_name(params.crate_name);
            crate.m_crate_type = ::AST::Crate::Type::Unknown;
        }

        // Iterate all items in the AST, applying syntax extensions
        CompilePhaseV("Expand", [&]() {
            Expand(crate);

            if (params.test_harness) {
                ExpandTestHarness(crate);
            }
        });

        // Extract the crate type and name from the crate attributes
        auto crate_type = params.crate_type;
        if (crate_type == ::AST::Crate::Type::Unknown) {
            crate_type = crate.m_crate_type;
        }
        if (crate_type == ::AST::Crate::Type::Unknown) {
            // Assume to be executable
            crate_type = ::AST::Crate::Type::Executable;
        }
        crate.m_crate_type = crate_type;

        if (crate.m_crate_type == ::AST::Crate::Type::ProcMacro) {
            ExpandProcMacroHarness(crate);
        }

        auto crate_name = params.crate_name;
        if (crate_name == "") {
            crate_name = crate.m_crate_name_set;
        }
        if (crate_name == "") {
            auto s = params.infile.find_last_of('/');
            if (s == ::std::string::npos) {
                s = 0;
            } else {
                s += 1;
            }
            auto s2 = params.infile.find_last_of('\\');
            if (s2 == ::std::string::npos) {
                s2 = 0;
            } else {
                s2 += 1;
            }
            s = std::max(s, s2);
            auto e = params.infile.find_first_of('.', s);
            if (e == ::std::string::npos) {
                e = params.infile.size() - s;
            }

            crate_name = ::std::string(params.infile.begin() + s, params.infile.begin() + e);
            for (auto& b : crate_name) {
                if ('0' <= b && b <= '9') {
                } else if ('A' <= b && b <= 'Z') {
                } else if (b == '_') {
                } else if (b == '-') {
                    b = '_';
                } else {
                    // TODO: Error?
                }
            }
        }
        if (params.test_harness) {
            crate_name += "$test";
        }
        crate.set_crate_name(crate_name);

        if (params.outfile == "") {
            switch (crate.m_crate_type) {
                case ::AST::Crate::Type::RustLib:
                    params.outfile = FMT(params.output_dir << "lib" << crate.m_crate_name_set << ".rlib");
                    break;
                case ::AST::Crate::Type::Executable:
                    params.outfile = FMT(params.output_dir << crate.m_crate_name_set);
                    break;
                case ::AST::Crate::Type::ProcMacro:
                    params.outfile = FMT(params.output_dir << "lib" << crate.m_crate_name_set << "-plugin");
                    break;
                default:
                    params.outfile = FMT(params.output_dir << crate.m_crate_name_set << ".o");
                    break;
            }
            DEBUG("params.outfile = " << params.outfile);
        }

        if (params.debug.dump_ast) {
            CompilePhaseV("Dump Expanded", [&]() {
                DumpRust(FMT(params.outfile << "_1_ast.rs").c_str(), crate);
            });
        }

        if (params.last_stage == ProgramParams::STAGE_EXPAND) {
            return 0;
        }
        memory_dump("Expanded");

        // Allocator and panic strategies
        CompilePhaseV("Implicit Crates", [&]() {
            if (crate.m_crate_type == ::AST::Crate::Type::Executable || params.test_harness || crate.m_crate_type == ::AST::Crate::Type::ProcMacro) {
                bool allocator_crate_loaded = false;
                RcString alloc_crate_name;
                bool panic_runtime_loaded = false;
                RcString panic_crate_name;
                bool panic_runtime_needed = false;
                for (const auto& ec : crate.m_extern_crates) {
                    ::std::ostringstream ss;
                    for (const auto& e : ec.second.m_hir->m_lang_items) {
                        ss << e << ",";
                    }
                    DEBUG("Looking at lang items from " << ec.first << " : " << ss.str());
                    if (ec.second.m_hir->m_lang_items.count("mrustc-allocator")) {
                        if (allocator_crate_loaded) {
                            ERROR(Span(), E0000, "Multiple allocator crates loaded - " << alloc_crate_name << " and " << ec.first);
                        }
                        alloc_crate_name = ec.first;
                        allocator_crate_loaded = true;
                    }
                    if (ec.second.m_hir->m_lang_items.count("mrustc-panic_runtime")) {
                        if (panic_runtime_loaded) {
                            //ERROR(Span(), E0000, "Multiple panic_runtime crates loaded - " << panic_crate_name << " and " << ec.first);
                            WARNING(Span(), W0000, "Multiple panic_runtime crates loaded - " << panic_crate_name << " and " << ec.first);
                        } else {
                            panic_crate_name = ec.first;
                            panic_runtime_loaded = true;
                        }
                    }
                    if (ec.second.m_hir->m_lang_items.count("mrustc-needs_panic_runtime")) {
                        panic_runtime_needed = true;
                    }
                }
                // The default (system) allocator is provided by liballoc.
                allocator_crate_loaded = true;
                if (!allocator_crate_loaded) {
                    crate.load_extern_crate(Span(), "alloc_system");
                }

                if (panic_runtime_needed /*&& !panic_runtime_loaded*/) {
                    auto panic_crate = "panic_" + params.codegen.panic_type;
                    crate.load_extern_crate(Span(), panic_crate.c_str());
                }

                // - `mrustc-main` lang item default
                if (!crate.m_no_main) {
                    crate.m_lang_items.insert(::std::make_pair(::std::string("mrustc-main"), ::AST::AbsolutePath("", {"main"})));
                }
            }
        });

        /// Emit the dependency files
        if (params.emit_depfile != "") {
            // - Iterate all loaded files for modules
            struct PathEnumerator {
                ::std::vector<::std::string> out;

                void visit_module(::AST::Module& mod) {
                    if (mod.m_file_info.path != "!" && mod.m_file_info.path.back() != '/') {
                        out.push_back(mod.m_file_info.path);
                    }
                    // TODO: Should we check anon modules?
                    //for(auto& amod : mod.anon_mods()) {
                    //    this->visit_module(*amod);
                    //}
                    for (auto& i : mod.m_items) {
                        if (i->data.is_Module()) {
                            this->visit_module(i->data.as_Module());
                        }
                    }
                }
            };

            PathEnumerator pe;
            pe.visit_module(crate.m_root_module);

            ::std::ofstream of{params.emit_depfile};
            // TODO: Escape spaces and colons in these paths
            of << params.outfile << ": " << params.infile;
            for (const auto& mod_path : pe.out) {
                of << " " << mod_path;
            }
            of << ::std::endl;

            of << params.outfile << ":";
            // - Iterate all loaded crates files
            for (const auto& ec : crate.m_extern_crates) {
                of << " " << ec.second.m_filename;
            }
            // - Iterate all extra files (include! and friends)
        }

        // Resolve names to be absolute names (include references to the relevant struct/global/function)
        // - This does name checking on types and free functions.
        // - Resolves all identifiers/paths to references
        CompilePhaseV("Resolve Use", [&]() {
            ResolveUse(crate); // - Absolutise and resolve use statements
        });
        CompilePhaseV("Resolve Index", [&]() {
            ResolveIndex(crate); // - Build up a per-module index of avalable names (faster and simpler later resolve)
        });
        CompilePhaseV("Resolve Absolute", [&]() {
            ResolveAbsolutise(crate); // - Convert all paths to Absolute or UFCS, and resolve variables
        });
        memory_dump("Resolved");

        if (params.debug.dump_ast) {
            CompilePhaseV("Temp output - Resolved", [&]() {
                DumpRust(FMT(params.outfile << "_1_ast.rs").c_str(), crate);
            });
        }

        if (params.last_stage == ProgramParams::STAGE_RESOLVE) {
            return 0;
        }

        // --------------------------------------
        // HIR Section
        // --------------------------------------
        // Construct the HIR beside the AST in the compilation object pool.
        ::HIR::Crate* hir_crate = CompilePhase<::HIR::Crate*>("HIR Lower", [&]() {
            return LowerHIRFromAST(pool, crate);
        });
        memory_dump("HIR Gen");
        if (params.debug.dump_hir) {
            CompilePhaseV("Dump HIR", [&]() {
                ::std::ofstream os(FMT(params.outfile << "_2_hir.rs"));
                HIRDump(os, *hir_crate);
            });
        }
        memory_dump("HIR");

        CompilePhaseV("Lifetime Elision", [&]() {
            ConvertHIRLifetimeElision(*hir_crate);
        });

        // Replace type aliases (`type`) into the actual type
        // - Does simple replacements
        // - Done before bind so type alises can be used in patterns?
        CompilePhaseV("Resolve Type Aliases", [&]() {
            ConvertHIRExpandAliases(*hir_crate);
        });
        // Set up bindings and other useful information.
        CompilePhaseV("Resolve Bind", [&]() {
            ConvertHIRBind(*hir_crate);
        });

        // Determine what trait to use for <T>::Foo in outer scope
        // - Also inserts defaults in trait impls
        CompilePhaseV("Resolve UFCS Outer", [&]() {
            ConvertHIRResolveUFCSOuter(*hir_crate);
        });
        // Expand `Self` into the true type
        // - TODO: Move this later on, but that requires fixing some of the resolve logic around trait impl lookup
        CompilePhaseV("Resolve HIR Self Type", [&]() {
            ConvertHIRExpandAliasesSelf(*hir_crate);
        });
        // Enumerate marker impls on types and other useful metadata
        CompilePhaseV("Resolve HIR Markings", [&]() {
            ConvertHIRMarkings(*hir_crate);
        });
        CompilePhaseV("Sort Impls", [&]() {
            ConvertHIRResolveUFCSSortImpls(*hir_crate);
        });
        // Determine what trait to use for <T>::Foo (and does some associated type expansion)
        CompilePhaseV("Resolve UFCS paths", [&]() {
            ConvertHIRResolveUFCS(*hir_crate);
        });
        if (params.debug.dump_hir) {
            CompilePhaseV("Dump HIR", [&]() {
                ::std::ofstream os(FMT(params.outfile << "_2_hir.rs"));
                HIRDump(os, *hir_crate);
            });
        }
        // TODO: Expand vtables here?
        // - Some parts of constant evaluate require it
        // Basic constant evalulation (intergers/floats only)
        CompilePhaseV("Constant Evaluate", [&]() {
            ConvertHIRConstantEvaluate(*hir_crate);
        });

        if (params.debug.dump_hir) {
            // DUMP after initial consteval
            CompilePhaseV("Dump HIR", [&]() {
                ::std::ofstream os(FMT(params.outfile << "_2_hir.rs"));
                HIRDump(os, *hir_crate);
            });
        }

        // === Type checking ===
        // - This can recurse and call the MIR lower to evaluate constants

        // Check outer items first (types of constants/functions/statics/impls/...)
        // - Doesn't do any expressions except those in types
        CompilePhaseV("Typecheck Outer", [&]() {
            TypecheckModuleLevel(*hir_crate);
        });
        // Check the rest of the expressions (including function bodies)
        CompilePhaseV("Typecheck Expressions", [&]() {
            TypecheckExpressions(*hir_crate);
        });
        // === HIR Expansion ===
        // Annotate how each node's result is used
        CompilePhaseV("Expand HIR Annotate", [&]() {
            HIRExpandAnnotateUsage(*hir_crate);
        });
        CompilePhaseV("Expand HIR Static Borrow Mark", [&]() {
            HIRExpandStaticBorrowConstantsMark(*hir_crate);
        });
        // - Needs to be done after static borrows, but before closures
        CompilePhaseV("Expand HIR Lifetimes", [&]() {
            HIRExpandLifetimeInfer(*hir_crate);
        });
        // - Now that all types are known, closures can be desugared
        CompilePhaseV("Expand HIR Closures", [&]() {
            HIRExpandClosures(*hir_crate);
        });
        CompilePhaseV("Expand HIR Static Borrow", [&]() {
            HIRExpandStaticBorrowConstants(*hir_crate);
        });
        // - Construct VTables for all traits and impls.
        //  TODO: How early can this be done?
        //  > Requires consteval completed for types to be fully valid?
        //  TODO: Would prefer to have this done before consteval, as consteval might reference a vtable
        CompilePhaseV("Expand HIR VTables", [&]() {
            HIRExpandVTables(*hir_crate);
        });
        // - And calls can be turned into UFCS
        CompilePhaseV("Expand HIR Calls", [&]() {
            HIRExpandUfcsEverything(*hir_crate);
        });
        CompilePhaseV("Expand HIR Reborrows", [&]() {
            HIRExpandReborrows(*hir_crate);
        });
        CompilePhaseV("Expand HIR ErasedType", [&]() {
            HIRExpandErasedType(*hir_crate);
        });
        if (params.debug.dump_hir) {
            // DUMP after typecheck (before validation)
            CompilePhaseV("Dump HIR", [&]() {
                ::std::ofstream os(FMT(params.outfile << "_2_hir.rs"));
                HIRDump(os, *hir_crate);
            });
        }
        // - Ensure that typeck worked (including Fn trait call insertion etc)
        CompilePhaseV("Typecheck Expressions (validate)", [&]() {
            TypecheckExpressionsValidate(*hir_crate);
        });
        // HACK?: Run lifetime inference again, so that bad closures are caught
        // - Doesn't quite work, can't seem to run this twice?
        //CompilePhaseV("Expand HIR Lifetimes (validate)", [&]() {
        //    HIR_Expand_LifetimeInfer_Validate(*hir_crate);
        //    });

        if (params.last_stage == ProgramParams::STAGE_TYPECK) {
            return 0;
        }
        memory_dump("Typecheck");

        // Lower expressions into MIR
        CompilePhaseV("Lower MIR", [&]() {
            HIRGenerateMIR(*hir_crate);
        });

        if (params.debug.dump_mir) {
            // DUMP after generation
            CompilePhaseV("Dump MIR", [&]() {
                ::std::ofstream os(FMT(params.outfile << "_3_mir.rs"));
                MIRDump(os, *hir_crate);
            });
        }
        memory_dump("MIR Gen");

        // LowerMIR validates every function before returning. The next validation is
        // performed after MIR_Cleanup has actually changed the crate.

        // - Expand constants in HIR and virtualise calls
        CompilePhaseV("MIR Cleanup", [&]() {
            MIRCleanupCrate(*hir_crate);
        });
        if (params.debug.full_validate_early || getenv("MRUSTC_FULL_VALIDATE_PREOPT")) {
            CompilePhaseV("MIR Validate Full Early", [&]() {
                MIRCheckCrateFull(*hir_crate);
            });
        }

        // Optional for now
        if (params.run_borrowcheck) {
            CompilePhaseV("MIR Borrowcheck", [&]() {
                MIRBorrowCheckCrate(*hir_crate);
            });
        }

        // Optimise the MIR
        CompilePhaseV("MIR Optimise", [&]() {
            MIROptimiseCrate(*hir_crate, mir_opt_level, enable_mir_inlining);
        });

        if (params.debug.dump_mir) {
            // DUMP: After optimisation
            CompilePhaseV("Dump MIR", [&]() {
                ::std::ofstream os(FMT(params.outfile << "_3_mir.rs"));
                MIRDump(os, *hir_crate);
            });
        }
        CompilePhaseV("MIR Validate PO", [&]() {
            MIRCheckCrate(*hir_crate);
        });
        // - Exhaustive MIR validation (follows every code path and checks variable validity)
        // > DEBUGGING ONLY
        CompilePhaseV("MIR Validate Full", [&]() {
            if (params.debug.full_validate || getenv("MRUSTC_FULL_VALIDATE")) {
                MIRCheckCrateFull(*hir_crate);
            }
        });

        if (params.last_stage == ProgramParams::STAGE_MIR) {
            return 0;
        }
        memory_dump("MIR Opt");

        // TODO: Pass to mark items that are..
        // - Signature Exportable (public)
        // - MIR Exportable (public generic, #[inline], or used by a either of those)
        // - Require codegen (public or used by an exported function)
        TransOptions trans_opt;
        trans_opt.mode = params.codegen.codegen_type == "" ? "c" : params.codegen.codegen_type;
        trans_opt.build_command_file = params.codegen.emit_build_command;
        trans_opt.linker_args = params.codegen.linker_args;
        trans_opt.opt_level = params.opt_level;
        trans_opt.panic_crate = "panic_" + params.codegen.panic_type;
        for (const char* libdir : params.lib_search_dirs) {
            // Store these paths for use in final linking.
            hir_crate->m_link_paths.push_back(libdir);
        }
        for (const char* libname : params.libraries) {
            hir_crate->m_ext_libs.push_back(::HIR::ExternLibrary{libname});
        }
        trans_opt.debug_info = params.debug_info;

        // Generate code for non-generic public items (if requested)
        if (params.test_harness) {
            // If the test harness is enabled, override crate type to "Executable"
            crate_type = ::AST::Crate::Type::Executable;
        }

        // TODO: For 1.29 executables/dylibs, add oom/panic shims
        if (crate_type == ::AST::Crate::Type::ProcMacro) {
            // - Save a very basic HIR dump, making sure that there's no lang items in it (e.g. `mrustc-main`)
            CompilePhaseV("HIR Serialise", [&]() {
                HIR::Crate crate_for_ser(pool, *types);
                crate_for_ser.m_crate_name = hir_crate->m_crate_name;
                crate_for_ser.m_edition = hir_crate->m_edition;
                for (const auto& i : hir_crate->m_root_module.m_macro_items) {
                    DEBUG(i.first << ": " << i.second->ent.tag_str());
                    if (const auto* e = i.second->ent.opt_ProcMacro()) {
                        crate_for_ser.m_root_module.m_macro_items.insert(std::make_pair(i.first, box$(HIR::VisEnt<HIR::MacroItem>{i.second->publicity, *e})));
                    }
                }
                crate_for_ser.m_exported_macro_names = hir_crate->m_exported_macro_names;
                HIRSerialise(params.outfile + ".hir", crate_for_ser);
            });
        }

        // Enumerate items to be passed to codegen
        TransList items = CompilePhase<TransList>("Trans Enumerate", [&]() {
            switch (crate_type) {
                case ::AST::Crate::Type::Unknown:
                    ::std::cerr << "BUG? Unknown crate type" << ::std::endl;
                    exit(1);
                    break;
                case ::AST::Crate::Type::RustLib:
                case ::AST::Crate::Type::RustDylib:
                case ::AST::Crate::Type::CDylib:
                    return TransEnumeratePublic(*hir_crate);
                case ::AST::Crate::Type::ProcMacro:
                case ::AST::Crate::Type::Executable:
                    return TransEnumerateMain(*hir_crate);
            }
            throw ::std::runtime_error("Invalid crate_type value");
        });
        // - Generate automatic impls (mainly Clone for 1.29)
        CompilePhaseV("Trans Auto Impls", [&]() {
            // TODO: Drop glue generation?
            TransAutoImpls(*hir_crate, items);
        });
        // - Generate monomorphised versions of all functions
        CompilePhaseV("Trans Monomorph", [&]() {
            TransMonomorphiseList(*hir_crate, items, mir_opt_level);
        });
        // - Do post-monomorph inlining
        CompilePhaseV("MIR Optimise Inline", [&]() {
            MIROptimiseCrateInlining(*hir_crate, items, false, mir_opt_level, enable_mir_inlining);
        });

        // - Expand constants in HIR (using ones that were monomorphised above)
        CompilePhaseV("MIR Cleanup 2", [&]() {
            MIRCleanupSetPostMonomorph();
            MIRCleanupCrate(*hir_crate);
        });

        memory_dump("Trans");

        std::string hir_file;
        switch (crate_type) {
            case ::AST::Crate::Type::RustLib:
                // Save a loadable HIR dump
                hir_file = params.outfile + ".hir";
                CompilePhaseV("HIR Serialise", [&]() {
                    HIRSerialise(hir_file, *hir_crate);
                });
                break;
            case ::AST::Crate::Type::RustDylib:
                // Save a loadable HIR dump
                CompilePhaseV("HIR Serialise", [&]() {
                    //auto saved_ext_crates = ::std::move(hir_crate->m_ext_crates);
                    HIRSerialise(hir_file, *hir_crate);
                    //hir_crate->m_ext_crates = ::std::move(saved_ext_crates);
                });
                break;
            default:
                break;
        }

        // - Do post-monomorph inlining
        CompilePhaseV("MIR Optimise Inline PostSave", [&]() {
            MIROptimiseCrateInlining(*hir_crate, items, true, mir_opt_level, enable_mir_inlining);
        });
        // - Clean up ununused functions
        CompilePhaseV("Trans Enumerate Cleanup", [&]() {
            TransEnumerateCleanup(*hir_crate, items);
        });

        switch (crate_type) {
            case ::AST::Crate::Type::Unknown:
                throw "";
            case ::AST::Crate::Type::RustLib:
                // Generate a linkable .o
                CompilePhaseV("Trans Codegen", [&]() {
                    TransCodegen(params.outfile, CodegenOutput::StaticLibrary, trans_opt, hir_crate, std::move(items), hir_file);
                });
                break;
            case ::AST::Crate::Type::RustDylib:
            case ::AST::Crate::Type::CDylib:
                // Generate a shared library
                CompilePhaseV("Trans Codegen", [&]() {
                    TransCodegen(params.outfile, CodegenOutput::DynamicLibrary, trans_opt, hir_crate, std::move(items), hir_file);
                });
                break;
            case ::AST::Crate::Type::ProcMacro: {
                // Needs: An executable (the actual macro handler), metadata (for `extern crate foo;`)
                // - Metadata was done before enumerate
                CompilePhaseV("Trans Codegen", [&]() {
                    TransCodegen(params.outfile, CodegenOutput::Executable, trans_opt, hir_crate, std::move(items), hir_file);
                });
                break;
            }
            case ::AST::Crate::Type::Executable:
                CompilePhaseV("Trans Codegen", [&]() {
                    TransCodegen(params.outfile, CodegenOutput::Executable, trans_opt, hir_crate, std::move(items), "");
                });
                break;
        }
    } catch (unsigned int) {
    }
    //catch(const CompileError::Base& e)
    //{
    //    ::std::cerr << "Parser Error: " << e.what() << ::std::endl;
    //    return 2;
    //}
    //catch(const ::std::exception& e)
    //{
    //    ::std::cerr << "Misc Error: " << e.what() << ::std::endl;
    //    return 2;
    //}
    //catch(const char* e)
    //{
    //    ::std::cerr << "Internal Compiler Error: " << e << ::std::endl;
    //    return 2;
    //}

    return 0;
}

ProgramParams::ProgramParams(int argc, char* argv[]) {
    if (const auto* a = getenv("MRUSTC_LIBDIR")) {
        this->lib_search_dirs.push_back(a);
    }

    // Parse the rustc-compatible command-line subset supported by this driver.
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        // The following imitates rustc's version output (which the crate `rustc_version` tries to parse)
        // Report the emulated rustc release together with the native compiler version.
        if (strcmp(arg, "-vV") == 0) {
            const char* rustc_target = RUSTC_TARGET_VERSION;

            ::std::cout << "rustc " << rustc_target << ".100 (mrustc " << VersionGetString() << ")" << ::std::endl;
            ::std::cout << "binary: rustc" << ::std::endl;
            ::std::cout << "commit-hash: " << gsVersionGitHash << ::std::endl;
            ::std::cout << "commit-date: UNKNOWN" << ::std::endl;
            ::std::cout << "build-date: " << gsVersionBuildTime << ::std::endl;
            ::std::cout << "host: UNKNOWN" << ::std::endl;
            ::std::cout << "release: " << rustc_target << ".100" << ::std::endl;

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
                        this->lib_search_dirs.push_back(argv[++i]);
                    } else {
                        this->lib_search_dirs.push_back(arg + 1);
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
                    const char* lint_name;
                    if (arg[1] == '\0') {
                        if (i == argc - 1) {
                            ::std::cerr << "Option -" << flag << " requires an argument" << ::std::endl;
                            exit(1);
                        }
                        lint_name = argv[++i];
                    } else {
                        lint_name = arg + 1;
                    }
                    if (lint_name[0] == '\0') {
                        ::std::cerr << "Option -" << flag << " requires an argument" << ::std::endl;
                        exit(1);
                    }
                    const auto level = flag == 'A' ? CfgLintLevel::Allow
                        : flag == 'W' ? CfgLintLevel::Warn
                        : flag == 'D' ? CfgLintLevel::Deny
                        : CfgLintLevel::Forbid;
                    CfgSetLintLevel(lint_name, level);
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
                    auto eq_pos = optname.find('=');
                    if (eq_pos != ::std::string::npos) {
                        optval = optname.substr(eq_pos + 1);
                        optname.resize(eq_pos);
                    }
                    auto get_optval = [&]() {
                        if (eq_pos == ::std::string::npos) {
                            ::std::cerr << "Flag -C " << optname << " requires an argument" << ::std::endl;
                            exit(1);
                        }
                    };
                    //auto no_optval = [&]() {
                    //    if(eq_pos != ::std::string::npos) {
                    //        ::std::cerr << "Flag -C " << optname << " doesn't take an argument" << ::std::endl;
                    //        exit(1);
                    //    }
                    //    };

                    if (optname == "emit-build-command") {
                        get_optval();
                        this->codegen.emit_build_command = optval;
                    } else if (optname == "codegen-type") {
                        get_optval();
                        this->codegen.codegen_type = optval;
                    } else if (optname == "emit-depfile") {
                        get_optval();
                        this->emit_depfile = optval;
                    } else if (optname == "panic") {
                        get_optval();
                        this->codegen.panic_type = optval;
                    } else if (optname == "link-arg") {
                        get_optval();
                        this->codegen.linker_args.push_back(optval);
                    } else if (optname == "overflow-checks" || optname == "overflow_checks") {
                        get_optval();
                        if (optval == "n" || optval == "no" || optval == "off" || optval == "false") {
                            // The current MIR pipeline performs wrapping integer
                            // arithmetic, which is exactly the requested mode.
                        } else if (optval == "y" || optval == "yes" || optval == "on" || optval == "true") {
                            ::std::cerr << "-C " << optname << "=on is not implemented" << ::std::endl;
                            exit(1);
                        } else {
                            ::std::cerr << "invalid value for -C " << optname << ": '" << optval << "'" << ::std::endl;
                            exit(1);
                        }
                    } else if (optname == "opt-level") {
                        get_optval();
                        if (optval == "0") {
                            this->opt_level = OptimizationLevel::None;
                        } else if (optval == "1") {
                            this->opt_level = OptimizationLevel::Less;
                        } else if (optval == "2") {
                            this->opt_level = OptimizationLevel::More;
                        } else if (optval == "3") {
                            this->opt_level = OptimizationLevel::Aggressive;
                        } else if (optval == "s") {
                            this->opt_level = OptimizationLevel::Size;
                        } else if (optval == "z") {
                            this->opt_level = OptimizationLevel::SizeMin;
                        } else {
                            ::std::cerr << "optimization level needs to be between 0-3, s or z (instead was '" << optval << "')" << ::std::endl;
                            exit(1);
                        }
                    } else if (optname == "debug-assertions") {
                        if (eq_pos == ::std::string::npos || optval == "y" || optval == "yes" || optval == "on" || optval == "true") {
                            this->debug_assertions = true;
                        } else if (optval == "n" || optval == "no" || optval == "off" || optval == "false") {
                            this->debug_assertions = false;
                        } else {
                            ::std::cerr << "invalid value for -C debug-assertions: '" << optval << "'" << ::std::endl;
                            exit(1);
                        }
                        this->debug_assertions_explicit = true;
                    } else if (optname == "debuginfo") {
                        get_optval();
                        if (optval == "0" || optval == "none") {
                            this->debug_info = DebugInfoLevel::None;
                        } else if (optval == "line-directives-only") {
                            this->debug_info = DebugInfoLevel::LineDirectivesOnly;
                        } else if (optval == "line-tables-only") {
                            this->debug_info = DebugInfoLevel::LineTablesOnly;
                        } else if (optval == "1" || optval == "limited") {
                            this->debug_info = DebugInfoLevel::Limited;
                        } else if (optval == "2" || optval == "full") {
                            this->debug_info = DebugInfoLevel::Full;
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
                    auto eq_pos = optname.find('=');
                    if (eq_pos != ::std::string::npos) {
                        optval = optname.substr(eq_pos + 1);
                        optname.resize(eq_pos);
                    }
                    auto get_optval = [&]() {
                        if (eq_pos == ::std::string::npos) {
                            ::std::cerr << "Flag -Z " << optname << " requires an argument" << ::std::endl;
                            exit(1);
                        }
                    };
                    auto no_optval = [&]() {
                        if (eq_pos != ::std::string::npos) {
                            ::std::cerr << "Flag -Z " << optname << " doesn't take an argument" << ::std::endl;
                            exit(1);
                        }
                    };

                    if (optname == "disable-mir-opt") {
                        no_optval();
                        this->mir_opt_level = 0;
                        this->mir_opt_level_explicit = true;
                    } else if (optname == "mir-opt-level") {
                        get_optval();
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
                        this->mir_opt_level = value;
                        this->mir_opt_level_explicit = true;
                    } else if (optname == "next-solver") {
                        if (eq_pos == ::std::string::npos || optval == "globally") {
                            this->trait_solver.coherence = true;
                            this->trait_solver.globally = true;
                        } else if (optval == "coherence") {
                            this->trait_solver.coherence = true;
                            this->trait_solver.globally = false;
                        } else if (optval == "no") {
                            this->trait_solver.coherence = false;
                            this->trait_solver.globally = false;
                        } else {
                            ::std::cerr << "Invalid value for -Z next-solver: '" << optval
                                       << "' (expected 'no', 'coherence', or 'globally')"
                                       << ::std::endl;
                            exit(1);
                        }
                    } else if (optname == "full-validate") {
                        no_optval();
                        this->debug.full_validate = true;
                    } else if (optname == "full-validate-early") {
                        no_optval();
                        this->debug.full_validate_early = true;
                    } else if (optname == "dump-ast") {
                        no_optval();
                        this->debug.dump_ast = true;
                    } else if (optname == "dump-hir") {
                        no_optval();
                        this->debug.dump_hir = true;
                    } else if (optname == "dump-mir") {
                        no_optval();
                        this->debug.dump_mir = true;
                    } else if (optname == "stop-after") {
                        get_optval();
                        if (optval == "parse") {
                            this->last_stage = STAGE_PARSE;
                        } else if (optval == "expand") {
                            this->last_stage = STAGE_EXPAND;
                        } else if (optval == "resolve") {
                            this->last_stage = STAGE_RESOLVE;
                        } else if (optval == "typeck") {
                            this->last_stage = STAGE_TYPECK;
                        } else if (optval == "mir") {
                            this->last_stage = STAGE_MIR;
                        } else {
                            ::std::cerr << "Unknown argument to -Z stop-after - '" << optval << "'" << ::std::endl;
                            exit(1);
                        }
                    } else if (optname == "pause-after-start") {
                        this->debug.pause = true;
                    } else if (optname == "print-cfgs") {
                        no_optval();
                        this->print_cfgs = true;
                    } else if (optname == "check-cfg-all-expected") {
                        // This only controls how many expected cfg values rustc
                        // prints in diagnostics.  mrustc emits a compact
                        // diagnostic and has no corresponding display limit.
                        no_optval();
                    } else if (optname == "borrowcheck") {
                        no_optval();
                        this->run_borrowcheck = true;
                    } else {
                        ::std::cerr << "Unknown -Z flag: '" << optname << "'" << ::std::endl;
                        exit(1);
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
                        this->opt_level = OptimizationLevel::Aggressive;
                        break;
                    case 'g':
                        this->debug_info = DebugInfoLevel::Full;
                        break;
                    default:
                        ::std::cerr << "Unknown option: '-" << *arg << "'" << ::std::endl;
                        exit(1);
                }
            }
        } else {
            auto check_with_arg = [&](const char* name) -> const char* {
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
                this->show_help();
                exit(0);
            } else if (strcmp(arg, "--version") == 0) {
                const char* rustc_target = RUSTC_TARGET_VERSION;
                // NOTE: Starts the version with "rustc 1.29.100" so build scripts don't get confused
                ::std::cout << "rustc " << rustc_target << ".100 (mrustc " << VersionGetString() << ")" << ::std::endl;
                ::std::cout << "release: " << rustc_target << ".100" << ::std::endl; // `autoconfig` looks for this line
                ::std::cout << "- Build time: " << gsVersionBuildTime << ::std::endl;
                ::std::cout << "- Commit: " << gsVersionGitHash << (gbVersionGitDirty ? " (dirty tree)" : "") << ::std::endl;
                exit(0);
            }
            // --out-dir <dir>  >> Set the output directory for automatically-named files
            else if (const char* out_dir = check_with_arg("out-dir")) {
                this->output_dir = out_dir;
                if (this->output_dir != "" && this->output_dir.back() != '/') {
                    this->output_dir += '/';
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
                if (pos == nullptr) {
                    ::std::cerr << "--extern takes an argument of the format name=path" << ::std::endl;
                    exit(1);
                }

                auto name = ::std::string(desc, pos);
                auto path = ::std::string(pos + 1);
                this->crate_overrides.insert(::std::make_pair(mv$(name), mv$(path)));
            }
            // --crate-tag <name>  >> Specify a version/identifier suffix for the crate
            else if (const auto* name_str = check_with_arg("crate-tag")) {
                this->crate_name_suffix = name_str;
            }
            // --crate-name <name>  >> Specify the crate name (overrides `#![crate_name="<name>"]`)
            else if (const auto* name_str = check_with_arg("crate-name")) {
                this->crate_name = name_str;
            }
            // `--crate-type <name>`    - Specify the crate type (overrides `#![crate_type="<name>"]`)
            else if (const char* type_str = check_with_arg("crate-type")) {
                if (strcmp(type_str, "lib") == 0 || strcmp(type_str, "rlib") == 0) {
                    this->crate_type = ::AST::Crate::Type::RustLib;
                } else if (strcmp(type_str, "dylib") == 0) {
                    this->crate_type = ::AST::Crate::Type::RustDylib;
                } else if (strcmp(type_str, "bin") == 0) {
                    this->crate_type = ::AST::Crate::Type::Executable;
                } else if (strcmp(type_str, "proc-macro") == 0) {
                    this->crate_type = ::AST::Crate::Type::ProcMacro;
                } else {
                    ::std::cerr << "Unknown value for --crate-type: " << type_str << ::std::endl;
                    exit(1);
                }
            }
            // `--cfg <flag>` / `--cfg=<flag>`
            // `--cfg <var>=<value>` / `--cfg=<var>=<value>`
            else if (const char* cfg_spec = check_with_arg("cfg")) {
                ::std::string name;
                ::std::string value;
                ::std::string error;
                bool has_value = false;
                if (!CfgParseOption(cfg_spec, name, has_value, value, error)) {
                    ::std::cerr << "invalid `--cfg` argument: `" << cfg_spec << "`: " << error << ::std::endl;
                    exit(1);
                }
                if (has_value) {
                    if (name == "feature") {
                        this->features.insert(value);
                    } else {
                        CfgSetValue(mv$(name), mv$(value));
                    }
                } else {
                    CfgSetFlag(mv$(name));
                }
            } else if (const char* check_cfg_spec = check_with_arg("check-cfg")) {
                ::std::string error;
                if (!CfgSetCheckSpec(check_cfg_spec, error)) {
                    ::std::cerr << "invalid `--check-cfg` argument: `" << check_cfg_spec << "`: " << error << ::std::endl;
                    exit(1);
                }
            } else if (const char* force_warn = check_with_arg("force-warn")) {
                if (force_warn[0] == '\0') {
                    ::std::cerr << "Flag --force-warn requires an argument" << ::std::endl;
                    exit(1);
                }
                CfgSetLintLevel(force_warn, CfgLintLevel::ForceWarn);
            } else if (const char* lint_cap = check_with_arg("cap-lints")) {
                CfgLintLevel level;
                if (strcmp(lint_cap, "allow") == 0) {
                    level = CfgLintLevel::Allow;
                } else if (strcmp(lint_cap, "warn") == 0) {
                    level = CfgLintLevel::Warn;
                } else if (strcmp(lint_cap, "deny") == 0) {
                    level = CfgLintLevel::Deny;
                } else if (strcmp(lint_cap, "forbid") == 0) {
                    level = CfgLintLevel::Forbid;
                } else {
                    ::std::cerr << "unknown lint level: `" << lint_cap << "`" << ::std::endl;
                    exit(1);
                }
                CfgSetLintCap(level);
            } else if (const char* emit = check_with_arg("emit")) {
                ::std::cerr << "Ignoring `--emit " << emit << "` for compatability with rustc" << std::endl;
            }
            // `--target <triple>`  - Override the default compiler target
            else if (const char* target_name = check_with_arg("target")) {
                this->target = target_name;
            } else if (strcmp(arg, "--dump-target-spec") == 0) {
                if (i == argc - 1) {
                    ::std::cerr << "Flag " << arg << " requires an argument" << ::std::endl;
                    exit(1);
                }
                this->target_saveback = argv[++i];
            } else if (strcmp(arg, "--test") == 0) {
                this->test_harness = true;
            } else if (const char* edition_str = check_with_arg("edition")) {
                if (strcmp(edition_str, "2015") == 0) {
                    this->edition = AST::Edition::Rust2015;
                } else if (strcmp(edition_str, "2018") == 0) {
                    this->edition = AST::Edition::Rust2018;
                } else if (strcmp(edition_str, "2021") == 0) {
                    this->edition = AST::Edition::Rust2021;
                } else if (strcmp(edition_str, "2024") == 0) {
                    this->edition = AST::Edition::Rust2024;
                } else {
                    ::std::cerr << "Unknown value for " << arg << " - '" << edition_str << "'" << ::std::endl;
                    exit(1);
                }
            } else {
                ::std::cerr << "Unknown option '" << arg << "'" << ::std::endl;
                exit(1);
            }
        }
    }

    if (const auto* a = getenv("MRUSTC_DUMP")) {
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
                this->debug.dump_ast = true;
            } else if (s == "hir") {
                this->debug.dump_hir = true;
            } else if (s == "mir") {
                this->debug.dump_mir = true;
            } else {
                ::std::cerr << "Unknown option in $MRUSTC_DUMP '" << s << "'" << ::std::endl;
                // - No terminate, just warn
            }
        }
    }
}

void ProgramParams::show_help() const {
    ::std::cout << "USAGE: mrustc <sourcefile>\n"
                   "\n"
                   "OPTIONS:\n"
                   "-L <dir>           : Search for crate files (.hir) in this directory\n"
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
