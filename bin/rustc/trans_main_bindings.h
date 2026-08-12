#pragma once

#include "trans_trans_list.h"

namespace HIR {
    class Crate;
}

enum class OptimizationLevel : unsigned {
    None,
    Less,
    More,
    Aggressive,
    Size,
    SizeMin,
};

enum class DebugInfoLevel : unsigned {
    None,
    LineDirectivesOnly,
    LineTablesOnly,
    Limited,
    Full,
};

struct TransOptions {
    ::std::string mode = "c";
    OptimizationLevel opt_level = OptimizationLevel::None;
    DebugInfoLevel debugInfo = DebugInfoLevel::None;
    ::std::string buildCommandFile;
    ::std::vector<::std::string> linkerArgs;

    ::std::string panic_crate;

    ::std::vector<::std::string> librarySearchDirs;
    ::std::vector<::std::string> libraries;
};

enum class CodegenOutput {
    Object,         // .o
    StaticLibrary,  // .a
    DynamicLibrary, // .so
    Executable,     // no suffix, includes main stub (TODO: Can't that just be added earlier?)
};

extern TransList TransEnumerateMain(const ::HIR::Crate& crate);
// NOTE: This also sets the saveout flags
extern TransList TransEnumeratePublic(::HIR::Crate& crate);

/// Re-run enumeration on monomorphised functions, removing now-unused items
extern void TransEnumerateCleanup(const ::HIR::Crate& crate, TransList& list);

extern void TransAutoImpls(::HIR::Crate& crate, TransList& trans_list);

extern void TransMonomorphiseList(const ::HIR::Crate& crate, TransList& list, unsigned mir_opt_level);

extern void TransCodegen(const ::std::string& outfile, CodegenOutput out_ty, const TransOptions& opt, ::HIR::Crate* crate, TransList list, const ::std::string& hirFile);
