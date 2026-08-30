#pragma once

#include "hir_type.h"
#include "hir_item_path.h"
#include "hir_typeck_common.h"
#include "hir_generic_params.h"

static inline bool monomorphiseGenericpathNeeded(const HIRGenericPath& tpl) {
    return monomorphisePathparamsNeeded(tpl.params);
}

class MonomorphiserNop: public Monomorphiser {
public:
    using Monomorphiser::Monomorphiser;

    const HIRType* getType(const Span& sp, const HIRGenericRef& ty) const override;

    HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& val) const override;
};

class OpaqueAliasParamMonomorph: public MonomorphiserNop {
    const HIRTypeDataErasedTypeAliasInner& alias;
    const HIRPathParams& params;

public:
    OpaqueAliasParamMonomorph(HIRTypeInterner& types, const HIRTypeDataErasedTypeAliasInner& alias, const HIRPathParams& params);

    const HIRType* getType(const Span& sp, const HIRGenericRef& generic) const override;
    HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& generic) const override;
};

static inline const HIRType* monomorphiseTypeWithOpt(const Span& sp, const HIRType* tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return monomorphiseTypeNeeded(tpl) ? mono.monomorphType(sp, tpl, allowInfer) : tpl;
}

static inline const HIRPath& monomorphisePathWithOpt(const Span& sp, HIRPath& tmp, const HIRPath& tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphisePathNeeded(tpl) ? tmp = mono.monomorphPath(sp, tpl, allowInfer) : tpl);
}

static inline const HIRGenericPath& monomorphiseGenericpathWithOpt(const Span& sp, HIRGenericPath& tmp, const HIRGenericPath& tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphiseGenericpathNeeded(tpl) ? tmp = mono.monomorphGenericpath(sp, tpl, allowInfer) : tpl);
}

static inline const HIRTraitPath& monomorphiseTraitpathWithOpt(const Span& sp, HIRTraitPath& tmp, const HIRTraitPath& tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphiseTraitpathNeeded(tpl) ? tmp = mono.monomorphTraitpath(sp, tpl, allowInfer) : tpl);
}

static inline const HIRPathParams& monomorphisePathparamsWithOpt(const Span& sp, HIRPathParams& tmp, const HIRPathParams& tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphisePathparamsNeeded(tpl) ? tmp = mono.monomorphPathParams(sp, tpl, allowInfer) : tpl);
}

struct MonomorphStatePtr: public MonomorphiserPP {
    const HIRType* selfTy;
    const HIRPathParams* ppImpl;
    const HIRPathParams* ppMethod;

    const HIRPathParams* ppHrb;

    explicit MonomorphStatePtr(HIRTypeInterner& types);

    MonomorphStatePtr(HIRTypeInterner& types, const HIRType* selfTy, const HIRPathParams* paramsI, const HIRPathParams* paramsM, const HIRPathParams* paramsP = nullptr, const HIRPathParams* paramsH = nullptr);

    MonomorphStatePtr(MonomorphStatePtr&& x);

    MonomorphStatePtr(const MonomorphStatePtr& x);

    MonomorphStatePtr& operator=(MonomorphStatePtr&& x);

    const HIRType* getSelfType() const override;

    const HIRPathParams* getImplParams() const override;

    const HIRPathParams* getMethodParams() const override;

    const HIRPathParams* getHrbParams() const override;
};

struct MonomorphState: public MonomorphiserPP {
    const HIRType* selfTy;
    const HIRPathParams* ppImpl;
    const HIRPathParams* ppMethod;

    HIRPathParams ppImplData;

    explicit MonomorphState(HIRTypeInterner& types);

    MonomorphState(MonomorphState&& x);

    MonomorphState& operator=(MonomorphState&& x);

    MonomorphState clone() const;

    void setImplParams(HIRPathParams pp);

    bool hasTypes() const {
        return (ppMethod && ppMethod->hasParams()) || (ppImpl && ppImpl->hasParams());
    }

    const HIRType* getSelfType() const override;

    const HIRPathParams* getImplParams() const override;

    const HIRPathParams* getMethodParams() const override;

    const HIRPathParams* getHrbParams() const override;
};
