#include "hir_typeck_resolve_common.h"

#include "wire_board.h"
#include "hir_typeck_monomorph.h" // MonomorphStatePtr

void TraitResolveCommon::prepIndexes(const Span& sp) {

    typeEqualities.clear();
    traitBounds.clear();

    this->iterateBounds([&](const HIRGenericBound& b) -> bool {
        switch (b.tag()) {
default:
            break;
            case HIRGenericBound::TAG_TraitBound: {
                auto& be = b.as_TraitBound();
                this->prepIndexesAddTraitBound(sp, be.type, be.trait.clone());
                break;
            }
            case HIRGenericBound::TAG_TypeEquality: {
                auto& be = b.as_TypeEquality();
                this->prepIndexesAddEquality(sp, be.type, be.otherType);
                break;
            }
        }
        return false;
    });
}

void TraitResolveCommon::prepIndexesAddEquality(const Span& sp, HIRTypeRef longTy, HIRTypeRef shortTy) {
    // TODO: Sort the two types by "complexity" (most of the time long >= short)
    this->typeEqualities.insert(::std::make_pair(mv$(longTy), CachedEquality{mv$(shortTy)}));
}

void TraitResolveCommon::prepIndexesAddTraitBound(const Span& sp, HIRTypeRef type, HIRTraitPath traitPath, bool addParents /*=true*/) {

    const auto boundConstness = traitPath.constness;
    auto getOrAddTraitBound = [&](const HIRGenericPath& genericPath) -> CachedBound& {
        auto it = ::std::find_if(traitBounds.begin(), traitBounds.end(), [&](const auto& entry) {
            const auto& boundType = entry.first.first;
            const auto& boundTrait = entry.first.second;
            return (boundType == type || boundType->equalsIgnoringRegions(type)) && boundTrait.equalsIgnoringRegions(genericPath);
        });
        if (it != traitBounds.end()) {
            if (boundConstness == HIRBoundConstness::Always || (boundConstness == HIRBoundConstness::Maybe && it->second.constness == HIRBoundConstness::Never)) {
                it->second.constness = boundConstness;
            }
            return it->second;
        }
        auto& rv = traitBounds[std::make_pair(type, genericPath.clone())];
        rv.traitPtr = &crate.getTraitByPath(sp, genericPath.path);
        rv.constness = boundConstness;
        return rv;
    };
    auto pushType = [&](const RcString& name, const HIRTraitPath::AtyEqual& atye) {
        auto& b = getOrAddTraitBound(atye.sourceTrait);
        b.assoc.insert(std::make_pair(name, atye.clone()));
    };

    auto& traitParams = traitPath.path.params;
    auto monomorph = MonomorphStatePtr(crate.types, type, &traitParams, nullptr);

    const auto& trait = crate.getTraitByPath(sp, traitPath.path.path);
    while (traitParams.types.size() < trait.params.types.size()) {
        traitParams.types.push_back(monomorph.monomorphType(sp, trait.params.types[traitParams.types.size()].defaultValue));
    }

    getOrAddTraitBound(traitPath.path);

    for (const auto& tb : traitPath.typeBounds) {
        pushType(tb.first, tb.second);

        auto tyL = crate.types.path(HIRPath(type, tb.second.sourceTrait.clone(), tb.first, tb.second.atyParams.clone()), HIRTypePathBinding::make_Opaque({}));
        prepIndexesAddEquality(sp, tyL, tb.second.type);
    }

    // ATY Trait bounds
    for (const auto& tb : traitPath.traitBounds) {
        for (const auto& trait : tb.second.traits) {
            auto tyL = crate.types.path(HIRPath(type, tb.second.sourceTrait.clone(), tb.first, tb.second.atyParams.clone()), HIRTypePathBinding::make_Opaque({}));
            prepIndexesAddTraitBound(sp, std::move(tyL), trait.clone());
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
            HIRPath(type, traitPath.path.clone(), aTy.first, HIRPathParams()),
            HIRTypePathBinding::make_Opaque({})
        );
        monomorph.ppMethod = &tyA->as_Path().path.data.as_UfcsKnown().params;

        for (const auto& aTyB : aTy.second.traitBounds) {
            auto traitMono = monomorph.monomorphTraitpath(sp, aTyB, false);
            for (auto& tb : traitMono.typeBounds) {

                auto tyL = crate.types.path(HIRPath(tyA, tb.second.sourceTrait.clone(), tb.first, tb.second.atyParams.clone()), HIRTypePathBinding::make_Opaque({}));

                prepIndexesAddEquality(sp, mv$(tyL), std::move(tb.second.type));
            }
        }

        monomorph.ppMethod = nullptr;
    }

    for (const auto& st : trait.allParentTraits) {
        prepIndexesAddTraitBound(sp, type, monomorph.monomorphTraitpath(sp, st, false), /*add_parents*/ false);
    }
}

/// Obtain the type for a given constant parameter
const HIRTypeData* TraitResolveCommon::getConstParamType(const Span& sp, unsigned binding) const {
    const HIRGenericParams* p;
    switch (binding >> 8) {
        case 0: // impl level
            p = implGenerics_;
            break;
        case 1: // method level
            p = itemGenerics_;
            break;
        default:
            TODO(sp, "Typecheck const generics - look up the type");
    }
    auto slot = binding & 0xFF;
    ASSERT_BUG(sp, p, "No generic list for " << (binding >> 8) << ":" << slot);
    ASSERT_BUG(sp, slot < p->values.size(), "Generic param index out of range");
    return p->values.at(slot).type;
}

::std::ostream& operator<<(::std::ostream& s, const TraitResolveCommon::CachedEquality& x) {
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
    ORD(a.second.path, b.second);
    return OrdEqual;
}

// 1.90 (well, added earlier)

TraitResolveCommon::TraitResolveCommon(const WireBoard& wb)
    : wb(wb)
    , crate(*wb.crate)
    , implGenerics_(nullptr)
    , itemGenerics_(nullptr)
{
}

const HIRGenericParams& TraitResolveCommon::implGenerics() const {
    return implGenerics_ ? *implGenerics_ : emptyGenerics_;
}

const HIRGenericParams& TraitResolveCommon::itemGenerics() const {
    return itemGenerics_ ? *itemGenerics_ : emptyGenerics_;
}

/// Iterate over in-scope bounds (function then type)
bool TraitResolveCommon::iterateBoundsCb(HIRGenericBoundCallback& cb) const {
    const HIRGenericParams* v[2] = {itemGenerics_, implGenerics_};
    for (auto p : v) {
        if (!p) {
            continue;
        }
        for (const auto& b : p->bounds) {
            if (cb.visit(b)) {
                return true;
            }
        }
    }
    return false;
}
