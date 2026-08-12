#include "hir_typeck_resolve_common.h"
#include "hir_typeck_monomorph.h" // MonomorphStatePtr

void TraitResolveCommon::prepIndexes(const Span& sp) {
    TRACE_FUNCTION_F("");

    if (implGenerics) {
        DEBUG("m_impl_generics = " << implGenerics->fmtArgs() << implGenerics->fmtBounds());
    }
    if (itemGenerics) {
        DEBUG("m_item_generics = " << itemGenerics->fmtArgs() << itemGenerics->fmtBounds());
    }

    typeEqualities.clear();
    traitBounds.clear();

    this->iterateBounds([&](const HIR::GenericBound& b) -> bool {
        TU_MATCH_HDRA( (b), { )
        default:
            break;
            TU_ARMA(TraitBound, be) {
                this->prepIndexesAddTraitBound(sp, be.hrtbs.get(), be.type, be.trait.clone());
            }
            TU_ARMA(TypeEquality, be) {
                DEBUG("Equality - " << be.type << " = " << be.otherType);
                this->prepIndexesAddEquality(sp, nullptr, be.type, be.otherType);
            }
        }
        return false;
    });
    DEBUG(traitBounds.size() << " trait bounds");
    for (const auto& tb : traitBounds) {
        DEBUG(tb.first.first << " : " << (!tb.second.hrbs.is_empty() ? "for" : "") << tb.second.hrbs.fmtArgs() << tb.first.second << " - " << tb.second.assoc);
    }
}

void TraitResolveCommon::prepIndexesAddEquality(const Span& sp, const ::HIR::GenericParams* hrtbs, ::HIR::TypeRef longTy, ::HIR::TypeRef shortTy) {
    DEBUG("ADD " << longTy << " => " << shortTy);
    if (!hrtbs) {
        static const HIR::GenericParams emptyHrtbs;
        hrtbs = &emptyHrtbs;
    }
    // TODO: Sort the two types by "complexity" (most of the time long >= short)
    this->typeEqualities.insert(::std::make_pair(mv$(longTy), CachedEquality{hrtbs->clone(), mv$(shortTy)}));
}

void TraitResolveCommon::prepIndexesAddTraitBound(const Span& sp, const ::HIR::GenericParams* outerHrtbs, ::HIR::TypeRef type, ::HIR::TraitPath trait_path, bool addParents /*=true*/) {
    TRACE_FUNCTION_F(FMT_CB(os, if (outerHrtbs) os << "for" << outerHrtbs->fmtArgs() << " ";) << type << " : " << trait_path);

    const auto boundConstness = trait_path.constness;
    auto getOrAddTraitBound = [&](const HIR::GenericParams* hrbs, const HIR::GenericPath& genericPath) -> CachedBound& {
        auto it = ::std::find_if(traitBounds.begin(), traitBounds.end(), [&](const auto& entry) {
            const auto& boundType = entry.first.first;
            const auto& boundTrait = entry.first.second;
            return (boundType == type || boundType->equalsIgnoringRegions(type))
                && boundTrait.equalsIgnoringRegions(genericPath);
        });
        if (it != traitBounds.end()) {
            DEBUG("[get_or_add_trait_bound] Existing " << FMT_CB(os, if (hrbs) os << "for" << hrbs->fmtArgs() << " ";) << genericPath);
            if (boundConstness == HIR::BoundConstness::Always
                || (boundConstness == HIR::BoundConstness::Maybe && it->second.constness == HIR::BoundConstness::Never)) {
                it->second.constness = boundConstness;
            }
            return it->second;
        }
        DEBUG("[get_or_add_trait_bound] Add " << FMT_CB(os, if (outerHrtbs) os << "for" << outerHrtbs->fmtArgs() << " ";) << " ?: " << FMT_CB(os, if (hrbs) os << "for" << hrbs->fmtArgs() << " ";) << genericPath);
        auto& rv = traitBounds[std::make_pair(type, genericPath.clone())];
        if (outerHrtbs && !outerHrtbs->is_empty()) {
            rv.hrbs = outerHrtbs->clone();
        }
        if (hrbs && !hrbs->is_empty()) {
            rv.hrbs = hrbs->clone();
        }
        rv.trait_ptr = &crate.getTraitByPath(sp, genericPath.mPath);
        rv.constness = boundConstness;
        return rv;
    };
    auto pushType = [&](const RcString& name, const HIR::GenericParams* hrbs, const HIR::TraitPath::AtyEqual& atye) {
        auto& b = getOrAddTraitBound(hrbs, atye.sourceTrait);
        b.assoc.insert(std::make_pair(name, atye.clone()));
    };

    auto& traitParams = trait_path.mPath.mParams;
    auto monomorph = MonomorphStatePtr(crate.types, type, &traitParams, nullptr);

    const auto& trait = crate.getTraitByPath(sp, trait_path.mPath.mPath);
#if 1
    while (traitParams.types.size() < trait.mParams.types.size()) {
        traitParams.types.push_back(monomorph.monomorphType(sp, trait.mParams.types[traitParams.types.size()].defaultValue));
    }
#endif

    getOrAddTraitBound(trait_path.hrtbs.get(), trait_path.mPath);

    for (const auto& tb : trait_path.typeBounds) {
        DEBUG("Equality (TB) - <" << type << " as " << tb.second.sourceTrait << ">::" << tb.first << " = " << tb.second);
        pushType(tb.first, trait_path.hrtbs.get(), tb.second);

        auto ty_l = crate.types.path(::HIR::Path(type, tb.second.sourceTrait.clone(), tb.first), ::HIR::TypePathBinding::make_Opaque({}));
        prepIndexesAddEquality(sp, trait_path.hrtbs.get(), ty_l, tb.second.type);
    }

    if (trait_path.hrtbs && !trait_path.hrtbs->is_empty()) {
        if (outerHrtbs && !outerHrtbs->is_empty()) {
            TODO(sp, "Handle multiple layers of HRTBs");
        }
        outerHrtbs = trait_path.hrtbs.get();
    }

    // ATY Trait bounds
    for (const auto& tb : trait_path.traitBounds) {
        for (const auto& trait : tb.second.traits) {
            auto ty_l = crate.types.path(::HIR::Path(type, tb.second.sourceTrait.clone(), tb.first), ::HIR::TypePathBinding::make_Opaque({}));
            DEBUG("Bound (TB) - <" << type << " as " << tb.second.sourceTrait << ">::" << tb.first << " : " << trait);
            prepIndexesAddTraitBound(sp, outerHrtbs, std::move(ty_l), trait.clone());
        }
    }

    for (const auto& aTy : trait.types) {
        // if no bounds, don't bother making the type
        if (aTy.second.traitBounds.empty()) {
            continue;
        }

        if (aTy.second.generics.isGeneric() || !aTy.second.generics.is_empty()) {
            continue;
        }
        ASSERT_BUG(sp, !aTy.second.generics.isGeneric(), "prep_indexes__add_trait_bound: Handle type generic ATYs - " << aTy.first << aTy.second.generics.fmtArgs() << " in " << trait_path);
        auto ty_a = crate.types.path(
            // TODO: Empty params works for now, as there's no type generics (yet)
            ::HIR::Path(type, trait_path.mPath.clone(), aTy.first, aTy.second.generics.makeEmptyParams(true)),
            ::HIR::TypePathBinding::make_Opaque({})
        );
        monomorph.ppMethod = &ty_a->as_Path().path.mData.as_UfcsKnown().params;

        for (const auto& aTyB : aTy.second.traitBounds) {
            DEBUG("(Assoc) " << aTyB);
            auto traitMono = monomorph.monomorphTraitpath(sp, aTyB, false);
            for (auto& tb : traitMono.typeBounds) {
                DEBUG("Equality (ATB) - <" << ty_a << " as " << tb.second.sourceTrait << ">::" << tb.first << " = " << tb.second);

                auto ty_l = crate.types.path(::HIR::Path(ty_a, tb.second.sourceTrait.clone(), tb.first, tb.second.atyParams.clone()), ::HIR::TypePathBinding::make_Opaque({}));

                if (outerHrtbs && outerHrtbs->is_empty()) {
                    outerHrtbs = nullptr;
                }

                // TODO: what if `trait_mono` has HRLs too?
                if (outerHrtbs && traitMono.hrtbs) {
                    TODO(sp, "Double-layerd HRLs - outer=" << outerHrtbs->fmtArgs() << " and inner=" << traitMono.hrtbs->fmtArgs());
                }
                auto* innerHrtbs = outerHrtbs ? outerHrtbs : aTyB.hrtbs.get();
                prepIndexesAddEquality(sp, innerHrtbs, mv$(ty_l), std::move(tb.second.type));
            }
        }

        monomorph.ppMethod = nullptr;
    }

    for (const auto& st : trait.allParentTraits) {
        DEBUG("(Parent) " << st);
        prepIndexesAddTraitBound(sp, outerHrtbs, type, monomorph.monomorphTraitpath(sp, st, false), /*add_parents*/ false);
    }
}

/// Obtain the type for a given constant parameter
const ::HIR::TypeData* TraitResolveCommon::getConstParamType(const Span& sp, unsigned binding) const {
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
            DEBUG("Impl: " << implGenerics->fmtArgs());
        }
        if (itemGenerics) {
            DEBUG("Item: " << itemGenerics->fmtArgs());
        }
    }
    ASSERT_BUG(sp, p, "No generic list for " << (binding >> 8) << ":" << slot);
    ASSERT_BUG(sp, slot < p->values.size(), "Generic param index out of range");
    return p->values.at(slot).mType;
}

::std::ostream& operator<<(::std::ostream& s, const TraitResolveCommon::CachedEquality& x) {
    if (!x.hrbs.is_empty()) {
        s << "for" << x.hrbs.fmtArgs() << " ";
    }
    s << x.ty;
    return s;
}

Ordering TraitResolveCommon::CachedBoundCmp::ord(const key_t& a, const refT& b) const {
    ORD(a.first, b.first);
    ORD(a.second, b.second);
    return OrdEqual;
}
Ordering TraitResolveCommon::CachedBoundCmp::ord(const key_t& a, const refSpT& b) const {
    ORD(a.first, b.first);
    ORD(a.second.mPath, b.second);
    return OrdEqual;
}
// 1.90 (well, added earlier)

TraitResolveCommon::TraitResolveCommon(const ::HIR::Crate& crate)
    : crate(crate)
    , implGenerics(nullptr)
    , itemGenerics(nullptr) {
    mLangCopy = crate.getLangItemPathOpt("copy");
    mLangClone = crate.getLangItemPathOpt("clone");
    mLangDrop = crate.getLangItemPathOpt("drop");
    mLangSized = crate.getLangItemPathOpt("sized");
    mLangUnsize = crate.getLangItemPathOpt("unsize");
    mLangFn = crate.getLangItemPathOpt("fn");
    mLangFnMut = crate.getLangItemPathOpt("fn_mut");
    mLangFnOnce = crate.getLangItemPathOpt("fn_once");
    mLangBox = crate.getLangItemPathOpt("owned_box");
    mLangPhantomData = crate.getLangItemPathOpt("phantom_data");
    mLangGenerator = crate.getLangItemPathOpt("coroutine");
    mLangDiscriminantKind = crate.getLangItemPathOpt("discriminant_kind");
    mLangPointee = crate.getLangItemPathOpt("pointee_trait");
    mLangDynMetadata = crate.getLangItemPathOpt("dyn_metadata");
    mLangPointeeSized = crate.getLangItemPathOpt("pointee_sized");
    mLangMetaSized = crate.getLangItemPathOpt("meta_sized");
    mLangDestruct = crate.getLangItemPathOpt("destruct");
    mLangFuture = crate.getLangItemPathOpt("future_trait");
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
bool TraitResolveCommon::iterateBounds(::std::function<bool(const ::HIR::GenericBound&)> cb) const {
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
