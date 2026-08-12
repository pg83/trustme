#pragma once

#include <iostream>
#include "hir_hir.h"

class TransList;

extern void HIRGenerateMIR(HIRCrate& crate);
extern void MIRDump(::std::ostream& sink, const HIRCrate& crate);
extern void MIRCheckCrate(/*const*/ HIRCrate& crate);
extern void MIRCheckCrateFull(/*const*/ HIRCrate& crate);
extern void MIRBorrowCheckCrate(HIRCrate& crate);

extern void MIRCleanupCrate(HIRCrate& crate);
extern void MIRCleanupSetPostMonomorph();
extern void MIROptimiseCrate(HIRCrate& crate, unsigned optLevel, bool enableInlining);
extern void MIROptimiseCrateInlining(const HIRCrate& crate, TransList& list, bool postSave, unsigned optLevel, bool enableInlining);

extern void HIRGenerateMIRExpr(const HIRCrate& crate, const HIRItemPath& path, HIRExprPtr& exprPtr, const HIRFunction::argsT& args, const HIRTypeData* resTy);
