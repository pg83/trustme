#pragma once

#include "output.h"
#include "hir_item_path.h"
#include "hir_typeck_static.h"

namespace stl {
    class ObjPool;
}

struct WireBoard;
class HIRCrate;
struct TransList;
void MIRCreateOperationsContext(WireBoard& wb, stl::ObjPool& pool);

void MIRCleanup(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn, const HIRFunction::argsT& args, const HIRType* retType);

void MIROptimise(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn, const HIRFunction::argsT& args, const HIRType* retType, unsigned optLevel, bool doInline = true, bool validate = true);
void MIROptimiseMin(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn, const HIRFunction::argsT& args, const HIRType* retType);
void MIRSortBlocks(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn);
void MIRCleanupCrate(const WireBoard& wb, HIRCrate& crate);
void MIROptimiseCrate(const WireBoard& wb, HIRCrate& crate, unsigned optLevel, bool enableInlining);
void MIROptimiseCrateInlining(const WireBoard& wb, const HIRCrate& crate, TransList& list, bool postSave, unsigned optLevel, bool enableInlining);
