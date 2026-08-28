#include "hir_typeck_impl_ref.h"

#include "hir_hir.h"
#include "hir_typeck_static.h"

namespace {
    bool pathParamsEqual(const HIRPathParams* left, const HIRPathParams* right) {
        if (!left || !right) {
            return (!left || !left->hasParams()) && (!right || !right->hasParams());
        }
        return *left == *right;
    }

    size_t associatedSize(const HIRTraitPath::assocListT* associated) {
        return associated ? associated->size() : 0;
    }
}

bool ImplRef::moreSpecificThan(HIRTypeInterner& types, const ImplRef& other) const {
    switch (this->data.tag()) {
        case Data::TAG_TraitImpl: {
            auto& te = this->data.as_TraitImpl();
            if (te.impl == nullptr) {
                return false;
            }
            switch (other.data.tag()) {
                case Data::TAG_TraitImpl: {
                    auto& oe = other.data.as_TraitImpl();
                    if (oe.impl == nullptr) {
                        return true;
                    }
                    return te.impl->moreSpecificThan(types, *oe.impl);
                    break;
                }
                case Data::TAG_BoundedPtr: {
                    return false;
                }
                case Data::TAG_Bounded: {
                    return false;
                }
            }
            break;
        }
        case Data::TAG_BoundedPtr: {
            auto& te = this->data.as_BoundedPtr();
            if (!other.data.is_BoundedPtr()) {
                return false;
            }
            const auto& oe = other.data.as_BoundedPtr();
            assert(te.type == oe.type);
            assert(pathParamsEqual(te.traitArgs, oe.traitArgs));
            if (associatedSize(te.assoc) > associatedSize(oe.assoc)) {
                return true;
            }
            return false;
            break;
        }
        case Data::TAG_Bounded: {
            auto& te = this->data.as_Bounded();
            if (!other.data.is_Bounded()) {
                return false;
            }
            const auto& oe = other.data.as_Bounded();
            assert(te.type == oe.type);
            assert(te.traitArgs == oe.traitArgs);
            if (te.assoc.size() > oe.assoc.size()) {
                return true;
            }
            return false;
            break;
        }
    }
    UNREACHABLE();
}

bool ImplRef::overlapsWith(const HIRCrate& crate, const ImplRef& other) const {
    if (this->data.tag() != other.data.tag()) {
        return false;
    }
    switch (this->data.tag()) {
        case Data::TAG_TraitImpl: {
            auto& te = this->data.as_TraitImpl();
            auto& oe = other.data.as_TraitImpl();
            if (te.impl != nullptr && oe.impl != nullptr) {
                return te.impl->overlapsWith(crate, *oe.impl);
            }
            break;
        }
        case Data::TAG_BoundedPtr: {
            auto& te = this->data.as_BoundedPtr();
            auto& oe = other.data.as_BoundedPtr();
            // TODO: Bounded and BoundedPtr are compatible
            if (te.type != oe.type) {
                return false;
            }
            if (!pathParamsEqual(te.traitArgs, oe.traitArgs)) {
                return false;
            }
            return true;
        }
        case Data::TAG_Bounded: {
            auto& te = this->data.as_Bounded();
            auto& oe = other.data.as_Bounded();
            if (te.type != oe.type) {
                return false;
            }
            if (te.traitArgs != oe.traitArgs) {
                return false;
            }
            return true;
        }
    }
    return false;
}

bool ImplRef::hasMagicParams() const {
    if (const auto* e = data.opt_TraitImpl()) {
        for (const auto& t : e->implParams.types) {
            if (visitTyWith(t, [](const HIRTypeData* t) {
                return t->is_Generic() && t->as_Generic().isPlaceholder();
            })) {
                return true;
            }
        }
        for (const auto& v : e->implParams.values) {
            if (v.is_Generic() && v.as_Generic().isPlaceholder()) {
                return true;
            }
        }
    }
    return false;
}

bool ImplRef::typeIsSpecialisable(const char* name) const {
    switch (this->data.tag()) {
        case ImplRefData::TAG_TraitImpl: {
            auto& e = this->data.as_TraitImpl();
            if (e.impl == nullptr) {
                return true;
            }
            auto it = e.impl->types.find(name);
            if (it == e.impl->types.end()) {
                return false;
            }
            return it->second.isSpecialisable;
        }
        case ImplRefData::TAG_BoundedPtr: {
            return false;
        }
        case ImplRefData::TAG_Bounded: {
            return false;
        }
    }
    UNREACHABLE();
}

ImplRef::Monomorph ImplRef::getCbMonomorphTraitimpl(HIRTypeInterner& types, const Span& sp, const HIRPathParams& params) const {
    const auto& e = this->data.as_TraitImpl();
    return Monomorph(types, e, params);
}

HIRTypeRef ImplRef::Monomorph::getType(const Span& sp, const HIRGenericRef& ge) const /*override*/
{
    if (ge.isSelf()) {
        if (this->ti.selfCache == HIRTypeRef()) {
            this->ti.selfCache = types.diverge();
            this->ti.selfCache = this->monomorphType(sp, this->ti.impl->type);
        } else if (this->ti.selfCache == types.diverge()) {
            // BUG!
            BUG(sp, "Use of `Self` in expansion of `Self`");
        } else {
        }
        return this->ti.selfCache;
    }
    return MonomorphStatePtr(types, nullptr, &this->ti.implParams, &this->params).getType(sp, ge);
}

HIRConstGeneric ImplRef::Monomorph::getValue(const Span& sp, const HIRGenericRef& val) const /*override*/
{
    return MonomorphStatePtr(types, nullptr, &this->ti.implParams, &this->params).getValue(sp, val);
}

HIRTypeRef ImplRef::getImplType(HIRTypeInterner& types) const {
    Span sp;
    switch (this->data.tag()) {
        case ImplRefData::TAG_TraitImpl: {
            auto& e = this->data.as_TraitImpl();
            if (e.impl == nullptr) {
                BUG(Span(), "nullptr");
            }
            return this->getCbMonomorphTraitimpl(types, sp, {}).monomorphType(sp, e.impl->type);
        }
        case ImplRefData::TAG_BoundedPtr: {
            auto& e = this->data.as_BoundedPtr();
            return e.type;
        }
        case ImplRefData::TAG_Bounded: {
            auto& e = this->data.as_Bounded();
            return e.type;
        }
    }
    UNREACHABLE();
}

HIRPathParams ImplRef::getTraitParams(HIRTypeInterner& types) const {
    Span sp;
    switch (this->data.tag()) {
        case ImplRefData::TAG_TraitImpl: {
            auto& e = this->data.as_TraitImpl();
            if (e.impl == nullptr) {
                BUG(Span(), "nullptr");
            }
            return this->getCbMonomorphTraitimpl(types, sp, {}).monomorphPathParams(sp, e.impl->traitArgs, true);
        }
        case ImplRefData::TAG_BoundedPtr: {
            auto& e = this->data.as_BoundedPtr();
            return e.traitArgs ? e.traitArgs->clone() : HIRPathParams();
        }
        case ImplRefData::TAG_Bounded: {
            auto& e = this->data.as_Bounded();
            return e.traitArgs.clone();
        }
    }
    UNREACHABLE();
}

HIRTypeRef ImplRef::getTraitTyParam(HIRTypeInterner& types, unsigned int idx) const {
    Span sp;
    switch (this->data.tag()) {
        case ImplRefData::TAG_TraitImpl: {
            auto& e = this->data.as_TraitImpl();
            if (e.impl == nullptr) {
                BUG(Span(), "nullptr");
            }
            if (idx >= e.impl->traitArgs.types.size()) {
                return HIRTypeRef();
            }
            return this->getCbMonomorphTraitimpl(types, sp, {}).monomorphType(sp, e.impl->traitArgs.types[idx]);
        }
        case ImplRefData::TAG_BoundedPtr: {
            auto& e = this->data.as_BoundedPtr();
            if (!e.traitArgs || idx >= e.traitArgs->types.size()) {
                return HIRTypeRef();
            }
            return e.traitArgs->types.at(idx);
        }
        case ImplRefData::TAG_Bounded: {
            auto& e = this->data.as_Bounded();
            if (idx >= e.traitArgs.types.size()) {
                return HIRTypeRef();
            }
            return e.traitArgs.types.at(idx);
        }
    }
    UNREACHABLE();
}

HIRTypeRef ImplRef::getType(HIRTypeInterner& types, const char* name, const HIRPathParams& params) const {
    if (!name[0]) {
        return HIRTypeRef();
    }
    Span sp;
    switch (this->data.tag()) {
        case ImplRefData::TAG_TraitImpl: {
            auto& e = this->data.as_TraitImpl();
            auto it = e.impl->types.find(name);
            if (it == e.impl->types.end()) {
                const HIRTypeRef tySelf = types.self();
                if (e.traitPtr->types.count(name) && e.traitPtr->types.at(name).hasDefault) {
                    auto def = MonomorphStatePtr(types, tySelf, &e.impl->traitArgs, nullptr).monomorphType(sp, e.traitPtr->types.at(name).defaultValue);
                    return this->getCbMonomorphTraitimpl(types, sp, params).monomorphType(sp, def);
                }
                return HIRTypeRef();
            }
            const HIRTypeData* tplTy = it->second.data;
            return this->getCbMonomorphTraitimpl(types, sp, params).monomorphType(sp, tplTy);
        }
        case ImplRefData::TAG_BoundedPtr: {
            auto& e = this->data.as_BoundedPtr();
            if (!e.assoc) {
                return HIRTypeRef();
            }
            auto it = e.assoc->find(name);
            if (it == e.assoc->end()) {
                return HIRTypeRef();
            }
            if (!it->second.atyParams.equalsIgnoringRegions(params)) {
                return HIRTypeRef();
            }
            return it->second.type;
        }
        case ImplRefData::TAG_Bounded: {
            auto& e = this->data.as_Bounded();
            auto it = e.assoc.find(name);
            if (it == e.assoc.end()) {
                return HIRTypeRef();
            }
            if (!it->second.atyParams.equalsIgnoringRegions(params)) {
                return HIRTypeRef();
            }
            return it->second.type;
        }
    }
    return HIRTypeRef();
}

::std::ostream& operator<<(::std::ostream& os, const ImplRef& x) {
    switch (x.data.tag()) {
        case ImplRefData::TAG_TraitImpl: {
            auto& e = x.data.as_TraitImpl();
            if (e.impl == nullptr) {
                os << "none";
            } else {
                os << "impl";
                os << "(" << e.impl << ")";
                os << e.impl->params.fmtArgs();
                os << " " << *e.traitPath << e.impl->traitArgs << " for " << e.impl->type << e.impl->params.fmtBounds();
                os << " {";
                for (unsigned int i = 0; i < e.impl->params.types.size(); i++) {
                    const auto& tyD = e.impl->params.types[i];
                    os << tyD.name << " = ";
                    if (e.implParams.types[i] != HIRTypeRef()) {
                        os << e.implParams.types[i];
                    } else {
                        os << "?";
                    }
                    os << ",";
                }
                for (unsigned int i = 0; i < e.impl->params.values.size(); i++) {
                    const auto& d = e.impl->params.values[i];
                    os << d.name << " = ";
                    if (e.implParams.values[i] != HIRConstGeneric()) {
                        os << e.implParams.values[i];
                    } else {
                        os << "?";
                    }
                    os << ",";
                }
                for (const auto& aty : e.impl->types) {
                    os << "Self::" << aty.first << " = " << aty.second.data << ",";
                }
                os << "}";
            }
            break;
        }
        case ImplRefData::TAG_BoundedPtr: {
            auto& e = x.data.as_BoundedPtr();
            assert(e.type);
            os << "bound (ptr) " << e.type << " : ?";
            if (e.traitArgs) {
                os << *e.traitArgs;
            }
            os << " + {";
            if (e.assoc) {
                os << *e.assoc;
            }
            os << "}";
            break;
        }
        case ImplRefData::TAG_Bounded: {
            auto& e = x.data.as_Bounded();
            os << "bound " << e.type << " : ?" << e.traitArgs << " + {" << e.assoc << "}";
            break;
        }
    }
    return os;
}

ImplRef::ImplRef()
    : data(Data::make_TraitImpl({{}, nullptr, nullptr, nullptr}))
{
}

ImplRef::ImplRef(HIRPathParams implParams, const HIRTrait& traitRef, const HIRSimplePath& trait, const HIRTraitImpl& impl)
    : data(Data::make_TraitImpl({mv$(implParams), &traitRef, &trait, &impl}))
{
}

ImplRef::ImplRef(const HIRTypeData* type, const HIRPathParams* args, const HIRTraitPath::assocListT* assoc, HIRBoundConstness constness)
    : data(Data::make_BoundedPtr({type, args, assoc, constness}))
{
}

ImplRef::ImplRef(HIRTypeRef type, HIRPathParams args, HIRTraitPath::assocListT assoc, HIRBoundConstness constness)
    : data(Data::make_Bounded({mv$(type), mv$(args), mv$(assoc), constness}))
{
}

HIRBoundConstness ImplRef::boundConstness() const {
    if (const auto* e = data.opt_BoundedPtr()) {
        return e->constness;
    }
    if (const auto* e = data.opt_Bounded()) {
        return e->constness;
    }
    return HIRBoundConstness::Never;
}

ImplRef::Monomorph::Monomorph(HIRTypeInterner& types, const ImplRef::Data::Data_TraitImpl& ti, const HIRPathParams& params)
    : Monomorphiser(types)
    , ti(ti)
    , params(params)
{
}
