#include "hir_typeck_static.h"

#include "hir_expr.h"
#include "settings.h"
#include "wire_board.h"
#include "hir_visitor.h"
#include "hir_item_path.h"
#include "hir_typeck_helpers.h"
#include "hir_conv_main_bindings.h"

#include <std/alg/defer.h>
#include <std/mem/obj_pool.h>

#include <algorithm>

using namespace stl;

namespace {
    bool specializationLookupNeedsResolution(const HIRTypeData* type, const HIRPathParams& params) {
        auto typeNeedsResolution = [](const HIRTypeData* inner) {
            return inner->hasTypeInfer() || inner->needsMonomorphisation() || inner->mayHaveAssociatedType();
        };

        if (typeNeedsResolution(type) || monomorphisePathparamsNeeded(params)) {
            return true;
        }
        for (const auto& inner : params.types) {
            if (typeNeedsResolution(inner)) {
                return true;
            }
        }
        return ::std::any_of(params.values.begin(), params.values.end(), [](const auto& value) {
            return value.is_Infer();
        });
    }
}

struct StaticTraitResolve::NextSolverBridge {
    HMTypeInferrence ivars;
    HIRSimplePath visibility;
    TraitResolution resolve_;

    explicit NextSolverBridge(const WireBoard& wb);

    bool findImpl(const Span& sp, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRSimplePath& trait, const HIRPathParams* params, const HIRTypeData* type, StaticImplCallback& callback);

    bool findValue(const Span& sp, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const char* valueName, StaticImplCallback& callback);

    bool normalize(const Span& sp, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRTypeData* projection, HIRTypeRef& output);

    bool typeIsCopy(const Span& sp, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRTypeData* type);
};

bool StaticTraitResolve::findImplCb(const Span& sp, const HIRSimplePath& traitPath, const HIRPathParams* traitParams, const HIRTypeData* type, StaticImplCallback& foundCb) const {
    if (traitPath.components().empty()) {
        return false;
    }
    if (const auto* path = type->opt_Path(); path && path->path.data.is_UfcsKnown()) {
        HIRTypeRef normalizedType = type;
        this->expandAssociatedTypes(sp, normalizedType);
        if (normalizedType != type) {
            return this->findImplCb(sp, traitPath, traitParams, normalizedType, foundCb);
        }
    }

    if (!nextSolver) {
        ASSERT_BUG(sp, crate.pool, "next-solver requires the crate object pool");
        nextSolver = crate.pool->make<NextSolverBridge>(this->wb);
    }
    return nextSolver->findImpl(sp, implGenerics_, itemGenerics_, traitPath, traitParams, type, foundCb);
}

namespace {
    // Generic-header matching is also used by inherent impl lookup.  Keep this
    // small collector independent of trait candidate selection: it only records
    // substitutions produced by HIR's structural matcher.
    struct GetParams: public HIRMatchGenerics {
        struct ParamsSet {
            Vector<u8> types;
            Vector<u8> values;
        };

        Span sp;
        HIRPathParams& implParams;
        ParamsSet& paramsSet;

        GetParams(Span sp, ObjPool& valuePool, const HIRGenericParams& implParamsDef, HIRPathParams& implParams, ParamsSet& paramsSet);

        HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType resolveCb) override;

        HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& value) override;
    };
}

bool StaticTraitResolve::findImplCheckCrateRawCb(const Span& sp, const HIRSimplePath& desTraitPath, const HIRPathParams* desTraitParams, const HIRTypeData* desType, const HIRGenericParams& implParamsDef, const HIRPathParams& implTraitParams, const HIRTypeData* implType, StaticImplMatchCallback& foundCb) const {
    auto cbIdent = HIRResolvePlaceholdersNop();

    // Cache the result of this function
    // 100% required for 1.90's librustc_session - "Trans Monomorph" took 20mins without that
    // The key is structural: the impl side by definition addresses, the
    // destination side by interned pointers; the variable desTraitParams
    // content is matched inside the (typically tiny) bucket.
    auto& cacheBucket = cachedImplChecks[ImplCheckKey{&implParamsDef, &implTraitParams, implType, desTraitPath.rawData(), desType}];
    {
        for (const auto& ent : cacheBucket) {
            if (ent.hasDesParams != (desTraitParams != nullptr)) {
                continue;
            }
            if (desTraitParams && ent.desParams != *desTraitParams) {
                continue;
            }
            return foundCb.visit(ent.implParams.clone(), ent.result);
        }
    }
    // TODO: What if `des_trait_params` already has impl placeholders?

    HIRPathParams implParams;
    GetParams::ParamsSet paramsSet;
    GetParams getParams{sp, *crate.pool, implParamsDef, implParams, paramsSet};

    auto match = implType->matchTestGenericsFuzz(sp, desType, cbIdent, getParams);

    struct BaseImplPlaceholderIdx {
        unsigned ty = 0;
        unsigned val = 0;
    } baseImplPlaceholderIdx;

    if (desTraitParams) {
        ASSERT_BUG(sp, desTraitParams->types.size() == implTraitParams.types.size(), "Size mismatch in arguments for " << desTraitPath << " - " << *desTraitParams << " and " << implTraitParams);
        match &= implTraitParams.matchTestGenericsFuzz(sp, *desTraitParams, cbIdent, getParams);

        unsigned maxImplIdxTy = 0;
        unsigned maxImplIdxVal = 0;
        // TODO: Get a generic visitor (running the same way as `Monomorphiser`)
        for (const auto& r : desTraitParams->types) {
            visitTyWith(r, [&](const HIRTypeData* t) -> bool {
                if (t->is_Generic() && t->as_Generic().isPlaceholder()) {
                    unsigned implIdx = t->as_Generic().idx();
                    maxImplIdxTy = ::std::max(maxImplIdxTy, implIdx);
                }
                // TODO: Path param lifetimes, etc
                return false;
            });
        }
        baseImplPlaceholderIdx.ty = maxImplIdxTy + 1;
        baseImplPlaceholderIdx.val = maxImplIdxVal + 1;

        size_t nPlaceholderTysNeeded = ::std::count(paramsSet.types.begin(), paramsSet.types.end(), false);
        size_t nPlaceholderValsNeeded = ::std::count(paramsSet.values.begin(), paramsSet.values.end(), false);
        if (nPlaceholderTysNeeded > 0) {
            ASSERT_BUG(sp, baseImplPlaceholderIdx.ty + implParams.types.size() <= 256, "Out of impl placeholder types");
        }
        if (nPlaceholderValsNeeded > 0) {
            ASSERT_BUG(sp, baseImplPlaceholderIdx.val + implParams.values.size() <= 256, "Out of impl placeholder values");
        }
    }
    if (match == HIRCompare::Unequal) {
        return false;
    }

    auto placeholderName = RcString::newInterned(FMT("impl_?_" << &implParamsDef));
    GetParams::ParamsSet placeholdersSet;
    HIRPathParams placeholders;
    for (unsigned int i = 0; i < implParams.types.size(); i++) {
        if (!paramsSet.types[i]) {
            if (placeholders.types.size() == 0) {
                placeholders.types.resize(implParams.types.size());
                placeholdersSet.types.zero(implParams.types.size());
            }
            placeholders.types[i] = crate.types.generic(placeholderName, 2 * 256 + baseImplPlaceholderIdx.ty + i);
        }
    }
    for (size_t i = 0; i < implParams.values.size(); i++) {
        if (!paramsSet.values[i]) {
            if (placeholders.values.size() == 0) {
                placeholders.values.resize(implParams.values.size());
                placeholdersSet.values.zero(implParams.values.size());
            }
            placeholders.values[i] = HIRConstGeneric::make_Generic(HIRGenericRef(placeholderName, 2 * 256 + baseImplPlaceholderIdx.val + i));
        }
    }

    struct Matcher: public HIRMatchGenerics, public Monomorphiser {
        Span sp;
        const HIRPathParams& implParams;
        const GetParams::ParamsSet& paramsSet;
        const BaseImplPlaceholderIdx& baseImplPlaceholderIdx;
        RcString placeholderName;
        HIRPathParams& placeholders;
        GetParams::ParamsSet& placeholdersSet;

        Matcher(HIRTypeInterner& types, Span sp, const HIRPathParams& implParams, const GetParams::ParamsSet& paramsSet, RcString placeholderName, const BaseImplPlaceholderIdx& baseImplPlaceholderIdx, HIRPathParams& placeholders, GetParams::ParamsSet& placeholdersSet)
            : HIRMatchGenerics(types.objectPool())
            , Monomorphiser(types)
            , sp(sp)
            , implParams(implParams)
            , paramsSet(paramsSet)
            , baseImplPlaceholderIdx(baseImplPlaceholderIdx)
            , placeholderName(placeholderName)
            , placeholders(placeholders)
            , placeholdersSet(placeholdersSet)
        {
        }

        HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType resolveCb) override {
            if (ty->is_Generic() && ty->as_Generic().binding == g.binding) {
                return HIRCompare::Equal;
            }
            if (g.isPlaceholder()) {
                if (g.idx() >= baseImplPlaceholderIdx.ty) {
                    auto i = g.idx() - baseImplPlaceholderIdx.ty;
                    ASSERT_BUG(sp, !paramsSet.types[i], "Placeholder to populated type returned. new " << ty << ", existing " << implParams.types[i]);
                    auto& ph = placeholders.types[i];
                    if (!placeholdersSet.types[i]) {
                        placeholdersSet.types.mut(i) = true;
                        ph = ty;
                        return HIRCompare::Equal;
                    } else if (ph == ty) {
                        return HIRCompare::Equal;
                    } else {
                        return ph->compareWithPlaceholders(sp, ty, resolveCb);
                        //TODO(sp, "[find_impl__check_crate_raw] Compare placeholder " << i << " " << ph << " == " << ty);
                    }
                } else {
                    return HIRCompare::Fuzzy;
                }
            } else {
                return HIRCompare::Unequal;
            }
        }

        HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& val) override {
            if (const auto* ge = val.opt_Generic()) {
                if (ge->binding == g.binding) {
                    return Equal;
                }
            }

            if (g.isPlaceholder()) {
                if (g.idx() >= baseImplPlaceholderIdx.val) {
                    auto i = g.idx() - baseImplPlaceholderIdx.val;
                    ASSERT_BUG(sp, !paramsSet.values[i], "Placeholder to populated value returned. new " << val << ", existing " << implParams.values[i]);
                    auto& ph = placeholders.values[i];
                    if (!placeholdersSet.values[i]) {
                        placeholdersSet.values.mut(i) = true;
                        ph = val.clone();
                        return HIRCompare::Equal;
                    } else if (ph == val) {
                        return HIRCompare::Equal;
                    } else {
                        TODO(sp, "[find_impl__check_crate_raw] Compare placeholder value " << i << " " << ph << " == " << val);
                    }
                } else {
                    return HIRCompare::Fuzzy;
                }
            }

            TODO(Span(), "Matcher::match_val " << g << " with " << val);
            return HIRCompare::Unequal;
        }

        HIRTypeRef getType(const Span& sp, const HIRGenericRef& ge) const override {
            if (ge.isSelf()) {
                // TODO: `impl_type` or `des_type`
                //TODO(sp, "[find_impl__check_crate_raw] Self - " << impl_type << " or " << des_type);
                TODO(sp, "get_type Self");
            }
            ASSERT_BUG(sp, !ge.isPlaceholder(), "[find_impl__check_crate_raw] Placeholder param seen - " << ge);
            if (paramsSet.types[ge.binding]) {
                return implParams.types.at(ge.binding);
            }
            return placeholders.types.at(ge.binding);
        }

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& val) const override {
            ASSERT_BUG(sp, val.binding < 256, "Generic value binding in " << val << " out of range (>=256)");
            ASSERT_BUG(sp, val.binding < implParams.values.size(), "Generic value binding in " << val << " out of range (>= " << implParams.values.size() << ")");
            if (paramsSet.values[val.binding]) {
                return implParams.values.at(val.binding).clone();
            }
            ASSERT_BUG(sp, placeholders.values.size() == implParams.values.size(), "Placeholder size mismatch: " << placeholders.values.size() << " != " << implParams.values.size() << " - value=" << implParams.values.at(val.binding));
            return placeholders.values.at(val.binding).clone();
        }
    };

    Matcher matcher{crate.types, sp, implParams, paramsSet, placeholderName, baseImplPlaceholderIdx, placeholders, placeholdersSet};

    // Bounds can infer impl parameters through associated type equalities. Keep
    // checking until those inferences stop changing the placeholder set, so the
    // result does not depend on the declaration order of the bounds.
    HIRPathParams previousPlaceholders;
    do {
        previousPlaceholders = placeholders.clone();
        for (const auto& bound : implParamsDef.bounds) {
            if (const auto* ep = bound.opt_TraitBound()) {
                const auto& e = *ep;

                auto bTyMono = matcher.monomorphType(sp, e.type);
                this->expandAssociatedTypes(sp, bTyMono);
                auto bTpMono = matcher.monomorphTraitpath(sp, e.trait, false);
                expandAssociatedTypesTp(sp, bTpMono);
                // HACK: If the type is '_', assume the bound passes
                if (bTyMono->is_Infer()) {
                    continue;
                }

                // TODO: This is extrememly inefficient (looks up the trait impl 1+N times)
                if (bTpMono.typeBounds.size() > 0) {
                    for (const auto& assocBound : bTpMono.typeBounds) {
                        // TODO: Can bounds have generic params (GATs)
                        const auto& atyName = assocBound.first;
                        const HIRTypeData* exp = assocBound.second.type;

                        // TODO: use `assoc_bound.second.source_trait`
                        HIRGenericPath atySrcTrait;
                        traitContainsType(sp, bTpMono.path, *e.trait.traitPtr, atyName.c_str(), atySrcTrait);

                        bool rv = false;
                        if (bTyMono->is_Generic() && bTyMono->as_Generic().isPlaceholder()) {
                            rv = true;
                        } else {
                            rv = this->findImpl(sp, atySrcTrait.path, atySrcTrait.params, bTyMono, [&](const ImplRef& impl, SolverCertainty) -> bool {
                                HIRTypeRef have = impl.getType(crate.types, atyName.c_str(), assocBound.second.atyParams);
                                if (have == HIRTypeRef()) {
                                    have = crate.types.path(HIRPath(impl.getImplType(crate.types), HIRGenericPath(atySrcTrait.path, impl.getTraitParams(crate.types)), atyName), HIRTypePathBinding::make_Unbound({}));
                                }
                                this->expandAssociatedTypes(sp, have);

                                auto cmp = exp->matchTestGenericsFuzz(sp, have, cbIdent, matcher);
                                if (cmp == HIRCompare::Unequal) {
                                }
                                return cmp != HIRCompare::Unequal;
                            });
                        }
                        if (!rv) {
                            return false;
                        }
                    }
                }

                // TODO: Detect if the associated type bound above is from directly the bounded trait, and skip this if it's the case
                //else
                {
                    bool rv = false;
                    if (bTyMono->is_Generic() && bTyMono->as_Generic().isPlaceholder()) {
                        rv = true;
                    } else {
                        rv = this->findImpl(sp, bTpMono.path.path, bTpMono.path.params, bTyMono, [&](const auto& impl, SolverCertainty) {
                            return true;
                        });
                    }
                    if (!rv && visitTyWith(bTyMono, [](const HIRTypeData* ty) {
                        return ty->is_Generic() && ty->as_Generic().isPlaceholder();
                    })) {
                        rv = true;
                    }
                    if (!rv && visitTraitPathTysWith(bTpMono, [](const HIRTypeData* ty) {
                        return ty->is_Generic() && ty->as_Generic().isPlaceholder();
                    })) {
                        rv = true;
                    }
                    if (!rv) {
                        return false;
                    }
                }
            }
            //else if( const auto* be
            else // bound.opt_TraitBound()
            {
                // Ignore
            }
        }
    } while (placeholders != previousPlaceholders);

    for (size_t i = 0; i < implParams.types.size(); i++) {
        if (!paramsSet.types[i]) {
            if (!placeholdersSet.types[i]) {
            }
            implParams.types[i] = std::move(placeholders.types[i]);
        }
    }

    assert(implParamsDef.types.size() == implParams.types.size());
    for (size_t i = 0; i < implParamsDef.types.size(); i++) {
        if (implParamsDef.types.at(i).isSized) {
            // An unresolved parameter has no known sizedness yet.  It used to be
            // represented by a default-constructed ASTType*; the interned type
            // model represents that state explicitly as Infer.
            if (!implParams.types[i]->is_Infer()) {
                if (!typeIsSized(sp, implParams.types[i])) {
                    return false;
                }
            }
        }
    }

    // TODO: Can this be cached?
    // - Needs to cache the result
    {
        cacheBucket.push_back(ImplCheckEntry{desTraitParams != nullptr, desTraitParams ? desTraitParams->clone() : HIRPathParams(), implParams.clone(), match});
    }
    return foundCb.visit(mv$(implParams), match);
}

const HIRTypeData* StaticTraitResolve::fixTraitDefaultReturn(const Span& sp, const HIRItemPath& path, const HIRTypeData* tpl, HIRTypeRef& tmp) const {
    const auto& topIp = path.getTopIp();
    if (topIp.ty && topIp.trait && topIp.ty == crate.types.self()) {
        auto prefix = FMT(ATY_PREFIX_ERASED << path.name << "_");
        const auto& trait = crate.getTraitByPath(sp, *topIp.trait);
        tmp = cloneTyWith(crate.types, sp, tpl, [&](const HIRTypeData* inner, HIRTypeRef& out) -> bool {
            if (const auto* typePath = inner->opt_Path()) {
                if (const auto* projection = typePath->path.data.opt_UfcsKnown()) {
                    if (projection->type == topIp.ty && projection->trait.path == *topIp.trait && std::strncmp(projection->item.c_str(), prefix.c_str(), prefix.size()) == 0) {
                        const auto& type = trait.types.at(projection->item);
                        if (type.hasDefault) {
                            out = type.defaultValue;
                            return true;
                        }
                    }
                }
            }
            return false;
        });
        return tmp;
    }
    return tpl;
}

void StaticTraitResolve::expandAssociatedTypes(const Span& sp, HIRTypeRef& input) const {
    input = this->expandAssociatedTypesInner(sp, input);
    while (reveal_ == OpaqueReveal::All && visitTyWith(input, [](const HIRTypeData* inner) {
        return inner->is_ErasedType();
    })) {
        this->revealOpaqueTypesShallow(sp, input);
        input = this->expandAssociatedTypesInner(sp, input);
    }
}

void StaticTraitResolve::revealOpaqueTypesShallow(const Span& sp, HIRTypeRef& input) const {
    struct Visitor: public HIRVisitor {
        const Span& sp;
        const StaticTraitResolve& resolve;
        bool clearOpaque = false;

        void revealOpaqueType(HIRTypeRef& type) {
            const auto& erased = type->as_ErasedType();
            HIRTypeRef revealed;

            switch (erased.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    const auto& functionOpaque = erased.inner.as_Fcn();
                    MonomorphState monomorph(resolve.hirCrate().types);
                    auto value = resolve.getValue(sp, functionOpaque.origin, monomorph);
                    if (value.is_NotYetKnown() && functionOpaque.origin.data.is_UfcsKnown()) {
                        const auto& path = functionOpaque.origin.data.as_UfcsKnown();
                        auto name = RcString::newInterned(FMT(ATY_PREFIX_ERASED << path.item << "_" << functionOpaque.index));
                        revealed = resolve.hirCrate().types.path(HIRPath(path.type, path.trait.clone(), name, path.params.clone()), {});
                    } else {
                        ASSERT_BUG(sp, value.is_Function(), "ErasedType with Fcn type doesn't point at a function: " << functionOpaque.origin << ": " << value.tagStr());
                        const auto& function = *value.as_Function();
                        if (functionOpaque.index >= function.code.erasedTypes.size()) {
                            resolve.hirCrate().getOrGenMir(resolve.board(), HIRItemPath(functionOpaque.origin), function);
                        }
                        ASSERT_BUG(sp, functionOpaque.index < function.code.erasedTypes.size(), "Erased type index out of range for " << functionOpaque.origin << " - " << functionOpaque.index << " >= " << function.code.erasedTypes.size());
                        revealed = monomorph.monomorphType(sp, function.code.erasedTypes[functionOpaque.index]);
                    }
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Alias: {
                    const auto& alias = erased.inner.as_Alias();
                    if (alias.inner->type == HIRTypeRef()) {
                        auto definers = resolve.hirCrate().opaqueTypeDefiners.find(alias.inner->path);
                        if (definers != resolve.hirCrate().opaqueTypeDefiners.end()) {
                            for (const auto& path : definers->second) {
                                MonomorphState monomorph(resolve.hirCrate().types);
                                auto value = resolve.getValue(sp, path, monomorph);
                                if (const auto* function = value.opt_Function()) {
                                    resolve.hirCrate().getOrGenMir(resolve.board(), HIRItemPath(path), **function);
                                }
                                if (alias.inner->type != HIRTypeRef()) {
                                    break;
                                }
                            }
                        }
                        if (alias.inner->type == HIRTypeRef()) {
                            ERROR(sp, E0000, "Erased type alias " << alias.inner->path << " never set");
                        }
                    }
                    revealed = MonomorphStatePtr(resolve.hirCrate().types, nullptr, &alias.params, nullptr).monomorphType(sp, alias.inner->type);
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Known:
                    revealed = erased.inner.as_Known();
                    break;
            }

            type = std::move(revealed);
        }

        Visitor(const Span& sp, const StaticTraitResolve& resolve)
            : HIRVisitor(nullptr, resolve.hirCrate().types)
            , sp(sp)
            , resolve(resolve)
        {
        }

        [[nodiscard]] HIRTypeRef visitType(HIRTypeRef type) override {
            auto savedClearOpaque = clearOpaque;
            clearOpaque = false;
            if (type->is_ErasedType()) {
                revealOpaqueType(type);
                type = visitType(type);
                clearOpaque = true;
            } else {
                type = HIRVisitor::visitType(type);
                if (clearOpaque && type->is_Path() && type->as_Path().binding.is_Opaque()) {
                    auto data = type->cloneData();
                    data.as_Path().binding = HIRTypePathBinding::make_Unbound({});
                    type = resolve.hirCrate().types.intern(std::move(data));
                }
            }
            clearOpaque |= savedClearOpaque;
            return type;
        }
    } visitor(sp, *this);

    input = visitor.visitType(input);
}

void StaticTraitResolve::revealOpaqueTypes(const Span& sp, HIRTypeRef& input) const {
    this->expandAssociatedTypes(sp, input);
    while (visitTyWith(input, [](const HIRTypeData* inner) {
        return inner->is_ErasedType();
    })) {
        this->revealOpaqueTypesShallow(sp, input);
        this->expandAssociatedTypes(sp, input);
    }
}

void StaticTraitResolve::revealOpaqueTypesPath(const Span& sp, HIRPath& input) const {
    auto revealParams = [&](HIRPathParams& params) {
        for (auto& type : params.types) {
            revealOpaqueTypes(sp, type);
        }
    };

    expandAssociatedTypesPath(sp, input);
    switch (input.data.tag()) {
        case HIRPathData::TAG_Generic:
            revealParams(input.data.as_Generic().params);
            break;
        case HIRPathData::TAG_UfcsInherent: {
            auto& path = input.data.as_UfcsInherent();
            revealOpaqueTypes(sp, path.type);
            revealParams(path.params);
            revealParams(path.implParams);
            break;
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& path = input.data.as_UfcsKnown();
            revealOpaqueTypes(sp, path.type);
            revealParams(path.trait.params);
            revealParams(path.params);
            break;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            auto& path = input.data.as_UfcsUnknown();
            revealOpaqueTypes(sp, path.type);
            revealParams(path.params);
            break;
        }
    }
    expandAssociatedTypesPath(sp, input);
}

void StaticTraitResolve::evaluateArraySize(const Span& sp, HIRArraySize& size) const {
    ConvertHIRConstantEvaluateArraySize(sp, this->wb, crate, HIRSimplePath(crate.crateName, {}), size);
}

void StaticTraitResolve::evaluateConstGeneric(const Span& sp, HIRConstGeneric& value) const {
    ConvertHIRConstantEvaluateConstGeneric(sp, this->wb, crate, value);
}

void StaticTraitResolve::evaluatePathParams(const Span& sp, HIRPathParams& params) const {
    for (auto& value : params.values) {
        evaluateConstGeneric(sp, value);
    }
}

void StaticTraitResolve::expandAssociatedTypesPath(const Span& sp, HIRPath& input) const {
    switch (input.data.tag()) {
        case HIRPathData::TAG_Generic:
            this->expandAssociatedTypesParams(sp, input.data.as_Generic().params);
            break;
        case HIRPathData::TAG_UfcsInherent: {
            auto& path = input.data.as_UfcsInherent();
            path.type = this->expandAssociatedTypesInner(sp, path.type);
            this->expandAssociatedTypesParams(sp, path.params);
            for (auto& argument : path.implParams.types) {
                argument = this->expandAssociatedTypesInner(sp, argument);
            }
            break;
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& path = input.data.as_UfcsKnown();
            path.type = this->expandAssociatedTypesInner(sp, path.type);
            this->expandAssociatedTypesParams(sp, path.trait.params);
            this->expandAssociatedTypesParams(sp, path.params);
            break;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            auto& path = input.data.as_UfcsUnknown();
            path.type = this->expandAssociatedTypesInner(sp, path.type);
            this->expandAssociatedTypesParams(sp, path.params);
            break;
        }
    }
}

bool StaticTraitResolve::expandAssociatedTypesSingle(const Span& sp, HIRTypeRef& input) const {
    if (input->is_Path()) {
        if (input->as_Path().path.data.is_UfcsInherent()) {
            return expandAssociatedTypesUfcsInherent(sp, input);
        }
        if (input->as_Path().path.data.is_UfcsKnown()) {
            return expandAssociatedTypesUfcsKnown(sp, input, /*recurse=*/false);
        }
    }
    return false;
}

bool StaticTraitResolve::typesEqualResolvingOpaque(const Span& sp, const HIRTypeData* left, const HIRTypeData* right) const {
    auto reveal = [&](HIRTypeRef type) {
        for (unsigned depth = 0; depth < 64; depth++) {
            bool replaced = false;
            auto next = cloneTyWith(crate.types, sp, type, [&](const HIRTypeData* candidate, HIRTypeRef& output) {
                const auto* erased = candidate->opt_ErasedType();
                const auto* alias = erased ? erased->inner.opt_Alias() : nullptr;
                if (!alias || !alias->inner->type) {
                    return false;
                }
                output = MonomorphStatePtr(crate.types, nullptr, &alias->params, nullptr).monomorphType(sp, alias->inner->type);
                replaced = true;
                return true;
            });
            type = next;
            if (!replaced) {
                return type;
            }
        }
        BUG(sp, "Cycle while revealing opaque type " << type);
    };

    const auto revealedLeft = reveal(left);
    const auto revealedRight = reveal(right);
    return revealedLeft == revealedRight || revealedLeft->equalsIgnoringRegions(revealedRight);
}

void StaticTraitResolve::expandAssociatedTypesParams(const Span& sp, HIRPathParams& params) const {
    for (auto& arg : params.types) {
        arg = this->expandAssociatedTypesInner(sp, arg);
    }
}

void StaticTraitResolve::expandAssociatedTypesTp(const Span& sp, HIRTraitPath& input) const {
    expandAssociatedTypesParams(sp, input.path.params);
    for (auto& arg : input.typeBounds) {
        this->expandAssociatedTypesParams(sp, arg.second.sourceTrait.params);
        arg.second.type = this->expandAssociatedTypesInner(sp, arg.second.type);
    }
    for (auto& arg : input.traitBounds) {
        this->expandAssociatedTypesParams(sp, arg.second.sourceTrait.params);
        for (auto& t : arg.second.traits) {
            this->expandAssociatedTypesTp(sp, t);
        }
    }
}

HIRTypeRef StaticTraitResolve::expandAssociatedTypesInner(const Span& sp, HIRTypeRef input) const {
    switch (input->tag()) {
        case HIRTypeData::TAG_Infer:
        case HIRTypeData::TAG_Diverge:
        case HIRTypeData::TAG_Primitive:
        case HIRTypeData::TAG_Generic:
        case HIRTypeData::TAG_NodeType:
            return input;
        case HIRTypeData::TAG_Path: {
            const auto& e = input->as_Path();
            switch (e.path.data.tag()) {
                case HIRPathData::TAG_Generic: {
                    const auto& e2 = e.path.data.as_Generic();
                    // The evaluation helpers only act on unevaluated values;
                    // types are folded with the usual first-change scan.
                    bool valueWork = false;
                    for (const auto& v : e2.params.values) {
                        if (v.is_Unevaluated()) {
                            valueWork = true;
                            break;
                        }
                    }
                    size_t tyIdx = e2.params.types.size();
                    HIRTypeRef nty = nullptr;
                    for (size_t i = 0; i < e2.params.types.size(); i++) {
                        nty = expandAssociatedTypesInner(sp, e2.params.types[i]);
                        if (nty != e2.params.types[i]) {
                            tyIdx = i;
                            break;
                        }
                    }
                    if (!valueWork && tyIdx == e2.params.types.size()) {
                        return input;
                    }
                    auto data = input->cloneData();
                    auto& ne2 = data.as_Path().path.data.as_Generic();
                    if (tyIdx < ne2.params.types.size()) {
                        ne2.params.types[tyIdx] = nty;
                        for (size_t j = tyIdx + 1; j < ne2.params.types.size(); j++) {
                            ne2.params.types[j] = expandAssociatedTypesInner(sp, ne2.params.types[j]);
                        }
                    }
                    if (valueWork) {
                        evaluatePathParams(sp, ne2.params);
                        ConvertHIRConstantEvaluateMethodParams(sp, this->wb, crate, data.as_Path().binding.getGenerics(), ne2.params);
                        expandAssociatedTypesParams(sp, ne2.params);
                    }
                    return crate.types.intern(mv$(data));
                }
                case HIRPathData::TAG_UfcsInherent: {
                    auto data = input->cloneData();
                    auto& e2 = data.as_Path().path.data.as_UfcsInherent();
                    e2.type = this->expandAssociatedTypesInner(sp, e2.type);
                    expandAssociatedTypesParams(sp, e2.params);
                    for (auto& arg : e2.implParams.types) {
                        arg = this->expandAssociatedTypesInner(sp, arg);
                    }
                    auto rv = crate.types.intern(mv$(data));
                    if (this->expandAssociatedTypesUfcsInherent(sp, rv)) {
                        rv = this->expandAssociatedTypesInner(sp, rv);
                    }
                    return rv;
                }
                case HIRPathData::TAG_UfcsKnown: {
                    // An opaque associated type is not a resolved type. It only records
                    // that an earlier normalization attempt couldn't make progress. In a
                    // later (static) context more bounds can be available, so retry it.
                    const bool wasUnbound = e.binding.is_Unbound();
                    const bool wasOpaque = e.binding.is_Opaque();
                    if (!wasUnbound && !wasOpaque) {
                        return input;
                    }

                    if (wasOpaque) {
                        auto rv = input;
                        this->expandAssociatedTypesUfcsKnown(sp, rv, false);
                        if (rv != input) {
                            rv = this->expandAssociatedTypesInner(sp, rv);
                        }
                        return rv;
                    }
                    auto it = atyCache.find(input);
                    if (it != atyCache.end()) {
                        return it->second;
                    }
                    auto rv = input;
                    this->expandAssociatedTypesUfcsKnown(sp, rv);
                    if (!(rv->is_Path() && rv->as_Path().binding.is_Opaque())) {
                        atyCache.insert(std::make_pair(input, rv));
                    }
                    return rv;
                }
                case HIRPathData::TAG_UfcsUnknown: {
                    auto data = input->cloneData();
                    auto& e2 = data.as_Path().path.data.as_UfcsUnknown();
                    e2.type = this->expandAssociatedTypesInner(sp, e2.type);
                    expandAssociatedTypesParams(sp, e2.params);
                    return crate.types.intern(mv$(data));
                }
            }
            return input;
        }
        case HIRTypeData::TAG_TraitObject: {
            auto data = input->cloneData();
            auto& e = data.as_TraitObject();
            expandAssociatedTypesTp(sp, e.trait);
            for (auto& m : e.markers) {
                expandAssociatedTypesParams(sp, m.params);
            }
            return crate.types.intern(mv$(data));
        }
        case HIRTypeData::TAG_ErasedType: {
            auto data = input->cloneData();
            auto& e = data.as_ErasedType();
            for (auto& trait : e.traits) {
                expandAssociatedTypesTp(sp, trait);
            }
            expandAssociatedTypesParams(sp, e.use);
            switch (e.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Known: {
                    auto& ee = e.inner.as_Known();
                    ee = expandAssociatedTypesInner(sp, ee);
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    auto& ee = e.inner.as_Fcn();
                    expandAssociatedTypesPath(sp, ee.origin);
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Alias: {
                    auto& ee = e.inner.as_Alias();
                    expandAssociatedTypesParams(sp, ee.params);
                    break;
                }
            }
            return crate.types.intern(mv$(data));
        }
        case HIRTypeData::TAG_Array: {
            const auto& e = input->as_Array();
            auto ninner = expandAssociatedTypesInner(sp, e.inner);
            bool sizeWork = e.size.is_Unevaluated();
            if (ninner == e.inner && !sizeWork) {
                return input;
            }
            auto data = input->cloneData();
            auto& ne = data.as_Array();
            ne.inner = ninner;
            if (sizeWork) {
                ConvertHIRConstantEvaluateArraySize(sp, this->wb, crate, HIRSimplePath(crate.crateName, {}), ne.size);
            }
            return crate.types.intern(mv$(data));
        }
        case HIRTypeData::TAG_Slice: {
            auto ninner = expandAssociatedTypesInner(sp, input->as_Slice().inner);
            if (ninner == input->as_Slice().inner) {
                return input;
            }
            auto data = input->cloneData();
            data.as_Slice().inner = ninner;
            return crate.types.intern(mv$(data));
        }
        case HIRTypeData::TAG_Pattern: {
            const auto& e = input->as_Pattern();
            auto ninner = expandAssociatedTypesInner(sp, e.inner);
            bool rangeWork = false;
            for (const auto& range : e.pattern.alternatives) {
                if ((range.hasStart && range.start.is_Unevaluated()) || (range.hasEnd && range.end.is_Unevaluated())) {
                    rangeWork = true;
                    break;
                }
            }
            if (ninner == e.inner && !rangeWork) {
                return input;
            }
            auto data = input->cloneData();
            auto& ne = data.as_Pattern();
            ne.inner = ninner;
            for (auto& range : ne.pattern.alternatives) {
                if (range.hasStart) {
                    ConvertHIRConstantEvaluateConstGeneric(sp, this->wb, crate, ne.inner, range.start);
                }
                if (range.hasEnd) {
                    ConvertHIRConstantEvaluateConstGeneric(sp, this->wb, crate, ne.inner, range.end);
                }
            }
            return crate.types.intern(mv$(data));
        }
        case HIRTypeData::TAG_Tuple: {
            const auto& e = input->as_Tuple();
            for (size_t i = 0; i < e.size(); i++) {
                auto nt = expandAssociatedTypesInner(sp, e[i]);
                if (nt != e[i]) {
                    auto data = input->cloneData();
                    auto& ne = data.as_Tuple();
                    ne[i] = nt;
                    for (size_t j = i + 1; j < ne.size(); j++) {
                        ne[j] = expandAssociatedTypesInner(sp, ne[j]);
                    }
                    return crate.types.intern(mv$(data));
                }
            }
            return input;
        }
        case HIRTypeData::TAG_Borrow: {
            auto ninner = expandAssociatedTypesInner(sp, input->as_Borrow().inner);
            if (ninner == input->as_Borrow().inner) {
                return input;
            }
            auto data = input->cloneData();
            data.as_Borrow().inner = ninner;
            return crate.types.intern(mv$(data));
        }
        case HIRTypeData::TAG_Pointer: {
            auto ninner = expandAssociatedTypesInner(sp, input->as_Pointer().inner);
            if (ninner == input->as_Pointer().inner) {
                return input;
            }
            auto data = input->cloneData();
            data.as_Pointer().inner = ninner;
            return crate.types.intern(mv$(data));
        }
        case HIRTypeData::TAG_NamedFunction: {
            auto data = input->cloneData();
            auto& e = data.as_NamedFunction();
            switch (e.path.data.tag()) {
                case HIRPathData::TAG_Generic: {
                    auto& e2 = e.path.data.as_Generic();
                    expandAssociatedTypesParams(sp, e2.params);
                    break;
                }
                case HIRPathData::TAG_UfcsInherent: {
                    auto& e2 = e.path.data.as_UfcsInherent();
                    e2.type = this->expandAssociatedTypesInner(sp, e2.type);
                    expandAssociatedTypesParams(sp, e2.params);
                    // TODO: impl params too?
                    for (auto& arg : e2.implParams.types) {
                        arg = this->expandAssociatedTypesInner(sp, arg);
                    }
                    break;
                }
                case HIRPathData::TAG_UfcsKnown: {
                    auto& e2 = e.path.data.as_UfcsKnown();
                    e2.type = this->expandAssociatedTypesInner(sp, e2.type);
                    expandAssociatedTypesParams(sp, e2.trait.params);
                    expandAssociatedTypesParams(sp, e2.params);
                    break;
                }
                case HIRPathData::TAG_UfcsUnknown: {
                    auto& e2 = e.path.data.as_UfcsUnknown();
                    e2.type = this->expandAssociatedTypesInner(sp, e2.type);
                    expandAssociatedTypesParams(sp, e2.params);
                    break;
                }
            }
            return crate.types.intern(mv$(data));
        }
        case HIRTypeData::TAG_Function: {
            const auto& e = input->as_Function();
            auto nret = expandAssociatedTypesInner(sp, e.rettype);
            size_t argIdx = e.argTypes.size();
            HIRTypeRef narg = nullptr;
            for (size_t i = 0; i < e.argTypes.size(); i++) {
                narg = expandAssociatedTypesInner(sp, e.argTypes[i]);
                if (narg != e.argTypes[i]) {
                    argIdx = i;
                    break;
                }
            }
            if (nret == e.rettype && argIdx == e.argTypes.size()) {
                return input;
            }
            auto data = input->cloneData();
            auto& ne = data.as_Function();
            ne.rettype = nret;
            if (argIdx < ne.argTypes.size()) {
                ne.argTypes[argIdx] = narg;
                for (size_t j = argIdx + 1; j < ne.argTypes.size(); j++) {
                    ne.argTypes[j] = expandAssociatedTypesInner(sp, ne.argTypes[j]);
                }
            }
            return crate.types.intern(mv$(data));
        }
    }
    return input;
}

bool StaticTraitResolve::expandAssociatedTypesUfcsInherent(const Span& sp, HIRTypeRef& input) const {
    ASSERT_BUG(sp, input->is_Path() && input->as_Path().path.data.is_UfcsInherent(), input);

    const auto& pe = input->as_Path().path.data.as_UfcsInherent();
    if (visitTyWith(pe.type, [](const HIRTypeData* type) {
        const auto* erased = type->opt_ErasedType();
        const auto* opaque = erased ? erased->inner.opt_Alias() : nullptr;
        return opaque && !opaque->inner->type;
    })) {
        return false;
    }
    const HIRTypeAlias* alias = nullptr;
    const HIRGenericParams* implParamsDef = nullptr;
    HIRPathParams implParams;
    HIRCompare bestMatch = HIRCompare::Unequal;
    const HIRPathParams noTraitParams;

    crate.findTypeImpls(pe.type, HIRResolvePlaceholdersNop(), [&](const auto& impl) {
        const auto itemIt = impl.types.find(pe.item);
        if (itemIt == impl.types.end()) {
            return false;
        }

        bool selected = false;
        this->findImplCheckCrateRaw(sp, HIRSimplePath(), nullptr, pe.type, impl.params, noTraitParams, impl.type, [&](HIRPathParams candidateParams, HIRCompare match) {
            if (match != HIRCompare::Unequal && (bestMatch == HIRCompare::Unequal || match == HIRCompare::Equal)) {
                alias = &itemIt->second.data;
                implParamsDef = &impl.params;
                implParams = mv$(candidateParams);
                bestMatch = match;
                selected = true;
            }
            return selected;
        });
        return selected && bestMatch == HIRCompare::Equal;
    });

    if (!alias) {
        return false;
    }

    ConvertHIRConstantEvaluateMethodParams(sp, this->wb, crate, implParamsDef, implParams);

    auto itemParams = pe.params.clone();
    if (itemParams.types.size() != alias->params.types.size() || itemParams.values.size() != alias->params.values.size()) {
        ERROR(sp, E0000, "Incorrect generic arguments for inherent associated type " << input);
    }
    ConvertHIRConstantEvaluateMethodParams(sp, this->wb, crate, &alias->params, itemParams);

    input = MonomorphStatePtr(crate.types, pe.type, &implParams, &itemParams).monomorphType(sp, alias->type);
    return true;
}

namespace {}

bool StaticTraitResolve::expandAssociatedTypesUfcsKnown(const Span& sp, HIRTypeRef& input, bool recurse /*=true*/) const {
    ASSERT_BUG(sp, input->is_Path() && input->as_Path().path.data.is_UfcsKnown(), input);

    auto data = input->cloneData();
    auto& projection = data.as_Path().path.data.as_UfcsKnown();
    projection.type = this->expandAssociatedTypesInner(sp, projection.type);
    for (auto& argument : projection.params.types) {
        argument = this->expandAssociatedTypesInner(sp, argument);
    }
    for (auto& argument : projection.trait.params.types) {
        argument = this->expandAssociatedTypesInner(sp, argument);
    }
    const auto& trait = crate.getTraitByPath(sp, projection.trait.path);
    ConvertHIRConstantEvaluateMethodParams(sp, this->wb, crate, &trait.params, projection.trait.params);
    input = crate.types.intern(::std::move(data));

    // A projection whose root is still an inference variable is retryable.
    // Marking it opaque would turn a temporary lack of information into a
    // permanent static-resolution decision.
    {
        const HIRTypeData* root = input;
        while (const auto* path = root->opt_Path()) {
            const auto* known = path->path.data.opt_UfcsKnown();
            if (!known) {
                break;
            }
            root = known->type;
        }
        if (root->is_Infer()) {
            return false;
        }
    }

    if (this->replaceEqualities(input)) {
        if (recurse) {
            input = this->expandAssociatedTypesInner(sp, input);
        }
        return true;
    }

    if (!nextSolver) {
        ASSERT_BUG(sp, crate.pool, "next-solver requires the crate object pool");
        nextSolver = crate.pool->make<NextSolverBridge>(this->wb);
    }
    HIRTypeRef output = nullptr;
    nextSolver->normalize(sp, implGenerics_, itemGenerics_, input, output);
    if (output != HIRTypeRef()) {
        input = ::std::move(output);
        if (recurse) {
            input = this->expandAssociatedTypesInner(sp, input);
        }
        return true;
    }

    auto opaque = input->cloneData();
    opaque.as_Path().binding = HIRTypePathBinding::make_Opaque({});
    input = crate.types.intern(::std::move(opaque));
    return false;
}

bool StaticTraitResolve::replaceEqualities(HIRTypeRef& input) const {
    const Span sp;
    // - Check if there's an alias for this opaque name
    auto a = ::std::find_if(typeEqualities.begin(), typeEqualities.end(), [&](const auto& entry) {
        return entry.first == input || entry.first->equalsIgnoringRegions(input);
    });
    if (a != typeEqualities.end()) {
        // HACK: Shouldn't need this, but works around some missing cases
        input = a->second.ty;
        return true;
    } else {
        return false;
    }
}

// -------------------------------------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------------------------------------

bool StaticTraitResolve::iterateAtyBoundsCb(const Span& sp, const HIRPath::Data::Data_UfcsKnown& pe, StaticTraitPathCallback& cb) const {
    const auto& traitRef = crate.getTraitByPath(sp, pe.trait.path);
    ASSERT_BUG(sp, traitRef.types.count(pe.item) != 0, "Trait " << pe.trait.path << " doesn't contain an associated type " << pe.item);
    const auto& atyDef = traitRef.types.find(pe.item)->second;

    for (const auto& bound : atyDef.traitBounds) {
        if (cb.visit(bound)) {
            return true;
        }
    }
    // Search `<Self as Trait>::Name` bounds on the trait itself
    for (const auto& bound : traitRef.params.bounds) {
        if (!bound.is_TraitBound()) {
            continue;
        }
        const auto& be = bound.as_TraitBound();

        if (!be.type->is_Path()) {
            continue;
        }
        if (!be.type->as_Path().binding.is_Opaque()) {
            continue;
        }

        const auto& beTypePe = be.type->as_Path().path.data.as_UfcsKnown();
        if (beTypePe.type != crate.types.self()) {
            continue;
        }
        if (beTypePe.trait.path != pe.trait.path) {
            continue;
        }
        if (beTypePe.item != pe.item) {
            continue;
        }

        if (cb.visit(be.trait)) {
            return true;
        }
    }

    return false;
}

// -------------------------------------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------------------------------------
bool StaticTraitResolve::findNamedTraitInTraitCb(const Span& sp, const HIRSimplePath& des, const HIRPathParams& desParams, const HIRTrait& traitPtr, const HIRSimplePath& traitPath, const HIRPathParams& pp, const HIRTypeData* targetType, StaticNamedTraitCallback& callback) const {
    if (pp.types.size() != traitPtr.params.types.size()) {
        BUG(sp, "Incorrect number of parameters for trait - " << traitPath << pp);
    }

    if (des == traitPath) {
        auto cmp = pp.compareWithPlaceholders(sp, desParams, HIRResolvePlaceholdersNop());
        if (cmp != HIRCompare::Unequal) {
            // Return an empty ATY list, this is valid because callers also check the input ATY list in the callback
            return callback.visit(pp, {});
        }
    }

    auto monomorph = MonomorphStatePtr(crate.types, targetType, &pp, nullptr);
    for (const auto& pt : traitPtr.allParentTraits) {
        auto ptMono = monomorph.monomorphTraitpath(sp, pt, false);
        this->expandAssociatedTypesTp(sp, ptMono);

        // TODO: When in pre-typecheck mode, this needs to be a fuzzy match (because there might be a UfcsUnknown in the
        // monomorphed version) OR, there may be placeholders
        if (pt.path.path == des) {
            auto cmp = ptMono.path.params.compareWithPlaceholders(sp, desParams, HIRResolvePlaceholdersNop());
            // pt_mono.m_path.m_params == des_params )
            if (cmp != HIRCompare::Unequal) {
                return callback.visit(ptMono.path.params, mv$(ptMono.typeBounds));
            }
        }
    }

    return false;
}

bool StaticTraitResolve::traitContainsType(const Span& sp, const HIRGenericPath& traitPath, const HIRTrait& traitPtr, const char* name, HIRGenericPath& outPath) const {
    auto it = traitPtr.types.find(name);
    if (it != traitPtr.types.end()) {
        outPath = traitPath.clone();
        return true;
    }

    auto tySelf = crate.types.self();
    auto monomorph = MonomorphStatePtr(crate.types, tySelf, &traitPath.params, nullptr);
    for (const auto& st : traitPtr.allParentTraits) {
        if (st.traitPtr->types.count(name)) {
            outPath.path = st.path.path;
            outPath.params = monomorph.monomorphPathParams(sp, st.path.params, false);
            return true;
        }
    }
    return false;
}

bool StaticTraitResolve::typeIsCopy(const Span& sp, const HIRTypeData* type) const {
    if (const auto it = copyCache.find(type); it != copyCache.end()) {
        return it->second;
    }

    if (!nextSolver) {
        ASSERT_BUG(sp, crate.pool, "next-solver requires the crate object pool");
        nextSolver = crate.pool->make<NextSolverBridge>(this->wb);
    }
    const bool proven = nextSolver->typeIsCopy(sp, implGenerics_, itemGenerics_, type);
    copyCache.insert(::std::make_pair(type, proven));
    return proven;
}

bool StaticTraitResolve::typeIsClone(const Span& sp, const HIRTypeData* type) const {
    if (const auto it = cloneCache.find(type); it != cloneCache.end()) {
        return it->second;
    }

    HIRPathParams params;
    bool proven = false;
    this->findImpl(sp, langClone(), &params, type, [&](ImplRef, SolverCertainty certainty) {
        proven = certainty == SolverCertainty::Proven;
        return proven;
    });
    cloneCache.insert(::std::make_pair(type, proven));
    return proven;
}

bool StaticTraitResolve::typeIsSized(const Span& sp, const HIRTypeData* type) const {
    HIRPathParams params;
    bool proven = false;
    this->findImpl(sp, langSized(), &params, type, [&](ImplRef, SolverCertainty certainty) {
        proven = certainty == SolverCertainty::Proven;
        return proven;
    });
    return proven;
}

bool StaticTraitResolve::typeIsImpossible(const Span& sp, const HIRTypeData* ty) const {
    switch ((*ty).tag()) {
        break;
        default:
            return false;
        case HIRTypeData::TAG_Diverge: {
            return true;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = (*ty).as_Path();
            switch (e.binding.tag()) {
                case HIRTypePathBinding::TAG_Unbound: {
                    // BUG?
                    return false;
                }
                case HIRTypePathBinding::TAG_Opaque: {
                    // TODO: This can only be with UfcsKnown, so check if the trait specifies ?Sized
                    return false;
                }
                case HIRTypePathBinding::TAG_Struct: {
                    auto& pbe = e.binding.as_Struct();
                    const auto& params = e.path.data.as_Generic().params;
                    const auto& str = *pbe;
                    switch (str.data.tag()) {
                        case HIRStructData::TAG_Unit: {
                            return false;
                        }
                        case HIRStructData::TAG_Tuple: {
                            auto& e = str.data.as_Tuple();
                            for (const auto& fld : e) {
                                HIRTypeRef tmp;
                                const auto& fieldTy = this->monomorphExpandOpt(sp, tmp, fld.ent, MonomorphStatePtr(crate.types, ty, &params, nullptr));
                                if (typeIsImpossible(sp, fieldTy)) {
                                    return true;
                                }
                            }
                            return false;
                        }
                        case HIRStructData::TAG_Named: {
                            auto& e = str.data.as_Named();
                            for (const auto& fld : e) {
                                HIRTypeRef tmp;
                                const auto& fieldTy = this->monomorphExpandOpt(sp, tmp, fld.ty, MonomorphStatePtr(crate.types, ty, &params, nullptr));
                                if (typeIsImpossible(sp, fieldTy)) {
                                    return true;
                                }
                            }
                            return false;
                        }
                    }
                    break;
                }
                case HIRTypePathBinding::TAG_Enum: {
                    auto& pbe = e.binding.as_Enum();
                    const auto& params = e.path.data.as_Generic().params;
                    switch (pbe->data.tag()) {
                        case HIREnumClass::TAG_Value: {
                            auto& e = pbe->data.as_Value();
                            return e.variants.size() == 0;
                        }
                        case HIREnumClass::TAG_Data: {
                            auto& e = pbe->data.as_Data();
                            // If all variants are impossible, then this type is impossible
                            for (const auto& fld : e) {
                                const auto& tpl = fld.type;
                                HIRTypeRef tmp;
                                const auto& fieldTy = this->monomorphExpandOpt(sp, tmp, tpl, MonomorphStatePtr(crate.types, ty, &params, nullptr));
                                // Not impossible, ergo the enum is possible
                                if (!typeIsImpossible(sp, fieldTy)) {
                                    return false;
                                }
                            }
                            return true;
                        }
                    }
                    TODO(sp, "type_is_impossible for enum " << ty);
                    break;
                }
                case HIRTypePathBinding::TAG_Union: {
                    // TODO: Check all variants? Or just one?
                    TODO(sp, "type_is_impossible for union " << ty);
                    break;
                }
                case HIRTypePathBinding::TAG_ExternType: {
                    // Extern types are possible, just not usable
                    return false;
                }
            }
            return true;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = (*ty).as_Borrow();
            return typeIsImpossible(sp, e.inner);
        }
        case HIRTypeData::TAG_Pointer: {
            return false;
        }
        case HIRTypeData::TAG_Function: {
            // TODO: Check all arguments?
            return true;
        }
        case HIRTypeData::TAG_Array: {
            auto& e = (*ty).as_Array();
            return typeIsImpossible(sp, e.inner);
        }
        case HIRTypeData::TAG_Slice: {
            auto& e = (*ty).as_Slice();
            return typeIsImpossible(sp, e.inner);
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = (*ty).as_Pattern();
            return typeIsImpossible(sp, e.inner);
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = (*ty).as_Tuple();
            for (const auto& ty : e) {
                if (typeIsImpossible(sp, ty)) {
                    return true;
                }
            }
            return false;
        }
    }
    UNREACHABLE();
}

bool StaticTraitResolve::canUnsize(const Span& sp, const HIRTypeData* dstTy, const HIRTypeData* srcTy) const {
    ASSERT_BUG(sp, !dstTy->is_Infer(), "_ seen after inferrence - " << dstTy);
    ASSERT_BUG(sp, !srcTy->is_Infer(), "_ seen after inferrence - " << srcTy);

    {
        if (dstTy == srcTy) {
            return true;
        }
    }

    auto ir = traitBounds.equal_range(std::make_pair(srcTy, std::ref(langUnsize())));
    for (auto it = ir.first; it != ir.second; ++it) {
        const auto& beDst = it->first.second.params.types.at(0);

        if (dstTy == beDst) {
            return HIRCompare::Equal;
        }
    }

    // Associated types, check the bounds in the trait.
    if (srcTy->is_Path() && srcTy->as_Path().path.data.is_UfcsKnown()) {
        const auto& pe = srcTy->as_Path().path.data.as_UfcsKnown();
        auto ms = MonomorphStatePtr(crate.types, pe.type, &pe.trait.params, nullptr);
        auto foundBound = this->iterateAtyBounds(sp, pe, [&](const HIRTraitPath& bound) {
            if (bound.path.path != langUnsize()) {
                return false;
            }
            const auto& beDstTpl = bound.path.params.types.at(0);
            HIRTypeRef tmpTy;
            const auto& beDst = ms.maybeMonomorphType(sp, tmpTy, beDstTpl);

            if (dstTy != beDst) {
                return false;
            }
            return true;
        });
        if (foundBound) {
            return true;
        }
    }

    // Struct<..., T, ...>: Unsize<Struct<..., U, ...>>
    if (dstTy->is_Path() && srcTy->is_Path()) {
        bool dstIsUnsizable = dstTy->as_Path().binding.is_Struct() && dstTy->as_Path().binding.as_Struct()->structMarkings.canUnsize;
        bool srcIsUnsizable = srcTy->as_Path().binding.is_Struct() && srcTy->as_Path().binding.as_Struct()->structMarkings.canUnsize;
        if (dstIsUnsizable && srcIsUnsizable) {
            const auto& str = *dstTy->as_Path().binding.as_Struct();
            const auto& dstGp = dstTy->as_Path().path.data.as_Generic();
            const auto& srcGp = srcTy->as_Path().path.data.as_Generic();

            if (dstGp == srcGp) {
                return false;
            } else if (dstGp.path == srcGp.path) {
                if (str.structMarkings.dstType == HIRStructMarkings::DstType::Possible) {
                    const auto& dstInner = dstGp.params.types.at(str.structMarkings.unsizedParam);
                    const auto& srcInner = srcGp.params.types.at(str.structMarkings.unsizedParam);
                    return this->canUnsize(sp, dstInner, srcInner);
                }

                auto monomorphField = [&](const HIRTypeData* self, const HIRPathParams& params, const HIRTypeData* tpl) {
                    return this->monomorphExpand(sp, tpl, MonomorphStatePtr(crate.types, self, &params, nullptr));
                };
                auto checkField = [&](const HIRTypeData* tpl) {
                    return monomorphField(dstTy, dstGp.params, tpl) == monomorphField(srcTy, srcGp.params, tpl);
                };
                const HIRTypeData* tailTpl = nullptr;
                switch (str.data.tag()) {
                    case HIRStructData::TAG_Unit:
                        BUG(sp, "Potentially-unsized unit struct " << dstTy);
                    case HIRStructData::TAG_Tuple: {
                        const auto& fields = str.data.as_Tuple();
                        tailTpl = fields.at(str.structMarkings.unsizedField).ent;
                        for (size_t i = 0; i < fields.size(); i++) {
                            if (i != str.structMarkings.unsizedField && !checkField(fields[i].ent)) {
                                return false;
                            }
                        }
                        break;
                    }
                    case HIRStructData::TAG_Named: {
                        const auto& fields = str.data.as_Named();
                        tailTpl = fields.at(str.structMarkings.unsizedField).ty;
                        for (size_t i = 0; i < fields.size(); i++) {
                            if (i != str.structMarkings.unsizedField && !checkField(fields[i].ty)) {
                                return false;
                            }
                        }
                        break;
                    }
                }
                return this->canUnsize(sp, monomorphField(dstTy, dstGp.params, tailTpl), monomorphField(srcTy, srcGp.params, tailTpl));
            } else {
                return false;
            }
        }
    }

    // (Trait) <- Foo
    if (const auto* de = dstTy->opt_TraitObject()) {
        // TODO: Check if src_ty is !Sized
        // - Only allowed if the source is a trait object with the same data trait and lesser bounds

        // (Trait) <- (Trait+Foo)
        if (const auto* se = srcTy->opt_TraitObject()) {
            // 1. Data trait must be the same
            if (de->trait.path.path != se->trait.path.path) {
                // Ensure that `de->m_trait` is a parent of `se->m_trait`
                const auto& trait = *se->trait.traitPtr;
                bool found = false;
                for (const auto& pt : trait.allParentTraits) {
                    if (pt.path.path == de->trait.path.path) {
                        auto p = MonomorphStatePtr(crate.types, nullptr, &se->trait.path.params, nullptr).monomorphGenericpath(sp, pt.path);
                        if (p == de->trait.path) {
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    return false;
                }
            } else {
                if (de->trait.path != se->trait.path) {
                    return false;
                }
            }
            // 2. Destination markers must be a strict subset
            for (const auto& mt : de->markers) {
                bool found = false;
                for (const auto& omt : se->markers) {
                    if (omt == mt) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    // Return early.
                    return false;
                }
            }

            return true;
        }

        bool good;

        HIRTypeData::Data_TraitObject tmpE;
        tmpE.trait.path = de->trait.path.path;

        // Check data trait first.
        if (de->trait.path.path == HIRSimplePath()) {
            ASSERT_BUG(sp, de->markers.size() > 0, "TraitObject with no traits - " << dstTy);
            good = true;
        } else {
            good = false;
            findImpl(sp, de->trait.path.path, de->trait.path.params, srcTy, [&](const auto impl, SolverCertainty) {
                good = true;
                for (const auto& aty : de->trait.typeBounds) {
                    // TODO: Can ATY bounds have generics
                    auto atyv = impl.getType(crate.types, aty.first.c_str(), aty.second.atyParams);
                    if (atyv == HIRTypeRef()) {
                        // Get the trait from which this associated type comes.
                        // Insert a UfcsKnown path for that
                        atyv = crate.types.path(HIRPath(srcTy, aty.second.sourceTrait.clone(), aty.first), {});
                    }
                    // Run EAT
                    this->expandAssociatedTypes(sp, atyv);
                    // Region identity is not part of the associated-type
                    // relation used for trait-object coercions.  In
                    // particular, closure output inference can retain an
                    // omitted region while the HRTB destination has already
                    // rebound it.  ASTType* equality is deliberately pointer
                    // identity, so use the explicit region-erasing relation
                    // here.
                    if (aty.second.type != atyv && !aty.second.type->equalsIgnoringRegions(atyv)) {
                        good = false;
                    }
                }
                return true;
            });
        }

        // Then markers
        auto cb = [&](const auto impl, SolverCertainty) {
            tmpE.markers.back().params = impl.getTraitParams(crate.types);
            return true;
        };
        for (const auto& marker : de->markers) {
            if (!good) {
                break;
            }
            tmpE.markers.push_back(marker.path);
            good &= this->findImpl(sp, marker.path, marker.params, srcTy, cb);
        }

        return good;
    }

    // [T] <- [T; n]
    if (const auto* de = dstTy->opt_Slice()) {
        if (const auto* se = srcTy->opt_Array()) {
            return se->inner == de->inner || se->inner->equalsIgnoringRegions(de->inner);
        }
    }

    return false;
}

// Check if the passed type contains an UnsafeCell
// Returns `Fuzzy` if generic, `Equal` if it does contain an UnsafeCell, and `Unequal` if it doesn't (shared=immutable)
HIRCompare StaticTraitResolve::typeIsInteriorMutable(const Span& sp, const HIRTypeData* ty) const {
    switch ((*ty).tag()) {
        case HIRTypeData::TAG_Infer: {
            // Is this a bug?
            return HIRCompare::Fuzzy;
        }
        case HIRTypeData::TAG_Diverge: {
            return HIRCompare::Unequal;
        }
        case HIRTypeData::TAG_Primitive: {
            return HIRCompare::Unequal;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = (*ty).as_Path();
            auto monomorphCb = MonomorphStatePtr(crate.types, nullptr, e.path.data.is_Generic() ? &e.path.data.as_Generic().params : nullptr, nullptr);
            HIRTypeRef tmpTy;
            auto monomorph = [&](const auto& tpl) -> const HIRTypeData* {
                return this->monomorphExpandOpt(sp, tmpTy, tpl, monomorphCb);
            };
            switch (e.binding.tag()) {
                case HIRTypePathBinding::TAG_Unbound: {
                    return HIRCompare::Fuzzy;
                }
                case HIRTypePathBinding::TAG_Opaque: {
                    return HIRCompare::Fuzzy;
                }
                case HIRTypePathBinding::TAG_ExternType: {
                    return HIRCompare::Unequal;
                }
                case HIRTypePathBinding::TAG_Struct: {
                    auto& pbe = e.binding.as_Struct();
                    const HIRGenericPath& p = e.path.data.as_Generic();
                    if (p.path == crate.getLangItemPath(sp, "unsafe_cell")) {
                        return HIRCompare::Equal;
                    }
                    // TODO: Cache this result?
                    switch (pbe->data.tag()) {
                        case HIRStructData::TAG_Unit: {
                            auto& _ = pbe->data.as_Unit();
                            return HIRCompare::Unequal;
                        }
                        case HIRStructData::TAG_Tuple: {
                            auto& e = pbe->data.as_Tuple();
                            for (const auto& v : e) {
                                switch (this->typeIsInteriorMutable(sp, monomorph(v.ent))) {
                                    case HIRCompare::Equal:
                                        return HIRCompare::Equal;
                                    case HIRCompare::Fuzzy:
                                        return HIRCompare::Fuzzy;
                                    default:
                                        continue;
                                }
                            }
                            return HIRCompare::Unequal;
                        }
                        case HIRStructData::TAG_Named: {
                            auto& e = pbe->data.as_Named();
                            for (const auto& v : e) {
                                switch (this->typeIsInteriorMutable(sp, monomorph(v.ty))) {
                                    case HIRCompare::Equal:
                                        return HIRCompare::Equal;
                                    case HIRCompare::Fuzzy:
                                        return HIRCompare::Fuzzy;
                                    default:
                                        continue;
                                }
                            }
                            return HIRCompare::Unequal;
                        }
                    }
                    break;
                }
                case HIRTypePathBinding::TAG_Enum: {
                    auto& pbe = e.binding.as_Enum();
                    switch (pbe->data.tag()) {
                        case HIREnumClass::TAG_Value: {
                            auto& _ = pbe->data.as_Value();
                            return HIRCompare::Unequal;
                        }
                        case HIREnumClass::TAG_Data: {
                            auto& ee = pbe->data.as_Data();
                            for (const auto& var : ee) {
                                switch (this->typeIsInteriorMutable(sp, monomorph(var.type))) {
                                    case HIRCompare::Equal:
                                        return HIRCompare::Equal;
                                    case HIRCompare::Fuzzy:
                                        return HIRCompare::Fuzzy;
                                    default:
                                        continue;
                                }
                            }
                            return HIRCompare::Unequal;
                        }
                    }
                    break;
                }
                case HIRTypePathBinding::TAG_Union: {
                    auto& pbe = e.binding.as_Union();
                    for (const auto& var : pbe->variants) {
                        switch (this->typeIsInteriorMutable(sp, monomorph(var.ty))) {
                            case HIRCompare::Equal:
                                return HIRCompare::Equal;
                            case HIRCompare::Fuzzy:
                                return HIRCompare::Fuzzy;
                            default:
                                continue;
                        }
                    }
                    return HIRCompare::Unequal;
                }
            }
            break;
        }
        case HIRTypeData::TAG_Generic: {
            return HIRCompare::Fuzzy;
        }
        case HIRTypeData::TAG_TraitObject: {
            // Can't know with a trait object
            return HIRCompare::Fuzzy;
        }
        case HIRTypeData::TAG_ErasedType: {
            // Can't know with an erased type (effectively a generic)
            return HIRCompare::Fuzzy;
        }
        case HIRTypeData::TAG_Array: {
            auto& e = (*ty).as_Array();
            return this->typeIsInteriorMutable(sp, e.inner);
        }
        case HIRTypeData::TAG_Slice: {
            auto& e = (*ty).as_Slice();
            return this->typeIsInteriorMutable(sp, e.inner);
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = (*ty).as_Pattern();
            return this->typeIsInteriorMutable(sp, e.inner);
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = (*ty).as_Tuple();
            for (const auto& t : e) {
                auto rv = this->typeIsInteriorMutable(sp, t);
                if (rv != HIRCompare::Unequal) {
                    return rv;
                }
            }
            return HIRCompare::Unequal;
        }
        case HIRTypeData::TAG_NodeType: {
            auto& e = (*ty).as_NodeType();
            switch (e.tag()) {
                case HIRTypeDataNodeType::TAG_Closure: {
                    auto& nodeP = e.as_Closure();
                    // Return fuzzy (i.e. might be) if the closure class is still unknown.
                    if (nodeP->cls == HIRExprNodeClosure::Class::Unknown) {
                        return HIRCompare::Fuzzy;
                    }
                    // Shortcut: Copy closures won't be imut
                    if (nodeP->isCopy) {
                        return HIRCompare::Unequal;
                    }
                    // Check all captures
                    for (const auto& c : nodeP->captures) {
                        auto rv = this->typeIsInteriorMutable(sp, c->resType);
                        if (rv != HIRCompare::Unequal) {
                            return rv;
                        }
                    }
                    // If no capture possibly imut, then return no
                    return HIRCompare::Unequal;
                }
                case HIRTypeDataNodeType::TAG_Generator: {
                    auto& nodeP = e.as_Generator();
                    // Check all captures
                    for (const auto& c : nodeP->captures) {
                        auto rv = this->typeIsInteriorMutable(sp, c->resType);
                        if (rv != HIRCompare::Unequal) {
                            return rv;
                        }
                    }
                    // If no capture possibly imut, then return no
                    return HIRCompare::Unequal;
                }
                case HIRTypeDataNodeType::TAG_Async: {
                    TODO(sp, "type_is_interior_mutable on async");
                    break;
                }
            }
            break;
        }
        case HIRTypeData::TAG_Borrow: {
            return HIRCompare::Unequal;
        }
        case HIRTypeData::TAG_Pointer: {
            return HIRCompare::Unequal;
        }
        case HIRTypeData::TAG_NamedFunction: {
            return HIRCompare::Unequal;
        }
        case HIRTypeData::TAG_Function: {
            return HIRCompare::Unequal;
        }
    }
    return HIRCompare::Fuzzy;
}

MetadataType StaticTraitResolve::metadataType(const Span& sp, const HIRTypeData* ty, bool errOnUnknown /*=false*/) const {
    switch ((*ty).tag()) {
        default:
            return MetadataType::None;
        case HIRTypeData::TAG_Generic: {
            auto& e = (*ty).as_Generic();
            // Check for an explicit `Sized` bound
            if (this->typeIsSized(sp, ty)) {
                return MetadataType::None;
            }
            if (e.binding == 0xFFFF) {
                ASSERT_BUG(sp, implGenerics_, "Use of `Self` with no self type (no impl generics)");
                return selfMetadata;
            } else if ((e.binding >> 8) == 0) {
                auto idx = e.binding & 0xFF;
                ASSERT_BUG(sp, implGenerics_, "Encountered generic " << ty << " without impl generics available");
                ASSERT_BUG(sp, idx < implGenerics_->types.size(), "Encountered generic " << ty << " out of range of impl generic spec");
                if (implGenerics_->types[idx].isSized) {
                    return MetadataType::None;
                } else {
                    return MetadataType::Unknown;
                }
            } else if ((e.binding >> 8) == 1) {
                auto idx = e.binding & 0xFF;
                ASSERT_BUG(sp, itemGenerics_, "Encountered generic " << ty << " without item generics available");
                ASSERT_BUG(sp, idx < itemGenerics_->types.size(), "Encountered generic " << ty << " out of range of item generic spec");
                if (itemGenerics_->types[idx].isSized) {
                    return MetadataType::None;
                } else {
                    return MetadataType::Unknown;
                }
            } else if (e.isPlaceholder()) {
                return MetadataType::None;
            } else {
                BUG(sp, "Unknown generic binding on " << ty);
            }
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            auto& e = (*ty).as_ErasedType();
            if (e.isSized) {
                return MetadataType::None;
            } else {
                return MetadataType::Unknown;
            }
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = (*ty).as_Path();
            switch (e.binding.tag()) {
                case HIRTypePathBinding::TAG_Unbound: {
                    // TODO: Should this return something else?
                    return MetadataType::Unknown;
                }
                case HIRTypePathBinding::TAG_Opaque: {
                    if (const auto* pe = e.path.data.opt_UfcsKnown()) {
                        const auto& trait = crate.getTraitByPath(sp, pe->trait.path);
                        const auto* aty = trait.getAtyDef(pe->item).first;
                        if (aty && !aty->isSized) {
                            return MetadataType::Unknown;
                        }
                    }
                    return MetadataType::None;
                }
                case HIRTypePathBinding::TAG_Struct: {
                    auto& pbe = e.binding.as_Struct();
                    switch (pbe->structMarkings.dstType) {
                        case HIRStructMarkings::DstType::Slice:
                            return MetadataType::Slice;
                        case HIRStructMarkings::DstType::TraitObject:
                            return MetadataType::TraitObject;
                        case HIRStructMarkings::DstType::None:
                        case HIRStructMarkings::DstType::Possible:
                        case HIRStructMarkings::DstType::Projection: {
                            const auto& params = e.path.data.as_Generic().params;
                            auto monomorph = [&](const auto& tpl) {
                                return this->monomorphExpand(sp, tpl, MonomorphStatePtr(crate.types, ty, &params, nullptr));
                            };
                            switch (pbe->data.tag()) {
                                case HIRStructData::TAG_Unit: {
                                    return MetadataType::None;
                                }
                                case HIRStructData::TAG_Tuple: {
                                    auto& se = pbe->data.as_Tuple();
                                    return se.empty() ? MetadataType::None : this->metadataType(sp, monomorph(se.back().ent));
                                }
                                case HIRStructData::TAG_Named: {
                                    auto& se = pbe->data.as_Named();
                                    return se.empty() ? MetadataType::None : this->metadataType(sp, monomorph(se.back().ty));
                                }
                            }
                            UNREACHABLE();
                        }
                    }
                    break;
                }
                case HIRTypePathBinding::TAG_ExternType: {
                    // Extern types aren't Sized, but have no metadata
                    return MetadataType::Zero;
                }
                case HIRTypePathBinding::TAG_Enum: {
                    break;
                }
                case HIRTypePathBinding::TAG_Union: {
                    break;
                }
            }
            return MetadataType::None;
        }
        case HIRTypeData::TAG_Infer: {
            // Shouldn't be hit? but can early on
            return MetadataType::Unknown;
        }
        case HIRTypeData::TAG_Diverge: {
            // The ! type is kinda Sized ...
            return MetadataType::None;
        }
        case HIRTypeData::TAG_Primitive: {
            auto& e = (*ty).as_Primitive();
            // All primitives (except the unsized `str`) are Sized
            if (e == HIRCoreType::Str) {
                return MetadataType::Slice;
            } else {
                return MetadataType::None;
            }
            break;
        }
        case HIRTypeData::TAG_Slice: {
            return MetadataType::Slice;
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = (*ty).as_Pattern();
            return this->metadataType(sp, e.inner, errOnUnknown);
        }
        case HIRTypeData::TAG_TraitObject: {
            return MetadataType::TraitObject;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = (*ty).as_Tuple();
            // A tuple is unsized when its last element is, just as a struct is.
            return e.empty() ? MetadataType::None : this->metadataType(sp, e.back(), errOnUnknown);
        }
    }
    UNREACHABLE();
}

bool StaticTraitResolve::typeNeedsDropGlue(const Span& sp, const HIRTypeData* ty) const {
    // A crate without the Drop lang item cannot define a destructor.  In that
    // language mode no type can require compiler-generated drop glue, and in
    // particular the resolver must not try to look up an empty trait path.
    if (langDrop().components().empty()) {
        return false;
    }

    // If `T: Copy`, then it can't need drop glue
    if (typeIsCopy(sp, ty)) {
        return false;
    }

    switch ((*ty).tag()) {
        case HIRTypeData::TAG_Generic: {
            // TODO: Is this an error?
            return true;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = (*ty).as_Path();
            if (e.binding.is_Opaque()) {
                return true;
            }

            if (e.path.data.as_Generic().path == crate.getLangItemPathOpt("manually_drop")) {
                return false;
            }

            auto it = dropCache.find(ty);
            if (it != dropCache.end()) {
                return it->second;
            }

            auto pp = HIRPathParams();
            bool hasDirectDrop = this->findImpl(sp, langDrop(), &pp, ty, [&](auto, SolverCertainty) {
                return true;
            });
            if (hasDirectDrop) {
                dropCache.insert(::std::make_pair(ty, true));
                return true;
            }

            HIRTypeRef tmpTy;
            const auto& pe = e.path.data.as_Generic();
            auto monomorphCb = MonomorphStatePtr(crate.types, ty, &pe.params, nullptr);
            auto monomorph = [&](const auto& tpl) -> const HIRTypeData* {
                return this->monomorphExpandOpt(sp, tmpTy, tpl, monomorphCb);
            };
            bool needsDropGlue = false;
            switch (e.binding.tag()) {
                case HIRTypePathBinding::TAG_Unbound: {
                    BUG(sp, "Unbound path");
                    break;
                }
                case HIRTypePathBinding::TAG_Opaque: {
                    // Technically a bug, checked above
                    return true;
                }
                case HIRTypePathBinding::TAG_Struct: {
                    auto& pbe = e.binding.as_Struct();
                    switch (pbe->data.tag()) {
                        case HIRStructData::TAG_Unit: {
                            break;
                        }
                        case HIRStructData::TAG_Tuple: {
                            auto& se = pbe->data.as_Tuple();
                            for (const auto& e : se) {
                                if (typeNeedsDropGlue(sp, monomorph(e.ent))) {
                                    needsDropGlue = true;
                                    break;
                                }
                            }
                            break;
                        }
                        case HIRStructData::TAG_Named: {
                            auto& se = pbe->data.as_Named();
                            for (const auto& e : se) {
                                if (typeNeedsDropGlue(sp, monomorph(e.ty))) {
                                    needsDropGlue = true;
                                    break;
                                }
                            }
                            break;
                        }
                    }
                    break;
                }
                case HIRTypePathBinding::TAG_Enum: {
                    auto& pbe = e.binding.as_Enum();
                    if (const auto* e = pbe->data.opt_Data()) {
                        for (const auto& var : *e) {
                            if (typeNeedsDropGlue(sp, monomorph(var.type))) {
                                needsDropGlue = true;
                                break;
                            }
                        }
                    }
                    break;
                }
                case HIRTypePathBinding::TAG_Union: {
                    // Unions don't have drop glue unless they impl Drop
                    needsDropGlue = false;
                    break;
                }
                case HIRTypePathBinding::TAG_ExternType: {
                    // Extern types don't have drop glue
                    needsDropGlue = false;
                    break;
                }
            }
            dropCache.insert(::std::make_pair(ty, needsDropGlue));
            return needsDropGlue;
        }
        case HIRTypeData::TAG_Diverge: {
            return false;
        }
        case HIRTypeData::TAG_NodeType: {
            // All magic node types need glue
            return true;
        }
        case HIRTypeData::TAG_Infer: {
            BUG(sp, "type_needs_drop_glue on _");
            return false;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = (*ty).as_Borrow();
            // &-ptrs don't have drop glue
            if (e.type != HIRBorrowType::Owned) {
                return false;
            }
            return typeNeedsDropGlue(sp, e.inner);
        }
        case HIRTypeData::TAG_Pointer: {
            return false;
        }
        case HIRTypeData::TAG_NamedFunction: {
            return false;
        }
        case HIRTypeData::TAG_Function: {
            return false;
        }
        case HIRTypeData::TAG_Primitive: {
            return false;
        }
        case HIRTypeData::TAG_Array: {
            auto& e = (*ty).as_Array();
            return typeNeedsDropGlue(sp, e.inner);
        }
        case HIRTypeData::TAG_Slice: {
            auto& e = (*ty).as_Slice();
            return typeNeedsDropGlue(sp, e.inner);
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = (*ty).as_Pattern();
            return typeNeedsDropGlue(sp, e.inner);
        }
        case HIRTypeData::TAG_TraitObject: {
            return true;
        }
        case HIRTypeData::TAG_ErasedType: {
            // Is this an error?
            return true;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = (*ty).as_Tuple();
            for (const auto& ty : e) {
                if (typeNeedsDropGlue(sp, ty)) {
                    return true;
                }
            }
            return false;
        }
    }
    assert(!"Fell off the end of type_needs_drop_glue");
    UNREACHABLE();
}

bool StaticTraitResolve::findAsyncDrop(const Span& sp, const HIRTypeData* ty, HIRPath& path, HIRTypeRef& futureTy) const {
    const auto& trait = crate.getLangItemPathOpt("async_drop");
    if (trait.components().empty() || monomorphiseTypeNeeded(ty)) {
        return false;
    }

    bool found = false;
    findImpl(sp, trait, HIRPathParams{}, ty, [&](ImplRef impl, SolverCertainty certainty) {
        if (certainty == SolverCertainty::Proven && impl.data.is_TraitImpl()) {
            found = true;
            return true;
        }
        return false;
    });
    if (!found) {
        return false;
    }

    path = HIRPath(ty, HIRGenericPath(trait), RcString::newInterned("drop"), HIRPathParams{});
    MonomorphState params(crate.types);
    auto value = getValue(sp, path, params);
    const auto* function = value.opt_Function();
    ASSERT_BUG(sp, function, "AsyncDrop::drop did not resolve for " << ty);
    futureTy = params.monomorphType(sp, (*function)->returnType);
    expandAssociatedTypes(sp, futureTy);
    return true;
}

bool StaticTraitResolve::typeNeedsAsyncDropInner(const Span& sp, const HIRTypeData* ty, HIRTypeRefSet& stack) const {
    HIRPath path{HIRSimplePath()};
    HIRTypeRef futureTy;
    if (findAsyncDrop(sp, ty, path, futureTy)) {
        return true;
    }
    if (!stack.insert(ty).second) {
        return false;
    }

    bool rv = false;
    if (const auto* array = ty->opt_Array()) {
        rv = !array->size.is_Known() || array->size.as_Known() != 0 ? typeNeedsAsyncDropInner(sp, array->inner, stack) : false;
    } else if (const auto* tuple = ty->opt_Tuple()) {
        for (const auto& field : *tuple) {
            if (typeNeedsAsyncDropInner(sp, field, stack)) {
                rv = true;
                break;
            }
        }
    } else if (const auto* pathTy = ty->opt_Path()) {
        const auto* generic = pathTy->path.data.opt_Generic();
        if (generic && generic->path != crate.getLangItemPathOpt("manually_drop")) {
            auto monomorph = MonomorphStatePtr(crate.types, ty, &generic->params, nullptr);
            if (const auto* str = pathTy->binding.opt_Struct()) {
                if (pathTy->isFuture() || pathTy->isGenerator()) {
                    const auto* fields = (*str)->data.opt_Tuple();
                    ASSERT_BUG(sp, fields && !fields->empty(), "coroutine without its state field: " << ty);
                    for (size_t i = 0; i < fields->size(); i++) {
                        auto fieldTy = monomorph.monomorphType(sp, fields->at(i).ent);
                        expandAssociatedTypes(sp, fieldTy);
                        if (i == 0) {
                            const auto* fieldPath = fieldTy->opt_Path();
                            ASSERT_BUG(sp, fieldPath && fieldPath->path.data.is_Generic() && fieldPath->path.data.as_Generic().path == crate.getLangItemPath(sp, "maybe_uninit") && fieldPath->path.data.as_Generic().params.types.size() == 1, "coroutine state is not MaybeUninit<State>: " << fieldTy);
                            fieldTy = fieldPath->path.data.as_Generic().params.types[0];
                        }
                        if (typeNeedsAsyncDropInner(sp, fieldTy, stack)) {
                            rv = true;
                            break;
                        }
                    }
                    stack.erase(ty);
                    return rv;
                }
                switch (((*str)->data).tag()) {
                    case HIRStructData::TAG_Unit:
                        break;
                    case HIRStructData::TAG_Tuple:
                        for (const auto& field : ((*str)->data).as_Tuple()) {
                            auto fieldTy = monomorph.monomorphType(sp, field.ent);
                            expandAssociatedTypes(sp, fieldTy);
                            if (typeNeedsAsyncDropInner(sp, fieldTy, stack)) {
                                rv = true;
                                break;
                            }
                        }
                        break;
                    case HIRStructData::TAG_Named:
                        for (const auto& field : ((*str)->data).as_Named()) {
                            auto fieldTy = monomorph.monomorphType(sp, field.ty);
                            expandAssociatedTypes(sp, fieldTy);
                            if (typeNeedsAsyncDropInner(sp, fieldTy, stack)) {
                                rv = true;
                                break;
                            }
                        }
                        break;
                }
            } else if (const auto* enm = pathTy->binding.opt_Enum()) {
                if (const auto* variants = (*enm)->data.opt_Data()) {
                    for (const auto& variant : *variants) {
                        auto fieldTy = monomorph.monomorphType(sp, variant.type);
                        expandAssociatedTypes(sp, fieldTy);
                        if (typeNeedsAsyncDropInner(sp, fieldTy, stack)) {
                            rv = true;
                            break;
                        }
                    }
                }
            }
        }
    }
    stack.erase(ty);
    return rv;
}

bool StaticTraitResolve::typeNeedsAsyncDrop(const Span& sp, const HIRTypeData* ty) const {
    HIRTypeRefSet stack;
    return typeNeedsAsyncDropInner(sp, ty, stack);
}

const HIRTypeData* StaticTraitResolve::isTypeOwnedBox(const HIRTypeData* ty) const {
    if (!ty->is_Path()) {
        return nullptr;
    }
    const auto& te = ty->as_Path();

    if (!te.path.data.is_Generic()) {
        return nullptr;
    }
    const auto& pe = te.path.data.as_Generic();

    if (pe.path != langBox()) {
        return nullptr;
    }
    // TODO: Properly assert?
    return pe.params.types.at(0);
}

const HIRTypeData* StaticTraitResolve::isTypePhantomData(const HIRTypeData* ty) const {
    if (!ty->is_Path()) {
        return nullptr;
    }
    const auto& te = ty->as_Path();

    if (!te.path.data.is_Generic()) {
        return nullptr;
    }
    const auto& pe = te.path.data.as_Generic();

    if (pe.path != langPhantomData()) {
        return nullptr;
    }
    // TODO: Properly assert?
    return pe.params.types.at(0);
}

HIRTypeRef StaticTraitResolve::getFieldType(const Span& sp, const HIRTypeData* ty, const RcString& name) const {
    switch ((*ty).tag()) {
        default:
            TODO(sp, "" << ty << " " << name);
        case HIRTypeData::TAG_Borrow: {
            auto& te = (*ty).as_Borrow();
            ASSERT_BUG(sp, name == RcString(), "get_field_type: Deref with non-empty field (`" << name << "`)");
            return te.inner;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& te = (*ty).as_Tuple();
            ::std::stringstream ss{name.c_str()};
            int idx = -1;
            ss >> idx;
            ASSERT_BUG(sp, idx >= 0, "Malformed tuple index field name - `" << name << "`");
            ASSERT_BUG(sp, size_t(idx) < te.size(), "Tuple index out of bounds");
            return te.at(idx);
        }
        case HIRTypeData::TAG_Path: {
            auto& te = (*ty).as_Path();
            switch (te.binding.tag()) {
                default:
                    BUG(sp, "Getting field on invalid type - " << ty);
                case HIRTypePathBinding::TAG_Struct: {
                    auto& pbe = te.binding.as_Struct();
                    MonomorphStatePtr ms{crate.types, nullptr, &te.path.data.as_Generic().params, nullptr};
                    switch (pbe->data.tag()) {
                        case HIRStructData::TAG_Named: {
                            auto& se = pbe->data.as_Named();
                            for (const auto& f : se) {
                                if (f.name == name) {
                                    return ms.monomorphType(sp, f.ty);
                                }
                            }
                            BUG(sp, "Unknown field `" << name << "` on " << ty);
                            break;
                        }
                        case HIRStructData::TAG_Tuple: {
                            auto& se = pbe->data.as_Tuple();
                            unsigned index = std::strtol(name.c_str(), nullptr, 10);
                            ASSERT_BUG(sp, index < se.size(), "" << ty << " " << name);
                            return ms.monomorphType(sp, se.at(index).ent);
                        }
                        case HIRStructData::TAG_Unit: {
                            BUG(sp, "Getting field from unit-like struct - " << ty);
                            break;
                        }
                    }
                    break;
                }
                case HIRTypePathBinding::TAG_Union: {
                    auto& pbe = te.binding.as_Union();
                    MonomorphStatePtr ms{crate.types, nullptr, &te.path.data.as_Generic().params, nullptr};
                    for (const auto& f : pbe->variants) {
                        if (f.name == name) {
                            return ms.monomorphType(sp, f.ty);
                        }
                    }
                    BUG(sp, "Unknown field `" << name << "` on " << ty);
                    break;
                }
            }
            break;
        }
    }
    BUG(sp, "Reached end of `get_field_type` - " << ty);
}

StaticTraitResolve::ValuePtr StaticTraitResolve::getValue(const Span& sp, const HIRPath& p, MonomorphState& outParams, bool signatureOnly /*=false*/, const HIRGenericParams** outImplParamsDef /*=nullptr*/, ResolvedTraitImplPath* outTraitImplPath /*=nullptr*/) const {
    outParams = MonomorphState{crate.types};
    if (outTraitImplPath) {
        outTraitImplPath->type = nullptr;
        outTraitImplPath->traitParams = HIRPathParams();
    }
    switch (p.data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& pe = p.data.as_Generic();
            if (pe.path.components().size() > 1) {
                const auto& ti = crate.getTypeitemByPath(sp, pe.path, /*ignore_crate_name=*/false, /*ignore_last_node=*/true);
                if (const auto* e = ti.opt_Enum()) {
                    if (outImplParamsDef) {
                        *outImplParamsDef = &e->params;
                    }
                    outParams.ppImpl = &pe.params;
                    auto idx = e->findVariant(pe.path.components().back());
                    if (e->data.is_Data()) {
                        if (e->data.as_Data()[idx].type != crate.types.unit()) {
                            return ValuePtr::Data_EnumConstructor{e, idx};
                        }
                    }
                    return ValuePtr::Data_EnumValue{e, idx};
                }
            }
            const auto& v = crate.getValitemByPath(sp, pe.path);
            switch (v.tag()) {
                case HIRValueItem::TAG_Import: {
                    BUG(sp, "Module Import");
                    break;
                }
                case HIRValueItem::TAG_Constant: {
                    outParams.ppMethod = &pe.params;
                    return v.as_Constant();
                }
                case HIRValueItem::TAG_Static: {
                    outParams.ppMethod = &pe.params;
                    return v.as_Static();
                }
                case HIRValueItem::TAG_Function: {
                    outParams.ppMethod = &pe.params;
                    return v.as_Function();
                }
                case HIRValueItem::TAG_StructConstant: {
                    outParams.ppImpl = &pe.params;
                    TODO(sp, "StructConstant - " << p);
                    break;
                }
                case HIRValueItem::TAG_StructConstructor: {
                    auto& ve = v.as_StructConstructor();
                    outParams.ppImpl = &pe.params;
                    const auto& str = crate.getStructByPath(sp, ve.ty);
                    if (outImplParamsDef) {
                        *outImplParamsDef = &str.params;
                    }
                    return ValuePtr::Data_StructConstructor{&ve.ty, &str};
                    break;
                }
            }
            UNREACHABLE();
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& pe = p.data.as_UfcsKnown();
            if (pe.trait.path == HIRSimplePath() && pe.item == "vtable#") {
                return ValuePtr::make_NotYetKnown({});
            }
            outParams.selfTy = pe.type;
            outParams.ppImpl = &pe.trait.params;
            outParams.ppMethod = &pe.params;
            const HIRTrait& tr = crate.getTraitByPath(sp, pe.trait.path);
            if (!tr.values.count(pe.item)) {
                return ValuePtr();
            }

            if (outImplParamsDef) {
                *outImplParamsDef = &tr.params;
                // Updated if an impl is found+used
            }

            const HIRTraitValueItem& v = tr.values.at(pe.item);
            if (signatureOnly) {
                switch (v.tag()) {
                    case HIRTraitValueItem::TAG_Constant: {
                        auto& ve = v.as_Constant();
                        return &ve;
                    }
                    case HIRTraitValueItem::TAG_Static: {
                        auto& ve = v.as_Static();
                        return &ve;
                    }
                    case HIRTraitValueItem::TAG_Function: {
                        auto& ve = v.as_Function();
                        return &ve;
                    }
                }
            } else {
                bool bestIsSpec = false;
                bool hasBoundedImpl = false;
                bool hasAmbiguousImpl = false;
                ImplRef bestImpl;
                ValuePtr rv;
                bool lookupNeedsResolution = specializationLookupNeedsResolution(pe.type, pe.trait.params);
                auto visitImpl = [&](auto impl, SolverCertainty certainty) -> bool {
                    if (!impl.data.is_TraitImpl()) {
                        hasBoundedImpl = true;
                        return false;
                    }
                    if (certainty != SolverCertainty::Proven && lookupNeedsResolution) {
                        // A body or associated constant from a merely possible
                        // impl cannot be selected until the receiver is known.
                        hasAmbiguousImpl = true;
                        return false;
                    }
                    const HIRTraitImpl& ti = *impl.data.as_TraitImpl().impl;
                    bool isSpec = false;

                    ValuePtr thisRv;
                    // - Constants
                    if (thisRv.is_NotFound()) {
                        auto it = ti.constants.find(pe.item);
                        if (it != ti.constants.end()) {
                            isSpec = it->second.isSpecialisable;
                            thisRv = &it->second.data;
                        }
                    }
                    // - Statics
                    if (thisRv.is_NotFound()) {
                        auto it = ti.statics.find(pe.item);
                        if (it != ti.statics.end()) {
                            isSpec = it->second.isSpecialisable;
                            thisRv = &it->second.data;
                        }
                    }
                    // - Functions
                    if (thisRv.is_NotFound()) {
                        auto it = ti.methods.find(pe.item);
                        if (it != ti.methods.end()) {
                            isSpec = it->second.isSpecialisable;
                            thisRv = &it->second.data;
                        }
                    }

                    if (thisRv.is_NotFound()) {
                        return false;
                    } else if (!impl.moreSpecificThan(crate.types, bestImpl)) {
                        // Keep searching
                        return false;
                    } else {
                        bestIsSpec = isSpec;
                        bestImpl = mv$(impl);
                        rv = std::move(thisRv);
                        // NOTE: There could be an overlapping and more-specific impl without `default` being involved
                        return false;
                    }
                };

                if (!nextSolver) {
                    ASSERT_BUG(sp, crate.pool, "next-solver requires the crate object pool");
                    nextSolver = crate.pool->make<NextSolverBridge>(this->wb);
                }
                StaticImplCb<decltype(visitImpl)> callback(visitImpl);
                nextSolver->findValue(sp, implGenerics_, itemGenerics_, pe.trait.path, pe.trait.params, pe.type, pe.item.c_str(), callback);
                if (!bestImpl.isValid()) {
                    if (hasBoundedImpl || hasAmbiguousImpl) {
                        return ValuePtr::make_NotYetKnown({});
                    }
                    // If the type and impl are fully known, then look for trait provided values/bodies
                    if (!monomorphiseTypeNeeded(pe.type) && !monomorphisePathparamsNeeded(pe.trait.params)) {
                        // Look for provided bodies
                        switch (v.tag()) {
                            case HIRTraitValueItem::TAG_Constant: {
                                auto& ve = v.as_Constant();
                                // Constants?
                                if (ve.value || ve.valueState != HIRConstant::ValueState::Unknown) {
                                    // NOTE: The parameters have already been set
                                    return &ve;
                                } else {
                                }
                                break;
                            }
                            case HIRTraitValueItem::TAG_Static: {
                                // Statics?
                                break;
                            }
                            case HIRTraitValueItem::TAG_Function: {
                                auto& ve = v.as_Function();
                                if (ve.code || ve.code.mir) {
                                    // NOTE: The parameters have already been set
                                    return &ve;
                                }
                                // Fall through if there's no provided body
                                break;
                            }
                        }
                    } else {
                    }
                    return ValuePtr::make_NotYetKnown({});
                }
                if (bestIsSpec) {
                    // If there's generics present in the path, return NotYetKnown
                    if (monomorphiseTypeNeeded(pe.type) || monomorphisePathparamsNeeded(pe.trait.params)) {
                        return ValuePtr::make_NotYetKnown({});
                    }
                }

                if (!bestImpl.data.is_TraitImpl()) {
                    TODO(sp, "Use bounded constant values for " << p);
                }
                auto& ie = bestImpl.data.as_TraitImpl();
                if (outImplParamsDef) {
                    *outImplParamsDef = &ie.impl->params;
                }
                // Only concrete impls have one published symbol independent
                // of the caller's spelling. Generic impls must retain the
                // path used to enumerate their particular instantiation.
                if (outTraitImplPath && !ie.impl->params.isGeneric()) {
                    outTraitImplPath->type = bestImpl.getImplType(crate.types);
                    outTraitImplPath->traitParams = bestImpl.getTraitParams(crate.types);
                }
                outParams.ppImpl = &outParams.ppImplData;
                outParams.ppImplData = ie.implParams.clone();
                ASSERT_BUG(sp, !rv.is_NotFound(), "");
                return rv;
            }
            UNREACHABLE();
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& pe = p.data.as_UfcsInherent();
            outParams.selfTy = pe.type;
            outParams.ppImpl = &pe.implParams;
            outParams.ppMethod = &pe.params;
            ValuePtr rv;
            crate.findTypeImpls(pe.type, HIRResolvePlaceholdersNop(), [&](const auto& impl) {
                // Populate pp_impl if not populated
                if (!pe.implParams.hasParams()) {
                    GetParams::ParamsSet paramsSet;
                    GetParams getParams{sp, *crate.pool, impl.params, outParams.ppImplData, paramsSet};

                    auto cbIdent = HIRResolvePlaceholdersNop();
                    impl.type->matchTestGenericsFuzz(sp, pe.type, cbIdent, getParams);

                    const auto& implParams = outParams.ppImplData;

                    outParams.ppImpl = &outParams.ppImplData;
                } else {
                }

                if (outImplParamsDef) {
                    *outImplParamsDef = &impl.params;
                }

                // TODO: Specialisation
                {
                    auto fit = impl.methods.find(pe.item);
                    if (fit != impl.methods.end()) {
                        ASSERT_BUG(sp, impl.params.types.size() == outParams.ppImpl->types.size(), "Mismatch in param counts `" << *outParams.ppImpl << "`, params are `" << impl.params.fmtArgs() << "`\n- in " << p);
                        rv = ValuePtr{&fit->second.data};
                        return true;
                    }
                }
                {
                    auto it = impl.constants.find(pe.item);
                    if (it != impl.constants.end()) {
                        // The impl parameters may have been deduced above, as
                        // the method branch already accounts for.
                        ASSERT_BUG(sp, impl.params.types.size() == outParams.ppImpl->types.size(), "Mismatch in param counts " << p << ", params are " << impl.params.fmtArgs());
                        rv = ValuePtr{&it->second.data};
                        return true;
                    }
                }
                return false;
            });
            return rv;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            BUG(sp, "UfcsUnknown - " << p);
            break;
        }
    }
    UNREACHABLE();
}

StaticTraitResolve::StaticTraitResolve(const WireBoard& wb, OpaqueReveal reveal)
    : TraitResolveCommon(wb)
    , reveal_(reveal)
{
}

void StaticTraitResolve::prepIndexes() {
    copyCache.clear();
    cloneCache.clear();
    dropCache.clear();
    atyCache.clear();
    cachedImplChecks.clear();
    TraitResolveCommon::prepIndexes(Span());
}

/// \brief State manipulation
/// \{
NullOnDrop<const HIRGenericParams> StaticTraitResolve::setImplGenerics(HIRStructMarkings::DstType structDstType, const HIRGenericParams& gps) {
    MetadataType mt = MetadataType::None;
    switch (structDstType) {
        case HIRStructMarkings::DstType::None:
            break;
        case HIRStructMarkings::DstType::Possible:
        case HIRStructMarkings::DstType::Projection:
            mt = MetadataType::Unknown;
            break;
        case HIRStructMarkings::DstType::Slice:
            mt = MetadataType::Slice;
            break;
        case HIRStructMarkings::DstType::TraitObject:
            mt = MetadataType::TraitObject;
            break;
    }
    setImplGenericsRaw(mt, gps);
    return NullOnDrop<const HIRGenericParams>(implGenerics_);
}

NullOnDrop<const HIRGenericParams> StaticTraitResolve::setImplGenerics(MetadataType selfMetaType, const HIRGenericParams& gps) {
    setImplGenericsRaw(selfMetaType, gps);
    return NullOnDrop<const HIRGenericParams>(implGenerics_);
}

NullOnDrop<const HIRGenericParams> StaticTraitResolve::setImplGenerics(const HIRTypeData* selfTy, const HIRGenericParams& gps) {
    setImplGenericsRaw(MetadataType::Unknown, gps);
    selfMetadata = metadataType(Span(), selfTy);
    return NullOnDrop<const HIRGenericParams>(implGenerics_);
}

void StaticTraitResolve::updateImplSelfMetadata(const HIRTypeData* selfTy) {
    assert(implGenerics_);
    selfMetadata = metadataType(Span(), selfTy);
}

NullOnDrop<const HIRGenericParams> StaticTraitResolve::setItemGenerics(const HIRGenericParams& gps) {
    setItemGenericsRaw(gps);
    return NullOnDrop<const HIRGenericParams>(itemGenerics_);
}

void StaticTraitResolve::setImplGenericsRaw(MetadataType selfMetaType, const HIRGenericParams& gps) {
    assert(!implGenerics_);
    selfMetadata = selfMetaType;
    implGenerics_ = &gps;
    prepIndexes();
}

void StaticTraitResolve::clearImplGenerics() {
    selfMetadata = MetadataType::Unknown;
    implGenerics_ = nullptr;
    prepIndexes();
}

void StaticTraitResolve::setItemGenericsRaw(const HIRGenericParams& gps) {
    assert(!itemGenerics_);
    itemGenerics_ = &gps;
    prepIndexes();
}

void StaticTraitResolve::clearItemGenerics() {
    itemGenerics_ = nullptr;
    prepIndexes();
}

void StaticTraitResolve::setBothGenericsRaw(const HIRGenericParams* gpsImpl, const HIRGenericParams* gpsFcn) {
    assert(!implGenerics_);
    assert(!itemGenerics_);
    implGenerics_ = gpsImpl;
    itemGenerics_ = gpsFcn;
    prepIndexes();
}

void StaticTraitResolve::clearBothGenerics() {
    selfMetadata = MetadataType::Unknown;
    implGenerics_ = nullptr;
    itemGenerics_ = nullptr;
    prepIndexes();
}

// Helper: Run monomorphise+EAT if the type contains generics
const HIRTypeData* StaticTraitResolve::monomorphExpandOpt(const Span& sp, HIRTypeRef& tmp, const HIRTypeData* input, const Monomorphiser& m) const {
    if (monomorphiseTypeNeeded(input)) {
        return tmp = monomorphExpand(sp, input, m);
    } else {
        return input;
    }
}

HIRTypeRef StaticTraitResolve::monomorphExpand(const Span& sp, const HIRTypeData* input, const Monomorphiser& m) const {
    auto rv = m.monomorphType(sp, input);
    expandAssociatedTypes(sp, rv);
    return rv;
}

std::ostream& operator<<(std::ostream& os, const MetadataType& x) {
    switch (x) {
        case MetadataType::Unknown:
            return os << "Unknown";
        case MetadataType::None:
            return os << "None";
        case MetadataType::Zero:
            return os << "Zero";
        case MetadataType::Slice:
            return os << "Slice";
        case MetadataType::TraitObject:
            return os << "TraitObject";
    }
    return os << "?";
}

StaticTraitResolve::NextSolverBridge::NextSolverBridge(const WireBoard& wb)
    : ivars(wb.crate->types)
    , visibility(wb.crate->crateName, {})
    , resolve_(ivars, wb, nullptr, nullptr, visibility, nullptr)
{
}

auto StaticTraitResolve::NextSolverBridge::findImpl(const Span& sp, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRSimplePath& trait, const HIRPathParams* params, const HIRTypeData* type, StaticImplCallback& callback) -> bool {
    resolve_.setGenericContext(implGenerics, itemGenerics);

    HIRPathParams inferredParams;
    if (!params) {
        const auto& traitDef = resolve_.hirCrate().getTraitByPath(sp, trait);
        // This resolver owns m_ivars, so its inference indexes must not
        // escape into HIR and be mistaken for indexes in expression typeck.
        const auto placeholderName = RcString::newInterned(FMT("static_find_impl_" << &inferredParams));
        inferredParams.types.reserve(traitDef.params.types.size());
        for (size_t i = 0; i < traitDef.params.types.size(); i++) {
            inferredParams.types.push_back(resolve_.hirCrate().types.generic(placeholderName, GENERICPlaceholder * 256 + i));
        }
        inferredParams.values.reserve(traitDef.params.values.size());
        for (size_t i = 0; i < traitDef.params.values.size(); i++) {
            inferredParams.values.push_back(HIRConstGeneric::make_Generic({placeholderName, static_cast<unsigned int>(GENERICPlaceholder * 256 + i)}));
        }
        params = &inferredParams;
    }

    return resolve_.solveTraitGoal(sp, trait, *params, type, [&](SolverResponse response) {
        if (!response.hasImpl || !response.impl) {
            return false;
        }
        return callback.visit(response.impl->legacy(), response.certainty);
    }, {.assocName = ""});
}

auto StaticTraitResolve::NextSolverBridge::findValue(const Span& sp, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const char* valueName, StaticImplCallback& callback) -> bool {
    resolve_.setGenericContext(implGenerics, itemGenerics);
    return resolve_.solveTraitGoal(sp, trait, params, type, [&](SolverResponse response) {
        if (!response.hasImpl || !response.impl) {
            return false;
        }
        return callback.visit(response.impl->legacy(), response.certainty);
    }, {.valueName = valueName});
}

auto StaticTraitResolve::NextSolverBridge::normalize(const Span& sp, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRTypeData* projection, HIRTypeRef& output) -> bool {
    resolve_.setGenericContext(implGenerics, itemGenerics);
    return resolve_.solveNormalizesTo(sp, NormalizesTo{projection}, [&](NormalizesToResponse response) {
        if (response.output != HIRTypeRef() && response.output != projection) {
            output = ::std::move(response.output);
        }
        return true;
    });
}

auto StaticTraitResolve::NextSolverBridge::typeIsCopy(const Span& sp, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRTypeData* type) -> bool {
    resolve_.setGenericContext(implGenerics, itemGenerics);
    return resolve_.typeIsCopy(sp, type) == HIRCompare::Equal;
}

GetParams::GetParams(Span sp, ObjPool& valuePool, const HIRGenericParams& implParamsDef, HIRPathParams& implParams, ParamsSet& paramsSet)
    : HIRMatchGenerics(valuePool)
    , sp(sp)
    , implParams(implParams)
    , paramsSet(paramsSet)
{
    implParams.types.resize(implParamsDef.types.size());
    implParams.values.resize(implParamsDef.values.size());
    paramsSet.types.zero(implParamsDef.types.size());
    paramsSet.values.zero(implParamsDef.values.size());
}

auto GetParams::matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType resolveCb) -> HIRCompare {
    ASSERT_BUG(sp, g.binding < implParams.types.size(), "[GetParams] Type generic " << g << " out of bounds (" << implParams.types.size() << ")");
    if (!paramsSet.types[g.binding]) {
        paramsSet.types.mut(g.binding) = true;
        implParams.types[g.binding] = ty;
        return HIRCompare::Equal;
    }
    return implParams.types[g.binding]->compareWithPlaceholders(sp, ty, resolveCb);
}

auto GetParams::matchVal(const HIRGenericRef& g, const HIRConstGeneric& value) -> HIRCompare {
    ASSERT_BUG(sp, g.binding < implParams.values.size(), "[GetParams] Value generic " << g << " out of range (" << implParams.values.size() << ")");
    if (!paramsSet.values[g.binding]) {
        paramsSet.values.mut(g.binding) = true;
        implParams.values[g.binding] = value.clone();
        return HIRCompare::Equal;
    }
    return implParams.values[g.binding] == value ? HIRCompare::Equal : HIRCompare::Unequal;
}
