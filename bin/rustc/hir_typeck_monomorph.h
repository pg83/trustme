#pragma once

#include "hir_type.h"
#include "hir_item_path.h"
#include "hir_generic_params.h"

extern bool monomorphise_pathparams_needed(const ::HIR::PathParams& tpl, bool ignoreLifetimes = false);

static inline bool monomorphise_genericpath_needed(const ::HIR::GenericPath& tpl, bool ignoreLifetimes = false) {
    return monomorphise_pathparams_needed(tpl.mParams, ignoreLifetimes);
}

extern bool monomorphise_path_needed(const ::HIR::Path& tpl, bool ignoreLifetimes = false);
extern bool monomorphise_traitpath_needed(const ::HIR::TraitPath& tpl, bool ignoreLifetimes = false);
extern bool monomorphise_type_needed(const ::HIR::TypeData* tpl, bool ignoreLifetimes = false);

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

    void set_consteval_state(const HIR::Crate& crate, HIR::ItemPath ip);

    virtual ::HIR::TypeRef getType(const Span& sp, const ::HIR::GenericRef& g) const = 0;
    virtual ::HIR::ConstGeneric getValue(const Span& sp, const ::HIR::GenericRef& g) const = 0;
    virtual ::HIR::LifetimeRef getLifetime(const Span& sp, const ::HIR::GenericRef& g) const = 0;

    virtual ::HIR::TypeRef monomorph_type(const Span& sp, const ::HIR::TypeData* ty, bool allowInfer = true) const;
    virtual ::HIR::LifetimeRef monomorph_lifetime(const Span& sp, const ::HIR::LifetimeRef& tpl) const;
    ::HIR::Path monomorph_path(const Span& sp, const ::HIR::Path& tpl, bool allowInfer = true) const;
    ::HIR::TraitPath monomorph_traitpath(const Span& sp, const ::HIR::TraitPath& tpl, bool allowInfer, bool ignoreHrls = false) const;
    ::HIR::TraitPath::AtyEqual monomorph_tp_aty_equal(const Span& sp, const ::HIR::TraitPath::AtyEqual& tpl, bool allowInfer) const;
    ::HIR::PathParams monomorph_path_params(const Span& sp, const ::HIR::PathParams& tpl, bool allowInfer) const;
    virtual ::HIR::GenericPath monomorph_genericpath(const Span& sp, const ::HIR::GenericPath& tpl, bool allowInfer = true, bool ignoreHrls = false) const;

    ::HIR::ConstGeneric monomorph_constgeneric(const Span& sp, const ::HIR::ConstGeneric& val, bool allowInfer) const;
    ::HIR::ArraySize monomorph_arraysize(const Span& sp, const ::HIR::ArraySize& tpl) const;

    const ::HIR::TypeData* maybe_monomorph_type(const Span& sp, ::HIR::TypeRef& tmp, const ::HIR::TypeData* ty, bool allowInfer = true) const;
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
static inline const ::HIR::TypeData* monomorphise_type_with_opt(const Span& sp, ::HIR::TypeRef& tmp, const ::HIR::TypeData* tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphise_type_needed(tpl) ? tmp = mono.monomorph_type(sp, tpl, allowInfer) : tpl);
}

static inline const ::HIR::Path& monomorphise_path_with_opt(const Span& sp, ::HIR::Path& tmp, const ::HIR::Path& tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphise_path_needed(tpl) ? tmp = mono.monomorph_path(sp, tpl, allowInfer) : tpl);
}

static inline const ::HIR::GenericPath& monomorphise_genericpath_with_opt(const Span& sp, ::HIR::GenericPath& tmp, const ::HIR::GenericPath& tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphise_genericpath_needed(tpl) ? tmp = mono.monomorph_genericpath(sp, tpl, allowInfer, false) : tpl);
}

static inline const ::HIR::TraitPath& monomorphise_traitpath_with_opt(const Span& sp, ::HIR::TraitPath& tmp, const ::HIR::TraitPath& tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphise_traitpath_needed(tpl) ? tmp = mono.monomorph_traitpath(sp, tpl, allowInfer, false) : tpl);
}

static inline const ::HIR::PathParams& monomorphise_pathparams_with_opt(const Span& sp, ::HIR::PathParams& tmp, const ::HIR::PathParams& tpl, const Monomorphiser& mono, bool allowInfer = true) {
    return (monomorphise_pathparams_needed(tpl) ? tmp = mono.monomorph_path_params(sp, tpl, allowInfer) : tpl);
}

// Helper for passing a group of params around
struct MonomorphStatePtr: public MonomorphiserPP {
    const ::HIR::TypeData* self_ty;
    const ::HIR::PathParams* pp_impl;
    const ::HIR::PathParams* pp_method;
    //const ::HIR::PathParams*    pp_placeholder;
    const ::HIR::PathParams* pp_hrb;

    explicit MonomorphStatePtr(HIR::TypeInterner& types);

    MonomorphStatePtr(HIR::TypeInterner& types, const ::HIR::TypeData* self_ty, const ::HIR::PathParams* params_i, const ::HIR::PathParams* params_m, const ::HIR::PathParams* params_p = nullptr, const ::HIR::PathParams* params_h = nullptr);

    MonomorphStatePtr(MonomorphStatePtr&& x);

    MonomorphStatePtr(const MonomorphStatePtr& x);

    MonomorphStatePtr& operator=(MonomorphStatePtr&& x);

    const ::HIR::TypeData* getSelfType() const override {
        return self_ty;
    }

    const ::HIR::PathParams* getImplParams() const override {
        return pp_impl;
    }

    const ::HIR::PathParams* getMethodParams() const override {
        return pp_method;
    }

    const ::HIR::PathParams* getHrbParams() const override {
        return pp_hrb;
    }
};

//extern ::std::ostream& operator<<(::std::ostream& os, const MonomorphStatePtr& ms);

struct MonomorphHrlsOnly: public Monomorphiser {
    const ::HIR::PathParams* pp_hrb;

    MonomorphHrlsOnly(HIR::TypeInterner& types, const ::HIR::PathParams& params_h);

    ::HIR::TypeRef getType(const Span& sp, const ::HIR::GenericRef& ty) const override;

    ::HIR::ConstGeneric getValue(const Span& sp, const ::HIR::GenericRef& val) const override;

    ::HIR::LifetimeRef getLifetime(const Span& sp, const ::HIR::GenericRef& lftRef) const override;
};

// Helper for passing a group of params around
struct MonomorphState: public MonomorphiserPP {
    ::HIR::TypeRef self_ty;
    const ::HIR::PathParams* pp_impl;
    const ::HIR::PathParams* pp_method;

    ::HIR::PathParams pp_impl_data;

    explicit MonomorphState(HIR::TypeInterner& types);

    MonomorphState(MonomorphState&& x);

    MonomorphState& operator=(MonomorphState&& x);

    MonomorphState clone() const;

    void set_impl_params(HIR::PathParams pp);

    bool hasTypes() const {
        return (pp_method && pp_method->hasParams()) || (pp_impl && pp_impl->hasParams());
    }

    const ::HIR::TypeData* getSelfType() const override {
        return self_ty;
    }

    const ::HIR::PathParams* getImplParams() const override {
        return pp_impl;
    }

    const ::HIR::PathParams* getMethodParams() const override {
        return pp_method;
    }

    const ::HIR::PathParams* getHrbParams() const override {
        return nullptr;
    }
};

extern ::std::ostream& operator<<(::std::ostream& os, const MonomorphState& ms);
