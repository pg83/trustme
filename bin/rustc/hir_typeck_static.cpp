#include "hir_typeck_static.h"
#include "hir_typeck_helpers.h"
#include "trait_solver_mode.h"
#include <algorithm>
#include <std/mem/obj_pool.h>
#include "hir_expr.h"
#include "hir_conv_main_bindings.h"

namespace {
    const HIR::GenericParams emptyParams;

    struct MatchHrls: public HIR::MatchGenerics, public Monomorphiser {
        ::HIR::PathParams hrls;

        MatchHrls(HIR::TypeInterner& types, const std::unique_ptr<::HIR::GenericParams>& x)
            : MatchHrls(types, x ? *x : emptyParams)
        {
        }

        MatchHrls(HIR::TypeInterner& types, const ::HIR::GenericParams& x)
            : Monomorphiser(types)
            , hrls(x.makeEmptyParams(true))
        {
        }

        virtual ::HIR::Compare matchTy(const ::HIR::GenericRef& g, const ::HIR::TypeData* ty, HIR::t_cb_resolve_type resolveCb) {
            return (ty->is_Generic() && ty->as_Generic().binding == g.binding) ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal;
        }

        virtual ::HIR::Compare matchVal(const ::HIR::GenericRef& g, const ::HIR::ConstGeneric& sz) {
            return sz == g ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal;
        }

        virtual ::HIR::Compare matchLft(const ::HIR::GenericRef& g, const ::HIR::LifetimeRef& lft) {
            if (!::HIR::MatchGenerics::hasHrb() && g.group() == ::HIR::GENERICHrtb) {
                ASSERT_BUG(Span(), g.idx() < hrls.mLifetimes.size(), "HRL index out of range");
                hrls.mLifetimes.at(g.idx()) = lft;
                return ::HIR::Compare::Equal;
            }
            return lft.binding == g.binding ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal;
        }

        // Monomorphiser
        ::HIR::TypeRef getType(const Span& sp, const ::HIR::GenericRef& g) const {
            return types.generic(g.name, g.binding);
        }

        ::HIR::ConstGeneric getValue(const Span& sp, const ::HIR::GenericRef& g) const {
            return g;
        }

        ::HIR::LifetimeRef getLifetime(const Span& sp, const ::HIR::GenericRef& g) const {
            if (g.group() == ::HIR::GENERICHrtb) {
                return hrls.mLifetimes.at(g.idx());
            }
            return ::HIR::LifetimeRef(g.binding);
        }
    };

    HIR::PathParams getHrls(HIR::TypeInterner& types, const Span& sp, const ::HIR::GenericParams& x, const ::HIR::PathParams& trait_pps, const ::HIR::PathParams* desPps) {
        MatchHrls m{types, x};
        if (desPps) {
            trait_pps.matchTestGenericsFuzz(sp, *desPps, HIR::ResolvePlaceholdersNop(), m);
            DEBUG("from " << *desPps);
        }
        DEBUG("hrls = " << m.hrls << " for trait_pps = " << trait_pps);
        return std::move(m.hrls);
    }

    HIR::PathParams getHrls(HIR::TypeInterner& types, const Span& sp, const ::std::unique_ptr<::HIR::GenericParams>& x, const ::HIR::PathParams& trait_pps, const ::HIR::PathParams* desPps) {
        MatchHrls m{types, x};
        if (desPps) {
            trait_pps.matchTestGenericsFuzz(sp, *desPps, HIR::ResolvePlaceholdersNop(), m);
        }
        return std::move(m.hrls);
    }
}

class StaticTraitResolve::NextSolverBridge {
    HMTypeInferrence ivars;
    ::HIR::SimplePath visibility;
    TraitResolution mResolve;

public:
    explicit NextSolverBridge(const ::HIR::Crate& crate)
        : ivars(crate.types)
        , visibility(crate.crateName, {})
        , mResolve(ivars, crate, nullptr, nullptr, visibility, nullptr)
    {
    }

    bool findImpl(
        const Span& sp,
        const ::HIR::GenericParams* impl_generics,
        const ::HIR::GenericParams* item_generics,
        const ::HIR::SimplePath& trait,
        const ::HIR::PathParams* params,
        const ::HIR::TypeData* type,
        StaticTraitResolve::t_cb_find_impl callback
    ) {
        mResolve.set_generic_context(impl_generics, item_generics);

        ::HIR::PathParams inferredParams;
        if (!params) {
            const auto& trait_def = mResolve.crate.getTraitByPath(sp, trait);
            // This resolver owns m_ivars, so its inference indexes must not
            // escape into HIR and be mistaken for indexes in expression typeck.
            const auto placeholderName = RcString::newInterned(
                FMT("static_find_impl_" << &inferredParams)
            );
            inferredParams.mLifetimes = ThinVector<::HIR::LifetimeRef>(
                trait_def.mParams.mLifetimes.size()
            );
            inferredParams.types.reserve(trait_def.mParams.types.size());
            for (size_t i = 0; i < trait_def.mParams.types.size(); i++) {
                inferredParams.types.push_back(mResolve.crate.types.generic(
                    placeholderName, ::HIR::GENERICPlaceholder * 256 + i
                ));
            }
            inferredParams.values.reserve(trait_def.mParams.values.size());
            for (size_t i = 0; i < trait_def.mParams.values.size(); i++) {
                inferredParams.values.push_back(
                    ::HIR::ConstGeneric::make_Generic({
                        placeholderName,
                        static_cast<unsigned int>(
                            ::HIR::GENERICPlaceholder * 256 + i
                        )
                    })
                );
            }
            params = &inferredParams;
        }

        return mResolve.findTraitImplsNext(
            sp,
            trait,
            *params,
            type,
            [&](ImplRef impl, ::HIR::Compare match) {
                return callback(::std::move(impl), match != ::HIR::Compare::Equal);
            },
            ""
        );
    }
};

bool StaticTraitResolve::findImpl(const Span& sp, const ::HIR::SimplePath& trait_path, const ::HIR::PathParams* trait_params, const ::HIR::TypeData* type, t_cb_find_impl foundCb, bool dontHandoffToSpecialised) const {
    TRACE_FUNCTION_F(trait_path << FMT_CB(os, if (trait_params) { os << *trait_params; } else { os << "<?>"; }) << " for " << type);
    auto cbIdent = HIR::ResolvePlaceholdersNop();

    if (gTraitSolverConfig.globally && !dontHandoffToSpecialised) {
        if (!nextSolver) {
            ASSERT_BUG(sp, crate.pool, "next-solver requires the crate object pool");
            nextSolver = crate.pool->make<NextSolverBridge>(crate);
        }
        return nextSolver->findImpl(
            sp,
            implGenerics,
            itemGenerics,
            trait_path,
            trait_params,
            type,
            ::std::move(foundCb)
        );
    }

    static ::HIR::GenericParams nullHrls;
    static ::HIR::PathParams nullParams;
    static ::HIR::TraitPath::assocListT nullAssoc;

    if (!dontHandoffToSpecialised) {
        if (trait_path == mLangCopy) {
            if (this->type_is_copy(sp, type)) {
                return foundCb(ImplRef(HIR::PathParams(), type, &nullParams, &nullAssoc), false);
            }
        } else if (trait_path == mLangClone) {
            // NOTE: Duplicated check for enumerate
            if (type->is_Tuple() || type->is_Array() || type->is_Function() || type->is_NodeType() || type->is_NamedFunction() || TU_TEST1(*type, Path, .isClosure())) {
                if (this->type_is_clone(sp, type)) {
                    return foundCb(ImplRef(HIR::PathParams(), type, &nullParams, &nullAssoc), false);
                }
            }
        } else if (trait_path == mLangSized) {
            if (this->type_is_sized(sp, type)) {
                return foundCb(ImplRef(HIR::PathParams(), type, &nullParams, &nullAssoc), false);
            }
        } else if (trait_path == mLangUnsize) {
            ASSERT_BUG(sp, trait_params, "TODO: Support no params for Unsize");
            const auto& dstTy = trait_params->types.at(0);
            if (this->canUnsize(sp, dstTy, type)) {
                return foundCb(ImplRef(HIR::PathParams(), type, trait_params, &nullAssoc), false);
            }
        } else if (trait_path == mLangDiscriminantKind) {
            // If the type is generic, then don't populate the ATY
            // Otherwise, populate the ATY with the correct type
            // - Unit for non-enums
            // - Enum type (usize probably) for enums
            if (type->is_Generic() || (type->is_Path() && type->as_Path().binding.is_Opaque())) {
                return foundCb(ImplRef(HIR::PathParams(), type, trait_params, &nullAssoc), false);
            } else if (type->is_Path()) {
                if (const auto* enmpp = type->as_Path().binding.opt_Enum()) {
                    const auto& enm = **enmpp;
                    HIR::TypeRef tag_ty = crate.types.primitive(enm.getReprType(enm.tagRepr));
                    ::HIR::TraitPath::assocListT assocList;
                    assocList.insert(std::make_pair(RcString::newInterned("Discriminant"), HIR::TraitPath::AtyEqual{mLangDiscriminantKind, {}, std::move(tag_ty)}));
                    return foundCb(ImplRef(type, {}, std::move(assocList)), false);
                } else {
                }
            } else {
            }
            static ::HIR::TraitPath::assocListT assocU8;
            if (assocU8.empty()) {
                assocU8.insert(std::make_pair(RcString::newInterned("Discriminant"), HIR::TraitPath::AtyEqual{mLangDiscriminantKind, {}, crate.types.primitive(HIR::CoreType::U8)}));
            }
            return foundCb(ImplRef(HIR::PathParams(), type, trait_params, &assocU8), false);
        } else if (trait_path == mLangPointee) {
            static ::HIR::TraitPath::assocListT assocUnit;
            static ::HIR::TraitPath::assocListT assocSlice;
            static RcString nameMetadata;
            if (assocUnit.empty()) {
                nameMetadata = RcString::newInterned("Metadata");
                assocUnit.insert(std::make_pair(nameMetadata, HIR::TraitPath::AtyEqual{mLangPointee, {}, crate.types.unit()}));
                assocSlice.insert(std::make_pair(nameMetadata, HIR::TraitPath::AtyEqual{mLangPointee, {}, crate.types.primitive(HIR::CoreType::Usize)}));
            }

            // Generics (or opaque ATYs)
            if (type->is_Generic() || (type->is_Path() && type->as_Path().binding.is_Opaque())) {
                // If the type is `Sized` return `()` as the type
                if (type_is_sized(sp, type)) {
                    return foundCb(ImplRef(HIR::PathParams(), type, trait_params, &assocUnit), false);
                } else {
                    // Return unbounded
                    return foundCb(ImplRef(HIR::PathParams(), type, trait_params, &nullAssoc), false);
                }
            }
            // Trait object: `Metadata=DynMetadata<T>`
            else if (type->is_TraitObject()) {
                ::HIR::TraitPath::assocListT assocList;
                assocList.insert(std::make_pair(nameMetadata, HIR::TraitPath::AtyEqual{mLangPointee, {}, crate.types.path(::HIR::GenericPath(mLangDynMetadata, HIR::PathParams(type)), &crate.getStructByPath(sp, mLangDynMetadata))}));
                return foundCb(ImplRef(type, {}, std::move(assocList)), false);
            }
            // Slice and str
            else if (type->is_Slice() || TU_TEST1(*type, Primitive, == HIR::CoreType::Str)) {
                return foundCb(ImplRef(HIR::PathParams(), type, trait_params, &assocSlice), false);
            }
            // Structs: Can delegate their metadata
            else if (type->is_Path() && type->as_Path().binding.is_Struct()) {
                const auto& str = *type->as_Path().binding.as_Struct();
                switch (str.structMarkings.dst_type) {
                    case HIR::StructMarkings::DstType::None:
                        return foundCb(ImplRef(HIR::PathParams(), type, trait_params, &assocUnit), false);
                    case HIR::StructMarkings::DstType::Possible:
                    case HIR::StructMarkings::DstType::TraitObject: {
                        const ::HIR::TypeData* tail_tpl = nullptr;
                        TU_MATCHA((str.mData), (se),
                            (Unit, BUG(sp, "Unsized unit struct in Pointee lookup - " << type);),
                            (Tuple, ASSERT_BUG(sp, !se.empty(), "Unsized tuple struct without fields - " << type); tail_tpl = se.back().ent;),
                            (Named, ASSERT_BUG(sp, !se.empty(), "Unsized struct without fields - " << type); tail_tpl = se.back().ty;)
                        )
                        ASSERT_BUG(sp, tail_tpl, "Missing unsized tail field for " << type);

                        const auto& path = type->as_Path().path.mData.as_Generic();
                        auto tail_ty = MonomorphStatePtr(crate.types, type, &path.mParams, nullptr).monomorphType(sp, tail_tpl);
                        this->expandAssociatedTypes(sp, tail_ty);

                        return findImpl(sp, trait_path, trait_params, tail_ty, [&](ImplRef impl, bool unk) {
                            ::HIR::TraitPath::assocListT assoc;
                            auto metadataTy = impl.getType(crate.types, "Metadata", {});
                            if (metadataTy) {
                                assoc.insert(std::make_pair(nameMetadata, HIR::TraitPath::AtyEqual{trait_path, {}, std::move(metadataTy)}));
                            }
                            return foundCb(ImplRef(type, trait_params ? trait_params->clone() : HIR::PathParams(), std::move(assoc)), unk);
                        });
                    }
                    case HIR::StructMarkings::DstType::Slice:
                        return foundCb(ImplRef(type, trait_params, &assocSlice), false);
                }
            }
            return foundCb(ImplRef(type, trait_params, &assocUnit), false);
        } else if (trait_path == mLangPointeeSized) {
            // Lowest level of sizedness: This _might_ be sized (i.e. it's not an extern type?)
            return foundCb(ImplRef(type, &nullParams, &nullAssoc), false);
            //switch( this->metadata_type(sp, type) )
            //{
            //case MetadataType::Unknown:
            //    break;
            //case MetadataType::None:
            //case MetadataType::Slice:
            //case MetadataType::TraitObject:
            //case MetadataType::Zero:
            //    return found_cb( ImplRef(&null_hrls, type, &null_params, &null_assoc), false );
            //}
        } else if (trait_path == mLangMetaSized) {
            // Next level of sizedness: There's metadata that allows getting the size
            // - No difference to the above?
            switch (this->metadataType(sp, type)) {
                case MetadataType::Unknown:
                    break;
                case MetadataType::None:
                case MetadataType::Slice:
                case MetadataType::TraitObject:
                case MetadataType::Zero: // TODO: Does zero apply here?
                    return foundCb(ImplRef(type, &nullParams, &nullAssoc), false);
            }
        } else if (trait_path == mLangDestruct) {
            // is there anything indestructible? Maybe extern types
            return foundCb(ImplRef(type, &nullParams, &nullAssoc), false);
        }
    }

    // Special case: Generic placeholder
    if (const auto* e = type->opt_Generic()) {
        if (e->group() == HIR::GENERICPlaceholder) {
            // TODO: If the type is a magic placeholder, assume it impls the specified trait.
            // TODO: Restructure so this knows that the placehlder impls the impl-provided bounds.
            return foundCb(ImplRef(type, trait_params, &nullAssoc), false);
        }
    }

    struct H {
        static const HIR::TypeData* getRootTy(const HIR::TypeData* t) {
            if (const auto* e = t->opt_Path()) {
                TU_MATCH_HDRA( (e->path.mData), {)
                TU_ARMA(Generic, ee) {
                    }
                    TU_ARMA(UfcsKnown, ee) return getRootTy(ee.type);
                    TU_ARMA(UfcsUnknown, ee) return getRootTy(ee.type);
                    TU_ARMA(UfcsInherent, ee) return getRootTy(ee.type);
                }
            }
            return t;
        }

        static bool checkParams(const Span& sp, const HIR::PathParams& target_params, const HIR::PathParams* trait_params) {
            if (!trait_params) {
                return true;
            }

            return target_params.compareWithPlaceholders(sp, *trait_params, HIR::ResolvePlaceholdersNop()) != HIR::Compare::Unequal;
        }
    };

    if (type != HIR::TypeRef() && H::getRootTy(type) == HIR::TypeRef()) {
        return foundCb(ImplRef(HIR::PathParams(), type, trait_params, &nullAssoc), false);
    }

    // --- MAGIC IMPLS ---
    // TODO: There should be quite a few more here, but laziness
    TU_MATCH_HDRA( (*type), {)
    default:
        // Nothing magic
    TU_ARMA(Tuple, e) {
            if (trait_path == crate.getLangItemPath(sp, "tuple_trait")) {
                return foundCb(ImplRef(type, HIR::PathParams(), ::HIR::TraitPath::assocListT()), false);
            }
        }
        TU_ARMA(Function, e) {
            if (trait_path == mLangFn || trait_path == mLangFnMut || trait_path == mLangFnOnce) {
                if (trait_params) {
                    const auto& desArgTys = trait_params->types.at(0)->as_Tuple();
                    if (desArgTys.size() != e.argTypes.size()) {
                        return false;
                    }
                    for (unsigned int i = 0; i < desArgTys.size(); i++) {
                        if (desArgTys[i]->compareWithPlaceholders(sp, e.argTypes[i], cbIdent) == ::HIR::Compare::Unequal) {
                            return false;
                        }
                    }
                }
                std::vector<HIR::TypeRef> arg_types;
                for (unsigned int i = 0; i < e.argTypes.size(); i++) {
                    arg_types.push_back(e.argTypes[i]);
                }
                HIR::PathParams params;
                params.types.push_back(crate.types.tuple(std::move(arg_types)));
                ::HIR::TraitPath::assocListT assoc;
                assoc.insert(::std::make_pair("Output", ::HIR::TraitPath::AtyEqual{::HIR::GenericPath(mLangFnOnce, params.clone()), {}, e.mRettype}));
                auto hrls = getHrls(crate.types, sp, e.hrls, params, trait_params);
                return foundCb(ImplRef(std::move(hrls), type, mv$(params), mv$(assoc)), false);
            }
            // 1.74: Magic impls of `eq` for function pointers
            if (trait_path == this->crate.getLangItemPathOpt("fn_ptr_trait")) {
                return foundCb(ImplRef(type, {}, {}), false);
            }
        }
        TU_ARMA(NamedFunction, realE) {
            if (trait_path == mLangFn || trait_path == mLangFnMut || trait_path == mLangFnOnce) {
                auto e = realE.decay(crate.types, sp);
                if (trait_params) {
                    const auto& desArgTys = trait_params->types.at(0)->as_Tuple();
                    if (desArgTys.size() != e.argTypes.size()) {
                        return false;
                    }
                    for (unsigned int i = 0; i < desArgTys.size(); i++) {
                        if (desArgTys[i]->compareWithPlaceholders(sp, e.argTypes[i], cbIdent) == ::HIR::Compare::Unequal) {
                            return false;
                        }
                    }
                }
                std::vector<HIR::TypeRef> arg_types;
                for (unsigned int i = 0; i < e.argTypes.size(); i++) {
                    arg_types.push_back(e.argTypes[i]);
                }
                HIR::PathParams params;
                params.types.push_back(crate.types.tuple(std::move(arg_types)));
                ::HIR::TraitPath::assocListT assoc;
                assoc.insert(::std::make_pair("Output", ::HIR::TraitPath::AtyEqual{::HIR::GenericPath(mLangFnOnce, params.clone()), {}, e.mRettype}));
                auto hrls = getHrls(crate.types, sp, e.hrls, params, trait_params);
                return foundCb(ImplRef(std::move(hrls), type, mv$(params), mv$(assoc)), false);
            }
        }
        TU_ARMA(NodeType, e) {
        TU_MATCH_HDRA((e), {)
        TU_ARMA(Closure, nodeP) {
                    if (trait_path == mLangFn || trait_path == mLangFnMut || trait_path == mLangFnOnce) {
                        if (trait_params) {
                            const auto& desArgTys = trait_params->types.at(0)->as_Tuple();
                            if (desArgTys.size() != nodeP->mArgs.size()) {
                                return false;
                            }
                            for (unsigned int i = 0; i < desArgTys.size(); i++) {
                                if (desArgTys[i]->compareWithPlaceholders(sp, nodeP->mArgs[i].second, HIR::ResolvePlaceholdersNop()) == ::HIR::Compare::Unequal) {
                                    return false;
                                }
                            }
                        } else {
                            trait_params = &nullParams;
                        }
                        switch (nodeP->cls) {
                            case ::HIR::ExprNodeClosure::Class::Unknown:
                                break;
                            case ::HIR::ExprNodeClosure::Class::NoCapture:
                                break;
                            case ::HIR::ExprNodeClosure::Class::Once:
                                if (trait_path == mLangFnMut) {
                                    return false;
                                }
                            case ::HIR::ExprNodeClosure::Class::Mut:
                                if (trait_path == mLangFn) {
                                    return false;
                                }
                            case ::HIR::ExprNodeClosure::Class::Shared:
                                break;
                        }
                        ::HIR::TraitPath::assocListT assoc;
                        assoc.insert(::std::make_pair("Output", ::HIR::TraitPath::AtyEqual{::HIR::GenericPath(mLangFnOnce, trait_params->clone()), {}, nodeP->returnType}));
                        return foundCb(ImplRef(type, trait_params->clone(), mv$(assoc)), false);
                    }
                }
                TU_ARMA(Generator, nodeP) {
                    if (trait_path == mLangGenerator) {
                        ::HIR::TraitPath::assocListT assoc;
                        assoc.insert(::std::make_pair("Yield", ::HIR::TraitPath::AtyEqual{trait_path.clone(), {}, nodeP->yieldTy}));
                        assoc.insert(::std::make_pair("Return", ::HIR::TraitPath::AtyEqual{trait_path.clone(), {}, nodeP->returnType}));
                        HIR::PathParams params;
                        params.types.push_back(nodeP->resumeTy);
                        return foundCb(ImplRef(type, mv$(params), mv$(assoc)), ::HIR::Compare::Equal);
                    }
                }
                TU_ARMA(Async, nodeP) {
                    if (trait_path == mLangFuture) {
                        ::HIR::TraitPath::assocListT assoc;
                        assoc.insert(::std::make_pair("Output", ::HIR::TraitPath::AtyEqual{trait_path.clone(), {}, nodeP->mCode->resType}));
                        ::HIR::PathParams params;
                        return foundCb(ImplRef(type, mv$(params), mv$(assoc)), ::HIR::Compare::Equal);
                    }
                }
        }
        }
        // ----
        // TraitObject traits and supertraits
        // ----
        TU_ARMA(TraitObject, e) {
            if (trait_path == e.mTrait.mPath.mPath) {
                if (H::checkParams(sp, e.mTrait.mPath.mParams, trait_params)) {
                    auto hrls = getHrls(crate.types, sp, e.mTrait.hrtbs, e.mTrait.mPath.mParams, trait_params);
                    return foundCb(ImplRef(std::move(hrls), type, &e.mTrait.mPath.mParams, &e.mTrait.typeBounds, e.mTrait.constness), false);
                }
            }
            // Markers too
            for (const auto& mt : e.markers) {
                if (trait_path == mt.mPath) {
                    if (H::checkParams(sp, mt.mParams, trait_params)) {
                        return foundCb(ImplRef(type, &mt.mParams, &nullAssoc), false);
                    }
                }
            }

            // - Check if the desired trait is a supertrait of this.
            // TODO: What if `trait_params` is nullptr?
            bool rv = false;
            bool isSupertrait = trait_params && e.mTrait.traitPtr && this->findNamedTraitInTrait(sp, trait_path, *trait_params, *e.mTrait.traitPtr, e.mTrait.mPath.mPath, e.mTrait.mPath.mParams, type, [&](const HIR::PathParams& iParams, ::HIR::TraitPath::assocListT iAssoc) -> bool {
                // Match the input trait params and the output trait params, to resolve HRLs
                MatchHrls matchHrls{crate.types, e.mTrait.hrtbs ? *e.mTrait.hrtbs : emptyParams};
                iParams.matchTestGenericsFuzz(sp, *trait_params, HIR::ResolvePlaceholdersNop(), matchHrls);

                // Invoke callback with a proper ImplRef
                ::HIR::TraitPath::assocListT assocClone;
                for (const auto& e : iAssoc) {
                    assocClone.insert(::std::make_pair(e.first, matchHrls.monomorphTpAtyEqual(sp, e.second, true)));
                }
                // HACK! Just add all the associated type bounds (only inserted if not already present)
                for (const auto& e2 : e.mTrait.typeBounds) {
                    assocClone.insert(::std::make_pair(e2.first, matchHrls.monomorphTpAtyEqual(sp, e2.second, true)));
                }

                ImplRef ir{
                    HIR::PathParams(), // HRLs already handled
                    type,
                    matchHrls.monomorphPathParams(sp, iParams, true),
                    std::move(assocClone)
                };
                DEBUG("[TraitObject] - ir = " << ir);
                rv = foundCb(mv$(ir), false);
                return true;
            });
            if (isSupertrait) {
                return rv;
            }
        }
        // Same for ErasedType
        TU_ARMA(ErasedType, e) {
            for (const auto& trait : e.traits) {
                bool rv = false;
                // TODO: If `trait_params` is nullptr, this doesn't run (is that sane?)
                bool isSupertrait = trait_params && this->findNamedTraitInTrait(sp, trait_path, *trait_params, *trait.traitPtr, trait.mPath.mPath, trait.mPath.mParams, type, [&](const auto& iParams, const auto& iAssoc) {
                    // Invoke callback with a proper ImplRef
                    ::HIR::TraitPath::assocListT assocClone;
                    for (const auto& assocE : iAssoc) {
                        assocClone.insert(::std::make_pair(assocE.first, assocE.second.clone()));
                    }
                    // HACK! Just add all the associated type bounds (only inserted if not already present)
                    for (const auto& e2 : trait.typeBounds) {
                        assocClone.insert(::std::make_pair(e2.first, e2.second.clone()));
                    }
                    auto hrls = getHrls(crate.types, sp, trait.hrtbs, iParams, trait_params);
                    auto ir = ImplRef(std::move(hrls), type, iParams.clone(), mv$(assocClone));
                    DEBUG("[ErasedType] - ir = " << ir);
                    rv = foundCb(mv$(ir), false);
                    return true;
                });
                if (isSupertrait) {
                    return rv;
                }
            }
        }

        // ---
        // If this type is an opaque UfcsKnown - check bounds
        // ---
        TU_ARMA(Path, e) {
            if (e.binding.is_Opaque()) {
                ASSERT_BUG(sp, e.path.mData.is_UfcsKnown(), "Opaque bound type wasn't UfcsKnown - " << type);
                const auto& pe = e.path.mData.as_UfcsKnown();
                DEBUG("Checking bounds on definition of " << pe.item << " in " << pe.trait);

                // If this associated type has a bound of the desired trait, return it.
                const auto& trait_ref = crate.getTraitByPath(sp, pe.trait.mPath);
                ASSERT_BUG(sp, trait_ref.types.count(pe.item) != 0, "Trait " << pe.trait.mPath << " doesn't contain an associated type " << pe.item);
                const auto& atyDef = trait_ref.types.find(pe.item)->second;

                auto monomorphCb = MonomorphStatePtr(crate.types, pe.type, &pe.trait.mParams, &pe.params);

                auto checkBound = [&](const ::HIR::TraitPath& bound) {
                    const auto& bParams = bound.mPath.mParams;
                    ::HIR::PathParams paramsMonoO;
                    const auto& bParamsMono = monomorphisePathparamsWithOpt(sp, paramsMonoO, bParams, monomorphCb, false);
                    this->expandAssociatedTypesParams(sp, paramsMonoO);
                    DEBUG("[find_impl] ATY : " << bound.mPath.mPath << bParamsMono);

                    if (bound.mPath.mPath == trait_path) {
                        if (H::checkParams(sp, bParamsMono, trait_params)) {
                            auto hrls = getHrls(crate.types, sp, bound.hrtbs, bParamsMono, trait_params);
                            // Optimisation: If this was a monomorphised path, then move ownership into the ImplRef
                            if (&bParamsMono == &paramsMonoO || ::std::any_of(bound.typeBounds.begin(), bound.typeBounds.end(), [&](const auto& x) {
                                return monomorphiseTypeNeeded(x.second.type);
                            })) {
                                ::HIR::TraitPath::assocListT atys;
                                if (!bound.typeBounds.empty()) {
                                    for (const auto& tb : bound.typeBounds) {
                                        auto src = monomorphCb.monomorphGenericpath(sp, tb.second.source_trait, false);
                                        auto aty = monomorphCb.monomorphType(sp, tb.second.type, false);
                                        expandAssociatedTypes(sp, aty);
                                        expandAssociatedTypesParams(sp, src.mParams);
                                        atys.insert(::std::make_pair(tb.first, ::HIR::TraitPath::AtyEqual{mv$(src), {}, mv$(aty)}));
                                    }
                                }
                                if (foundCb(ImplRef(std::move(hrls), type, mv$(paramsMonoO), mv$(atys), bound.constness), false)) {
                                    return true;
                                }
                                paramsMonoO = monomorphCb.monomorphPathParams(sp, bParams, false);
                            } else {
                                if (foundCb(ImplRef(std::move(hrls), type, &bound.mPath.mParams, &bound.typeBounds, bound.constness), false)) {
                                    return true;
                                }
                            }
                        }
                    }

                    if (trait_params) {
                        return this->findNamedTraitInTrait(sp, trait_path, *trait_params, *bound.traitPtr, bound.mPath.mPath, bParamsMono, type, [&](const auto& iParams, const auto& iAssoc) {
                            if (iParams != *trait_params) {
                                return false;
                            }
                            DEBUG("impl " << trait_path << iParams << " for " << type << " -- desired " << trait_path << *trait_params);
                            return foundCb(ImplRef(type, iParams.clone(), {}, bound.constness), false);
                        });
                    } else {
                        auto monomorph = MonomorphStatePtr(crate.types, type, &bParamsMono, nullptr);

                        for (const auto& pt : bound.traitPtr->allParentTraits) {
                            auto ptMono = monomorph.monomorphTraitpath(sp, pt, false);

                            //DEBUG(pt << " => " << pt_mono);
                            // TODO: When in pre-typecheck mode, this needs to be a fuzzy match (because there might be a UfcsUnknown in the
                            // monomorphed version) OR, there may be placeholders
                            if (pt.mPath.mPath == trait_path) {
                                // TODO: Monomorphse trait params
                                //DEBUG("impl " << trait_path << i_params << " for " << type << " -- desired " << trait_path << *trait_params);
                                return foundCb(ImplRef(type, mv$(ptMono.mPath.mParams), {}, ptMono.constness), false);
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
                for (const auto& bound : trait_ref.mParams.bounds) {
                    if (!bound.is_TraitBound()) {
                        continue;
                    }
                    const auto& be = bound.as_TraitBound();

                    DEBUG("be.type = " << be.type);
                    if (!be.type->is_Path()) {
                        continue;
                    }
                    if (!be.type->as_Path().path.mData.is_UfcsKnown()) {
                        continue;
                    }
                    {
                        const auto& pe2 = be.type->as_Path().path.mData.as_UfcsKnown();
                        if (pe2.type != crate.types.self()) {
                            continue;
                        }
                        if (pe2.trait.mPath != pe.trait.mPath) {
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
                    ::std::vector<const HIR::Path::Data::Data_UfcsKnown*> stack;
                    stack.push_back(&pe);
                    const auto* ity = &pe.type;
                    while (const auto* inner = (*ity)->opt_Path()) {
                        if (const auto* ufcs = inner->path.mData.opt_UfcsKnown()) {
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
                            const HIR::TraitPath* tp = nullptr;
                            for (const auto& t : *traits) {
                                if (t.mPath == pe->trait) {
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
        }
        // --- /UfcsKnown ---
    }

    bool ret;

    if( crate.getTraitByPath(sp, trait_path).isMarker )
    {
        struct H {
            static bool findImplAutoTraitCheck(const StaticTraitResolve& self, const Span& sp, const ::HIR::SimplePath& trait_path, const ::HIR::PathParams* trait_params, const ::HIR::TypeData* type, t_cb_find_impl foundCb, const ::HIR::MarkerImpl& impl, bool& outRv) {
                DEBUG("- Auto " << (impl.isPositive ? "Pos" : "Neg") << " impl" << impl.mParams.fmtArgs() << " " << trait_path << impl.traitArgs << " for " << impl.mType << " " << impl.mParams.fmtBounds());
                if (impl.isPositive) {
                    return self.findImplCheckCrateRaw(sp, trait_path, trait_params, type, impl.mParams, impl.traitArgs, impl.mType, [&](auto impl_params, auto cmp) -> bool {
                        //rv = found_cb( ImplRef(impl_params, trait_path, impl, mv$(placeholders)), (cmp == ::HIR::Compare::Fuzzy) );
                        outRv = foundCb(ImplRef(type, trait_params, &nullAssoc), cmp == ::HIR::Compare::Fuzzy);
                        return outRv;
                    });
                } else {
                    return self.findImplCheckCrateRaw(sp, trait_path, trait_params, type, impl.mParams, impl.traitArgs, impl.mType, [&](auto impl_params, auto cmp) -> bool {
                        outRv = false;
                        return true;
                    });
                }
            }
        };

        // Positive/negative impls
        bool rv = false;
        ret = this->crate.findAutoTraitImpls(trait_path, type, cbIdent, [&](const auto& impl) -> bool {
            return H::findImplAutoTraitCheck(*this, sp, trait_path, trait_params, type, foundCb, impl, rv);
        });
        if (ret)
            return rv;

        // Legacy static lookup is recursive too.  Keep its active goals on
        // this resolver instance instead of in process-global state.
        for (const auto& ent : findImplStack) {
            if (*::std::get<0>(ent) != trait_path)
                continue;
            if (::std::get<1>(ent) && trait_params
                && *::std::get<1>(ent) != *trait_params)
                continue;
            if (::std::get<2>(ent) != type)
                continue;

            return foundCb(ImplRef(type, trait_params, &nullAssoc), false);
        }
        findImplStack.push_back(
            ::std::make_tuple(&trait_path, trait_params, type)
        );
        struct FindImplStackGuard {
            decltype(findImplStack)& stack;
            ~FindImplStackGuard() { stack.pop_back(); }
        } stack_guard{findImplStack};

        auto cmp = this->checkAutoTraitImplDestructure(sp, trait_path, trait_params, type);
        if (cmp != ::HIR::Compare::Unequal)
            return foundCb(ImplRef(type, trait_params, &nullAssoc), cmp == ::HIR::Compare::Fuzzy);
    }
    else
    {
        // Search the crate for impls
        DEBUG("Search for " << trait_path << " for " << type);
        ret = crate.findTraitImpls(trait_path, type, cbIdent, [&](const auto& impl) {
            return this->findImplCheckCrate(sp, trait_path, trait_params, type, foundCb, impl);
        });
        if (ret)
            return true;
    }


    // TODO: A bound can imply something via its associated types. How deep can this go?
    // E.g. `T: IntoIterator<Item=&u8>` implies `<T as IntoIterator>::IntoIter : Iterator<Item=&u8>`
    if( this->findImplBounds(sp, trait_path, trait_params, type, foundCb) )
    {
        DEBUG("Success");
        return true;
    }

    if( type->is_Path() )
    {
    }

    return false;
}

bool StaticTraitResolve::findImplBounds(const Span& sp, const ::HIR::SimplePath& trait_path, const ::HIR::PathParams* trait_params, const ::HIR::TypeData* type, t_cb_find_impl foundCb) const {
    struct H {
        static bool comparePp(const Span& sp, const ::HIR::PathParams& left, const ::HIR::PathParams& right) {
            ASSERT_BUG(sp, left.types.size() == right.types.size(), "Parameter count mismatch between " << left << " and " << right);
            for (unsigned int i = 0; i < left.types.size(); i++) {
                // TODO: Permits fuzzy comparison to handle placeholder params, should instead do a match/test/assign
                if (left.types[i]->compareWithPlaceholders(sp, right.types[i], HIR::ResolvePlaceholdersNop()) == ::HIR::Compare::Unequal) {
                    //if( left.m_types[i] != right.m_types[i] ) {
                    return false;
                }
            }
            return true;
        }
    };

    const bool type_has_infer = visit_ty_with(type, [&](const HIR::TypeData* t) -> bool {
        return t->is_Infer();
    });
    for (auto it = traitBounds.begin(); it != traitBounds.end(); ++it) {
        if (it->first.second.mPath != trait_path) {
            continue;
        }
        const auto& bType = it->first.first;
        const auto& bParams = it->first.second.mParams;

        if (type_has_infer) {
            DEBUG("ivar present: type ?= " << bType);
            if (bType->compareWithPlaceholders(sp, type, HIR::ResolvePlaceholdersNop()) == ::HIR::Compare::Unequal) {
                continue;
            }
        } else if (bType != type && !bType->equalsIgnoringRegions(type)) {
            continue;
        }
        DEBUG(bType << ": " << "for" << it->second.hrbs.fmtArgs() << " " << trait_path << bParams);
        // Check against `params`
        if (trait_params) {
            if (!H::comparePp(sp, *trait_params, bParams)) {
                continue;
            }
        }
        // Hand off to the closure, and return true if it does
        auto hrls = getHrls(crate.types, sp, it->second.hrbs, bParams, trait_params);
        if (foundCb(ImplRef(std::move(hrls), bType, &bParams, &it->second.assoc, it->second.constness), false)) {
            return true;
        }
    }

    // Obtain a pointer to UfcsKnown for magic later
    const ::HIR::Path::Data::Data_UfcsKnown* assocInfo = nullptr;
    if (const auto* e = type->opt_Path()) {
        assocInfo = e->path.mData.opt_UfcsKnown();
    }
    if (assocInfo) {
        for (auto it = traitBounds.begin(); it != traitBounds.end(); ++it) {
            if (it->first.second.mPath != assocInfo->trait.mPath
                || (it->first.first != assocInfo->type && !it->first.first->equalsIgnoringRegions(assocInfo->type))) {
                continue;
            }
            const auto& bound = *it;
            const auto& bParams = it->first.second.mParams;

            if (H::comparePp(sp, bParams, assocInfo->trait.mParams)) {
                const auto& trait_ref = *bound.second.trait_ptr;
                const auto& at = trait_ref.types.at(assocInfo->item);
                for (const auto& bound : at.traitBounds) {
                    if (bound.mPath.mPath == trait_path && (!trait_params || H::comparePp(sp, bound.mPath.mParams, *trait_params))) {
                        DEBUG("- Found an associated type impl");

                        auto tp_mono = MonomorphStatePtr(crate.types, assocInfo->type, &assocInfo->trait.mParams, &assocInfo->params).monomorphTraitpath(sp, bound, false);
                        // - Expand associated types
                        for (auto& ty : tp_mono.typeBounds) {
                            this->expandAssociatedTypes(sp, ty.second.type);
                        }
                        DEBUG("- tp_mono = " << tp_mono);
                        // TODO: Instead of using `type` here, build the real type
                        auto hrls = getHrls(crate.types, sp, bound.hrtbs, tp_mono.mPath.mParams, trait_params);
                        if (foundCb(ImplRef(std::move(hrls), type, mv$(tp_mono.mPath.mParams), mv$(tp_mono.typeBounds), tp_mono.constness), false)) {
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

    class GetParams: public ::HIR::MatchGenerics {
    public:
        struct ParamsSet {
            std::vector<bool> types;
            std::vector<bool> mLifetimes;
            std::vector<bool> values;
        };

    private:
        Span sp;
        HIR::PathParams& impl_params;
        ParamsSet& paramsSet;

    public:
        GetParams(Span sp, const HIR::GenericParams& implParamsDef, HIR::PathParams& impl_params, ParamsSet& paramsSet)
            : sp(sp)
            , impl_params(impl_params)
            , paramsSet(paramsSet)
        {
            impl_params.mLifetimes.resize(implParamsDef.mLifetimes.size());
            impl_params.types.resize(implParamsDef.types.size());
            impl_params.values.resize(implParamsDef.values.size());
            paramsSet.mLifetimes.resize(implParamsDef.mLifetimes.size());
            paramsSet.types.resize(implParamsDef.types.size());
            paramsSet.values.resize(implParamsDef.values.size());
        }

        ::HIR::Compare matchTy(const ::HIR::GenericRef& g, const ::HIR::TypeData* ty, ::HIR::t_cb_resolve_type resolveCb) override {
            ASSERT_BUG(sp, g.binding < impl_params.types.size(), "[GetParams] Type generic " << g << " out of bounds (" << impl_params.types.size() << ")");
            if (!paramsSet.types[g.binding]) {
                paramsSet.types[g.binding] = true;
                impl_params.types[g.binding] = ty;
                DEBUG("[GetParams] Set impl ty param " << g << " to " << ty);
                return ::HIR::Compare::Equal;
            } else {
                return impl_params.types[g.binding]->compareWithPlaceholders(sp, ty, resolveCb);
            }
        }

        ::HIR::Compare matchVal(const ::HIR::GenericRef& g, const ::HIR::ConstGeneric& sz) override {
            ASSERT_BUG(sp, g.binding < impl_params.values.size(), "[GetParams] Value generic " << g << " out of range (" << impl_params.values.size() << ")");
            if (!paramsSet.values[g.binding]) {
                paramsSet.values[g.binding] = true;
                impl_params.values[g.binding] = sz.clone();
                DEBUG("[GetParams] Set impl val param " << g << " to " << sz);
                return ::HIR::Compare::Equal;
            } else {
                if (impl_params.values[g.binding] != sz) {
                    return HIR::Compare::Unequal;
                } else {
                    return HIR::Compare::Equal;
                }
            }
        }

        ::HIR::Compare matchLft(const ::HIR::GenericRef& g, const ::HIR::LifetimeRef& lft) override {
            if (g.binding >= 2 * 256) {
                return HIR::Compare::Equal;
            }
            ASSERT_BUG(sp, g.binding < impl_params.mLifetimes.size(), "[GetParams] Lifetime generic " << g << " out of range (" << impl_params.mLifetimes.size() << ")");
            if (!paramsSet.mLifetimes[g.binding]) {
                paramsSet.mLifetimes[g.binding] = true;
                impl_params.mLifetimes[g.binding] = lft;
                DEBUG("[GetParams] Set impl lifetime param " << g << " to " << lft);
                return ::HIR::Compare::Equal;
            } else {
                DEBUG("[GetParams] Compare  " << g << " (" << impl_params.mLifetimes[g.binding] << ") with " << lft);
                if (impl_params.mLifetimes[g.binding] != lft) {
                    return HIR::Compare::Unequal;
                } else {
                    return HIR::Compare::Equal;
                }
            }
        }
    };
}

bool StaticTraitResolve::findImplCheckCrateRaw(const Span& sp, const ::HIR::SimplePath& desTraitPath, const ::HIR::PathParams* desTraitParams, const ::HIR::TypeData* desType, const ::HIR::GenericParams& implParamsDef, const ::HIR::PathParams& implTraitParams, const ::HIR::TypeData* impl_type, ::std::function<bool(HIR::PathParams, ::HIR::Compare)> foundCb) const {
    auto cbIdent = HIR::ResolvePlaceholdersNop();
    TRACE_FUNCTION_F("impl" << implParamsDef.fmtArgs() << " " << desTraitPath << implTraitParams << " for " << impl_type << implParamsDef.fmtBounds());

    // Cache the result of this function
    // 100% required for 1.90's librustc_session - "Trans Monomorph" took 20mins without that
    std::string cacheKey;
    {
        ::std::stringstream ss;
        ss << "impl" << implParamsDef.fmtArgs() << " " << desTraitPath << implTraitParams << " for " << impl_type << implParamsDef.fmtBounds();
        ss << " vs ";
        if (desTraitParams) {
            ss << *desTraitParams;
        } else {
            ss << "<?>";
        }
        ss << " for " << desType;
        cacheKey = ss.str();
    }
    {
        auto it = cachedImplChecks.find(cacheKey);
        if (it != cachedImplChecks.end()) {
            const auto& r = it->second;
            DEBUG("CACHED: " << r.second << " impl_params=" << r.first);
            return foundCb(r.first.clone(), r.second);
        }
    }
    // TODO: What if `des_trait_params` already has impl placeholders?

    HIR::PathParams impl_params;
    GetParams::ParamsSet paramsSet;
    GetParams get_params{sp, implParamsDef, impl_params, paramsSet};

    auto match = impl_type->matchTestGenericsFuzz(sp, desType, cbIdent, get_params);

    struct BaseImplPlaceholderIdx {
        unsigned ty = 0;
        unsigned val = 0;
        unsigned lft = 0;
    } baseImplPlaceholderIdx;

    if (desTraitParams) {
        ASSERT_BUG(sp, desTraitParams->types.size() == implTraitParams.types.size(), "Size mismatch in arguments for " << desTraitPath << " - " << *desTraitParams << " and " << implTraitParams);
        match &= implTraitParams.matchTestGenericsFuzz(sp, *desTraitParams, cbIdent, get_params);

        unsigned maxImplIdxTy = 0;
        unsigned maxImplIdxVal = 0;
        unsigned maxImplIdxLft = 0;
        auto visit_lft = [&](const ::HIR::LifetimeRef& l) {
            if (l.isParam() && l.asParam().is_placeholder()) {
                maxImplIdxLft = ::std::max(maxImplIdxLft, l.asParam().idx());
            }
        };
        // TODO: Get a generic visitor (running the same way as `Monomorphiser`)
        for (const auto& r : desTraitParams->types) {
            visit_ty_with(r, [&](const ::HIR::TypeData* t) -> bool {
                if (t->is_Generic() && t->as_Generic().is_placeholder()) {
                    unsigned implIdx = t->as_Generic().idx();
                    maxImplIdxTy = ::std::max(maxImplIdxTy, implIdx);
                }
                if (const auto* te = t->opt_Borrow()) {
                    visit_lft(te->lifetime);
                }
                // TODO: Path param lifetimes, etc
                return false;
            });
        }
        for (const auto& l : desTraitParams->mLifetimes) {
            visit_lft(l);
        }
        baseImplPlaceholderIdx.ty = maxImplIdxTy + 1;
        baseImplPlaceholderIdx.val = maxImplIdxVal + 1;
        baseImplPlaceholderIdx.lft = maxImplIdxLft + 1;

        size_t nPlaceholderTysNeeded = ::std::count(paramsSet.types.begin(), paramsSet.types.end(), false);
        size_t nPlaceholderValsNeeded = ::std::count(paramsSet.values.begin(), paramsSet.values.end(), false);
        size_t nPlaceholderLftsNeeded = 0;
        for (unsigned int i = 0; i < impl_params.mLifetimes.size(); i++) {
            if (!paramsSet.mLifetimes[i]) {
                nPlaceholderLftsNeeded++;
            }
        }
        if (nPlaceholderTysNeeded > 0) {
            ASSERT_BUG(sp, baseImplPlaceholderIdx.ty + impl_params.types.size() <= 256, "Out of impl placeholder types");
        }
        if (nPlaceholderValsNeeded > 0) {
            ASSERT_BUG(sp, baseImplPlaceholderIdx.val + impl_params.values.size() <= 256, "Out of impl placeholder values");
        }
        if (nPlaceholderTysNeeded > 0) {
            ASSERT_BUG(sp, baseImplPlaceholderIdx.lft + impl_params.mLifetimes.size() <= 256, "Out of impl placeholder lifetimes");
        }
    }
    if (match == ::HIR::Compare::Unequal) {
        DEBUG(" > Type mismatch");
        return false;
    }

    auto placeholderName = RcString::newInterned(FMT("impl_?_" << &implParamsDef));
    GetParams::ParamsSet placeholdersSet;
    HIR::PathParams placeholders;
    for (unsigned int i = 0; i < impl_params.types.size(); i++) {
        if (!paramsSet.types[i]) {
            if (placeholders.types.size() == 0) {
                placeholders.types.resize(impl_params.types.size());
                placeholdersSet.types.resize(impl_params.types.size());
            }
            placeholders.types[i] = crate.types.generic(placeholderName, 2 * 256 + baseImplPlaceholderIdx.ty + i);
            DEBUG("Placeholder " << placeholders.types[i] << " for I:" << i << " " << implParamsDef.types[i].mName);
        }
    }
    for (size_t i = 0; i < impl_params.values.size(); i++) {
        if (!paramsSet.values[i]) {
            if (placeholders.values.size() == 0) {
                placeholders.values.resize(impl_params.values.size());
                placeholdersSet.values.resize(impl_params.values.size());
            }
            placeholders.values[i] = ::HIR::ConstGeneric::make_Generic(::HIR::GenericRef(placeholderName, 2 * 256 + baseImplPlaceholderIdx.val + i));
        }
    }
    for (size_t i = 0; i < impl_params.mLifetimes.size(); i++) {
        if (!paramsSet.mLifetimes[i]) {
            if (placeholders.mLifetimes.size() == 0) {
                placeholders.mLifetimes.resize(impl_params.mLifetimes.size());
                placeholdersSet.mLifetimes.resize(impl_params.mLifetimes.size());
            }
            placeholders.mLifetimes[i] = ::HIR::LifetimeRef(2 * 256 + baseImplPlaceholderIdx.lft + i);
        }
    }

    struct Matcher: public ::HIR::MatchGenerics, public Monomorphiser {
        Span sp;
        const HIR::PathParams& impl_params;
        const GetParams::ParamsSet& paramsSet;
        const BaseImplPlaceholderIdx& baseImplPlaceholderIdx;
        RcString placeholderName;
        HIR::PathParams& placeholders;
        GetParams::ParamsSet& placeholdersSet;

        Matcher(HIR::TypeInterner& types, Span sp, const HIR::PathParams& impl_params, const GetParams::ParamsSet& paramsSet, RcString placeholderName, const BaseImplPlaceholderIdx& baseImplPlaceholderIdx, HIR::PathParams& placeholders, GetParams::ParamsSet& placeholdersSet)
            : Monomorphiser(types)
            , sp(sp)
            , impl_params(impl_params)
            , paramsSet(paramsSet)
            , baseImplPlaceholderIdx(baseImplPlaceholderIdx)
            , placeholderName(placeholderName)
            , placeholders(placeholders)
            , placeholdersSet(placeholdersSet)
        {
        }

        ::HIR::Compare matchTy(const ::HIR::GenericRef& g, const ::HIR::TypeData* ty, ::HIR::t_cb_resolve_type resolveCb) override {
            if (ty->is_Generic() && ty->as_Generic().binding == g.binding) {
                return ::HIR::Compare::Equal;
            }
            if (g.is_placeholder()) {
                if (g.idx() >= baseImplPlaceholderIdx.ty) {
                    auto i = g.idx() - baseImplPlaceholderIdx.ty;
                    ASSERT_BUG(sp, !paramsSet.types[i], "Placeholder to populated type returned. new " << ty << ", existing " << impl_params.types[i]);
                    auto& ph = placeholders.types[i];
                    if (!placeholdersSet.types[i]) {
                        DEBUG("[find_impl__check_crate_raw] Bind placeholder " << i << " to " << ty);
                        placeholdersSet.types[i] = true;
                        ph = ty;
                        return ::HIR::Compare::Equal;
                    } else if (ph == ty) {
                        return ::HIR::Compare::Equal;
                    } else {
                        return ph->compareWithPlaceholders(sp, ty, resolveCb);
                        //TODO(sp, "[find_impl__check_crate_raw] Compare placeholder " << i << " " << ph << " == " << ty);
                    }
                } else {
                    return ::HIR::Compare::Fuzzy;
                }
            } else {
                return ::HIR::Compare::Unequal;
            }
        }

        ::HIR::Compare matchVal(const ::HIR::GenericRef& g, const ::HIR::ConstGeneric& val) override {
            if (const auto* ge = val.opt_Generic()) {
                if (ge->binding == g.binding) {
                    return HIR::Equal;
                }
            }

            if (g.is_placeholder()) {
                if (g.idx() >= baseImplPlaceholderIdx.val) {
                    auto i = g.idx() - baseImplPlaceholderIdx.val;
                    ASSERT_BUG(sp, !paramsSet.values[i], "Placeholder to populated value returned. new " << val << ", existing " << impl_params.values[i]);
                    auto& ph = placeholders.values[i];
                    if (!placeholdersSet.values[i]) {
                        DEBUG("[find_impl__check_crate_raw] Bind placeholder value " << i << " to " << val);
                        placeholdersSet.values[i] = true;
                        ph = val.clone();
                        return ::HIR::Compare::Equal;
                    } else if (ph == val) {
                        return ::HIR::Compare::Equal;
                    } else {
                        TODO(sp, "[find_impl__check_crate_raw] Compare placeholder value " << i << " " << ph << " == " << val);
                    }
                } else {
                    return ::HIR::Compare::Fuzzy;
                }
            }

            TODO(Span(), "Matcher::match_val " << g << " with " << val);
            return ::HIR::Compare::Unequal;
        }

        ::HIR::Compare matchLft(const ::HIR::GenericRef& g, const ::HIR::LifetimeRef& lft) override {
            if (lft.isParam() && lft.binding == g.binding) {
                return HIR::Equal;
            }
            if (g.is_placeholder()) {
                if (g.idx() >= baseImplPlaceholderIdx.lft) {
                    auto i = g.idx() - baseImplPlaceholderIdx.lft;
                    ASSERT_BUG(sp, !paramsSet.mLifetimes[i], "Placeholder to populated lifetime returned. new " << lft << ", existing " << impl_params.mLifetimes[i]);
                    auto& ph = placeholders.mLifetimes[i];
                    if (!placeholdersSet.mLifetimes[i]) {
                        DEBUG("[find_impl__check_crate_raw] Bind placeholder lifetime " << i << " to " << lft);
                        placeholdersSet.mLifetimes[i] = true;
                        ph = lft;
                        return ::HIR::Compare::Equal;
                    } else if (ph == lft) {
                        return ::HIR::Compare::Equal;
                    } else {
                        TODO(sp, "[find_impl__check_crate_raw] Compare placeholder lifetime " << i << " " << ph << " == " << lft);
                    }
                } else {
                    return ::HIR::Compare::Fuzzy;
                }
            }
            if (lft == HIR::LifetimeRef()) {
                return HIR::Equal;
            }
            return HIR::Unequal;
        }

        ::HIR::TypeRef getType(const Span& sp, const ::HIR::GenericRef& ge) const override {
            if (ge.isSelf()) {
                // TODO: `impl_type` or `des_type`
                //    DEBUG("[find_impl__check_crate_raw] Self - " << impl_type << " or " << des_type);
                //TODO(sp, "[find_impl__check_crate_raw] Self - " << impl_type << " or " << des_type);
                //    return impl_type;
                TODO(sp, "get_type Self");
            }
            ASSERT_BUG(sp, !ge.is_placeholder(), "[find_impl__check_crate_raw] Placeholder param seen - " << ge);
            if (paramsSet.types.at(ge.binding)) {
                return impl_params.types.at(ge.binding);
            }
            return placeholders.types.at(ge.binding);
        }

        ::HIR::ConstGeneric getValue(const Span& sp, const ::HIR::GenericRef& val) const override {
            ASSERT_BUG(sp, val.binding < 256, "Generic value binding in " << val << " out of range (>=256)");
            ASSERT_BUG(sp, val.binding < impl_params.values.size(), "Generic value binding in " << val << " out of range (>= " << impl_params.values.size() << ")");
            if (paramsSet.values.at(val.binding)) {
                return impl_params.values.at(val.binding).clone();
            }
            ASSERT_BUG(sp, placeholders.values.size() == impl_params.values.size(), "Placeholder size mismatch: " << placeholders.values.size() << " != " << impl_params.values.size() << " - value=" << impl_params.values.at(val.binding));
            return placeholders.values.at(val.binding).clone();
        }

        ::HIR::LifetimeRef getLifetime(const Span& sp, const ::HIR::GenericRef& g) const override {
            if (g.group() == 3) {
                return HIR::LifetimeRef(g.binding);
            }
            ASSERT_BUG(sp, g.group() == 0, "Generic lifetime binding in " << g << " out of range (must be impl)");
            ASSERT_BUG(sp, g.idx() < impl_params.mLifetimes.size(), "Generic lifetime binding in " << g << " out of range (>= " << impl_params.mLifetimes.size() << ")");
            if (paramsSet.mLifetimes.at(g.binding)) {
                return impl_params.mLifetimes.at(g.binding);
            }
            ASSERT_BUG(sp, placeholders.mLifetimes.size() == impl_params.mLifetimes.size(), "Placeholder (lifetime) size mismatch: " << placeholders.mLifetimes.size() << " != " << impl_params.mLifetimes.size());
            return placeholders.mLifetimes.at(g.binding);
        }
    };

    Matcher matcher{crate.types, sp, impl_params, paramsSet, placeholderName, baseImplPlaceholderIdx, placeholders, placeholdersSet};

    // Bounds
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
                //
                for (const auto& assocBound : bTpMono.typeBounds) {
                    // TODO: Can bounds have generic params (GATs)
                    const auto& atyName = assocBound.first;
                    const ::HIR::TypeData* exp = assocBound.second.type;

                    // TODO: use `assoc_bound.second.source_trait`
                    ::HIR::GenericPath atySrcTrait;
                    trait_contains_type(sp, bTpMono.mPath, *e.trait.traitPtr, atyName.c_str(), atySrcTrait);

                    bool rv = false;
                    if (bTyMono->is_Generic() && bTyMono->as_Generic().is_placeholder()) {
                        DEBUG("- Placeholder param " << bTyMono << ", magic success");
                        rv = true;
                    } else {
                        rv = this->findImpl(sp, atySrcTrait.mPath, atySrcTrait.mParams, bTyMono, [&](const ImplRef& impl, bool) -> bool {
                            ::HIR::TypeRef have = impl.getType(crate.types, atyName.c_str(), assocBound.second.atyParams);
                            if (have == HIR::TypeRef()) {
                                have = crate.types.path(HIR::Path(impl.getImplType(crate.types), HIR::GenericPath(atySrcTrait.mPath, impl.getTraitParams(crate.types)), atyName), HIR::TypePathBinding::make_Unbound({}));
                            }
                            this->expandAssociatedTypes(sp, have);

                            DEBUG("[find_impl__check_crate_raw] ATY ::" << atyName << " - " << have << " ?= " << exp);
                            //auto cmp = have .match_test_generics_fuzz(sp, exp, cb_ident, matcher);
                            auto cmp = exp->matchTestGenericsFuzz(sp, have, cbIdent, matcher);
                            if (cmp == ::HIR::Compare::Unequal) {
                                DEBUG("Assoc ty " << atyName << " mismatch, " << have << " != des " << exp);
                            }
                            return cmp != ::HIR::Compare::Unequal;
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
                if (bTyMono->is_Generic() && bTyMono->as_Generic().is_placeholder()) {
                    DEBUG("- Placeholder param " << bTyMono << ", magic success");
                    rv = true;
                } else {
                    rv = this->findImpl(sp, bTpMono.mPath.mPath, bTpMono.mPath.mParams, bTyMono, [&](const auto& impl, bool) {
                        return true;
                    });
                }
                if (!rv && visit_ty_with(bTyMono, [](const HIR::TypeData* ty) {
                    return ty->is_Generic() && ty->as_Generic().is_placeholder();
                })) {
                    DEBUG("- Placeholder param within " << bTyMono << ", magic success");
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

    for (size_t i = 0; i < impl_params.types.size(); i++) {
        if (!paramsSet.types[i]) {
            if (!placeholdersSet.types[i]) {
                //BUG(sp, "Placeholder types shouldn't leak :( - " << placeholders.m_types[i]);
            }
            impl_params.types[i] = std::move(placeholders.types[i]);
        }
        //ASSERT_BUG(sp, impl_params.m_types[i] != HIR::TypeRef(), "Impl type parameter #" << i << " wasn't set (or even a placeholder)");
    }
    for (size_t i = 0; i < impl_params.mLifetimes.size(); i++) {
        if (!paramsSet.mLifetimes[i]) {
            if (desTraitParams) {
                if (!placeholdersSet.mLifetimes[i]) {
                    //BUG(sp, "Placeholder lifetimes shouldn't leak :( - " << placeholders.m_lifetimes[i]);
                    impl_params.mLifetimes[i] = HIR::LifetimeRef();
                    continue;
                }
            }
            impl_params.mLifetimes[i] = std::move(placeholders.mLifetimes[i]);
        }
    }
    DEBUG("impl_params = " << impl_params);

    assert(implParamsDef.types.size() == impl_params.types.size());
    for (size_t i = 0; i < implParamsDef.types.size(); i++) {
        if (implParamsDef.types.at(i).isSized) {
            // An unresolved parameter has no known sizedness yet.  It used to be
            // represented by a default-constructed TypeRef; the interned type
            // model represents that state explicitly as Infer.
            if (!impl_params.types[i]->is_Infer()) {
                if (!type_is_sized(sp, impl_params.types[i])) {
                    DEBUG("- Sized bound failed for " << impl_params.types[i]);
                    return false;
                }
            }
        }
    }

    // TODO: Can this be cached?
    // - Needs to cache the result
    {
        cachedImplChecks.insert(::std::make_pair(cacheKey, std::make_pair(impl_params.clone(), match)));
    }
    return foundCb(mv$(impl_params), match);
}

bool StaticTraitResolve::findImplCheckCrate(const Span& sp, const ::HIR::SimplePath& trait_path, const ::HIR::PathParams* trait_params, const ::HIR::TypeData* type, t_cb_find_impl foundCb, const ::HIR::TraitImpl& impl) const {
    DEBUG("impl" << impl.mParams.fmtArgs() << " " << trait_path << impl.traitArgs << " for " << impl.mType << impl.mParams.fmtBounds());
    return this->findImplCheckCrateRaw(sp, trait_path, trait_params, type, impl.mParams, impl.traitArgs, impl.mType, [&](auto impl_params, auto match) {
        return foundCb(ImplRef(mv$(impl_params), crate.getTraitByPath(sp, trait_path), trait_path, impl), (match == ::HIR::Compare::Fuzzy));
    });
}

::HIR::Compare StaticTraitResolve::checkAutoTraitImplDestructure(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams* paramsPtr, const ::HIR::TypeData* type) const {
    TRACE_FUNCTION_F("trait = " << trait << ", type = " << type);
    // HELPER: Search for an impl of this trait for an inner type, and return the match type
    auto type_impls_trait = [&](const auto& innerTy) -> ::HIR::Compare {
        auto lRes = ::HIR::Compare::Unequal;
        this->findImpl(sp, trait, *paramsPtr, innerTy, [&](auto, auto isFuzzy) {
            lRes = isFuzzy ? ::HIR::Compare::Fuzzy : ::HIR::Compare::Equal;
            return !isFuzzy;
        });
        DEBUG("[check_auto_trait_impl_destructure] " << innerTy << " - " << lRes);
        return lRes;
    };

    // - If the type is a path (struct/enum/...), search for impls for all contained types.
    if (const auto* ep = type->opt_Path()) {
        const auto& e = *ep;
        ::HIR::Compare res = ::HIR::Compare::Equal;
        TU_MATCH_HDRA( (e.path.mData), {)
        TU_ARMA(Generic, pe) {
                ::HIR::TypeRef tmp;
                auto monomorph = MonomorphStatePtr(crate.types, nullptr, &pe.mParams, nullptr);
                // HELPER: Get a possibily monomorphised version of the input type (stored in `tmp` if needed)
                auto monomorphGet = [&](const auto& ty) -> const ::HIR::TypeData* {
                    return this->monomorphExpandOpt(sp, tmp, ty, monomorph);
                };

            TU_MATCH_HDRA( (e.binding), {)
            TU_ARMA(Opaque, tpb) {
                        BUG(sp, "Opaque binding on generic path - " << type);
                    }
                    TU_ARMA(Unbound, tpb) {
                        BUG(sp, "Unbound binding on generic path - " << type);
                    }
                    TU_ARMA(Struct, tpb) {
                        const auto& str = *tpb;

                        // TODO: Somehow store a ruleset for auto traits on the type
                        // - Map of trait->does_impl for local fields?
                        // - Problems occur with type parameters
                        TU_MATCH(
                            ::HIR::Struct::Data,
                            (str.mData),
                            (se),
                            (Unit, ),
                            (Tuple,
                             for (const auto& fld : se) {
                                 const auto& fldTyMono = monomorphGet(fld.ent);
                                 DEBUG("Struct::Tuple " << fldTyMono);
                                 res &= type_impls_trait(fldTyMono);
                                 if (res == ::HIR::Compare::Unequal) {
                                     return ::HIR::Compare::Unequal;
                                 }
                             }),
                            (Named, for (const auto& fld : se) {
                                const auto& fldTyMono = monomorphGet(fld.ty);
                                DEBUG("Struct::Named '" << fld.name << "' " << fldTyMono);

                                res &= type_impls_trait(fldTyMono);
                                if (res == ::HIR::Compare::Unequal) {
                                    return ::HIR::Compare::Unequal;
                                }
                            })
                        )
                    }
                    TU_ARMA(Enum, tpb) {
                        if (const auto* e = tpb->mData.opt_Data()) {
                            for (const auto& var : *e) {
                                const auto& fldTyMono = monomorphGet(var.type);
                                DEBUG("Enum '" << var.name << "'" << fldTyMono);
                                res &= type_impls_trait(fldTyMono);
                                if (res == ::HIR::Compare::Unequal) {
                                    return ::HIR::Compare::Unequal;
                                }
                            }
                        }
                    }
                    TU_ARMA(Union, tpb) {
                        for (const auto& fld : tpb->mVariants) {
                            const auto& fldTyMono = monomorphGet(fld.ty);
                            DEBUG("Union '" << fld.name << "' " << fldTyMono);
                            res &= type_impls_trait(fldTyMono);
                            if (res == ::HIR::Compare::Unequal) {
                                return ::HIR::Compare::Unequal;
                            }
                        }
                    }
                    TU_ARMA(ExternType, tpb) {
                        TODO(sp, "Check auto trait destructure on extern type " << type);
                    }
            }
            DEBUG("- Nothing failed, calling callback");
            }
            TU_ARMA(UfcsUnknown, pe) {
                BUG(sp, "UfcsUnknown in typeck - " << type);
            }
            TU_ARMA(UfcsKnown, pe) {
                return ::HIR::Compare::Unequal;
                //TODO(sp, "Check trait bounds for bound on UfcsKnown " << type);
            }
            TU_ARMA(UfcsInherent, pe) {
                TODO(sp, "Auto trait lookup on UFCS Inherent type");
            }
        }
        return res;
    } else if (const auto* ep = type->opt_Tuple()) {
        ::HIR::Compare res = ::HIR::Compare::Equal;
        for (const auto& sty : *ep) {
            res &= type_impls_trait(sty);
            if (res == ::HIR::Compare::Unequal) {
                return ::HIR::Compare::Unequal;
            }
        }
        return res;
    } else if (const auto* e = type->opt_Array()) {
        return type_impls_trait(e->inner);
    }
    // Otherwise, there's no negative so it must be positive
    else {
        return ::HIR::Compare::Equal;
    }
}

const ::HIR::TypeData* StaticTraitResolve::fixTraitDefaultReturn(const Span& sp, const HIR::ItemPath& p, const ::HIR::TypeData* tpl, ::HIR::TypeRef& tmp) const {
    // If in a trait, then force expand erased associated types:
    // These are `<Self/**/ as ::"bin#"::TestTrait>::erased#with_default_0<'M0,>/*O*/`
    // Detect this by first ensuring that we're in a trait body, then if there's an ATY from that trait
    const auto& top_ip = p.getTopIp();
    if (top_ip.ty && top_ip.trait && top_ip.ty == crate.types.self()) {
        auto prefix = FMT(ATY_PREFIX_ERASED << p.name << "_");
        const auto& trait = crate.getTraitByPath(sp, *top_ip.trait);
        tmp = cloneTyWith(crate.types, sp, tpl, [&](const ::HIR::TypeData* tpl, ::HIR::TypeRef& out) -> bool {
            if (const auto* p = tpl->opt_Path()) {
                if (const auto* pe = p->path.mData.opt_UfcsKnown()) {
                    DEBUG("ATY " << tpl);
                    if (pe->type == top_ip.ty && pe->trait.mPath == *top_ip.trait && std::strncmp(pe->item.c_str(), prefix.c_str(), prefix.size()) == 0) {
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

void StaticTraitResolve::expandAssociatedTypes(const Span& sp, ::HIR::TypeRef& input) const {
    TRACE_FUNCTION_FR(input, input);
    this->expandAssociatedTypesInner(sp, input);
}

void StaticTraitResolve::evaluateArraySize(const Span& sp, ::HIR::ArraySize& size) const {
    ConvertHIRConstantEvaluateArraySize(sp, crate, HIR::SimplePath(crate.crateName, {}), size);
}

void StaticTraitResolve::evaluateConstGeneric(const Span& sp, ::HIR::ConstGeneric& value) const {
    ConvertHIRConstantEvaluateConstGeneric(sp, crate, value);
}

void StaticTraitResolve::evaluatePathParams(const Span& sp, ::HIR::PathParams& params) const {
    for (auto& value : params.values) {
        evaluateConstGeneric(sp, value);
    }
}

void StaticTraitResolve::expandAssociatedTypesPath(const Span& sp, ::HIR::Path& input) const {
    TRACE_FUNCTION_FR(input, input);
    TU_MATCH_HDRA( (input.mData), { )
    TU_ARMA(Generic, e2) {
            this->expandAssociatedTypesParams(sp, e2.mParams);
        }
        TU_ARMA(UfcsInherent, e2) {
            this->expandAssociatedTypesInner(sp, e2.type);
            this->expandAssociatedTypesParams(sp, e2.params);
            // TODO: impl params too?
            for (auto& arg : e2.impl_params.types) {
                this->expandAssociatedTypesInner(sp, arg);
            }
        }
        TU_ARMA(UfcsKnown, e2) {
            this->expandAssociatedTypesInner(sp, e2.type);
            this->expandAssociatedTypesParams(sp, e2.trait.mParams);
            this->expandAssociatedTypesParams(sp, e2.params);
        }
        TU_ARMA(UfcsUnknown, e2) {
            BUG(sp, "Encountered UfcsUnknown in EAT - " << input);
        }
    }
}

bool StaticTraitResolve::expandAssociatedTypesSingle(const Span& sp, ::HIR::TypeRef& input) const {
    TRACE_FUNCTION_F(input);
    if (input->is_Path()) {
        if (input->as_Path().path.mData.is_UfcsInherent()) {
            return expandAssociatedTypesUfcsInherent(sp, input);
        }
        if (input->as_Path().path.mData.is_UfcsKnown()) {
            return expandAssociatedTypesUfcsKnown(sp, input, /*recurse=*/false);
        }
    }
    return false;
}

bool StaticTraitResolve::types_equal_resolving_opaque(const Span& sp, const ::HIR::TypeData* left, const ::HIR::TypeData* right) const {
    auto reveal = [&](::HIR::TypeRef type) {
        for (unsigned depth = 0; depth < 64; depth++) {
            bool replaced = false;
            auto next = cloneTyWith(crate.types, sp, type, [&](const ::HIR::TypeData* candidate, ::HIR::TypeRef& output) {
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

void StaticTraitResolve::expandAssociatedTypesParams(const Span& sp, ::HIR::PathParams& params) const {
    for (auto& arg : params.types) {
        this->expandAssociatedTypesInner(sp, arg);
    }
}

void StaticTraitResolve::expandAssociatedTypesTp(const Span& sp, ::HIR::TraitPath& input) const {
    expandAssociatedTypesParams(sp, input.mPath.mParams);
    for (auto& arg : input.typeBounds) {
        this->expandAssociatedTypesParams(sp, arg.second.source_trait.mParams);
        this->expandAssociatedTypesInner(sp, arg.second.type);
    }
    for (auto& arg : input.traitBounds) {
        this->expandAssociatedTypesParams(sp, arg.second.source_trait.mParams);
        for (auto& t : arg.second.traits) {
            this->expandAssociatedTypesTp(sp, t);
        }
    }
}

void StaticTraitResolve::expandAssociatedTypesInner(const Span& sp, ::HIR::TypeRef& input) const {
    auto data = input->cloneData();
    TU_MATCH_HDRA( (data), {)
    TU_ARMA(Infer, e) {
            //if( m_treat_ivars_as_bugs ) {
            //    BUG(sp, "Encountered inferrence variable in static context");
            //}
        }
        TU_ARMA(Diverge, e) {
        }
        TU_ARMA(Primitive, e) {
        }
        TU_ARMA(Path, e) {
        TU_MATCH_HDRA( (e.path.mData), { )
        TU_ARMA(Generic, e2) {
                    evaluatePathParams(sp, e2.mParams);
                    ConvertHIRConstantEvaluateMethodParams(sp, crate, HIR::SimplePath(crate.crateName, {}), implGenerics, itemGenerics, e.binding.getGenerics(), e2.mParams);
                    expandAssociatedTypesParams(sp, e2.mParams);
                }
                TU_ARMA(UfcsInherent, e2) {
                    this->expandAssociatedTypesInner(sp, e2.type);
                    expandAssociatedTypesParams(sp, e2.params);
                    for (auto& arg : e2.impl_params.types) {
                        this->expandAssociatedTypesInner(sp, arg);
                    }
                    input = crate.types.intern(mv$(data));
                    if (this->expandAssociatedTypesUfcsInherent(sp, input)) {
                        this->expandAssociatedTypesInner(sp, input);
                    }
                    return;
                }
                TU_ARMA(UfcsKnown, e2) {
                    // An opaque associated type is not a resolved type. It only records
                    // that an earlier normalization attempt couldn't make progress. In a
                    // later (static) context more bounds can be available, so retry it.
                    const bool was_unbound = e.binding.is_Unbound();
                    const bool was_opaque = e.binding.is_Opaque();
                    if (!was_unbound && !was_opaque) {
                        return;
                    }

                    input = crate.types.intern(data.cloneData());
                    if (was_opaque) {
                        const auto opaque = input;
                        this->expandAssociatedTypesUfcsKnown(sp, input, false);
                        if (input != opaque) {
                            this->expandAssociatedTypesInner(sp, input);
                        }
                    }
                    else {
                        auto k = FMT(e.path);
                        auto it = atyCache.find(k);
                        if (it != atyCache.end()) {
                            DEBUG("Cached " << it->second);
                            input = it->second;
                        } else {
                            this->expandAssociatedTypesUfcsKnown(sp, input);
                            atyCache.insert(std::make_pair(std::move(k), input));
                        }
                    }
                    return;
                }
                TU_ARMA(UfcsUnknown, e2) {
                    BUG(sp, "Encountered UfcsUnknown in EAT - " << e.path);
                }
        }
        }
        TU_ARMA(Generic, e) {
        }
        TU_ARMA(TraitObject, e) {
            expandAssociatedTypesTp(sp, e.mTrait);
            for (auto& m : e.markers) {
                expandAssociatedTypesParams(sp, m.mParams);
            }
        }
        TU_ARMA(ErasedType, e) {
            for (auto& trait : e.traits) {
                expandAssociatedTypesTp(sp, trait);
            }
            expandAssociatedTypesParams(sp, e.use);
            TU_MATCH_HDRA( (e.inner), {)
            TU_ARMA(Known, ee) {
                    expandAssociatedTypesInner(sp, ee);
                }
                TU_ARMA(Fcn, ee) {
                    expandAssociatedTypesPath(sp, ee.origin);
                }
                TU_ARMA(Alias, ee) {
                    expandAssociatedTypesParams(sp, ee.params);
                }
            }
        }
        TU_ARMA(Array, e) {
            ConvertHIRConstantEvaluateArraySize(sp, crate, HIR::SimplePath(crate.crateName, {}), e.size);
            expandAssociatedTypesInner(sp, e.inner);
        }
        TU_ARMA(Slice, e) {
            expandAssociatedTypesInner(sp, e.inner);
        }
        TU_ARMA(Tuple, e) {
            for (auto& sub : e) {
                expandAssociatedTypesInner(sp, sub);
            }
        }
        TU_ARMA(Borrow, e) {
            expandAssociatedTypesInner(sp, e.inner);
        }
        TU_ARMA(Pointer, e) {
            expandAssociatedTypesInner(sp, e.inner);
        }
        TU_ARMA(NamedFunction, e) {
        TU_MATCH_HDRA( (e.path.mData), { )
        TU_ARMA(Generic, e2) {
                    //ConvertHIR_ConstantEvaluate_MethodParams(sp, m_crate, HIR::SimplePath(m_crate.m_crate_name, {}), m_impl_generics, m_item_generics, *e.binding.get_generics(), e2.m_params);
                    expandAssociatedTypesParams(sp, e2.mParams);
                }
                TU_ARMA(UfcsInherent, e2) {
                    this->expandAssociatedTypesInner(sp, e2.type);
                    expandAssociatedTypesParams(sp, e2.params);
                    // TODO: impl params too?
                    for (auto& arg : e2.impl_params.types) {
                        this->expandAssociatedTypesInner(sp, arg);
                    }
                }
                TU_ARMA(UfcsKnown, e2) {
                    this->expandAssociatedTypesInner(sp, e2.type);
                    expandAssociatedTypesParams(sp, e2.trait.mParams);
                    expandAssociatedTypesParams(sp, e2.params);
                }
                TU_ARMA(UfcsUnknown, e2) {
                    BUG(sp, "Encountered UfcsUnknown in EAT - " << e.path);
                }
        }
        }
        TU_ARMA(Function, e) {
            // Recurse?
            for (auto& ty : e.argTypes) {
                expandAssociatedTypesInner(sp, ty);
            }
            expandAssociatedTypesInner(sp, e.mRettype);
        }
        TU_ARMA(NodeType, e) {
        }
    }
    input = crate.types.intern(std::move(data));
}

bool StaticTraitResolve::expandAssociatedTypesUfcsInherent(const Span& sp, ::HIR::TypeRef& input) const {
    TRACE_FUNCTION_FR(input, input);
    ASSERT_BUG(sp, input->is_Path() && input->as_Path().path.mData.is_UfcsInherent(), input);

    const auto& pe = input->as_Path().path.mData.as_UfcsInherent();
    if (visit_ty_with(pe.type, [](const ::HIR::TypeData* type) {
        const auto* erased = type->opt_ErasedType();
        const auto* opaque = erased ? erased->inner.opt_Alias() : nullptr;
        return opaque && !opaque->inner->type;
    })) {
        DEBUG("Deferring inherent associated type with unresolved opaque receiver " << input);
        return false;
    }
    const ::HIR::TypeAlias* alias = nullptr;
    const ::HIR::GenericParams* implParamsDef = nullptr;
    ::HIR::PathParams impl_params;
    ::HIR::Compare bestMatch = ::HIR::Compare::Unequal;
    static const ::HIR::PathParams noTraitParams;

    crate.findTypeImpls(pe.type, HIR::ResolvePlaceholdersNop(), [&](const auto& impl) {
        const auto itemIt = impl.types.find(pe.item);
        if (itemIt == impl.types.end()) {
            return false;
        }

        bool selected = false;
        this->findImplCheckCrateRaw(
            sp,
            ::HIR::SimplePath(),
            nullptr,
            pe.type,
            impl.mParams,
            noTraitParams,
            impl.mType,
            [&](::HIR::PathParams candidateParams, ::HIR::Compare match) {
                if (match != ::HIR::Compare::Unequal
                    && (bestMatch == ::HIR::Compare::Unequal || match == ::HIR::Compare::Equal)) {
                    alias = &itemIt->second.data;
                    implParamsDef = &impl.mParams;
                    impl_params = mv$(candidateParams);
                    bestMatch = match;
                    selected = true;
                }
                return selected;
            }
        );
        return selected && bestMatch == ::HIR::Compare::Equal;
    });

    if (!alias) {
        DEBUG("No inherent associated type candidate for " << input);
        return false;
    }

    ConvertHIRConstantEvaluateMethodParams(
        sp,
        crate,
        ::HIR::SimplePath(crate.crateName, {}),
        implGenerics,
        itemGenerics,
        implParamsDef,
        impl_params
    );

    auto item_params = pe.params.clone();
    if (item_params.mLifetimes.empty()) {
        item_params.mLifetimes.resize(alias->mParams.mLifetimes.size());
    }
    if (item_params.mLifetimes.size() != alias->mParams.mLifetimes.size()
        || item_params.types.size() != alias->mParams.types.size()
        || item_params.values.size() != alias->mParams.values.size()) {
        ERROR(sp, E0000, "Incorrect generic arguments for inherent associated type " << input);
    }
    ConvertHIRConstantEvaluateMethodParams(
        sp,
        crate,
        ::HIR::SimplePath(crate.crateName, {}),
        implGenerics,
        itemGenerics,
        &alias->mParams,
        item_params
    );

    input = MonomorphStatePtr(crate.types, pe.type, &impl_params, &item_params).monomorphType(sp, alias->mType);
    return true;
}

namespace {
    bool valid_for_opaque(const ::HIR::TypeData* ty) {
        return monomorphiseTypeNeeded(ty) || visit_ty_with(ty, [](const HIR::TypeData* t) {
            return t->is_ErasedType() || t->is_Infer();
        });
    }
}

bool StaticTraitResolve::expandAssociatedTypesUfcsKnown(const Span& sp, ::HIR::TypeRef& input, bool recurse /*=true*/) const {
    TRACE_FUNCTION_FR(input, input);
    auto data = input->cloneData();
    auto& e = data.as_Path();
    auto& e2 = e.path.mData.as_UfcsKnown();
    auto publish = [&]() {
        input = crate.types.intern(data.cloneData());
    };

    static unsigned sRecursionLevel;

    struct RecurseEntry {
        HIR::TypeRef ty;
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
            ::std::vector<const HIR::TypeData*> ents;
            for (const auto& ent : sRecursionStack) {
                if (ent.level == sRecursionLevel) {
                    ents.push_back(ent.ty);
                }
            }
            if (ents.size() > 1) {
                std::sort(ents.begin(), ents.end(), [](const HIR::TypeData* a, const HIR::TypeData* b) {
                    return a < b;
                });
                input = ents[0];
            }
            DEBUG("-> " << input);
            auto opaqueData = input->cloneData();
            opaqueData.as_Path().binding = HIR::TypePathBinding::make_Opaque({});
            input = crate.types.intern(std::move(opaqueData));
            return false;
        }
    }

    struct StackGuard {
        ~StackGuard() {
            sRecursionStack.pop_back();
        }
    } _;

    sRecursionStack.push_back(RecurseEntry{crate.types.path(HIR::Path(e2.type, e2.trait.clone(), e2.item), {}), sRecursionLevel});

    sRecursionLevel += 1;
    this->expandAssociatedTypesInner(sp, e2.type);
    for (auto& arg : e2.trait.mParams.types) {
        this->expandAssociatedTypesInner(sp, arg);
    }
    sRecursionLevel -= 1;
    publish();

    DEBUG("Locating associated type for " << e.path);

    {
        const auto* t = &e2.type;
        while ((*t)->is_Path() && (*t)->as_Path().path.mData.is_UfcsKnown()) {
            t = &(*t)->as_Path().path.mData.as_UfcsKnown().type;
        }
        if ((*t)->is_Infer()) {
            DEBUG("Infer seen in static EAT, leaving as-is");
            return false;
        }
    }

    TU_MATCH_HDRA( (*e2.type), {)
    default:
        // Nothing special
        break;
        TU_ARMA(Infer, te) {
            DEBUG("Infer seen in static EAT, leaving as-is");
            return false;
        }
        // - If it's a closure, then the only trait impls are those generated by typeck
        TU_ARMA(NodeType, te) {
        TU_MATCH_HDRA((te), {)
        TU_ARMA(Closure, nodeP) {
                    if (e2.trait.mPath == mLangFn || e2.trait.mPath == mLangFnMut || e2.trait.mPath == mLangFnOnce) {
                        if (e2.item == "Output") {
                            input = nodeP->returnType;
                            return true;
                        } else {
                            ERROR(sp, E0000, "No associated type " << e2.item << " for trait " << e2.trait);
                        }
                    }
                }
                TU_ARMA(Generator, nodeP) {
                }
                TU_ARMA(Async, nodeP) {
                }
        }
        }
        // If it's a TraitObject, then maybe we're asking for a bound
        TU_ARMA(TraitObject, te) {
            //const auto& data_trait = te.m_trait.m_path;
            //if( e2.trait.m_path == data_trait.m_path ) {
            //    if( e2.trait.m_params == data_trait.m_params )
            //    {
            //        auto it = te.m_trait.m_type_bounds.find( e2.item );
            //        if( it == te.m_trait.m_type_bounds.end() ) {
            //            // TODO: Mark as opaque and return.
            //            // - Why opaque? It's not bounded, don't even bother
            //            TODO(sp, "Handle unconstrained associate type " << e2.item << " from " << e2.type);
            //        }
            //
            //        input = it->second.type.clone();
            //        return true;
            //    }
            //}
        }
        // TODO: ErasedType? Does that need a bounds check?
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
            this->expandAssociatedTypesInner(sp, input);
        }
        return true;
    }

    // If the type of this UfcsKnown is ALSO a UfcsKnown - Check if it's bounded by this trait with equality
    // Use bounds on other associated types too (if `e2.type` was resolved to a fixed associated type)
    if(const auto* te_inner = e2.type->opt_Path())
    {
        if (const auto* peInnerP = te_inner->path.mData.opt_UfcsKnown()) {
            const auto& peInner = *peInnerP;
            // TODO: Search for equality bounds on this associated type (e3) that match the entire type (e2)
            // - Does simplification of complex associated types
            const auto& trait_ptr = this->crate.getTraitByPath(sp, peInner.trait.mPath);
            const auto& assocTy = trait_ptr.types.at(peInner.item);

            DEBUG("Inner UfcsKnown");

            // Resolve where Self=pe_inner.type (i.e. for the trait this inner UFCS is on)
            auto cbPlaceholdersTrait = MonomorphStatePtr(crate.types, peInner.type, &peInner.trait.mParams, &peInner.params);
            for (const auto& bound : assocTy.traitBounds) {
                // Associated equalities on a trait bound carry the trait that
                // actually declares the item. That can be a parent trait of
                // `bound.m_path` (e.g. `Int<Unsigned = T>` where `Unsigned`
                // is declared by `MinInt`), so matching the outer path loses
                // exactly the equality we need.
                auto it = bound.typeBounds.find(e2.item);
                if (it != bound.typeBounds.end()) {
                    auto source_trait = cbPlaceholdersTrait.monomorphGenericpath(sp, it->second.source_trait, false);
                    auto atyParams = cbPlaceholdersTrait.monomorphPathParams(sp, it->second.atyParams, false);
                    if (source_trait.equalsIgnoringRegions(e2.trait)
                        && atyParams.equalsIgnoringRegions(e2.params)) {
                        DEBUG("Found inner bound from " << source_trait << ": " << it->second.type);
                        input = monomorphiseTypeNeeded(it->second.type)
                            ? cbPlaceholdersTrait.monomorphType(sp, it->second.type)
                            : it->second.type;
                        if (recurse) {
                            this->expandAssociatedTypes(sp, input);
                        }
                        return true;
                    }
                }

                // Find trait in this trait.
                auto boundParams = cbPlaceholdersTrait.monomorphPathParams(sp, bound.mPath.mParams, false);
                const auto& boundTrait = crate.getTraitByPath(sp, bound.mPath.mPath);
                bool replaced = this->findNamedTraitInTrait(sp, e2.trait.mPath, e2.trait.mParams, boundTrait, bound.mPath.mPath, boundParams, e2.type, [&](const auto& params, const auto& assoc) {
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
    ::HIR::GenericPath  trait_path;
    if( !this->trait_contains_type(sp, e2.trait, this->crate.getTraitByPath(sp, e2.trait.mPath), e2.item.c_str(), trait_path) )
        BUG(sp, "Cannot find associated type " << e2.item << " anywhere in trait " << e2.trait);
    //e2.trait = mv$(trait_path);

    bool replacementHappened = true;
    ::ImplRef  bestImpl;
    rv = this->findImpl(sp, trait_path.mPath, trait_path.mParams, e2.type, [&](ImplRef impl, bool fuzzy) {
        DEBUG("[expand_associated_types] Found " << impl);
        // If a fuzzy match was found, monomorphise and EAT the checked types and try again
        // - A fuzzy can be caused by an opaque match.
        // - TODO: Move this logic into `find_impl`
        if (fuzzy) {
            auto cbIdent = HIR::ResolvePlaceholdersNop();
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
            if (pp.compareWithPlaceholders(sp, trait_path.mParams, cbIdent) == HIR::Compare::Unequal) {
                DEBUG("[expand_associated_types] - Fuzzy - params don't match: " << pp << " != " << trait_path.mParams);
                return false;
            }
            DEBUG("[expand_associated_types] - Fuzzy - Actually matches");
        }

        if (impl.type_is_specialisable(e2.item.c_str())) {
            if (impl.moreSpecificThan(crate.types, bestImpl)) {
                bestImpl = mv$(impl);
                DEBUG("- Still specialisable");
            }
            return false;
        } else {
            auto nt = impl.getType(crate.types, e2.item.c_str(), e2.params);
            if (nt != ::HIR::TypeRef()) {
                DEBUG("Converted UfcsKnown - " << e.path << " = " << nt);
                if (input == nt) {
                    replacementHappened = false;
                    return true;
                }
                input = mv$(nt);
                replacementHappened = true;
            } else {
                DEBUG("Mark " << e.path << " as opaque");
                e.binding = ::HIR::TypePathBinding::make_Opaque({});
                publish();
                ASSERT_BUG(sp, valid_for_opaque(input), "Set opaque on a non-generic type: " << input);
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
        e.binding = ::HIR::TypePathBinding::make_Opaque({});
        publish();
        ASSERT_BUG(sp, valid_for_opaque(input), "Set opaque on a non-generic type: " << input);
        this->replaceEqualities(input);
        DEBUG("- Couldn't find a non-specialised impl of " << trait_path << " for " << e2.type << " - treating as opaque");
        return false;
    }

    if( assumeOpaque ) {
        e.binding = ::HIR::TypePathBinding::make_Opaque({});
        publish();
        ASSERT_BUG(sp, valid_for_opaque(input), "Set opaque on a non-generic type: " << input);
        DEBUG("Assuming that " << input << " is an opaque name");

        bool rv = this->replaceEqualities(input);
        if (recurse) {
            this->expandAssociatedTypesInner(sp, input);
        }
        return rv;
    }

    ERROR(sp, E0000, "Cannot find an implementation of " << trait_path << " for " << e2.type);
}

bool StaticTraitResolve::replaceEqualities(::HIR::TypeRef& input) const {
    const Span sp;
    TRACE_FUNCTION_F("input=" << input);
    DEBUG("m_type_equalities = {" << typeEqualities << "}");
    // - Check if there's an alias for this opaque name
    auto a = ::std::find_if(typeEqualities.begin(), typeEqualities.end(), [&](const auto& entry) {
        return entry.first == input || entry.first->equalsIgnoringRegions(input);
    });
    if (a != typeEqualities.end()) {
        // HACK: Shouldn't need this, but works around some missing cases
        if (a->second.hrbs.is_empty()) {
            input = a->second.ty;
            return true;
        }
        // Match HRLs in the source, and expand them into the output
        MatchHrls matchHrls{crate.types, a->second.hrbs};
        DEBUG("Found for" << a->second.hrbs.fmtArgs() << " " << a->second.ty);
        a->first->matchTestGenerics(sp, input, HIR::ResolvePlaceholdersNop(), matchHrls);
        DEBUG("HRLs resolved to: " << matchHrls.hrls);
        input = matchHrls.monomorphType(sp, a->second.ty);
        DEBUG("- Replace with " << input);
        return true;
    } else {
        return false;
    }
}

// -------------------------------------------------------------------------------------------------------------------
//
// -------------------------------------------------------------------------------------------------------------------

bool StaticTraitResolve::iterateAtyBounds(const Span& sp, const ::HIR::Path::Data::Data_UfcsKnown& pe, ::std::function<bool(const ::HIR::TraitPath&)> cb) const {
    const auto& trait_ref = crate.getTraitByPath(sp, pe.trait.mPath);
    ASSERT_BUG(sp, trait_ref.types.count(pe.item) != 0, "Trait " << pe.trait.mPath << " doesn't contain an associated type " << pe.item);
    const auto& atyDef = trait_ref.types.find(pe.item)->second;

    for (const auto& bound : atyDef.traitBounds) {
        if (cb(bound)) {
            return true;
        }
    }
    // Search `<Self as Trait>::Name` bounds on the trait itself
    for (const auto& bound : trait_ref.mParams.bounds) {
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

        const auto& beTypePe = be.type->as_Path().path.mData.as_UfcsKnown();
        if (beTypePe.type != crate.types.self()) {
            continue;
        }
        if (beTypePe.trait.mPath != pe.trait.mPath) {
            continue;
        }
        if (beTypePe.item != pe.item) {
            continue;
        }

        if (cb(be.trait)) {
            return true;
        }
    }

    return false;
}

// -------------------------------------------------------------------------------------------------------------------
//
// -------------------------------------------------------------------------------------------------------------------
bool StaticTraitResolve::findNamedTraitInTrait(const Span& sp, const ::HIR::SimplePath& des, const ::HIR::PathParams& desParams, const ::HIR::Trait& trait_ptr, const ::HIR::SimplePath& trait_path, const ::HIR::PathParams& pp, const ::HIR::TypeData* target_type, ::std::function<bool(const ::HIR::PathParams&, ::HIR::TraitPath::assocListT)> callback) const {
    TRACE_FUNCTION_F(des << desParams << " from " << trait_path << pp);
    if (pp.types.size() != trait_ptr.mParams.types.size()) {
        BUG(sp, "Incorrect number of parameters for trait - " << trait_path << pp);
    }

    if (des == trait_path) {
        auto cmp = pp.compareWithPlaceholders(sp, desParams, HIR::ResolvePlaceholdersNop());
        if (cmp != ::HIR::Compare::Unequal) {
            // Return an empty ATY list, this is valid because callers also check the input ATY list in the callback
            return callback(pp, {});
        }
    }

    auto monomorph = MonomorphStatePtr(crate.types, target_type, &pp, nullptr);
    for (const auto& pt : trait_ptr.allParentTraits) {
        auto ptMono = monomorph.monomorphTraitpath(sp, pt, false);
        this->expandAssociatedTypesTp(sp, ptMono);

        DEBUG(pt << " => " << ptMono);
        // TODO: When in pre-typecheck mode, this needs to be a fuzzy match (because there might be a UfcsUnknown in the
        // monomorphed version) OR, there may be placeholders
        if (pt.mPath.mPath == des) {
            auto cmp = ptMono.mPath.mParams.compareWithPlaceholders(sp, desParams, HIR::ResolvePlaceholdersNop());
            // pt_mono.m_path.m_params == des_params )
            if (cmp != ::HIR::Compare::Unequal) {
                return callback(ptMono.mPath.mParams, mv$(ptMono.typeBounds));
            }
        }
    }

    return false;
}

bool StaticTraitResolve::trait_contains_type(const Span& sp, const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait_ptr, const char* name, ::HIR::GenericPath& outPath) const {
    TRACE_FUNCTION_FR("name=" << name << ", trait=" << trait_path, outPath);
    auto it = trait_ptr.types.find(name);
    if (it != trait_ptr.types.end()) {
        outPath = trait_path.clone();
        return true;
    }

    auto tySelf = crate.types.self();
    auto monomorph = MonomorphStatePtr(crate.types, tySelf, &trait_path.mParams, nullptr);
    for (const auto& st : trait_ptr.allParentTraits) {
        if (st.traitPtr->types.count(name)) {
            outPath.mPath = st.mPath.mPath;
            outPath.mParams = monomorph.monomorphPathParams(sp, st.mPath.mParams, false);
            return true;
        }
    }
    return false;
}

bool StaticTraitResolve::type_is_copy(const Span& sp, const ::HIR::TypeData* ty) const {
    TU_MATCH_HDRA( (*ty), {)
    TU_ARMA(Generic, e) {
            {
                auto it = copyCache.find(ty);
                if (it != copyCache.end()) {
                    return it->second;
                }
            }
            auto pp = ::HIR::PathParams();
            bool rv = this->findImplBounds(sp, mLangCopy, &pp, ty, [&](auto, bool) {
                return true;
            });
            copyCache.insert(::std::make_pair(ty, rv));
            return rv;
        }
        TU_ARMA(Path, e) {
            const auto* markings = e.binding.getTraitMarkings();
            if (markings) {
                if (!markings->is_copy) {
                    // Doesn't impl Copy
                    return false;
                } else if (!e.path.mData.as_Generic().mParams.hasParams()) {
                    // No params, must be Copy
                    return true;
                } else {
                    // TODO: Also have a marking that indicates that the type is unconditionally Copy
                }
            }

            {
                auto it = copyCache.find(ty);
                if (it != copyCache.end()) {
                    DEBUG("CACHED " << ty << " = " << it->second);
                    return it->second;
                }
            }
            auto pp = ::HIR::PathParams();
            bool rv = this->findImpl(sp, mLangCopy, &pp, ty, [&](auto, bool) {
                return true;
            }, true);
            copyCache.insert(::std::make_pair(ty, rv));
            return rv;
        }
        TU_ARMA(Diverge, e) {
            // The ! type is kinda Copy ...
            return true;
        }
        TU_ARMA(NodeType, e) {
        TU_MATCH_HDRA((e), {)
        TU_ARMA(Closure, nodeP) {
                    return nodeP->isCopy;
                }
                TU_ARMA(Generator, nodeP) {
                    // NOTE: Generators aren't Copy
                    return false;
                }
                TU_ARMA(Async, nodeP) {
                    // NOTE: Async blocks aren't Copy? Can they be?
                    return false;
                }
        }
        }
        TU_ARMA(Infer, e) {
            // Shouldn't be hit
            return false;
        }
        TU_ARMA(Borrow, e) {
            // Only shared &-ptrs are copy
            return (e.type == ::HIR::BorrowType::Shared);
        }
        TU_ARMA(Pointer, e) {
            // All raw pointers are Copy
            return true;
        }
        TU_ARMA(NamedFunction, e) {
            // All function pointers are Copy/Clone
            return true;
        }
        TU_ARMA(Function, e) {
            // All function pointers are Copy
            return true;
        }
        TU_ARMA(Primitive, e) {
            // All primitives (except the unsized `str`) are Copy
            return e != ::HIR::CoreType::Str;
        }
        TU_ARMA(Array, e) {
            // TODO: Why is `[T; 0]` treated as `Copy`?
            if (TU_TEST1(e.size, Known, == 0)) {
                return true;
            }
            return type_is_copy(sp, e.inner);
        }
        TU_ARMA(Slice, e) {
            // [T] isn't Sized, so isn't Copy ether
            return false;
        }
        TU_ARMA(TraitObject, e) {
            // (Trait) isn't Sized, so isn't Copy ether
            return false;
        }
        TU_ARMA(ErasedType, e) {
            for (const auto& trait : e.traits) {
                if (findNamedTraitInTrait(sp, mLangCopy, {}, *trait.traitPtr, trait.mPath.mPath, trait.mPath.mParams, ty, [](const auto&, auto) {
                    return true;
                })) {
                    return true;
                }
            }
            return false;
        }
        TU_ARMA(Tuple, e) {
            for (const auto& ty : e) {
                if (!type_is_copy(sp, ty)) {
                    return false;
                }
            }
            return true;
        }
    }
    throw "";
}

bool StaticTraitResolve::type_is_clone(const Span& sp, const ::HIR::TypeData* ty) const {
    TU_MATCH_HDRA( (*ty), {)
    TU_ARMA(Generic, e) {
            {
                auto it = cloneCache.find(ty);
                if (it != cloneCache.end()) {
                    return it->second;
                }
            }
            auto pp = ::HIR::PathParams();
            bool rv = this->findImplBounds(sp, mLangClone, &pp, ty, [&](auto, bool) {
                return true;
            });
            cloneCache.insert(::std::make_pair(ty, rv));
            return rv;
        }
        TU_ARMA(Path, e) {
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
            auto pp = ::HIR::PathParams();
            bool rv = this->findImpl(sp, mLangClone, &pp, ty, [&](auto, bool) {
                return true;
            }, true);
            cloneCache.insert(::std::make_pair(ty, rv));
            return rv;
        }
        TU_ARMA(Diverge, e) {
            // The ! type is kinda Copy/Clone ...
            return true;
        }
        TU_ARMA(NodeType, e) {
        TU_MATCH_HDRA((e), {)
        TU_ARMA(Closure, nodeP) {
                    return nodeP->isCopy;
                }
                TU_ARMA(Generator, nodeP) {
                    TODO(sp, "type_is_clone - Generator");
                }
                TU_ARMA(Async, nodeP) {
                    TODO(sp, "type_is_clone - Async");
                }
        }
        }
        TU_ARMA(Infer, e) {
            // Shouldn't be hit
            return false;
        }
        TU_ARMA(Borrow, e) {
            // Only shared &-ptrs are copy/clone
            return (e.type == ::HIR::BorrowType::Shared);
        }
        TU_ARMA(Pointer, e) {
            // All raw pointers are Copy/Clone
            return true;
        }
        TU_ARMA(NamedFunction, e) {
            // All function pointers are Copy/Clone
            return true;
        }
        TU_ARMA(Function, e) {
            // All function pointers are Copy/Clone
            return true;
        }
        TU_ARMA(Primitive, e) {
            // All primitives (except the unsized `str`) are Copy/Clone
            return e != ::HIR::CoreType::Str;
        }
        TU_ARMA(Array, e) {
            return (e.size.is_Known() && e.size.as_Known() == 0) || type_is_clone(sp, e.inner);
        }
        TU_ARMA(Slice, e) {
            // [T] isn't Sized, so isn't Copy ether
            return false;
        }
        TU_ARMA(TraitObject, e) {
            // (Trait) isn't Sized, so isn't Copy ether
            return false;
        }
        TU_ARMA(ErasedType, e) {
            for (const auto& trait : e.traits) {
                if (findNamedTraitInTrait(sp, mLangClone, {}, *trait.traitPtr, trait.mPath.mPath, trait.mPath.mParams, ty, [](const auto&, auto) {
                    return true;
                })) {
                    return true;
                }
            }
            return false;
        }
        TU_ARMA(Tuple, e) {
            for (const auto& ty : e) {
                if (!type_is_clone(sp, ty)) {
                    return false;
                }
            }
            return true;
        }
    }
    throw "";
}

bool StaticTraitResolve::type_is_sized(const Span& sp, const ::HIR::TypeData* ty) const {
    switch (this->metadataType(sp, ty)) {
        case MetadataType::None:
            return true;
        default:
            return false;
    }
}

bool StaticTraitResolve::type_is_impossible(const Span& sp, const ::HIR::TypeData* ty) const {
    TU_MATCH_HDRA( (*ty), {)
        break;
        default:
            return false;
            TU_ARMA(Diverge, _e)
            return true;
            TU_ARMA(Path, e) {
        TU_MATCH_HDRA( (e.binding), {)
        TU_ARMA(Unbound, pbe) {
                        // BUG?
                        return false;
                    }
                    TU_ARMA(Opaque, pbe) {
                        // TODO: This can only be with UfcsKnown, so check if the trait specifies ?Sized
                        return false;
                    }
                    TU_ARMA(Struct, pbe) {
                        const auto& params = e.path.mData.as_Generic().mParams;
                        const auto& str = *pbe;
            TU_MATCH_HDRA( (str.mData), {)
            TU_ARMA(Unit, e)
                return false;
                            TU_ARMA(Tuple, e) {
                                for (const auto& fld : e) {
                                    ::HIR::TypeRef tmp;
                                    const auto& fieldTy = this->monomorphExpandOpt(sp, tmp, fld.ent, MonomorphStatePtr(crate.types, ty, &params, nullptr));
                                    if (type_is_impossible(sp, fieldTy)) {
                                        return true;
                                    }
                                }
                                return false;
                            }
                            TU_ARMA(Named, e) {
                                for (const auto& fld : e) {
                                    ::HIR::TypeRef tmp;
                                    const auto& fieldTy = this->monomorphExpandOpt(sp, tmp, fld.ty, MonomorphStatePtr(crate.types, ty, &params, nullptr));
                                    if (type_is_impossible(sp, fieldTy)) {
                                        return true;
                                    }
                                }
                                return false;
                            }
            }
                    }
                    TU_ARMA(Enum, pbe) {
                        const auto& params = e.path.mData.as_Generic().mParams;
            TU_MATCH_HDRA( (pbe->mData), { )
            TU_ARMA(Value, e) {
                                return e.variants.size() == 0;
                            }
                            TU_ARMA(Data, e) {
                                // If all variants are impossible, then this type is impossible
                                for (const auto& fld : e) {
                                    const auto& tpl = fld.type;
                                    ::HIR::TypeRef tmp;
                                    const auto& fieldTy = this->monomorphExpandOpt(sp, tmp, tpl, MonomorphStatePtr(crate.types, ty, &params, nullptr));
                                    // Not impossible, ergo the enum is possible
                                    if (!type_is_impossible(sp, fieldTy)) {
                                        return false;
                                    }
                                }
                                return true;
                            }
            }
            TODO(sp, "type_is_impossible for enum " << ty);
                    }
                    TU_ARMA(Union, pbe) {
                        // TODO: Check all variants? Or just one?
                        TODO(sp, "type_is_impossible for union " << ty);
                    }
                    TU_ARMA(ExternType, pbe) {
                        // Extern types are possible, just not usable
                        return false;
                    }
        }
        return true;
            }
            TU_ARMA(Borrow, e)
            return type_is_impossible(sp, e.inner);
            TU_ARMA(Pointer, e) {
                return false;
                //return type_is_impossible(sp, e.inner);
            }
            TU_ARMA(Function, e) {
                // TODO: Check all arguments?
                return true;
            }
            TU_ARMA(Array, e) {
                return type_is_impossible(sp, e.inner);
            }
            TU_ARMA(Slice, e) {
                return type_is_impossible(sp, e.inner);
            }
            TU_ARMA(Tuple, e) {
                for (const auto& ty : e) {
                    if (type_is_impossible(sp, ty)) {
                        return true;
                    }
                }
                return false;
            }
    }
    throw "";
}

bool StaticTraitResolve::canUnsize(const Span& sp, const ::HIR::TypeData* dstTy, const ::HIR::TypeData* src_ty) const {
    TRACE_FUNCTION_F(dstTy << " <- " << src_ty);

    ASSERT_BUG(sp, !dstTy->is_Infer(), "_ seen after inferrence - " << dstTy);
    ASSERT_BUG(sp, !src_ty->is_Infer(), "_ seen after inferrence - " << src_ty);

    {
        //ASSERT_BUG(sp, dst_ty != src_ty, "Equal types for can_unsize - " << dst_ty << " <-" << src_ty );
        if (dstTy == src_ty) {
            return true;
        }
    }

    auto ir = traitBounds.equal_range(std::make_pair(src_ty, std::ref(mLangUnsize)));
    for (auto it = ir.first; it != ir.second; ++it) {
        const auto& beDst = it->first.second.mParams.types.at(0);

        if (dstTy == beDst) {
            DEBUG("Found bounded");
            return ::HIR::Compare::Equal;
        }
    }

    // Associated types, check the bounds in the trait.
    if (src_ty->is_Path() && src_ty->as_Path().path.mData.is_UfcsKnown()) {
        const auto& pe = src_ty->as_Path().path.mData.as_UfcsKnown();
        auto ms = MonomorphStatePtr(crate.types, pe.type, &pe.trait.mParams, nullptr);
        auto foundBound = this->iterateAtyBounds(sp, pe, [&](const ::HIR::TraitPath& bound) {
            if (bound.mPath.mPath != mLangUnsize) {
                return false;
            }
            const auto& beDstTpl = bound.mPath.mParams.types.at(0);
            ::HIR::TypeRef tmp_ty;
            const auto& beDst = ms.maybeMonomorphType(sp, tmp_ty, beDstTpl);

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
    if (dstTy->is_Path() && src_ty->is_Path()) {
        bool dstIsUnsizable = dstTy->as_Path().binding.is_Struct() && dstTy->as_Path().binding.as_Struct()->structMarkings.canUnsize;
        bool src_is_unsizable = src_ty->as_Path().binding.is_Struct() && src_ty->as_Path().binding.as_Struct()->structMarkings.canUnsize;
        if (dstIsUnsizable || src_is_unsizable) {
            DEBUG("Struct unsize? " << dstTy << " <- " << src_ty);
            const auto& str = *dstTy->as_Path().binding.as_Struct();
            const auto& dstGp = dstTy->as_Path().path.mData.as_Generic();
            const auto& src_gp = src_ty->as_Path().path.mData.as_Generic();

            if (dstGp == src_gp) {
                DEBUG("Can't Unsize, destination and source are identical");
                return false;
            } else if (dstGp.mPath == src_gp.mPath) {
                DEBUG("Checking for Unsize " << dstGp << " <- " << src_gp);
                // Structures are equal, add the requirement that the ?Sized parameter also impl Unsize
                const auto& dstInner = dstGp.mParams.types.at(str.structMarkings.unsized_param);
                const auto& src_inner = src_gp.mParams.types.at(str.structMarkings.unsized_param);
                return this->canUnsize(sp, dstInner, src_inner);
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

        DEBUG("TraitObject unsize? " << dstTy << " <- " << src_ty);

        // (Trait) <- (Trait+Foo)
        if (const auto* se = src_ty->opt_TraitObject()) {
            // 1. Data trait must be the same
            if (de->mTrait.mPath.mPath != se->mTrait.mPath.mPath) {
                // Ensure that `de->m_trait` is a parent of `se->m_trait`
                const auto& trait = *se->mTrait.traitPtr;
                bool found = false;
                for (const auto& pt : trait.allParentTraits) {
                    if (pt.mPath.mPath == de->mTrait.mPath.mPath) {
                        auto p = MonomorphStatePtr(crate.types, nullptr, &se->mTrait.mPath.mParams, nullptr).monomorphGenericpath(sp, pt.mPath);
                        if (p == de->mTrait.mPath) {
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
                if (de->mTrait.mPath != se->mTrait.mPath) {
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

        ::HIR::TypeData::Data_TraitObject tmp_e;
        tmp_e.mTrait.mPath = de->mTrait.mPath.mPath;

        // Check data trait first.
        if (de->mTrait.mPath.mPath == ::HIR::SimplePath()) {
            ASSERT_BUG(sp, de->markers.size() > 0, "TraitObject with no traits - " << dstTy);
            good = true;
        } else {
            good = false;
            findImpl(sp, de->mTrait.mPath.mPath, de->mTrait.mPath.mParams, src_ty, [&](const auto impl, auto fuzz) {
                //ASSERT_BUG(sp, !fuzz, "Fuzzy match in can_unsize - " << dst_ty << " <- " << src_ty << " - " << impl);
                good = true;
                for (const auto& aty : de->mTrait.typeBounds) {
                    // TODO: Can ATY bounds have generics
                    auto atyv = impl.getType(crate.types, aty.first.c_str(), aty.second.atyParams);
                    if (atyv == ::HIR::TypeRef()) {
                        // Get the trait from which this associated type comes.
                        // Insert a UfcsKnown path for that
                        atyv = crate.types.path(::HIR::Path(src_ty, aty.second.source_trait.clone(), aty.first), {});
                    }
                    // Run EAT
                    this->expandAssociatedTypes(sp, atyv);
                    // Region identity is not part of the associated-type
                    // relation used for trait-object coercions.  In
                    // particular, closure output inference can retain an
                    // omitted region while the HRTB destination has already
                    // rebound it.  TypeRef equality is deliberately pointer
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
            tmp_e.markers.back().mParams = impl.getTraitParams(crate.types);
            return true;
        };
        for (const auto& marker : de->markers) {
            if (!good) {
                break;
            }
            tmp_e.markers.push_back(marker.mPath);
            good &= this->findImpl(sp, marker.mPath, marker.mParams, src_ty, cb);
        }

        return good;
    }

    // [T] <- [T; n]
    if (const auto* de = dstTy->opt_Slice()) {
        if (const auto* se = src_ty->opt_Array()) {
            DEBUG("Array unsize? " << de->inner << " <- " << se->inner);
            return se->inner == de->inner || se->inner->equalsIgnoringRegions(de->inner);
        }
    }

    DEBUG("Can't unsize, no rules matched");
    return false;
}

// Check if the passed type contains an UnsafeCell
// Returns `Fuzzy` if generic, `Equal` if it does contain an UnsafeCell, and `Unequal` if it doesn't (shared=immutable)
HIR::Compare StaticTraitResolve::type_is_interior_mutable(const Span& sp, const ::HIR::TypeData* ty) const {
    TU_MATCH_HDRA( (*ty), {)
    TU_ARMA(Infer, e) {
            // Is this a bug?
            return HIR::Compare::Fuzzy;
        }
        TU_ARMA(Diverge, e) {
            return HIR::Compare::Unequal;
        }
        TU_ARMA(Primitive, e) {
            return HIR::Compare::Unequal;
        }
        TU_ARMA(Path, e) {
            auto monomorphCb = MonomorphStatePtr(crate.types, nullptr, e.path.mData.is_Generic() ? &e.path.mData.as_Generic().mParams : nullptr, nullptr);
            HIR::TypeRef tmp_ty;
            auto monomorph = [&](const auto& tpl) -> const ::HIR::TypeData* {
                return this->monomorphExpandOpt(sp, tmp_ty, tpl, monomorphCb);
            };
        TU_MATCH_HDRA( (e.binding), {)
        TU_ARMA(Unbound, pbe)
            return HIR::Compare::Fuzzy;
                TU_ARMA(Opaque, pbe)
                return HIR::Compare::Fuzzy;
                TU_ARMA(ExternType, pbe) // Extern types can't be interior mutable (but they also shouldn't be direct)
                return HIR::Compare::Unequal;

                TU_ARMA(Struct, pbe) {
                    const HIR::GenericPath& p = e.path.mData.as_Generic();
                    if (p.mPath == crate.getLangItemPath(sp, "unsafe_cell")) {
                        return HIR::Compare::Equal;
                    }
                    // TODO: Cache this result?
            TU_MATCH_HDRA( (pbe->mData), { )
            TU_ARMA(Unit, _)    return HIR::Compare::Unequal;
                        TU_ARMA(Tuple, e) {
                            for (const auto& v : e) {
                                switch (this->type_is_interior_mutable(sp, monomorph(v.ent))) {
                                    case HIR::Compare::Equal:
                                        return HIR::Compare::Equal;
                                    case HIR::Compare::Fuzzy:
                                        return HIR::Compare::Fuzzy;
                                    default:
                                        continue;
                                }
                            }
                            return HIR::Compare::Unequal;
                        }
                        TU_ARMA(Named, e) {
                            for (const auto& v : e) {
                                switch (this->type_is_interior_mutable(sp, monomorph(v.ty))) {
                                    case HIR::Compare::Equal:
                                        return HIR::Compare::Equal;
                                    case HIR::Compare::Fuzzy:
                                        return HIR::Compare::Fuzzy;
                                    default:
                                        continue;
                                }
                            }
                            return HIR::Compare::Unequal;
                        }
            }
                }
                TU_ARMA(Enum, pbe) {
            TU_MATCH_HDRA( (pbe->mData), { )
            TU_ARMA(Value, _)   return HIR::Compare::Unequal;
                        TU_ARMA(Data, ee) {
                            for (const auto& var : ee) {
                                switch (this->type_is_interior_mutable(sp, monomorph(var.type))) {
                                    case HIR::Compare::Equal:
                                        return HIR::Compare::Equal;
                                    case HIR::Compare::Fuzzy:
                                        return HIR::Compare::Fuzzy;
                                    default:
                                        continue;
                                }
                            }
                            return HIR::Compare::Unequal;
                        }
            }
                }
                TU_ARMA(Union, pbe) {
                    for (const auto& var : pbe->mVariants) {
                        switch (this->type_is_interior_mutable(sp, monomorph(var.ty))) {
                            case HIR::Compare::Equal:
                                return HIR::Compare::Equal;
                            case HIR::Compare::Fuzzy:
                                return HIR::Compare::Fuzzy;
                            default:
                                continue;
                        }
                    }
                    return HIR::Compare::Unequal;
                }
        }
        }
        TU_ARMA(Generic, e) {
            return HIR::Compare::Fuzzy;
        }
        TU_ARMA(TraitObject, e) {
            // Can't know with a trait object
            return HIR::Compare::Fuzzy;
        }
        TU_ARMA(ErasedType, e) {
            // Can't know with an erased type (effectively a generic)
            return HIR::Compare::Fuzzy;
        }
        TU_ARMA(Array, e) {
            return this->type_is_interior_mutable(sp, e.inner);
        }
        TU_ARMA(Slice, e) {
            return this->type_is_interior_mutable(sp, e.inner);
        }
        TU_ARMA(Tuple, e) {
            for (const auto& t : e) {
                auto rv = this->type_is_interior_mutable(sp, t);
                if (rv != HIR::Compare::Unequal) {
                    return rv;
                }
            }
            return HIR::Compare::Unequal;
        }
        TU_ARMA(NodeType, e) {
        TU_MATCH_HDRA((e), {)
        TU_ARMA(Closure, nodeP) {
                    // Return fuzzy (i.e. might be) if the closure class is still unknown.
                    if (nodeP->cls == HIR::ExprNodeClosure::Class::Unknown) {
                        return HIR::Compare::Fuzzy;
                    }
                    // Shortcut: Copy closures won't be imut
                    if (nodeP->isCopy) {
                        return HIR::Compare::Unequal;
                    }
                    // Check all captures
                    for (const auto& c : nodeP->captures) {
                        auto rv = this->type_is_interior_mutable(sp, c->resType);
                        if (rv != HIR::Compare::Unequal) {
                            return rv;
                        }
                    }
                    // If no capture possibly imut, then return no
                    return HIR::Compare::Unequal;
                }
                TU_ARMA(Generator, nodeP) {
                    // Check all captures
                    for (const auto& c : nodeP->captures) {
                        auto rv = this->type_is_interior_mutable(sp, c->resType);
                        if (rv != HIR::Compare::Unequal) {
                            return rv;
                        }
                    }
                    // If no capture possibly imut, then return no
                    return HIR::Compare::Unequal;
                }
                TU_ARMA(Async, nodeP) {
                    TODO(sp, "type_is_interior_mutable on async");
                }
        }
        }
        // Borrow and pointer are not interior mutable (they might point to something, but that doesn't matter)
        TU_ARMA(Borrow, e) {
            return HIR::Compare::Unequal;
        }
        TU_ARMA(Pointer, e) {
            return HIR::Compare::Unequal;
        }
        TU_ARMA(NamedFunction, e) {
            return HIR::Compare::Unequal;
        }
        TU_ARMA(Function, e) {
            return HIR::Compare::Unequal;
        }
    }
    return HIR::Compare::Fuzzy;
}

MetadataType StaticTraitResolve::metadataType(const Span& sp, const ::HIR::TypeData* ty, bool errOnUnknown /*=false*/) const {
    TU_MATCH_HDRA( (*ty), {)
    default:
        return MetadataType::None;
        TU_ARMA(Generic, e) {
            // Check for an explicit `Sized` bound
            auto pp = ::HIR::PathParams();
            bool rv = this->findImplBounds(sp, mLangSized, &pp, ty, [&](auto, bool) {
                return true;
            });
            if (rv) {
                return MetadataType::None;
            }
            if (e.binding == 0xFFFF) {
                ASSERT_BUG(sp, implGenerics, "Use of `Self` with no self type (no impl generics)");
                return selfMetadata;
            } else if ((e.binding >> 8) == 0) {
                auto idx = e.binding & 0xFF;
                ASSERT_BUG(sp, implGenerics, "Encountered generic " << ty << " without impl generics available");
                ASSERT_BUG(sp, idx < implGenerics->types.size(), "Encountered generic " << ty << " out of range of impl generic spec");
                if (implGenerics->types[idx].isSized) {
                    return MetadataType::None;
                } else {
                    return MetadataType::Unknown;
                }
            } else if ((e.binding >> 8) == 1) {
                auto idx = e.binding & 0xFF;
                ASSERT_BUG(sp, itemGenerics, "Encountered generic " << ty << " without item generics available");
                ASSERT_BUG(sp, idx < itemGenerics->types.size(), "Encountered generic " << ty << " out of range of item generic spec");
                if (itemGenerics->types[idx].isSized) {
                    return MetadataType::None;
                } else {
                    return MetadataType::Unknown;
                }
            } else if (e.is_placeholder()) {
                return MetadataType::None;
            } else {
                BUG(sp, "Unknown generic binding on " << ty);
            }
        }
        TU_ARMA(ErasedType, e) {
            if (e.isSized) {
                return MetadataType::None;
            } else {
                return MetadataType::Unknown;
            }
        }
        TU_ARMA(Path, e) {
        TU_MATCH_HDRA( (e.binding), { )
        TU_ARMA(Unbound, pbe) {
                    // TODO: Should this return something else?
                    return MetadataType::Unknown;
                }
                TU_ARMA(Opaque, pbe) {
                    //auto pp = ::HIR::PathParams();
                    //return this->find_impl(sp, m_lang_Sized, &pp, ty, [&](auto , bool){ return true; }, true);
                    // TODO: This can only be with UfcsKnown, so check if the trait specifies ?Sized
                    //return MetadataType::Unknown;
                    return MetadataType::None;
                }
                TU_ARMA(Struct, pbe) {
                    switch (pbe->structMarkings.dst_type) {
                        case ::HIR::StructMarkings::DstType::Slice:
                            return MetadataType::Slice;
                        case ::HIR::StructMarkings::DstType::TraitObject:
                            return MetadataType::TraitObject;
                        case ::HIR::StructMarkings::DstType::None:
                        case ::HIR::StructMarkings::DstType::Possible: {
                            const auto& params = e.path.mData.as_Generic().mParams;
                            auto monomorph = [&](const auto& tpl) {
                                return this->monomorphExpand(sp, tpl, MonomorphStatePtr(crate.types, ty, &params, nullptr));
                            };
                            TU_MATCHA((pbe->mData), (se), (Unit, return MetadataType::None;), (Tuple, return se.empty() ? MetadataType::None : this->metadataType(sp, monomorph(se.back().ent));), (Named, return se.empty() ? MetadataType::None : this->metadataType(sp, monomorph(se.back().ty));))
                            throw "";
                        }
                    }
                }
                TU_ARMA(ExternType, pbe) {
                    // Extern types aren't Sized, but have no metadata
                    return MetadataType::Zero;
                }
                TU_ARMA(Enum, pbe) {
                }
                TU_ARMA(Union, pbe) {
                }
        }
        return MetadataType::None;
        }
        TU_ARMA(Infer, e) {
            // Shouldn't be hit? but can early on
            //BUG(sp, "metadata_type: Found ivar? " << ty);
            return MetadataType::Unknown;
        }
        TU_ARMA(Diverge, e) {
            // The ! type is kinda Sized ...
            return MetadataType::None;
        }
        TU_ARMA(Primitive, e) {
            // All primitives (except the unsized `str`) are Sized
            if (e == ::HIR::CoreType::Str) {
                return MetadataType::Slice;
            } else {
                return MetadataType::None;
            }
        }
        TU_ARMA(Slice, e) {
            return MetadataType::Slice;
        }
        TU_ARMA(TraitObject, e) {
            return MetadataType::TraitObject;
        }
        TU_ARMA(Tuple, e) {
            // TODO: Unsized tuples? are they a thing?
            //for(const auto& ty : e)
            //    if( !type_is_sized(sp, ty) )
            //        return false;
            return MetadataType::None;
        }
    }
    throw "bug";
}

bool StaticTraitResolve::type_needs_drop_glue(const Span& sp, const ::HIR::TypeData* ty) const {
    // A crate without the Drop lang item cannot define a destructor.  In that
    // language mode no type can require compiler-generated drop glue, and in
    // particular the resolver must not try to look up an empty trait path.
    if (mLangDrop.components().empty()) {
        return false;
    }

    // If `T: Copy`, then it can't need drop glue
    if (type_is_copy(sp, ty)) {
        return false;
    }

    TU_MATCH_HDRA( (*ty), {)
    TU_ARMA(Generic, e) {
            // TODO: Is this an error?
            return true;
        }
        TU_ARMA(Path, e) {
            if (e.binding.is_Opaque()) {
                return true;
            }

            if (e.path.mData.as_Generic().mPath == crate.getLangItemPathOpt("manually_drop")) {
                return false;
            }

            auto it = dropCache.find(ty);
            if (it != dropCache.end()) {
                return it->second;
            }

            auto pp = ::HIR::PathParams();
            bool hasDirectDrop = this->findImpl(sp, mLangDrop, &pp, ty, [&](auto, bool) {
                return true;
            }, true);
            if (hasDirectDrop) {
                dropCache.insert(::std::make_pair(ty, true));
                return true;
            }

            ::HIR::TypeRef tmp_ty;
            const auto& pe = e.path.mData.as_Generic();
            auto monomorphCb = MonomorphStatePtr(crate.types, nullptr, &pe.mParams, nullptr);
            auto monomorph = [&](const auto& tpl) -> const ::HIR::TypeData* {
                return this->monomorphExpandOpt(sp, tmp_ty, tpl, monomorphCb);
            };
            bool needsDropGlue = false;
            TU_MATCHA(
                (e.binding),
                (pbe),
                (Unbound, BUG(sp, "Unbound path");),
                (Opaque,
                 // Technically a bug, checked above
                 return true;),
                (Struct,
                 TU_MATCHA(
                     (pbe->mData),
                     (se),
                     (Unit, ),
                     (Tuple,
                      for (const auto& e : se) {
                          if (type_needs_drop_glue(sp, monomorph(e.ent))) {
                              needsDropGlue = true;
                              break;
                          }
                      }),
                     (Named,
                      for (const auto& e : se) {
                          if (type_needs_drop_glue(sp, monomorph(e.ty))) {
                              needsDropGlue = true;
                              break;
                          }
                      })
                 )),
                (Enum,
                 if (const auto* e = pbe->mData.opt_Data()) {
                     for (const auto& var : *e) {
                         if (type_needs_drop_glue(sp, monomorph(var.type))) {
                             needsDropGlue = true;
                             break;
                         }
                     }
                 }),
                (Union,
                 // Unions don't have drop glue unless they impl Drop
                 needsDropGlue = false;),
                (ExternType,
                 // Extern types don't have drop glue
                 needsDropGlue = false;)
            )
            dropCache.insert(::std::make_pair(ty, needsDropGlue));
            return needsDropGlue;
        }
        TU_ARMA(Diverge, e) {
            return false;
        }
        TU_ARMA(NodeType, e) {
            // All magic node types need glue
            return true;
        }
        TU_ARMA(Infer, e) {
            BUG(sp, "type_needs_drop_glue on _");
            return false;
        }
        TU_ARMA(Borrow, e) {
            // &-ptrs don't have drop glue
            if (e.type != ::HIR::BorrowType::Owned) {
                return false;
            }
            return type_needs_drop_glue(sp, e.inner);
        }
        TU_ARMA(Pointer, e) {
            return false;
        }
        TU_ARMA(NamedFunction, e) {
            return false;
        }
        TU_ARMA(Function, e) {
            return false;
        }
        TU_ARMA(Primitive, e) {
            return false;
        }
        TU_ARMA(Array, e) {
            return type_needs_drop_glue(sp, e.inner);
        }
        TU_ARMA(Slice, e) {
            return type_needs_drop_glue(sp, e.inner);
        }
        TU_ARMA(TraitObject, e) {
            return true;
        }
        TU_ARMA(ErasedType, e) {
            // Is this an error?
            return true;
        }
        TU_ARMA(Tuple, e) {
            for (const auto& ty : e) {
                if (type_needs_drop_glue(sp, ty)) {
                    return true;
                }
            }
            return false;
        }
    }
    assert(!"Fell off the end of type_needs_drop_glue");
    throw "";
}

const ::HIR::TypeData* StaticTraitResolve::isTypeOwnedBox(const ::HIR::TypeData* ty) const {
    if (!ty->is_Path()) {
        return nullptr;
    }
    const auto& te = ty->as_Path();

    if (!te.path.mData.is_Generic()) {
        return nullptr;
    }
    const auto& pe = te.path.mData.as_Generic();

    if (pe.mPath != mLangBox) {
        return nullptr;
    }
    // TODO: Properly assert?
    return pe.mParams.types.at(0);
}

const ::HIR::TypeData* StaticTraitResolve::isTypePhantomData(const ::HIR::TypeData* ty) const {
    if (!ty->is_Path()) {
        return nullptr;
    }
    const auto& te = ty->as_Path();

    if (!te.path.mData.is_Generic()) {
        return nullptr;
    }
    const auto& pe = te.path.mData.as_Generic();

    if (pe.mPath != mLangPhantomData) {
        return nullptr;
    }
    // TODO: Properly assert?
    return pe.mParams.types.at(0);
}

HIR::TypeRef StaticTraitResolve::getFieldType(const Span& sp, const ::HIR::TypeData* ty, const RcString& name) const {
    TU_MATCH_HDRA((*ty), {)
    default:
        TODO(sp, "" << ty << " " << name);
        TU_ARMA(Borrow, te) {
            ASSERT_BUG(sp, name == RcString(), "get_field_type: Deref with non-empty field (`" << name << "`)");
            return te.inner;
        }
        TU_ARMA(Tuple, te) {
            ::std::stringstream ss{name.c_str()};
            int idx = -1;
            ss >> idx;
            ASSERT_BUG(sp, idx >= 0, "Malformed tuple index field name - `" << name << "`");
            ASSERT_BUG(sp, size_t(idx) < te.size(), "Tuple index out of bounds");
            return te.at(idx);
        }
        TU_ARMA(Path, te) {
        TU_MATCH_HDRA( (te.binding), {)
        default:
            BUG(sp, "Getting field on invalid type - " << ty);
                TU_ARMA(Struct, pbe) {
                    MonomorphStatePtr ms{crate.types, nullptr, &te.path.mData.as_Generic().mParams, nullptr};
            TU_MATCH_HDRA( (pbe->mData), { )
            TU_ARMA(Named, se) {
                            for (const auto& f : se) {
                                if (f.name == name) {
                                    return ms.monomorphType(sp, f.ty);
                                }
                            }
                            BUG(sp, "Unknown field `" << name << "` on " << ty);
                        }
                        TU_ARMA(Tuple, se) {
                            unsigned index = std::strtol(name.c_str(), nullptr, 10);
                            ASSERT_BUG(sp, index < se.size(), "" << ty << " " << name);
                            return ms.monomorphType(sp, se.at(index).ent);
                        }
                        TU_ARMA(Unit, se) {
                            BUG(sp, "Getting field from unit-like struct - " << ty);
                        }
            }
                }
                TU_ARMA(Union, pbe) {
                    MonomorphStatePtr ms{crate.types, nullptr, &te.path.mData.as_Generic().mParams, nullptr};
                    TODO(sp, "" << ty << " " << name);
                }
        }
        }
    }
    BUG(sp, "Reached end of `get_field_type` - " << ty);
}

StaticTraitResolve::ValuePtr StaticTraitResolve::getValue(const Span& sp, const ::HIR::Path& p, MonomorphState& outParams, bool signature_only /*=false*/, const HIR::GenericParams** outImplParamsDef /*=nullptr*/) const {
    TRACE_FUNCTION_F(p << ", signature_only=" << signature_only);
    outParams = MonomorphState{crate.types};
    TU_MATCH_HDR( (p.mData), {)
    TU_ARM(p.mData, Generic, pe) {
            if (pe.mPath.components().size() > 1) {
                const auto& ti = crate.getTypeitemByPath(sp, pe.mPath, /*ignore_crate_name=*/false, /*ignore_last_node=*/true);
                if (const auto* e = ti.opt_Enum()) {
                    if (outImplParamsDef) {
                        *outImplParamsDef = &e->mParams;
                    }
                    outParams.ppImpl = &pe.mParams;
                    auto idx = e->findVariant(pe.mPath.components().back());
                    if (e->mData.is_Data()) {
                        if (e->mData.as_Data()[idx].type != crate.types.unit()) {
                            return ValuePtr::Data_EnumConstructor{e, idx};
                        }
                    }
                    return ValuePtr::Data_EnumValue{e, idx};
                }
            }
            const auto& v = crate.getValitemByPath(sp, pe.mPath);
            TU_MATCHA((v), (ve), (Import, BUG(sp, "Module Import");), (Constant, outParams.ppMethod = &pe.mParams; return &ve;), (Static, outParams.ppMethod = &pe.mParams; return &ve;), (Function, outParams.ppMethod = &pe.mParams; return &ve;), (StructConstant, outParams.ppImpl = &pe.mParams; TODO(sp, "StructConstant - " << p);), (StructConstructor, outParams.ppImpl = &pe.mParams; const auto& str = crate.getStructByPath(sp, ve.ty); if (outImplParamsDef) { *outImplParamsDef = &str.mParams; } return ValuePtr::Data_StructConstructor{&ve.ty, &str};))
            throw "";
        }
        TU_ARM(p.mData, UfcsKnown, pe) {
            if (pe.trait.mPath == HIR::SimplePath() && pe.item == "vtable#") {
                DEBUG("Empty trait VTable, return NotYetKnown");
                return ValuePtr::make_NotYetKnown({});
            }
            outParams.self_ty = pe.type;
            outParams.ppImpl = &pe.trait.mParams;
            outParams.ppMethod = &pe.params;
            const ::HIR::Trait& tr = crate.getTraitByPath(sp, pe.trait.mPath);
            if (!tr.values.count(pe.item)) {
                DEBUG("Value " << pe.item << " not found in trait " << pe.trait.mPath);
                return ValuePtr();
            }

            if (outImplParamsDef) {
                *outImplParamsDef = &tr.mParams;
                // Updated if an impl is found+used
            }

            const ::HIR::TraitValueItem& v = tr.values.at(pe.item);
            if (signature_only) {
                TU_MATCHA((v), (ve), (Constant, return &ve;), (Static, return &ve;), (Function, return &ve;))
            } else {
                bool bestIsSpec = false;
                ImplRef bestImpl;
                ValuePtr rv;
                this->findImpl(sp, pe.trait.mPath, &pe.trait.mParams, pe.type, [&](auto impl, bool isFuzz) -> bool {
                    DEBUG(impl);
                    if (!impl.mData.is_TraitImpl()) {
                        return false;
                    }
                    const ::HIR::TraitImpl& ti = *impl.mData.as_TraitImpl().impl;
                    bool isSpec = false;

                    ValuePtr this_rv;
                    // - Constants
                    if (this_rv.is_NotFound()) {
                        auto it = ti.constants.find(pe.item);
                        if (it != ti.constants.end()) {
                            isSpec = it->second.isSpecialisable;
                            this_rv = &it->second.data;
                        }
                    }
                    // - Statics
                    if (this_rv.is_NotFound()) {
                        auto it = ti.statics.find(pe.item);
                        if (it != ti.statics.end()) {
                            isSpec = it->second.isSpecialisable;
                            this_rv = &it->second.data;
                        }
                    }
                    // - Functions
                    if (this_rv.is_NotFound()) {
                        auto it = ti.methods.find(pe.item);
                        if (it != ti.methods.end()) {
                            isSpec = it->second.isSpecialisable;
                            this_rv = &it->second.data;
                        }
                    }

                    if (this_rv.is_NotFound()) {
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
                        rv = std::move(this_rv);
                        // NOTE: There could be an overlapping and more-specific impl without `default` being involved
                        //return !is_spec;
                        return false;
                    }
                });
                if (!bestImpl.isValid()) {
                    // If the type and impl are fully known, then look for trait provided values/bodies
                    if (!monomorphiseTypeNeeded(pe.type, true) && !monomorphisePathparamsNeeded(pe.trait.mParams, true)) {
                        // Look for provided bodies
                    TU_MATCH_HDRA( (v), {)
                    TU_ARMA(Constant, ve) {
                                // Constants?
                                if (ve.mValue || ve.valueState != HIR::Constant::ValueState::Unknown) {
                                    DEBUG("Trait provided value");
                                    // NOTE: The parameters have already been set
                                    return &ve;
                                } else {
                                    DEBUG("Trait did not provide a value");
                                }
                            }
                            TU_ARMA(Static, ve) {
                                // Statics?
                            }
                            TU_ARMA(Function, ve) {
                                if (ve.mCode || ve.mCode.mir) {
                                    DEBUG("Trait provided body");
                                    // NOTE: The parameters have already been set
                                    return &ve;
                                }
                                // Fall through if there's no provided body
                            }
                    }
                    } else {
                        DEBUG("No best impl, but monomorph needed - can't check trait");
                    }
                    return ValuePtr::make_NotYetKnown({});
                }
                if (bestIsSpec) {
                    // If there's generics present in the path, return NotYetKnown
                    if (monomorphiseTypeNeeded(pe.type, true) || monomorphisePathparamsNeeded(pe.trait.mParams, true)) {
                        DEBUG("Specialisable and still generic, return NotYetKnown");
                        return ValuePtr::make_NotYetKnown({});
                    }
                }

                if (!bestImpl.mData.is_TraitImpl()) {
                    TODO(sp, "Use bounded constant values for " << p);
                }
                auto& ie = bestImpl.mData.as_TraitImpl();
                if (outImplParamsDef) {
                    *outImplParamsDef = &ie.impl->mParams;
                }
                outParams.ppImpl = &outParams.ppImplData;
                outParams.ppImplData = ie.impl_params.clone();
                ASSERT_BUG(sp, !rv.is_NotFound(), "");
                return rv;
            }
            throw "";
        }
        TU_ARM(p.mData, UfcsInherent, pe) {
            outParams.self_ty = pe.type;
            //out_params.pp_impl = &out_params.pp_impl_data;
            outParams.ppImpl = &pe.impl_params;
            outParams.ppMethod = &pe.params;
            ValuePtr rv;
            crate.findTypeImpls(pe.type, HIR::ResolvePlaceholdersNop(), [&](const auto& impl) {
                DEBUG("Found impl" << impl.mParams.fmtArgs() << " " << impl.mType);
                // Populate pp_impl if not populated
                if (!pe.impl_params.hasParams()) {
                    GetParams::ParamsSet paramsSet;
                    GetParams get_params{sp, impl.mParams, outParams.ppImplData, paramsSet};

                    auto cbIdent = HIR::ResolvePlaceholdersNop();
                    impl.mType->matchTestGenericsFuzz(sp, pe.type, cbIdent, get_params);

                    if (!pe.impl_params.mLifetimes.empty()) {
                        outParams.ppImplData.mLifetimes = pe.impl_params.mLifetimes;
                    }

                    const auto& impl_params = outParams.ppImplData;
                    /*for(size_t i = 0; i < impl_params.m_types.size(); i ++ ) {
                    if( !params_set.m_types[i] ) {
                        // TODO: Error when there's a type param that can't be determined?
                    }
                }
                for(size_t i = 0; i < impl_params.m_values.size(); i ++ ) {
                    if( !params_set.m_values[i] ) {
                        // TODO: Error when there's a value param that can't be determined?
                    }
                }
                for(size_t i = 0; i < impl_params.m_lifetimes.size(); i ++ ) {
                    if( !params_set.m_lifetimes[i] ) {
                        // TODO: Error when there's a lifetime param that can't be determined?
                    }
                }*/

                    outParams.ppImpl = &outParams.ppImplData;
                    DEBUG("PP impl = " << *outParams.ppImpl);
                } else {
                    DEBUG("Pre-existing imp params = " << *outParams.ppImpl);
                }

                if (outImplParamsDef) {
                    *outImplParamsDef = &impl.mParams;
                }

                // TODO: Specialisation
                {
                    auto fit = impl.methods.find(pe.item);
                    if (fit != impl.methods.end()) {
                        ASSERT_BUG(sp, impl.mParams.types.size() == outParams.ppImpl->types.size(), "Mismatch in param counts `" << *outParams.ppImpl << "`, params are `" << impl.mParams.fmtArgs() << "`\n- in " << p);
                        DEBUG("- Contains method, good");
                        rv = ValuePtr{&fit->second.data};
                        return true;
                    }
                }
                {
                    auto it = impl.constants.find(pe.item);
                    if (it != impl.constants.end()) {
                        ASSERT_BUG(sp, impl.mParams.types.size() == pe.impl_params.types.size(), "Mismatch in param counts " << p << ", params are " << impl.mParams.fmtArgs());
                        rv = ValuePtr{&it->second.data};
                        return true;
                    }
                }
                return false;
            });
            return rv;
        }
        TU_ARM(p.mData, UfcsUnknown, pe) {
            BUG(sp, "UfcsUnknown - " << p);
        }
    }
    throw "";
}

StaticTraitResolve::StaticTraitResolve(const ::HIR::Crate& crate)
    : TraitResolveCommon(crate) {
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
NullOnDrop<const ::HIR::GenericParams> StaticTraitResolve::set_impl_generics(HIR::StructMarkings::DstType struct_dst_type, const ::HIR::GenericParams& gps) {
    MetadataType mt = MetadataType::None;
    switch (struct_dst_type) {
        case HIR::StructMarkings::DstType::None:
            break;
        case HIR::StructMarkings::DstType::Possible:
            mt = MetadataType::Unknown;
            break;
        case HIR::StructMarkings::DstType::Slice:
            mt = MetadataType::Slice;
            break;
        case HIR::StructMarkings::DstType::TraitObject:
            mt = MetadataType::TraitObject;
            break;
    }
    set_impl_generics_raw(mt, gps);
    return NullOnDrop<const ::HIR::GenericParams>(implGenerics);
}
NullOnDrop<const ::HIR::GenericParams> StaticTraitResolve::set_impl_generics(MetadataType self_meta_type, const ::HIR::GenericParams& gps) {
    set_impl_generics_raw(self_meta_type, gps);
    return NullOnDrop<const ::HIR::GenericParams>(implGenerics);
}
NullOnDrop<const ::HIR::GenericParams> StaticTraitResolve::set_impl_generics(const ::HIR::TypeData* self_ty, const ::HIR::GenericParams& gps) {
    set_impl_generics_raw(MetadataType::Unknown, gps);
    selfMetadata = metadataType(Span(), self_ty);
    return NullOnDrop<const ::HIR::GenericParams>(implGenerics);
}
void StaticTraitResolve::update_impl_self_metadata(const ::HIR::TypeData* self_ty) {
    assert(implGenerics);
    selfMetadata = metadataType(Span(), self_ty);
}
NullOnDrop<const ::HIR::GenericParams> StaticTraitResolve::set_item_generics(const ::HIR::GenericParams& gps) {
    set_item_generics_raw(gps);
    return NullOnDrop<const ::HIR::GenericParams>(itemGenerics);
}
void StaticTraitResolve::set_impl_generics_raw(MetadataType self_meta_type, const ::HIR::GenericParams& gps) {
    assert(!implGenerics);
    selfMetadata = self_meta_type;
    implGenerics = &gps;
    prepIndexes();
}
void StaticTraitResolve::clearImplGenerics() {
    selfMetadata = MetadataType::Unknown;
    implGenerics = nullptr;
    prepIndexes();
}
void StaticTraitResolve::set_item_generics_raw(const ::HIR::GenericParams& gps) {
    assert(!itemGenerics);
    itemGenerics = &gps;
    prepIndexes();
}
void StaticTraitResolve::clearItemGenerics() {
    itemGenerics = nullptr;
    prepIndexes();
}
void StaticTraitResolve::set_both_generics_raw(const ::HIR::GenericParams* gpsImpl, const ::HIR::GenericParams* gpsFcn) {
    assert(!implGenerics);
    assert(!itemGenerics);
    implGenerics = gpsImpl;
    itemGenerics = gpsFcn;
    prepIndexes();
}
void StaticTraitResolve::clearBothGenerics() {
    selfMetadata = MetadataType::Unknown;
    implGenerics = nullptr;
    itemGenerics = nullptr;
    prepIndexes();
}
// Helper: Run monomorphise+EAT if the type contains generics
const ::HIR::TypeData* StaticTraitResolve::monomorphExpandOpt(const Span& sp, ::HIR::TypeRef& tmp, const ::HIR::TypeData* input, const Monomorphiser& m) const {
    if (monomorphiseTypeNeeded(input)) {
        return tmp = monomorphExpand(sp, input, m);
    } else {
        return input;
    }
}
::HIR::TypeRef StaticTraitResolve::monomorphExpand(const Span& sp, const ::HIR::TypeData* input, const Monomorphiser& m) const {
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
