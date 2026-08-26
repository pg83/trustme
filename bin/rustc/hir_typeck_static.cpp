#include "hir_typeck_static.h"

#include "hir_expr.h"
#include "settings.h"
#include "wire_board.h"
#include "hir_typeck_helpers.h"
#include "hir_conv_main_bindings.h"
#include "hir_item_path.h"
#include "hir_visitor.h"

#include <std/alg/defer.h>
#include <std/mem/obj_pool.h>

#include <algorithm>

namespace {
    const HIRGenericParams emptyParams;

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

class StaticTraitResolve::NextSolverBridge {
    HMTypeInferrence ivars;
    HIRSimplePath visibility;
    TraitResolution resolve_;

public:
    explicit NextSolverBridge(const WireBoard& wb)
        : ivars(wb.crate->types)
        , visibility(wb.crate->crateName, {})
        , resolve_(ivars, wb, nullptr, nullptr, visibility, nullptr)
    {
    }

    bool findImpl(const Span& sp, const HIRGenericParams* implGenerics, const HIRGenericParams* itemGenerics, const HIRSimplePath& trait, const HIRPathParams* params, const HIRTypeData* type, StaticImplCallback& callback) {
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

        return resolve_.findTraitImplsNext(sp, trait, *params, type, [&](ImplRef impl, HIRCompare match) {
            return callback.visit(::std::move(impl), match != HIRCompare::Equal);
        }, "");
    }
};

namespace {
    bool typeHasUnresolvedPath(const HIRTypeData* ty) {
        return ty && visitTyWith(ty, [](const HIRTypeData* inner) {
            const auto* path = inner->opt_Path();
            return path && path->path.data.is_UfcsUnknown();
        });
    }
}

bool StaticTraitResolve::genericBoundsUnresolved(const HIRGenericParams* params) {
    if (!params) {
        return false;
    }
    for (const auto& bound : params->bounds) {
        if (const auto* tb = bound.opt_TraitBound()) {
            if (typeHasUnresolvedPath(tb->type)) {
                return true;
            }
            for (const auto& ty : tb->trait.path.params.types) {
                if (typeHasUnresolvedPath(ty)) {
                    return true;
                }
            }
        } else if (const auto* eq = bound.opt_TypeEquality()) {
            if (typeHasUnresolvedPath(eq->type) || typeHasUnresolvedPath(eq->otherType)) {
                return true;
            }
        }
    }
    return false;
}

bool StaticTraitResolve::findImplCb(const Span& sp, const HIRSimplePath& traitPath, const HIRPathParams* traitParams, const HIRTypeData* type, StaticImplCallback& foundCb, bool dontHandoffToSpecialised, bool noGoalBridge) const {
    TRACE_FUNCTION_F(traitPath << FMT_CB(os, if (traitParams) { os << *traitParams; } else { os << "<?>"; }) << " for " << type);
    auto cbIdent = HIRResolvePlaceholdersNop();

    if (const auto* path = type->opt_Path(); path && path->path.data.is_UfcsKnown()) {
        HIRTypeRef normalizedType = type;
        this->expandAssociatedTypes(sp, normalizedType);
        if (normalizedType != type) {
            return this->findImplCb(sp, traitPath, traitParams, normalizedType, foundCb, dontHandoffToSpecialised, noGoalBridge);
        }
    }

    static HIRPathParams nullParams;
    static HIRTraitPath::assocListT nullAssoc;

    if (!dontHandoffToSpecialised) {
        if (traitPath == langCopy()) {
            if (this->typeIsCopy(sp, type)) {
                return foundCb.visit(ImplRef(type, &nullParams, &nullAssoc), false);
            }
        } else if (traitPath == langClone()) {
            // NOTE: Duplicated check for enumerate
            if (type->is_Tuple() || type->is_Array() || type->is_Function() || type->is_NodeType() || type->is_NamedFunction() || ((*type).is_Path() && ((*type).as_Path().isClosure()))) {
                if (this->typeIsClone(sp, type)) {
                    return foundCb.visit(ImplRef(type, &nullParams, &nullAssoc), false);
                }
            }
        } else if (traitPath == langSized()) {
            if (this->typeIsSized(sp, type)) {
                return foundCb.visit(ImplRef(type, &nullParams, &nullAssoc), false);
            }
        } else if (traitPath == langUnsize()) {
            ASSERT_BUG(sp, traitParams, "TODO: Support no params for Unsize");
            const auto& dstTy = traitParams->types.at(0);
            if (this->canUnsize(sp, dstTy, type)) {
                return foundCb.visit(ImplRef(type, traitParams, &nullAssoc), false);
            }
        } else if (traitPath == langDiscriminantKind()) {
            // If the type is generic, then don't populate the ATY
            // Otherwise, populate the ATY with the correct type
            // - Unit for non-enums
            // - Enum type (usize probably) for enums
            if (type->is_Generic() || (type->is_Path() && type->as_Path().binding.is_Opaque())) {
                return foundCb.visit(ImplRef(type, traitParams, &nullAssoc), false);
            } else if (type->is_Path()) {
                if (const auto* enmpp = type->as_Path().binding.opt_Enum()) {
                    const auto& enm = **enmpp;
                    HIRTypeRef tagTy = crate.types.primitive(enm.getReprType(enm.tagRepr));
                    HIRTraitPath::assocListT assocList;
                    assocList.insert(std::make_pair(RcString::newInterned("Discriminant"), HIRTraitPath::AtyEqual{langDiscriminantKind(), {}, std::move(tagTy)}));
                    return foundCb.visit(ImplRef(type, {}, std::move(assocList)), false);
                } else {
                }
            } else {
            }
            static HIRTraitPath::assocListT assocU8;
            static HIRTraitPath::assocListT assocU32;
            if (assocU8.empty()) {
                assocU8.insert(std::make_pair(RcString::newInterned("Discriminant"), HIRTraitPath::AtyEqual{langDiscriminantKind(), {}, crate.types.primitive(HIRCoreType::U8)}));
                assocU32.insert(std::make_pair(RcString::newInterned("Discriminant"), HIRTraitPath::AtyEqual{langDiscriminantKind(), {}, crate.types.primitive(HIRCoreType::U32)}));
            }
            if ((type->is_NodeType() && (type->as_NodeType().is_Generator() || type->as_NodeType().is_Async()))
                || (type->is_Path() && (type->as_Path().isGenerator() || type->as_Path().isFuture()))) {
                return foundCb.visit(ImplRef(type, traitParams, &assocU32), false);
            }
            return foundCb.visit(ImplRef(type, traitParams, &assocU8), false);
        } else if (traitPath == langPointee()) {
            static HIRTraitPath::assocListT assocUnit;
            static HIRTraitPath::assocListT assocSlice;
            static RcString nameMetadata;
            if (assocUnit.empty()) {
                nameMetadata = RcString::newInterned("Metadata");
                assocUnit.insert(std::make_pair(nameMetadata, HIRTraitPath::AtyEqual{langPointee(), {}, crate.types.unit()}));
                assocSlice.insert(std::make_pair(nameMetadata, HIRTraitPath::AtyEqual{langPointee(), {}, crate.types.primitive(HIRCoreType::Usize)}));
            }

            // Generics (or opaque ATYs)
            if (type->is_Generic() || (type->is_Path() && type->as_Path().binding.is_Opaque())) {
                // If the type is `Sized` return `()` as the type
                if (typeIsSized(sp, type)) {
                    return foundCb.visit(ImplRef(type, traitParams, &assocUnit), false);
                } else {
                    // Return unbounded
                    return foundCb.visit(ImplRef(type, traitParams, &nullAssoc), false);
                }
            }
            // Trait object: `Metadata=DynMetadata<T>`
            else if (type->is_TraitObject()) {
                HIRTraitPath::assocListT assocList;
                assocList.insert(std::make_pair(nameMetadata, HIRTraitPath::AtyEqual{langPointee(), {}, crate.types.path(HIRGenericPath(langDynMetadata(), HIRPathParams(type)), &crate.getStructByPath(sp, langDynMetadata()))}));
                return foundCb.visit(ImplRef(type, {}, std::move(assocList)), false);
            }
            // Slice and str
            else if (type->is_Slice() || ((*type).is_Primitive() && ((*type).as_Primitive() == HIRCoreType::Str))) {
                return foundCb.visit(ImplRef(type, traitParams, &assocSlice), false);
            }
            // Structs: Can delegate their metadata
            else if (type->is_Path() && type->as_Path().binding.is_Struct()) {
                const auto& str = *type->as_Path().binding.as_Struct();
                switch (str.structMarkings.dstType) {
                    case HIRStructMarkings::DstType::None:
                        return foundCb.visit(ImplRef(type, traitParams, &assocUnit), false);
                    case HIRStructMarkings::DstType::Possible:
                    case HIRStructMarkings::DstType::Projection:
                    case HIRStructMarkings::DstType::TraitObject: {
                        const HIRTypeData* tailTpl = nullptr;
                        switch (str.data.tag()) {
                            case HIRStructData::TAG_Unit: {
                                BUG(sp, "Unsized unit struct in Pointee lookup - " << type);
                                break;
                            }
                            case HIRStructData::TAG_Tuple: {
                                auto& se = str.data.as_Tuple();
                                ASSERT_BUG(sp, !se.empty(), "Unsized tuple struct without fields - " << type); tailTpl = se.back().ent;
                                break;
                            }
                            case HIRStructData::TAG_Named: {
                                auto& se = str.data.as_Named();
                                ASSERT_BUG(sp, !se.empty(), "Unsized struct without fields - " << type); tailTpl = se.back().ty;
                                break;
                            }
                        }
                        ASSERT_BUG(sp, tailTpl, "Missing unsized tail field for " << type);

                        const auto& path = type->as_Path().path.data.as_Generic();
                        auto tailTy = MonomorphStatePtr(crate.types, type, &path.params, nullptr).monomorphType(sp, tailTpl);
                        this->expandAssociatedTypes(sp, tailTy);

                        return findImpl(sp, traitPath, traitParams, tailTy, [&](ImplRef impl, bool unk) {
                            HIRTraitPath::assocListT assoc;
                            auto metadataTy = impl.getType(crate.types, "Metadata", {});
                            if (metadataTy) {
                                assoc.insert(std::make_pair(nameMetadata, HIRTraitPath::AtyEqual{traitPath, {}, std::move(metadataTy)}));
                            }
                            return foundCb.visit(ImplRef(type, traitParams ? traitParams->clone() : HIRPathParams(), std::move(assoc)), unk);
                        });
                    }
                    case HIRStructMarkings::DstType::Slice:
                        return foundCb.visit(ImplRef(type, traitParams, &assocSlice), false);
                }
            }
            // A tuple is unsized when its last element is, and it takes that
            // element's metadata.
            else if (type->is_Tuple() && !type->as_Tuple().empty()) {
                auto tailTy = HIRTypeRef(type->as_Tuple().back());
                this->expandAssociatedTypes(sp, tailTy);
                return findImpl(sp, traitPath, traitParams, tailTy, [&](ImplRef impl, bool unk) {
                    HIRTraitPath::assocListT assoc;
                    auto metadataTy = impl.getType(crate.types, "Metadata", {});
                    if (metadataTy) {
                        assoc.insert(std::make_pair(nameMetadata, HIRTraitPath::AtyEqual{traitPath, {}, std::move(metadataTy)}));
                    }
                    return foundCb.visit(ImplRef(type, traitParams ? traitParams->clone() : HIRPathParams(), std::move(assoc)), unk);
                });
            }
            return foundCb.visit(ImplRef(type, traitParams, &assocUnit), false);
        } else if (traitPath == langPointeeSized()) {
            // Lowest level of sizedness: This _might_ be sized (i.e. it's not an extern type?)
            return foundCb.visit(ImplRef(type, &nullParams, &nullAssoc), false);
            //switch( this->metadata_type(sp, type) )
            //{
            //case MetadataType::Unknown:
            //case MetadataType::None:
            //case MetadataType::Slice:
            //case MetadataType::TraitObject:
            //case MetadataType::Zero:
            //}
        } else if (traitPath == langMetaSized()) {
            // Next level of sizedness: There's metadata that allows getting the size
            // - No difference to the above?
            switch (this->metadataType(sp, type)) {
                case MetadataType::Unknown:
                    break;
                case MetadataType::None:
                case MetadataType::Slice:
                case MetadataType::TraitObject:
                case MetadataType::Zero: // TODO: Does zero apply here?
                    return foundCb.visit(ImplRef(type, &nullParams, &nullAssoc), false);
            }
        } else if (traitPath == langDestruct()) {
            // is there anything indestructible? Maybe extern types
            return foundCb.visit(ImplRef(type, &nullParams, &nullAssoc), false);
        }
    }

    // Special case: Generic placeholder
    if (const auto* e = type->opt_Generic()) {
        if (e->group() == GENERICPlaceholder) {
            // TODO: If the type is a magic placeholder, assume it impls the specified trait.
            // TODO: Restructure so this knows that the placehlder impls the impl-provided bounds.
            return foundCb.visit(ImplRef(type, traitParams, &nullAssoc), false);
        }
    }

    // The definitional shortcuts above answer from type structure alone and
    // must stay authoritative: general candidate assembly walks in-scope
    // bounds, and early pipeline phases (metadata for ResolveUFCSOuter)
    // still hold those in unresolved UfcsUnknown form -- data the goal
    // machinery must not see. Once the bounds are resolved the bridge takes
    // every query.
    if (this->wb.settings->solver.globally && !dontHandoffToSpecialised && !noGoalBridge
        && !genericBoundsUnresolved(implGenerics_) && !genericBoundsUnresolved(itemGenerics_)) {
        if (!nextSolver) {
            ASSERT_BUG(sp, crate.pool, "next-solver requires the crate object pool");
            nextSolver = crate.pool->make<NextSolverBridge>(this->wb);
        }
        return nextSolver->findImpl(sp, implGenerics_, itemGenerics_, traitPath, traitParams, type, foundCb);
    }

    struct H {
        static const HIRTypeData* getRootTy(const HIRTypeData* t) {
            if (const auto* e = t->opt_Path()) {
                switch (e->path.data.tag()) {
                    case HIRPathData::TAG_Generic: {
                        break;
                    }
                    case HIRPathData::TAG_UfcsKnown: {
                        auto& ee = e->path.data.as_UfcsKnown();
                        return getRootTy(ee.type);
                    }
                    case HIRPathData::TAG_UfcsUnknown: {
                        auto& ee = e->path.data.as_UfcsUnknown();
                        return getRootTy(ee.type);
                    }
                    case HIRPathData::TAG_UfcsInherent: {
                        auto& ee = e->path.data.as_UfcsInherent();
                        return getRootTy(ee.type);
                    }
                }
            }
            return t;
        }

        static bool checkParams(const Span& sp, const HIRPathParams& targetParams, const HIRPathParams* traitParams) {
            if (!traitParams) {
                return true;
            }

            return targetParams.compareWithPlaceholders(sp, *traitParams, HIRResolvePlaceholdersNop()) != HIRCompare::Unequal;
        }
    };

    if (type != HIRTypeRef() && H::getRootTy(type) == HIRTypeRef()) {
        return foundCb.visit(ImplRef(type, traitParams, &nullAssoc), false);
    }

    const bool isAsyncCallableTrait = traitPath == langAsyncFn() || traitPath == langAsyncFnMut() || traitPath == langAsyncFnOnce();
    auto findAsyncCallable = [&](const ::std::vector<HIRTypeRef>& inputTypes, const HIRTypeData* futureType, bool supportsShared, bool supportsMutable) {
        if (!isAsyncCallableTrait || (traitPath == langAsyncFn() && !supportsShared) || (traitPath == langAsyncFnMut() && !supportsMutable)) {
            return false;
        }

        HIRPathParams actualParams;
        actualParams.types.push_back(crate.types.tuple(inputTypes));
        if (!H::checkParams(sp, actualParams, traitParams)) {
            return false;
        }

        HIRTypeRef outputType = HIRTypeRef();
        bool futureUnknown = false;
        this->findImpl(sp, langFuture(), nullptr, futureType, [&](ImplRef impl, bool unknown) {
            auto candidateOutput = impl.getType(crate.types, "Output", {});
            if (candidateOutput == HIRTypeRef()) {
                return false;
            }
            outputType = mv$(candidateOutput);
            futureUnknown = unknown;
            return true;
        });
        if (outputType == HIRTypeRef()) {
            return false;
        }

        HIRGenericPath oncePath(langAsyncFnOnce(), actualParams.clone());
        HIRTraitPath::assocListT assoc;
        assoc.insert(::std::make_pair("Output", HIRTraitPath::AtyEqual{oncePath.clone(), {}, outputType}));
        assoc.insert(::std::make_pair("CallOnceFuture", HIRTraitPath::AtyEqual{mv$(oncePath), {}, futureType}));
        // A by-reference call hands back the same future; its lifetime parameter
        // is not carried in HIR.
        assoc.insert(::std::make_pair("CallRefFuture", HIRTraitPath::AtyEqual{HIRGenericPath(langAsyncFnMut(), actualParams.clone()), {}, futureType}));
        return foundCb.visit(ImplRef(type, mv$(actualParams), mv$(assoc)), futureUnknown);
    };

    // --- MAGIC IMPLS ---
    // TODO: There should be quite a few more here, but laziness
    switch ((*type).tag()) {
default:
        // Nothing magic
        break;
        case HIRTypeData::TAG_Tuple: {
            if (traitPath == crate.getLangItemPath(sp, "tuple_trait")) {
                return foundCb.visit(ImplRef(type, HIRPathParams(), HIRTraitPath::assocListT()), false);
            }
            break;
        }
        case HIRTypeData::TAG_Function: {
            auto& e = (*type).as_Function();
            if (isAsyncCallableTrait) {
                if (e.abi != ABI_RUST || e.isUnsafe) {
                    return false;
                }
                return findAsyncCallable(e.argTypes, e.rettype, true, true);
            }
            if (traitPath == langFn() || traitPath == langFnMut() || traitPath == langFnOnce()) {
                if (traitParams) {
                    const auto& desArgTys = traitParams->types.at(0)->as_Tuple();
                    if (desArgTys.size() != e.argTypes.size()) {
                        return false;
                    }
                    for (unsigned int i = 0; i < desArgTys.size(); i++) {
                        if (desArgTys[i]->compareWithPlaceholders(sp, e.argTypes[i], cbIdent) == HIRCompare::Unequal) {
                            return false;
                        }
                    }
                }
                std::vector<HIRTypeRef> argTypes;
                for (unsigned int i = 0; i < e.argTypes.size(); i++) {
                    argTypes.push_back(e.argTypes[i]);
                }
                HIRPathParams params;
                params.types.push_back(crate.types.tuple(std::move(argTypes)));
                HIRTraitPath::assocListT assoc;
                assoc.insert(::std::make_pair("Output", HIRTraitPath::AtyEqual{HIRGenericPath(langFnOnce(), params.clone()), {}, e.rettype}));
                return foundCb.visit(ImplRef(type, mv$(params), mv$(assoc)), false);
            }
            // 1.74: Magic impls of `eq` for function pointers
            if (traitPath == this->crate.getLangItemPathOpt("fn_ptr_trait")) {
                return foundCb.visit(ImplRef(type, {}, {}), false);
            }
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            auto& realE = (*type).as_NamedFunction();
            if (isAsyncCallableTrait) {
                auto e = realE.decay(crate.types, sp);
                if (e.abi != ABI_RUST || e.isUnsafe) {
                    return false;
                }
                return findAsyncCallable(e.argTypes, e.rettype, true, true);
            }
            if (traitPath == langFn() || traitPath == langFnMut() || traitPath == langFnOnce()) {
                auto e = realE.decay(crate.types, sp);
                if (traitParams) {
                    const auto& desArgTys = traitParams->types.at(0)->as_Tuple();
                    if (desArgTys.size() != e.argTypes.size()) {
                        return false;
                    }
                    for (unsigned int i = 0; i < desArgTys.size(); i++) {
                        if (desArgTys[i]->compareWithPlaceholders(sp, e.argTypes[i], cbIdent) == HIRCompare::Unequal) {
                            return false;
                        }
                    }
                }
                std::vector<HIRTypeRef> argTypes;
                for (unsigned int i = 0; i < e.argTypes.size(); i++) {
                    argTypes.push_back(e.argTypes[i]);
                }
                HIRPathParams params;
                params.types.push_back(crate.types.tuple(std::move(argTypes)));
                HIRTraitPath::assocListT assoc;
                assoc.insert(::std::make_pair("Output", HIRTraitPath::AtyEqual{HIRGenericPath(langFnOnce(), params.clone()), {}, e.rettype}));
                return foundCb.visit(ImplRef(type, mv$(params), mv$(assoc)), false);
            }
            break;
        }
        case HIRTypeData::TAG_NodeType: {
            auto& e = (*type).as_NodeType();
            switch (e.tag()) {
                case HIRTypeDataNodeType::TAG_Closure: {
                    auto& nodeP = e.as_Closure();
                    if (isAsyncCallableTrait) {
                        bool supportsShared = true;
                        bool supportsMutable = true;
                        if (nodeP->cls == HIRExprNodeClosure::Class::Once) {
                            supportsShared = false;
                            supportsMutable = false;
                        } else if (nodeP->cls == HIRExprNodeClosure::Class::Mut) {
                            supportsShared = false;
                        }
                        ::std::vector<HIRTypeRef> inputs;
                        inputs.reserve(nodeP->args.size());
                        for (const auto& arg : nodeP->args) {
                            inputs.push_back(arg.second);
                        }
                        return findAsyncCallable(inputs, nodeP->returnType, supportsShared, supportsMutable);
                    }
                    if (traitPath == langFn() || traitPath == langFnMut() || traitPath == langFnOnce()) {
                        if (traitParams) {
                            const auto& desArgTys = traitParams->types.at(0)->as_Tuple();
                            if (desArgTys.size() != nodeP->args.size()) {
                                return false;
                            }
                            for (unsigned int i = 0; i < desArgTys.size(); i++) {
                                if (desArgTys[i]->compareWithPlaceholders(sp, nodeP->args[i].second, HIRResolvePlaceholdersNop()) == HIRCompare::Unequal) {
                                    return false;
                                }
                            }
                        } else {
                            traitParams = &nullParams;
                        }
                        switch (nodeP->cls) {
                            case HIRExprNodeClosure::Class::Unknown:
                                break;
                            case HIRExprNodeClosure::Class::NoCapture:
                                break;
                            case HIRExprNodeClosure::Class::Once:
                                if (traitPath == langFnMut()) {
                                    return false;
                                }
                            case HIRExprNodeClosure::Class::Mut:
                                if (traitPath == langFn()) {
                                    return false;
                                }
                            case HIRExprNodeClosure::Class::Shared:
                                break;
                        }
                        HIRTraitPath::assocListT assoc;
                        assoc.insert(::std::make_pair("Output", HIRTraitPath::AtyEqual{HIRGenericPath(langFnOnce(), traitParams->clone()), {}, nodeP->returnType}));
                        return foundCb.visit(ImplRef(type, traitParams->clone(), mv$(assoc)), false);
                    }
                    break;
                }
                case HIRTypeDataNodeType::TAG_Generator: {
                    auto& nodeP = e.as_Generator();
                    if (traitPath == langGenerator()) {
                        HIRTraitPath::assocListT assoc;
                        assoc.insert(::std::make_pair("Yield", HIRTraitPath::AtyEqual{traitPath.clone(), {}, nodeP->yieldTy}));
                        assoc.insert(::std::make_pair("Return", HIRTraitPath::AtyEqual{traitPath.clone(), {}, nodeP->returnType}));
                        HIRPathParams params;
                        params.types.push_back(nodeP->resumeTy);
                        return foundCb.visit(ImplRef(type, mv$(params), mv$(assoc)), HIRCompare::Equal);
                    }
                    break;
                }
                case HIRTypeDataNodeType::TAG_Async: {
                    auto& nodeP = e.as_Async();
                    if (nodeP->isAsyncGen) {
                        // An `async gen` block is an AsyncIterator, not a Future.
                        if (traitPath == langAsyncIterator()) {
                            HIRTraitPath::assocListT assoc;
                            assoc.insert(::std::make_pair("Item", HIRTraitPath::AtyEqual{traitPath.clone(), {}, nodeP->yieldTy}));
                            return foundCb.visit(ImplRef(type, HIRPathParams(), mv$(assoc)), HIRCompare::Equal);
                        }
                    } else if (traitPath == langFuture()) {
                        HIRTraitPath::assocListT assoc;
                        assoc.insert(::std::make_pair("Output", HIRTraitPath::AtyEqual{traitPath.clone(), {}, nodeP->code->resType}));
                        HIRPathParams params;
                        return foundCb.visit(ImplRef(type, mv$(params), mv$(assoc)), HIRCompare::Equal);
                    }
                    break;
                }
            }
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& e = (*type).as_TraitObject();
            if (traitPath == e.trait.path.path) {
                if (H::checkParams(sp, e.trait.path.params, traitParams)) {
                    return foundCb.visit(ImplRef(type, &e.trait.path.params, &e.trait.typeBounds, e.trait.constness), false);
                }
            }
            // Markers too
            for (const auto& mt : e.markers) {
                if (traitPath == mt.path) {
                    if (H::checkParams(sp, mt.params, traitParams)) {
                        return foundCb.visit(ImplRef(type, &mt.params, &nullAssoc), false);
                    }
                }
            }

            // - Check if the desired trait is a supertrait of this.
            // TODO: What if `trait_params` is nullptr?
            bool rv = false;
            bool isSupertrait = traitParams && e.trait.traitPtr && this->findNamedTraitInTrait(sp, traitPath, *traitParams, *e.trait.traitPtr, e.trait.path.path, e.trait.path.params, type, [&](const HIRPathParams& iParams, HIRTraitPath::assocListT iAssoc) -> bool {
                // Invoke callback with a proper ImplRef
                HIRTraitPath::assocListT assocClone;
                for (const auto& e : iAssoc) {
                    assocClone.insert(::std::make_pair(e.first, e.second.clone()));
                }
                // HACK! Just add all the associated type bounds (only inserted if not already present)
                for (const auto& e2 : e.trait.typeBounds) {
                    assocClone.insert(::std::make_pair(e2.first, e2.second.clone()));
                }

                ImplRef ir{type, iParams.clone(), std::move(assocClone)};
                DEBUG("[TraitObject] - ir = " << ir);
                rv = foundCb.visit(mv$(ir), false);
                return true;
            });
            if (isSupertrait) {
                return rv;
            }
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            auto& e = (*type).as_ErasedType();
            for (const auto& trait : e.traits) {
                bool rv = false;
                // TODO: If `trait_params` is nullptr, this doesn't run (is that sane?)
                bool isSupertrait = traitParams && this->findNamedTraitInTrait(sp, traitPath, *traitParams, *trait.traitPtr, trait.path.path, trait.path.params, type, [&](const auto& iParams, const auto& iAssoc) {
                    // Invoke callback with a proper ImplRef
                    HIRTraitPath::assocListT assocClone;
                    for (const auto& assocE : iAssoc) {
                        assocClone.insert(::std::make_pair(assocE.first, assocE.second.clone()));
                    }
                    // HACK! Just add all the associated type bounds (only inserted if not already present)
                    for (const auto& e2 : trait.typeBounds) {
                        assocClone.insert(::std::make_pair(e2.first, e2.second.clone()));
                    }
                    auto ir = ImplRef(type, iParams.clone(), mv$(assocClone));
                    DEBUG("[ErasedType] - ir = " << ir);
                    rv = foundCb.visit(mv$(ir), false);
                    return true;
                });
                if (isSupertrait) {
                    return rv;
                }
            }
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = (*type).as_Path();
            if (e.binding.is_Opaque()) {
                ASSERT_BUG(sp, e.path.data.is_UfcsKnown(), "Opaque bound type wasn't UfcsKnown - " << type);
                const auto& pe = e.path.data.as_UfcsKnown();
                DEBUG("Checking bounds on definition of " << pe.item << " in " << pe.trait);

                // If this associated type has a bound of the desired trait, return it.
                const auto& traitRef = crate.getTraitByPath(sp, pe.trait.path);
                ASSERT_BUG(sp, traitRef.types.count(pe.item) != 0, "Trait " << pe.trait.path << " doesn't contain an associated type " << pe.item);
                const auto& atyDef = traitRef.types.find(pe.item)->second;

                auto monomorphCb = MonomorphStatePtr(crate.types, pe.type, &pe.trait.params, &pe.params);

                auto checkBound = [&](const HIRTraitPath& bound) {
                    const auto& bParams = bound.path.params;
                    HIRPathParams paramsMonoO;
                    const auto& bParamsMono = monomorphisePathparamsWithOpt(sp, paramsMonoO, bParams, monomorphCb, false);
                    this->expandAssociatedTypesParams(sp, paramsMonoO);
                    DEBUG("[find_impl] ATY : " << bound.path.path << bParamsMono);

                    if (bound.path.path == traitPath) {
                        if (H::checkParams(sp, bParamsMono, traitParams)) {
                            // Optimisation: If this was a monomorphised path, then move ownership into the ImplRef
                            if (&bParamsMono == &paramsMonoO || ::std::any_of(bound.typeBounds.begin(), bound.typeBounds.end(), [&](const auto& x) {
                                return monomorphisePathparamsNeeded(x.second.atyParams)
                                    || monomorphiseTypeNeeded(x.second.type);
                            })) {
                                HIRTraitPath::assocListT atys;
                                if (!bound.typeBounds.empty()) {
                                    for (const auto& tb : bound.typeBounds) {
                                        auto src = monomorphCb.monomorphGenericpath(sp, tb.second.sourceTrait, false);
                                        auto atyParams = monomorphCb.monomorphPathParams(sp, tb.second.atyParams, false);
                                        auto aty = monomorphCb.monomorphType(sp, tb.second.type, false);
                                        expandAssociatedTypes(sp, aty);
                                        expandAssociatedTypesParams(sp, src.params);
                                        expandAssociatedTypesParams(sp, atyParams);
                                        atys.insert(::std::make_pair(tb.first, HIRTraitPath::AtyEqual{mv$(src), mv$(atyParams), mv$(aty)}));
                                    }
                                }
                                if (foundCb.visit(ImplRef(type, mv$(paramsMonoO), mv$(atys), bound.constness), false)) {
                                    return true;
                                }
                                paramsMonoO = monomorphCb.monomorphPathParams(sp, bParams, false);
                            } else {
                                if (foundCb.visit(ImplRef(type, &bound.path.params, &bound.typeBounds, bound.constness), false)) {
                                    return true;
                                }
                            }
                        }
                    }

                    if (traitParams) {
                        return this->findNamedTraitInTrait(sp, traitPath, *traitParams, *bound.traitPtr, bound.path.path, bParamsMono, type, [&](const auto& iParams, const auto& iAssoc) {
                            if (iParams != *traitParams) {
                                return false;
                            }
                            DEBUG("impl " << traitPath << iParams << " for " << type << " -- desired " << traitPath << *traitParams);
                            return foundCb.visit(ImplRef(type, iParams.clone(), {}, bound.constness), false);
                        });
                    } else {
                        auto monomorph = MonomorphStatePtr(crate.types, type, &bParamsMono, nullptr);

                        for (const auto& pt : bound.traitPtr->allParentTraits) {
                            auto ptMono = monomorph.monomorphTraitpath(sp, pt, false);

                            // TODO: When in pre-typecheck mode, this needs to be a fuzzy match (because there might be a UfcsUnknown in the
                            // monomorphed version) OR, there may be placeholders
                            if (pt.path.path == traitPath) {
                                // TODO: Monomorphse trait params
                                return foundCb.visit(ImplRef(type, mv$(ptMono.path.params), {}, ptMono.constness), false);
                            }
                        }
                        return false;
                    }
                };

                for (const auto& bound : atyDef.traitBounds) {
                    if (checkBound(bound)) {
                        return true;
                    }
                }

                // Check `where` clauses on the trait too
                for (const auto& bound : traitRef.params.bounds) {
                    if (!bound.is_TraitBound()) {
                        continue;
                    }
                    const auto& be = bound.as_TraitBound();

                    DEBUG("be.type = " << be.type);
                    if (!be.type->is_Path()) {
                        continue;
                    }
                    if (!be.type->as_Path().path.data.is_UfcsKnown()) {
                        continue;
                    }
                    {
                        const auto& pe2 = be.type->as_Path().path.data.as_UfcsKnown();
                        if (pe2.type != crate.types.self()) {
                            continue;
                        }
                        if (pe2.trait.path != pe.trait.path) {
                            continue;
                        }
                        if (pe2.item != pe.item) {
                            continue;
                        }
                    }

                    if (checkBound(be.trait)) {
                        return true;
                    }
                }

                // Recurse into the type to find an inner `impl Foo`
                //if( pe.type->is_Path() )
                {
                    ::std::vector<const HIRPath::Data::Data_UfcsKnown*> stack;
                    stack.push_back(&pe);
                    const auto* ity = &pe.type;
                    while (const auto* inner = (*ity)->opt_Path()) {
                        if (const auto* ufcs = inner->path.data.opt_UfcsKnown()) {
                            stack.push_back(ufcs);
                            ity = &ufcs->type;
                        }
                        break;
                    }
                    if (const auto* innerErased = (*ity)->opt_ErasedType()) {
                        DEBUG("ErasedBounds: " << *ity);
                        assert(!stack.empty());
                        const auto* traits = &innerErased->traits;

                        for (;;) {
                            const auto* pe = stack.back();
                            DEBUG("ErasedBounds: " << pe->trait << " :: " << pe->item);
                            const HIRTraitPath* tp = nullptr;
                            for (const auto& t : *traits) {
                                if (t.path == pe->trait) {
                                    tp = &t;
                                    break;
                                }
                            }
                            assert(tp != nullptr);
                            if (tp->traitBounds.count(pe->item)) {
                                traits = &tp->traitBounds.at(pe->item).traits;
                            } else {
                                DEBUG("No bounds on this item");
                                traits = nullptr;
                                break;
                            }

                            stack.pop_back();
                            if (stack.empty()) {
                                break;
                            }
                        }

                        // Found the final trait list
                        // - This is for the top-level trait
                        if (traits) {
                            for (const auto& t : *traits) {
                                if (checkBound(t)) {
                                    return true;
                                }
                            }
                        }
                    }
                }

                DEBUG("- No bounds on trait/aty matched");
            }
            break;
        }
    }

    const bool isMarker = crate.getTraitByPath(sp, traitPath).isMarker;
    bool pushedNonMarkerGoal = false;
    if (!isMarker) {
        for (const auto& ent : findImplStack) {
            if (*::std::get<0>(ent) != traitPath) {
                continue;
            }
            if ((::std::get<1>(ent) == nullptr) != (traitParams == nullptr)) {
                continue;
            }
            if (traitParams && *::std::get<1>(ent) != *traitParams) {
                continue;
            }
            if (::std::get<2>(ent) != type) {
                continue;
            }

            // Ordinary trait goals are inductive: seeing the same fully
            // interned goal while checking one of its candidates means that
            // candidate cannot prove itself.
            return false;
        }
        findImplStack.push_back(::std::make_tuple(&traitPath, traitParams, type));
        pushedNonMarkerGoal = true;
    }
    STD_DEFER {
        if (pushedNonMarkerGoal) {
            findImplStack.pop_back();
        }
    };

    bool ret;

    if( isMarker )
    {
        struct H {
            static bool findImplAutoTraitCheck(const StaticTraitResolve& self, const Span& sp, const HIRSimplePath& traitPath, const HIRPathParams* traitParams, const HIRTypeData* type, StaticImplCallback& foundCb, const HIRMarkerImpl& impl, bool& outRv) {
                DEBUG("- Auto " << (impl.isPositive ? "Pos" : "Neg") << " impl" << impl.params.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << impl.type << " " << impl.params.fmtBounds());
                if (impl.isPositive) {
                    return self.findImplCheckCrateRaw(sp, traitPath, traitParams, type, impl.params, impl.traitArgs, impl.type, [&](auto implParams, auto cmp) -> bool {
                        outRv = foundCb.visit(ImplRef(type, traitParams, &nullAssoc), cmp == HIRCompare::Fuzzy);
                        return outRv;
                    });
                } else {
                    return self.findImplCheckCrateRaw(sp, traitPath, traitParams, type, impl.params, impl.traitArgs, impl.type, [&](auto implParams, auto cmp) -> bool {
                        outRv = false;
                        return true;
                    });
                }
            }
        };

        // Positive/negative impls
        bool rv = false;
        ret = this->crate.findAutoTraitImpls(traitPath, type, cbIdent, [&](const auto& impl) -> bool {
            return H::findImplAutoTraitCheck(*this, sp, traitPath, traitParams, type, foundCb, impl, rv);
        });
        if (ret) {
            return rv;
        }

        // Legacy static lookup is recursive too.  Keep its active goals on
        // this resolver instance instead of in process-global state.
        for (const auto& ent : findImplStack) {
            if (*::std::get<0>(ent) != traitPath) {
                continue;
            }
            if (::std::get<1>(ent) && traitParams && *::std::get<1>(ent) != *traitParams) {
                continue;
            }
            if (::std::get<2>(ent) != type) {
                continue;
            }

            return foundCb.visit(ImplRef(type, traitParams, &nullAssoc), false);
        }
        findImplStack.push_back(::std::make_tuple(&traitPath, traitParams, type));
        STD_DEFER {
            findImplStack.pop_back();
        };

        auto cmp = this->checkAutoTraitImplDestructure(sp, traitPath, traitParams, type);
        if (cmp != HIRCompare::Unequal) {
            return foundCb.visit(ImplRef(type, traitParams, &nullAssoc), cmp == HIRCompare::Fuzzy);
        }
    }
    else
    {
        // Search the crate for impls
        DEBUG("Search for " << traitPath << " for " << type);
        ret = crate.findTraitImpls(traitPath, type, cbIdent, [&](const auto& impl) {
            return this->findImplCheckCrateCb(sp, traitPath, traitParams, type, foundCb, impl);
        });
        if (ret) {
            return true;
        }
    }

    // A closure that returns a future is async-callable. The closure's own
    // `Fn*` impls are generated (so the magic impl above no longer applies once
    // the closure is a struct), and each async callable trait forwards to the
    // `Fn*` trait that passes the closure the same way.
    if (isAsyncCallableTrait && type->is_Path() && type->as_Path().isClosure() && traitParams && traitParams->types.size() == 1 && traitParams->types[0]->is_Tuple()) {
        const auto& fnTrait = traitPath == langAsyncFn() ? langFn() : (traitPath == langAsyncFnMut() ? langFnMut() : langFnOnce());
        // `Output` lives on `FnOnce`, so the returned future comes from there
        // whichever of the three traits decides how the closure is passed.
        HIRTypeRef futureType;
        this->findImpl(sp, langFnOnce(), traitParams, type, [&](ImplRef impl, bool unknown) {
            futureType = impl.getType(crate.types, "Output", {});
            return futureType != HIRTypeRef();
        });
        const bool callable = fnTrait == langFnOnce() || this->findImpl(sp, fnTrait, traitParams, type, [](ImplRef, bool) {
            return true;
        });
        DEBUG("Closure " << type << " callable through " << fnTrait << " = " << callable << ", returning " << futureType);
        if (callable && futureType != HIRTypeRef() && findAsyncCallable(traitParams->types[0]->as_Tuple(), futureType, true, true)) {
            DEBUG("Success");
            return true;
        }
    }

    // TODO: A bound can imply something via its associated types. How deep can this go?
    // E.g. `T: IntoIterator<Item=&u8>` implies `<T as IntoIterator>::IntoIter : Iterator<Item=&u8>`
    if( this->findImplBoundsCb(sp, traitPath, traitParams, type, foundCb) )
    {
        DEBUG("Success");
        return true;
    }

    if( type->is_Path() )
    {
    }

    return false;
}

bool StaticTraitResolve::findImplBoundsCb(const Span& sp, const HIRSimplePath& traitPath, const HIRPathParams* traitParams, const HIRTypeData* type, StaticImplCallback& foundCb) const {
    struct H {
        static bool comparePp(const Span& sp, const HIRPathParams& left, const HIRPathParams& right) {
            ASSERT_BUG(sp, left.types.size() == right.types.size(), "Parameter count mismatch between " << left << " and " << right);
            for (unsigned int i = 0; i < left.types.size(); i++) {
                // TODO: Permits fuzzy comparison to handle placeholder params, should instead do a match/test/assign
                if (left.types[i]->compareWithPlaceholders(sp, right.types[i], HIRResolvePlaceholdersNop()) == HIRCompare::Unequal) {
                    return false;
                }
            }
            return true;
        }
    };

    const bool typeHasInfer = visitTyWith(type, [&](const HIRTypeData* t) -> bool {
        return t->is_Infer();
    });
    for (auto it = traitBounds.begin(); it != traitBounds.end(); ++it) {
        if (it->first.second.path != traitPath) {
            continue;
        }
        const auto& bType = it->first.first;
        const auto& bParams = it->first.second.params;

        HIRPathParams normalizedParams;
        const HIRPathParams* comparableParams = &bParams;
        for (const auto& type : bParams.types) {
            if (type->mayHaveAssociatedType()) {
                normalizedParams = bParams.clone();
                this->expandAssociatedTypesParams(sp, normalizedParams);
                comparableParams = &normalizedParams;
                break;
            }
        }

        HIRTypeRef normalizedBound;
        const HIRTypeData* comparableBound = bType;
        if (bType->mayHaveAssociatedType() && !normalizingBoundType) {
            normalizingBoundType = true;
            STD_DEFER {
                normalizingBoundType = false;
            };
            normalizedBound = bType;
            this->expandAssociatedTypes(sp, normalizedBound);
            comparableBound = normalizedBound;
        }

        if (typeHasInfer) {
            DEBUG("ivar present: type ?= " << comparableBound);
            if (comparableBound->compareWithPlaceholders(sp, type, HIRResolvePlaceholdersNop()) == HIRCompare::Unequal) {
                continue;
            }
        } else if (comparableBound != type && !comparableBound->equalsIgnoringRegions(type)) {
            continue;
        }
        DEBUG(comparableBound << ": " << traitPath << *comparableParams);
        // Check against `params`
        if (traitParams) {
            if (!H::comparePp(sp, *traitParams, *comparableParams)) {
                continue;
            }
        }
        // Hand off to the closure, and return true if it does
        if (foundCb.visit(ImplRef(type, comparableParams, &it->second.assoc, it->second.constness), false)) {
            return true;
        }
    }

    // Obtain a pointer to UfcsKnown for magic later
    const HIRPath::Data::Data_UfcsKnown* assocInfo = nullptr;
    if (const auto* e = type->opt_Path()) {
        assocInfo = e->path.data.opt_UfcsKnown();
    }
    if (assocInfo) {
        for (auto it = traitBounds.begin(); it != traitBounds.end(); ++it) {
            if (it->first.second.path != assocInfo->trait.path || (it->first.first != assocInfo->type && !it->first.first->equalsIgnoringRegions(assocInfo->type))) {
                continue;
            }
            const auto& bound = *it;
            const auto& bParams = it->first.second.params;

            if (H::comparePp(sp, bParams, assocInfo->trait.params)) {
                const auto& traitRef = *bound.second.traitPtr;
                const auto& at = traitRef.types.at(assocInfo->item);
                for (const auto& bound : at.traitBounds) {
                    if (bound.path.path == traitPath && (!traitParams || H::comparePp(sp, bound.path.params, *traitParams))) {
                        DEBUG("- Found an associated type impl");

                        auto tpMono = MonomorphStatePtr(crate.types, assocInfo->type, &assocInfo->trait.params, &assocInfo->params).monomorphTraitpath(sp, bound, false);
                        // - Expand associated types
                        for (auto& ty : tpMono.typeBounds) {
                            this->expandAssociatedTypes(sp, ty.second.type);
                        }
                        DEBUG("- tp_mono = " << tpMono);
                        // TODO: Instead of using `type` here, build the real type
                        if (foundCb.visit(ImplRef(type, mv$(tpMono.path.params), mv$(tpMono.typeBounds), tpMono.constness), false)) {
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

namespace {

    class GetParams: public HIRMatchGenerics {
    public:
        struct ParamsSet {
            std::vector<bool> types;
            std::vector<bool> values;
        };

    private:
        Span sp;
        HIRPathParams& implParams;
        ParamsSet& paramsSet;

    public:
        GetParams(Span sp, const HIRGenericParams& implParamsDef, HIRPathParams& implParams, ParamsSet& paramsSet)
            : sp(sp)
            , implParams(implParams)
            , paramsSet(paramsSet)
        {
            implParams.types.resize(implParamsDef.types.size());
            implParams.values.resize(implParamsDef.values.size());
            paramsSet.types.resize(implParamsDef.types.size());
            paramsSet.values.resize(implParamsDef.values.size());
        }

        HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType resolveCb) override {
            ASSERT_BUG(sp, g.binding < implParams.types.size(), "[GetParams] Type generic " << g << " out of bounds (" << implParams.types.size() << ")");
            if (!paramsSet.types[g.binding]) {
                paramsSet.types[g.binding] = true;
                implParams.types[g.binding] = ty;
                DEBUG("[GetParams] Set impl ty param " << g << " to " << ty);
                return HIRCompare::Equal;
            } else {
                return implParams.types[g.binding]->compareWithPlaceholders(sp, ty, resolveCb);
            }
        }

        HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& sz) override {
            ASSERT_BUG(sp, g.binding < implParams.values.size(), "[GetParams] Value generic " << g << " out of range (" << implParams.values.size() << ")");
            if (!paramsSet.values[g.binding]) {
                paramsSet.values[g.binding] = true;
                implParams.values[g.binding] = sz.clone();
                DEBUG("[GetParams] Set impl val param " << g << " to " << sz);
                return HIRCompare::Equal;
            } else {
                if (implParams.values[g.binding] != sz) {
                    return HIRCompare::Unequal;
                } else {
                    return HIRCompare::Equal;
                }
            }
        }
    };
}

bool StaticTraitResolve::findImplCheckCrateRawCb(const Span& sp, const HIRSimplePath& desTraitPath, const HIRPathParams* desTraitParams, const HIRTypeData* desType, const HIRGenericParams& implParamsDef, const HIRPathParams& implTraitParams, const HIRTypeData* implType, StaticImplMatchCallback& foundCb) const {
    auto cbIdent = HIRResolvePlaceholdersNop();
    TRACE_FUNCTION_F("impl" << implParamsDef.fmtArgs() << " " << desTraitPath << implTraitParams << " for " << implType << implParamsDef.fmtBounds());

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
            DEBUG("CACHED: " << ent.result << " impl_params=" << ent.implParams);
            return foundCb.visit(ent.implParams.clone(), ent.result);
        }
    }
    // TODO: What if `des_trait_params` already has impl placeholders?

    HIRPathParams implParams;
    GetParams::ParamsSet paramsSet;
    GetParams getParams{sp, implParamsDef, implParams, paramsSet};

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
        DEBUG(" > Type mismatch");
        return false;
    }

    auto placeholderName = RcString::newInterned(FMT("impl_?_" << &implParamsDef));
    GetParams::ParamsSet placeholdersSet;
    HIRPathParams placeholders;
    for (unsigned int i = 0; i < implParams.types.size(); i++) {
        if (!paramsSet.types[i]) {
            if (placeholders.types.size() == 0) {
                placeholders.types.resize(implParams.types.size());
                placeholdersSet.types.resize(implParams.types.size());
            }
            placeholders.types[i] = crate.types.generic(placeholderName, 2 * 256 + baseImplPlaceholderIdx.ty + i);
            DEBUG("Placeholder " << placeholders.types[i] << " for I:" << i << " " << implParamsDef.types[i].name);
        }
    }
    for (size_t i = 0; i < implParams.values.size(); i++) {
        if (!paramsSet.values[i]) {
            if (placeholders.values.size() == 0) {
                placeholders.values.resize(implParams.values.size());
                placeholdersSet.values.resize(implParams.values.size());
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
            : Monomorphiser(types)
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
                        DEBUG("[find_impl__check_crate_raw] Bind placeholder " << i << " to " << ty);
                        placeholdersSet.types[i] = true;
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
                        DEBUG("[find_impl__check_crate_raw] Bind placeholder value " << i << " to " << val);
                        placeholdersSet.values[i] = true;
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
            if (paramsSet.types.at(ge.binding)) {
                return implParams.types.at(ge.binding);
            }
            return placeholders.types.at(ge.binding);
        }

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& val) const override {
            ASSERT_BUG(sp, val.binding < 256, "Generic value binding in " << val << " out of range (>=256)");
            ASSERT_BUG(sp, val.binding < implParams.values.size(), "Generic value binding in " << val << " out of range (>= " << implParams.values.size() << ")");
            if (paramsSet.values.at(val.binding)) {
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

                DEBUG("Trait bound " << e.type << " : " << e.trait);
                auto bTyMono = matcher.monomorphType(sp, e.type);
                this->expandAssociatedTypes(sp, bTyMono);
                auto bTpMono = matcher.monomorphTraitpath(sp, e.trait, false);
                expandAssociatedTypesTp(sp, bTpMono);
                DEBUG("- b_ty_mono = " << bTyMono << ", b_tp_mono = " << bTpMono);
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
                            DEBUG("- Placeholder param " << bTyMono << ", magic success");
                            rv = true;
                        } else {
                            rv = this->findImpl(sp, atySrcTrait.path, atySrcTrait.params, bTyMono, [&](const ImplRef& impl, bool) -> bool {
                                HIRTypeRef have = impl.getType(crate.types, atyName.c_str(), assocBound.second.atyParams);
                                if (have == HIRTypeRef()) {
                                    have = crate.types.path(HIRPath(impl.getImplType(crate.types), HIRGenericPath(atySrcTrait.path, impl.getTraitParams(crate.types)), atyName), HIRTypePathBinding::make_Unbound({}));
                                }
                                this->expandAssociatedTypes(sp, have);

                                DEBUG("[find_impl__check_crate_raw] ATY ::" << atyName << " - " << have << " ?= " << exp);
                                auto cmp = exp->matchTestGenericsFuzz(sp, have, cbIdent, matcher);
                                if (cmp == HIRCompare::Unequal) {
                                    DEBUG("Assoc ty " << atyName << " mismatch, " << have << " != des " << exp);
                                }
                                return cmp != HIRCompare::Unequal;
                            });
                        }
                        if (!rv) {
                            DEBUG("> Fail (assoc " << atyName << ") - " << bTyMono << " : " << atySrcTrait);
                            return false;
                        }
                    }
                }

                // TODO: Detect if the associated type bound above is from directly the bounded trait, and skip this if it's the case
                //else
                {
                    bool rv = false;
                    if (bTyMono->is_Generic() && bTyMono->as_Generic().isPlaceholder()) {
                        DEBUG("- Placeholder param " << bTyMono << ", magic success");
                        rv = true;
                    } else {
                        rv = this->findImpl(sp, bTpMono.path.path, bTpMono.path.params, bTyMono, [&](const auto& impl, bool) {
                            return true;
                        });
                    }
                    if (!rv && visitTyWith(bTyMono, [](const HIRTypeData* ty) {
                        return ty->is_Generic() && ty->as_Generic().isPlaceholder();
                    })) {
                        DEBUG("- Placeholder param within " << bTyMono << ", magic success");
                        rv = true;
                    }
                    if (!rv && visitTraitPathTysWith(bTpMono, [](const HIRTypeData* ty) {
                        return ty->is_Generic() && ty->as_Generic().isPlaceholder();
                    })) {
                        DEBUG("- Placeholder param within " << bTpMono << ", defer until the next bounds pass");
                        rv = true;
                    }
                    if (!rv) {
                        DEBUG("> Fail - " << bTyMono << ": " << bTpMono);
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
    DEBUG("impl_params = " << implParams);

    assert(implParamsDef.types.size() == implParams.types.size());
    for (size_t i = 0; i < implParamsDef.types.size(); i++) {
        if (implParamsDef.types.at(i).isSized) {
            // An unresolved parameter has no known sizedness yet.  It used to be
            // represented by a default-constructed ASTType*; the interned type
            // model represents that state explicitly as Infer.
            if (!implParams.types[i]->is_Infer()) {
                if (!typeIsSized(sp, implParams.types[i])) {
                    DEBUG("- Sized bound failed for " << implParams.types[i]);
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

bool StaticTraitResolve::findImplCheckCrateCb(const Span& sp, const HIRSimplePath& traitPath, const HIRPathParams* traitParams, const HIRTypeData* type, StaticImplCallback& foundCb, const HIRTraitImpl& impl) const {
    DEBUG("impl" << impl.params.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << impl.type << impl.params.fmtBounds());
    return this->findImplCheckCrateRaw(sp, traitPath, traitParams, type, impl.params, impl.traitArgs, impl.type, [&](auto implParams, auto match) {
        return foundCb.visit(ImplRef(mv$(implParams), crate.getTraitByPath(sp, traitPath), traitPath, impl), (match == HIRCompare::Fuzzy));
    });
}

HIRCompare StaticTraitResolve::checkAutoTraitImplDestructure(const Span& sp, const HIRSimplePath& trait, const HIRPathParams* paramsPtr, const HIRTypeData* type) const {
    TRACE_FUNCTION_F("trait = " << trait << ", type = " << type);
    // HELPER: Search for an impl of this trait for an inner type, and return the match type
    auto typeImplsTrait = [&](const auto& innerTy) -> HIRCompare {
        auto lRes = HIRCompare::Unequal;
        this->findImpl(sp, trait, *paramsPtr, innerTy, [&](auto, auto isFuzzy) {
            lRes = isFuzzy ? HIRCompare::Fuzzy : HIRCompare::Equal;
            return !isFuzzy;
        });
        DEBUG("[check_auto_trait_impl_destructure] " << innerTy << " - " << lRes);
        return lRes;
    };

    // - If the type is a path (struct/enum/...), search for impls for all contained types.
    if (const auto* ep = type->opt_Path()) {
        const auto& e = *ep;
        HIRCompare res = HIRCompare::Equal;
        switch (e.path.data.tag()) {
            case HIRPathData::TAG_Generic: {
                auto& pe = e.path.data.as_Generic();
                HIRTypeRef tmp;
                    auto monomorph = MonomorphStatePtr(crate.types, nullptr, &pe.params, nullptr);
                    // HELPER: Get a possibily monomorphised version of the input type (stored in `tmp` if needed)
                    auto monomorphGet = [&](const auto& ty) -> const HIRTypeData* {
                        return this->monomorphExpandOpt(sp, tmp, ty, monomorph);
                    };

                switch (e.binding.tag()) {
                    case HIRTypePathBinding::TAG_Opaque: {
                        BUG(sp, "Opaque binding on generic path - " << type);
                        break;
                    }
                    case HIRTypePathBinding::TAG_Unbound: {
                        BUG(sp, "Unbound binding on generic path - " << type);
                        break;
                    }
                    case HIRTypePathBinding::TAG_Struct: {
                        auto& tpb = e.binding.as_Struct();
                        const auto& str = *tpb;

                        // TODO: Somehow store a ruleset for auto traits on the type
                        // - Map of trait->does_impl for local fields?
                        // - Problems occur with type parameters
                        switch (str.data.tag()) {
                            case HIRStruct::Data::TAG_Unit: {
                                break;
                            }
                            case HIRStruct::Data::TAG_Tuple: {
                                auto& se = str.data.as_Tuple();
                                for (const auto& fld : se) {
                                    const auto& fldTyMono = monomorphGet(fld.ent);
                                    DEBUG("Struct::Tuple " << fldTyMono);
                                    res &= typeImplsTrait(fldTyMono);
                                    if (res == HIRCompare::Unequal) {
                                        return HIRCompare::Unequal;
                                    }
                                }
                                break;
                            }
                            case HIRStruct::Data::TAG_Named: {
                                auto& se = str.data.as_Named();
                                for (const auto& fld : se) {
                                    const auto& fldTyMono = monomorphGet(fld.ty);
                                    DEBUG("Struct::Named '" << fld.name << "' " << fldTyMono);

                                    res &= typeImplsTrait(fldTyMono);
                                    if (res == HIRCompare::Unequal) {
                                        return HIRCompare::Unequal;
                                    }
                                }
                                break;
                            }
                        }
                        break;
                    }
                    case HIRTypePathBinding::TAG_Enum: {
                        auto& tpb = e.binding.as_Enum();
                        if (const auto* e = tpb->data.opt_Data()) {
                            for (const auto& var : *e) {
                                const auto& fldTyMono = monomorphGet(var.type);
                                DEBUG("Enum '" << var.name << "'" << fldTyMono);
                                res &= typeImplsTrait(fldTyMono);
                                if (res == HIRCompare::Unequal) {
                                    return HIRCompare::Unequal;
                                }
                            }
                        }
                        break;
                    }
                    case HIRTypePathBinding::TAG_Union: {
                        auto& tpb = e.binding.as_Union();
                        for (const auto& fld : tpb->variants) {
                            const auto& fldTyMono = monomorphGet(fld.ty);
                            DEBUG("Union '" << fld.name << "' " << fldTyMono);
                            res &= typeImplsTrait(fldTyMono);
                            if (res == HIRCompare::Unequal) {
                                return HIRCompare::Unequal;
                            }
                        }
                        break;
                    }
                    case HIRTypePathBinding::TAG_ExternType: {
                        TODO(sp, "Check auto trait destructure on extern type " << type);
                        break;
                    }
                }
                DEBUG("- Nothing failed, calling callback");
                break;
            }
            case HIRPathData::TAG_UfcsUnknown: {
                BUG(sp, "UfcsUnknown in typeck - " << type);
                break;
            }
            case HIRPathData::TAG_UfcsKnown: {
                return HIRCompare::Unequal;
                //TODO(sp, "Check trait bounds for bound on UfcsKnown " << type);
                break;
            }
            case HIRPathData::TAG_UfcsInherent: {
                TODO(sp, "Auto trait lookup on UFCS Inherent type");
                break;
            }
        }
        return res;
    } else if (const auto* ep = type->opt_Tuple()) {
        HIRCompare res = HIRCompare::Equal;
        for (const auto& sty : *ep) {
            res &= typeImplsTrait(sty);
            if (res == HIRCompare::Unequal) {
                return HIRCompare::Unequal;
            }
        }
        return res;
    } else if (const auto* e = type->opt_Array()) {
        return typeImplsTrait(e->inner);
    }
    // Otherwise, there's no negative so it must be positive
    else {
        return HIRCompare::Equal;
    }
}

const HIRTypeData* StaticTraitResolve::fixTraitDefaultReturn(const Span& sp, const HIRItemPath& p, const HIRTypeData* tpl, HIRTypeRef& tmp) const {
    // If in a trait, then force expand erased associated types:
    // These are `<Self/**/ as ::"bin#"::TestTrait>::erased#with_default_0<'M0,>/*O*/`
    // Detect this by first ensuring that we're in a trait body, then if there's an ATY from that trait
    const auto& topIp = p.getTopIp();
    if (topIp.ty && topIp.trait && topIp.ty == crate.types.self()) {
        auto prefix = FMT(ATY_PREFIX_ERASED << p.name << "_");
        const auto& trait = crate.getTraitByPath(sp, *topIp.trait);
        tmp = cloneTyWith(crate.types, sp, tpl, [&](const HIRTypeData* tpl, HIRTypeRef& out) -> bool {
            if (const auto* p = tpl->opt_Path()) {
                if (const auto* pe = p->path.data.opt_UfcsKnown()) {
                    DEBUG("ATY " << tpl);
                    if (pe->type == topIp.ty && pe->trait.path == *topIp.trait && std::strncmp(pe->item.c_str(), prefix.c_str(), prefix.size()) == 0) {
                        // Does this type have a default?
                        const auto& ty = trait.types.at(pe->item);
                        DEBUG("-> " << ty.defaultValue);
                        if (ty.hasDefault) {
                            out = ty.defaultValue;
                            return true;
                        }
                    }
                }
            }
            return false;
        });
        DEBUG("fix_trait_default_return: fixed to " << tmp);
        return tmp;
    }
    return tpl;
}

void StaticTraitResolve::expandAssociatedTypes(const Span& sp, HIRTypeRef& input) const {
    TRACE_FUNCTION_FR(input, input);
    input = this->expandAssociatedTypesInner(sp, input);
}

void StaticTraitResolve::revealOpaqueTypes(const Span& sp, HIRTypeRef& input) const {
    class Visitor: public HIRVisitor {
        const Span& sp;
        const StaticTraitResolve& resolve;
        bool clearOpaque = false;

        void revealOpaqueType(HIRTypeRef& ty) {
            const auto& erased = ty->as_ErasedType();
            HIRTypeRef revealed;

            switch (erased.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    auto& e = erased.inner.as_Fcn();
                    MonomorphState monomorph(resolve.hirCrate().types);
                    auto value = resolve.getValue(sp, e.origin, monomorph);
                    if (value.is_NotYetKnown() && e.origin.data.is_UfcsKnown()) {
                        const auto& path = e.origin.data.as_UfcsKnown();
                        auto name = RcString::newInterned(FMT(ATY_PREFIX_ERASED << path.item << "_" << e.index));
                        revealed = resolve.hirCrate().types.path(HIRPath(path.type, path.trait.clone(), name, path.params.clone()), {});
                    } else {
                        ASSERT_BUG(sp, value.is_Function(), "ErasedType with Fcn type doesn't point at a function: " << e.origin << ": " << value.tagStr());
                        const auto& function = *value.as_Function();
                        if (e.index >= function.code.erasedTypes.size()) {
                            resolve.hirCrate().getOrGenMir(resolve.board(), HIRItemPath(e.origin), function);
                        }
                        ASSERT_BUG(sp, e.index < function.code.erasedTypes.size(), "Erased type index out of range for " << e.origin << " - " << e.index << " >= " << function.code.erasedTypes.size());
                        revealed = monomorph.monomorphType(sp, function.code.erasedTypes[e.index]);
                    }
                    resolve.expandAssociatedTypes(sp, revealed);
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Alias: {
                    auto& e = erased.inner.as_Alias();
                    if (e.inner->type == HIRTypeRef()) {
                        auto definers = resolve.hirCrate().opaqueTypeDefiners.find(e.inner->path);
                        if (definers != resolve.hirCrate().opaqueTypeDefiners.end()) {
                            for (const auto& path : definers->second) {
                                MonomorphState monomorph(resolve.hirCrate().types);
                                auto value = resolve.getValue(sp, path, monomorph);
                                if (const auto* function = value.opt_Function()) {
                                    resolve.hirCrate().getOrGenMir(resolve.board(), HIRItemPath(path), **function);
                                }
                                if (e.inner->type != HIRTypeRef()) {
                                    break;
                                }
                            }
                        }
                        if (e.inner->type == HIRTypeRef()) {
                            ERROR(sp, E0000, "Erased type alias " << e.inner->path << " never set");
                        }
                    }
                    revealed = MonomorphStatePtr(resolve.hirCrate().types, nullptr, &e.params, nullptr).monomorphType(sp, e.inner->type);
                    resolve.expandAssociatedTypes(sp, revealed);
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Known: {
                    auto& e = erased.inner.as_Known();
                    revealed = e;
                    break;
                }
            }

            DEBUG("> " << ty << " => " << revealed);
            ty = std::move(revealed);
        }

    public:
        Visitor(const Span& sp, const StaticTraitResolve& resolve)
            : HIRVisitor(nullptr, resolve.hirCrate().types)
            , sp(sp)
            , resolve(resolve)
        {
        }

        [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override {
            auto savedClearOpaque = clearOpaque;
            clearOpaque = false;
            if (ty->is_ErasedType()) {
                revealOpaqueType(ty);
                ty = visitType(ty);
                clearOpaque = true;
            } else {
                ty = HIRVisitor::visitType(ty);
                if (clearOpaque && ty->is_Path() && ty->as_Path().binding.is_Opaque()) {
                    auto data = ty->cloneData();
                    data.as_Path().binding = HIRTypePathBinding::make_Unbound({});
                    ty = resolve.hirCrate().types.intern(std::move(data));
                }
            }
            clearOpaque |= savedClearOpaque;
            return ty;
        }
    } visitor(sp, *this);

    expandAssociatedTypes(sp, input);
    input = visitor.visitType(input);
    expandAssociatedTypes(sp, input);
}

void StaticTraitResolve::revealOpaqueTypesPath(const Span& sp, HIRPath& input) const {
    auto revealParams = [&](HIRPathParams& params) {
        for (auto& type : params.types) {
            revealOpaqueTypes(sp, type);
        }
    };

    expandAssociatedTypesPath(sp, input);
    switch (input.data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& e = input.data.as_Generic();
            revealParams(e.params);
            break;
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& e = input.data.as_UfcsInherent();
            revealOpaqueTypes(sp, e.type);
            revealParams(e.params);
            revealParams(e.implParams);
            break;
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& e = input.data.as_UfcsKnown();
            revealOpaqueTypes(sp, e.type);
            revealParams(e.trait.params);
            revealParams(e.params);
            break;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            auto& e = input.data.as_UfcsUnknown();
            revealOpaqueTypes(sp, e.type);
            revealParams(e.params);
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
        case HIRPathData::TAG_Generic: {
            auto& e2 = input.data.as_Generic();
            this->expandAssociatedTypesParams(sp, e2.params);
            break;
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& e2 = input.data.as_UfcsInherent();
            e2.type = this->expandAssociatedTypesInner(sp, e2.type);
            this->expandAssociatedTypesParams(sp, e2.params);
            // TODO: impl params too?
            for (auto& arg : e2.implParams.types) {
                arg = this->expandAssociatedTypesInner(sp, arg);
            }
            break;
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& e2 = input.data.as_UfcsKnown();
            e2.type = this->expandAssociatedTypesInner(sp, e2.type);
            this->expandAssociatedTypesParams(sp, e2.trait.params);
            this->expandAssociatedTypesParams(sp, e2.params);
            break;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            auto& e2 = input.data.as_UfcsUnknown();
            e2.type = this->expandAssociatedTypesInner(sp, e2.type);
            this->expandAssociatedTypesParams(sp, e2.params);
            break;
        }
    }
}

bool StaticTraitResolve::expandAssociatedTypesSingle(const Span& sp, HIRTypeRef& input) const {
    TRACE_FUNCTION_F(input);
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
                        DEBUG("Cached " << it->second);
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
                if (range.hasStart) ConvertHIRConstantEvaluateConstGeneric(sp, this->wb, crate, ne.inner, range.start);
                if (range.hasEnd) ConvertHIRConstantEvaluateConstGeneric(sp, this->wb, crate, ne.inner, range.end);
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
    TRACE_FUNCTION_FR(input, input);
    ASSERT_BUG(sp, input->is_Path() && input->as_Path().path.data.is_UfcsInherent(), input);

    const auto& pe = input->as_Path().path.data.as_UfcsInherent();
    if (visitTyWith(pe.type, [](const HIRTypeData* type) {
        const auto* erased = type->opt_ErasedType();
        const auto* opaque = erased ? erased->inner.opt_Alias() : nullptr;
        return opaque && !opaque->inner->type;
    })) {
        DEBUG("Deferring inherent associated type with unresolved opaque receiver " << input);
        return false;
    }
    const HIRTypeAlias* alias = nullptr;
    const HIRGenericParams* implParamsDef = nullptr;
    HIRPathParams implParams;
    HIRCompare bestMatch = HIRCompare::Unequal;
    static const HIRPathParams noTraitParams;

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
        DEBUG("No inherent associated type candidate for " << input);
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
    TRACE_FUNCTION_FR(input, input);
    auto data = input->cloneData();
    auto& e = data.as_Path();
    auto& e2 = e.path.data.as_UfcsKnown();
    auto publish = [&]() {
        input = crate.types.intern(data.cloneData());
    };

    static unsigned sRecursionLevel;

    struct RecurseEntry {
        HIRTypeRef ty;
        unsigned level;
    };

    static std::vector<RecurseEntry> sRecursionStack;
    {
        bool hitSameLevelLoop = false;
        for (const auto& ent : sRecursionStack) {
            DEBUG(ent.ty << " " << ent.level);
            if (ent.ty == input) {
                if (ent.level == sRecursionLevel) {
                    hitSameLevelLoop = true;
                } else {
                    BUG(sp, "Loop in EAT");
                }
            }
        }
        if (hitSameLevelLoop) {
            DEBUG("Loop in EAT at same level");
            ::std::vector<const HIRTypeData*> ents;
            for (const auto& ent : sRecursionStack) {
                if (ent.level == sRecursionLevel) {
                    ents.push_back(ent.ty);
                }
            }
            if (ents.size() > 1) {
                std::sort(ents.begin(), ents.end(), [](const HIRTypeData* a, const HIRTypeData* b) {
                    return a < b;
                });
                input = ents[0];
            }
            DEBUG("-> " << input);
            auto opaqueData = input->cloneData();
            opaqueData.as_Path().binding = HIRTypePathBinding::make_Opaque({});
            input = crate.types.intern(std::move(opaqueData));
            return false;
        }
    }

    sRecursionStack.push_back(RecurseEntry{crate.types.path(HIRPath(e2.type, e2.trait.clone(), e2.item), {}), sRecursionLevel});
    STD_DEFER {
        sRecursionStack.pop_back();
    };

    sRecursionLevel += 1;
    e2.type = this->expandAssociatedTypesInner(sp, e2.type);
    for (auto& arg : e2.trait.params.types) {
        arg = this->expandAssociatedTypesInner(sp, arg);
    }
    sRecursionLevel -= 1;
    publish();

    DEBUG("Locating associated type for " << e.path);

    {
        const auto* t = &e2.type;
        while ((*t)->is_Path() && (*t)->as_Path().path.data.is_UfcsKnown()) {
            t = &(*t)->as_Path().path.data.as_UfcsKnown().type;
        }
        if ((*t)->is_Infer()) {
            DEBUG("Infer seen in static EAT, leaving as-is");
            return false;
        }
    }

    auto expandAsyncCallableAssociated = [&](const HIRTypeData* futureType) {
        if (e2.item == "CallOnceFuture" || e2.item == "CallRefFuture") {
            input = futureType;
            return true;
        }
        if (e2.item != "Output") {
            ERROR(sp, E0000, "No associated type " << e2.item << " for trait " << e2.trait);
        }

        bool found = false;
        this->findImpl(sp, langFuture(), nullptr, futureType, [&](ImplRef impl, bool) {
            auto output = impl.getType(crate.types, "Output", {});
            if (output == HIRTypeRef()) {
                return false;
            }
            input = mv$(output);
            found = true;
            return true;
        });
        return found;
    };

    switch ((*e2.type).tag()) {
default:
        // Nothing special
        break;
        case HIRTypeData::TAG_Infer: {
            DEBUG("Infer seen in static EAT, leaving as-is");
            return false;
        }
        case HIRTypeData::TAG_NodeType: {
            auto& te = (*e2.type).as_NodeType();
            switch (te.tag()) {
                case HIRTypeDataNodeType::TAG_Closure: {
                    auto& nodeP = te.as_Closure();
                    if (e2.trait.path == langAsyncFn() || e2.trait.path == langAsyncFnMut() || e2.trait.path == langAsyncFnOnce()) {
                        if (expandAsyncCallableAssociated(nodeP->returnType)) {
                            return true;
                        }
                    }
                    if (e2.trait.path == langFn() || e2.trait.path == langFnMut() || e2.trait.path == langFnOnce()) {
                        if (e2.item == "Output") {
                            input = nodeP->returnType;
                            return true;
                        } else {
                            ERROR(sp, E0000, "No associated type " << e2.item << " for trait " << e2.trait);
                        }
                    }
                    break;
                }
                case HIRTypeDataNodeType::TAG_Generator: {
                    break;
                }
                case HIRTypeDataNodeType::TAG_Async: {
                    break;
                }
            }
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            //    if( e2.trait.m_params == data_trait.m_params )
            //    {
            //            // TODO: Mark as opaque and return.
            //            // - Why opaque? It's not bounded, don't even bother
            //            TODO(sp, "Handle unconstrained associate type " << e2.item << " from " << e2.type);
            //        }
            //    }
            //}
            break;
        }
    }

    // 1. Bounds
    bool rv = false;
    bool assumeOpaque = true;
    if(!rv)
    {
        if (replaceEqualities(input)) {
            rv = true;
            assumeOpaque = false;
        }
    }
    if(!rv)
    {
        for (const auto& bound : traitBounds) {
            const auto& beType = bound.first.first;
            const auto& beTrait = bound.first.second;

            DEBUG("Trait bound - " << beType << " : " << beTrait);
            // 1. Check if the type matches
            //  - TODO: This should be a fuzzier match?
            if (beType != e2.type && !beType->equalsIgnoringRegions(e2.type)) {
                continue;
            }
            // 2. Check if the trait (or any supertrait) includes e2.trait
            if (beTrait.equalsIgnoringRegions(e2.trait)) {
                auto it = bound.second.assoc.find(e2.item);
                // 1. Check if the bounds include the desired item
                if (it == bound.second.assoc.end()) {
                    // If not, assume it's opaque and return as such
                    // TODO: What happens if there's two bounds that overlap? 'F: FnMut<()>, F: FnOnce<(), Output=Bar>'
                    DEBUG("Found impl for " << input << " but no bound on item, assuming opaque");
                } else {
                    assumeOpaque = false;
                    input = it->second.type;
                    rv = true;
                }
                break;
            }
        }
    }
    if( rv ) {
        if (recurse) {
            input = this->expandAssociatedTypesInner(sp, input);
        }
        return true;
    }

    // If the type of this UfcsKnown is ALSO a UfcsKnown - Check if it's bounded by this trait with equality
    // Use bounds on other associated types too (if `e2.type` was resolved to a fixed associated type)
    if(const auto* teInner = e2.type->opt_Path())
    {
        if (const auto* peInnerP = teInner->path.data.opt_UfcsKnown()) {
            const auto& peInner = *peInnerP;
            // TODO: Search for equality bounds on this associated type (e3) that match the entire type (e2)
            // - Does simplification of complex associated types
            const auto& traitPtr = this->crate.getTraitByPath(sp, peInner.trait.path);
            const auto& assocTy = traitPtr.types.at(peInner.item);

            DEBUG("Inner UfcsKnown");

            // Resolve where Self=pe_inner.type (i.e. for the trait this inner UFCS is on)
            auto cbPlaceholdersTrait = MonomorphStatePtr(crate.types, peInner.type, &peInner.trait.params, &peInner.params);
            for (const auto& bound : assocTy.traitBounds) {
                // Associated equalities on a trait bound carry the trait that
                // actually declares the item. That can be a parent trait of
                // `bound.m_path` (e.g. `Int<Unsigned = T>` where `Unsigned`
                // is declared by `MinInt`), so matching the outer path loses
                // exactly the equality we need.
                auto it = bound.typeBounds.find(e2.item);
                if (it != bound.typeBounds.end()) {
                    auto sourceTrait = cbPlaceholdersTrait.monomorphGenericpath(sp, it->second.sourceTrait, false);
                    auto atyParams = cbPlaceholdersTrait.monomorphPathParams(sp, it->second.atyParams, false);
                    if (sourceTrait.equalsIgnoringRegions(e2.trait) && atyParams.equalsIgnoringRegions(e2.params)) {
                        DEBUG("Found inner bound from " << sourceTrait << ": " << it->second.type);
                        input = monomorphiseTypeNeeded(it->second.type) ? cbPlaceholdersTrait.monomorphType(sp, it->second.type) : it->second.type;
                        if (recurse) {
                            this->expandAssociatedTypes(sp, input);
                        }
                        return true;
                    }
                }

                // Find trait in this trait.
                auto boundParams = cbPlaceholdersTrait.monomorphPathParams(sp, bound.path.params, false);
                const auto& boundTrait = crate.getTraitByPath(sp, bound.path.path);
                bool replaced = this->findNamedTraitInTrait(sp, e2.trait.path, e2.trait.params, boundTrait, bound.path.path, boundParams, e2.type, [&](const auto& params, const auto& assoc) {
                    auto it = assoc.find(e2.item);
                    if (it != assoc.end()) {
                        input = it->second.type;
                        return true;
                    }
                    return false;
                });
                if (replaced) {
                    if (recurse) {
                        this->expandAssociatedTypes(sp, input);
                    }
                    return true;
                }
            }
            DEBUG("e2 = " << e2.type << ", input = " << input);
        }
    }

    // 2. Crate-level impls

    // - Search for the actual trait containing this associated type
    HIRGenericPath  traitPath;
    if( !this->traitContainsType(sp, e2.trait, this->crate.getTraitByPath(sp, e2.trait.path), e2.item.c_str(), traitPath) )
        BUG(sp, "Cannot find associated type " << e2.item << " anywhere in trait " << e2.trait);

    bool replacementHappened = true;
    ::ImplRef  bestImpl;
    rv = this->findImpl(sp, traitPath.path, traitPath.params, e2.type, [&](ImplRef impl, bool fuzzy) {
        DEBUG("[expand_associated_types] Found " << impl);
        // If a fuzzy match was found, monomorphise and EAT the checked types and try again
        // - A fuzzy can be caused by an opaque match.
        // - TODO: Move this logic into `find_impl`
        if (fuzzy) {
            auto cbIdent = HIRResolvePlaceholdersNop();
            DEBUG("[expand_associated_types] - Fuzzy, monomorph+expand and recheck");

            auto implTy = impl.getImplType(crate.types);
            this->expandAssociatedTypes(sp, implTy);
            DEBUG("impl_ty -> " << implTy);
            if (implTy != e2.type) {
                DEBUG("[expand_associated_types] - Fuzzy - impl type doesn't match: " << implTy << " != " << e2.type);
                return false;
            }
            auto pp = impl.getTraitParams(crate.types);
            for (auto& ty : pp.types) {
                this->expandAssociatedTypes(sp, ty);
            }
            DEBUG("pp -> " << pp);
            if (pp.compareWithPlaceholders(sp, traitPath.params, cbIdent) == HIRCompare::Unequal) {
                DEBUG("[expand_associated_types] - Fuzzy - params don't match: " << pp << " != " << traitPath.params);
                return false;
            }
            DEBUG("[expand_associated_types] - Fuzzy - Actually matches");
        }

        if (impl.typeIsSpecialisable(e2.item.c_str())) {
            if (impl.moreSpecificThan(crate.types, bestImpl)) {
                bestImpl = mv$(impl);
                DEBUG("- Still specialisable");
            }
            return false;
        } else {
            auto nt = impl.getType(crate.types, e2.item.c_str(), e2.params);
            if (nt != HIRTypeRef()) {
                DEBUG("Converted UfcsKnown - " << e.path << " = " << nt);
                if (input == nt) {
                    replacementHappened = false;
                    return true;
                }
                input = mv$(nt);
                replacementHappened = true;
            } else {
                DEBUG("Mark " << e.path << " as opaque");
                e.binding = HIRTypePathBinding::make_Opaque({});
                publish();
                replacementHappened = this->replaceEqualities(input);
            }
            return true;
        }
        });
    if( rv ) {
        if (recurse) {
            this->expandAssociatedTypes(sp, input);
        }
        return replacementHappened;
    }
    if( bestImpl.isValid() ) {
        // A default associated type is the selected projection once the
        // receiver is concrete and no more-specific impl matched. Generic or
        // unresolved receivers must remain opaque until specialization can be
        // decided.
        if (!specializationLookupNeedsResolution(e2.type, traitPath.params)) {
            auto nt = bestImpl.getType(crate.types, e2.item.c_str(), e2.params);
            if (nt != HIRTypeRef()) {
                input = mv$(nt);
                if (recurse) {
                    this->expandAssociatedTypes(sp, input);
                }
                return true;
            }
        }
        e.binding = HIRTypePathBinding::make_Opaque({});
        publish();
        this->replaceEqualities(input);
        DEBUG("- Couldn't find a non-specialised impl of " << traitPath << " for " << e2.type << " - treating as opaque");
        return false;
    }

    if( assumeOpaque ) {
        e.binding = HIRTypePathBinding::make_Opaque({});
        publish();
        DEBUG("Assuming that " << input << " is an opaque name");

        bool rv = this->replaceEqualities(input);
        if (recurse) {
            input = this->expandAssociatedTypesInner(sp, input);
        }
        return rv;
    }

    ERROR(sp, E0000, "Cannot find an implementation of " << traitPath << " for " << e2.type);
}

bool StaticTraitResolve::replaceEqualities(HIRTypeRef& input) const {
    const Span sp;
    TRACE_FUNCTION_F("input=" << input);
    DEBUG("m_type_equalities = {" << typeEqualities << "}");
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
    TRACE_FUNCTION_F(des << desParams << " from " << traitPath << pp);
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

        DEBUG(pt << " => " << ptMono);
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
    TRACE_FUNCTION_FR("name=" << name << ", trait=" << traitPath, outPath);
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

bool StaticTraitResolve::typeIsCopy(const Span& sp, const HIRTypeData* ty) const {
    {
        auto it = copyCache.find(ty);
        if (it != copyCache.end()) {
            return it->second;
        }
    }

    // Bounds in the active parameter environment take precedence over the
    // structural rules below.  This matters for deliberately inconsistent
    // bounds accepted by `trivial_bounds`, such as `where str: Copy`.
    if (traitBounds.size() != 0) {
        auto pp = HIRPathParams();
        if (this->findImplBounds(sp, langCopy(), &pp, ty, [](auto, bool) {
                return true;
            })) {
            copyCache.insert(::std::make_pair(ty, true));
            return true;
        }
    }

    switch ((*ty).tag()) {
        case HIRTypeData::TAG_Generic: {
            copyCache.insert(::std::make_pair(ty, false));
            return false;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = (*ty).as_Path();
            const auto* markings = e.binding.getTraitMarkings();
            if (markings) {
                if (!markings->isCopy) {
                    // Doesn't impl Copy
                    return false;
                } else if (!e.path.data.as_Generic().params.hasParams()) {
                    // No params, must be Copy
                    return true;
                } else {
                    // TODO: Also have a marking that indicates that the type is unconditionally Copy
                }
            }

            auto pp = HIRPathParams();
            bool rv = this->findImpl(sp, langCopy(), &pp, ty, [&](auto, bool) {
                return true;
            }, true);
            copyCache.insert(::std::make_pair(ty, rv));
            return rv;
        }
        case HIRTypeData::TAG_Diverge: {
            // The ! type is kinda Copy ...
            return true;
        }
        case HIRTypeData::TAG_NodeType: {
            auto& e = (*ty).as_NodeType();
            switch (e.tag()) {
                case HIRTypeDataNodeType::TAG_Closure: {
                    auto& nodeP = e.as_Closure();
                    return nodeP->isCopy;
                }
                case HIRTypeDataNodeType::TAG_Generator: {
                    // NOTE: Generators aren't Copy
                    return false;
                }
                case HIRTypeDataNodeType::TAG_Async: {
                    // NOTE: Async blocks aren't Copy? Can they be?
                    return false;
                }
            }
            break;
        }
        case HIRTypeData::TAG_Infer: {
            // Shouldn't be hit
            return false;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = (*ty).as_Borrow();
            // Only shared &-ptrs are copy
            return (e.type == HIRBorrowType::Shared);
        }
        case HIRTypeData::TAG_Pointer: {
            // All raw pointers are Copy
            return true;
        }
        case HIRTypeData::TAG_NamedFunction: {
            // All function pointers are Copy/Clone
            return true;
        }
        case HIRTypeData::TAG_Function: {
            // All function pointers are Copy
            return true;
        }
        case HIRTypeData::TAG_Primitive: {
            auto& e = (*ty).as_Primitive();
            // All primitives (except the unsized `str`) are Copy
            return e != HIRCoreType::Str;
        }
        case HIRTypeData::TAG_Array: {
            auto& e = (*ty).as_Array();
            // TODO: Why is `[T; 0]` treated as `Copy`?
            if ((e.size.is_Known() && (e.size.as_Known() == 0))) {
                return true;
            }
            return typeIsCopy(sp, e.inner);
        }
        case HIRTypeData::TAG_Slice: {
            // [T] isn't Sized, so isn't Copy ether
            return false;
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = (*ty).as_Pattern();
            return typeIsCopy(sp, e.inner);
        }
        case HIRTypeData::TAG_TraitObject: {
            // (Trait) isn't Sized, so isn't Copy ether
            return false;
        }
        case HIRTypeData::TAG_ErasedType: {
            auto& e = (*ty).as_ErasedType();
            for (const auto& trait : e.traits) {
                if (findNamedTraitInTrait(sp, langCopy(), {}, *trait.traitPtr, trait.path.path, trait.path.params, ty, [](const auto&, auto) {
                    return true;
                })) {
                    return true;
                }
            }
            return false;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = (*ty).as_Tuple();
            for (const auto& ty : e) {
                if (!typeIsCopy(sp, ty)) {
                    return false;
                }
            }
            return true;
        }
    }
    UNREACHABLE();
}

bool StaticTraitResolve::typeIsClone(const Span& sp, const HIRTypeData* ty) const {
    switch ((*ty).tag()) {
        case HIRTypeData::TAG_Generic: {
            {
                auto it = cloneCache.find(ty);
                if (it != cloneCache.end()) {
                    return it->second;
                }
            }
            auto pp = HIRPathParams();
            bool rv = this->findImplBounds(sp, langClone(), &pp, ty, [&](auto, bool) {
                return true;
            });
            cloneCache.insert(::std::make_pair(ty, rv));
            return rv;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = (*ty).as_Path();
            if (true) {
                auto it = cloneCache.find(ty);
                if (it != cloneCache.end()) {
                    return it->second;
                }
            }
            if (e.isClosure()) {
                bool rv = true;
                // TODO: Check all captures
                cloneCache.insert(::std::make_pair(ty, rv));
                return rv;
            }
            auto pp = HIRPathParams();
            bool rv = this->findImpl(sp, langClone(), &pp, ty, [&](auto, bool) {
                return true;
            }, true);
            cloneCache.insert(::std::make_pair(ty, rv));
            return rv;
        }
        case HIRTypeData::TAG_Diverge: {
            // The ! type is kinda Copy/Clone ...
            return true;
        }
        case HIRTypeData::TAG_NodeType: {
            auto& e = (*ty).as_NodeType();
            switch (e.tag()) {
                case HIRTypeDataNodeType::TAG_Closure: {
                    auto& nodeP = e.as_Closure();
                    return nodeP->isCopy;
                }
                case HIRTypeDataNodeType::TAG_Generator: {
                    TODO(sp, "type_is_clone - Generator");
                    break;
                }
                case HIRTypeDataNodeType::TAG_Async: {
                    TODO(sp, "type_is_clone - Async");
                    break;
                }
            }
            break;
        }
        case HIRTypeData::TAG_Infer: {
            // Shouldn't be hit
            return false;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = (*ty).as_Borrow();
            // Only shared &-ptrs are copy/clone
            return (e.type == HIRBorrowType::Shared);
        }
        case HIRTypeData::TAG_Pointer: {
            // All raw pointers are Copy/Clone
            return true;
        }
        case HIRTypeData::TAG_NamedFunction: {
            // All function pointers are Copy/Clone
            return true;
        }
        case HIRTypeData::TAG_Function: {
            // All function pointers are Copy/Clone
            return true;
        }
        case HIRTypeData::TAG_Primitive: {
            auto& e = (*ty).as_Primitive();
            // All primitives (except the unsized `str`) are Copy/Clone
            return e != HIRCoreType::Str;
        }
        case HIRTypeData::TAG_Array: {
            auto& e = (*ty).as_Array();
            return (e.size.is_Known() && e.size.as_Known() == 0) || typeIsClone(sp, e.inner);
        }
        case HIRTypeData::TAG_Slice: {
            // [T] isn't Sized, so isn't Copy ether
            return false;
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = (*ty).as_Pattern();
            return typeIsClone(sp, e.inner);
        }
        case HIRTypeData::TAG_TraitObject: {
            // (Trait) isn't Sized, so isn't Copy ether
            return false;
        }
        case HIRTypeData::TAG_ErasedType: {
            auto& e = (*ty).as_ErasedType();
            for (const auto& trait : e.traits) {
                if (findNamedTraitInTrait(sp, langClone(), {}, *trait.traitPtr, trait.path.path, trait.path.params, ty, [](const auto&, auto) {
                    return true;
                })) {
                    return true;
                }
            }
            return false;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = (*ty).as_Tuple();
            for (const auto& ty : e) {
                if (!typeIsClone(sp, ty)) {
                    return false;
                }
            }
            return true;
        }
    }
    UNREACHABLE();
}

bool StaticTraitResolve::typeIsSized(const Span& sp, const HIRTypeData* ty) const {
    switch (this->metadataType(sp, ty)) {
        case MetadataType::None:
            return true;
        default:
            return false;
    }
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
    TRACE_FUNCTION_F(dstTy << " <- " << srcTy);

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
            DEBUG("Found bounded");
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
            DEBUG("Struct unsize? " << dstTy << " <- " << srcTy);
            const auto& str = *dstTy->as_Path().binding.as_Struct();
            const auto& dstGp = dstTy->as_Path().path.data.as_Generic();
            const auto& srcGp = srcTy->as_Path().path.data.as_Generic();

            if (dstGp == srcGp) {
                DEBUG("Can't Unsize, destination and source are identical");
                return false;
            } else if (dstGp.path == srcGp.path) {
                DEBUG("Checking for Unsize " << dstGp << " <- " << srcGp);
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
                DEBUG("Can't Unsize, destination and source are different structs");
                return false;
            }
        }
    }

    // (Trait) <- Foo
    if (const auto* de = dstTy->opt_TraitObject()) {
        // TODO: Check if src_ty is !Sized
        // - Only allowed if the source is a trait object with the same data trait and lesser bounds

        DEBUG("TraitObject unsize? " << dstTy << " <- " << srcTy);

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
                    DEBUG("Not a parent trait");
                    return false;
                }
            } else {
                if (de->trait.path != se->trait.path) {
                    DEBUG("Mismatched data trait params");
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
            findImpl(sp, de->trait.path.path, de->trait.path.params, srcTy, [&](const auto impl, auto fuzz) {
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
                        DEBUG("ATY " << aty.first << " mismatch - " << aty.second << " != " << atyv);
                    }
                }
                return true;
            });
        }

        // Then markers
        auto cb = [&](const auto impl, auto) {
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
            DEBUG("Array unsize? " << de->inner << " <- " << se->inner);
            return se->inner == de->inner || se->inner->equalsIgnoringRegions(de->inner);
        }
    }

    DEBUG("Can't unsize, no rules matched");
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
            auto pp = HIRPathParams();
            bool rv = this->findImplBounds(sp, langSized(), &pp, ty, [&](auto, bool) {
                return true;
            });
            if (rv) {
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
            bool hasDirectDrop = this->findImpl(sp, langDrop(), &pp, ty, [&](auto, bool) {
                return true;
            }, true);
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
    findImpl(sp, trait, HIRPathParams{}, ty, [&](ImplRef impl, bool fuzzed) {
        if (!fuzzed && impl.data.is_TraitImpl()) {
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
        rv = !array->size.is_Known() || array->size.as_Known() != 0
            ? typeNeedsAsyncDropInner(sp, array->inner, stack)
            : false;
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
                            ASSERT_BUG(sp, fieldPath && fieldPath->path.data.is_Generic()
                                && fieldPath->path.data.as_Generic().path == crate.getLangItemPath(sp, "maybe_uninit")
                                && fieldPath->path.data.as_Generic().params.types.size() == 1,
                                "coroutine state is not MaybeUninit<State>: " << fieldTy);
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

StaticTraitResolve::ValuePtr StaticTraitResolve::getValue(const Span& sp, const HIRPath& p, MonomorphState& outParams,
    bool signatureOnly /*=false*/, const HIRGenericParams** outImplParamsDef /*=nullptr*/,
    ResolvedTraitImplPath* outTraitImplPath /*=nullptr*/) const {
    TRACE_FUNCTION_F(p << ", signature_only=" << signatureOnly);
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
                    outParams.ppMethod = &pe.params; return v.as_Constant();
                }
                case HIRValueItem::TAG_Static: {
                    outParams.ppMethod = &pe.params; return v.as_Static();
                }
                case HIRValueItem::TAG_Function: {
                    outParams.ppMethod = &pe.params; return v.as_Function();
                }
                case HIRValueItem::TAG_StructConstant: {
                    outParams.ppImpl = &pe.params; TODO(sp, "StructConstant - " << p);
                    break;
                }
                case HIRValueItem::TAG_StructConstructor: {
                    auto& ve = v.as_StructConstructor();
                    outParams.ppImpl = &pe.params; const auto& str = crate.getStructByPath(sp, ve.ty); if (outImplParamsDef) { *outImplParamsDef = &str.params; } return ValuePtr::Data_StructConstructor{&ve.ty, &str};
                    break;
                }
            }
            UNREACHABLE();
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& pe = p.data.as_UfcsKnown();
            if (pe.trait.path == HIRSimplePath() && pe.item == "vtable#") {
                DEBUG("Empty trait VTable, return NotYetKnown");
                return ValuePtr::make_NotYetKnown({});
            }
            outParams.selfTy = pe.type;
            outParams.ppImpl = &pe.trait.params;
            outParams.ppMethod = &pe.params;
            const HIRTrait& tr = crate.getTraitByPath(sp, pe.trait.path);
            if (!tr.values.count(pe.item)) {
                DEBUG("Value " << pe.item << " not found in trait " << pe.trait.path);
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
                bool hasFuzzyImpl = false;
                ImplRef bestImpl;
                ValuePtr rv;
                bool lookupNeedsResolution = specializationLookupNeedsResolution(pe.type, pe.trait.params);
                auto searchImpls = [&](bool noGoalBridge) {
                this->findImpl(sp, pe.trait.path, &pe.trait.params, pe.type, [&](auto impl, bool isFuzz) -> bool {
                    DEBUG(impl);
                    if (!impl.data.is_TraitImpl()) {
                        hasBoundedImpl = true;
                        return false;
                    }
                    if (isFuzz && lookupNeedsResolution) {
                        // A body or associated constant from a merely possible
                        // impl cannot be selected until the receiver is known.
                        hasFuzzyImpl = true;
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
                        DEBUG("- Missing the target item");
                        return false;
                    } else if (!impl.moreSpecificThan(crate.types, bestImpl)) {
                        // Keep searching
                        DEBUG("- Less specific");
                        return false;
                    } else {
                        DEBUG("- More specific (is_spec=" << isSpec << ")");
                        bestIsSpec = isSpec;
                        bestImpl = mv$(impl);
                        rv = std::move(thisRv);
                        // NOTE: There could be an overlapping and more-specific impl without `default` being involved
                        return false;
                    }
                }, /*dontHandoffToSpecialised=*/false, noGoalBridge);
                };
                searchImpls(/*noGoalBridge=*/false);
                if (!bestImpl.isValid() && this->wb.settings->solver.globally) {
                    // The goal bridge returns one merged response -- the most
                    // specific impl.  When that impl omits the requested item
                    // (`SizeHint for Empty` overrides only upper_bound), the
                    // item is inherited from the impl it shadows: iterate
                    // impls the legacy way to find the providing ancestor.
                    hasBoundedImpl = false;
                    hasFuzzyImpl = false;
                    searchImpls(/*noGoalBridge=*/true);
                }
                if (!bestImpl.isValid()) {
                    if (hasBoundedImpl || hasFuzzyImpl) {
                        DEBUG("Trait item depends on an in-scope bound or fuzzy impl");
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
                                DEBUG("Trait provided value");
                                // NOTE: The parameters have already been set
                                return &ve;
                            } else {
                                DEBUG("Trait did not provide a value");
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
                                DEBUG("Trait provided body");
                                // NOTE: The parameters have already been set
                                return &ve;
                            }
                            // Fall through if there's no provided body
                            break;
                        }
                    }
                    } else {
                        DEBUG("No best impl, but monomorph needed - can't check trait");
                    }
                    return ValuePtr::make_NotYetKnown({});
                }
                if (bestIsSpec) {
                    // If there's generics present in the path, return NotYetKnown
                    if (monomorphiseTypeNeeded(pe.type) || monomorphisePathparamsNeeded(pe.trait.params)) {
                        DEBUG("Specialisable and still generic, return NotYetKnown");
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
                DEBUG("Found impl" << impl.params.fmtArgs() << " " << impl.type);
                // Populate pp_impl if not populated
                if (!pe.implParams.hasParams()) {
                    GetParams::ParamsSet paramsSet;
                    GetParams getParams{sp, impl.params, outParams.ppImplData, paramsSet};

                    auto cbIdent = HIRResolvePlaceholdersNop();
                    impl.type->matchTestGenericsFuzz(sp, pe.type, cbIdent, getParams);

                    const auto& implParams = outParams.ppImplData;

                    outParams.ppImpl = &outParams.ppImplData;
                    DEBUG("PP impl = " << *outParams.ppImpl);
                } else {
                    DEBUG("Pre-existing imp params = " << *outParams.ppImpl);
                }

                if (outImplParamsDef) {
                    *outImplParamsDef = &impl.params;
                }

                // TODO: Specialisation
                {
                    auto fit = impl.methods.find(pe.item);
                    if (fit != impl.methods.end()) {
                        ASSERT_BUG(sp, impl.params.types.size() == outParams.ppImpl->types.size(), "Mismatch in param counts `" << *outParams.ppImpl << "`, params are `" << impl.params.fmtArgs() << "`\n- in " << p);
                        DEBUG("- Contains method, good");
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

StaticTraitResolve::StaticTraitResolve(const WireBoard& wb)
    : TraitResolveCommon(wb)
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
