#include "hir_typeck_impl_ref.h"
#include "hir_hir.h"
#include "hir_typeck_static.h" // for monomorphise_type_with

bool ImplRef::moreSpecificThan(HIR::TypeInterner& types, const ImplRef& other) const {
    TU_MATCH(Data, (this->mData), (te), (TraitImpl, if (te.impl == nullptr) { return false; } TU_MATCH(Data, (other.mData), (oe), (TraitImpl, if (oe.impl == nullptr) { return true; } return te.impl->moreSpecificThan(types, *oe.impl);), (BoundedPtr, return false;), (Bounded, return false;))), (BoundedPtr, if (!other.mData.is_BoundedPtr()) return false; const auto& oe = other.mData.as_BoundedPtr(); assert(te.type == oe.type); assert(*te.traitArgs == *oe.traitArgs); if (te.assoc->size() > oe.assoc->size()) return true; return false;), (Bounded, if (!other.mData.is_Bounded()) return false; const auto& oe = other.mData.as_Bounded(); assert(te.type == oe.type); assert(te.traitArgs == oe.traitArgs); if (te.assoc.size() > oe.assoc.size()) return true; return false;))
    throw "";
}

bool ImplRef::overlapsWith(const ::HIR::Crate& crate, const ImplRef& other) const {
    if (this->mData.tag() != other.mData.tag()) {
        return false;
    }
    TU_MATCH(
        Data,
        (this->mData, other.mData),
        (te, oe),
        (TraitImpl, if (te.impl != nullptr && oe.impl != nullptr) return te.impl->overlapsWith(crate, *oe.impl);),
        (BoundedPtr,
         // TODO: Bounded and BoundedPtr are compatible
         if (te.type != oe.type) return false;
         if (*te.traitArgs != *oe.traitArgs) return false;
         // Don't check associated types
         return true;),
        (Bounded, if (te.type != oe.type) return false; if (te.traitArgs != oe.traitArgs) return false;
         // Don't check associated types
         return true;)
    )
    return false;
}

bool ImplRef::hasMagicParams() const {
    if (const auto* e = mData.opt_TraitImpl()) {
        for (const auto& t : e->implParams.types) {
            if (visitTyWith(t, [](const ::HIR::TypeData* t) {
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
    TU_MATCH_HDRA( (this->mData), {)
    TU_ARMA(TraitImpl, e) {
            if (e.impl == nullptr) {
                // No impl yet? This type is specialisable.
                return true;
            }
            auto it = e.impl->types.find(name);
            if (it == e.impl->types.end()) {
                // If not present (which might happen during UFCS resolution), assume that it's not specialisable
                return false;
            }
            return it->second.isSpecialisable;
        }
        TU_ARMA(BoundedPtr, e) {
            return false;
        }
        TU_ARMA(Bounded, E) {
            return false;
        }
    }
    throw "";
}

// Returns a closure to monomorphise including placeholders (if present)
ImplRef::Monomorph ImplRef::getCbMonomorphTraitimpl(HIR::TypeInterner& types, const Span& sp, const ::HIR::PathParams& params) const {
    const auto& e = this->mData.as_TraitImpl();
    return Monomorph(types, e, params);
}

::HIR::TypeRef ImplRef::Monomorph::getType(const Span& sp, const ::HIR::GenericRef& ge) const /*override*/
{
    if (ge.isSelf()) {
        // Store (or cache) a monomorphisation of Self, and error if this recurses
        if (this->ti.selfCache == ::HIR::TypeRef()) {
            this->ti.selfCache = types.diverge();
            this->ti.selfCache = this->monomorphType(sp, this->ti.impl->mType);
        } else if (this->ti.selfCache == types.diverge()) {
            // BUG!
            BUG(sp, "Use of `Self` in expansion of `Self`");
        } else {
        }
        return this->ti.selfCache;
    }
    return MonomorphStatePtr(types, nullptr, &this->ti.implParams, &this->params).getType(sp, ge);
}

::HIR::ConstGeneric ImplRef::Monomorph::getValue(const Span& sp, const ::HIR::GenericRef& val) const /*override*/
{
    return MonomorphStatePtr(types, nullptr, &this->ti.implParams, &this->params).getValue(sp, val);
}

::HIR::LifetimeRef ImplRef::Monomorph::getLifetime(const Span& sp, const ::HIR::GenericRef& g) const /*override*/ {
    return MonomorphStatePtr(types, nullptr, &this->ti.implParams, &this->params).getLifetime(sp, g);
}

::HIR::TypeRef ImplRef::getImplType(HIR::TypeInterner& types) const {
    Span sp;
    TU_MATCH_HDRA( (this->mData), {)
    TU_ARMA(TraitImpl, e) {
            if (e.impl == nullptr) {
                BUG(Span(), "nullptr");
            }
            return this->getCbMonomorphTraitimpl(types, sp, {}).monomorphType(sp, e.impl->mType);
        }
        TU_ARMA(BoundedPtr, e) {
            // HRLs needed?
            return e.type;
        }
        TU_ARMA(Bounded, e) {
            return e.type;
        }
    }
    throw "";
}

::HIR::PathParams ImplRef::getTraitParams(HIR::TypeInterner& types) const {
    Span sp;
    TU_MATCH_HDRA( (this->mData), {)
    TU_ARMA(TraitImpl, e) {
            if (e.impl == nullptr) {
                BUG(Span(), "nullptr");
            }
            return this->getCbMonomorphTraitimpl(types, sp, {}).monomorphPathParams(sp, e.impl->traitArgs, true);
        }
        TU_ARMA(BoundedPtr, e) {
            return MonomorphHrlsOnly(types, e.hrls).monomorphPathParams(sp, *e.traitArgs, true);
        }
        TU_ARMA(Bounded, e) {
            return MonomorphHrlsOnly(types, e.hrls).monomorphPathParams(sp, e.traitArgs, true);
        }
    }
    throw "";
}

::HIR::TypeRef ImplRef::getTraitTyParam(HIR::TypeInterner& types, unsigned int idx) const {
    Span sp;
    TU_MATCH_HDRA( (this->mData), {)
    TU_ARMA(TraitImpl, e) {
            if (e.impl == nullptr) {
                BUG(Span(), "nullptr");
            }
            if (idx >= e.impl->traitArgs.types.size()) {
                return ::HIR::TypeRef();
            }
            return this->getCbMonomorphTraitimpl(types, sp, {}).monomorphType(sp, e.impl->traitArgs.types[idx]);
        }
        TU_ARMA(BoundedPtr, e) {
            if (idx >= e.traitArgs->types.size()) {
                return ::HIR::TypeRef();
            }
            return MonomorphHrlsOnly(types, e.hrls).monomorphType(sp, e.traitArgs->types.at(idx), true);
        }
        TU_ARMA(Bounded, e) {
            if (idx >= e.traitArgs.types.size()) {
                return ::HIR::TypeRef();
            }
            return MonomorphHrlsOnly(types, e.hrls).monomorphType(sp, e.traitArgs.types.at(idx), true);
        }
    }
    throw "";
}

::HIR::TypeRef ImplRef::getType(HIR::TypeInterner& types, const char* name, const HIR::PathParams& params) const {
    if (!name[0]) {
        return ::HIR::TypeRef();
    }
    static Span sp;
    TU_MATCH_HDRA( (this->mData), {)
    TU_ARMA(TraitImpl, e) {
            auto it = e.impl->types.find(name);
            if (it == e.impl->types.end()) {
                const HIR::TypeRef tySelf = types.self();
                if (e.traitPtr->types.count(name) && e.traitPtr->types.at(name).hasDefault) {
                    // Monomorph twice, first from trait to trait impl, second from trait impl to current
                    auto def = MonomorphStatePtr(types, tySelf, &e.impl->traitArgs, nullptr).monomorphType(sp, e.traitPtr->types.at(name).defaultValue);
                    return this->getCbMonomorphTraitimpl(types, sp, params).monomorphType(sp, def);
                }
                return ::HIR::TypeRef();
            }
            const ::HIR::TypeData* tplTy = it->second.data;
            DEBUG("name=" << name << " tpl_ty=" << tplTy << " " << *this);
            return this->getCbMonomorphTraitimpl(types, sp, params).monomorphType(sp, tplTy);
        }
        TU_ARMA(BoundedPtr, e) {
            auto it = e.assoc->find(name);
            if (it == e.assoc->end()) {
                return ::HIR::TypeRef();
            }
            ASSERT_BUG(Span(), !params.hasParams(), "TODO: BoundedPtr ATY with params?");
            return MonomorphHrlsOnly(types, e.hrls).monomorphType(sp, it->second.type, true);
        }
        TU_ARMA(Bounded, e) {
            auto it = e.assoc.find(name);
            if (it == e.assoc.end()) {
                return ::HIR::TypeRef();
            }
            ASSERT_BUG(Span(), !params.hasParams(), "TODO: Bounded ATY with params?");
            return MonomorphHrlsOnly(types, e.hrls).monomorphType(sp, it->second.type, true);
        }
    }
    return ::HIR::TypeRef();
}

::std::ostream& operator<<(::std::ostream& os, const ImplRef& x) {
    TU_MATCH_HDR( (x.mData), { )
    TU_ARM(x.mData, TraitImpl, e) {
            if (e.impl == nullptr) {
                os << "none";
            } else {
                os << "impl";
                os << "(" << e.impl << ")";
                os << e.impl->mParams.fmtArgs();
                os << " " << *e.traitPath << e.impl->traitArgs << " for " << e.impl->mType << e.impl->mParams.fmtBounds();
                os << " {";
                for (unsigned int i = 0; i < e.impl->mParams.mLifetimes.size(); i++) {
                    const auto& d = e.impl->mParams.mLifetimes[i];
                    os << d.mName << " = ";
                    if (e.implParams.mLifetimes[i] != HIR::LifetimeRef()) {
                        os << e.implParams.mLifetimes[i];
                    } else {
                        os << "?";
                    }
                    os << ",";
                }
                for (unsigned int i = 0; i < e.impl->mParams.types.size(); i++) {
                    const auto& tyD = e.impl->mParams.types[i];
                    os << tyD.mName << " = ";
                    if (e.implParams.types[i] != HIR::TypeRef()) {
                        os << e.implParams.types[i];
                    } else {
                        os << "?";
                    }
                    os << ",";
                }
                for (unsigned int i = 0; i < e.impl->mParams.values.size(); i++) {
                    const auto& d = e.impl->mParams.values[i];
                    os << d.mName << " = ";
                    if (e.implParams.values[i] != HIR::ConstGeneric()) {
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
        }
        TU_ARM(x.mData, BoundedPtr, e) {
            assert(e.type);
            assert(e.traitArgs);
            assert(e.assoc);
            os << "bound (ptr) for" << e.hrls << " " << e.type << " : ?" << *e.traitArgs << " + {" << *e.assoc << "}";
        }
        TU_ARM(x.mData, Bounded, e) {
            os << "bound for" << e.hrls << " " << e.type << " : ?" << e.traitArgs << " + {" << e.assoc << "}";
        }
    }
    return os;
}

ImplRef::ImplRef()
    : mData(Data::make_TraitImpl({{}, nullptr, nullptr, nullptr})) {
}
ImplRef::ImplRef(HIR::PathParams implParams, const HIR::Trait& traitRef, const ::HIR::SimplePath& trait, const ::HIR::TraitImpl& impl)
    : mData(Data::make_TraitImpl({mv$(implParams), &traitRef, &trait, &impl})) {
}
ImplRef::ImplRef(const ::HIR::TypeData* type, const ::HIR::PathParams* args, const ::HIR::TraitPath::assocListT* assoc, ::HIR::BoundConstness constness)
    : mData(Data::make_BoundedPtr({HIR::PathParams(), type, args, assoc, constness})) {
}
ImplRef::ImplRef(::HIR::PathParams hrls, const ::HIR::TypeData* type, const ::HIR::PathParams* args, const ::HIR::TraitPath::assocListT* assoc, ::HIR::BoundConstness constness)
    : mData(Data::make_BoundedPtr({std::move(hrls), type, args, assoc, constness})) {
}
ImplRef::ImplRef(::HIR::TypeRef type, ::HIR::PathParams args, ::HIR::TraitPath::assocListT assoc, ::HIR::BoundConstness constness)
    : mData(Data::make_Bounded({::HIR::PathParams(), mv$(type), mv$(args), mv$(assoc), constness})) {
}
ImplRef::ImplRef(::HIR::PathParams hrls, ::HIR::TypeRef type, ::HIR::PathParams args, ::HIR::TraitPath::assocListT assoc, ::HIR::BoundConstness constness)
    : mData(Data::make_Bounded({mv$(hrls), mv$(type), mv$(args), mv$(assoc), constness})) {
}
::HIR::BoundConstness ImplRef::boundConstness() const {
    if (const auto* e = mData.opt_BoundedPtr()) {
        return e->constness;
    }
    if (const auto* e = mData.opt_Bounded()) {
        return e->constness;
    }
    return ::HIR::BoundConstness::Never;
}
ImplRef::Monomorph::Monomorph(HIR::TypeInterner& types, const ImplRef::Data::Data_TraitImpl& ti, const ::HIR::PathParams& params)
    : Monomorphiser(types)
    , ti(ti)
    , params(params) {
}
