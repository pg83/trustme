#include "hir_typeck_resolve_common.h"
#include "hir_typeck_monomorph.h" // MonomorphStatePtr

void TraitResolveCommon::prep_indexes(const Span& sp) {
    TRACE_FUNCTION_F("");

    if (implGenerics) {
        DEBUG("m_impl_generics = " << implGenerics->fmt_args() << implGenerics->fmt_bounds());
    }
    if (itemGenerics) {
        DEBUG("m_item_generics = " << itemGenerics->fmt_args() << itemGenerics->fmt_bounds());
    }

    typeEqualities.clear();
    traitBounds.clear();

    this->iterate_bounds([&](const HIR::GenericBound& b) -> bool {
        TU_MATCH_HDRA( (b), { )
        default:
            break;
            TU_ARMA(TraitBound, be) {
                this->prepIndexesAddTraitBound(sp, be.hrtbs.get(), be.type, be.trait.clone());
            }
            TU_ARMA(TypeEquality, be) {
                DEBUG("Equality - " << be.type << " = " << be.other_type);
                this->prepIndexesAddEquality(sp, nullptr, be.type, be.other_type);
            }
        }
        return false;
    });
    DEBUG(traitBounds.size() << " trait bounds");
    for (const auto& tb : traitBounds) {
        DEBUG(tb.first.first << " : " << (!tb.second.hrbs.is_empty() ? "for" : "") << tb.second.hrbs.fmt_args() << tb.first.second << " - " << tb.second.assoc);
    }
}

void TraitResolveCommon::prepIndexesAddEquality(const Span& sp, const ::HIR::GenericParams* hrtbs, ::HIR::TypeRef long_ty, ::HIR::TypeRef short_ty) {
    DEBUG("ADD " << long_ty << " => " << short_ty);
    if (!hrtbs) {
        static const HIR::GenericParams emptyHrtbs;
        hrtbs = &emptyHrtbs;
    }
    // TODO: Sort the two types by "complexity" (most of the time long >= short)
    this->typeEqualities.insert(::std::make_pair(mv$(long_ty), CachedEquality{hrtbs->clone(), mv$(short_ty)}));
}

void TraitResolveCommon::prepIndexesAddTraitBound(const Span& sp, const ::HIR::GenericParams* outer_hrtbs, ::HIR::TypeRef type, ::HIR::TraitPath trait_path, bool addParents /*=true*/) {
    TRACE_FUNCTION_F(FMT_CB(os, if (outer_hrtbs) os << "for" << outer_hrtbs->fmt_args() << " ";) << type << " : " << trait_path);

    const auto boundConstness = trait_path.constness;
    auto get_or_add_trait_bound = [&](const HIR::GenericParams* hrbs, const HIR::GenericPath& generic_path) -> CachedBound& {
        auto it = ::std::find_if(traitBounds.begin(), traitBounds.end(), [&](const auto& entry) {
            const auto& boundType = entry.first.first;
            const auto& boundTrait = entry.first.second;
            return (boundType == type || boundType->equalsIgnoringRegions(type))
                && boundTrait.equalsIgnoringRegions(generic_path);
        });
        if (it != traitBounds.end()) {
            DEBUG("[get_or_add_trait_bound] Existing " << FMT_CB(os, if (hrbs) os << "for" << hrbs->fmt_args() << " ";) << generic_path);
            if (boundConstness == HIR::BoundConstness::Always
                || (boundConstness == HIR::BoundConstness::Maybe && it->second.constness == HIR::BoundConstness::Never)) {
                it->second.constness = boundConstness;
            }
            return it->second;
        }
        DEBUG("[get_or_add_trait_bound] Add " << FMT_CB(os, if (outer_hrtbs) os << "for" << outer_hrtbs->fmt_args() << " ";) << " ?: " << FMT_CB(os, if (hrbs) os << "for" << hrbs->fmt_args() << " ";) << generic_path);
        auto& rv = traitBounds[std::make_pair(type, generic_path.clone())];
        if (outer_hrtbs && !outer_hrtbs->is_empty()) {
            rv.hrbs = outer_hrtbs->clone();
        }
        if (hrbs && !hrbs->is_empty()) {
            rv.hrbs = hrbs->clone();
        }
        rv.trait_ptr = &crate.get_trait_by_path(sp, generic_path.mPath);
        rv.constness = boundConstness;
        return rv;
    };
    auto push_type = [&](const RcString& name, const HIR::GenericParams* hrbs, const HIR::TraitPath::AtyEqual& atye) {
        auto& b = get_or_add_trait_bound(hrbs, atye.source_trait);
        b.assoc.insert(std::make_pair(name, atye.clone()));
    };

    auto& trait_params = trait_path.mPath.mParams;
    auto monomorph = MonomorphStatePtr(crate.types, type, &trait_params, nullptr);

    const auto& trait = crate.get_trait_by_path(sp, trait_path.mPath.mPath);
#if 1
    while (trait_params.types.size() < trait.mParams.types.size()) {
        trait_params.types.push_back(monomorph.monomorph_type(sp, trait.mParams.types[trait_params.types.size()].defaultValue));
    }
#endif

    get_or_add_trait_bound(trait_path.hrtbs.get(), trait_path.mPath);

    for (const auto& tb : trait_path.typeBounds) {
        DEBUG("Equality (TB) - <" << type << " as " << tb.second.source_trait << ">::" << tb.first << " = " << tb.second);
        push_type(tb.first, trait_path.hrtbs.get(), tb.second);

        auto ty_l = crate.types.path(::HIR::Path(type, tb.second.source_trait.clone(), tb.first), ::HIR::TypePathBinding::make_Opaque({}));
        prepIndexesAddEquality(sp, trait_path.hrtbs.get(), ty_l, tb.second.type);
    }

    if (trait_path.hrtbs && !trait_path.hrtbs->is_empty()) {
        if (outer_hrtbs && !outer_hrtbs->is_empty()) {
            TODO(sp, "Handle multiple layers of HRTBs");
        }
        outer_hrtbs = trait_path.hrtbs.get();
    }

    // ATY Trait bounds
    for (const auto& tb : trait_path.traitBounds) {
        for (const auto& trait : tb.second.traits) {
            auto ty_l = crate.types.path(::HIR::Path(type, tb.second.source_trait.clone(), tb.first), ::HIR::TypePathBinding::make_Opaque({}));
            DEBUG("Bound (TB) - <" << type << " as " << tb.second.source_trait << ">::" << tb.first << " : " << trait);
            prepIndexesAddTraitBound(sp, outer_hrtbs, std::move(ty_l), trait.clone());
        }
    }

    for (const auto& aTy : trait.types) {
        // if no bounds, don't bother making the type
        if (aTy.second.traitBounds.empty()) {
            continue;
        }

        if (aTy.second.generics.is_generic() || !aTy.second.generics.is_empty()) {
            continue;
        }
        ASSERT_BUG(sp, !aTy.second.generics.is_generic(), "prep_indexes__add_trait_bound: Handle type generic ATYs - " << aTy.first << aTy.second.generics.fmt_args() << " in " << trait_path);
        auto ty_a = crate.types.path(
            // TODO: Empty params works for now, as there's no type generics (yet)
            ::HIR::Path(type, trait_path.mPath.clone(), aTy.first, aTy.second.generics.make_empty_params(true)),
            ::HIR::TypePathBinding::make_Opaque({})
        );
        monomorph.pp_method = &ty_a->as_Path().path.mData.as_UfcsKnown().params;

        for (const auto& aTyB : aTy.second.traitBounds) {
            DEBUG("(Assoc) " << aTyB);
            auto trait_mono = monomorph.monomorph_traitpath(sp, aTyB, false);
            for (auto& tb : trait_mono.typeBounds) {
                DEBUG("Equality (ATB) - <" << ty_a << " as " << tb.second.source_trait << ">::" << tb.first << " = " << tb.second);

                auto ty_l = crate.types.path(::HIR::Path(ty_a, tb.second.source_trait.clone(), tb.first, tb.second.atyParams.clone()), ::HIR::TypePathBinding::make_Opaque({}));

                if (outer_hrtbs && outer_hrtbs->is_empty()) {
                    outer_hrtbs = nullptr;
                }

                // TODO: what if `trait_mono` has HRLs too?
                if (outer_hrtbs && trait_mono.hrtbs) {
                    TODO(sp, "Double-layerd HRLs - outer=" << outer_hrtbs->fmt_args() << " and inner=" << trait_mono.hrtbs->fmt_args());
                }
                auto* inner_hrtbs = outer_hrtbs ? outer_hrtbs : aTyB.hrtbs.get();
                prepIndexesAddEquality(sp, inner_hrtbs, mv$(ty_l), std::move(tb.second.type));
            }
        }

        monomorph.pp_method = nullptr;
    }

    for (const auto& st : trait.allParentTraits) {
        DEBUG("(Parent) " << st);
        prepIndexesAddTraitBound(sp, outer_hrtbs, type, monomorph.monomorph_traitpath(sp, st, false), /*add_parents*/ false);
    }
}

/// Obtain the type for a given constant parameter
const ::HIR::TypeData* TraitResolveCommon::get_const_param_type(const Span& sp, unsigned binding) const {
    const HIR::GenericParams* p;
    switch (binding >> 8) {
        case 0: // impl level
            p = implGenerics;
            break;
        case 1: // method level
            p = itemGenerics;
            break;
        default:
            TODO(sp, "Typecheck const generics - look up the type");
    }
    auto slot = binding & 0xFF;
    if (!p) {
        if (implGenerics) {
            DEBUG("Impl: " << implGenerics->fmt_args());
        }
        if (itemGenerics) {
            DEBUG("Item: " << itemGenerics->fmt_args());
        }
    }
    ASSERT_BUG(sp, p, "No generic list for " << (binding >> 8) << ":" << slot);
    ASSERT_BUG(sp, slot < p->values.size(), "Generic param index out of range");
    return p->values.at(slot).mType;
}

::std::ostream& operator<<(::std::ostream& s, const TraitResolveCommon::CachedEquality& x) {
    if (!x.hrbs.is_empty()) {
        s << "for" << x.hrbs.fmt_args() << " ";
    }
    s << x.ty;
    return s;
}

Ordering TraitResolveCommon::CachedBoundCmp::ord(const key_t& a, const ref_t& b) const {
    ORD(a.first, b.first);
    ORD(a.second, b.second);
    return OrdEqual;
}
Ordering TraitResolveCommon::CachedBoundCmp::ord(const key_t& a, const ref_sp_t& b) const {
    ORD(a.first, b.first);
    ORD(a.second.mPath, b.second);
    return OrdEqual;
}
// 1.90 (well, added earlier)

TraitResolveCommon::TraitResolveCommon(const ::HIR::Crate& crate)
    : crate(crate)
    , implGenerics(nullptr)
    , itemGenerics(nullptr) {
    mLangCopy = crate.get_lang_item_path_opt("copy");
    mLangClone = crate.get_lang_item_path_opt("clone");
    mLangDrop = crate.get_lang_item_path_opt("drop");
    mLangSized = crate.get_lang_item_path_opt("sized");
    mLangUnsize = crate.get_lang_item_path_opt("unsize");
    mLangFn = crate.get_lang_item_path_opt("fn");
    mLangFnMut = crate.get_lang_item_path_opt("fn_mut");
    mLangFnOnce = crate.get_lang_item_path_opt("fn_once");
    mLangBox = crate.get_lang_item_path_opt("owned_box");
    mLangPhantomData = crate.get_lang_item_path_opt("phantom_data");
    mLangGenerator = crate.get_lang_item_path_opt("coroutine");
    mLangDiscriminantKind = crate.get_lang_item_path_opt("discriminant_kind");
    mLangPointee = crate.get_lang_item_path_opt("pointee_trait");
    mLangDynMetadata = crate.get_lang_item_path_opt("dyn_metadata");
    mLangPointeeSized = crate.get_lang_item_path_opt("pointee_sized");
    mLangMetaSized = crate.get_lang_item_path_opt("meta_sized");
    mLangDestruct = crate.get_lang_item_path_opt("destruct");
    mLangFuture = crate.get_lang_item_path_opt("future_trait");
}
const ::HIR::GenericParams& TraitResolveCommon::impl_generics() const {
    static ::HIR::GenericParams empty;
    return implGenerics ? *implGenerics : empty;
}
const ::HIR::GenericParams& TraitResolveCommon::item_generics() const {
    static ::HIR::GenericParams empty;
    return itemGenerics ? *itemGenerics : empty;
}
/// Iterate over in-scope bounds (function then type)
bool TraitResolveCommon::iterate_bounds(::std::function<bool(const ::HIR::GenericBound&)> cb) const {
    const ::HIR::GenericParams* v[2] = {itemGenerics, implGenerics};
    for (auto p : v) {
        if (!p) {
            continue;
        }
        for (const auto& b : p->bounds) {
            if (cb(b)) {
                return true;
            }
        }
    }
    return false;
}
