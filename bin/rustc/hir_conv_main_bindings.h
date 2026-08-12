#pragma once

#include <utility>
#include <vector>

struct Span;

namespace HIR {
    class Crate;
    class ItemPath;
    class ExprPtr;
    struct Pattern;
    class Enum;
    class Constant;
    class TypeData;
    using TypeRef = const TypeData*;
    class TypeInterner;
    struct SimplePath;
    struct GenericPath;
    class GenericParams;
    struct PathParams;
    class ConstGeneric;
    class ArraySize;
};

extern void ConvertHIRLifetimeElision(::HIR::Crate& crate);
extern ::HIR::PathParams ConvertHIRCompleteAliasParams(::HIR::TypeInterner& types, const Span& sp, const ::HIR::GenericParams& paramsDef, const ::HIR::GenericPath& path, bool isExpr);
extern void ConvertHIRExpandAliases(::HIR::Crate& crate);
extern void ConvertHIRExpandAliasesSelf(::HIR::Crate& crate);
extern void ConvertHIRExpandAliasesSelfExpr(
    const ::HIR::Crate& crate,
    const ::HIR::TypeData* impl_type,
    ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>>& args,
    ::HIR::TypeRef& ret_ty,
    ::HIR::ExprPtr& expr
    );
extern void ConvertHIRBind(::HIR::Crate& crate);
extern void ConvertHIRResolveUFCSSortImpls(::HIR::Crate& crate);
extern void ConvertHIRResolveUFCSOuter(::HIR::Crate& crate);
extern void ConvertHIRResolveUFCS(::HIR::Crate& crate);
extern void ConvertHIRMarkings(::HIR::Crate& crate);
extern void ConvertHIRConstantEvaluate(::HIR::Crate& hirCrate);

extern void ConvertHIRResolveUFCSExpr(const ::HIR::Crate& crate, const ::HIR::ItemPath& ip, ::HIR::ExprPtr& expr_ptr);
extern void ConvertHIRConstantEvaluateExpr(const ::HIR::Crate& crate, const ::HIR::ItemPath& ip, ::HIR::ExprPtr& exp);
extern void ConvertHIRConstantEvaluateEnum(const ::HIR::Crate& crate, const ::HIR::ItemPath& ip, const ::HIR::Enum& enm);
extern void ConvertHIRConstantEvaluateConstant(const ::HIR::Crate& crate, const ::HIR::GenericParams* impl_params, const ::HIR::ItemPath& ip, ::HIR::Constant& e);
extern void ConvertHIRConstantEvaluateMethodParams(const Span& sp, const ::HIR::Crate& crate, const HIR::SimplePath& mod_path, const ::HIR::GenericParams* impl_generics, const ::HIR::GenericParams* item_generics, const ::HIR::GenericParams* paramsDef, ::HIR::PathParams& params);
extern void ConvertHIRConstantEvaluateConstGeneric(const Span& sp, const ::HIR::Crate& crate, const HIR::TypeData* ty, ::HIR::ConstGeneric& cg);
extern void ConvertHIRConstantEvaluateConstGeneric(const Span& sp, const ::HIR::Crate& crate, ::HIR::ConstGeneric& cg);
extern void ConvertHIRConstantEvaluateArraySize(const Span& sp, const ::HIR::Crate& crate, const ::HIR::SimplePath& path, ::HIR::ArraySize& size);
