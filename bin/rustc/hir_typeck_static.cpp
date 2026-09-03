#include "hir_typeck_static.h"

#include "output.h"
#include "hir_expr.h"
#include "settings.h"
#include "wire_board.h"
#include "hir_visitor.h"
#include "hir_item_path.h"
#include "hir_typeck_helpers.h"
#include "hir_typeck_monomorph.h"
#include "hir_conv_constant_evaluation.h"

#include <std/alg/defer.h>
#include <std/mem/obj_pool.h>

#include <sstream>
#include <algorithm>

using namespace stl;

namespace {
    struct ProvenGenericParamMatcher final: HIRMatchGenerics {
        struct TypeBinding {
            u32 binding;
            const HIRType* type;
        };

        struct ValueBinding {
            u32 binding;
            const HIRConstGeneric* value;
        };

        Vector<TypeBinding> typeBindings;
        Vector<ValueBinding> valueBindings;

        ProvenGenericParamMatcher()
            : HIRMatchGenerics(BorrowMatchedValues{})
        {
        }

        HIRCompare matchTy(const HIRGenericRef& generic, const HIRType* type, tCbResolveType resolve) override {
            type = resolve.getType(Span(), type);
            for (const auto& existing : typeBindings) {
                if (existing.binding == generic.binding) {
                    return existing.type == type ? HIRCompare::Equal : HIRCompare::Unequal;
                }
            }
            typeBindings.pushBack({generic.binding, type});
            return HIRCompare::Equal;
        }

        HIRCompare matchVal(const HIRGenericRef& generic, const HIRConstGeneric& value) override {
            for (const auto& existing : valueBindings) {
                if (existing.binding == generic.binding) {
                    return *existing.value == value ? HIRCompare::Equal : HIRCompare::Unequal;
                }
            }
            valueBindings.pushBack({generic.binding, &value});
            return HIRCompare::Equal;
        }
    };

    bool paramsExactlyEqual(const HIRPathParams& left, const HIRPathParams& right) {
        if (left.types.size() != right.types.size() || left.values.size() != right.values.size()) {
            return false;
        }
        for (size_t i = 0; i < left.types.size(); i++) {
            if (left.types[i] != right.types[i]) {
                return false;
            }
        }
        for (size_t i = 0; i < left.values.size(); i++) {
            if (left.values[i] != right.values[i]) {
                return false;
            }
        }
        return true;
    }

    bool paramsMatchProven(const Span& sp, const HIRPathParams& left, const HIRPathParams& right) {
        if (paramsExactlyEqual(left, right)) {
            return true;
        }
        if (left.types.size() != right.types.size() || left.values.size() != right.values.size()) {
            return false;
        }
        if (monomorphisePathparamsNeeded(left)) {
            ProvenGenericParamMatcher matcher;
            if (left.matchTestGenericsFuzz(sp, right, HIRResolvePlaceholdersNop(), matcher) == HIRCompare::Equal) {
                return true;
            }
        }
        if (monomorphisePathparamsNeeded(right)) {
            ProvenGenericParamMatcher matcher;
            if (right.matchTestGenericsFuzz(sp, left, HIRResolvePlaceholdersNop(), matcher) == HIRCompare::Equal) {
                return true;
            }
        }
        return false;
    }

    bool specializationLookupNeedsResolution(const HIRType* type, const HIRPathParams& params) {
        auto typeNeedsResolution = [](const HIRType* inner) {
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
        return std::any_of(params.values.begin(), params.values.end(), [](const auto& value) {
            return value.is_Infer();
        });
    }
}

struct StaticTraitResolve::NextSolverBridge {
    HMTypeInferrence ivars;
    HIRSimplePath visibility;
    TraitResolution resolve_;

    explicit NextSolverBridge(const WireBoard& wb);

    bool findImpl(const Span& sp, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRSimplePath& trait, const HIRPathParams* params, const HIRType* type, SolverResponseCallback& callback);

    bool findValue(const Span& sp, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const char* valueName, SolverResponseCallback& callback);

    const HIRType* normalize(const Span& sp, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRType* projection);

    bool typeIsCopy(const Span& sp, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRType* type);

    InherentImplSelection selectInherentImpl(const Span& sp, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRType* receiver, const RcString& item, InherentItemKind kind, const HIRPathParams* initialParams = nullptr);
};

bool StaticTraitResolve::findImplCb(const Span& sp, const HIRSimplePath& traitPath, const HIRPathParams* traitParams, const HIRType* type, SolverResponseCallback& foundCb) const {
    if (traitPath.components().empty()) {
        return false;
    }
    if (const auto* path = type->opt_Path(); path && path->path.data.is_UfcsKnown()) {
        const HIRType* normalizedType = type;
        normalizedType = this->expandAssociatedTypes(sp, normalizedType);
        if (normalizedType != type) {
            return this->findImplCb(sp, traitPath, traitParams, normalizedType, foundCb);
        }
    }

    if (!nextSolver) {
        ASSERT_BUG(sp, crate.pool, StringView("next-solver requires the crate object pool"));
        nextSolver = crate.pool->make<NextSolverBridge>(this->wb);
    }
    return nextSolver->findImpl(sp, implGenerics_, itemGenerics_, traitPath, traitParams, type, foundCb);
}

const HIRType* StaticTraitResolve::fixTraitDefaultReturn(const Span& sp, const HIRItemPath& path, const HIRType* tpl) const {
    const auto& topIp = path.getTopIp();
    if (topIp.ty && topIp.trait && topIp.ty == crate.types.self()) {
        auto prefix = FMT(ATY_PREFIX_ERASED << path.name << StringView("_"));
        const auto& trait = crate.getTraitByPath(sp, *topIp.trait);
        const auto* result = cloneTyWith(crate.types, sp, tpl, [&](const HIRType* inner) -> const HIRType* {
            if (const auto* typePath = inner->opt_Path()) {
                if (const auto* projection = typePath->path.data.opt_UfcsKnown()) {
                    if (projection->type == topIp.ty && projection->trait.path == *topIp.trait && std::strncmp(projection->item.c_str(), prefix.c_str(), prefix.size()) == 0) {
                        const auto& type = trait.types.at(projection->item);
                        if (type.hasDefault) {
                            return type.defaultValue;
                        }
                    }
                }
            }
            return nullptr;
        });
        DEBUG(StringView("fix_trait_default_return: fixed to ") << result);
        return result;
    }
    return tpl;
}

const HIRType* StaticTraitResolve::expandAssociatedTypes(const Span& sp, const HIRType* input) const {
    TRACE_FUNCTION_FR(input, input);
    input = this->expandAssociatedTypesInner(sp, input);
    while (reveal_ == OpaqueReveal::All && visitTyWith(input, [](const HIRType* inner) {
        return inner->is_ErasedType();
    })) {
        input = this->revealOpaqueTypesShallow(sp, input);
        input = this->expandAssociatedTypesInner(sp, input);
    }
    return input;
}

const HIRType* StaticTraitResolve::revealOpaqueTypesShallow(const Span& sp, const HIRType* input) const {
    struct Visitor: public HIRVisitor {
        const Span& sp;
        const StaticTraitResolve& resolve;
        bool clearOpaque = false;

        const HIRType* revealOpaqueType(const HIRType* type) {
            const auto& erased = type->as_ErasedType();
            const HIRType* revealed;

            switch (erased.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    const auto& functionOpaque = erased.inner.as_Fcn();
                    MonomorphState monomorph(resolve.hirCrate().types);
                    auto value = resolve.getValue(sp, functionOpaque.origin, monomorph);
                    if (value.is_NotYetKnown() && functionOpaque.origin.data.is_UfcsKnown()) {
                        const auto& path = functionOpaque.origin.data.as_UfcsKnown();
                        auto name = RcString::newInterned(FMT(ATY_PREFIX_ERASED << path.item << StringView("_") << functionOpaque.index));
                        revealed = resolve.hirCrate().types.path(HIRPath(path.type, path.trait.clone(), name, path.params.clone()), {});
                    } else {
                        ASSERT_BUG(sp, value.is_Function(), StringView("ErasedType with Fcn type doesn't point at a function: ") << functionOpaque.origin << StringView(": ") << value.tagStr());
                        auto& function = resolve.hirCrateMut().findFunctionMut(resolve.board(), sp, functionOpaque.origin, *value.as_Function());
                        if (functionOpaque.index >= function.code.erasedTypes.length()) {
                            resolve.hirCrateMut().getOrGenMir(resolve.board(), HIRItemPath(functionOpaque.origin), function);
                        }
                        ASSERT_BUG(sp, functionOpaque.index < function.code.erasedTypes.length(), StringView("Erased type index out of range for ") << functionOpaque.origin << StringView(" - ") << functionOpaque.index << StringView(" >= ") << function.code.erasedTypes.length());
                        revealed = monomorph.monomorphType(sp, function.code.erasedTypes[functionOpaque.index]);
                    }
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Alias: {
                    const auto& alias = erased.inner.as_Alias();
                    if (alias.inner->type == nullptr) {
                        auto definers = resolve.hirCrate().opaqueTypeDefiners.find(alias.inner->path);
                        if (definers != resolve.hirCrate().opaqueTypeDefiners.end()) {
                            for (const auto& path : definers->second) {
                                MonomorphState monomorph(resolve.hirCrate().types);
                                auto value = resolve.getValue(sp, path, monomorph);
                                if (const auto* function = value.opt_Function()) {
                                    auto& functionMut = resolve.hirCrateMut().findFunctionMut(resolve.board(), sp, path, **function);
                                    resolve.hirCrateMut().getOrGenMir(resolve.board(), HIRItemPath(path), functionMut);
                                }
                                if (alias.inner->type != nullptr) {
                                    break;
                                }
                            }
                        }
                        if (alias.inner->type == nullptr) {
                            ERROR(sp, E0000, StringView("Erased type alias ") << alias.inner->path << StringView(" never set"));
                        }
                    }
                    revealed = MonomorphStatePtr(resolve.hirCrate().types, nullptr, &alias.params, nullptr).monomorphType(sp, alias.inner->type);
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Known:
                    revealed = erased.inner.as_Known();
                    break;
            }

            return revealed;
        }

        Visitor(const Span& sp, const StaticTraitResolve& resolve)
            : HIRVisitor(nullptr, resolve.hirCrate().types)
            , sp(sp)
            , resolve(resolve)
        {
        }

        [[nodiscard]] const HIRType* visitType(const HIRType* type) override {
            auto savedClearOpaque = clearOpaque;
            clearOpaque = false;
            if (type->is_ErasedType()) {
                type = revealOpaqueType(type);
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

    return visitor.visitType(input);
}

const HIRType* StaticTraitResolve::revealOpaqueTypes(const Span& sp, const HIRType* input) const {
    input = this->expandAssociatedTypes(sp, input);
    while (visitTyWith(input, [](const HIRType* inner) {
        return inner->is_ErasedType();
    })) {
        input = this->revealOpaqueTypesShallow(sp, input);
        input = this->expandAssociatedTypes(sp, input);
    }
    return input;
}

void StaticTraitResolve::revealOpaqueTypesPath(const Span& sp, HIRPath& input) const {
    auto revealParams = [&](HIRPathParams& params) {
        for (auto& type : params.types) {
            type = revealOpaqueTypes(sp, type);
        }
    };

    expandAssociatedTypesPath(sp, input);
    switch (input.data.tag()) {
        case HIRPathData::TAG_Generic:
            revealParams(input.data.as_Generic().params);
            break;
        case HIRPathData::TAG_UfcsInherent: {
            auto& path = input.data.as_UfcsInherent();
            path.type = revealOpaqueTypes(sp, path.type);
            revealParams(path.params);
            revealParams(path.implParams);
            break;
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& path = input.data.as_UfcsKnown();
            path.type = revealOpaqueTypes(sp, path.type);
            revealParams(path.trait.params);
            revealParams(path.params);
            break;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            auto& path = input.data.as_UfcsUnknown();
            path.type = revealOpaqueTypes(sp, path.type);
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
    TRACE_FUNCTION_FR(input, input);
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

const HIRType* StaticTraitResolve::expandAssociatedTypesSingle(const Span& sp, const HIRType* input) const {
    TRACE_FUNCTION_F(input);
    if (input->is_Path()) {
        if (input->as_Path().path.data.is_UfcsInherent()) {
            if (const auto* expanded = expandAssociatedTypesUfcsInherent(sp, input)) {
                return expanded;
            }
        }
        if (input->as_Path().path.data.is_UfcsKnown()) {
            return expandAssociatedTypesUfcsKnown(sp, input, /*recurse=*/false);
        }
    }
    return input;
}

bool StaticTraitResolve::typesEqualResolvingOpaque(const Span& sp, const HIRType* left, const HIRType* right) const {
    auto reveal = [&](const HIRType* type) {
        for (unsigned depth = 0; depth < 64; depth++) {
            bool replaced = false;
            auto next = cloneTyWith(crate.types, sp, type, [&](const HIRType* candidate) -> const HIRType* {
                const auto* erased = candidate->opt_ErasedType();
                const auto* alias = erased ? erased->inner.opt_Alias() : nullptr;
                if (!alias || !alias->inner->type) {
                    return nullptr;
                }
                replaced = true;
                return MonomorphStatePtr(crate.types, nullptr, &alias->params, nullptr).monomorphType(sp, alias->inner->type);
            });
            type = next;
            if (!replaced) {
                return type;
            }
        }
        BUG(sp, StringView("Cycle while revealing opaque type ") << type);
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

const HIRType* StaticTraitResolve::expandAssociatedTypesInner(const Span& sp, const HIRType* input) const {
    switch (input->tag()) {
        case HIRType::TAG_Infer:
        case HIRType::TAG_Diverge:
        case HIRType::TAG_Primitive:
        case HIRType::TAG_Generic:
        case HIRType::TAG_NodeType:
            return input;
        case HIRType::TAG_Path: {
            const auto& e = input->as_Path();
            switch (e.path.data.tag()) {
                case HIRPathData::TAG_Generic: {
                    const auto& e2 = e.path.data.as_Generic();
                    bool valueWork = false;
                    for (const auto& v : e2.params.values) {
                        if (v.is_Unevaluated()) {
                            valueWork = true;
                            break;
                        }
                    }
                    size_t tyIdx = e2.params.types.size();
                    const HIRType* nty = nullptr;
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
                    if (const auto* expanded = this->expandAssociatedTypesUfcsInherent(sp, rv)) {
                        rv = this->expandAssociatedTypesInner(sp, expanded);
                    }
                    return rv;
                }
                case HIRPathData::TAG_UfcsKnown: {
                    const bool wasUnbound = e.binding.is_Unbound();
                    const bool wasOpaque = e.binding.is_Opaque();
                    if (!wasUnbound && !wasOpaque) {
                        return input;
                    }

                    if (wasOpaque) {
                        auto rv = input;
                        rv = this->expandAssociatedTypesUfcsKnown(sp, rv, false);
                        if (rv != input) {
                            rv = this->expandAssociatedTypesInner(sp, rv);
                        }
                        return rv;
                    }
                    auto it = atyCache.find(input);
                    if (it != atyCache.end()) {
                        DEBUG(StringView("Cached ") << it->second);
                        return it->second;
                    }
                    auto rv = input;
                    rv = this->expandAssociatedTypesUfcsKnown(sp, rv);
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
        case HIRType::TAG_TraitObject: {
            auto data = input->cloneData();
            auto& e = data.as_TraitObject();
            expandAssociatedTypesTp(sp, e.trait);
            for (auto& m : e.markers) {
                expandAssociatedTypesParams(sp, m.params);
            }
            return crate.types.intern(mv$(data));
        }
        case HIRType::TAG_ErasedType: {
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
        case HIRType::TAG_Array: {
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
        case HIRType::TAG_Slice: {
            auto ninner = expandAssociatedTypesInner(sp, input->as_Slice().inner);
            if (ninner == input->as_Slice().inner) {
                return input;
            }
            auto data = input->cloneData();
            data.as_Slice().inner = ninner;
            return crate.types.intern(mv$(data));
        }
        case HIRType::TAG_Pattern: {
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
        case HIRType::TAG_Tuple: {
            const auto& e = input->as_Tuple();
            for (size_t i = 0; i < e.length(); i++) {
                auto nt = expandAssociatedTypesInner(sp, e[i]);
                if (nt != e[i]) {
                    auto data = input->cloneData();
                    auto& ne = data.as_Tuple();
                    ne.mut(i) = nt;
                    for (size_t j = i + 1; j < ne.length(); j++) {
                        ne.mut(j) = expandAssociatedTypesInner(sp, ne[j]);
                    }
                    return crate.types.intern(mv$(data));
                }
            }
            return input;
        }
        case HIRType::TAG_Borrow: {
            auto ninner = expandAssociatedTypesInner(sp, input->as_Borrow().inner);
            if (ninner == input->as_Borrow().inner) {
                return input;
            }
            auto data = input->cloneData();
            data.as_Borrow().inner = ninner;
            return crate.types.intern(mv$(data));
        }
        case HIRType::TAG_Pointer: {
            auto ninner = expandAssociatedTypesInner(sp, input->as_Pointer().inner);
            if (ninner == input->as_Pointer().inner) {
                return input;
            }
            auto data = input->cloneData();
            data.as_Pointer().inner = ninner;
            return crate.types.intern(mv$(data));
        }
        case HIRType::TAG_NamedFunction: {
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
        case HIRType::TAG_Function: {
            const auto& e = input->as_Function();
            auto nret = expandAssociatedTypesInner(sp, e.rettype);
            size_t argIdx = e.argTypes.length();
            const HIRType* narg = nullptr;
            for (size_t i = 0; i < e.argTypes.length(); i++) {
                narg = expandAssociatedTypesInner(sp, e.argTypes[i]);
                if (narg != e.argTypes[i]) {
                    argIdx = i;
                    break;
                }
            }
            if (nret == e.rettype && argIdx == e.argTypes.length()) {
                return input;
            }
            auto data = input->cloneData();
            auto& ne = data.as_Function();
            ne.rettype = nret;
            if (argIdx < ne.argTypes.length()) {
                ne.argTypes.mut(argIdx) = narg;
                for (size_t j = argIdx + 1; j < ne.argTypes.length(); j++) {
                    ne.argTypes.mut(j) = expandAssociatedTypesInner(sp, ne.argTypes[j]);
                }
            }
            return crate.types.intern(mv$(data));
        }
    }
    return input;
}

const HIRType* StaticTraitResolve::expandAssociatedTypesUfcsInherent(const Span& sp, const HIRType* input) const {
    TRACE_FUNCTION_FR(input, input);
    ASSERT_BUG(sp, input->is_Path() && input->as_Path().path.data.is_UfcsInherent(), input);

    const auto& pe = input->as_Path().path.data.as_UfcsInherent();
    if (visitTyWith(pe.type, [](const HIRType* type) {
        const auto* erased = type->opt_ErasedType();
        const auto* opaque = erased ? erased->inner.opt_Alias() : nullptr;
        return opaque && !opaque->inner->type;
    })) {
        DEBUG(StringView("Deferring inherent associated type with unresolved opaque receiver ") << input);
        return nullptr;
    }
    if (!nextSolver) {
        ASSERT_BUG(sp, crate.pool, StringView("next-solver requires the crate object pool"));
        nextSolver = crate.pool->make<NextSolverBridge>(this->wb);
    }
    auto selection = nextSolver->selectInherentImpl(sp, implGenerics_, itemGenerics_, pe.type, pe.item, InherentItemKind::Type);
    if (selection.certainty != SolverCertainty::Proven || !selection.impl) {
        DEBUG(StringView("No proven inherent associated type candidate for ") << input);
        return nullptr;
    }
    const auto& impl = *selection.impl;
    const auto& alias = impl.types.at(pe.item).data;
    auto implParams = std::move(selection.implParams);
    ConvertHIRConstantEvaluateMethodParams(sp, this->wb, crate, &impl.params, implParams);

    auto itemParams = pe.params.clone();
    if (itemParams.types.size() != alias.params.types.size() || itemParams.values.size() != alias.params.values.size()) {
        ERROR(sp, E0000, StringView("Incorrect generic arguments for inherent associated type ") << input);
    }
    ConvertHIRConstantEvaluateMethodParams(sp, this->wb, crate, &alias.params, itemParams);

    return MonomorphStatePtr(crate.types, pe.type, &implParams, &itemParams).monomorphType(sp, alias.type);
}

const HIRType* StaticTraitResolve::expandAssociatedTypesUfcsKnown(const Span& sp, const HIRType* input, bool recurse /*=true*/) const {
    TRACE_FUNCTION_FR(input, input);
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
    input = crate.types.intern(std::move(data));

    {
        const HIRType* root = input;
        while (const auto* path = root->opt_Path()) {
            const auto* known = path->path.data.opt_UfcsKnown();
            if (!known) {
                break;
            }
            root = known->type;
        }
        if (root->is_Infer()) {
            return input;
        }
    }

    /* A `_` written in a path's generic arguments is a placeholder that only
       expression type checking populates.  Normalizing a projection that still holds
       one would have the solver resolve an inference variable that does not exist
       yet, so leave the projection to the pass that populates it - the same answer
       the unresolved-self case above gives. */
    if (input->hasTypeInfer() && visitTyWith(input, [](const HIRType* inner) {
            const auto* infer = inner->opt_Infer();
            return infer && infer->index == ~0u;
        })) {
        return input;
    }

    if (const auto* replacement = this->replaceEqualities(input)) {
        input = replacement;
        if (recurse) {
            input = this->expandAssociatedTypesInner(sp, input);
        }
        return input;
    }

    if (!nextSolver) {
        ASSERT_BUG(sp, crate.pool, StringView("next-solver requires the crate object pool"));
        nextSolver = crate.pool->make<NextSolverBridge>(this->wb);
    }
    const auto* output = nextSolver->normalize(sp, implGenerics_, itemGenerics_, input);
    if (output != nullptr) {
        input = std::move(output);
        if (recurse) {
            input = this->expandAssociatedTypesInner(sp, input);
        }
        return input;
    }

    auto opaque = input->cloneData();
    opaque.as_Path().binding = HIRTypePathBinding::make_Opaque({});
    return crate.types.intern(std::move(opaque));
}

const HIRType* StaticTraitResolve::replaceEqualities(const HIRType* input) const {
    const Span sp;
    TRACE_FUNCTION_F(StringView("input=") << input);
    DEBUG(StringView("m_type_equalities = {") << typeEqualities << StringView("}"));
    auto a = std::find_if(typeEqualities.begin(), typeEqualities.end(), [&](const auto& entry) {
        return entry.first == input || entry.first->equalsIgnoringRegions(input);
    });
    if (a != typeEqualities.end()) {
        // HACK: Shouldn't need this, but works around some missing cases
        return a->second.ty;
    } else {
        return nullptr;
    }
}

bool StaticTraitResolve::iterateAtyBoundsCb(const Span& sp, const HIRPath::Data::Data_UfcsKnown& pe, StaticTraitPathCallback& cb) const {
    const auto& traitRef = crate.getTraitByPath(sp, pe.trait.path);
    ASSERT_BUG(sp, traitRef.types.count(pe.item) != 0, StringView("Trait ") << pe.trait.path << StringView(" doesn't contain an associated type ") << pe.item);
    const auto& atyDef = traitRef.types.find(pe.item)->second;

    for (const auto& bound : atyDef.traitBounds) {
        if (cb.visit(bound)) {
            return true;
        }
    }
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

bool StaticTraitResolve::findNamedTraitInTraitCb(const Span& sp, const HIRSimplePath& des, const HIRPathParams& desParams, const HIRTrait& traitPtr, const HIRSimplePath& traitPath, const HIRPathParams& pp, const HIRType* targetType, StaticNamedTraitCallback& callback) const {
    TRACE_FUNCTION_F(des << desParams << StringView(" from ") << traitPath << pp);
    if (pp.types.size() != traitPtr.params.types.size()) {
        BUG(sp, StringView("Incorrect number of parameters for trait - ") << traitPath << pp);
    }

    if (des == traitPath) {
        if (paramsMatchProven(sp, pp, desParams)) {
            return callback.visit(pp, {});
        }
    }

    auto monomorph = MonomorphStatePtr(crate.types, targetType, &pp, nullptr);
    for (const auto& pt : traitPtr.allParentTraits) {
        auto ptMono = monomorph.monomorphTraitpath(sp, pt, false);
        this->expandAssociatedTypesTp(sp, ptMono);

        DEBUG(pt << StringView(" => ") << ptMono);

        if (pt.path.path == des && paramsMatchProven(sp, ptMono.path.params, desParams)) {
            return callback.visit(ptMono.path.params, mv$(ptMono.typeBounds));
        }
    }

    return false;
}

bool StaticTraitResolve::traitContainsType(const Span& sp, const HIRGenericPath& traitPath, const HIRTrait& traitPtr, const char* name, HIRGenericPath& outPath) const {
    TRACE_FUNCTION_FR(StringView("name=") << name << StringView(", trait=") << traitPath, outPath);
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

bool StaticTraitResolve::typeIsCopy(const Span& sp, const HIRType* type) const {
    if (const auto it = copyCache.find(type); it != copyCache.end()) {
        return it->second;
    }

    if (!nextSolver) {
        ASSERT_BUG(sp, crate.pool, StringView("next-solver requires the crate object pool"));
        nextSolver = crate.pool->make<NextSolverBridge>(this->wb);
    }
    const bool proven = nextSolver->typeIsCopy(sp, implGenerics_, itemGenerics_, type);
    copyCache.insert(std::make_pair(type, proven));
    return proven;
}

bool StaticTraitResolve::typeIsClone(const Span& sp, const HIRType* type) const {
    if (const auto it = cloneCache.find(type); it != cloneCache.end()) {
        return it->second;
    }

    HIRPathParams params;
    bool proven = false;
    this->findImpl(sp, langClone(), &params, type, [&](SolverSelection) {
        proven = true;
        return proven;
    });
    cloneCache.insert(std::make_pair(type, proven));
    return proven;
}

bool StaticTraitResolve::typeIsSized(const Span& sp, const HIRType* type) const {
    HIRPathParams params;
    bool proven = false;
    this->findImpl(sp, langSized(), &params, type, [&](SolverSelection) {
        proven = true;
        return proven;
    });
    return proven;
}

bool StaticTraitResolve::typeIsImpossible(const Span& sp, const HIRType* ty) const {
    switch ((*ty).tag()) {
        break;
        default:
            return false;
        case HIRType::TAG_Diverge: {
            return true;
        }
        case HIRType::TAG_Path: {
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
                                const HIRType* tmp;
                                const auto& fieldTy = this->monomorphExpandOpt(sp, fld.ent, MonomorphStatePtr(crate.types, ty, &params, nullptr));
                                if (typeIsImpossible(sp, fieldTy)) {
                                    return true;
                                }
                            }
                            return false;
                        }
                        case HIRStructData::TAG_Named: {
                            auto& e = str.data.as_Named();
                            for (const auto& fld : e) {
                                const HIRType* tmp;
                                const auto& fieldTy = this->monomorphExpandOpt(sp, fld.ty, MonomorphStatePtr(crate.types, ty, &params, nullptr));
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
                            for (const auto& fld : e) {
                                const auto& tpl = fld.type;
                                const HIRType* tmp;
                                const auto& fieldTy = this->monomorphExpandOpt(sp, tpl, MonomorphStatePtr(crate.types, ty, &params, nullptr));
                                if (!typeIsImpossible(sp, fieldTy)) {
                                    return false;
                                }
                            }
                            return true;
                        }
                    }
                    TODO(sp, StringView("type_is_impossible for enum ") << ty);
                    break;
                }
                case HIRTypePathBinding::TAG_Union: {
                    // TODO: Check all variants? Or just one?
                    TODO(sp, StringView("type_is_impossible for union ") << ty);
                    break;
                }
                case HIRTypePathBinding::TAG_ExternType: {
                    return false;
                }
            }
            return true;
        }
        case HIRType::TAG_Borrow: {
            auto& e = (*ty).as_Borrow();
            return typeIsImpossible(sp, e.inner);
        }
        case HIRType::TAG_Pointer: {
            return false;
        }
        case HIRType::TAG_Function: {
            // TODO: Check all arguments?
            return true;
        }
        case HIRType::TAG_Array: {
            auto& e = (*ty).as_Array();
            return typeIsImpossible(sp, e.inner);
        }
        case HIRType::TAG_Slice: {
            auto& e = (*ty).as_Slice();
            return typeIsImpossible(sp, e.inner);
        }
        case HIRType::TAG_Pattern: {
            auto& e = (*ty).as_Pattern();
            return typeIsImpossible(sp, e.inner);
        }
        case HIRType::TAG_Tuple: {
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

bool StaticTraitResolve::canUnsize(const Span& sp, const HIRType* dstTy, const HIRType* srcTy) const {
    TRACE_FUNCTION_F(dstTy << StringView(" <- ") << srcTy);
    ASSERT_BUG(sp, !dstTy->is_Infer(), StringView("_ seen after inferrence - ") << dstTy);
    ASSERT_BUG(sp, !srcTy->is_Infer(), StringView("_ seen after inferrence - ") << srcTy);
    return findImpl(sp, langUnsize(), HIRPathParams(dstTy), srcTy, [](SolverSelection) {
        return true;
    });
}

InteriorMutability StaticTraitResolve::typeIsInteriorMutable(const Span& sp, const HIRType* ty) const {
    switch ((*ty).tag()) {
        case HIRType::TAG_Infer: {
            // Is this a bug?
            return InteriorMutability::Unknown;
        }
        case HIRType::TAG_Diverge: {
            return InteriorMutability::No;
        }
        case HIRType::TAG_Primitive: {
            return InteriorMutability::No;
        }
        case HIRType::TAG_Path: {
            auto& e = (*ty).as_Path();
            auto monomorphCb = MonomorphStatePtr(crate.types, nullptr, e.path.data.is_Generic() ? &e.path.data.as_Generic().params : nullptr, nullptr);
            const HIRType* tmpTy;
            auto monomorph = [&](const auto& tpl) -> const HIRType* {
                return this->monomorphExpandOpt(sp, tpl, monomorphCb);
            };
            switch (e.binding.tag()) {
                case HIRTypePathBinding::TAG_Unbound: {
                    return InteriorMutability::Unknown;
                }
                case HIRTypePathBinding::TAG_Opaque: {
                    return InteriorMutability::Unknown;
                }
                case HIRTypePathBinding::TAG_ExternType: {
                    return InteriorMutability::No;
                }
                case HIRTypePathBinding::TAG_Struct: {
                    auto& pbe = e.binding.as_Struct();
                    const HIRGenericPath& p = e.path.data.as_Generic();
                    if (p.path == crate.getLangItemPath(sp, "unsafe_cell")) {
                        return InteriorMutability::Yes;
                    }
                    // TODO: Cache this result?
                    switch (pbe->data.tag()) {
                        case HIRStructData::TAG_Unit: {
                            auto& _ = pbe->data.as_Unit();
                            return InteriorMutability::No;
                        }
                        case HIRStructData::TAG_Tuple: {
                            auto& e = pbe->data.as_Tuple();
                            for (const auto& v : e) {
                                switch (this->typeIsInteriorMutable(sp, monomorph(v.ent))) {
                                    case InteriorMutability::Yes:
                                        return InteriorMutability::Yes;
                                    case InteriorMutability::Unknown:
                                        return InteriorMutability::Unknown;
                                    default:
                                        continue;
                                }
                            }
                            return InteriorMutability::No;
                        }
                        case HIRStructData::TAG_Named: {
                            auto& e = pbe->data.as_Named();
                            for (const auto& v : e) {
                                switch (this->typeIsInteriorMutable(sp, monomorph(v.ty))) {
                                    case InteriorMutability::Yes:
                                        return InteriorMutability::Yes;
                                    case InteriorMutability::Unknown:
                                        return InteriorMutability::Unknown;
                                    default:
                                        continue;
                                }
                            }
                            return InteriorMutability::No;
                        }
                    }
                    break;
                }
                case HIRTypePathBinding::TAG_Enum: {
                    auto& pbe = e.binding.as_Enum();
                    switch (pbe->data.tag()) {
                        case HIREnumClass::TAG_Value: {
                            auto& _ = pbe->data.as_Value();
                            return InteriorMutability::No;
                        }
                        case HIREnumClass::TAG_Data: {
                            auto& ee = pbe->data.as_Data();
                            for (const auto& var : ee) {
                                switch (this->typeIsInteriorMutable(sp, monomorph(var.type))) {
                                    case InteriorMutability::Yes:
                                        return InteriorMutability::Yes;
                                    case InteriorMutability::Unknown:
                                        return InteriorMutability::Unknown;
                                    default:
                                        continue;
                                }
                            }
                            return InteriorMutability::No;
                        }
                    }
                    break;
                }
                case HIRTypePathBinding::TAG_Union: {
                    auto& pbe = e.binding.as_Union();
                    for (const auto& var : pbe->variants) {
                        switch (this->typeIsInteriorMutable(sp, monomorph(var.ty))) {
                            case InteriorMutability::Yes:
                                return InteriorMutability::Yes;
                            case InteriorMutability::Unknown:
                                return InteriorMutability::Unknown;
                            default:
                                continue;
                        }
                    }
                    return InteriorMutability::No;
                }
            }
            break;
        }
        case HIRType::TAG_Generic: {
            return InteriorMutability::Unknown;
        }
        case HIRType::TAG_TraitObject: {
            return InteriorMutability::Unknown;
        }
        case HIRType::TAG_ErasedType: {
            return InteriorMutability::Unknown;
        }
        case HIRType::TAG_Array: {
            auto& e = (*ty).as_Array();
            return this->typeIsInteriorMutable(sp, e.inner);
        }
        case HIRType::TAG_Slice: {
            auto& e = (*ty).as_Slice();
            return this->typeIsInteriorMutable(sp, e.inner);
        }
        case HIRType::TAG_Pattern: {
            auto& e = (*ty).as_Pattern();
            return this->typeIsInteriorMutable(sp, e.inner);
        }
        case HIRType::TAG_Tuple: {
            auto& e = (*ty).as_Tuple();
            for (const auto& t : e) {
                auto rv = this->typeIsInteriorMutable(sp, t);
                if (rv != InteriorMutability::No) {
                    return rv;
                }
            }
            return InteriorMutability::No;
        }
        case HIRType::TAG_NodeType: {
            auto& e = (*ty).as_NodeType();
            switch (e.tag()) {
                case HIRTypeDataNodeType::TAG_Closure: {
                    auto& nodeP = e.as_Closure();
                    if (nodeP->cls == HIRExprNodeClosure::Class::Unknown) {
                        return InteriorMutability::Unknown;
                    }
                    if (nodeP->isCopy) {
                        return InteriorMutability::No;
                    }
                    for (const auto& c : nodeP->captures) {
                        auto rv = this->typeIsInteriorMutable(sp, c->resType);
                        if (rv != InteriorMutability::No) {
                            return rv;
                        }
                    }
                    return InteriorMutability::No;
                }
                case HIRTypeDataNodeType::TAG_Generator: {
                    auto& nodeP = e.as_Generator();
                    for (const auto& c : nodeP->captures) {
                        auto rv = this->typeIsInteriorMutable(sp, c->resType);
                        if (rv != InteriorMutability::No) {
                            return rv;
                        }
                    }
                    return InteriorMutability::No;
                }
                case HIRTypeDataNodeType::TAG_Async: {
                    TODO(sp, StringView("type_is_interior_mutable on async"));
                    break;
                }
            }
            break;
        }
        case HIRType::TAG_Borrow: {
            return InteriorMutability::No;
        }
        case HIRType::TAG_Pointer: {
            return InteriorMutability::No;
        }
        case HIRType::TAG_NamedFunction: {
            return InteriorMutability::No;
        }
        case HIRType::TAG_Function: {
            return InteriorMutability::No;
        }
    }
    return InteriorMutability::Unknown;
}

MetadataType StaticTraitResolve::metadataType(const Span& sp, const HIRType* ty, bool errOnUnknown /*=false*/) const {
    switch ((*ty).tag()) {
        default:
            return MetadataType::None;
        case HIRType::TAG_Generic: {
            auto& e = (*ty).as_Generic();
            if (this->typeIsSized(sp, ty)) {
                return MetadataType::None;
            }
            if (e.binding == 0xFFFF) {
                ASSERT_BUG(sp, implGenerics_, StringView("Use of `Self` with no self type (no impl generics)"));
                return selfMetadata;
            } else if ((e.binding >> 8) == 0) {
                auto idx = e.binding & 0xFF;
                ASSERT_BUG(sp, implGenerics_, StringView("Encountered generic ") << ty << StringView(" without impl generics available"));
                ASSERT_BUG(sp, idx < implGenerics_->types.size(), StringView("Encountered generic ") << ty << StringView(" out of range of impl generic spec"));
                if (implGenerics_->types[idx].isSized) {
                    return MetadataType::None;
                } else {
                    return MetadataType::Unknown;
                }
            } else if ((e.binding >> 8) == 1) {
                auto idx = e.binding & 0xFF;
                ASSERT_BUG(sp, itemGenerics_, StringView("Encountered generic ") << ty << StringView(" without item generics available"));
                ASSERT_BUG(sp, idx < itemGenerics_->types.size(), StringView("Encountered generic ") << ty << StringView(" out of range of item generic spec"));
                if (itemGenerics_->types[idx].isSized) {
                    return MetadataType::None;
                } else {
                    return MetadataType::Unknown;
                }
            } else if (e.isPlaceholder()) {
                return MetadataType::None;
            } else {
                BUG(sp, StringView("Unknown generic binding on ") << ty);
            }
            break;
        }
        case HIRType::TAG_ErasedType: {
            auto& e = (*ty).as_ErasedType();
            if (e.isSized) {
                return MetadataType::None;
            } else {
                return MetadataType::Unknown;
            }
            break;
        }
        case HIRType::TAG_Path: {
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
        case HIRType::TAG_Infer: {
            return MetadataType::Unknown;
        }
        case HIRType::TAG_Diverge: {
            return MetadataType::None;
        }
        case HIRType::TAG_Primitive: {
            auto& e = (*ty).as_Primitive();
            if (e == HIRCoreType::Str) {
                return MetadataType::Slice;
            } else {
                return MetadataType::None;
            }
            break;
        }
        case HIRType::TAG_Slice: {
            return MetadataType::Slice;
        }
        case HIRType::TAG_Pattern: {
            auto& e = (*ty).as_Pattern();
            return this->metadataType(sp, e.inner, errOnUnknown);
        }
        case HIRType::TAG_TraitObject: {
            return MetadataType::TraitObject;
        }
        case HIRType::TAG_Tuple: {
            auto& e = (*ty).as_Tuple();
            return e.empty() ? MetadataType::None : this->metadataType(sp, e.back(), errOnUnknown);
        }
    }
    UNREACHABLE();
}

bool StaticTraitResolve::typeNeedsDropGlue(const Span& sp, const HIRType* ty) const {
    if (langDrop().components().empty()) {
        return false;
    }

    if (typeIsCopy(sp, ty)) {
        return false;
    }

    switch ((*ty).tag()) {
        case HIRType::TAG_Generic: {
            // TODO: Is this an error?
            return true;
        }
        case HIRType::TAG_Path: {
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
            bool hasDirectDrop = this->findImpl(sp, langDrop(), &pp, ty, [&](SolverSelection) {
                return true;
            });
            if (hasDirectDrop) {
                dropCache.insert(std::make_pair(ty, true));
                return true;
            }

            const HIRType* tmpTy;
            const auto& pe = e.path.data.as_Generic();
            auto monomorphCb = MonomorphStatePtr(crate.types, ty, &pe.params, nullptr);
            auto monomorph = [&](const auto& tpl) -> const HIRType* {
                return this->monomorphExpandOpt(sp, tpl, monomorphCb);
            };
            bool needsDropGlue = false;
            switch (e.binding.tag()) {
                case HIRTypePathBinding::TAG_Unbound: {
                    BUG(sp, StringView("Unbound path"));
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
                    needsDropGlue = false;
                    break;
                }
                case HIRTypePathBinding::TAG_ExternType: {
                    needsDropGlue = false;
                    break;
                }
            }
            dropCache.insert(std::make_pair(ty, needsDropGlue));
            return needsDropGlue;
        }
        case HIRType::TAG_Diverge: {
            return false;
        }
        case HIRType::TAG_NodeType: {
            return true;
        }
        case HIRType::TAG_Infer: {
            BUG(sp, StringView("type_needs_drop_glue on _"));
            return false;
        }
        case HIRType::TAG_Borrow: {
            auto& e = (*ty).as_Borrow();
            if (e.type != HIRBorrowType::Owned) {
                return false;
            }
            return typeNeedsDropGlue(sp, e.inner);
        }
        case HIRType::TAG_Pointer: {
            return false;
        }
        case HIRType::TAG_NamedFunction: {
            return false;
        }
        case HIRType::TAG_Function: {
            return false;
        }
        case HIRType::TAG_Primitive: {
            return false;
        }
        case HIRType::TAG_Array: {
            auto& e = (*ty).as_Array();
            return typeNeedsDropGlue(sp, e.inner);
        }
        case HIRType::TAG_Slice: {
            auto& e = (*ty).as_Slice();
            return typeNeedsDropGlue(sp, e.inner);
        }
        case HIRType::TAG_Pattern: {
            auto& e = (*ty).as_Pattern();
            return typeNeedsDropGlue(sp, e.inner);
        }
        case HIRType::TAG_TraitObject: {
            return true;
        }
        case HIRType::TAG_ErasedType: {
            return true;
        }
        case HIRType::TAG_Tuple: {
            auto& e = (*ty).as_Tuple();
            for (const auto& ty : e) {
                if (typeNeedsDropGlue(sp, ty)) {
                    return true;
                }
            }
            return false;
        }
    }
    BUG_ASSERT(!"Fell off the end of type_needs_drop_glue");
    UNREACHABLE();
}

const HIRType* StaticTraitResolve::findAsyncDrop(const Span& sp, const HIRType* ty, HIRPath& path) const {
    const auto& trait = crate.getLangItemPathOpt("async_drop");
    if (trait.components().empty() || monomorphiseTypeNeeded(ty)) {
        return nullptr;
    }

    bool found = false;
    findImpl(sp, trait, HIRPathParams{}, ty, [&](SolverSelection selection) {
        if (selection.impl.traitImpl) {
            found = true;
            return true;
        }
        return false;
    });
    if (!found) {
        return nullptr;
    }

    path = HIRPath(ty, HIRGenericPath(trait), RcString::newInterned("drop"), HIRPathParams{});
    MonomorphState params(crate.types);
    auto value = getValue(sp, path, params);
    const auto* function = value.opt_Function();
    ASSERT_BUG(sp, function, StringView("AsyncDrop::drop did not resolve for ") << ty);
    return expandAssociatedTypes(sp, params.monomorphType(sp, (*function)->returnType));
}

bool StaticTraitResolve::typeNeedsAsyncDropInner(const Span& sp, const HIRType* ty, HIRTypeRefSet& stack) const {
    HIRPath path{HIRSimplePath()};
    if (findAsyncDrop(sp, ty, path)) {
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
                    ASSERT_BUG(sp, fields && !fields->empty(), StringView("coroutine without its state field: ") << ty);
                    for (size_t i = 0; i < fields->size(); i++) {
                        auto fieldTy = monomorph.monomorphType(sp, fields->at(i).ent);
                        fieldTy = expandAssociatedTypes(sp, fieldTy);
                        if (i == 0) {
                            const auto* fieldPath = fieldTy->opt_Path();
                            ASSERT_BUG(sp, fieldPath && fieldPath->path.data.is_Generic() && fieldPath->path.data.as_Generic().path == crate.getLangItemPath(sp, "maybe_uninit") && fieldPath->path.data.as_Generic().params.types.size() == 1, StringView("coroutine state is not MaybeUninit<State>: ") << fieldTy);
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
                            fieldTy = expandAssociatedTypes(sp, fieldTy);
                            if (typeNeedsAsyncDropInner(sp, fieldTy, stack)) {
                                rv = true;
                                break;
                            }
                        }
                        break;
                    case HIRStructData::TAG_Named:
                        for (const auto& field : ((*str)->data).as_Named()) {
                            auto fieldTy = monomorph.monomorphType(sp, field.ty);
                            fieldTy = expandAssociatedTypes(sp, fieldTy);
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
                        fieldTy = expandAssociatedTypes(sp, fieldTy);
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

bool StaticTraitResolve::typeNeedsAsyncDrop(const Span& sp, const HIRType* ty) const {
    HIRTypeRefSet stack;
    return typeNeedsAsyncDropInner(sp, ty, stack);
}

const HIRType* StaticTraitResolve::isTypeOwnedBox(const HIRType* ty) const {
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

const HIRType* StaticTraitResolve::isTypePhantomData(const HIRType* ty) const {
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

const HIRType* StaticTraitResolve::getFieldType(const Span& sp, const HIRType* ty, const RcString& name) const {
    switch ((*ty).tag()) {
        default:
            TODO(sp, StringView("") << ty << StringView(" ") << name);
        case HIRType::TAG_Borrow: {
            auto& te = (*ty).as_Borrow();
            ASSERT_BUG(sp, name == RcString(), StringView("get_field_type: Deref with non-empty field (`") << name << StringView("`)"));
            return te.inner;
        }
        case HIRType::TAG_Tuple: {
            auto& te = (*ty).as_Tuple();
            std::stringstream ss{name.c_str()};
            int idx = -1;
            ss >> idx;
            ASSERT_BUG(sp, idx >= 0, StringView("Malformed tuple index field name - `") << name << StringView("`"));
            ASSERT_BUG(sp, size_t(idx) < te.length(), StringView("Tuple index out of bounds"));
            return te[idx];
        }
        case HIRType::TAG_Path: {
            auto& te = (*ty).as_Path();
            switch (te.binding.tag()) {
                default:
                    BUG(sp, StringView("Getting field on invalid type - ") << ty);
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
                            BUG(sp, StringView("Unknown field `") << name << StringView("` on ") << ty);
                            break;
                        }
                        case HIRStructData::TAG_Tuple: {
                            auto& se = pbe->data.as_Tuple();
                            unsigned index = std::strtol(name.c_str(), nullptr, 10);
                            ASSERT_BUG(sp, index < se.size(), StringView("") << ty << StringView(" ") << name);
                            return ms.monomorphType(sp, se.at(index).ent);
                        }
                        case HIRStructData::TAG_Unit: {
                            BUG(sp, StringView("Getting field from unit-like struct - ") << ty);
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
                    BUG(sp, StringView("Unknown field `") << name << StringView("` on ") << ty);
                    break;
                }
            }
            break;
        }
    }
    BUG(sp, StringView("Reached end of `get_field_type` - ") << ty);
}

StaticTraitResolve::ValuePtr StaticTraitResolve::getValue(const Span& sp, const HIRPath& p, MonomorphState& outParams, bool signatureOnly /*=false*/, const HIRGenericParams** outImplParamsDef /*=nullptr*/, ResolvedTraitImplPath* outTraitImplPath /*=nullptr*/) const {
    TRACE_FUNCTION_F(p << StringView(", signature_only=") << signatureOnly);
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
                    BUG(sp, StringView("Module Import"));
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
                    TODO(sp, StringView("StructConstant - ") << p);
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
                DEBUG(StringView("Empty trait VTable, return NotYetKnown"));
                return ValuePtr::make_NotYetKnown({});
            }
            outParams.selfTy = pe.type;
            outParams.ppImpl = &pe.trait.params;
            outParams.ppMethod = &pe.params;
            const HIRTrait& tr = crate.getTraitByPath(sp, pe.trait.path);
            if (!tr.values.count(pe.item)) {
                DEBUG(StringView("Value ") << pe.item << StringView(" not found in trait ") << pe.trait.path);
                return ValuePtr();
                DEBUG(StringView("Pre-existing imp params = ") << *outParams.ppImpl);
            }

            if (outImplParamsDef) {
                *outImplParamsDef = &tr.params;
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
                bool selectedIsSpecialisable = false;
                bool hasBoundedImpl = false;
                bool hasAmbiguousImpl = false;
                const SolverImpl* selectedImpl = nullptr;
                ValuePtr rv;
                bool lookupNeedsResolution = specializationLookupNeedsResolution(pe.type, pe.trait.params);
                auto visitImpl = [&](SolverMayApply probe) -> bool {
                    if (!probe.candidate) {
                        hasAmbiguousImpl |= probe.effects.certainty == SolverCertainty::Ambiguous;
                        return false;
                    }
                    DEBUG(probe.candidate->traitPath << StringView(" for ") << probe.candidate->type);
                    if (!probe.candidate->traitImpl) {
                        hasBoundedImpl = true;
                        return false;
                    }
                    if (probe.effects.certainty != SolverCertainty::Proven && lookupNeedsResolution) {
                        hasAmbiguousImpl = true;
                        return false;
                    }
                    const HIRTraitImpl& ti = *probe.candidate->traitImpl;
                    bool isSpec = false;

                    ValuePtr thisRv;
                    if (thisRv.is_NotFound()) {
                        auto it = ti.constants.find(pe.item);
                        if (it != ti.constants.end()) {
                            isSpec = it->second.isSpecialisable;
                            thisRv = &it->second.data;
                        }
                    }
                    if (thisRv.is_NotFound()) {
                        auto it = ti.statics.find(pe.item);
                        if (it != ti.statics.end()) {
                            isSpec = it->second.isSpecialisable;
                            thisRv = &it->second.data;
                        }
                    }
                    if (thisRv.is_NotFound()) {
                        auto it = ti.methods.find(pe.item);
                        if (it != ti.methods.end()) {
                            isSpec = it->second.isSpecialisable;
                            thisRv = &it->second.data;
                        }
                    }

                    if (thisRv.is_NotFound()) {
                        DEBUG(StringView("- Missing the target item"));
                        return false;
                    }
                    selectedIsSpecialisable = isSpec;
                    selectedImpl = probe.candidate;
                    rv = std::move(thisRv);
                    return false;
                };

                if (!nextSolver) {
                    ASSERT_BUG(sp, crate.pool, StringView("next-solver requires the crate object pool"));
                    nextSolver = crate.pool->make<NextSolverBridge>(this->wb);
                }
                SolverMayApplyCb<decltype(visitImpl)> callback(visitImpl);
                nextSolver->findValue(sp, implGenerics_, itemGenerics_, pe.trait.path, pe.trait.params, pe.type, pe.item.c_str(), callback);
                if (!selectedImpl) {
                    if (hasBoundedImpl || hasAmbiguousImpl) {
                        DEBUG(StringView("Trait item depends on an in-scope bound or fuzzy impl"));
                        return ValuePtr::make_NotYetKnown({});
                    }
                    if (!monomorphiseTypeNeeded(pe.type) && !monomorphisePathparamsNeeded(pe.trait.params)) {
                        switch (v.tag()) {
                            case HIRTraitValueItem::TAG_Constant: {
                                auto& ve = v.as_Constant();
                                if (ve.value || ve.valueState != HIRConstant::ValueState::Unknown) {
                                    DEBUG(StringView("Trait provided value"));
                                    return &ve;
                                } else {
                                    DEBUG(StringView("Trait did not provide a value"));
                                }
                                break;
                            }
                            case HIRTraitValueItem::TAG_Static: {
                                break;
                            }
                            case HIRTraitValueItem::TAG_Function: {
                                auto& ve = v.as_Function();
                                if (ve.code || ve.code.mir) {
                                    DEBUG(StringView("Trait provided body"));
                                    return &ve;
                                }
                                break;
                            }
                        }
                    } else {
                        DEBUG(StringView("No best impl, but monomorph needed - can't check trait"));
                    }
                    return ValuePtr::make_NotYetKnown({});
                }
                if (selectedIsSpecialisable) {
                    if (monomorphiseTypeNeeded(pe.type) || monomorphisePathparamsNeeded(pe.trait.params)) {
                        DEBUG(StringView("Specialisable and still generic, return NotYetKnown"));
                        return ValuePtr::make_NotYetKnown({});
                    }
                }

                ASSERT_BUG(sp, selectedImpl->traitImpl, StringView("Selected trait value has no concrete impl: ") << p);
                const auto& selected = *selectedImpl;
                const auto& impl = *selected.traitImpl;
                if (outImplParamsDef) {
                    *outImplParamsDef = &impl.params;
                }
                if (outTraitImplPath && !impl.params.isGeneric()) {
                    outTraitImplPath->type = impl.type;
                    outTraitImplPath->traitParams = impl.traitArgs.clone();
                }
                outParams.ppImpl = &outParams.ppImplData;
                outParams.ppImplData = selected.implParams.clone();
                ASSERT_BUG(sp, !rv.is_NotFound(), StringView(""));
                return rv;
            }
            UNREACHABLE();
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& pe = p.data.as_UfcsInherent();
            outParams.selfTy = pe.type;
            outParams.ppMethod = &pe.params;
            if (!nextSolver) {
                ASSERT_BUG(sp, crate.pool, StringView("next-solver requires the crate object pool"));
                nextSolver = crate.pool->make<NextSolverBridge>(this->wb);
            }
            auto selection = nextSolver->selectInherentImpl(sp, implGenerics_, itemGenerics_, pe.type, pe.item, InherentItemKind::Value, &pe.implParams);
            if (selection.certainty == SolverCertainty::Ambiguous) {
                return ValuePtr::make_NotYetKnown({});
            }
            if (selection.certainty == SolverCertainty::NoSolution || !selection.impl) {
                return ValuePtr();
            }
            const auto& impl = *selection.impl;
            ValuePtr value;
            if (auto fit = impl.methods.find(pe.item); fit != impl.methods.end()) {
                value = ValuePtr{&fit->second.data};
            } else {
                value = ValuePtr{&impl.constants.at(pe.item).data};
            }
            ASSERT_BUG(sp, impl.params.types.size() == selection.implParams.types.size(), StringView("Mismatch in param counts `") << selection.implParams << StringView("`, params are `") << impl.params.fmtArgs() << StringView("`\n- in ") << p);
            ASSERT_BUG(sp, impl.params.values.size() == selection.implParams.values.size(), StringView("Mismatch in value param counts `") << selection.implParams << StringView("`, params are `") << impl.params.fmtArgs() << StringView("`\n- in ") << p);
            if (outImplParamsDef) {
                *outImplParamsDef = &impl.params;
            }
            outParams.ppImplData = std::move(selection.implParams);
            outParams.ppImpl = &outParams.ppImplData;
            return value;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            BUG(sp, StringView("UfcsUnknown - ") << p);
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
    TraitResolveCommon::prepIndexes(Span());
}

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

NullOnDrop<const HIRGenericParams> StaticTraitResolve::setImplGenerics(const HIRType* selfTy, const HIRGenericParams& gps) {
    setImplGenericsRaw(MetadataType::Unknown, gps);
    selfMetadata = metadataType(Span(), selfTy);
    return NullOnDrop<const HIRGenericParams>(implGenerics_);
}

void StaticTraitResolve::updateImplSelfMetadata(const HIRType* selfTy) {
    BUG_ASSERT(implGenerics_);
    selfMetadata = metadataType(Span(), selfTy);
}

NullOnDrop<const HIRGenericParams> StaticTraitResolve::setItemGenerics(const HIRGenericParams& gps) {
    setItemGenericsRaw(gps);
    return NullOnDrop<const HIRGenericParams>(itemGenerics_);
}

void StaticTraitResolve::setImplGenericsRaw(MetadataType selfMetaType, const HIRGenericParams& gps) {
    BUG_ASSERT(!implGenerics_);
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
    BUG_ASSERT(!itemGenerics_);
    itemGenerics_ = &gps;
    prepIndexes();
}

void StaticTraitResolve::clearItemGenerics() {
    itemGenerics_ = nullptr;
    prepIndexes();
}

void StaticTraitResolve::setBothGenericsRaw(const HIRGenericParams* gpsImpl, const HIRGenericParams* gpsFcn) {
    BUG_ASSERT(!implGenerics_);
    BUG_ASSERT(!itemGenerics_);
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

const HIRType* StaticTraitResolve::monomorphExpandOpt(const Span& sp, const HIRType* input, const Monomorphiser& m) const {
    if (monomorphiseTypeNeeded(input)) {
        return monomorphExpand(sp, input, m);
    } else {
        return input;
    }
}

const HIRType* StaticTraitResolve::monomorphExpand(const Span& sp, const HIRType* input, const Monomorphiser& m) const {
    auto rv = m.monomorphType(sp, input);
    rv = expandAssociatedTypes(sp, rv);
    return rv;
}

StaticTraitResolve::NextSolverBridge::NextSolverBridge(const WireBoard& wb)
    : ivars(wb.crate->types)
    , visibility(wb.crate->crateName, {})
    , resolve_(ivars, wb, nullptr, nullptr, visibility, nullptr)
{
}

auto StaticTraitResolve::NextSolverBridge::findImpl(const Span& sp, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRSimplePath& trait, const HIRPathParams* params, const HIRType* type, SolverResponseCallback& callback) -> bool {
    resolve_.setGenericContext(implGenerics, itemGenerics);

    HIRPathParams inferredParams;
    if (!params) {
        const auto& traitDef = resolve_.hirCrate().getTraitByPath(sp, trait);
        inferredParams = resolve_.solverExistentials(sp, traitDef.params).clone();
        params = &inferredParams;
    }

    return resolve_.solveTraitGoalCb(sp, trait, *params, type, callback, {.ambiguity = SolverAmbiguityPolicy::Report});
}

auto StaticTraitResolve::NextSolverBridge::findValue(const Span& sp, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const char* valueName, SolverResponseCallback& callback) -> bool {
    resolve_.setGenericContext(implGenerics, itemGenerics);
    return resolve_.solveTraitGoalCb(sp, trait, params, type, callback, {.valueName = valueName});
}

auto StaticTraitResolve::NextSolverBridge::normalize(const Span& sp, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRType* projection) -> const HIRType* {
    resolve_.setGenericContext(implGenerics, itemGenerics);
    const HIRType* output = nullptr;
    resolve_.solveNormalizesTo(sp, NormalizesTo{projection}, [&](NormalizesToResponse response) {
        if (response.output != nullptr && response.output != projection) {
            output = std::move(response.output);
        }
        return true;
    });
    return output;
}

auto StaticTraitResolve::NextSolverBridge::typeIsCopy(const Span& sp, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRType* type) -> bool {
    resolve_.setGenericContext(implGenerics, itemGenerics);
    return resolve_.typeIsCopy(sp, type) == SolverCertainty::Proven;
}

auto StaticTraitResolve::NextSolverBridge::selectInherentImpl(const Span& sp, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRType* receiver, const RcString& item, InherentItemKind kind, const HIRPathParams* initialParams) -> InherentImplSelection {
    resolve_.setGenericContext(implGenerics, itemGenerics);
    return resolve_.selectInherentImpl(sp, receiver, item, kind, initialParams);
}

template <>
void stl::output<ZeroCopyOutput, MetadataType>(ZeroCopyOutput& out, MetadataType value) {
    switch (value) {
        case MetadataType::Unknown:
            out << StringView("Unknown");
            return;
        case MetadataType::None:
            out << StringView("None");
            return;
        case MetadataType::Zero:
            out << StringView("Zero");
            return;
        case MetadataType::Slice:
            out << StringView("Slice");
            return;
        case MetadataType::TraitObject:
            out << StringView("TraitObject");
            return;
    }
    out << StringView("?");
}
