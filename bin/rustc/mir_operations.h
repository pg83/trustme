#pragma once

#include "hir_typeck_static.h"
#include "hir_item_path.h"

// Check that the MIR is well-formed
extern void MIRValidate(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, const ::MIR::Function& fcn, const ::HIR::Function::args_t& args, const ::HIR::TypeData* ret_type);
// -
extern void MIRValidateFull(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, const ::MIR::Function& fcn, const ::HIR::Function::args_t& args, const ::HIR::TypeData* ret_type);
// Perform needed changes to the generated MIR (virtualisation, Unsize/CoerceUnsize, ...)
extern void MIRCleanup(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, ::MIR::Function& fcn, const ::HIR::Function::args_t& args, const ::HIR::TypeData* ret_type);
// Optimise the MIR
extern void MIROptimise(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, ::MIR::Function& fcn, const ::HIR::Function::args_t& args, const ::HIR::TypeData* ret_type, unsigned opt_level, bool do_inline = true, bool validate = true);
extern void MIROptimiseMin(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, ::MIR::Function& fcn, const ::HIR::Function::args_t& args, const ::HIR::TypeData* ret_type);
extern void MIRSortBlocks(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, ::MIR::Function& fcn);

extern void MIRBorrowCheck(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, ::MIR::Function& fcn, const ::HIR::Function::args_t& args, const ::HIR::TypeData* ret_type);

extern void MIRDumpFcn(::std::ostream& sink, const ::MIR::Function& fcn, unsigned int il = 0);
