#pragma once

#include "hir_hir.h"

#include <iostream>

struct WireBoard;

class TransList;

void HIRGenerateMIR(const WireBoard& wb, HIRCrate& crate);
void MIRDump(std::ostream& sink, const HIRCrate& crate);

void MIRCleanupCrate(const WireBoard& wb, HIRCrate& crate);
void MIROptimiseCrate(const WireBoard& wb, HIRCrate& crate, unsigned optLevel, bool enableInlining);
void MIROptimiseCrateInlining(const WireBoard& wb, const HIRCrate& crate, TransList& list, bool postSave, unsigned optLevel, bool enableInlining);

void HIRGenerateMIRExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& path, HIRExprPtr& exprPtr, const HIRFunction::argsT& args, const HIRTypeData* resTy);
