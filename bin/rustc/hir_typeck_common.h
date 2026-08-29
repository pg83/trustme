#pragma once

#include "hir_hir.h"
#include "hir_type.h"
#include "hir_item_path.h"
#include "hir_generic_params.h"

bool monomorphisePathparamsNeeded(const HIRPathParams& tpl);
bool monomorphisePathNeeded(const HIRPath& tpl);
bool monomorphiseTraitpathNeeded(const HIRTraitPath& tpl);
bool monomorphiseTypeNeeded(const HIRTypeData* tpl);

struct WireBoard;

class Monomorphiser {
protected:
    HIRTypeInterner& types;

private:
    const WireBoard* constevalWb;
    HIRItemPath constevalPath;

public:
    explicit Monomorphiser(HIRTypeInterner& types);

    virtual ~Monomorphiser() = default;

    HIRTypeInterner& typeInterner() const {
        return types;
    }

    void setConstevalState(const WireBoard& wb, HIRItemPath ip);

    virtual HIRTypeRef getType(const Span& sp, const HIRGenericRef& g) const = 0;
    virtual HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& g) const = 0;

    virtual HIRTypeRef monomorphType(const Span& sp, const HIRTypeData* ty, bool allowInfer = true) const;
    HIRPath monomorphPath(const Span& sp, const HIRPath& tpl, bool allowInfer = true) const;
    HIRTraitPath monomorphTraitpath(const Span& sp, const HIRTraitPath& tpl, bool allowInfer) const;
    HIRTraitPath::AtyEqual monomorphTpAtyEqual(const Span& sp, const HIRTraitPath::AtyEqual& tpl, bool allowInfer) const;
    HIRPathParams monomorphPathParams(const Span& sp, const HIRPathParams& tpl, bool allowInfer) const;
    virtual HIRGenericPath monomorphGenericpath(const Span& sp, const HIRGenericPath& tpl, bool allowInfer = true) const;

    virtual HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& val, bool allowInfer) const;
    HIRArraySize monomorphArraysize(const Span& sp, const HIRArraySize& tpl) const;

    const HIRTypeData* maybeMonomorphType(const Span& sp, HIRTypeRef& tmp, const HIRTypeData* ty, bool allowInfer = true) const;
};

class MonomorphiserPP: public Monomorphiser {
public:
    explicit MonomorphiserPP(HIRTypeInterner& types);

    virtual const HIRTypeData* getSelfType() const = 0;
    virtual const HIRPathParams* getImplParams() const = 0;
    virtual const HIRPathParams* getMethodParams() const = 0;
    virtual const HIRPathParams* getHrbParams() const = 0;

    HIRTypeRef getType(const Span& sp, const HIRGenericRef& ty) const override;
    HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& val) const override;
};

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

bool visitTyWithCb(const HIRTypeData*, HIRTypeVisitorCallback& callback);
bool visitTraitPathTysWithCb(const HIRTraitPath&, HIRTypeVisitorCallback& callback);
bool visitPathTysWithCb(const HIRPath&, HIRTypeVisitorCallback& callback);

bool typeContainsGenericGroup(const HIRTypeData*, HIRGenericGroup group);
bool pathParamsContainGenericGroup(const HIRPathParams&, HIRGenericGroup group);

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

bool rewriteTyWithCb(HIRTypeInterner& types, HIRTypeRef& ty, HIRTypeRewriteCallback& callback);
bool rewritePathTysWithCb(HIRTypeInterner& types, HIRPath& path, HIRTypeRewriteCallback& callback);

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

HIRTypeRef cloneTyWithCb(HIRTypeInterner& types, const Span& sp, const HIRTypeData* tpl, HIRTypeCloneCallback& callback);
HIRPathParams clonePathParamsWithCb(HIRTypeInterner& types, const Span& sp, const HIRPathParams& tpl, HIRTypeCloneCallback& callback);

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

void checkTypeClassPrimitive(const Span& sp, const HIRTypeData* type, HIRInferClass ic, HIRCoreType ct);

bool typeClassPrimitiveCompatible(HIRInferClass ic, HIRCoreType ct);

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
