#pragma once

#include "hir_hir.h"
#include "hir_type.h"
#include "hir_typeck_monomorph.h"

class HIRTraitImpl;

#include "hir_typeck_impl_ref_tu.h"

struct ImplRef {
    using Data = ImplRefData;

    Data data;
    bool isAmbiguousIdentity_ = false;

    ImplRef();

    ImplRef(HIRPathParams implParams, const HIRTrait& traitRef, const HIRSimplePath& trait, const HIRTraitImpl& impl);

    ImplRef(const HIRTypeData* type, const HIRPathParams* args, const HIRTraitPath::assocListT* assoc, HIRBoundConstness constness = HIRBoundConstness::Never);

    ImplRef(HIRTypeRef type, HIRPathParams args, HIRTraitPath::assocListT assoc, HIRBoundConstness constness = HIRBoundConstness::Never);

    bool isValid() const {
        return !(data.is_TraitImpl() && data.as_TraitImpl().impl == nullptr);
    }

    bool isAmbiguousIdentity() const {
        return isAmbiguousIdentity_;
    }

    void markAmbiguousIdentity() {
        isAmbiguousIdentity_ = true;
    }

    HIRBoundConstness boundConstness() const;

    bool moreSpecificThan(HIRTypeInterner& types, const ImplRef& other) const;
    bool overlapsWith(const HIRCrate& crate, const ImplRef& other) const;

    bool hasMagicParams() const;

    class Monomorph: public Monomorphiser {
        friend struct ImplRef;
        const ImplRef::Data::Data_TraitImpl& ti;
        const HIRPathParams& params;

        Monomorph(HIRTypeInterner& types, const ImplRef::Data::Data_TraitImpl& ti, const HIRPathParams& params);

        HIRTypeRef getType(const Span& sp, const HIRGenericRef& ty) const override;
        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& val) const override;
    };

    Monomorph getCbMonomorphTraitimpl(HIRTypeInterner& types, const Span& sp, const HIRPathParams& params) const;

    HIRTypeRef getImplType(HIRTypeInterner& types) const;
    HIRPathParams getTraitParams(HIRTypeInterner& types) const;

    HIRTypeRef getTraitTyParam(HIRTypeInterner& types, unsigned int) const;

    bool typeIsSpecialisable(const char* name) const;
    HIRTypeRef getType(HIRTypeInterner& types, const char* name, const HIRPathParams& params) const;

    friend ::std::ostream& operator<<(::std::ostream& os, const ImplRef& x);
};
