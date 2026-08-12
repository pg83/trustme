#pragma once

#include "hir_type.h"
#include "hir_generic_params.h"
#include "hir_typeck_impl_ref.h"
#include "hir_typeck_monomorph.h"

typedef ::std::function<bool(const ::HIR::TypeData*)> tCbVisitTy;
/// Calls the provided callback on every type seen when recursing the type.
/// If the callback returns `true`, no further types are visited and the function returns `true`.
extern bool visitTyWith(const ::HIR::TypeData*, tCbVisitTy callback);
extern bool visitTraitPathTysWith(const ::HIR::TraitPath&, tCbVisitTy callback);
extern bool visitPathTysWith(const ::HIR::Path&, tCbVisitTy callback);

typedef ::std::function<bool(::HIR::TypeRef& rewritten, ::HIR::TypeData& data)> tCbRewriteTy;
extern bool rewriteTyWith(::HIR::TypeInterner& types, ::HIR::TypeRef& ty, tCbRewriteTy callback);
extern bool rewritePathTysWith(::HIR::TypeInterner& types, ::HIR::Path& path, tCbRewriteTy callback);

typedef ::std::function<bool(const ::HIR::TypeData*, ::HIR::TypeRef&)> tCbCloneTy;
/// Clones a type, calling the provided callback on every type (optionally providing a replacement)
///
/// Closure should return `true` if the passed output slot was populated.
extern ::HIR::TypeRef cloneTyWith(::HIR::TypeInterner& types, const Span& sp, const ::HIR::TypeData* tpl, tCbCloneTy callback);
extern ::HIR::PathParams clonePathParamsWith(::HIR::TypeInterner& types, const Span& sp, const ::HIR::PathParams& tpl, tCbCloneTy callback);

extern void checkTypeClassPrimitive(const Span& sp, const ::HIR::TypeData* type, ::HIR::InferClass ic, ::HIR::CoreType ct);

// The primitive operation is a language candidate, separate from an
// implementation of the operator trait.  Keeping this classification in
// one place makes type checking, UFCS expansion, and validation agree on
// which expressions may remain as MIR primitive operations.
enum class TypeckPrimitiveOperator {
    None,

    Add,
    Sub,
    Mul,
    Div,
    Rem,
    BitAnd,
    BitOr,
    BitXor,
    Shl,
    Shr,
    Equal,
    Order,
    Not,
    Neg,
    Deref,

    AddAssign,
    SubAssign,
    MulAssign,
    DivAssign,
    RemAssign,
    BitAndAssign,
    BitOrAssign,
    BitXorAssign,
    ShlAssign,
    ShrAssign,
};

bool primitiveOperatorHasBuiltin(TypeckPrimitiveOperator op, const ::HIR::TypeData* left, const ::HIR::TypeData* right);

// For these binary language operations, once the left-hand type is known
// it also fixes an otherwise untyped right-hand operand. Shifts are
// deliberately excluded: their right-hand side need only be an integer
// and may have a different type.
bool primitiveOperatorLhsDeterminesRhs(TypeckPrimitiveOperator op, const ::HIR::TypeData* left);

// A binary language candidate is available either when both operands are
// already known to be valid primitive inputs, or when the known lhs
// determines the still-inferred rhs.
bool primitiveOperatorHasLanguageCandidate(TypeckPrimitiveOperator op, const ::HIR::TypeData* left, const ::HIR::TypeData* right);

bool primitiveOperatorHasBuiltin(TypeckPrimitiveOperator op, const ::HIR::TypeData* value);

class StaticTraitResolve;
extern void TypecheckExpressionsValidateOne(const StaticTraitResolve& resolve, const ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>>& args, const ::HIR::TypeData* retTy, const ::HIR::ExprPtr& code);
