#pragma once

#include "output.h"

#include "hir_hir.h"


struct WireBoard;

class TransList;

void HIRGenerateMIR(const WireBoard& wb, HIRCrate& crate);
void MIRDump(stl::ZeroCopyOutput& sink, const HIRCrate& crate);

void MIRCleanupCrate(const WireBoard& wb, HIRCrate& crate);
void MIROptimiseCrate(const WireBoard& wb, HIRCrate& crate, unsigned optLevel, bool enableInlining);
void MIROptimiseCrateInlining(const WireBoard& wb, const HIRCrate& crate, TransList& list, bool postSave, unsigned optLevel, bool enableInlining);

void HIRGenerateMIRExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& path, HIRExprPtr& exprPtr, const HIRFunction::argsT& args, const HIRTypeData* resTy);
