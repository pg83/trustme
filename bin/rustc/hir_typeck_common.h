#pragma once

#include "hir_type.h"
#include "hir_generic_params.h"
#include "hir_typeck_impl_ref.h"
#include "hir_typeck_monomorph.h"

struct HIRTypeVisitorCallback {
    virtual bool visit(const HIRTypeData* type) = 0;
};

template <typename F>
struct HIRTypeVisitorCb final: HIRTypeVisitorCallback {
    F f;

    explicit HIRTypeVisitorCb(F f)
        : f(f)
    {
    }

    bool visit(const HIRTypeData* type) override {
        return f(type);
    }
};

/// Calls the provided callback on every type seen when recursing the type.
/// If the callback returns `true`, no further types are visited and the function returns `true`.
extern bool visitTyWithCb(const HIRTypeData*, HIRTypeVisitorCallback& callback);
extern bool visitTraitPathTysWithCb(const HIRTraitPath&, HIRTypeVisitorCallback& callback);
extern bool visitPathTysWithCb(const HIRPath&, HIRTypeVisitorCallback& callback);

template <typename F>
bool visitTyWith(const HIRTypeData* type, F f) {
    HIRTypeVisitorCb<F> cb(f);
    return visitTyWithCb(type, cb);
}

template <typename F>
bool visitTraitPathTysWith(const HIRTraitPath& path, F f) {
    HIRTypeVisitorCb<F> cb(f);
    return visitTraitPathTysWithCb(path, cb);
}

template <typename F>
bool visitPathTysWith(const HIRPath& path, F f) {
    HIRTypeVisitorCb<F> cb(f);
    return visitPathTysWithCb(path, cb);
}

struct HIRTypeRewriteCallback {
    virtual bool rewrite(HIRTypeRef& rewritten, HIRTypeData& data) = 0;
};

template <typename F>
struct HIRTypeRewriteCb final: HIRTypeRewriteCallback {
    F f;

    explicit HIRTypeRewriteCb(F f)
        : f(f)
    {
    }

    bool rewrite(HIRTypeRef& rewritten, HIRTypeData& data) override {
        return f(rewritten, data);
    }
};

extern bool rewriteTyWithCb(HIRTypeInterner& types, HIRTypeRef& ty, HIRTypeRewriteCallback& callback);
extern bool rewritePathTysWithCb(HIRTypeInterner& types, HIRPath& path, HIRTypeRewriteCallback& callback);

template <typename F>
bool rewriteTyWith(HIRTypeInterner& types, HIRTypeRef& ty, F f) {
    HIRTypeRewriteCb<F> cb(f);
    return rewriteTyWithCb(types, ty, cb);
}

template <typename F>
bool rewritePathTysWith(HIRTypeInterner& types, HIRPath& path, F f) {
    HIRTypeRewriteCb<F> cb(f);
    return rewritePathTysWithCb(types, path, cb);
}

struct HIRTypeCloneCallback {
    virtual bool clone(const HIRTypeData* type, HIRTypeRef& replacement) = 0;
};

template <typename F>
struct HIRTypeCloneCb final: HIRTypeCloneCallback {
    F f;

    explicit HIRTypeCloneCb(F f)
        : f(f)
    {
    }

    bool clone(const HIRTypeData* type, HIRTypeRef& replacement) override {
        return f(type, replacement);
    }
};

/// Clones a type, calling the provided callback on every type (optionally providing a replacement)
///
/// Closure should return `true` if the passed output slot was populated.
extern HIRTypeRef cloneTyWithCb(HIRTypeInterner& types, const Span& sp, const HIRTypeData* tpl, HIRTypeCloneCallback& callback);
extern HIRPathParams clonePathParamsWithCb(HIRTypeInterner& types, const Span& sp, const HIRPathParams& tpl, HIRTypeCloneCallback& callback);

template <typename F>
HIRTypeRef cloneTyWith(HIRTypeInterner& types, const Span& sp, const HIRTypeData* tpl, F f) {
    HIRTypeCloneCb<F> cb(f);
    return cloneTyWithCb(types, sp, tpl, cb);
}

template <typename F>
HIRPathParams clonePathParamsWith(HIRTypeInterner& types, const Span& sp, const HIRPathParams& tpl, F f) {
    HIRTypeCloneCb<F> cb(f);
    return clonePathParamsWithCb(types, sp, tpl, cb);
}

extern void checkTypeClassPrimitive(const Span& sp, const HIRTypeData* type, HIRInferClass ic, HIRCoreType ct);
/// Non-fatal form of the same compatibility rule, for callers that report a
/// mismatch as a result instead of a diagnostic.
extern bool typeClassPrimitiveCompatible(HIRInferClass ic, HIRCoreType ct);

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
