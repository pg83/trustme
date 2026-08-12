#include "hir_typeck_impl_ref.h"
#include "hir_hir.h"
#include "hir_typeck_static.h" // for monomorphise_type_with

bool ImplRef::more_specific_than(HIR::TypeInterner& types, const ImplRef& other) const {
    TU_MATCH(Data, (this->mData), (te), (TraitImpl, if (te.impl == nullptr) { return false; } TU_MATCH(Data, (other.mData), (oe), (TraitImpl, if (oe.impl == nullptr) { return true; } return te.impl->more_specific_than(types, *oe.impl);), (BoundedPtr, return false;), (Bounded, return false;))), (BoundedPtr, if (!other.mData.is_BoundedPtr()) return false; const auto& oe = other.mData.as_BoundedPtr(); assert(te.type == oe.type); assert(*te.trait_args == *oe.trait_args); if (te.assoc->size() > oe.assoc->size()) return true; return false;), (Bounded, if (!other.mData.is_Bounded()) return false; const auto& oe = other.mData.as_Bounded(); assert(te.type == oe.type); assert(te.trait_args == oe.trait_args); if (te.assoc.size() > oe.assoc.size()) return true; return false;))
    throw "";
}

bool ImplRef::overlaps_with(const ::HIR::Crate& crate, const ImplRef& other) const {
    if (this->mData.tag() != other.mData.tag()) {
        return false;
    }
    TU_MATCH(
        Data,
        (this->mData, other.mData),
        (te, oe),
        (TraitImpl, if (te.impl != nullptr && oe.impl != nullptr) return te.impl->overlaps_with(crate, *oe.impl);),
        (BoundedPtr,
         // TODO: Bounded and BoundedPtr are compatible
         if (te.type != oe.type) return false;
         if (*te.trait_args != *oe.trait_args) return false;
         // Don't check associated types
         return true;),
        (Bounded, if (te.type != oe.type) return false; if (te.trait_args != oe.trait_args) return false;
         // Don't check associated types
         return true;)
    )
    return false;
}

bool ImplRef::has_magic_params() const {
    if (const auto* e = mData.opt_TraitImpl()) {
        for (const auto& t : e->impl_params.types) {
            if (visit_ty_with(t, [](const ::HIR::TypeData* t) {
                return t->is_Generic() && t->as_Generic().is_placeholder();
            })) {
                return true;
            }
        }
        for (const auto& v : e->impl_params.values) {
            if (v.is_Generic() && v.as_Generic().is_placeholder()) {
                return true;
            }
        }
    }
    return false;
}

bool ImplRef::type_is_specialisable(const char* name) const {
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
            return it->second.is_specialisable;
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
ImplRef::Monomorph ImplRef::get_cb_monomorph_traitimpl(HIR::TypeInterner& types, const Span& sp, const ::HIR::PathParams& params) const {
    const auto& e = this->mData.as_TraitImpl();
    return Monomorph(types, e, params);
}

::HIR::TypeRef ImplRef::Monomorph::get_type(const Span& sp, const ::HIR::GenericRef& ge) const /*override*/
{
    if (ge.is_self()) {
        // Store (or cache) a monomorphisation of Self, and error if this recurses
        if (this->ti.self_cache == ::HIR::TypeRef()) {
            this->ti.self_cache = types.diverge();
            this->ti.self_cache = this->monomorph_type(sp, this->ti.impl->mType);
        } else if (this->ti.self_cache == types.diverge()) {
            // BUG!
            BUG(sp, "Use of `Self` in expansion of `Self`");
        } else {
        }
        return this->ti.self_cache;
    }
    return MonomorphStatePtr(types, nullptr, &this->ti.impl_params, &this->params).get_type(sp, ge);
}

::HIR::ConstGeneric ImplRef::Monomorph::get_value(const Span& sp, const ::HIR::GenericRef& val) const /*override*/
{
    return MonomorphStatePtr(types, nullptr, &this->ti.impl_params, &this->params).get_value(sp, val);
}

::HIR::LifetimeRef ImplRef::Monomorph::get_lifetime(const Span& sp, const ::HIR::GenericRef& g) const /*override*/ {
    return MonomorphStatePtr(types, nullptr, &this->ti.impl_params, &this->params).get_lifetime(sp, g);
}

::HIR::TypeRef ImplRef::get_impl_type(HIR::TypeInterner& types) const {
    Span sp;
    TU_MATCH_HDRA( (this->mData), {)
    TU_ARMA(TraitImpl, e) {
            if (e.impl == nullptr) {
                BUG(Span(), "nullptr");
            }
            return this->get_cb_monomorph_traitimpl(types, sp, {}).monomorph_type(sp, e.impl->mType);
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

::HIR::PathParams ImplRef::get_trait_params(HIR::TypeInterner& types) const {
    Span sp;
    TU_MATCH_HDRA( (this->mData), {)
    TU_ARMA(TraitImpl, e) {
            if (e.impl == nullptr) {
                BUG(Span(), "nullptr");
            }
            return this->get_cb_monomorph_traitimpl(types, sp, {}).monomorph_path_params(sp, e.impl->traitArgs, true);
        }
        TU_ARMA(BoundedPtr, e) {
            return MonomorphHrlsOnly(types, e.hrls).monomorph_path_params(sp, *e.trait_args, true);
        }
        TU_ARMA(Bounded, e) {
            return MonomorphHrlsOnly(types, e.hrls).monomorph_path_params(sp, e.trait_args, true);
        }
    }
    throw "";
}

::HIR::TypeRef ImplRef::get_trait_ty_param(HIR::TypeInterner& types, unsigned int idx) const {
    Span sp;
    TU_MATCH_HDRA( (this->mData), {)
    TU_ARMA(TraitImpl, e) {
            if (e.impl == nullptr) {
                BUG(Span(), "nullptr");
            }
            if (idx >= e.impl->traitArgs.types.size()) {
                return ::HIR::TypeRef();
            }
            return this->get_cb_monomorph_traitimpl(types, sp, {}).monomorph_type(sp, e.impl->traitArgs.types[idx]);
        }
        TU_ARMA(BoundedPtr, e) {
            if (idx >= e.trait_args->types.size()) {
                return ::HIR::TypeRef();
            }
            return MonomorphHrlsOnly(types, e.hrls).monomorph_type(sp, e.trait_args->types.at(idx), true);
        }
        TU_ARMA(Bounded, e) {
            if (idx >= e.trait_args.types.size()) {
                return ::HIR::TypeRef();
            }
            return MonomorphHrlsOnly(types, e.hrls).monomorph_type(sp, e.trait_args.types.at(idx), true);
        }
    }
    throw "";
}

::HIR::TypeRef ImplRef::get_type(HIR::TypeInterner& types, const char* name, const HIR::PathParams& params) const {
    if (!name[0]) {
        return ::HIR::TypeRef();
    }
    static Span sp;
    TU_MATCH_HDRA( (this->mData), {)
    TU_ARMA(TraitImpl, e) {
            auto it = e.impl->types.find(name);
            if (it == e.impl->types.end()) {
                const HIR::TypeRef ty_self = types.self();
                if (e.trait_ptr->types.count(name) && e.trait_ptr->types.at(name).hasDefault) {
                    // Monomorph twice, first from trait to trait impl, second from trait impl to current
                    auto def = MonomorphStatePtr(types, ty_self, &e.impl->traitArgs, nullptr).monomorph_type(sp, e.trait_ptr->types.at(name).defaultValue);
                    return this->get_cb_monomorph_traitimpl(types, sp, params).monomorph_type(sp, def);
                }
                return ::HIR::TypeRef();
            }
            const ::HIR::TypeData* tpl_ty = it->second.data;
            DEBUG("name=" << name << " tpl_ty=" << tpl_ty << " " << *this);
            return this->get_cb_monomorph_traitimpl(types, sp, params).monomorph_type(sp, tpl_ty);
        }
        TU_ARMA(BoundedPtr, e) {
            auto it = e.assoc->find(name);
            if (it == e.assoc->end()) {
                return ::HIR::TypeRef();
            }
            ASSERT_BUG(Span(), !params.has_params(), "TODO: BoundedPtr ATY with params?");
            return MonomorphHrlsOnly(types, e.hrls).monomorph_type(sp, it->second.type, true);
        }
        TU_ARMA(Bounded, e) {
            auto it = e.assoc.find(name);
            if (it == e.assoc.end()) {
                return ::HIR::TypeRef();
            }
            ASSERT_BUG(Span(), !params.has_params(), "TODO: Bounded ATY with params?");
            return MonomorphHrlsOnly(types, e.hrls).monomorph_type(sp, it->second.type, true);
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
                os << e.impl->mParams.fmt_args();
                os << " " << *e.trait_path << e.impl->traitArgs << " for " << e.impl->mType << e.impl->mParams.fmt_bounds();
                os << " {";
                for (unsigned int i = 0; i < e.impl->mParams.mLifetimes.size(); i++) {
                    const auto& d = e.impl->mParams.mLifetimes[i];
                    os << d.mName << " = ";
                    if (e.impl_params.mLifetimes[i] != HIR::LifetimeRef()) {
                        os << e.impl_params.mLifetimes[i];
                    } else {
                        os << "?";
                    }
                    os << ",";
                }
                for (unsigned int i = 0; i < e.impl->mParams.types.size(); i++) {
                    const auto& ty_d = e.impl->mParams.types[i];
                    os << ty_d.mName << " = ";
                    if (e.impl_params.types[i] != HIR::TypeRef()) {
                        os << e.impl_params.types[i];
                    } else {
                        os << "?";
                    }
                    os << ",";
                }
                for (unsigned int i = 0; i < e.impl->mParams.values.size(); i++) {
                    const auto& d = e.impl->mParams.values[i];
                    os << d.mName << " = ";
                    if (e.impl_params.values[i] != HIR::ConstGeneric()) {
                        os << e.impl_params.values[i];
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
            assert(e.trait_args);
            assert(e.assoc);
            os << "bound (ptr) for" << e.hrls << " " << e.type << " : ?" << *e.trait_args << " + {" << *e.assoc << "}";
        }
        TU_ARM(x.mData, Bounded, e) {
            os << "bound for" << e.hrls << " " << e.type << " : ?" << e.trait_args << " + {" << e.assoc << "}";
        }
    }
    return os;
}

ImplRef::ImplRef()
    : mData(Data::make_TraitImpl({{}, nullptr, nullptr, nullptr})) {
}
ImplRef::ImplRef(HIR::PathParams impl_params, const HIR::Trait& trait_ref, const ::HIR::SimplePath& trait, const ::HIR::TraitImpl& impl)
    : mData(Data::make_TraitImpl({mv$(impl_params), &trait_ref, &trait, &impl})) {
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
