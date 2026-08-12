#include "hir_generic_params.h"
#include "hir_type.h"

namespace HIR {
    ::std::ostream& operator<<(::std::ostream& os, const GenericBound& x) {
        TU_MATCH(::HIR::GenericBound, (x), (e), (Lifetime, os << e.test << ": " << e.valid_for;), (TypeLifetime, os << e.type << ": " << e.valid_for;), (TraitBound, os << e.type << ": " << e.trait /*.m_path*/;), (TypeEquality, os << e.type << " = " << e.other_type;))
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const ::HIR::GenericParams::PrintArgs& x) {
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
            for (const auto& val_p : x.gp.values) {
                os << val_p.mName;
                os << ": ";
                os << val_p.mType;
                os << ",";
            }
            os << ">";
        }
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const ::HIR::GenericParams::PrintBounds& x) {
        if (x.gp.bounds.size() > 0) {
            os << " where ";
            bool comma_needed = false;
            for (const auto& b : x.gp.bounds) {
                if (comma_needed) {
                    os << ", ";
                }
                os << b;
                comma_needed = true;
            }
        }
        return os;
    }
}

Ordering HIR::GenericBound::ord(const HIR::GenericBound& b) const {
    if (this->tag() != b.tag()) {
        return this->tag() < b.tag() ? OrdLess : OrdGreater;
    }
    TU_MATCHA((*this, b), (ae, be), (Lifetime, auto cmp = ::ord(ae.test, be.test); if (cmp != OrdEqual) return cmp; cmp = ::ord(ae.valid_for, be.valid_for); if (cmp != OrdEqual) return cmp;), (TypeLifetime, auto cmp = ae.type->ord_ignoring_regions(be.type); if (cmp != OrdEqual) return cmp; cmp = ::ord(ae.valid_for, be.valid_for); if (cmp != OrdEqual) return cmp;), (TraitBound, auto cmp = ae.type->ord_ignoring_regions(be.type); if (cmp != OrdEqual) return cmp; cmp = ae.trait.ord(be.trait); if (cmp != OrdEqual) return cmp;), (TypeEquality, auto cmp = ae.type->ord_ignoring_regions(be.type); if (cmp != OrdEqual) return cmp; cmp = ae.other_type->ord_ignoring_regions(be.other_type); if (cmp != OrdEqual) return cmp;))
    return OrdEqual;
}

HIR::PathParams HIR::GenericParams::make_nop_params(TypeInterner& types, unsigned level, bool lifetimes_only /*=false*/) const {
    assert(!lifetimes_only || this->types.empty());
    assert(!lifetimes_only || this->values.empty());

    HIR::PathParams rv;
    rv.mLifetimes = ThinVector<HIR::LifetimeRef>(this->mLifetimes.size());
    rv.types = ThinVector<HIR::TypeRef>(this->types.size());
    rv.values = ThinVector<HIR::ConstGeneric>(this->values.size());
    for (size_t i = 0; i < this->mLifetimes.size(); i++) {
        rv.mLifetimes[i] = HIR::LifetimeRef(256 * level + i);
    }
    for (size_t i = 0; i < this->types.size(); i++) {
        rv.types[i] = types.generic(this->types[i].mName, 256 * level + i);
    }
    for (size_t i = 0; i < this->values.size(); i++) {
        rv.values[i] = HIR::GenericRef(this->values[i].mName, 256 * level + i);
    }
    return rv;
}

::HIR::GenericParams HIR::GenericParams::clone() const {
    ::HIR::GenericParams rv;
    rv.types.reserve(types.size());
    for (const auto& type : types) {
        rv.types.push_back(::HIR::TypeParamDef{type.mName, type.defaultValue, type.isSized});
    }
    rv.values.reserve(values.size());
    for (const auto& type : values) {
        rv.values.push_back(::HIR::ValueParamDef{type.mName, type.mType, type.defaultValue.clone()});
    }
    rv.mLifetimes = mLifetimes;
    rv.bounds.reserve(bounds.size());
    for (const auto& bound : bounds) {
        rv.bounds.push_back(bound.clone());
    }
    return rv;
}

::HIR::GenericBound HIR::GenericBound::clone() const {
    TU_MATCH_HDRA( (*this), {)
    TU_ARMA(Lifetime, e) {
            return ::HIR::GenericBound::make_Lifetime(e);
        }
        TU_ARMA(TypeLifetime, e) {
            return ::HIR::GenericBound::make_TypeLifetime({e.type, e.valid_for});
        }
        TU_ARMA(TraitBound, e) {
            return ::HIR::GenericBound::make_TraitBound({e.hrtbs ? box$(e.hrtbs->clone()) : nullptr, e.type, e.trait.clone(), e.constness});
        } /*
    TU_ARMA(NotTrait, e) {
        return ::HIR::GenericBound::make_NotTrait({
            e.type.clone(),
            e.trait.clone()
            });
        }*/
        TU_ARMA(TypeEquality, e) {
            return ::HIR::GenericBound::make_TypeEquality({e.type, e.other_type});
        }
    }
    throw "Unreachable";
}

namespace HIR {

Ordering TypeParamDef::ord(const TypeParamDef& x) const {
    ORD(mName, x.mName);
    ORD(defaultValue, x.defaultValue);
    ORD(isSized, x.isSized);
    return OrdEqual;
}
Ordering LifetimeDef::ord(const LifetimeDef& x) const {
    ORD(mName, x.mName);
    return OrdEqual;
}
Ordering ValueParamDef::ord(const ValueParamDef& x) const {
    ORD(mName, x.mName);
    ORD(mType, x.mType);
    //ORD(m_default, x.m_default);
    return OrdEqual;
}
bool GenericParams::is_empty() const {
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
bool GenericParams::is_generic() const {
    if (!types.empty()) {
        return true;
    }
    // Note: Lifetimes don't matter
    if (!values.empty()) {
        return true;
    }
    return false;
}
PathParams GenericParams::make_empty_params(bool lifetimes_only) const {
    assert(lifetimes_only);
    PathParams rv;
    rv.mLifetimes = ThinVector<LifetimeRef>(mLifetimes.size());
    return rv;
}
GenericParams::PrintArgs::PrintArgs(const GenericParams& gp)
    : gp(gp) {
}
GenericParams::PrintBounds::PrintBounds(const GenericParams& gp)
    : gp(gp) {
}
Ordering GenericParams::ord(const HIR::GenericParams& x) const {
    ORD(types, x.types);
    ORD(mLifetimes, x.mLifetimes);
    ORD(values, x.values);
    ORD(bounds, x.bounds);
    return OrdEqual;
}
}
