#include "hir_typeck_resolve_common.h"

#include "output.h"
#include "wire_board.h"
#include "hir_typeck_monomorph.h"
#include "hir_type.h"
#include "common.h"

using namespace stl;

namespace {
    size_t mixHash(size_t state, size_t value) {
        return state ^ (value + 0x9e3779b97f4a7c15ULL + (state << 6) + (state >> 2));
    }

    size_t hashBound(const HIRGenericBound& bound) {
        size_t h = static_cast<size_t>(bound.tag());
        switch (bound.tag()) {
            case HIRGenericBound::TAG_TraitBound: {
                auto& e = bound.as_TraitBound();
                h = mixHash(h, reinterpret_cast<uintptr_t>(e.type));
                h = mixHash(h, hirTraitPathHash(e.trait));
                break;
            }
            case HIRGenericBound::TAG_TypeEquality: {
                auto& e = bound.as_TypeEquality();
                h = mixHash(h, reinterpret_cast<uintptr_t>(e.type));
                h = mixHash(h, reinterpret_cast<uintptr_t>(e.otherType));
                break;
            }
        }
        return h;
    }

    bool sameBound(const HIRGenericBound& a, const HIRGenericBound& b) {
        if (a.tag() != b.tag()) {
            return false;
        }
        switch (a.tag()) {
            case HIRGenericBound::TAG_TraitBound: {
                auto& ae = a.as_TraitBound();
                auto& be = b.as_TraitBound();
                return ae.type == be.type && hirTraitPathIdentical(ae.trait, be.trait);
            }
            case HIRGenericBound::TAG_TypeEquality: {
                auto& ae = a.as_TypeEquality();
                auto& be = b.as_TypeEquality();
                return ae.type == be.type && ae.otherType == be.otherType;
            }
        }
        UNREACHABLE();
    }

    void hashParams(size_t& h, const HIRGenericParams* params) {
        const size_t typeCount = params ? params->types.size() : 0;
        const size_t valueCount = params ? params->values.size() : 0;
        h = mixHash(h, typeCount);
        for (size_t i = 0; i < typeCount; i++) {
            h = mixHash(h, params->types[i].isSized ? 1 : 0);
        }
        h = mixHash(h, valueCount);
        for (size_t i = 0; i < valueCount; i++) {
            h = mixHash(h, reinterpret_cast<uintptr_t>(params->values[i].type));
        }
    }

    bool sameParams(const HIRGenericParams* params, const ThinVector<u8>& sized, const ThinVector<const HIRType*>& valueTypes) {
        const size_t typeCount = params ? params->types.size() : 0;
        const size_t valueCount = params ? params->values.size() : 0;
        if (sized.size() != typeCount || valueTypes.size() != valueCount) {
            return false;
        }
        for (size_t i = 0; i < typeCount; i++) {
            if ((params->types[i].isSized ? 1 : 0) != sized[i]) {
                return false;
            }
        }
        for (size_t i = 0; i < valueCount; i++) {
            if (params->values[i].type != valueTypes[i]) {
                return false;
            }
        }
        return true;
    }

    void cloneParams(const HIRGenericParams* params, ThinVector<u8>& sized, ThinVector<const HIRType*>& valueTypes) {
        if (!params) {
            return;
        }
        for (const auto& type : params->types) {
            sized.push_back(type.isSized ? 1 : 0);
        }
        for (const auto& value : params->values) {
            valueTypes.push_back(value.type);
        }
    }
}

void TraitResolveCommon::prepIndexes(const Span& sp) {
    TRACE_FUNCTION_F(StringView(""));
    if (auto* interner = wb.typingEnvironments; interner && interner->enabled()) {
        environment_ = interner->intern(*this, sp);
        return;
    }
    environment_ = nullptr;
    buildIndex(sp, localIndex_);
}

size_t TraitResolveCommon::environmentHash() const {
    size_t h = 0x5f3759df;
    this->iterateBounds([&](const HIRGenericBound& b) -> bool {
        h = mixHash(h, hashBound(b));
        return false;
    });
    hashParams(h, implGenerics_);
    hashParams(h, itemGenerics_);
    return h;
}

bool TraitResolveCommon::environmentMatches(const TypingEnvironment& environment) const {
    size_t i = 0;
    bool same = true;
    this->iterateBounds([&](const HIRGenericBound& b) -> bool {
        if (i >= environment.bounds.size() || !sameBound(b, environment.bounds[i])) {
            same = false;
            return true;
        }
        i++;
        return false;
    });
    return same && i == environment.bounds.size() && sameParams(implGenerics_, environment.implSized, environment.implValueTypes) && sameParams(itemGenerics_, environment.itemSized, environment.itemValueTypes);
}

void TraitResolveCommon::cloneEnvironmentInto(TypingEnvironment& environment) const {
    this->iterateBounds([&](const HIRGenericBound& b) -> bool {
        environment.bounds.emplace_back(b.clone());
        return false;
    });
    cloneParams(implGenerics_, environment.implSized, environment.implValueTypes);
    cloneParams(itemGenerics_, environment.itemSized, environment.itemValueTypes);
}

void TraitResolveCommon::buildIndex(const Span& sp, BoundIndex& index) const {
    index.typeEqualities.clear();
    index.traitBounds.clear();

    this->iterateBounds([&](const HIRGenericBound& b) -> bool {
        switch (b.tag()) {
            default:
                break;
            case HIRGenericBound::TAG_TraitBound: {
                auto& be = b.as_TraitBound();
                this->prepIndexesAddTraitBound(sp, index, be.type, be.trait.clone());
                break;
            }
            case HIRGenericBound::TAG_TypeEquality: {
                auto& be = b.as_TypeEquality();
                DEBUG(StringView("Equality - ") << be.type << StringView(" = ") << be.otherType);
                this->prepIndexesAddEquality(sp, index, be.type, be.otherType);
                break;
            }
        }
        return false;
    });
    DEBUG(index.traitBounds.size() << StringView(" trait bounds"));
}

void TraitResolveCommon::prepIndexesAddEquality(const Span& sp, BoundIndex& index, const HIRType* longTy, const HIRType* shortTy) const {
    DEBUG(StringView("ADD ") << longTy << StringView(" => ") << shortTy);
    // TODO: Sort the two types by "complexity" (most of the time long >= short)
    index.typeEqualities.insert(std::make_pair(mv$(longTy), CachedEquality{mv$(shortTy)}));
}

void TraitResolveCommon::prepIndexesAddTraitBound(const Span& sp, BoundIndex& index, const HIRType* type, HIRTraitPath traitPath, bool addParents /*=true*/) const {
    TRACE_FUNCTION_F(type << StringView(" : ") << traitPath);
    auto& traitBounds = index.traitBounds;
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
        prepIndexesAddEquality(sp, index, tyL, tb.second.type);
    }

    for (const auto& tb : traitPath.traitBounds) {
        for (const auto& trait : tb.second.traits) {
            auto tyL = crate.types.path(HIRPath(type, tb.second.sourceTrait.clone(), tb.first, tb.second.atyParams.clone()), HIRTypePathBinding::make_Opaque({}));
            DEBUG(StringView("Bound (TB) - <") << type << StringView(" as ") << tb.second.sourceTrait << StringView(">::") << tb.first << StringView(" : ") << trait);
            prepIndexesAddTraitBound(sp, index, std::move(tyL), trait.clone());
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

                prepIndexesAddEquality(sp, index, mv$(tyL), std::move(tb.second.type));
            }
        }

        monomorph.ppMethod = nullptr;
    }

    for (const auto& st : trait.allParentTraits) {
        DEBUG(StringView("(Parent) ") << st);
        prepIndexesAddTraitBound(sp, index, type, monomorph.monomorphTraitpath(sp, st, false), /*add_parents*/ false);
    }
}

const HIRType* TraitResolveCommon::getConstParamType(const Span& sp, unsigned binding) const {
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
void stl::output<ZeroCopyOutput, std::pair<const HIRType* const, TraitResolveCommon::CachedEquality>>(ZeroCopyOutput& out, std::pair<const HIRType* const, TraitResolveCommon::CachedEquality> value) {
    out << value.first << StringView(": ") << value.second;
}

template <>
void stl::output<ZeroCopyOutput, std::map<const HIRType*, TraitResolveCommon::CachedEquality, HIRTypeUidOrder>>(ZeroCopyOutput& out, const std::map<const HIRType*, TraitResolveCommon::CachedEquality, HIRTypeUidOrder>& values) {
    outCont(out, values);
}

TypingEnvironmentInterner::TypingEnvironmentInterner()
    : pool(ObjPool::fromMemory())
    , index(pool.mutPtr())
{
}

const TypingEnvironment* TypingEnvironmentInterner::intern(const TraitResolveCommon& resolve, const Span& sp) {
    const auto hash = resolve.environmentHash();
    auto* head = index.find(hash);
    for (auto* node = head ? *head : nullptr; node; node = node->next) {
        if (resolve.environmentMatches(*node)) {
            return node;
        }
    }
    auto* node = pool.mutPtr()->make<TypingEnvironment>(hash, pool.mutPtr(), head ? *head : nullptr);
    resolve.cloneEnvironmentInto(*node);
    resolve.buildIndex(sp, node->index);
    if (head) {
        *head = node;
    } else {
        index.insert(hash, node);
    }
    return node;
}

void TypeckCreateEnvironmentInterner(WireBoard& wb, ObjPool& pool) {
    BUG_ASSERT(!wb.typingEnvironments);
    wb.typingEnvironments = pool.make<TypingEnvironmentInterner>();
}
