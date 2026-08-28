#pragma once

#include "hir_type.h"
#include "hir_generic_params.h"
#include "hir_typeck_impl_ref.h"
#include "hir_typeck_monomorph.h"

enum class SolverCertainty : u8 {
    NoSolution,
    Ambiguous,
    Proven,
};

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

extern bool visitTyWithCb(const HIRTypeData*, HIRTypeVisitorCallback& callback);
extern bool visitTraitPathTysWithCb(const HIRTraitPath&, HIRTypeVisitorCallback& callback);
extern bool visitPathTysWithCb(const HIRPath&, HIRTypeVisitorCallback& callback);

extern bool typeContainsGenericGroup(const HIRTypeData*, HIRGenericGroup group);
extern bool pathParamsContainGenericGroup(const HIRPathParams&, HIRGenericGroup group);

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

extern bool typeClassPrimitiveCompatible(HIRInferClass ic, HIRCoreType ct);

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

bool primitiveOperatorLhsDeterminesRhs(TypeckPrimitiveOperator op, const HIRTypeData* left);

bool primitiveOperatorHasLanguageCandidate(TypeckPrimitiveOperator op, const HIRTypeData* left, const HIRTypeData* right);

bool primitiveOperatorHasBuiltin(TypeckPrimitiveOperator op, const HIRTypeData* value);

class StaticTraitResolve;
