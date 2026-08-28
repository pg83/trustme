#pragma once

#include "trans_trans_list.h"

class HIRCrate;
class MIRFunction;
struct WireBoard;

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
    OptimizationLevel optLevel = OptimizationLevel::None;
    DebugInfoLevel debugInfo = DebugInfoLevel::None;
    ::std::string buildCommandFile;

    bool emitCppOnly = false;
    ::std::vector<::std::string> linkerArgs;

    ::std::string panicCrate;

    ::std::vector<::std::string> librarySearchDirs;
    ::std::vector<::std::string> frameworkSearchDirs;
    ::std::vector<::std::string> libraries;
};

enum class CodegenOutput {
    Object,
    StaticLibrary,
    DynamicLibrary,
    Executable, // no suffix, includes main stub (TODO: Can't that just be added earlier?)
};

extern TransList TransEnumerateMain(const WireBoard& wb, HIRCrate& crate);

extern TransList TransEnumeratePublic(const WireBoard& wb, HIRCrate& crate);

extern void TransEnumerateCleanup(const WireBoard& wb, const HIRCrate& crate, TransList& list);

extern void TransAutoImpls(const WireBoard& wb, HIRCrate& crate, TransList& transList);

extern void TransEnumerateGeneratedStatics(const WireBoard& wb, TransList& list, const ::std::vector<HIRPath>& paths);
extern bool TransEnumerateGeneratedLiteral(const WireBoard& wb, TransList& list, const EncodedLiteral& literal);
extern bool TransEnumerateGeneratedMIR(const WireBoard& wb, TransList& list, const stl::Vector<const TransListFunction*>& functions);
extern void TransMonomorphiseList(const WireBoard& wb, HIRCrate& crate, TransList& list, unsigned mirOptLevel);

extern void TransCodegen(const WireBoard& wb, const ::std::string& outfile, CodegenOutput outTy, const TransOptions& opt, HIRCrate* crate, TransList list, const ::std::string& hirFile);
