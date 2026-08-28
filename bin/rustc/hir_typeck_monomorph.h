#pragma once

#include "hir_type.h"
#include "hir_item_path.h"
#include "hir_generic_params.h"

bool monomorphisePathparamsNeeded(const HIRPathParams& tpl);

static inline bool monomorphiseGenericpathNeeded(const HIRGenericPath& tpl) {
    return monomorphisePathparamsNeeded(tpl.params);
}

bool monomorphisePathNeeded(const HIRPath& tpl);
struct WireBoard;

bool monomorphiseTraitpathNeeded(const HIRTraitPath& tpl);
bool monomorphiseTypeNeeded(const HIRTypeData* tpl);

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

class MonomorphiserNop: public Monomorphiser {
public:
    using Monomorphiser::Monomorphiser;

    HIRTypeRef getType(const Span& sp, const HIRGenericRef& ty) const override;

    HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& val) const override;
};

class OpaqueAliasParamMonomorph: public MonomorphiserNop {
    const HIRTypeDataErasedTypeAliasInner& alias;
    const HIRPathParams& params;

public:
    OpaqueAliasParamMonomorph(HIRTypeInterner& types, const HIRTypeDataErasedTypeAliasInner& alias, const HIRPathParams& params);

    HIRTypeRef getType(const Span& sp, const HIRGenericRef& generic) const override;
    HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& generic) const override;
};

static inline const HIRTypeData* monomorphiseTypeWithOpt(const Span& sp, HIRTypeRef& tmp, const HIRTypeData* tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphiseTypeNeeded(tpl) ? tmp = mono.monomorphType(sp, tpl, allowInfer) : tpl);
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
    const HIRTypeData* selfTy;
    const HIRPathParams* ppImpl;
    const HIRPathParams* ppMethod;

    const HIRPathParams* ppHrb;

    explicit MonomorphStatePtr(HIRTypeInterner& types);

    MonomorphStatePtr(HIRTypeInterner& types, const HIRTypeData* selfTy, const HIRPathParams* paramsI, const HIRPathParams* paramsM, const HIRPathParams* paramsP = nullptr, const HIRPathParams* paramsH = nullptr);

    MonomorphStatePtr(MonomorphStatePtr&& x);

    MonomorphStatePtr(const MonomorphStatePtr& x);

    MonomorphStatePtr& operator=(MonomorphStatePtr&& x);

    const HIRTypeData* getSelfType() const override;

    const HIRPathParams* getImplParams() const override;

    const HIRPathParams* getMethodParams() const override;

    const HIRPathParams* getHrbParams() const override;
};

struct MonomorphState: public MonomorphiserPP {
    HIRTypeRef selfTy;
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

    const HIRTypeData* getSelfType() const override;

    const HIRPathParams* getImplParams() const override;

    const HIRPathParams* getMethodParams() const override;

    const HIRPathParams* getHrbParams() const override;
};

std::ostream& operator<<(std::ostream& os, const MonomorphState& ms);
