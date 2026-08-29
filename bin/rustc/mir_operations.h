#pragma once

#include "output.h"

#include "hir_item_path.h"
#include "hir_typeck_static.h"

namespace stl {
    class ObjPool;
}

struct WireBoard;
void MIRCreateOperationsContext(WireBoard& wb, stl::ObjPool& pool);

void MIRCleanup(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn, const HIRFunction::argsT& args, const HIRTypeData* retType);

void MIROptimise(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn, const HIRFunction::argsT& args, const HIRTypeData* retType, unsigned optLevel, bool doInline = true, bool validate = true);
void MIROptimiseMin(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn, const HIRFunction::argsT& args, const HIRTypeData* retType);
void MIRSortBlocks(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn);

void MIRDumpFcn(stl::ZeroCopyOutput& sink, const MIRFunction& fcn, unsigned int il = 0);
