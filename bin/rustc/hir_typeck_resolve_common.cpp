#include "hir_typeck_resolve_common.h"

#include "wire_board.h"
#include "hir_typeck_monomorph.h" // MonomorphStatePtr

void TraitResolveCommon::prepIndexes(const Span& sp) {
    TRACE_FUNCTION_F("");

    if (mImplGenerics) {
        DEBUG("m_impl_generics = " << mImplGenerics->fmtArgs() << mImplGenerics->fmtBounds());
    }
    if (mItemGenerics) {
        DEBUG("m_item_generics = " << mItemGenerics->fmtArgs() << mItemGenerics->fmtBounds());
    }

    typeEqualities.clear();
    traitBounds.clear();

    this->iterateBounds([&](const HIRGenericBound& b) -> bool {
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
        DEBUG(tb.first.first << " : " << (!tb.second.hrbs.isEmpty() ? "for" : "") << tb.second.hrbs.fmtArgs() << tb.first.second << " - " << tb.second.assoc);
    }
}

void TraitResolveCommon::prepIndexesAddEquality(const Span& sp, const HIRGenericParams* hrtbs, HIRTypeRef longTy, HIRTypeRef shortTy) {
    DEBUG("ADD " << longTy << " => " << shortTy);
    if (!hrtbs) {
        static const HIRGenericParams emptyHrtbs;
        hrtbs = &emptyHrtbs;
    }
    // TODO: Sort the two types by "complexity" (most of the time long >= short)
    this->typeEqualities.insert(::std::make_pair(mv$(longTy), CachedEquality{hrtbs->clone(), mv$(shortTy)}));
}

void TraitResolveCommon::prepIndexesAddTraitBound(const Span& sp, const HIRGenericParams* outerHrtbs, HIRTypeRef type, HIRTraitPath traitPath, bool addParents /*=true*/) {
    TRACE_FUNCTION_F(FMT_CB(os, if (outerHrtbs) os << "for" << outerHrtbs->fmtArgs() << " ";) << type << " : " << traitPath);

    const auto boundConstness = traitPath.constness;
    auto getOrAddTraitBound = [&](const HIRGenericParams* hrbs, const HIRGenericPath& genericPath) -> CachedBound& {
        auto it = ::std::find_if(traitBounds.begin(), traitBounds.end(), [&](const auto& entry) {
            const auto& boundType = entry.first.first;
            const auto& boundTrait = entry.first.second;
            return (boundType == type || boundType->equalsIgnoringRegions(type)) && boundTrait.equalsIgnoringRegions(genericPath);
        });
        if (it != traitBounds.end()) {
            DEBUG("[get_or_add_trait_bound] Existing " << FMT_CB(os, if (hrbs) os << "for" << hrbs->fmtArgs() << " ";) << genericPath);
            if (boundConstness == HIRBoundConstness::Always || (boundConstness == HIRBoundConstness::Maybe && it->second.constness == HIRBoundConstness::Never)) {
                it->second.constness = boundConstness;
            }
            return it->second;
        }
        DEBUG("[get_or_add_trait_bound] Add " << FMT_CB(os, if (outerHrtbs) os << "for" << outerHrtbs->fmtArgs() << " ";) << " ?: " << FMT_CB(os, if (hrbs) os << "for" << hrbs->fmtArgs() << " ";) << genericPath);
        auto& rv = traitBounds[std::make_pair(type, genericPath.clone())];
        if (outerHrtbs && !outerHrtbs->isEmpty()) {
            rv.hrbs = outerHrtbs->clone();
        }
        if (hrbs && !hrbs->isEmpty()) {
            rv.hrbs = hrbs->clone();
        }
        rv.traitPtr = &crate.getTraitByPath(sp, genericPath.mPath);
        rv.constness = boundConstness;
        return rv;
    };
    auto pushType = [&](const RcString& name, const HIRGenericParams* hrbs, const HIRTraitPath::AtyEqual& atye) {
        auto& b = getOrAddTraitBound(hrbs, atye.sourceTrait);
        b.assoc.insert(std::make_pair(name, atye.clone()));
    };

    auto& traitParams = traitPath.mPath.mParams;
    auto monomorph = MonomorphStatePtr(crate.types, type, &traitParams, nullptr);

    const auto& trait = crate.getTraitByPath(sp, traitPath.mPath.mPath);
    while (traitParams.types.size() < trait.mParams.types.size()) {
        traitParams.types.push_back(monomorph.monomorphType(sp, trait.mParams.types[traitParams.types.size()].defaultValue));
    }

    getOrAddTraitBound(traitPath.hrtbs.get(), traitPath.mPath);

    for (const auto& tb : traitPath.typeBounds) {
        DEBUG("Equality (TB) - <" << type << " as " << tb.second.sourceTrait << ">::" << tb.first << " = " << tb.second);
        pushType(tb.first, traitPath.hrtbs.get(), tb.second);

        auto tyL = crate.types.path(HIRPath(type, tb.second.sourceTrait.clone(), tb.first), HIRTypePathBinding::make_Opaque({}));
        prepIndexesAddEquality(sp, traitPath.hrtbs.get(), tyL, tb.second.type);
    }

    if (traitPath.hrtbs && !traitPath.hrtbs->isEmpty()) {
        if (outerHrtbs && !outerHrtbs->isEmpty()) {
            TODO(sp, "Handle multiple layers of HRTBs");
        }
        outerHrtbs = traitPath.hrtbs.get();
    }

    // ATY Trait bounds
    for (const auto& tb : traitPath.traitBounds) {
        for (const auto& trait : tb.second.traits) {
            auto tyL = crate.types.path(HIRPath(type, tb.second.sourceTrait.clone(), tb.first), HIRTypePathBinding::make_Opaque({}));
            DEBUG("Bound (TB) - <" << type << " as " << tb.second.sourceTrait << ">::" << tb.first << " : " << trait);
            prepIndexesAddTraitBound(sp, outerHrtbs, std::move(tyL), trait.clone());
        }
    }

    for (const auto& aTy : trait.types) {
        // if no bounds, don't bother making the type
        if (aTy.second.traitBounds.empty()) {
            continue;
        }

        if (aTy.second.generics.isGeneric() || !aTy.second.generics.isEmpty()) {
            continue;
        }
        ASSERT_BUG(sp, !aTy.second.generics.isGeneric(), "prep_indexes__add_trait_bound: Handle type generic ATYs - " << aTy.first << aTy.second.generics.fmtArgs() << " in " << traitPath);
        auto tyA = crate.types.path(
            // TODO: Empty params works for now, as there's no type generics (yet)
            HIRPath(type, traitPath.mPath.clone(), aTy.first, HIRPathParams()),
            HIRTypePathBinding::make_Opaque({})
        );
        monomorph.ppMethod = &tyA->as_Path().path.mData.as_UfcsKnown().params;

        for (const auto& aTyB : aTy.second.traitBounds) {
            DEBUG("(Assoc) " << aTyB);
            auto traitMono = monomorph.monomorphTraitpath(sp, aTyB, false);
            for (auto& tb : traitMono.typeBounds) {
                DEBUG("Equality (ATB) - <" << tyA << " as " << tb.second.sourceTrait << ">::" << tb.first << " = " << tb.second);

                auto tyL = crate.types.path(HIRPath(tyA, tb.second.sourceTrait.clone(), tb.first, tb.second.atyParams.clone()), HIRTypePathBinding::make_Opaque({}));

                if (outerHrtbs && outerHrtbs->isEmpty()) {
                    outerHrtbs = nullptr;
                }

                // TODO: what if `trait_mono` has HRLs too?
                if (outerHrtbs && traitMono.hrtbs) {
                    TODO(sp, "Double-layerd HRLs - outer=" << outerHrtbs->fmtArgs() << " and inner=" << traitMono.hrtbs->fmtArgs());
                }
                auto* innerHrtbs = outerHrtbs ? outerHrtbs : aTyB.hrtbs.get();
                prepIndexesAddEquality(sp, innerHrtbs, mv$(tyL), std::move(tb.second.type));
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
const HIRTypeData* TraitResolveCommon::getConstParamType(const Span& sp, unsigned binding) const {
    const HIRGenericParams* p;
    switch (binding >> 8) {
        case 0: // impl level
            p = mImplGenerics;
            break;
        case 1: // method level
            p = mItemGenerics;
            break;
        default:
            TODO(sp, "Typecheck const generics - look up the type");
    }
    auto slot = binding & 0xFF;
    if (!p) {
        if (mImplGenerics) {
            DEBUG("Impl: " << mImplGenerics->fmtArgs());
        }
        if (mItemGenerics) {
            DEBUG("Item: " << mItemGenerics->fmtArgs());
        }
    }
    ASSERT_BUG(sp, p, "No generic list for " << (binding >> 8) << ":" << slot);
    ASSERT_BUG(sp, slot < p->values.size(), "Generic param index out of range");
    return p->values.at(slot).mType;
}

::std::ostream& operator<<(::std::ostream& s, const TraitResolveCommon::CachedEquality& x) {
    if (!x.hrbs.isEmpty()) {
        s << "for" << x.hrbs.fmtArgs() << " ";
    }
    s << x.ty;
    return s;
}

Ordering TraitResolveCommon::CachedBoundCmp::ord(const keyT& a, const refT& b) const {
    ORD(a.first, b.first);
    ORD(a.second, b.second);
    return OrdEqual;
}

Ordering TraitResolveCommon::CachedBoundCmp::ord(const keyT& a, const refSpT& b) const {
    ORD(a.first, b.first);
    ORD(a.second.mPath, b.second);
    return OrdEqual;
}

// 1.90 (well, added earlier)

TraitResolveCommon::TraitResolveCommon(const WireBoard& wb)
    : wb(wb)
    , crate(*wb.crate)
    , mImplGenerics(nullptr)
    , mItemGenerics(nullptr)
{
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

const HIRGenericParams& TraitResolveCommon::implGenerics() const {
    static HIRGenericParams empty;
    return mImplGenerics ? *mImplGenerics : empty;
}

const HIRGenericParams& TraitResolveCommon::itemGenerics() const {
    static HIRGenericParams empty;
    return mItemGenerics ? *mItemGenerics : empty;
}

/// Iterate over in-scope bounds (function then type)
bool TraitResolveCommon::iterateBounds(::std::function<bool(const HIRGenericBound&)> cb) const {
    const HIRGenericParams* v[2] = {mItemGenerics, mImplGenerics};
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
