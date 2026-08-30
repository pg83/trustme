#include "hir_typeck_resolve_common.h"

#include "output.h"
#include "wire_board.h"
#include "hir_typeck_monomorph.h"

using namespace stl;

void TraitResolveCommon::prepIndexes(const Span& sp) {
    TRACE_FUNCTION_F(StringView(""));
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
                DEBUG(StringView("Equality - ") << be.type << StringView(" = ") << be.otherType);
                this->prepIndexesAddEquality(sp, be.type, be.otherType);
                break;
            }
        }
        return false;
    });
    DEBUG(traitBounds.size() << StringView(" trait bounds"));
}

void TraitResolveCommon::prepIndexesAddEquality(const Span& sp, const HIRTypeData* longTy, const HIRTypeData* shortTy) {
    DEBUG(StringView("ADD ") << longTy << StringView(" => ") << shortTy);
    // TODO: Sort the two types by "complexity" (most of the time long >= short)
    this->typeEqualities.insert(std::make_pair(mv$(longTy), CachedEquality{mv$(shortTy)}));
}

void TraitResolveCommon::prepIndexesAddTraitBound(const Span& sp, const HIRTypeData* type, HIRTraitPath traitPath, bool addParents /*=true*/) {
    TRACE_FUNCTION_F(type << StringView(" : ") << traitPath);
    const auto boundConstness = traitPath.constness;
    auto getOrAddTraitBound = [&](const HIRGenericPath& genericPath) -> CachedBound& {
        auto it = std::find_if(traitBounds.begin(), traitBounds.end(), [&](const auto& entry) {
            const auto& boundType = entry.first.first;
            const auto& boundTrait = entry.first.second;
            return (boundType == type || boundType->equalsIgnoringRegions(type)) && boundTrait.equalsIgnoringRegions(genericPath);
        });
        if (it != traitBounds.end()) {
            DEBUG(StringView("[get_or_add_trait_bound] Existing ") << genericPath);
            if (boundConstness == HIRBoundConstness::Always || (boundConstness == HIRBoundConstness::Maybe && it->second.constness == HIRBoundConstness::Never)) {
                it->second.constness = boundConstness;
            }
            return it->second;
        }
        DEBUG(StringView("[get_or_add_trait_bound] Add ") << genericPath);
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
        DEBUG(StringView("Equality (TB) - <") << type << StringView(" as ") << tb.second.sourceTrait << StringView(">::") << tb.first << StringView(" = ") << tb.second);
        pushType(tb.first, tb.second);

        auto tyL = crate.types.path(HIRPath(type, tb.second.sourceTrait.clone(), tb.first, tb.second.atyParams.clone()), HIRTypePathBinding::make_Opaque({}));
        prepIndexesAddEquality(sp, tyL, tb.second.type);
    }

    for (const auto& tb : traitPath.traitBounds) {
        for (const auto& trait : tb.second.traits) {
            auto tyL = crate.types.path(HIRPath(type, tb.second.sourceTrait.clone(), tb.first, tb.second.atyParams.clone()), HIRTypePathBinding::make_Opaque({}));
            DEBUG(StringView("Bound (TB) - <") << type << StringView(" as ") << tb.second.sourceTrait << StringView(">::") << tb.first << StringView(" : ") << trait);
            prepIndexesAddTraitBound(sp, std::move(tyL), trait.clone());
        }
    }

    for (const auto& aTy : trait.types) {
        if (aTy.second.traitBounds.empty()) {
            continue;
        }

        if (aTy.second.generics.isGeneric() || !aTy.second.generics.isEmpty()) {
            continue;
        }
        ASSERT_BUG(sp, !aTy.second.generics.isGeneric(), StringView("prep_indexes__add_trait_bound: Handle type generic ATYs - ") << aTy.first << aTy.second.generics.fmtArgs() << StringView(" in ") << traitPath);
        auto tyA = crate.types.path(
            // TODO: Empty params works for now, as there's no type generics (yet)
            HIRPath(type, traitPath.path.clone(), aTy.first, HIRPathParams()),
            HIRTypePathBinding::make_Opaque({})
        );
        monomorph.ppMethod = &tyA->as_Path().path.data.as_UfcsKnown().params;

        for (const auto& aTyB : aTy.second.traitBounds) {
            DEBUG(StringView("(Assoc) ") << aTyB);
            auto traitMono = monomorph.monomorphTraitpath(sp, aTyB, false);
            for (auto& tb : traitMono.typeBounds) {
                DEBUG(StringView("Equality (ATB) - <") << tyA << StringView(" as ") << tb.second.sourceTrait << StringView(">::") << tb.first << StringView(" = ") << tb.second);
                auto tyL = crate.types.path(HIRPath(tyA, tb.second.sourceTrait.clone(), tb.first, tb.second.atyParams.clone()), HIRTypePathBinding::make_Opaque({}));

                prepIndexesAddEquality(sp, mv$(tyL), std::move(tb.second.type));
            }
        }

        monomorph.ppMethod = nullptr;
    }

    for (const auto& st : trait.allParentTraits) {
        DEBUG(StringView("(Parent) ") << st);
        prepIndexesAddTraitBound(sp, type, monomorph.monomorphTraitpath(sp, st, false), /*add_parents*/ false);
    }
}

const HIRTypeData* TraitResolveCommon::getConstParamType(const Span& sp, unsigned binding) const {
    const HIRGenericParams* p;
    switch (binding >> 8) {
        case 0:
            p = implGenerics_;
            break;
        case 1:
            p = itemGenerics_;
            break;
        default:
            TODO(sp, StringView("Typecheck const generics - look up the type"));
    }
    auto slot = binding & 0xFF;
    ASSERT_BUG(sp, p, StringView("No generic list for ") << (binding >> 8) << StringView(":") << slot);
    ASSERT_BUG(sp, slot < p->values.size(), StringView("Generic param index out of range"));
    return p->values.at(slot).type;
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

template <>
void stl::output<ZeroCopyOutput, TraitResolveCommon::CachedEquality>(ZeroCopyOutput& s, TraitResolveCommon::CachedEquality x) {
    s << x.ty;
    return;
}

template <>
void stl::output<ZeroCopyOutput, std::pair<const HIRTypeData* const, TraitResolveCommon::CachedEquality>>(ZeroCopyOutput& out, std::pair<const HIRTypeData* const, TraitResolveCommon::CachedEquality> value) {
    out << value.first << StringView(": ") << value.second;
}

template <>
void stl::output<ZeroCopyOutput, std::map<const HIRTypeData*, TraitResolveCommon::CachedEquality, HIRTypeUidOrder>>(ZeroCopyOutput& out, const std::map<const HIRTypeData*, TraitResolveCommon::CachedEquality, HIRTypeUidOrder>& values) {
    outCont(out, values);
}
