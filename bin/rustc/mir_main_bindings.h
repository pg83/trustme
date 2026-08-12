#pragma once

#include "hir_hir.h"

#include <iostream>

struct WireBoard;

class TransList;

extern void HIRGenerateMIR(const WireBoard& wb, HIRCrate& crate);
extern void MIRDump(::std::ostream& sink, const HIRCrate& crate);

extern void MIRCleanupCrate(const WireBoard& wb, HIRCrate& crate);
extern void MIRCleanupSetPostMonomorph();
extern void MIROptimiseCrate(const WireBoard& wb, HIRCrate& crate, unsigned optLevel, bool enableInlining);
extern void MIROptimiseCrateInlining(const WireBoard& wb, const HIRCrate& crate, TransList& list, bool postSave, unsigned optLevel, bool enableInlining);

extern void HIRGenerateMIRExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& path, HIRExprPtr& exprPtr, const HIRFunction::argsT& args, const HIRTypeData* resTy);
