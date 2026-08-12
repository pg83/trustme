#include "hir_generic_params.h"
#include "hir_type.h"

namespace HIR {
    ::std::ostream& operator<<(::std::ostream& os, const GenericBound& x) {
        TU_MATCH(::HIR::GenericBound, (x), (e), (Lifetime, os << e.test << ": " << e.valid_for;), (TypeLifetime, os << e.type << ": " << e.valid_for;), (TraitBound, os << e.type << ": " << e.trait /*.m_path*/;), (TypeEquality, os << e.type << " = " << e.other_type;))
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const ::HIR::GenericParams::PrintArgs& x) {
        if (x.gp.m_lifetimes.size() > 0 || x.gp.m_types.size() > 0 || x.gp.m_values.size() > 0) {
            os << "<";
            for (const auto& lft : x.gp.m_lifetimes) {
                os << "'" << lft.m_name << ",";
            }
            for (const auto& typ : x.gp.m_types) {
                os << typ.m_name;
                if (!typ.m_is_sized) {
                    os << ": ?Sized";
                }
                if (typ.m_default && !typ.m_default->is_Infer()) {
                    os << " = " << typ.m_default;
                }
                os << ",";
            }
            if (!x.gp.m_values.empty()) {
                os << "const ";
            }
            for (const auto& val_p : x.gp.m_values) {
                os << val_p.m_name;
                os << ": ";
                os << val_p.m_type;
                os << ",";
            }
            os << ">";
        }
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const ::HIR::GenericParams::PrintBounds& x) {
        if (x.gp.m_bounds.size() > 0) {
            os << " where ";
            bool comma_needed = false;
            for (const auto& b : x.gp.m_bounds) {
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
    assert(!lifetimes_only || this->m_types.empty());
    assert(!lifetimes_only || this->m_values.empty());

    HIR::PathParams rv;
    rv.m_lifetimes = ThinVector<HIR::LifetimeRef>(this->m_lifetimes.size());
    rv.m_types = ThinVector<HIR::TypeRef>(this->m_types.size());
    rv.m_values = ThinVector<HIR::ConstGeneric>(this->m_values.size());
    for (size_t i = 0; i < this->m_lifetimes.size(); i++) {
        rv.m_lifetimes[i] = HIR::LifetimeRef(256 * level + i);
    }
    for (size_t i = 0; i < this->m_types.size(); i++) {
        rv.m_types[i] = types.generic(this->m_types[i].m_name, 256 * level + i);
    }
    for (size_t i = 0; i < this->m_values.size(); i++) {
        rv.m_values[i] = HIR::GenericRef(this->m_values[i].m_name, 256 * level + i);
    }
    return rv;
}

::HIR::GenericParams HIR::GenericParams::clone() const {
    ::HIR::GenericParams rv;
    rv.m_types.reserve(m_types.size());
    for (const auto& type : m_types) {
        rv.m_types.push_back(::HIR::TypeParamDef{type.m_name, type.m_default, type.m_is_sized});
    }
    rv.m_values.reserve(m_values.size());
    for (const auto& type : m_values) {
        rv.m_values.push_back(::HIR::ValueParamDef{type.m_name, type.m_type, type.m_default.clone()});
    }
    rv.m_lifetimes = m_lifetimes;
    rv.m_bounds.reserve(m_bounds.size());
    for (const auto& bound : m_bounds) {
        rv.m_bounds.push_back(bound.clone());
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
    ORD(m_name, x.m_name);
    ORD(m_default, x.m_default);
    ORD(m_is_sized, x.m_is_sized);
    return OrdEqual;
}
Ordering LifetimeDef::ord(const LifetimeDef& x) const {
    ORD(m_name, x.m_name);
    return OrdEqual;
}
Ordering ValueParamDef::ord(const ValueParamDef& x) const {
    ORD(m_name, x.m_name);
    ORD(m_type, x.m_type);
    //ORD(m_default, x.m_default);
    return OrdEqual;
}
bool GenericParams::is_empty() const {
    if (!m_types.empty()) {
        return false;
    }
    if (!m_lifetimes.empty()) {
        return false;
    }
    if (!m_values.empty()) {
        return false;
    }
    if (!m_bounds.empty()) {
        return false;
    }
    return true;
}
bool GenericParams::is_generic() const {
    if (!m_types.empty()) {
        return true;
    }
    // Note: Lifetimes don't matter
    if (!m_values.empty()) {
        return true;
    }
    return false;
}
PathParams GenericParams::make_empty_params(bool lifetimes_only) const {
    assert(lifetimes_only);
    PathParams rv;
    rv.m_lifetimes = ThinVector<LifetimeRef>(m_lifetimes.size());
    return rv;
}
GenericParams::PrintArgs::PrintArgs(const GenericParams& gp)
    : gp(gp) {
}
GenericParams::PrintBounds::PrintBounds(const GenericParams& gp)
    : gp(gp) {
}
Ordering GenericParams::ord(const HIR::GenericParams& x) const {
    ORD(m_types, x.m_types);
    ORD(m_lifetimes, x.m_lifetimes);
    ORD(m_values, x.m_values);
    ORD(m_bounds, x.m_bounds);
    return OrdEqual;
}
}
