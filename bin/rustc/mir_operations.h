#pragma once

#include "hir_typeck_static.h"
#include "hir_item_path.h"

// Check that the MIR is well-formed
extern void MIRValidate(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, const MIRFunction& fcn, const ::HIR::Function::argsT& args, const ::HIR::TypeData* retType);
// -
extern void MIRValidateFull(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, const MIRFunction& fcn, const ::HIR::Function::argsT& args, const ::HIR::TypeData* retType);
// Perform needed changes to the generated MIR (virtualisation, Unsize/CoerceUnsize, ...)
extern void MIRCleanup(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, MIRFunction& fcn, const ::HIR::Function::argsT& args, const ::HIR::TypeData* retType);
// Optimise the MIR
extern void MIROptimise(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, MIRFunction& fcn, const ::HIR::Function::argsT& args, const ::HIR::TypeData* retType, unsigned optLevel, bool doInline = true, bool validate = true);
extern void MIROptimiseMin(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, MIRFunction& fcn, const ::HIR::Function::argsT& args, const ::HIR::TypeData* retType);
extern void MIRSortBlocks(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, MIRFunction& fcn);

extern void MIRBorrowCheck(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, MIRFunction& fcn, const ::HIR::Function::argsT& args, const ::HIR::TypeData* retType);

extern void MIRDumpFcn(::std::ostream& sink, const MIRFunction& fcn, unsigned int il = 0);
