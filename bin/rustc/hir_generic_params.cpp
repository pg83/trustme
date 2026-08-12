#include "hir_generic_params.h"

#include "hir_type.h"

::std::ostream& operator<<(::std::ostream& os, const HIRGenericBound& x) {
    TU_MATCH(HIRGenericBound, (x), (e), (Lifetime, os << e.test << ": " << e.validFor;), (TypeLifetime, os << e.type << ": " << e.validFor;), (TraitBound, os << e.type << ": " << e.trait /*.m_path*/;), (TypeEquality, os << e.type << " = " << e.otherType;))
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const HIRGenericParams::PrintArgs& x) {
    if (x.gp.mLifetimes.size() > 0 || x.gp.types.size() > 0 || x.gp.values.size() > 0) {
        os << "<";
        for (const auto& lft : x.gp.mLifetimes) {
            os << "'" << lft.mName << ",";
        }
        for (const auto& typ : x.gp.types) {
            os << typ.mName;
            if (!typ.isSized) {
                os << ": ?Sized";
            }
            if (typ.defaultValue && !typ.defaultValue->is_Infer()) {
                os << " = " << typ.defaultValue;
            }
            os << ",";
        }
        if (!x.gp.values.empty()) {
            os << "const ";
        }
        for (const auto& valP : x.gp.values) {
            os << valP.mName;
            os << ": ";
            os << valP.mType;
            os << ",";
        }
        os << ">";
    }
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const HIRGenericParams::PrintBounds& x) {
    if (x.gp.bounds.size() > 0) {
        os << " where ";
        bool commaNeeded = false;
        for (const auto& b : x.gp.bounds) {
            if (commaNeeded) {
                os << ", ";
            }
            os << b;
            commaNeeded = true;
        }
    }
    return os;
}

Ordering HIRGenericBound::ord(const HIRGenericBound& b) const {
    if (this->tag() != b.tag()) {
        return this->tag() < b.tag() ? OrdLess : OrdGreater;
    }
    TU_MATCHA((*this, b), (ae, be), (Lifetime, auto cmp = ::ord(ae.test, be.test); if (cmp != OrdEqual) return cmp; cmp = ::ord(ae.validFor, be.validFor); if (cmp != OrdEqual) return cmp;), (TypeLifetime, auto cmp = ae.type->ordIgnoringRegions(be.type); if (cmp != OrdEqual) return cmp; cmp = ::ord(ae.validFor, be.validFor); if (cmp != OrdEqual) return cmp;), (TraitBound, auto cmp = ae.type->ordIgnoringRegions(be.type); if (cmp != OrdEqual) return cmp; cmp = ae.trait.ord(be.trait); if (cmp != OrdEqual) return cmp;), (TypeEquality, auto cmp = ae.type->ordIgnoringRegions(be.type); if (cmp != OrdEqual) return cmp; cmp = ae.otherType->ordIgnoringRegions(be.otherType); if (cmp != OrdEqual) return cmp;))
    return OrdEqual;
}

HIRPathParams HIRGenericParams::makeNopParams(HIRTypeInterner& types, unsigned level, bool lifetimesOnly /*=false*/) const {
    assert(!lifetimesOnly || this->types.empty());
    assert(!lifetimesOnly || this->values.empty());

    HIRPathParams rv;
    rv.mLifetimes = ThinVector<HIRLifetimeRef>(this->mLifetimes.size());
    rv.types = ThinVector<HIRTypeRef>(this->types.size());
    rv.values = ThinVector<HIRConstGeneric>(this->values.size());
    for (size_t i = 0; i < this->mLifetimes.size(); i++) {
        rv.mLifetimes[i] = HIRLifetimeRef(256 * level + i);
    }
    for (size_t i = 0; i < this->types.size(); i++) {
        rv.types[i] = types.generic(this->types[i].mName, 256 * level + i);
    }
    for (size_t i = 0; i < this->values.size(); i++) {
        rv.values[i] = HIRGenericRef(this->values[i].mName, 256 * level + i);
    }
    return rv;
}

HIRGenericParams HIRGenericParams::clone() const {
    HIRGenericParams rv;
    rv.types.reserve(types.size());
    for (const auto& type : types) {
        rv.types.push_back(HIRTypeParamDef{type.mName, type.defaultValue, type.isSized});
    }
    rv.values.reserve(values.size());
    for (const auto& type : values) {
        rv.values.push_back(HIRValueParamDef{type.mName, type.mType, type.defaultValue.clone()});
    }
    rv.mLifetimes = mLifetimes;
    rv.bounds.reserve(bounds.size());
    for (const auto& bound : bounds) {
        rv.bounds.push_back(bound.clone());
    }
    return rv;
}

HIRGenericBound HIRGenericBound::clone() const {
    TU_MATCH_HDRA( (*this), {)
    TU_ARMA(Lifetime, e) {
            return HIRGenericBound::make_Lifetime(e);
        }
        TU_ARMA(TypeLifetime, e) {
            return HIRGenericBound::make_TypeLifetime({e.type, e.validFor});
        }
        TU_ARMA(TraitBound, e) {
            return HIRGenericBound::make_TraitBound({e.hrtbs ? box$(e.hrtbs->clone()) : nullptr, e.type, e.trait.clone(), e.constness});
        } /*
    TU_ARMA(NotTrait, e) {
        return ::HIR::GenericBound::make_NotTrait({
            e.type.clone(),
            e.trait.clone()
            });
        }*/
        TU_ARMA(TypeEquality, e) {
            return HIRGenericBound::make_TypeEquality({e.type, e.otherType});
        }
    }
    throw "Unreachable";
}

Ordering HIRTypeParamDef::ord(const HIRTypeParamDef& x) const {
    ORD(mName, x.mName);
    ORD(defaultValue, x.defaultValue);
    ORD(isSized, x.isSized);
    return OrdEqual;
}

Ordering HIRLifetimeDef::ord(const HIRLifetimeDef& x) const {
    ORD(mName, x.mName);
    return OrdEqual;
}

Ordering HIRValueParamDef::ord(const HIRValueParamDef& x) const {
    ORD(mName, x.mName);
    ORD(mType, x.mType);
    //ORD(m_default, x.m_default);
    return OrdEqual;
}

bool HIRGenericParams::isEmpty() const {
    if (!types.empty()) {
        return false;
    }
    if (!mLifetimes.empty()) {
        return false;
    }
    if (!values.empty()) {
        return false;
    }
    if (!bounds.empty()) {
        return false;
    }
    return true;
}

bool HIRGenericParams::isGeneric() const {
    if (!types.empty()) {
        return true;
    }
    // Note: Lifetimes don't matter
    if (!values.empty()) {
        return true;
    }
    return false;
}

HIRPathParams HIRGenericParams::makeEmptyParams(bool lifetimesOnly) const {
    assert(lifetimesOnly);
    HIRPathParams rv;
    rv.mLifetimes = ThinVector<HIRLifetimeRef>(mLifetimes.size());
    return rv;
}

HIRGenericParams::PrintArgs::PrintArgs(const HIRGenericParams& gp)
    : gp(gp)
{
}

HIRGenericParams::PrintBounds::PrintBounds(const HIRGenericParams& gp)
    : gp(gp)
{
}

Ordering HIRGenericParams::ord(const HIRGenericParams& x) const {
    ORD(types, x.types);
    ORD(mLifetimes, x.mLifetimes);
    ORD(values, x.values);
    ORD(bounds, x.bounds);
    return OrdEqual;
}
