#pragma once

#include "hir_type.h"
#include "hir_hir.h"
#include "hir_typeck_monomorph.h"

namespace HIR {
    class TraitImpl;
}

struct ImplRef {
    TAGGED_UNION(
        Data,
        TraitImpl,
        (TraitImpl,
         struct {
             HIR::PathParams impl_params;
             const ::HIR::Trait* trait_ptr;
             const ::HIR::SimplePath* trait_path;
             const ::HIR::TraitImpl* impl;
             mutable ::HIR::TypeRef self_cache;
         }),
        (BoundedPtr,
         struct {
             ::HIR::PathParams hrls;
             const ::HIR::TypeData* type;
             const ::HIR::PathParams* trait_args;
             const ::HIR::TraitPath::assocListT* assoc;
             ::HIR::BoundConstness constness;
         }),
        (Bounded, struct {
            ::HIR::PathParams hrls;
            ::HIR::TypeRef type;
            ::HIR::PathParams trait_args;
            ::HIR::TraitPath::assocListT assoc;
            ::HIR::BoundConstness constness;
        })
    );

    Data mData;
    bool isAmbiguousIdentity = false;

    ImplRef();

    ImplRef(HIR::PathParams impl_params, const HIR::Trait& trait_ref, const ::HIR::SimplePath& trait, const ::HIR::TraitImpl& impl);

    ImplRef(const ::HIR::TypeData* type, const ::HIR::PathParams* args, const ::HIR::TraitPath::assocListT* assoc, ::HIR::BoundConstness constness = ::HIR::BoundConstness::Never);

    ImplRef(::HIR::PathParams hrls, const ::HIR::TypeData* type, const ::HIR::PathParams* args, const ::HIR::TraitPath::assocListT* assoc, ::HIR::BoundConstness constness = ::HIR::BoundConstness::Never);

    ImplRef(::HIR::TypeRef type, ::HIR::PathParams args, ::HIR::TraitPath::assocListT assoc, ::HIR::BoundConstness constness = ::HIR::BoundConstness::Never);

    ImplRef(::HIR::PathParams hrls, ::HIR::TypeRef type, ::HIR::PathParams args, ::HIR::TraitPath::assocListT assoc, ::HIR::BoundConstness constness = ::HIR::BoundConstness::Never);

    bool is_valid() const {
        return !(mData.is_TraitImpl() && mData.as_TraitImpl().impl == nullptr);
    }

    bool is_ambiguous_identity() const {
        return isAmbiguousIdentity;
    }

    void mark_ambiguous_identity() {
        isAmbiguousIdentity = true;
    }

    ::HIR::BoundConstness boundConstness() const;

    bool more_specific_than(HIR::TypeInterner& types, const ImplRef& other) const;
    bool overlaps_with(const ::HIR::Crate& crate, const ImplRef& other) const;

    bool hasMagicParams() const;

    /// HELPER: Returns callback to monomorphise a type using parameters from Data::TraitImpl
    class Monomorph: public Monomorphiser {
        friend struct ImplRef;
        const ImplRef::Data::Data_TraitImpl& ti;
        const ::HIR::PathParams& params;

        Monomorph(HIR::TypeInterner& types, const ImplRef::Data::Data_TraitImpl& ti, const ::HIR::PathParams& params);

        ::HIR::TypeRef getType(const Span& sp, const ::HIR::GenericRef& ty) const override;
        ::HIR::ConstGeneric getValue(const Span& sp, const ::HIR::GenericRef& val) const override;
        ::HIR::LifetimeRef getLifetime(const Span& sp, const ::HIR::GenericRef& val) const override;
    };

    Monomorph getCbMonomorphTraitimpl(HIR::TypeInterner& types, const Span& sp, const ::HIR::PathParams& params) const;

    ::HIR::TypeRef getImplType(HIR::TypeInterner& types) const;
    ::HIR::PathParams getTraitParams(HIR::TypeInterner& types) const;

    ::HIR::TypeRef getTraitTyParam(HIR::TypeInterner& types, unsigned int) const;

    bool type_is_specialisable(const char* name) const;
    ::HIR::TypeRef getType(HIR::TypeInterner& types, const char* name, const HIR::PathParams& params) const;

    friend ::std::ostream& operator<<(::std::ostream& os, const ImplRef& x);
};
