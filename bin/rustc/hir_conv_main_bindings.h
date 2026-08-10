/*
 * MRustC - Mutabah's Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * hir_conv/main_bindings.h
 * - Functions in the "HIR Conversion" group called by main
 */
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

extern void ConvertHIR_LifetimeElision(::HIR::Crate& crate);
extern ::HIR::PathParams ConvertHIR_CompleteAliasParams(::HIR::TypeInterner& types, const Span& sp, const ::HIR::GenericParams& params_def, const ::HIR::GenericPath& path, bool is_expr);
extern void ConvertHIR_ExpandAliases(::HIR::Crate& crate);
extern void ConvertHIR_ExpandAliases_Self(::HIR::Crate& crate);
extern void ConvertHIR_ExpandAliases_Self_Expr(
    const ::HIR::Crate& crate,
    const ::HIR::TypeRef& impl_type,
    ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>>& args,
    ::HIR::TypeRef& ret_ty,
    ::HIR::ExprPtr& expr
    );
extern void ConvertHIR_Bind(::HIR::Crate& crate);
extern void ConvertHIR_ResolveUFCS_SortImpls(::HIR::Crate& crate);
extern void ConvertHIR_ResolveUFCS_Outer(::HIR::Crate& crate);
extern void ConvertHIR_ResolveUFCS(::HIR::Crate& crate);
extern void ConvertHIR_Markings(::HIR::Crate& crate);
extern void ConvertHIR_ConstantEvaluate(::HIR::Crate& hir_crate);

extern void ConvertHIR_ResolveUFCS_Expr(const ::HIR::Crate& crate, const ::HIR::ItemPath& ip, ::HIR::ExprPtr& expr_ptr);
extern void ConvertHIR_ConstantEvaluate_Expr(const ::HIR::Crate& crate, const ::HIR::ItemPath& ip, ::HIR::ExprPtr& exp);
extern void ConvertHIR_ConstantEvaluate_Enum(const ::HIR::Crate& crate, const ::HIR::ItemPath& ip, const ::HIR::Enum& enm);
extern void ConvertHIR_ConstantEvaluate_Constant(const ::HIR::Crate& crate, const ::HIR::GenericParams* impl_params, const ::HIR::ItemPath& ip, ::HIR::Constant& e);
extern void ConvertHIR_ConstantEvaluate_MethodParams(const Span& sp, const ::HIR::Crate& crate, const HIR::SimplePath& mod_path, const ::HIR::GenericParams* impl_generics, const ::HIR::GenericParams* item_generics, const ::HIR::GenericParams* params_def, ::HIR::PathParams& params);
extern void ConvertHIR_ConstantEvaluate_ConstGeneric(const Span& sp, const ::HIR::Crate& crate, const HIR::TypeRef& ty, ::HIR::ConstGeneric& cg);
extern void ConvertHIR_ConstantEvaluate_ConstGeneric(const Span& sp, const ::HIR::Crate& crate, ::HIR::ConstGeneric& cg);
extern void ConvertHIR_ConstantEvaluate_ArraySize(const Span& sp, const ::HIR::Crate& crate, const ::HIR::SimplePath& path, ::HIR::ArraySize& size);
