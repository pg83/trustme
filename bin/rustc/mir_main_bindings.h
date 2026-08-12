#pragma once

#include <iostream>
#include "hir_hir.h"

class TransList;

extern void HIRGenerateMIR(::HIR::Crate& crate);
extern void MIRDump(::std::ostream& sink, const ::HIR::Crate& crate);
extern void MIRCheckCrate(/*const*/ ::HIR::Crate& crate);
extern void MIRCheckCrateFull(/*const*/ ::HIR::Crate& crate);
extern void MIRBorrowCheckCrate(::HIR::Crate& crate);

extern void MIRCleanupCrate(::HIR::Crate& crate);
extern void MIRCleanupSetPostMonomorph();
extern void MIROptimiseCrate(::HIR::Crate& crate, unsigned opt_level, bool enableInlining);
extern void MIROptimiseCrateInlining(const ::HIR::Crate& crate, TransList& list, bool post_save, unsigned opt_level, bool enableInlining);

extern void HIRGenerateMIRExpr(const ::HIR::Crate& crate, const ::HIR::ItemPath& path, ::HIR::ExprPtr& expr_ptr, const ::HIR::Function::argsT& args, const ::HIR::TypeData* res_ty);
