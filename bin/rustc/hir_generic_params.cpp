#include "hir_generic_params.h"

#include "hir_type.h"

std::ostream& operator<<(std::ostream& os, const HIRGenericBound& x) {
    switch (x.tag()) {
        case HIRGenericBound::TAG_TraitBound: {
            auto& e = x.as_TraitBound();
            os << e.type << ": " << e.trait /*.m_path*/;
            break;
        }
        case HIRGenericBound::TAG_TypeEquality: {
            auto& e = x.as_TypeEquality();
            os << e.type << " = " << e.otherType;
            break;
        }
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const HIRGenericParams::PrintArgs& x) {
    if (x.gp.types.size() > 0 || x.gp.values.size() > 0) {
        os << "<";
        size_t typeIndex = 0;
        size_t valueIndex = 0;
        for (size_t i = 0; i < x.gp.paramCount(); i++) {
            if (x.gp.paramKindAt(i) == HIRGenericParamKind::Type) {
                const auto& typ = x.gp.types[typeIndex++];
                os << typ.name;
                if (!typ.isSized) {
                    os << ": ?Sized";
                }
                if (typ.defaultValue && !typ.defaultValue->is_Infer()) {
                    os << " = " << typ.defaultValue;
                }
            } else {
                const auto& valP = x.gp.values[valueIndex++];
                os << "const " << valP.name << ": " << valP.type;
            }
            os << ",";
        }
        os << ">";
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const HIRGenericParams::PrintBounds& x) {
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
    switch ((*this).tag()) {
        case HIRGenericBound::TAG_TraitBound: {
            auto& ae = (*this).as_TraitBound();
            auto& be = b.as_TraitBound();
            auto cmp = ae.type->ordIgnoringRegions(be.type);
            if (cmp != OrdEqual) {
                return cmp;
            }
            cmp = ae.trait.ord(be.trait);
            if (cmp != OrdEqual) {
                return cmp;
            }
            cmp = ::ord(ae.isTrivial, be.isTrivial);
            if (cmp != OrdEqual) {
                return cmp;
            }
            break;
        }
        case HIRGenericBound::TAG_TypeEquality: {
            auto& ae = (*this).as_TypeEquality();
            auto& be = b.as_TypeEquality();
            auto cmp = ae.type->ordIgnoringRegions(be.type);
            if (cmp != OrdEqual) {
                return cmp;
            }
            cmp = ae.otherType->ordIgnoringRegions(be.otherType);
            if (cmp != OrdEqual) {
                return cmp;
            }
            break;
        }
    }
    return OrdEqual;
}

HIRPathParams HIRGenericParams::makeNopParams(HIRTypeInterner& types, unsigned level) const {
    HIRPathParams rv;
    rv.types = ThinVector<HIRTypeRef>(this->types.size());
    rv.values = ThinVector<HIRConstGeneric>(this->values.size());
    for (size_t i = 0; i < this->types.size(); i++) {
        rv.types[i] = types.generic(this->types[i].name, 256 * level + i);
    }
    for (size_t i = 0; i < this->values.size(); i++) {
        rv.values[i] = HIRGenericRef(this->values[i].name, 256 * level + i);
    }
    return rv;
}

HIRGenericParams HIRGenericParams::clone() const {
    HIRGenericParams rv;
    rv.paramKinds = paramKinds;
    rv.types.reserve(types.size());
    for (const auto& type : types) {
        rv.types.push_back(HIRTypeParamDef{type.name, type.defaultValue, type.isSized});
    }
    rv.values.reserve(values.size());
    for (const auto& type : values) {
        rv.values.push_back(HIRValueParamDef{type.name, type.type, type.defaultValue.clone()});
    }
    rv.bounds.reserve(bounds.size());
    for (const auto& bound : bounds) {
        rv.bounds.push_back(bound.clone());
    }
    return rv;
}

HIRGenericBound HIRGenericBound::clone() const {
    switch ((*this).tag()) {
        case HIRGenericBound::TAG_TraitBound: {
            auto& e = (*this).as_TraitBound();
            return HIRGenericBound::make_TraitBound({e.type, e.trait.clone(), e.constness, e.isTrivial});
        }
        case HIRGenericBound::TAG_TypeEquality: {
            auto& e = (*this).as_TypeEquality();
            return HIRGenericBound::make_TypeEquality({e.type, e.otherType});
        }
    }
    UNREACHABLE();
}

Ordering HIRTypeParamDef::ord(const HIRTypeParamDef& x) const {
    ORD(name, x.name);
    ORD(defaultValue, x.defaultValue);
    ORD(isSized, x.isSized);
    return OrdEqual;
}

Ordering HIRValueParamDef::ord(const HIRValueParamDef& x) const {
    ORD(name, x.name);
    ORD(type, x.type);
    return OrdEqual;
}

bool HIRGenericParams::isEmpty() const {
    if (!types.empty()) {
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
    if (!values.empty()) {
        return true;
    }
    return false;
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
    if (paramCount() != x.paramCount()) {
        return paramCount() < x.paramCount() ? OrdLess : OrdGreater;
    }
    for (size_t i = 0; i < paramCount(); i++) {
        if (paramKindAt(i) != x.paramKindAt(i)) {
            return paramKindAt(i) < x.paramKindAt(i) ? OrdLess : OrdGreater;
        }
    }
    ORD(types, x.types);
    ORD(values, x.values);
    ORD(bounds, x.bounds);
    return OrdEqual;
}
