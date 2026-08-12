#pragma once

#include "hir_type.h"
#include "hir_item_path.h"
#include "hir_generic_params.h"

extern bool monomorphisePathparamsNeeded(const HIRPathParams& tpl, bool ignoreLifetimes = false);

static inline bool monomorphiseGenericpathNeeded(const HIRGenericPath& tpl, bool ignoreLifetimes = false) {
    return monomorphisePathparamsNeeded(tpl.mParams, ignoreLifetimes);
}

extern bool monomorphisePathNeeded(const HIRPath& tpl, bool ignoreLifetimes = false);
extern bool monomorphiseTraitpathNeeded(const HIRTraitPath& tpl, bool ignoreLifetimes = false);
extern bool monomorphiseTypeNeeded(const HIRTypeData* tpl, bool ignoreLifetimes = false);

class Monomorphiser: virtual public HIRTrackHrbStack {
protected:
    HIRTypeInterner& types;

private:
    const HIRCrate* constevalCrate;
    HIRItemPath constevalPath;

public:
    explicit Monomorphiser(HIRTypeInterner& types);

    virtual ~Monomorphiser() = default;

    HIRTypeInterner& typeInterner() const {
        return types;
    }

    void setConstevalState(const HIRCrate& crate, HIRItemPath ip);

    virtual HIRTypeRef getType(const Span& sp, const HIRGenericRef& g) const = 0;
    virtual HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& g) const = 0;
    virtual HIRLifetimeRef getLifetime(const Span& sp, const HIRGenericRef& g) const = 0;

    virtual HIRTypeRef monomorphType(const Span& sp, const HIRTypeData* ty, bool allowInfer = true) const;
    virtual HIRLifetimeRef monomorphLifetime(const Span& sp, const HIRLifetimeRef& tpl) const;
    HIRPath monomorphPath(const Span& sp, const HIRPath& tpl, bool allowInfer = true) const;
    HIRTraitPath monomorphTraitpath(const Span& sp, const HIRTraitPath& tpl, bool allowInfer, bool ignoreHrls = false) const;
    HIRTraitPath::AtyEqual monomorphTpAtyEqual(const Span& sp, const HIRTraitPath::AtyEqual& tpl, bool allowInfer) const;
    HIRPathParams monomorphPathParams(const Span& sp, const HIRPathParams& tpl, bool allowInfer) const;
    virtual HIRGenericPath monomorphGenericpath(const Span& sp, const HIRGenericPath& tpl, bool allowInfer = true, bool ignoreHrls = false) const;

    HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& val, bool allowInfer) const;
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
    HIRLifetimeRef getLifetime(const Span& sp, const HIRGenericRef& lftRef) const override;
};

class MonomorphiserNop: public Monomorphiser {
public:
    using Monomorphiser::Monomorphiser;

    HIRTypeRef getType(const Span& sp, const HIRGenericRef& ty) const override {
        return types.generic(ty.name, ty.binding);
    }

    HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& val) const override {
        return HIRConstGeneric(val);
    }

    HIRLifetimeRef getLifetime(const Span& sp, const HIRGenericRef& lftRef) const override {
        return HIRLifetimeRef(lftRef.binding);
    }
};

// Wrappers to only monomorphise if required
static inline const HIRTypeData* monomorphiseTypeWithOpt(const Span& sp, HIRTypeRef& tmp, const HIRTypeData* tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphiseTypeNeeded(tpl) ? tmp = mono.monomorphType(sp, tpl, allowInfer) : tpl);
}

static inline const HIRPath& monomorphisePathWithOpt(const Span& sp, HIRPath& tmp, const HIRPath& tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphisePathNeeded(tpl) ? tmp = mono.monomorphPath(sp, tpl, allowInfer) : tpl);
}

static inline const HIRGenericPath& monomorphiseGenericpathWithOpt(const Span& sp, HIRGenericPath& tmp, const HIRGenericPath& tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphiseGenericpathNeeded(tpl) ? tmp = mono.monomorphGenericpath(sp, tpl, allowInfer, false) : tpl);
}

static inline const HIRTraitPath& monomorphiseTraitpathWithOpt(const Span& sp, HIRTraitPath& tmp, const HIRTraitPath& tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphiseTraitpathNeeded(tpl) ? tmp = mono.monomorphTraitpath(sp, tpl, allowInfer, false) : tpl);
}

static inline const HIRPathParams& monomorphisePathparamsWithOpt(const Span& sp, HIRPathParams& tmp, const HIRPathParams& tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphisePathparamsNeeded(tpl) ? tmp = mono.monomorphPathParams(sp, tpl, allowInfer) : tpl);
}

// Helper for passing a group of params around
struct MonomorphStatePtr: public MonomorphiserPP {
    const HIRTypeData* selfTy;
    const HIRPathParams* ppImpl;
    const HIRPathParams* ppMethod;
    //const ::HIR::PathParams*    pp_placeholder;
    const HIRPathParams* ppHrb;

    explicit MonomorphStatePtr(HIRTypeInterner& types);

    MonomorphStatePtr(HIRTypeInterner& types, const HIRTypeData* selfTy, const HIRPathParams* paramsI, const HIRPathParams* paramsM, const HIRPathParams* paramsP = nullptr, const HIRPathParams* paramsH = nullptr);

    MonomorphStatePtr(MonomorphStatePtr&& x);

    MonomorphStatePtr(const MonomorphStatePtr& x);

    MonomorphStatePtr& operator=(MonomorphStatePtr&& x);

    const HIRTypeData* getSelfType() const override {
        return selfTy;
    }

    const HIRPathParams* getImplParams() const override {
        return ppImpl;
    }

    const HIRPathParams* getMethodParams() const override {
        return ppMethod;
    }

    const HIRPathParams* getHrbParams() const override {
        return ppHrb;
    }
};

//extern ::std::ostream& operator<<(::std::ostream& os, const MonomorphStatePtr& ms);

struct MonomorphHrlsOnly: public Monomorphiser {
    const HIRPathParams* ppHrb;

    MonomorphHrlsOnly(HIRTypeInterner& types, const HIRPathParams& paramsH);

    HIRTypeRef getType(const Span& sp, const HIRGenericRef& ty) const override;

    HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& val) const override;

    HIRLifetimeRef getLifetime(const Span& sp, const HIRGenericRef& lftRef) const override;
};

// Helper for passing a group of params around
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

    const HIRTypeData* getSelfType() const override {
        return selfTy;
    }

    const HIRPathParams* getImplParams() const override {
        return ppImpl;
    }

    const HIRPathParams* getMethodParams() const override {
        return ppMethod;
    }

    const HIRPathParams* getHrbParams() const override {
        return nullptr;
    }
};

extern ::std::ostream& operator<<(::std::ostream& os, const MonomorphState& ms);
