#pragma once

#include "hir_type.h"
#include "hir_item_path.h"
#include "hir_generic_params.h"

extern bool monomorphisePathparamsNeeded(const ::HIR::PathParams& tpl, bool ignoreLifetimes = false);

static inline bool monomorphiseGenericpathNeeded(const ::HIR::GenericPath& tpl, bool ignoreLifetimes = false) {
    return monomorphisePathparamsNeeded(tpl.mParams, ignoreLifetimes);
}

extern bool monomorphisePathNeeded(const ::HIR::Path& tpl, bool ignoreLifetimes = false);
extern bool monomorphiseTraitpathNeeded(const ::HIR::TraitPath& tpl, bool ignoreLifetimes = false);
extern bool monomorphiseTypeNeeded(const ::HIR::TypeData* tpl, bool ignoreLifetimes = false);

class Monomorphiser: virtual public HIR::TrackHrbStack {
protected:
    HIR::TypeInterner& types;

private:
    const HIR::Crate* constevalCrate;
    HIR::ItemPath constevalPath;

public:
    explicit Monomorphiser(HIR::TypeInterner& types);

    virtual ~Monomorphiser() = default;

    HIR::TypeInterner& type_interner() const { return types; }

    void setConstevalState(const HIR::Crate& crate, HIR::ItemPath ip);

    virtual ::HIR::TypeRef getType(const Span& sp, const ::HIR::GenericRef& g) const = 0;
    virtual ::HIR::ConstGeneric getValue(const Span& sp, const ::HIR::GenericRef& g) const = 0;
    virtual ::HIR::LifetimeRef getLifetime(const Span& sp, const ::HIR::GenericRef& g) const = 0;

    virtual ::HIR::TypeRef monomorphType(const Span& sp, const ::HIR::TypeData* ty, bool allowInfer = true) const;
    virtual ::HIR::LifetimeRef monomorphLifetime(const Span& sp, const ::HIR::LifetimeRef& tpl) const;
    ::HIR::Path monomorphPath(const Span& sp, const ::HIR::Path& tpl, bool allowInfer = true) const;
    ::HIR::TraitPath monomorphTraitpath(const Span& sp, const ::HIR::TraitPath& tpl, bool allowInfer, bool ignoreHrls = false) const;
    ::HIR::TraitPath::AtyEqual monomorphTpAtyEqual(const Span& sp, const ::HIR::TraitPath::AtyEqual& tpl, bool allowInfer) const;
    ::HIR::PathParams monomorphPathParams(const Span& sp, const ::HIR::PathParams& tpl, bool allowInfer) const;
    virtual ::HIR::GenericPath monomorphGenericpath(const Span& sp, const ::HIR::GenericPath& tpl, bool allowInfer = true, bool ignoreHrls = false) const;

    ::HIR::ConstGeneric monomorphConstgeneric(const Span& sp, const ::HIR::ConstGeneric& val, bool allowInfer) const;
    ::HIR::ArraySize monomorphArraysize(const Span& sp, const ::HIR::ArraySize& tpl) const;

    const ::HIR::TypeData* maybeMonomorphType(const Span& sp, ::HIR::TypeRef& tmp, const ::HIR::TypeData* ty, bool allowInfer = true) const;
};

class MonomorphiserPP: public Monomorphiser {
public:
    explicit MonomorphiserPP(HIR::TypeInterner& types);

    virtual const ::HIR::TypeData* getSelfType() const = 0;
    virtual const ::HIR::PathParams* getImplParams() const = 0;
    virtual const ::HIR::PathParams* getMethodParams() const = 0;
    virtual const ::HIR::PathParams* getHrbParams() const = 0;

    ::HIR::TypeRef getType(const Span& sp, const ::HIR::GenericRef& ty) const override;
    ::HIR::ConstGeneric getValue(const Span& sp, const ::HIR::GenericRef& val) const override;
    ::HIR::LifetimeRef getLifetime(const Span& sp, const ::HIR::GenericRef& lftRef) const override;
};

class MonomorphiserNop: public Monomorphiser {
public:
    using Monomorphiser::Monomorphiser;

    ::HIR::TypeRef getType(const Span& sp, const ::HIR::GenericRef& ty) const override {
        return types.generic(ty.name, ty.binding);
    }

    ::HIR::ConstGeneric getValue(const Span& sp, const ::HIR::GenericRef& val) const override {
        return HIR::ConstGeneric(val);
    }

    ::HIR::LifetimeRef getLifetime(const Span& sp, const ::HIR::GenericRef& lftRef) const override {
        return ::HIR::LifetimeRef(lftRef.binding);
    }
};

// Wrappers to only monomorphise if required
static inline const ::HIR::TypeData* monomorphiseTypeWithOpt(const Span& sp, ::HIR::TypeRef& tmp, const ::HIR::TypeData* tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphiseTypeNeeded(tpl) ? tmp = mono.monomorphType(sp, tpl, allowInfer) : tpl);
}

static inline const ::HIR::Path& monomorphisePathWithOpt(const Span& sp, ::HIR::Path& tmp, const ::HIR::Path& tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphisePathNeeded(tpl) ? tmp = mono.monomorphPath(sp, tpl, allowInfer) : tpl);
}

static inline const ::HIR::GenericPath& monomorphiseGenericpathWithOpt(const Span& sp, ::HIR::GenericPath& tmp, const ::HIR::GenericPath& tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphiseGenericpathNeeded(tpl) ? tmp = mono.monomorphGenericpath(sp, tpl, allowInfer, false) : tpl);
}

static inline const ::HIR::TraitPath& monomorphiseTraitpathWithOpt(const Span& sp, ::HIR::TraitPath& tmp, const ::HIR::TraitPath& tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphiseTraitpathNeeded(tpl) ? tmp = mono.monomorphTraitpath(sp, tpl, allowInfer, false) : tpl);
}

static inline const ::HIR::PathParams& monomorphisePathparamsWithOpt(const Span& sp, ::HIR::PathParams& tmp, const ::HIR::PathParams& tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphisePathparamsNeeded(tpl) ? tmp = mono.monomorphPathParams(sp, tpl, allowInfer) : tpl);
}

// Helper for passing a group of params around
struct MonomorphStatePtr: public MonomorphiserPP {
    const ::HIR::TypeData* self_ty;
    const ::HIR::PathParams* ppImpl;
    const ::HIR::PathParams* ppMethod;
    //const ::HIR::PathParams*    pp_placeholder;
    const ::HIR::PathParams* ppHrb;

    explicit MonomorphStatePtr(HIR::TypeInterner& types);

    MonomorphStatePtr(HIR::TypeInterner& types, const ::HIR::TypeData* self_ty, const ::HIR::PathParams* paramsI, const ::HIR::PathParams* paramsM, const ::HIR::PathParams* paramsP = nullptr, const ::HIR::PathParams* paramsH = nullptr);

    MonomorphStatePtr(MonomorphStatePtr&& x);

    MonomorphStatePtr(const MonomorphStatePtr& x);

    MonomorphStatePtr& operator=(MonomorphStatePtr&& x);

    const ::HIR::TypeData* getSelfType() const override {
        return self_ty;
    }

    const ::HIR::PathParams* getImplParams() const override {
        return ppImpl;
    }

    const ::HIR::PathParams* getMethodParams() const override {
        return ppMethod;
    }

    const ::HIR::PathParams* getHrbParams() const override {
        return ppHrb;
    }
};

//extern ::std::ostream& operator<<(::std::ostream& os, const MonomorphStatePtr& ms);

struct MonomorphHrlsOnly: public Monomorphiser {
    const ::HIR::PathParams* ppHrb;

    MonomorphHrlsOnly(HIR::TypeInterner& types, const ::HIR::PathParams& paramsH);

    ::HIR::TypeRef getType(const Span& sp, const ::HIR::GenericRef& ty) const override;

    ::HIR::ConstGeneric getValue(const Span& sp, const ::HIR::GenericRef& val) const override;

    ::HIR::LifetimeRef getLifetime(const Span& sp, const ::HIR::GenericRef& lftRef) const override;
};

// Helper for passing a group of params around
struct MonomorphState: public MonomorphiserPP {
    ::HIR::TypeRef self_ty;
    const ::HIR::PathParams* ppImpl;
    const ::HIR::PathParams* ppMethod;

    ::HIR::PathParams ppImplData;

    explicit MonomorphState(HIR::TypeInterner& types);

    MonomorphState(MonomorphState&& x);

    MonomorphState& operator=(MonomorphState&& x);

    MonomorphState clone() const;

    void setImplParams(HIR::PathParams pp);

    bool hasTypes() const {
        return (ppMethod && ppMethod->hasParams()) || (ppImpl && ppImpl->hasParams());
    }

    const ::HIR::TypeData* getSelfType() const override {
        return self_ty;
    }

    const ::HIR::PathParams* getImplParams() const override {
        return ppImpl;
    }

    const ::HIR::PathParams* getMethodParams() const override {
        return ppMethod;
    }

    const ::HIR::PathParams* getHrbParams() const override {
        return nullptr;
    }
};

extern ::std::ostream& operator<<(::std::ostream& os, const MonomorphState& ms);
