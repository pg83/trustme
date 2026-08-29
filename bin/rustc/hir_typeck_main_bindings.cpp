#include "hir_typeck_main_bindings.h"
#include "hir_typeck_main_bindings.h"

#include "hir_hir.h"
#include "hir_expr.h"
#include "wire_board.h"
#include "hir_visitor.h"
#include "hir_typeck_static.h"

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

        std::vector<const HIRTypeData*> selfTypes;

        typedef std::vector<std::pair<const HIRSimplePath*, const HIRTrait*>> tTraitImports;
        tTraitImports traits;

        TypecheckVisitor(const WireBoard& wb, HIRCrate& crate);

        struct ModTraitsGuard {
            TypecheckVisitor* v;
            tTraitImports oldImports;

            ~ModTraitsGuard();
        };

        ModTraitsGuard pushModTraits(const HIRModule& mod);

        static bool traitBoundSatisfied(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* type, const HIRTraitPath& trait);

        static bool traitParamsMayHaveAssociatedType(const HIRPathParams& params);

        void checkParameters(const Span& sp, const HIRSimplePath& usedPath, PathContext pc, const HIRGenericParams& paramDef, HIRPathParams& paramVals);

        void visitPathParams(HIRPathParams& pp) override;

        [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override;

        void visitGenericPath(HIRGenericPath& p, PathContext pc) override;

        bool locateTraitItemInBounds(const Span& sp, HIRVisitor::PathContext pc, const HIRTypeData* tr, const HIRGenericParams& params, HIRPath::Data& pd);

        static HIRPath::Data getUfcsKnown(HIRPath::Data::Data_UfcsUnknown e, HIRGenericPath traitPath, const HIRTrait& trait);

        static bool locateItemInTrait(HIRVisitor::PathContext pc, const HIRTrait& trait, HIRPath::Data& pd);

        bool locateInTraitAndSet(const Span& sp, HIRVisitor::PathContext pc, const HIRGenericPath& traitPath, const HIRTrait& trait, HIRPath::Data& pd);

        bool setFromImpl(const HIRGenericPath& traitPath, const HIRTrait& trait, HIRPath::Data& pd);

        bool locateInTraitImplAndSet(HIRVisitor::PathContext pc, const HIRGenericPath& traitPath, const HIRTrait& trait, HIRPath::Data& pd);

        HIRGenericPath makeGenericPath(HIRSimplePath sp, const HIRTrait& trait);

        HIRGenericPath getCurrentTraitGp() const;

        void visitPathUfcsUnknown(const Span& sp, HIRPath& p, HIRVisitor::PathContext pc);

        void visitExpr(HIRExprPtr& exp) override;

        void visitPath(HIRPath& p, HIRVisitor::PathContext pc) override;

        void visitParams(HIRGenericParams& params) override;

        void visitModule(HIRItemPath p, HIRModule& mod) override;

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
    , resolve_(wb) {
}

auto TypecheckVisitor::pushModTraits(const HIRModule& mod) -> ModTraitsGuard {
    Span sp;
    DEBUG(StringView(""));
    auto rv = ModTraitsGuard{this, mv$(this->traits)};
    for (const auto& traitPath : mod.traits) {
        DEBUG(StringView("- ") << traitPath);
        traits.push_back(std::make_pair(&traitPath, &this->crate.getTraitByPath(sp, traitPath)));
    }
    return rv;
}

auto TypecheckVisitor::traitBoundSatisfied(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* type, const HIRTraitPath& trait) -> bool {
    return resolve.findImpl(sp, trait.path.path, &trait.path.params, type, [](SolverResponse response) {
        return response.certainty == SolverCertainty::Proven;
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

        paramVals.types.push_back(ms.monomorphType(sp, tyDef.defaultValue));
        DEBUG(StringView("Add missing param (using default): ") << paramVals.types.back());
    }

    if (paramVals.types.size() != paramDef.types.size()) {
        ERROR(sp, E0000, StringView("Incorrect number of parameters - expected ") << paramDef.types.size() << StringView(", got ") << paramVals.types.size());
    }

    for (unsigned int i = 0; i < paramVals.types.size(); i++) {
        if (paramVals.types[i] == HIRTypeRef()) {
            // TODO: Why is this pulling in the default? Why not just leave it as-is

            // TODO: Monomorphise?
            paramVals.types[i] = ms.monomorphType(sp, paramDef.types[i].defaultValue);
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
            case HIRGenericBound::TAG_TypeEquality: {
                auto& e = bound.as_TypeEquality();
                // TODO: Check that two types are equal in this case
                DEBUG(StringView("TODO: Check equality bound ") << e.type << StringView(" == ") << e.otherType);
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

[[nodiscard]] auto TypecheckVisitor::visitType(HIRTypeRef ty) -> HIRTypeRef {
    Span _sp;
    const Span& sp = _sp;

    BUG_ASSERT(ty);
    auto data = ty->cloneData();

    auto self = crate.types.self();
    if (data.is_ErasedType()) {
        selfTypes.push_back(self);
    }

    auto savedParams = std::make_pair(curParams, curParamsLevel);

    switch (data.tag()) {
        case HIRTypeData::TAG_Infer: {
            break;
        }
        case HIRTypeData::TAG_Diverge: {
            break;
        }
        case HIRTypeData::TAG_Primitive: {
            break;
        }
        case HIRTypeData::TAG_Generic: {
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = data.as_Path();
            this->visitPath(e.path, HIRVisitor::PathContext::TYPE);
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& e = data.as_TraitObject();
            if (e.trait.path != HIRSimplePath()) {
                this->visitTraitPath(e.trait);
            }
            for (auto& marker : e.markers) {
                this->visitGenericPath(marker, HIRVisitor::PathContext::TYPE);
            }
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            auto& e = data.as_ErasedType();
            {
                auto& tuMatch = e.inner;
                switch (tuMatch.tag()) {
                    case TypeDataErasedTypeInner::TAG_Known: {
                        auto& inner = tuMatch.as_Known();
                        inner = this->visitType(inner);
                        break;
                    }
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
        case HIRTypeData::TAG_Array: {
            auto& e = data.as_Array();
            e.inner = this->visitType(e.inner);
            if (auto* size = e.size.opt_Unevaluated()) {
                this->visitConstgeneric(*size);
            }
            break;
        }
        case HIRTypeData::TAG_Slice: {
            auto& e = data.as_Slice();
            e.inner = this->visitType(e.inner);
            break;
        }
        case HIRTypeData::TAG_Pattern: {
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
        case HIRTypeData::TAG_Tuple: {
            auto& e = data.as_Tuple();
            for (auto& inner : e) {
                inner = this->visitType(inner);
            }
            break;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = data.as_Borrow();
            e.inner = this->visitType(e.inner);
            break;
        }
        case HIRTypeData::TAG_Pointer: {
            auto& e = data.as_Pointer();
            e.inner = this->visitType(e.inner);
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            auto& e = data.as_NamedFunction();
            this->visitPath(e.path, HIRVisitor::PathContext::VALUE);
            break;
        }
        case HIRTypeData::TAG_Function: {
            auto& e = data.as_Function();
            for (auto& arg : e.argTypes) {
                arg = this->visitType(arg);
            }
            e.rettype = this->visitType(e.rettype);
            break;
        }
        case HIRTypeData::TAG_NodeType: {
            break;
        }
    }

    curParams = savedParams.first;
    curParamsLevel = savedParams.second;

    if (data.is_ErasedType()) {
        selfTypes.pop_back();
    }

    return crate.types.intern(mv$(data));

    if (const auto* e = ty->opt_Path()) {
        switch (e->path.data.tag()) {
            case HIRPath::Data::TAG_Generic: {
                break;
            }
            case HIRPath::Data::TAG_UfcsUnknown: {
                TODO(sp, StringView("Should UfcsKnown be encountered here?"));
                break;
            }
            case HIRPath::Data::TAG_UfcsInherent: {
                TRACE_FUNCTION_FR(StringView("UfcsInherent - ") << ty, ty);
                resolve_.expandAssociatedTypes(sp, ty);
                break;
            }
            case HIRPath::Data::TAG_UfcsKnown: {
                TRACE_FUNCTION_FR(StringView("UfcsKnown - ") << ty, ty);
                resolve_.expandAssociatedTypes(sp, ty);
                break;
            }
        }
    }
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

auto TypecheckVisitor::locateTraitItemInBounds(const Span& sp, HIRVisitor::PathContext pc, const HIRTypeData* tr, const HIRGenericParams& params, HIRPath::Data& pd) -> bool {
    for (const auto& b : params.bounds) {
        if (b.is_TraitBound()) {
            auto& e = b.as_TraitBound();
            DEBUG(StringView("- ") << e.type << StringView(" : ") << e.trait.path);
            if (e.type == tr) {
                DEBUG(StringView(" - Match"));
                if (locateInTraitAndSet(sp, pc, e.trait.path, this->crate.getTraitByPath(sp, e.trait.path.path), pd)) {
                    return true;
                }
            }
        }
    }
    return false;
}

auto TypecheckVisitor::getUfcsKnown(HIRPath::Data::Data_UfcsUnknown e, HIRGenericPath traitPath, const HIRTrait& trait) -> HIRPath::Data {
    return HIRPath::Data::make_UfcsKnown({mv$(e.type), mv$(traitPath), mv$(e.item), mv$(e.params)});
}

auto TypecheckVisitor::locateItemInTrait(HIRVisitor::PathContext pc, const HIRTrait& trait, HIRPath::Data& pd) -> bool {
    const auto& e = pd.as_UfcsUnknown();

    switch (pc) {
        case HIRVisitor::PathContext::VALUE:
            if (trait.values.find(e.item) != trait.values.end()) {
                return true;
            }
            break;
        case HIRVisitor::PathContext::TRAIT:
            break;
        case HIRVisitor::PathContext::TYPE:
            if (trait.types.find(e.item) != trait.types.end()) {
                return true;
            }
            break;
    }
    return false;
}

auto TypecheckVisitor::locateInTraitAndSet(const Span& sp, HIRVisitor::PathContext pc, const HIRGenericPath& traitPath, const HIRTrait& trait, HIRPath::Data& pd) -> bool {
    if (locateItemInTrait(pc, trait, pd)) {
        pd = getUfcsKnown(mv$(pd.as_UfcsUnknown()), makeGenericPath(traitPath.path, trait), trait);
        return true;
    }
    for (const auto& pt : trait.allParentTraits) {
        if (locateItemInTrait(pc, *pt.traitPtr, pd)) {
            pd = getUfcsKnown(mv$(pd.as_UfcsUnknown()), makeGenericPath(traitPath.path, trait), trait);
            return true;
        }
    }
    return false;
}

auto TypecheckVisitor::setFromImpl(const HIRGenericPath& traitPath, const HIRTrait& trait, HIRPath::Data& pd) -> bool {
    auto& e = pd.as_UfcsUnknown();
    const auto& type = e.type;
    return resolve_.findImpl(Span(), traitPath.path, traitPath.params, type, [&](SolverResponse response) {
        if (response.certainty == SolverCertainty::NoSolution) {
            return false;
        }
        pd = getUfcsKnown(mv$(e), makeGenericPath(traitPath.path, trait), trait);
        return true;
    });
}

auto TypecheckVisitor::locateInTraitImplAndSet(HIRVisitor::PathContext pc, const HIRGenericPath& traitPath, const HIRTrait& trait, HIRPath::Data& pd) -> bool {
    auto& e = pd.as_UfcsUnknown();
    if (this->locateItemInTrait(pc, trait, pd)) {
        return this->setFromImpl(traitPath, trait, pd);
        DEBUG(StringView("- Item ") << e.item << StringView(" not in trait ") << traitPath.path);
    }

    for (const auto& pt : trait.allParentTraits) {
        if (this->locateItemInTrait(pc, *pt.traitPtr, pd)) {
            // TODO: Monomorphise params?
            return setFromImpl(pt.path, *pt.traitPtr, pd);
        } else {
        }
    }
    return false;
}

auto TypecheckVisitor::makeGenericPath(HIRSimplePath sp, const HIRTrait& trait) -> HIRGenericPath {
    auto traitPathG = HIRGenericPath(mv$(sp));
    for (unsigned int i = 0; i < trait.params.types.size(); i++) {
        traitPathG.params.types.push_back(crate.types.generic(trait.params.types[i].name, i));
    }
    return traitPathG;
}

auto TypecheckVisitor::getCurrentTraitGp() const -> HIRGenericPath {
    BUG_ASSERT(currentTraitPath_);
    BUG_ASSERT(currentTrait);
    auto traitPath = HIRGenericPath(currentTraitPath_->getSimplePath());
    for (unsigned int i = 0; i < currentTrait->params.types.size(); i++) {
        traitPath.params.types.push_back(crate.types.generic(currentTrait->params.types[i].name, i));
    }
    return traitPath;
}

auto TypecheckVisitor::visitPathUfcsUnknown(const Span& sp, HIRPath& p, HIRVisitor::PathContext pc) -> void {
    TRACE_FUNCTION_FR(StringView("UfcsUnknown - p=") << p, p);
    auto& e = p.data.as_UfcsUnknown();

    e.type = this->visitType(e.type);
    this->visitPathParams(e.params);

    if (resolve_.itemGenericsPtr() != nullptr && locateTraitItemInBounds(sp, pc, e.type, *resolve_.itemGenericsPtr(), p.data)) {
        return;
    }
    if (resolve_.implGenericsPtr() != nullptr && locateTraitItemInBounds(sp, pc, e.type, *resolve_.implGenericsPtr(), p.data)) {
        return;
    }

    if (const auto* te = e.type->opt_Generic()) {
        // - TODO: This could be encoded by a `Self: Trait` bound in the generics, but that may have knock-on issues?
        if (te->name == "Self" && currentTrait) {
            auto traitPath = this->getCurrentTraitGp();
            if (this->locateInTraitAndSet(sp, pc, traitPath, *currentTrait, p.data)) {
                return;
            }
        }
        ERROR(sp, E0000, StringView("Failed to find impl with '") << e.item << StringView("' for ") << e.type);
        return;
    } else {
        if (this->crate.findTypeImpls(e.type, HIRResolvePlaceholdersNop(), [&](const auto& impl) {
            DEBUG(StringView("- matched inherent impl ") << e.type);
            switch (pc) {
                case HIRVisitor::PathContext::VALUE:
                    if (impl.methods.find(e.item) == impl.methods.end()) {
                        return false;
                    }
                    break;
                case HIRVisitor::PathContext::TRAIT:
                    return false;
                case HIRVisitor::PathContext::TYPE:
                    if (impl.types.find(e.item) == impl.types.end()) {
                        return false;
                    }
                    break;
            }

            return true;
        })) {
            auto newData = HIRPath::Data::make_UfcsInherent({mv$(e.type), mv$(e.item), mv$(e.params)});
            p.data = mv$(newData);
            DEBUG(StringView("- Resolved, replace with ") << p);
            return;
        }
        for (const auto& traitInfo : traits) {
            const auto& trait = *traitInfo.second;

            switch (pc) {
                case HIRVisitor::PathContext::VALUE:
                    if (trait.values.find(e.item) == trait.values.end()) {
                        continue;
                    }
                    break;
                case HIRVisitor::PathContext::TRAIT:
                case HIRVisitor::PathContext::TYPE:
                    if (trait.types.find(e.item) == trait.types.end()) {
                        continue;
                    }
                    break;
            }

            DEBUG(StringView("- Trying trait ") << *traitInfo.first);
            auto traitPath = HIRGenericPath(*traitInfo.first);
            for (unsigned int i = 0; i < trait.params.types.size(); i++) {
                traitPath.params.types.push_back(crate.types.infer());
            }

            // TODO: Search supertraits
            // TODO: Should impls be searched first, or item names?

            if (this->locateInTraitImplAndSet(pc, mv$(traitPath), trait, p.data)) {
                return;
            }
        }
    }

    ERROR(sp, E0000, StringView("Failed to find impl with '") << e.item << StringView("' for ") << e.type << StringView(" (in ") << p << StringView(")"));
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
            selfTypes.push_back(e.type);
            this->visitGenericPath(e.trait, HIRVisitor::PathContext::TRAIT);
            selfTypes.pop_back();
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
                selfTypes.push_back(e.type);
                this->visitTraitPath(e.trait);
                selfTypes.pop_back();

                if (checkingTypeDeclarationParams && !crate.featureEnabled("trivial_bounds") && e.isTrivial) {
                    StaticTraitResolve bareResolve(resolve_.board());
                    if (!traitBoundSatisfied(Span(), bareResolve, e.type, e.trait)) {
                        ERROR(Span(), E0000, StringView("trait bound `") << e.type << StringView(": ") << e.trait.path << StringView("` is not satisfied"));
                    }
                }
                break;
            }
            case HIRGenericBound::TAG_TypeEquality: {
                auto& e = bound.as_TypeEquality();
                e.type = this->visitType(e.type);
                e.otherType = this->visitType(e.otherType);
                break;
            }
        }
    }
}

auto TypecheckVisitor::visitModule(HIRItemPath p, HIRModule& mod) -> void {
    auto _ = this->pushModTraits(mod);
    HIRVisitor::visitModule(p, mod);
}

auto TypecheckVisitor::visitTrait(HIRItemPath p, HIRTrait& item) -> void {
    currentTrait = &item;
    currentTraitPath_ = &p;

    auto _ = resolve_.setImplGenerics(MetadataType::TraitObject, item.params);
    auto self = crate.types.self();
    selfTypes.push_back(self);
    HIRVisitor::visitTrait(p, item);
    selfTypes.pop_back();

    currentTrait = nullptr;
}

auto TypecheckVisitor::visitTraitAlias(HIRItemPath p, HIRTraitAlias& item) -> void {
    auto _ = resolve_.setImplGenerics(MetadataType::TraitObject, item.params);
    auto self = crate.types.self();
    selfTypes.push_back(self);
    HIRVisitor::visitTraitAlias(p, item);
    selfTypes.pop_back();
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
    selfTypes.push_back(tyAty);

    HIRVisitor::visitAssociatedtype(p, item);

    selfTypes.pop_back();
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
    selfTypes.push_back(impl.type);

    {
        curParams = &impl.params;
        curParamsLevel = 0;
        impl.type = this->visitType(impl.type);
        curParams = nullptr;
    }

    HIRVisitor::visitTypeImpl(impl);
    // TODO: Check that the type is valid

    selfTypes.pop_back();
}

auto TypecheckVisitor::visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) -> void {
    Span sp;
    TRACE_FUNCTION_F(StringView("impl") << impl.params.fmtArgs() << StringView(" ") << traitPath << impl.traitArgs << StringView(" for ") << impl.type);
    auto _ = resolve_.setImplGenerics(impl.type, impl.params);
    selfTypes.push_back(impl.type);

    {
        curParams = &impl.params;
        curParamsLevel = 0;
        impl.type = this->visitType(impl.type);
        this->visitPathParams(impl.traitArgs);
        curParams = nullptr;
    }

    HIRVisitor::visitTraitImpl(traitPath, impl);
    selfTypes.pop_back();

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
            HIRTypeRef tmp;
            auto maybeMonomorph = [&](const HIRTypeData* ty) -> const HIRTypeData* {
                if (monomorphiseTypeNeeded(ty)) {
                    tmp = ms.monomorphType(sp, ty);
                    resolve_.expandAssociatedTypes(sp, tmp);
                    return tmp;
                } else {
                    return ty;
                }
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
                    HIRTypeRef hasTy = implFcn.args[i].second;
                    resolve_.expandAssociatedTypes(sp, hasTy);

                    if (expTy != hasTy && !expTy->equalsIgnoringRegions(hasTy)) {
                        failures.push_back(FMT(StringView("Argument ") << 1 + i << StringView(" mismatch - expected ") << expTy << StringView(", got ") << hasTy));
                    }
                }
            }

            struct MCB: public HIRMatchGenerics {
                std::map<RcString, const HIRTypeData*> mapping;
                std::map<unsigned int, const HIRTypeData*> rpitMapping;

                MCB()
                    : HIRMatchGenerics(BorrowMatchedValues{}) {
                }

                HIRCompare cmpType(const Span& sp, const HIRTypeData* tyL, const HIRTypeData* tyR, tCbResolveType resolveCb) override {
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

                HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType resolveCb) override {
                    return (!ty->is_Generic() || ty->as_Generic() != g) ? HIRCompare::Unequal : HIRCompare::Equal;
                }

                HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& sz) override {
                    return (!sz.is_Generic() || sz.as_Generic() != g) ? HIRCompare::Unequal : HIRCompare::Equal;
                }
            } matchCb;

            const auto& expRetTy1 = maybeMonomorph(traitFcn.returnType);
            auto implRetTy = implFcn.returnType;
            resolve_.expandAssociatedTypes(sp, implRetTy);
            if (!expRetTy1->matchTestGenerics(sp, implRetTy, HIRResolvePlaceholdersNop(), matchCb)) {
                failures.push_back(
                    FMT(StringView("Mismatched return type:\n")
                        << StringView("  Expected ") << expRetTy1 << StringView("\n")
                        << StringView("  Found    ") << implRetTy)
                );
            }
            HIRTypeRef expRetTyReal;
            const auto& expRetTy = matchCb.mapping.empty() && matchCb.rpitMapping.empty() ? expRetTy1 : (expRetTyReal = cloneTyWith(crate.types, sp, expRetTy1, [&](const HIRTypeData* ref, HIRTypeRef& out) -> bool {
                if (const auto* erased = ref->opt_ErasedType(); erased && erased->inner.is_Fcn()) {
                    const auto it = matchCb.rpitMapping.find(erased->inner.as_Fcn().index);
                    if (it != matchCb.rpitMapping.end()) {
                        out = it->second;
                        return true;
                    }
                }
                if (const auto* tyP = ref->opt_Path()) {
                    if (const auto* pathP = tyP->path.data.opt_UfcsKnown()) {
                        auto it = matchCb.mapping.find(pathP->item);
                        if (it != matchCb.mapping.end()) {
                            out = it->second;
                            return true;
                        }
                    }
                }
                return false;
            }));

            if (!failures.empty()) {
                ERROR(
                    sp,
                    E0000,
                    StringView("Method ") << e.first << StringView(" doesn't match trait:\n")
                              << FMT_CB(os, for (const auto& f : failures) os << StringView("- ") << f << StringView("\n")) << StringView("Trait:\n")
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
                              << StringView("\n")
                              << StringView("Impl :\n")
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
                              << StringView("\n")
                              << StringView("in impl") << impl.params.fmtArgs() << StringView(" ") << traitPath << impl.traitArgs << StringView(" for ") << impl.type
                );
            }
            // HACK: Replace all types (which should be functionally identical) so lifetimes match

            // HACK: Clone the expected type, so the lifetimes match.
            DEBUG(StringView("Updating < ") << impl.type << StringView(" as ") << traitPath << impl.traitArgs << StringView(" >::") << e.first);
            if (!matchCb.rpitMapping.empty()) {
                implFcn.traitReturnType = expRetTy1;
                for (const auto& mapping : matchCb.rpitMapping) {
                    const auto name = RcString::newInterned(FMT(ATY_PREFIX_ERASED << e.first << StringView("_") << mapping.first));
                    impl.types.insert(std::make_pair(name, HIRTraitImpl::ImplEnt<HIRTypeRef>{e.second.isSpecialisable, mapping.second}));
                }
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
    selfTypes.push_back(impl.type);

    {
        curParams = &impl.params;
        curParamsLevel = 0;
        impl.type = this->visitType(impl.type);
        this->visitPathParams(impl.traitArgs);
        curParams = nullptr;
    }

    HIRVisitor::visitMarkerImpl(traitPath, impl);
    // TODO: Check that the type+trait is valid

    selfTypes.pop_back();
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

TypecheckVisitor::ModTraitsGuard::~ModTraitsGuard() {
    this->v->traits = mv$(this->oldImports);
}
