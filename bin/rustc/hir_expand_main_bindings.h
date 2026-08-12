#pragma once

#include <vector>
#include <utility> // std::pair

namespace HIR {
    class Crate;
    class ExprPtr;
    class TypeData;
    using TypeRef = const TypeData*;
    class TraitImpl;
    struct Pattern;
    class ItemPath;
};

extern void HIRExpandAnnotateUsage(::HIR::Crate& crate);
extern void HIRExpandVTables(::HIR::Crate& crate);
extern void HIRExpandClosures(::HIR::Crate& crate);
extern void HIRExpandUfcsEverything(::HIR::Crate& crate);
extern void HIRExpandReborrows(::HIR::Crate& crate);
extern void HIRExpandErasedType(::HIR::Crate& crate);
extern void HIRExpandStaticBorrowConstantsMark(::HIR::Crate& crate);
extern void HIRExpandStaticBorrowConstants(::HIR::Crate& crate);

extern void HIRExpandAnnotateUsageExpr(const ::HIR::Crate& crate, const ::HIR::ItemPath& ip, ::HIR::ExprPtr& exp);
extern void HIRExpandClosuresExpr(const ::HIR::Crate& crate, ::HIR::TypeRef& exp_ty, ::HIR::ExprPtr& exp);
extern void HIRExpandUfcsEverythingExpr(const ::HIR::Crate& crate, ::HIR::ExprPtr& exp, const ::HIR::TraitImpl* current_trait_impl = nullptr);
extern void HIRExpandReborrowsExpr(const ::HIR::Crate& crate, ::HIR::ExprPtr& exp);
extern void HIRExpandStaticBorrowConstantsMarkExpr(const ::HIR::Crate& crate, const ::HIR::ItemPath& ip, ::HIR::ExprPtr& exp);
extern void HIRExpandStaticBorrowConstantsExpr(const ::HIR::Crate& crate, const ::HIR::ItemPath& ip, ::HIR::ExprPtr& exp);
extern void HIRExpandLifetimeInfer(::HIR::Crate& crate);
extern void HIRExpandLifetimeInferValidate(::HIR::Crate& crate);
extern void HIRExpandLifetimeInferExpr(const ::HIR::Crate& crate, const ::HIR::ItemPath& ip, const ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>>& args, const HIR::TypeData* ret_ty, ::HIR::ExprPtr& exp);
