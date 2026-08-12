#pragma once

#include "hir_item_path.h"
#include "hir_typeck_static.h"

// Check that the MIR is well-formed
extern void MIRValidate(const StaticTraitResolve& resolve, const HIRItemPath& path, const MIRFunction& fcn, const HIRFunction::argsT& args, const HIRTypeData* retType);
// -
extern void MIRValidateFull(const StaticTraitResolve& resolve, const HIRItemPath& path, const MIRFunction& fcn, const HIRFunction::argsT& args, const HIRTypeData* retType);
// Perform needed changes to the generated MIR (virtualisation, Unsize/CoerceUnsize, ...)
extern void MIRCleanup(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn, const HIRFunction::argsT& args, const HIRTypeData* retType);
// Optimise the MIR
extern void MIROptimise(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn, const HIRFunction::argsT& args, const HIRTypeData* retType, unsigned optLevel, bool doInline = true, bool validate = true);
extern void MIROptimiseMin(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn, const HIRFunction::argsT& args, const HIRTypeData* retType);
extern void MIRSortBlocks(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn);


extern void MIRDumpFcn(::std::ostream& sink, const MIRFunction& fcn, unsigned int il = 0);
