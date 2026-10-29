#include "hir_typeck_main_bindings.h"

#include "hir_hir.h"
#include "hir_expr.h"
#include "wire_board.h"
#include "hir_visitor.h"
#include "hir_typeck_static.h"
#include "hir_typeck_monomorph.h"

#include <std/alg/range.h>
#include <std/lib/vector.h>

#include <algorithm>

using namespace stl;

namespace {
    struct TypecheckVisitor: public HIRVisitor {
        HIRCrate& crate;
        StaticTraitResolve resolve_;
        HIRGenericParams emptyParams;

        const HIRTrait* currentTrait = nullptr;
        const HIRItemPath* currentTraitPath_ = nullptr;

        HIRGenericParams* curParams = nullptr;
        unsigned curParamsLevel = 0;
        HIRItemPath* fcnPath = nullptr;
        HIRFunction* fcnPtr = nullptr;
        unsigned int fcnErasedCount = 0;
        bool checkingFunctionSignature = false;
        bool checkingTypeDeclarationParams = false;

        Vector<const HIRType*> selfTypes;

        TypecheckVisitor(const WireBoard& wb, HIRCrate& crate);

        static bool traitBoundSatisfied(const Span& sp, const StaticTraitResolve& resolve, const HIRType* type, const HIRTraitPath& trait);

        static bool traitParamsMayHaveAssociatedType(const HIRPathParams& params);

        void checkParameters(const Span& sp, const HIRSimplePath& usedPath, PathContext pc, const HIRGenericParams& paramDef, HIRPathParams& paramVals);

        void visitPathParams(HIRPathParams& pp) override;

        [[nodiscard]] const HIRType* visitType(const HIRType* ty) override;

        void visitGenericPath(HIRGenericPath& p, PathContext pc) override;

        HIRGenericPath getCurrentTraitGp() const;

        void visitExpr(HIRExprPtr& exp) override;

        void visitPath(HIRPath& p, HIRVisitor::PathContext pc) override;

        void visitParams(HIRGenericParams& params) override;

        void visitTrait(HIRItemPath p, HIRTrait& item) override;

        void visitTraitAlias(HIRItemPath p, HIRTraitAlias& item) override;

        void visitStruct(HIRItemPath p, HIRStruct& item) override;

        void visitUnion(HIRItemPath p, HIRUnion& item) override;

        void visitEnum(HIRItemPath p, HIREnum& item) override;

        void visitAssociatedtype(HIRItemPath p, HIRAssociatedType& item) override;

        void visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) override;

        void visitInherentType(HIRItemPath p, HIRTypeAlias& item) override;

        void visitTypeImpl(HIRTypeImpl& impl) override;

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override;

        void visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) override;

        void visitFunction(HIRItemPath p, HIRFunction& item) override;
    };

    const HIRGenericParams& getParamsForItem(const Span& sp, const HIRCrate& crate, const HIRSimplePath& path, HIRVisitor::PathContext pc, const HIRGenericParams& emptyParams) {
        if (path.components().size() > 1) {
            const auto& pitem = crate.getTypeitemByPath(sp, path, false, true);
            if (pitem.is_Enum()) {
                return pitem.as_Enum().params;
            }
        }

        switch (pc) {
            case HIRVisitor::PathContext::VALUE: {
                const auto& item = crate.getValitemByPath(sp, path);

                switch (item.tag()) {
                    case HIRValueItem::TAG_Import: {
                        auto& e = item.as_Import();
                        BUG(sp, StringView("Value path pointed to import - ") << path << StringView(" = ") << e.path);
                        break;
                    }
                    case HIRValueItem::TAG_Function: {
                        return item.as_Function()->params;
                    }
                    case HIRValueItem::TAG_Constant: {
                        return item.as_Constant()->params;
                    }
                    case HIRValueItem::TAG_Static: {
                        // TODO: Return an empty set?
                        BUG(sp, StringView("Attepted to get parameters for static ") << path);
                        break;
                    }
                    case HIRValueItem::TAG_StructConstructor: {
                        auto& e = item.as_StructConstructor();
                        return getParamsForItem(sp, crate, e.ty, HIRVisitor::PathContext::TYPE, emptyParams);
                    }
                    case HIRValueItem::TAG_StructConstant: {
                        auto& e = item.as_StructConstant();
                        return getParamsForItem(sp, crate, e.ty, HIRVisitor::PathContext::TYPE, emptyParams);
                    }
                }
            } break;
            case HIRVisitor::PathContext::TRAIT:
                // TODO: treat PathContext::TRAIT differently
            case HIRVisitor::PathContext::TYPE: {
                const auto& item = crate.getTypeitemByPath(sp, path);

                switch (item.tag()) {
                    case HIRTypeItem::TAG_Import: {
                        BUG(sp, StringView("Type path pointed to import - ") << path);
                        break;
                    }
                    case HIRTypeItem::TAG_TypeAlias: {
                        BUG(sp, StringView("Type path pointed to type alias - ") << path);
                        break;
                    }
                    case HIRTypeItem::TAG_TraitAlias: {
                        BUG(sp, StringView("Type path pointed to trait alias - ") << path);
                        break;
                    }
                    case HIRTypeItem::TAG_ExternType: {
                        return emptyParams;
                        break;
                    }
                    case HIRTypeItem::TAG_Module: {
                        BUG(sp, StringView("Type path pointed to module - ") << path);
                        break;
                    }
                    case HIRTypeItem::TAG_Struct: {
                        auto& e = item.as_Struct();
                        return e.params;
                    }
                    case HIRTypeItem::TAG_Enum: {
                        auto& e = item.as_Enum();
                        return e.params;
                    }
                    case HIRTypeItem::TAG_Union: {
                        auto& e = item.as_Union();
                        return e.params;
                    }
                    case HIRTypeItem::TAG_Trait: {
                        auto& e = item.as_Trait();
                        return e.params;
                    }
                }
            } break;
        }
        UNREACHABLE();
    }
}

void TypecheckModuleLevel(const WireBoard& wb, HIRCrate& crate) {
    TypecheckVisitor v{wb, crate};
    v.visitCrate(crate);
}

TypecheckVisitor::TypecheckVisitor(const WireBoard& wb, HIRCrate& crate)
    : HIRVisitor(nullptr, crate.types)
    , crate(crate)
    , resolve_(wb)
{
}

auto TypecheckVisitor::traitBoundSatisfied(const Span& sp, const StaticTraitResolve& resolve, const HIRType* type, const HIRTraitPath& trait) -> bool {
    return resolve.findImpl(sp, trait.path.path, &trait.path.params, type, [](SolverSelection) {
        return true;
    });
}

auto TypecheckVisitor::traitParamsMayHaveAssociatedType(const HIRPathParams& params) -> bool {
    for (const auto& type : params.types) {
        if (type->mayHaveAssociatedType()) {
            return true;
        }
    }
    return false;
}

auto TypecheckVisitor::checkParameters(const Span& sp, const HIRSimplePath& usedPath, PathContext pc, const HIRGenericParams& paramDef, HIRPathParams& paramVals) -> void {
    MonomorphStatePtr ms(crate.types, selfTypes.empty() ? nullptr : selfTypes.back(), &paramVals, nullptr);

    while (paramVals.types.size() < paramDef.types.size()) {
        unsigned int i = paramVals.types.size();
        const auto& tyDef = paramDef.types[i];
        if (tyDef.defaultValue->is_Infer()) {
            ERROR(sp, E0000, StringView("Unspecified parameter with no default - ") << paramDef.fmtArgs() << StringView(" with ") << paramVals);
        }

        paramVals = paramVals.appended(ms.monomorphType(sp, tyDef.defaultValue));
        DEBUG(StringView("Add missing param (using default): ") << paramVals.types.back());
    }

    if (paramVals.types.size() != paramDef.types.size()) {
        ERROR(sp, E0000, StringView("Incorrect number of parameters - expected ") << paramDef.types.size() << StringView(", got ") << paramVals.types.size());
    }

    for (unsigned int i = 0; i < paramVals.types.size(); i++) {
        if (paramVals.types[i] == nullptr) {
            // TODO: Why is this pulling in the default? Why not just leave it as-is

            // TODO: Monomorphise?
            paramVals = paramVals.withType(i, ms.monomorphType(sp, paramDef.types[i].defaultValue));
            DEBUG(StringView("Update `_` param (using default): ") << paramDef.types[i].defaultValue << StringView(" -> ") << paramVals.types[i]);
        }
    }

    for (const auto& bound : paramDef.bounds) {
        switch (bound.tag()) {
            case HIRGenericBound::TAG_TraitBound: {
                const auto& e = bound.as_TraitBound();
                const auto* boundedParam = e.type->opt_Generic();
                if (boundedParam && boundedParam->isSelf() && e.trait.path.path == usedPath) {
                    break;
                }
                if (!checkingFunctionSignature || pc != PathContext::TYPE || !boundedParam || boundedParam->group() != 0) {
                    break;
                }
                auto type = ms.monomorphType(sp, e.type);
                auto trait = ms.monomorphTraitpath(sp, e.trait, false);
                if (type->mayHaveAssociatedType() || traitParamsMayHaveAssociatedType(trait.path.params)) {
                    break;
                }
                if (const auto* actualGeneric = type->opt_Generic()) {
                    const auto* context = actualGeneric->group() == 0 ? resolve_.implGenericsPtr() : actualGeneric->group() == 1 ? resolve_.itemGenericsPtr() : nullptr;
                    if (!context || actualGeneric->idx() >= context->types.size()) {
                        break;
                    }
                }
                if (!traitBoundSatisfied(sp, resolve_, type, trait)) {
                    ERROR(sp, E0000, StringView("trait bound `") << type << StringView(": ") << trait.path << StringView("` is not satisfied"));
                }
                break;
            }
        }
    }
}

auto TypecheckVisitor::visitPathParams(HIRPathParams& pp) -> void {
    Span _sp;
    const Span& sp = _sp;

    HIRVisitor::visitPathParams(pp);
}

[[nodiscard]] auto TypecheckVisitor::visitType(const HIRType* ty) -> const HIRType* {
    Span _sp;
    const Span& sp = _sp;

    BUG_ASSERT(ty);
    auto data = ty->cloneData();

    auto self = crate.types.self();
    if (data.is_ErasedType()) {
        selfTypes.pushBack(self);
    }

    auto savedParams = std::make_pair(curParams, curParamsLevel);

    switch (data.tag()) {
        case HIRType::TAG_Infer: {
            if (checkingFunctionSignature) {
                ERROR(sp, E0000, StringView("the placeholder `_` is not allowed in function signatures"));
            }
            break;
        }
        case HIRType::TAG_Diverge: {
            break;
        }
        case HIRType::TAG_Primitive: {
            break;
        }
        case HIRType::TAG_Generic: {
            break;
        }
        case HIRType::TAG_Path: {
            auto& e = data.as_Path();
            this->visitPath(e.path, HIRVisitor::PathContext::TYPE);
            break;
        }
        case HIRType::TAG_TraitObject: {
            auto& e = data.as_TraitObject();
            if (e.trait.path != HIRSimplePath()) {
                this->visitTraitPath(e.trait);
            }
            for (auto& marker : e.markers) {
                this->visitGenericPath(marker, HIRVisitor::PathContext::TYPE);
            }
            break;
        }
        case HIRType::TAG_ErasedType: {
            auto& e = data.as_ErasedType();
            {
                auto& tuMatch = e.inner;
                switch (tuMatch.tag()) {
                    case TypeDataErasedTypeInner::TAG_Alias: {
                        auto& inner = tuMatch.as_Alias();
                        this->visitPathParams(inner.params);
                        break;
                    }
                    case TypeDataErasedTypeInner::TAG_Fcn: {
                        auto& inner = tuMatch.as_Fcn();
                        if (inner.origin != HIRSimplePath()) {
                            this->visitPath(inner.origin, HIRVisitor::PathContext::VALUE);
                        }
                        break;
                    }
                }
            }
            this->visitPathParams(e.use);
            for (auto& trait : e.traits) {
                this->visitTraitPath(trait);
            }
            break;
        }
        case HIRType::TAG_Array: {
            auto& e = data.as_Array();
            e.inner = this->visitType(e.inner);
            if (auto* size = e.size.opt_Unevaluated()) {
                this->visitConstgeneric(*size);
            }
            break;
        }
        case HIRType::TAG_Slice: {
            auto& e = data.as_Slice();
            e.inner = this->visitType(e.inner);
            break;
        }
        case HIRType::TAG_Pattern: {
            auto& e = data.as_Pattern();
            e.inner = this->visitType(e.inner);
            for (auto& range : e.pattern.alternatives) {
                if (range.hasStart) {
                    this->visitConstgeneric(range.start);
                }
                if (range.hasEnd) {
                    this->visitConstgeneric(range.end);
                }
            }
            break;
        }
        case HIRType::TAG_Tuple: {
            auto& e = data.as_Tuple();
            for (auto& type : mutRange(e)) {
                type = this->visitType(type);
            }
            break;
        }
        case HIRType::TAG_Borrow: {
            auto& e = data.as_Borrow();
            e.inner = this->visitType(e.inner);
            break;
        }
        case HIRType::TAG_Pointer: {
            auto& e = data.as_Pointer();
            e.inner = this->visitType(e.inner);
            break;
        }
        case HIRType::TAG_NamedFunction: {
            auto& e = data.as_NamedFunction();
            this->visitPath(e.path, HIRVisitor::PathContext::VALUE);
            break;
        }
        case HIRType::TAG_Function: {
            auto& e = data.as_Function();
            for (auto& type : mutRange(e.argTypes)) {
                type = this->visitType(type);
            }
            e.rettype = this->visitType(e.rettype);
            break;
        }
        case HIRType::TAG_NodeType: {
            break;
        }
    }

    curParams = savedParams.first;
    curParamsLevel = savedParams.second;

    if (data.is_ErasedType()) {
        selfTypes.popBack();
    }

    return crate.types.intern(mv$(data));
}

auto TypecheckVisitor::visitGenericPath(HIRGenericPath& p, PathContext pc) -> void {
    Span sp;
    TRACE_FUNCTION_F(StringView("p = ") << p);
    const auto& params = getParamsForItem(sp, crate, p.path, pc, emptyParams);
    auto& args = p.params;

    checkParameters(sp, p.path, pc, params, args);

    DEBUG(StringView("p = ") << p);
    HIRVisitor::visitGenericPath(p, pc);
}

auto TypecheckVisitor::getCurrentTraitGp() const -> HIRGenericPath {
    BUG_ASSERT(currentTraitPath_);
    BUG_ASSERT(currentTrait);
    HIRPathParamsBuilder params;
    for (unsigned int i = 0; i < currentTrait->params.types.size(); i++) {
        params.types.push_back(crate.types.generic(currentTrait->params.types[i].name, i));
    }
    return HIRGenericPath(currentTraitPath_->getSimplePath(), HIRPathParams(mv$(params)));
}

auto TypecheckVisitor::visitExpr(HIRExprPtr& exp) -> void {
}

auto TypecheckVisitor::visitPath(HIRPath& p, HIRVisitor::PathContext pc) -> void {
    switch (p.data.tag()) {
        case HIRPath::Data::TAG_Generic: {
            auto& e = p.data.as_Generic();
            this->visitGenericPath(e, pc);
            break;
        }
        case HIRPath::Data::TAG_UfcsKnown: {
            auto& e = p.data.as_UfcsKnown();
            e.type = this->visitType(e.type);
            selfTypes.pushBack(e.type);
            this->visitGenericPath(e.trait, HIRVisitor::PathContext::TRAIT);
            selfTypes.popBack();
            // TODO: Locate impl block and check parameters
            break;
        }
        case HIRPath::Data::TAG_UfcsInherent: {
            auto& e = p.data.as_UfcsInherent();
            e.type = this->visitType(e.type);
            // TODO: Locate impl block and check parameters
            break;
        }
        case HIRPath::Data::TAG_UfcsUnknown: {
            BUG(Span(), StringView("Encountered unknown-trait UFCS path during outer typeck - ") << p);
            break;
        }
    }
}

auto TypecheckVisitor::visitParams(HIRGenericParams& params) -> void {
    TRACE_FUNCTION_F(params.fmtArgs());
    for (auto& tps : params.types) {
        tps.defaultValue = this->visitType(tps.defaultValue);
    }

    for (auto& bound : params.bounds) {
        switch (bound.tag()) {
            case HIRGenericBound::TAG_TraitBound: {
                auto& e = bound.as_TraitBound();
                e.type = this->visitType(e.type);
                selfTypes.pushBack(e.type);
                this->visitTraitPath(e.trait);
                selfTypes.popBack();

                if (checkingTypeDeclarationParams && !crate.featureEnabled("trivial_bounds") && e.isTrivial) {
                    StaticTraitResolve bareResolve(resolve_.board());
                    if (!traitBoundSatisfied(Span(), bareResolve, e.type, e.trait)) {
                        ERROR(Span(), E0000, StringView("trait bound `") << e.type << StringView(": ") << e.trait.path << StringView("` is not satisfied"));
                    }
                }
                break;
            }
        }
    }
    for (auto& type : mutRange(params.wellFormedTypes)) {
        type = this->visitType(type);
    }
}

auto TypecheckVisitor::visitTrait(HIRItemPath p, HIRTrait& item) -> void {
    currentTrait = &item;
    currentTraitPath_ = &p;

    auto _ = resolve_.setImplGenerics(MetadataType::TraitObject, item.params);
    auto self = crate.types.self();
    selfTypes.pushBack(self);
    HIRVisitor::visitTrait(p, item);
    selfTypes.popBack();

    currentTrait = nullptr;
}

auto TypecheckVisitor::visitTraitAlias(HIRItemPath p, HIRTraitAlias& item) -> void {
    auto _ = resolve_.setImplGenerics(MetadataType::TraitObject, item.params);
    auto self = crate.types.self();
    selfTypes.pushBack(self);
    HIRVisitor::visitTraitAlias(p, item);
    selfTypes.popBack();
}

auto TypecheckVisitor::visitStruct(HIRItemPath p, HIRStruct& item) -> void {
    auto _ = resolve_.setImplGenerics(item.structMarkings.dstType, item.params);
    checkingTypeDeclarationParams = true;
    HIRVisitor::visitStruct(p, item);
    checkingTypeDeclarationParams = false;
}

auto TypecheckVisitor::visitUnion(HIRItemPath p, HIRUnion& item) -> void {
    auto _ = resolve_.setImplGenerics(MetadataType::None, item.params);
    checkingTypeDeclarationParams = true;
    HIRVisitor::visitUnion(p, item);
    checkingTypeDeclarationParams = false;
}

auto TypecheckVisitor::visitEnum(HIRItemPath p, HIREnum& item) -> void {
    auto _ = resolve_.setImplGenerics(MetadataType::None, item.params);
    checkingTypeDeclarationParams = true;
    HIRVisitor::visitEnum(p, item);
    checkingTypeDeclarationParams = false;
}

auto TypecheckVisitor::visitAssociatedtype(HIRItemPath p, HIRAssociatedType& item) -> void {
    auto pathAty = HIRPath(crate.types.self(), this->getCurrentTraitGp(), p.getName());
    auto tyAty = crate.types.path(mv$(pathAty), HIRTypePathBinding::make_Opaque({}));
    selfTypes.pushBack(tyAty);

    HIRVisitor::visitAssociatedtype(p, item);

    selfTypes.popBack();
}

auto TypecheckVisitor::visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) -> void {
}

auto TypecheckVisitor::visitInherentType(HIRItemPath p, HIRTypeAlias& item) -> void {
    auto _ = resolve_.setItemGenerics(item.params);
    auto savedParams = std::make_pair(curParams, curParamsLevel);
    curParams = &item.params;
    curParamsLevel = 1;
    HIRVisitor::visitInherentType(p, item);
    curParams = savedParams.first;
    curParamsLevel = savedParams.second;
}

auto TypecheckVisitor::visitTypeImpl(HIRTypeImpl& impl) -> void {
    auto _ = resolve_.setImplGenerics(impl.type, impl.params);
    selfTypes.pushBack(impl.type);

    {
        curParams = &impl.params;
        curParamsLevel = 0;
        impl.type = this->visitType(impl.type);
        curParams = nullptr;
    }

    HIRVisitor::visitTypeImpl(impl);
    // TODO: Check that the type is valid

    selfTypes.popBack();
}

auto TypecheckVisitor::visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) -> void {
    Span sp;
    TRACE_FUNCTION_F(StringView("impl") << impl.params.fmtArgs() << StringView(" ") << traitPath << impl.traitArgs << StringView(" for ") << impl.type);
    auto _ = resolve_.setImplGenerics(impl.type, impl.params);
    selfTypes.pushBack(impl.type);

    {
        curParams = &impl.params;
        curParamsLevel = 0;
        impl.type = this->visitType(impl.type);
        this->visitPathParams(impl.traitArgs);
        curParams = nullptr;
    }

    HIRVisitor::visitTraitImpl(traitPath, impl);
    selfTypes.popBack();

    if (traitPath == crate.getLangItemPathOpt("async_drop")) {
        const auto& dropTrait = crate.getLangItemPathOpt("drop");
        if (dropTrait.components().empty() || !resolve_.findImpl(sp, dropTrait, HIRPathParams{}, impl.type, [](SolverSelection) { return true; })) {
            ERROR(sp, E0000, StringView("AsyncDrop impl requires a Drop impl for ") << impl.type);
        }
    }

    // TODO: Check that the type+trait is valid

    {
        const auto& trait = resolve_.hirCrate().getTraitByPath(sp, traitPath);
        for (auto& e : impl.methods) {
            auto _ = resolve_.setItemGenerics(e.second.data.params);

            const auto vIt = trait.values.find(e.first);
            if (vIt == trait.values.end() || !vIt->second.is_Function()) {
                ERROR(sp, E0000, StringView("Trait ") << traitPath << StringView(" doesn't have a method named ") << e.first);
            }
            auto& implFcn = e.second.data;
            const auto& traitFcn = vIt->second.as_Function();

            auto fcnParams = traitFcn.params.makeNopParams(crate.types, 1);
            MonomorphStatePtr ms{crate.types, impl.type, &impl.traitArgs, &fcnParams};
            ms.setConstevalState(resolve_.board(), HIRItemPath(traitPath));
            const HIRType* tmp;
            auto maybeMonomorph = [&](const HIRType* ty) -> const HIRType* {
                tmp = monomorphiseTypeNeeded(ty) ? ms.monomorphType(sp, ty) : ty;
                return resolve_.expandAssociatedTypes(sp, tmp);
            };

            std::vector<std::string> failures;
            if (implFcn.params.types.size() != traitFcn.params.types.size()) {
                failures.push_back(FMT(StringView("Mismatched type param count (expected ") << traitFcn.params.types.size() << StringView(", got ") << implFcn.params.types.size() << StringView(")")));
            }
            if (implFcn.params.values.size() != traitFcn.params.values.size()) {
                failures.push_back(FMT(StringView("Mismatched const param count (expected ") << traitFcn.params.values.size() << StringView(", got ") << implFcn.params.values.size() << StringView(")")));
            }
            if (implFcn.args.size() != traitFcn.args.size()) {
                failures.push_back(FMT(StringView("Mismatched argument count (expected ") << traitFcn.args.size() << StringView(", got ") << implFcn.args.size() << StringView(")")));
            }
            if (implFcn.receiver != traitFcn.receiver) {
                failures.push_back(FMT(StringView("Receiver type")));
            }
            for (size_t i = 0; i < std::min(implFcn.args.size(), traitFcn.args.size()); i++) {
                if (!(i == 0 && (traitFcn.receiver == HIRFunction::Receiver::Free || implFcn.receiver == HIRFunction::Receiver::Free))) {
                    const auto& expTy = maybeMonomorph(traitFcn.args[i].second);
                    const HIRType* hasTy = implFcn.args[i].second;
                    hasTy = resolve_.expandAssociatedTypes(sp, hasTy);

                    if (expTy != hasTy && !expTy->equalsIgnoringRegions(hasTy)) {
                        failures.push_back(FMT(StringView("Argument ") << 1 + i << StringView(" mismatch - expected ") << expTy << StringView(", got ") << hasTy));
                    }
                }
            }

            struct MCB: public HIRMatchGenerics {
                std::map<RcString, const HIRType*> mapping;
                std::map<unsigned int, const HIRType*> rpitMapping;

                MCB()
                    : HIRMatchGenerics(BorrowMatchedValues{})
                {
                }

                HIRCompare cmpType(const Span& sp, const HIRType* tyL, const HIRType* tyR, tCbResolveType resolveCb) override {
                    if (const auto* erased = tyL->opt_ErasedType(); erased && erased->inner.is_Fcn()) {
                        const auto index = erased->inner.as_Fcn().index;
                        const auto inserted = rpitMapping.insert(std::make_pair(index, tyR));
                        if (!inserted.second) {
                            return HIRMatchGenerics::cmpType(sp, inserted.first->second, tyR, resolveCb);
                        }
                        return HIRCompare::Equal;
                    }
                    if (const auto* tyP = tyL->opt_Path()) {
                        if (const auto* pathP = tyP->path.data.opt_UfcsKnown()) {
                            if (pathP->item.compare(0, strlen(ATY_PREFIX_ERASED), ATY_PREFIX_ERASED) == 0) {
                                mapping.insert(std::make_pair(pathP->item, tyR));
                                return HIRCompare::Equal;
                            }
                        }
                    }
                    return HIRMatchGenerics::cmpType(sp, tyL, tyR, resolveCb);
                }

                HIRCompare matchTy(const HIRGenericRef& g, const HIRType* ty, tCbResolveType resolveCb) override {
                    return (!ty->is_Generic() || ty->as_Generic() != g) ? HIRCompare::Unequal : HIRCompare::Equal;
                }

                HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& sz) override {
                    return (!sz.is_Generic() || sz.as_Generic() != g) ? HIRCompare::Unequal : HIRCompare::Equal;
                }
            } matchCb;

            const auto& expRetTy1 = maybeMonomorph(traitFcn.returnType);
            auto implRetTy = implFcn.returnType;
            implRetTy = resolve_.expandAssociatedTypes(sp, implRetTy);
            if (!expRetTy1->matchTestGenerics(sp, implRetTy, HIRResolvePlaceholdersNop(), matchCb)) {
                failures.push_back(FMT(StringView("Mismatched return type:\n") << StringView("  Expected ") << expRetTy1 << StringView("\n") << StringView("  Found    ") << implRetTy));
            }
            const HIRType* expRetTyReal;
            const auto& expRetTy = matchCb.mapping.empty() && matchCb.rpitMapping.empty() ? expRetTy1 : (expRetTyReal = cloneTyWith(crate.types, sp, expRetTy1, [&](const HIRType* ref) -> const HIRType* {
                if (const auto* erased = ref->opt_ErasedType(); erased && erased->inner.is_Fcn()) {
                    const auto it = matchCb.rpitMapping.find(erased->inner.as_Fcn().index);
                    if (it != matchCb.rpitMapping.end()) {
                        return it->second;
                    }
                }
                if (const auto* tyP = ref->opt_Path()) {
                    if (const auto* pathP = tyP->path.data.opt_UfcsKnown()) {
                        auto it = matchCb.mapping.find(pathP->item);
                        if (it != matchCb.mapping.end()) {
                            return it->second;
                        }
                    }
                }
                return nullptr;
            }));

            if (!failures.empty()) {
                ERROR(
                    sp,
                    E0000,
                    StringView("Method ") << e.first << StringView(" doesn't match trait:\n") << FMT_CB(os, for (const auto& f : failures) os << StringView("- ") << f << StringView("\n")) << StringView("Trait:\n")
                                          << FMT_CB(
                                                 os,
                                                 {
                                                     os << StringView("    fn ") << e.first << traitFcn.params.fmtArgs() << StringView("(");
                                                     for (const auto& a : traitFcn.args) {
                                                         os << a.first << StringView(": ") << maybeMonomorph(a.second) << StringView(", ");
                                                     }
                                                     os << StringView(")\n");
                                                     os << StringView("    -> ") << maybeMonomorph(traitFcn.returnType) << StringView("\n");
                                                     os << StringView("    ") << traitFcn.params.fmtBounds();
                                                 }
                                             )
                                          << StringView("\n") << StringView("Impl :\n")
                                          << FMT_CB(
                                                 os,
                                                 {
                                                     os << StringView("    fn ") << e.first << implFcn.params.fmtArgs() << StringView("(");
                                                     for (const auto& a : implFcn.args) {
                                                         os << a.first << StringView(": ") << a.second << StringView(", ");
                                                     }
                                                     os << StringView(")\n");
                                                     os << StringView("    -> ") << implFcn.returnType << StringView("\n");
                                                     os << StringView("    ") << implFcn.params.fmtBounds();
                                                 }
                                             )
                                          << StringView("\n") << StringView("in impl") << impl.params.fmtArgs() << StringView(" ") << traitPath << impl.traitArgs << StringView(" for ") << impl.type
                );
            }
            // HACK: Replace all types (which should be functionally identical) so lifetimes match

            // HACK: Clone the expected type, so the lifetimes match.
            DEBUG(StringView("Updating < ") << impl.type << StringView(" as ") << traitPath << impl.traitArgs << StringView(" >::") << e.first);
            if (!matchCb.rpitMapping.empty()) {
                implFcn.traitReturnType = expRetTy1;
                for (const auto& mapping : matchCb.rpitMapping) {
                    const auto name = RcString::newInterned(FMT(ATY_PREFIX_ERASED << e.first << StringView("_") << mapping.first));
                    impl.types.insert(std::make_pair(name, HIRTraitImpl::ImplEnt<const HIRType*>{e.second.isSpecialisable, mapping.second}));
                }
            }
            for (const auto& mapping : matchCb.mapping) {
                impl.types.insert(std::make_pair(mapping.first, HIRTraitImpl::ImplEnt<const HIRType*>{e.second.isSpecialisable, mapping.second}));
            }
            implFcn.returnType = expRetTy;
            for (size_t i = 0; i < std::min(implFcn.args.size(), traitFcn.args.size()); i++) {
                DEBUG(StringView("ARG") << i << StringView("> ") << traitFcn.args[i].second);
                implFcn.args[i].second = resolve_.monomorphExpand(sp, traitFcn.args[i].second, ms);
            }
            DEBUG(StringView("Updated < ") << impl.type << StringView(" as ") << traitPath << impl.traitArgs << StringView(" >::") << e.first);
            DEBUG(FMT_CB(os, {
                os << StringView("fn ") << e.first << implFcn.params.fmtArgs() << StringView("(");
                for (const auto& a : implFcn.args) {
                    os << a.first << StringView(": ") << a.second << StringView(", ");
                }
                os << StringView(")");
                os << implFcn.params.fmtBounds();
            }));
        }
        for (const auto& e : impl.constants) {
            const auto& vi = trait.values.at(e.first);
            if (!vi.is_Constant()) {
                ERROR(sp, E0000, StringView("Trait ") << traitPath << StringView(" doesn't have a constant named ") << e.first);
            }
            const auto& implConst = e.second.data;
            const auto& traitConst = vi.as_Constant();
        }
        for (const auto& e : impl.statics) {
            const auto& vi = trait.values.at(e.first);
            if (!vi.is_Static()) {
                ERROR(sp, E0000, StringView("Trait ") << traitPath << StringView(" doesn't have a static named ") << e.first);
            }
            const auto& implStatic = e.second.data;
            const auto& traitStatic = vi.as_Static();
        }
        for (const auto& e : trait.types) {
            const auto& traitType = trait.types.at(e.first);
            const auto& implType = e.second;
        }
    }
}

auto TypecheckVisitor::visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) -> void {
    auto _ = resolve_.setImplGenerics(impl.type, impl.params);
    selfTypes.pushBack(impl.type);

    {
        curParams = &impl.params;
        curParamsLevel = 0;
        impl.type = this->visitType(impl.type);
        this->visitPathParams(impl.traitArgs);
        curParams = nullptr;
    }

    HIRVisitor::visitMarkerImpl(traitPath, impl);
    // TODO: Check that the type+trait is valid

    selfTypes.popBack();
}

auto TypecheckVisitor::visitFunction(HIRItemPath p, HIRFunction& item) -> void {
    TRACE_FUNCTION_F(p);
    if (resolve_.hirCrate().getLangItemPathOpt("sized").components().empty()) {
        ERROR(Span(), E0000, StringView("requires `sized` lang_item"));
    }

    auto _ = resolve_.setItemGenerics(item.params);
    visitParams(item.params);

    fcnPtr = &item;
    checkingFunctionSignature = true;

    curParams = &item.params;
    curParamsLevel = 1;
    for (auto& arg : item.args) {
        TRACE_FUNCTION_F(StringView("ARG ") << arg);
        arg.second = visitType(arg.second);
    }
    curParams = nullptr;

    fcnPath = &p;
    fcnErasedCount = 0;
    {
        TRACE_FUNCTION_F(StringView("RET ") << item.returnType);
        item.returnType = visitType(item.returnType);
    }
    fcnPath = nullptr;
    fcnPtr = nullptr;

    if (item.receiver == HIRFunction::Receiver::Custom) {
        ASSERT_BUG(Span(), item.receiverType, StringView("Custom receiver without a receiver type"));
        *item.receiverType = this->visitType(*item.receiverType);
    }
    checkingFunctionSignature = false;
    HIRVisitor::visitFunction(p, item);
}
