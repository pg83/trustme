#pragma once

#include "hir_type.h"
#include "hir_generic_params.h"
#include "hir_typeck_impl_ref.h"
#include "hir_typeck_monomorph.h"

typedef ::std::function<bool(const HIRTypeData*)> tCbVisitTy;
/// Calls the provided callback on every type seen when recursing the type.
/// If the callback returns `true`, no further types are visited and the function returns `true`.
extern bool visitTyWith(const HIRTypeData*, tCbVisitTy callback);
extern bool visitTraitPathTysWith(const HIRTraitPath&, tCbVisitTy callback);
extern bool visitPathTysWith(const HIRPath&, tCbVisitTy callback);

typedef ::std::function<bool(HIRTypeRef& rewritten, HIRTypeData& data)> tCbRewriteTy;
extern bool rewriteTyWith(HIRTypeInterner& types, HIRTypeRef& ty, tCbRewriteTy callback);
extern bool rewritePathTysWith(HIRTypeInterner& types, HIRPath& path, tCbRewriteTy callback);

typedef ::std::function<bool(const HIRTypeData*, HIRTypeRef&)> tCbCloneTy;
/// Clones a type, calling the provided callback on every type (optionally providing a replacement)
///
/// Closure should return `true` if the passed output slot was populated.
extern HIRTypeRef cloneTyWith(HIRTypeInterner& types, const Span& sp, const HIRTypeData* tpl, tCbCloneTy callback);
extern HIRPathParams clonePathParamsWith(HIRTypeInterner& types, const Span& sp, const HIRPathParams& tpl, tCbCloneTy callback);

extern void checkTypeClassPrimitive(const Span& sp, const HIRTypeData* type, HIRInferClass ic, HIRCoreType ct);

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

bool primitiveOperatorHasBuiltin(TypeckPrimitiveOperator op, const HIRTypeData* left, const HIRTypeData* right);

// For these binary language operations, once the left-hand type is known
// it also fixes an otherwise untyped right-hand operand. Shifts are
// deliberately excluded: their right-hand side need only be an integer
// and may have a different type.
bool primitiveOperatorLhsDeterminesRhs(TypeckPrimitiveOperator op, const HIRTypeData* left);

// A binary language candidate is available either when both operands are
// already known to be valid primitive inputs, or when the known lhs
// determines the still-inferred rhs.
bool primitiveOperatorHasLanguageCandidate(TypeckPrimitiveOperator op, const HIRTypeData* left, const HIRTypeData* right);

bool primitiveOperatorHasBuiltin(TypeckPrimitiveOperator op, const HIRTypeData* value);

class StaticTraitResolve;
extern void TypecheckExpressionsValidateOne(const StaticTraitResolve& resolve, const ::std::vector<::std::pair<HIRPattern, HIRTypeRef>>& args, const HIRTypeData* retTy, const HIRExprPtr& code);
