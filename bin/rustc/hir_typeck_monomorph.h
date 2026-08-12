#pragma once

#include "hir_type.h"
#include "hir_item_path.h"
#include "hir_generic_params.h"

extern bool monomorphise_pathparams_needed(const ::HIR::PathParams& tpl, bool ignore_lifetimes = false);

static inline bool monomorphise_genericpath_needed(const ::HIR::GenericPath& tpl, bool ignore_lifetimes = false) {
    return monomorphise_pathparams_needed(tpl.m_params, ignore_lifetimes);
}

extern bool monomorphise_path_needed(const ::HIR::Path& tpl, bool ignore_lifetimes = false);
extern bool monomorphise_traitpath_needed(const ::HIR::TraitPath& tpl, bool ignore_lifetimes = false);
extern bool monomorphise_type_needed(const ::HIR::TypeData* tpl, bool ignore_lifetimes = false);

class Monomorphiser: virtual public HIR::TrackHrbStack {
protected:
    HIR::TypeInterner& m_types;

private:
    const HIR::Crate* consteval_crate;
    HIR::ItemPath consteval_path;

public:
    explicit Monomorphiser(HIR::TypeInterner& types);

    virtual ~Monomorphiser() = default;

    HIR::TypeInterner& type_interner() const { return m_types; }

    void set_consteval_state(const HIR::Crate& crate, HIR::ItemPath ip);

    virtual ::HIR::TypeRef get_type(const Span& sp, const ::HIR::GenericRef& g) const = 0;
    virtual ::HIR::ConstGeneric get_value(const Span& sp, const ::HIR::GenericRef& g) const = 0;
    virtual ::HIR::LifetimeRef get_lifetime(const Span& sp, const ::HIR::GenericRef& g) const = 0;

    virtual ::HIR::TypeRef monomorph_type(const Span& sp, const ::HIR::TypeData* ty, bool allow_infer = true) const;
    virtual ::HIR::LifetimeRef monomorph_lifetime(const Span& sp, const ::HIR::LifetimeRef& tpl) const;
    ::HIR::Path monomorph_path(const Span& sp, const ::HIR::Path& tpl, bool allow_infer = true) const;
    ::HIR::TraitPath monomorph_traitpath(const Span& sp, const ::HIR::TraitPath& tpl, bool allow_infer, bool ignore_hrls = false) const;
    ::HIR::TraitPath::AtyEqual monomorph_tp_aty_equal(const Span& sp, const ::HIR::TraitPath::AtyEqual& tpl, bool allow_infer) const;
    ::HIR::PathParams monomorph_path_params(const Span& sp, const ::HIR::PathParams& tpl, bool allow_infer) const;
    virtual ::HIR::GenericPath monomorph_genericpath(const Span& sp, const ::HIR::GenericPath& tpl, bool allow_infer = true, bool ignore_hrls = false) const;

    ::HIR::ConstGeneric monomorph_constgeneric(const Span& sp, const ::HIR::ConstGeneric& val, bool allow_infer) const;
    ::HIR::ArraySize monomorph_arraysize(const Span& sp, const ::HIR::ArraySize& tpl) const;

    const ::HIR::TypeData* maybe_monomorph_type(const Span& sp, ::HIR::TypeRef& tmp, const ::HIR::TypeData* ty, bool allow_infer = true) const;
};

class MonomorphiserPP: public Monomorphiser {
public:
    explicit MonomorphiserPP(HIR::TypeInterner& types);

    virtual const ::HIR::TypeData* get_self_type() const = 0;
    virtual const ::HIR::PathParams* get_impl_params() const = 0;
    virtual const ::HIR::PathParams* get_method_params() const = 0;
    virtual const ::HIR::PathParams* get_hrb_params() const = 0;

    ::HIR::TypeRef get_type(const Span& sp, const ::HIR::GenericRef& ty) const override;
    ::HIR::ConstGeneric get_value(const Span& sp, const ::HIR::GenericRef& val) const override;
    ::HIR::LifetimeRef get_lifetime(const Span& sp, const ::HIR::GenericRef& lft_ref) const override;
};

class MonomorphiserNop: public Monomorphiser {
public:
    using Monomorphiser::Monomorphiser;

    ::HIR::TypeRef get_type(const Span& sp, const ::HIR::GenericRef& ty) const override {
        return m_types.generic(ty.name, ty.binding);
    }

    ::HIR::ConstGeneric get_value(const Span& sp, const ::HIR::GenericRef& val) const override {
        return HIR::ConstGeneric(val);
    }

    ::HIR::LifetimeRef get_lifetime(const Span& sp, const ::HIR::GenericRef& lft_ref) const override {
        return ::HIR::LifetimeRef(lft_ref.binding);
    }
};

// Wrappers to only monomorphise if required
static inline const ::HIR::TypeData* monomorphise_type_with_opt(const Span& sp, ::HIR::TypeRef& tmp, const ::HIR::TypeData* tpl, const Monomorphiser& mono, bool allow_infer = true) {
    return (monomorphise_type_needed(tpl) ? tmp = mono.monomorph_type(sp, tpl, allow_infer) : tpl);
}

static inline const ::HIR::Path& monomorphise_path_with_opt(const Span& sp, ::HIR::Path& tmp, const ::HIR::Path& tpl, const Monomorphiser& mono, bool allow_infer = true) {
    return (monomorphise_path_needed(tpl) ? tmp = mono.monomorph_path(sp, tpl, allow_infer) : tpl);
}

static inline const ::HIR::GenericPath& monomorphise_genericpath_with_opt(const Span& sp, ::HIR::GenericPath& tmp, const ::HIR::GenericPath& tpl, const Monomorphiser& mono, bool allow_infer = true) {
    return (monomorphise_genericpath_needed(tpl) ? tmp = mono.monomorph_genericpath(sp, tpl, allow_infer, false) : tpl);
}

static inline const ::HIR::TraitPath& monomorphise_traitpath_with_opt(const Span& sp, ::HIR::TraitPath& tmp, const ::HIR::TraitPath& tpl, const Monomorphiser& mono, bool allow_infer = true) {
    return (monomorphise_traitpath_needed(tpl) ? tmp = mono.monomorph_traitpath(sp, tpl, allow_infer, false) : tpl);
}

static inline const ::HIR::PathParams& monomorphise_pathparams_with_opt(const Span& sp, ::HIR::PathParams& tmp, const ::HIR::PathParams& tpl, const Monomorphiser& mono, bool allow_infer = true) {
    return (monomorphise_pathparams_needed(tpl) ? tmp = mono.monomorph_path_params(sp, tpl, allow_infer) : tpl);
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

    const ::HIR::TypeData* get_self_type() const override {
        return self_ty;
    }

    const ::HIR::PathParams* get_impl_params() const override {
        return pp_impl;
    }

    const ::HIR::PathParams* get_method_params() const override {
        return pp_method;
    }

    const ::HIR::PathParams* get_hrb_params() const override {
        return pp_hrb;
    }
};

//extern ::std::ostream& operator<<(::std::ostream& os, const MonomorphStatePtr& ms);

struct MonomorphHrlsOnly: public Monomorphiser {
    const ::HIR::PathParams* pp_hrb;

    MonomorphHrlsOnly(HIR::TypeInterner& types, const ::HIR::PathParams& params_h);

    ::HIR::TypeRef get_type(const Span& sp, const ::HIR::GenericRef& ty) const override {
        if (ty.group() == 3) {
            ASSERT_BUG(sp, ty.idx() < pp_hrb->m_types.size(), ty << " out of bounds (" << pp_hrb->m_types.size() << ")");
            return pp_hrb->m_types.at(ty.idx());
        }
        return m_types.generic(ty.name, ty.binding);
    }

    ::HIR::ConstGeneric get_value(const Span& sp, const ::HIR::GenericRef& val) const override {
        if (val.group() == 3) {
            ASSERT_BUG(sp, val.idx() < pp_hrb->m_values.size(), val << " out of bounds (" << pp_hrb->m_values.size() << ")");
            return pp_hrb->m_values.at(val.idx()).clone();
        }
        return HIR::ConstGeneric(val);
    }

    ::HIR::LifetimeRef get_lifetime(const Span& sp, const ::HIR::GenericRef& lft_ref) const override {
        if (lft_ref.group() == 3) {
            // If the HRL batch does not cover this index, pass the lifetime through rather than abort: not reliably in range for nested binders, and erased before codegen.
            if (lft_ref.idx() >= pp_hrb->m_lifetimes.size()) {
                DEBUG("HRL " << lft_ref << " out of bounds (" << pp_hrb->m_lifetimes.size() << ") - passthrough");
                return ::HIR::LifetimeRef(lft_ref.binding);
            }
            return pp_hrb->m_lifetimes.at(lft_ref.idx());
        }
        return ::HIR::LifetimeRef(lft_ref.binding);
    }
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

    bool has_types() const {
        return (pp_method && pp_method->has_params()) || (pp_impl && pp_impl->has_params());
    }

    const ::HIR::TypeData* get_self_type() const override {
        return self_ty;
    }

    const ::HIR::PathParams* get_impl_params() const override {
        return pp_impl;
    }

    const ::HIR::PathParams* get_method_params() const override {
        return pp_method;
    }

    const ::HIR::PathParams* get_hrb_params() const override {
        return nullptr;
    }
};

extern ::std::ostream& operator<<(::std::ostream& os, const MonomorphState& ms);
