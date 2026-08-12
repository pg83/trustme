#include "hir_conv_main_bindings.h"
#include "hir_conv_main_bindings.h"

#include "hir_hir.h"
#include "mir_mir.h"
#include "hir_expr.h"
#include "hir_visitor.h"
#include "mir_helpers.h"
#include "hir_expr_state.h"
#include "hir_typeck_common.h" // monomorphise_type_with
#include "hir_typeck_static.h"
#include "hir_typeck_expr_visit.h" // For ModuleState

#include <std/mem/obj_pool.h>

#include <algorithm> // std::find_if

void ConvertHIRBind(HIRCrate& crate);

namespace {

    enum class Target {
        TypeItem,
        Struct,
        Enum,
        EnumVariant,
    };

    const void* getTypePointer(const Span& sp, const HIRCrate& crate, const HIRSimplePath& path, Target t) {
        if (t == Target::EnumVariant) {
            return &crate.getTypeitemByPath(sp, path, /*ignore_crate_name=*/false, /*ignore_last_node=*/true).as_Enum();
        } else {
            const auto& ti = crate.getTypeitemByPath(sp, path);
            switch (t) {
                case Target::TypeItem:
                    return &ti;
                case Target::EnumVariant:
                    throw "";

                case Target::Struct:
                    TU_IFLET(HIRTypeItem, ti, Struct, e2, return &e2;)
                    else {
                        ERROR(sp, E0000, "Expected a struct at " << path << ", got a " << ti.tagStr());
                    }
                    break;
                case Target::Enum:
                    TU_IFLET(HIRTypeItem, ti, Enum, e2, return &e2;)
                    else {
                        ERROR(sp, E0000, "Expected a enum at " << path << ", got a " << ti.tagStr());
                    }
                    break;
            }
            throw "";
        }
    }

    void fixTypeParams(HIRTypeInterner& types, const Span& sp, const HIRGenericParams& paramsDef, HIRPathParams& params) {
        if (params.mLifetimes.size() == 0) {
            params.mLifetimes.resize(paramsDef.mLifetimes.size());
        }
        if (params.mLifetimes.size() != paramsDef.mLifetimes.size()) {
            ERROR(sp, E0000, "Incorrect lifetime param count, expected " << paramsDef.mLifetimes.size() << ", got " << params.mLifetimes.size());
        }

        if (params.types.size() == 0) {
            while (params.types.size() < paramsDef.types.size()) {
                params.types.push_back(types.infer());
            }
            // TODO: Optionally fill in the defaults?
        }
        if (params.types.size() != paramsDef.types.size()) {
            ERROR(sp, E0000, "Incorrect parameter count, expected " << paramsDef.types.size() << ", got " << params.types.size());
        }

        if (params.values.size() == 0) {
            params.values.resize(paramsDef.values.size());
        }
        if (params.values.size() != paramsDef.values.size()) {
            ERROR(sp, E0000, "Incorrect value parameter count, expected " << paramsDef.values.size() << ", got " << params.values.size());
        }
    }

    void fixParamCount(HIRTypeInterner& types, const Span& sp, const HIRGenericPath& path, const HIRGenericParams& paramDefs, HIRPathParams& params, bool fillInfer = true, const HIRTypeData* selfTy = nullptr) {
        TRACE_FUNCTION_FR(paramDefs.fmtArgs() << " -> " << params << " (fill_infer=" << fillInfer << ")", params);
        if (params.mLifetimes.size() != paramDefs.mLifetimes.size()) {
            if (params.mLifetimes.size() == 0 && fillInfer) {
                params.mLifetimes.resize(paramDefs.mLifetimes.size());
            }
        }
        if (params.types.size() != paramDefs.types.size()) {
            TRACE_FUNCTION_FR(path, params);

            if (params.types.size() == 0 && fillInfer) {
                while (params.types.size() < paramDefs.types.size()) {
                    params.types.push_back(types.infer());
                }
            } else if (params.types.size() > paramDefs.types.size()) {
                ERROR(sp, E0000, "Too many type parameters passed to " << path);
            } else {
                while (params.types.size() < paramDefs.types.size()) {
                    const auto& typ = paramDefs.types[params.types.size()];
                    if (typ.defaultValue->is_Infer()) {
                        ERROR(sp, E0000, "Omitted type parameter with no default in " << path);
                    } else {
                        // TODO: Does expanding defaults need a custom monomorphiser that can handle later defaults?
                        MonomorphStatePtr ms(types, selfTy, &params, nullptr);
                        auto ty = ms.monomorphType(sp, typ.defaultValue);
                        params.types.push_back(mv$(ty));
                    }
                }
            }
        }
        if (params.values.size() != paramDefs.values.size()) {
            if (params.values.size() == 0 && fillInfer) {
                params.values.resize(paramDefs.values.size());
            } else if (params.values.size() > paramDefs.values.size()) {
                ERROR(sp, E0000, "Too many value parameters passed to " << path);
            } else {
                while (params.values.size() < paramDefs.values.size()) {
                    const auto& val = paramDefs.values[params.values.size()];
                    if (val.defaultValue.is_Infer()) {
                        ERROR(sp, E0000, "Omitted value parameter with no default in " << path);
                    } else {
                        // TODO: Anything to be worried about with Unevaluated?, it may not have had its params set yet
                        params.values.push_back(val.defaultValue.clone());
                    }
                }
            }
        }
    }

    class BindVisitor: public HIRVisitor {
        const HIRCrate& crate;

        TypeckModuleState ms;

        struct CurMod {
            const HIRModule* ptr;
            const HIRItemPath* path;
        } curModule;

        unsigned inExpr;

        HIRItemPath* fcnPath = nullptr;
        HIRFunction* fcnPtr = nullptr;
        unsigned int fcnErasedCount = 0;

    public:
        BindVisitor(const HIRCrate& crate)
            : HIRVisitor(nullptr, crate.types)
            , crate(crate)
            , ms(crate)
            , inExpr(0)
        {
            static HIRItemPath rootPath("");
            curModule.ptr = &crate.mRootModule;
            curModule.path = &rootPath;
        }

        HIRTypeInterner& interner() const {
            return crate.types;
        }

        void visitModule(HIRItemPath p, HIRModule& mod) override {
            auto parentMod = curModule;
            curModule.ptr = &mod;
            curModule.path = &p;

            ms.pushTraits(p, mod);
            HIRVisitor::visitModule(p, mod);
            ms.popTraits(mod);

            curModule = parentMod;
        }

        void visitTraitPath(HIRTraitPath& p) override {
            static Span sp;
            p.traitPtr = &crate.getTraitByPath(sp, p.mPath.mPath);

            HIRVisitor::visitTraitPath(p);
        }

        void visitLiteral(const Span& sp, EncodedLiteral& lit) {
            for (auto& r : lit.relocations) {
                if (r.p) {
                    visitPath(*r.p, HIRVisitor::PathContext::VALUE);
                }
            }
        }

        void visitPatternValue(const Span& sp, HIRPattern& pat, HIRPattern::Value& val) {
            bool isSingleValue = pat.mData.is_Value();

            if (auto* ve = val.opt_Named()) {
                if (auto* pe = ve->path.mData.opt_Generic()) {
                    const auto& path = pe->mPath;
                    const auto& pc = path.components().back();
                    const HIRModule* mod = nullptr;
                    if (path.components().size() == 1) {
                        mod = &crate.getModByPath(sp, path, true);
                    } else {
                        const auto& ti = crate.getTypeitemByPath(sp, path, /*ignore_crate_name=*/false, /*ignore_last_node=*/true);
                        if (const auto& enm = ti.opt_Enum()) {
                            if (!isSingleValue) {
                                ERROR(sp, E0000, "Enum variant in range pattern - " << pat);
                            }

                            // Enum variant
                            auto idx = enm->findVariant(pc);
                            if (idx == SIZE_MAX) {
                                BUG(sp, "'" << pc << "' isn't a variant in path " << path);
                            }
                            HIRGenericPath path = std::move(*pe);
                            fixTypeParams(crate.types, sp, enm->mParams, path.mParams);
                            pat.mData = HIRPattern::Data::make_PathValue({mv$(path), HIRPattern::PathBinding::make_Enum({enm, static_cast<unsigned>(idx)})});
                        } else if ((mod = ti.opt_Module())) {
                            mod = &ti.as_Module();
                        } else {
                            BUG(sp, "Node " << path.components().size() - 2 << " of path " << ve->path << " wasn't a module");
                        }
                    }

                    if (mod) {
                        auto it = mod->valueItems.find(path.components().back());
                        if (it == mod->valueItems.end()) {
                            BUG(sp, "Couldn't find final component of " << path);
                        }
                        // Unit-like struct match or a constant
                        TU_MATCH_HDRA( (it->second->ent), { )
                        default:
                            ERROR(sp, E0000, "Value pattern " << pat << " pointing to unexpected item type - " << it->second->ent.tagStr());
                            TU_ARMA(Constant, e2) {
                                // Store reference to this item for later use
                                ve->binding = &e2;
                            }
                            TU_ARMA(StructConstant, e2) {
                                const auto& str = mod->modItems.find(pc)->second->ent.as_Struct();
                                // Convert into a dedicated pattern type
                                if (!isSingleValue) {
                                    ERROR(sp, E0000, "Struct in range pattern - " << pat);
                                }
                                auto path = mv$(*pe);
                                fixTypeParams(crate.types, sp, str.mParams, path.mParams);
                                pat.mData = HIRPattern::Data::make_PathValue({mv$(path), &str});
                            }
                        }
                    }
                } else {
                    // NOTE: Defer until Resolve UFCS (saves duplicating logic)
                }
            }
        }

        void visitPattern(HIRPattern& pat) override {
            static Span _sp = Span();
            const Span& sp = _sp;

            HIRVisitor::visitPattern(pat);

            TU_MATCH_HDRA( (pat.mData), {)
            default:
                // Nothing
            TU_ARMA(Value, e) {
                    this->visitPatternValue(sp, pat, e.val);
                }
                TU_ARMA(Range, e) {
                    if (e.start) {
                        this->visitPatternValue(sp, pat, *e.start);
                    }
                    if (e.end) {
                        this->visitPatternValue(sp, pat, *e.end);
                    }
                }
                TU_ARMA(PathValue, e) {
                }
                TU_ARMA(PathTuple, e) {
                }
                TU_ARMA(PathNamed, e) {
                }
            }
        }

        void visitConstgeneric(HIRConstGeneric& value) override {
            HIRVisitor::visitConstgeneric(value);
            if (auto* unevaluated = value.opt_Unevaluated()) {
                if (ms.mImplGenerics) {
                    (*unevaluated)->paramsImpl = ms.mImplGenerics->makeNopParams(crate.types, 0);
                }
                if (ms.mItemGenerics) {
                    (*unevaluated)->paramsItem = ms.mItemGenerics->makeNopParams(crate.types, 1);
                }
            }
        }

        void visitParams(HIRGenericParams& params) override {
            static Span sp;
            for (auto& bound : params.bounds) {
                if (auto* be = bound.opt_TraitBound()) {
                    {
                        const auto& trait = crate.getTraitByPath(sp, be->trait.mPath.mPath);
                        fixParamCount(crate.types, sp, be->trait.mPath, trait.mParams, be->trait.mPath.mParams, /*fill_infer=*/false, be->type);
                    }
                    // Also ensure that the defaults are filled in the source traits
                    // - Is there a better solution to this? It feels like it would give the wrong answer (filling defaults incorrectly)
                    for (auto& aty : be->trait.typeBounds) {
                        const auto& trait = crate.getTraitByPath(sp, aty.second.sourceTrait.mPath);
                        fixParamCount(crate.types, sp, be->trait.mPath, trait.mParams, aty.second.sourceTrait.mParams, /*fill_infer=*/false, be->type);
                    }
                    for (auto& aty : be->trait.typeBounds) {
                        const auto& trait = crate.getTraitByPath(sp, aty.second.sourceTrait.mPath);
                        fixParamCount(crate.types, sp, be->trait.mPath, trait.mParams, aty.second.sourceTrait.mParams, /*fill_infer=*/false, be->type);
                    }
                }
            }

            HIRVisitor::visitParams(params);
        }

        void visitAssociatedtype(HIRItemPath p, HIRAssociatedType& item) override {
            static Span sp;
            HIRVisitor::visitAssociatedtype(p, item);
            HIRTypeRef ty = crate.types.path(p.getFullPath(), {});
            for (auto& bound : item.traitBounds) {
                const auto& trait = crate.getTraitByPath(sp, bound.mPath.mPath);
                fixParamCount(crate.types, sp, bound.mPath, trait.mParams, bound.mPath.mParams, /*fill_infer=*/false, ty);
            }
        }

        void visitType(HIRTypeRef& ty) override {
            visitTypeInner(ty);
        }

        void visitTypeInner(HIRTypeRef& ty, bool doBind = true) {
            static Span sp;
            auto data = ty->cloneData();
            bool dataVisited = false;

            if (auto* e = data.opt_Path()) {
                TU_MATCH_HDRA( (e->path.mData), {)
                TU_ARMA(Generic, pe) {
                        if (!doBind) {
                            break;
                        }
                        const auto& item = *reinterpret_cast<const HIRTypeItem*>(getTypePointer(sp, crate, pe.mPath, Target::TypeItem));
                        TU_MATCH_DEF(
                            HIRTypeItem,
                            (item),
                            (e3),
                            (ERROR(sp, E0000, "Unexpected item type returned for " << pe.mPath << " - " << item.tagStr());),
                            (
                                TypeAlias, BUG(sp, "TypeAlias encountered after `Resolve Type Aliases` - " << ty);
                                // Assume it'll be filled out, with the correct binding
                            ),
                            (ExternType, e->binding = HIRTypePathBinding::make_ExternType(&e3); DEBUG("- " << ty);),
                            (Struct, fixParamCount(crate.types, sp, pe, e3.mParams, pe.mParams, /*fill_infer=*/inExpr != 0); e->binding = HIRTypePathBinding::make_Struct(&e3); DEBUG("- " << ty);),
                            (Union, fixParamCount(crate.types, sp, pe, e3.mParams, pe.mParams, /*fill_infer=*/inExpr != 0); e->binding = HIRTypePathBinding::make_Union(&e3); DEBUG("- " << ty);),
                            (Enum, fixParamCount(crate.types, sp, pe, e3.mParams, pe.mParams, /*fill_infer=*/inExpr != 0); e->binding = HIRTypePathBinding::make_Enum(&e3); DEBUG("- " << ty);),
                            (Trait,
                             // TODO: Should this reassign instead?
                             data = HIRTypeData::make_TraitObject({HIRTraitPath{{}, mv$(pe), {}, {}}, {}, {}});)
                        )
                    }
                    TU_ARMA(UfcsUnknown, pe) {
                        //TODO(sp, "Should UfcsKnown be encountered here?");
                    }
                    TU_ARMA(UfcsInherent, pe) {
                    }
                    TU_ARMA(UfcsKnown, pe) {
                        const auto& trait = crate.getTraitByPath(sp, pe.trait.mPath);
                        fixParamCount(crate.types, sp, pe.trait, trait.mParams, pe.trait.mParams, /*fill_infer=*/false, pe.type);

                        if (pe.type->is_Path() && pe.type->as_Path().binding.is_Opaque()) {
                            // - Opaque type, opaque result
                            e->binding = HIRTypePathBinding::make_Opaque({});
                        } else if (pe.type->is_Generic()) {
                            // - Generic type, opaque resut. (TODO: Sometimes these are known - via generic bounds)
                            e->binding = HIRTypePathBinding::make_Opaque({});
                        } else {
                            //    DEBUG("TODO");
                            //}
                            //TODO(sp, "Resolve known UfcsKnown - " << ty);
                        }
                    }
                }
            } else if (auto* te = data.opt_ErasedType()) {
                HIRTypeRef tyEself = crate.types.generic("ErasedSelf", GENERICErasedSelf);
                for (auto& t : te->traits) {
                    const auto& trait = crate.getTraitByPath(sp, t.mPath.mPath);
                    fixParamCount(crate.types, sp, t.mPath, trait.mParams, t.mPath.mParams, /*fill_infer=*/inExpr, tyEself);
                }

                if (auto* ee = te->inner.opt_Fcn()) {
                    DEBUG("Set origin of ErasedType - " << ty);
                    // If not, figure out what to do with it

                    // If the function path is set, we're processing the return type of a function
                    // - Add this to the list of erased types associated with the function
                    if (ee->origin != HIRSimplePath()) {
                        // Already set, somehow (maybe we're visiting the function after expansion)
                    } else if (fcnPath) {
                        assert(fcnPtr);
                        DEBUG(*fcnPath << " " << fcnErasedCount);

                        HIRPathParams params = fcnPtr->mParams.makeNopParams(crate.types, 1);
                        // Populate with function path
                        ee->origin = fcnPath->getFullPath();
                        TU_MATCH_HDRA( (ee->origin.mData), {)
                        TU_ARMA(Generic, e2) {
                                e2.mParams = mv$(params);
                            }
                            TU_ARMA(UfcsInherent, e2) {
                                e2.params = mv$(params);
                                // Impl params, just directly references the parameters.
                                // - Downstream monomorph will fix that
                                e2.implParams = ms.mImplGenerics->makeNopParams(crate.types, 0);
                            }
                            TU_ARMA(UfcsKnown, e2) {
                                e2.params = mv$(params);
                            }
                            TU_ARMA(UfcsUnknown, e2) {
                                throw "";
                            }
                        }
                        ee->index = fcnErasedCount++;
                    }
                    // If the function _pointer_ is set (but not the path), then we're in the function arguments
                    // - Add a un-namable generic parameter (TODO: Prevent this from being explicitly set when called)
                    else if (fcnPtr) {
                        // Visit inner first, to handle nested
                        visitTypeData(data);
                        dataVisited = true;

                        size_t idx = fcnPtr->mParams.types.size();
                        auto name = RcString::newInterned(FMT("erased$" << idx));
                        DEBUG("-> " << name);
                        auto newTy = crate.types.generic(name, 256 + idx);
                        fcnPtr->mParams.types.push_back({name, crate.types.infer(), te->isSized});
                        for (auto& trait : te->traits) {
                            struct M: MonomorphiserNop {
                                const HIRTypeData* newTy;

                                M(HIRTypeInterner& types, const HIRTypeData* ty)
                                    : MonomorphiserNop(types)
                                    , newTy(ty)
                                {
                                }

                                HIRTypeRef getType(const Span& sp, const HIRGenericRef& ty) const override {
                                    if (ty.binding == GENERICErasedSelf) {
                                        return newTy;
                                    }
                                    return types.generic(ty.name, ty.binding);
                                }
                            } m{crate.types, newTy};

                            // TODO: Monomorph the trait to replace `Self` with this generic?
                            // - Except, that should it be?
                            fcnPtr->mParams.bounds.push_back(HIRGenericBound::make_TraitBound({nullptr, newTy, m.monomorphTraitpath(sp, trait, false)}));
                        }
                        for (const auto& lft : te->lifetimeBounds) {
                            fcnPtr->mParams.bounds.push_back(HIRGenericBound::make_TypeLifetime({newTy, lft}));
                        }
                        ty = ::std::move(newTy);
                        return;
                    } else {
                        // TODO: If we're in a top-level `type`, then it must be used as the return type of a function.
                        // https://rust-lang.github.io/rfcs/2515-type_alias_impl_trait.html#type-alias
                        ERROR(sp, E0000, "Use of an erased type outside of a function return - " << ty);
                    }
                }
            } else if (auto* te = data.opt_TraitObject()) {
                if (te->mTrait.mPath.mPath != HIRSimplePath()) {
                    const auto& trait = crate.getTraitByPath(sp, te->mTrait.mPath.mPath);
                    fixParamCount(crate.types, sp, te->mTrait.mPath, trait.mParams, te->mTrait.mPath.mParams, /*fill_infer=*/inExpr, nullptr);
                }
                for (auto& m : te->markers) {
                    const auto& trait = crate.getTraitByPath(sp, m.mPath);
                    fixParamCount(crate.types, sp, m, trait.mParams, m.mParams, /*fill_infer=*/inExpr, nullptr);
                }
                DEBUG("- " << ty);
            }

            if (!dataVisited) {
                visitTypeData(data);
            }
            ty = crate.types.intern(mv$(data));
        }

        void visitTypeImpl(HIRTypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.mType << " - from " << impl.srcModule);
            auto _ = this->ms.setImplGenerics(impl.mParams);

            auto modIp = HIRItemPath(impl.srcModule);
            const auto* mod = (impl.srcModule != HIRSimplePath() ? &this->ms.crate.getModByPath(Span(), impl.srcModule) : nullptr);
            if (mod) {
                ms.pushTraits(impl.srcModule, *mod);
                curModule.ptr = mod;
                curModule.path = &modIp;
            }
            HIRVisitor::visitTypeImpl(impl);
            if (mod) {
                ms.popTraits(*mod);
            }
        }

        void visitInherentType(HIRItemPath p, HIRTypeAlias& item) override {
            auto _ = this->ms.setItemGenerics(item.mParams);
            HIRVisitor::visitInherentType(p, item);
        }

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
            TRACE_FUNCTION_F("impl " << traitPath << " for " << impl.mType);
            auto traitGpath = HIRGenericPath(traitPath, impl.traitArgs.clone());
            auto _0 = this->ms.setCurrentTraitImpl(impl);
            auto _1 = this->ms.setCurrentTrait(traitGpath);
            auto _ = this->ms.setImplGenerics(impl.mParams);

            auto modIp = HIRItemPath(impl.srcModule);
            const auto* mod = (impl.srcModule != HIRSimplePath() ? &this->ms.crate.getModByPath(Span(), impl.srcModule) : nullptr);
            if (mod) {
                ms.pushTraits(impl.srcModule, *mod);
                curModule.ptr = mod;
                curModule.path = &modIp;
            }
            ms.traits.push_back(::std::make_pair(&traitPath, &this->ms.crate.getTraitByPath(Span(), traitPath)));
            HIRVisitor::visitTraitImpl(traitPath, impl);
            ms.traits.pop_back();
            if (mod) {
                ms.popTraits(*mod);
            }
        }

        void visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) override {
            TRACE_FUNCTION_F("impl " << traitPath << " for " << impl.mType << " { }");
            auto _ = this->ms.setImplGenerics(impl.mParams);

            auto modIp = HIRItemPath(impl.srcModule);
            const auto* mod = (impl.srcModule != HIRSimplePath() ? &this->ms.crate.getModByPath(Span(), impl.srcModule) : nullptr);
            if (mod) {
                ms.pushTraits(impl.srcModule, *mod);
                curModule.ptr = mod;
                curModule.path = &modIp;
            }
            HIRVisitor::visitMarkerImpl(traitPath, impl);
            if (mod) {
                ms.popTraits(*mod);
            }
        }

        void visitTrait(HIRItemPath p, HIRTrait& item) override {
            auto _ = this->ms.setImplGenerics(item.mParams);
            HIRVisitor::visitTrait(p, item);
        }

        void visitEnum(HIRItemPath p, HIREnum& item) override {
            auto _ = this->ms.setImplGenerics(item.mParams);
            HIRVisitor::visitEnum(p, item);
        }

        void visitStruct(HIRItemPath p, HIRStruct& item) override {
            auto _ = this->ms.setImplGenerics(item.mParams);
            HIRVisitor::visitStruct(p, item);
        }

        void visitUnion(HIRItemPath p, HIRUnion& item) override {
            auto _ = this->ms.setImplGenerics(item.mParams);
            HIRVisitor::visitUnion(p, item);
        }

        void visitFunction(HIRItemPath p, HIRFunction& item) override {
            auto _ = this->ms.setItemGenerics(item.mParams);
            fcnPtr = &item;

            // Visit arguments
            // - Used to convert `impl Trait` in argument position into generics
            // - Done first so the path in return-position `impl Trait` is valid
            for (auto& arg : item.mArgs) {
                TRACE_FUNCTION_F("ARG " << arg);
                visitType(arg.second);
            }

            // Visit return type (populates path for `impl Trait` in return position
            fcnPath = &p;
            fcnErasedCount = 0;
            {
                TRACE_FUNCTION_F("RET " << item.returnType);
                visitType(item.returnType);
            }
            fcnPath = nullptr;
            fcnPtr = nullptr;

            HIRVisitor::visitFunction(p, item);
        }

        void visitStatic(HIRItemPath p, HIRStatic& item) override {
            HIRVisitor::visitStatic(p, item);
            visitLiteral(Span(), item.valueRes);
        }

        void visitConstant(HIRItemPath p, HIRConstant& item) override {
            auto _ = this->ms.setItemGenerics(item.mParams);
            HIRVisitor::visitConstant(p, item);
            visitLiteral(Span(), item.valueRes);
        }

        // Actual expressions
        void visitExpr(HIRExprPtr& expr) override {
            struct ExprVisitor: public HIRExprVisitorDef {
                BindVisitor& upperVisitor;

                ExprVisitor(BindVisitor& uv)
                    : HIRExprVisitorDef(uv.interner())
                    , upperVisitor(uv)
                {
                }

                void visitGenericPath(HIRVisitor::PathContext pc, HIRGenericPath& p) override {
                    upperVisitor.visitGenericPath(p, pc);
                }

                void visitType(HIRTypeRef& ty) override {
                    upperVisitor.visitTypeInner(ty, true);
                }

                void visitNodePtr(HIRExprNodeP& nodePtr) override {
                    upperVisitor.visitType(nodePtr->resType);
                    HIRExprVisitorDef::visitNodePtr(nodePtr);
                }

                void visit(HIRExprNodeLet& node) override {
                    upperVisitor.visitType(node.mType);
                    upperVisitor.visitPattern(node.pattern);
                    HIRExprVisitorDef::visit(node);
                }

                void visit(HIRExprNodeMatch& node) override {
                    for (auto& arm : node.arms) {
                        for (auto& pat : arm.patterns) {
                            upperVisitor.visitPattern(pat);
                        }
                        for (auto& g : arm.guards) {
                            upperVisitor.visitPattern(g.pat);
                        }
                    }
                    HIRExprVisitorDef::visit(node);
                }

                void visit(HIRExprNodePathValue& node) override {
                    upperVisitor.visitPath(node.mPath, HIRVisitor::PathContext::VALUE);
                }

                void visit(HIRExprNodeCallPath& node) override {
                    upperVisitor.visitPath(node.mPath, HIRVisitor::PathContext::VALUE);
                    HIRExprVisitorDef::visit(node);

                    // #[rustc_legacy_const_generics] - A backwards compatability hack added between 1.39 and 1.54 to be backwards compatible with the x86 intrinsics
                    // - Rewrites some literal arguments into const generics
                    if (auto* e = node.mPath.mData.opt_Generic()) {
                        auto& fcn = upperVisitor.crate.getFunctionByPath(node.span(), e->mPath);
                        if (!fcn.markings.rustcLegacyConstGenerics.empty()) {
                            if (node.mArgs.size() == fcn.mArgs.size()) {
                                // Acceptable
                            } else if (node.mArgs.size() == fcn.mArgs.size() + fcn.markings.rustcLegacyConstGenerics.size()) {
                                for (auto idx : fcn.markings.rustcLegacyConstGenerics) {
                                    auto& argNode = node.mArgs.at(idx);
                                    assert(argNode);
                                    // TODO: Check that the expression is a valid const (no locals referenced, no function calls?)
                                    // - Allow: Arithmatic, casts, literals
                                    //if( !cast<const HIR::ExprNodeLiteral>(arg_node.get()) )
                                    HIRExprPtr ep{std::move(argNode)};
                                    e->mParams.values.push_back(HIRConstGeneric(std::make_unique<HIRConstGenericUnevaluated>(std::move(ep))));
                                    // - Visit to ensure that the expr state gets filled
                                    upperVisitor.visitConstgeneric(e->mParams.values.back());
                                }
                                auto newEnd = std::remove_if(node.mArgs.begin(), node.mArgs.end(), [](const HIRExprNodeP& np) {
                                    return !np;
                                });
                                node.mArgs.erase(newEnd, node.mArgs.end());
                            } else {
                                // Will error downstream
                            }
                        }
                    }
                }

                void visit(HIRExprNodeCallMethod& node) override {
                    upperVisitor.visitPathParams(node.mParams);
                    HIRExprVisitorDef::visit(node);
                }

                void visit(HIRExprNodeStructLiteral& node) override {
                    upperVisitor.visitTypeInner(node.mType, false);

                    HIRExprVisitorDef::visit(node);
                }

                void visit(HIRExprNodeArraySized& node) override {
                    auto& as = node.mSize;
                    if (as.is_Unevaluated()) {
                        upperVisitor.visitConstgeneric(as.as_Unevaluated());
                    }
                    HIRExprVisitorDef::visit(node);
                }

                void visit(HIRExprNodeClosure& node) override {
                    upperVisitor.visitType(node.returnType);
                    for (auto& arg : node.mArgs) {
                        upperVisitor.visitPattern(arg.first);
                        upperVisitor.visitType(arg.second);
                    }
                    HIRExprVisitorDef::visit(node);
                }
            };

            for (auto& ty : expr.erasedTypes) {
                visitType(ty);
            }

            // Set up the module state
            {
                expr.state = HIRExprStatePtr(crate.pool, HIRExprState(crate.types, *curModule.ptr, curModule.path->getSimplePath()));
                expr.state->traits = ms.traits; // TODO: Only obtain the current module's set
                expr.state->mImplGenerics = ms.mImplGenerics;
                expr.state->mItemGenerics = ms.mItemGenerics;
                expr.state->currentTraitImpl = ms.currentTraitImpl;
                if (ms.currentTrait) {
                    expr.state->mCurrentTraitPath = ms.currentTrait->mPath;
                }
            }

            // Local expression
            if (expr.get() != nullptr) {
                // TODO: Disable type param defaults for this scope
                this->inExpr++;

                ExprVisitor v{*this};
                (*expr).visit(v);

                this->inExpr--;
            }
            // External expression (has MIR)
            else if (auto* mir = expr.getExtMirMut()) {
                for (auto& ty : mir->locals) {
                    this->visitType(ty);
                }

                struct MirVisitor: public MIRVisitorMut {
                    BindVisitor& upperVisitor;

                    MirVisitor(BindVisitor& upperVisitor)
                        : upperVisitor(upperVisitor)
                    {
                    }

                    void visitType(HIRTypeRef& t) override {
                        upperVisitor.visitType(t);
                    }

                    void visitPath(HIRPath& p) override {
                        upperVisitor.visitPath(p, HIRVisitor::PathContext::VALUE);
                    }

                    bool visitLvalue(MIRLValue& lv, MIRValUsage u) override {
                        if (lv.root.is_Static()) {
                            upperVisitor.visitPath(lv.root.as_Static(), HIRVisitor::PathContext::VALUE);
                        }
                        return false;
                    }
                };

                MirVisitor mv(*this);
                for (auto& block : mir->blocks) {
                    for (auto& stmt : block.statements) {
                        mv.visitStmt(stmt);
                    }
                    mv.visitTerminator(block.terminator);
                }
            } else {
            }
        }
    };

    class VisitorEnumSuperTraits: public HIRVisitor {
        const HIRCrate& crate;

    public:
        VisitorEnumSuperTraits(const HIRCrate& crate)
            : HIRVisitor(nullptr, crate.types)
            , crate(crate)
        {
        }

        void visitTrait(HIRItemPath ip, HIRTrait& tr) override {
            static Span sp;
            TRACE_FUNCTION_F(ip);
            const auto tySelf = crate.types.self();

            // Enumerate supertraits and save for later stages
            struct Enumerate {
                HIRTypeInterner& types;
                HIRTypeRef tySelf;
                ::std::vector<HIRTraitPath> supertraits;
                ::std::vector<const HIRTraitPath*> tpStack;

                Enumerate(HIRTypeInterner& types, HIRTypeRef tySelf)
                    : types(types)
                    , tySelf(tySelf)
                {
                }

                void enumSupertraitsIn(const HIRTrait& tr, HIRTraitPath path) {
                    TRACE_FUNCTION_F(path);
                    tpStack.push_back(&path);
                    auto& params = path.mPath.mParams;

                    // Fill defaulted parameters.
                    // NOTE: Doesn't do much error checking.
                    fixParamCount(types, sp, path.mPath, tr.mParams, path.mPath.mParams, false, tySelf);

                    auto monomorphCb = MonomorphStatePtr(types, tySelf, &params, nullptr);
                    auto monomorphTp = [&](const HIRTraitPath& tp) -> HIRTraitPath {
                        // TODO: if `path.m_path` has HRLs, then this needs HRLs (only if the HRLs get used?)
                        if ((tp.hrtbs && !tp.hrtbs->isEmpty()) && (path.hrtbs && !path.hrtbs->isEmpty())) {
                            // TODO: How to determine which to use?
                            // - May need to combine them.
                            TODO(sp, "Trait path and outer path both have HRLs, how to handle?");
                            return monomorphCb.monomorphTraitpath(sp, tp, false);
                        } else if (path.hrtbs && !path.hrtbs->isEmpty()) {
                            auto rv = monomorphCb.monomorphTraitpath(sp, tp, false);
                            rv.hrtbs = box$(path.hrtbs->clone());
                            return rv;
                        } else {
                            return monomorphCb.monomorphTraitpath(sp, tp, false);
                        }
                    };
                    if (tr.allParentTraits.size() > 0) {
                        for (const auto& pt : tr.allParentTraits) {
                            supertraits.push_back(monomorphTp(pt));
                            fillTypeAliases(supertraits.back());
                        }
                    } else {
                        // Recurse into parent traits
                        for (const auto& pt : tr.parentTraits) {
                            enumSupertraitsIn(*pt.traitPtr, monomorphTp(pt));
                        }
                        // - Bound parent traits
                        for (const auto& b : tr.mParams.bounds) {
                            if (!b.is_TraitBound()) {
                                continue;
                            }
                            const auto& be = b.as_TraitBound();
                            if (be.type != tySelf) {
                                continue;
                            }
                            const auto& pt = be.trait;
                            if (pt.mPath.mPath == path.mPath.mPath) {
                                continue;
                            }

                            enumSupertraitsIn(*pt.traitPtr, monomorphTp(pt));
                        }
                    }

                    // Build output path.
                    HIRTraitPath outPath;
                    outPath.hrtbs = mv$(path.hrtbs);
                    outPath.mPath = mv$(path.mPath);
                    outPath.traitPtr = &tr;
                    fillTypeAliases(outPath);
                    // TODO: HRLs?
                    supertraits.push_back(std::move(outPath));
                    // Fill aliases from this path too
                    for (auto& st : supertraits) {
                        for (auto& tb : path.typeBounds) {
                            if (tb.second.sourceTrait == st.mPath) {
                                DEBUG("Add TypeBound: " << tb.first << " = " << tb.second.type);
                                st.typeBounds.insert(std::make_pair(tb.first, std::move(tb.second)));
                            }
                        }
                        for (auto& tb : path.traitBounds) {
                            if (tb.second.sourceTrait == st.mPath) {
                                DEBUG("Add TraitBound: " << tb.first << ": " << tb.second.traits);
                                st.traitBounds.insert(std::make_pair(tb.first, std::move(tb.second)));
                            }
                        }
                    }
                    tpStack.pop_back();
                }

                void fillTypeAliases(HIRTraitPath& outPath) const {
                    const HIRTrait& tr = *outPath.traitPtr;
                    // - Locate associated types for this trait
                    for (const auto& ty : tr.types) {
                        if (outPath.typeBounds.count(ty.first) == 0) {
                            const HIRTypeData* found = nullptr;

                            for (auto oit = tpStack.rbegin(); oit != tpStack.rend(); ++oit) {
                                auto it = (*oit)->typeBounds.find(ty.first);
                                if (it != (*oit)->typeBounds.end()) {
                                    // TODO: Check the source trait
                                    found = it->second.type;
                                    break;
                                }
                            }
                            // TODO: What if there's multiple?
                            DEBUG(ty.first << " = " << found);

                            if (found) {
                                outPath.typeBounds.insert(::std::make_pair(ty.first, HIRTraitPath::AtyEqual{outPath.mPath.clone(), {}, found}));
                            }
                        }

                        if (outPath.traitBounds.count(ty.first) == 0) {
                            std::vector<HIRTraitPath> traits;
                            for (auto oit = tpStack.rbegin(); oit != tpStack.rend(); ++oit) {
                                auto it = (*oit)->traitBounds.find(ty.first);
                                if (it != (*oit)->traitBounds.end()) {
                                    // TODO: Check the source trait
                                    for (const auto& t : it->second.traits) {
                                        traits.push_back(t.clone());
                                    }
                                }
                            }
                            DEBUG(ty.first << ": " << traits);
                            if (!traits.empty()) {
                                outPath.traitBounds.insert(::std::make_pair(ty.first, HIRTraitPath::AtyBound{outPath.mPath.clone(), {}, mv$(traits)}));
                            }
                        }
                    }
                }
            };

            auto thisPath = ip.getSimplePath();
            thisPath.updateCrateName(crate.crateName);

            Enumerate e{crate.types, tySelf};
            for (const auto& pt : tr.parentTraits) {
                e.enumSupertraitsIn(*pt.traitPtr, pt.clone());
            }
            for (const auto& b : tr.mParams.bounds) {
                if (!b.is_TraitBound()) {
                    continue;
                }
                const auto& be = b.as_TraitBound();
                if (be.type != tySelf) {
                    continue;
                }
                const auto& pt = be.trait;

                // TODO: Remove this along with the from_ast.cpp hack
                if (pt.mPath.mPath == thisPath) {
                    // TODO: Should this restrict based on the parameters
                    continue;
                }

                e.enumSupertraitsIn(*be.trait.traitPtr, be.trait.clone());
            }

            ::std::sort(e.supertraits.begin(), e.supertraits.end());
            DEBUG("supertraits = " << e.supertraits);
            if (e.supertraits.size() > 0) {
                bool dedeupDone = false;
                auto prev = e.supertraits.begin();
                for (auto it = e.supertraits.begin() + 1; it != e.supertraits.end();) {
                    if (prev->mPath == it->mPath) {
                        DEBUG("MERGE:");
                        DEBUG("- " << *prev);
                        DEBUG("- " << *it);
                        for (auto& e : it->typeBounds) {
                            if (prev->typeBounds.count(e.first)) {
                                ASSERT_BUG(sp, prev->typeBounds[e.first].type == e.second.type, "TODO: Handle mismatched type bounds in merging supertrait ATY bounds: " << e.first << " =\n " << prev->typeBounds[e.first] << "\n " << e.second.type);
                            }
                            prev->typeBounds.insert(std::move(e));
                        }
                        for (auto& e : it->traitBounds) {
                            if (prev->traitBounds.count(e.first)) {
                                TODO(sp, "Merge trait bounds (and make sure to check the source trait)");
                            }
                            prev->traitBounds.insert(std::move(e));
                        }
                        DEBUG("= " << *prev);
                        it = e.supertraits.erase(it);
                        dedeupDone = true;
                    } else {
                        ++it;
                        ++prev;
                    }
                }
                if (dedeupDone) {
                    DEBUG("supertraits dd = " << e.supertraits);
                }
            }
            tr.allParentTraits = std::move(e.supertraits);
        }
    };

    class VisitorPost: public HIRVisitor {
        const HIRCrate& crate;

        TypeckModuleState ms;

    public:
        VisitorPost(const HIRCrate& crate)
            : HIRVisitor(nullptr, crate.types)
            , crate(crate)
            , ms(crate)
        {
        }

        HIRTypeInterner& interner() const {
            return crate.types;
        }

        void visitModule(HIRItemPath p, HIRModule& mod) override {
            ms.pushTraits(p, mod);
            HIRVisitor::visitModule(p, mod);
            ms.popTraits(mod);
        }

        void visitType(HIRTypeRef& ty) override {
            visitTypeInner(ty);
        }

        void visitTypeInner(HIRTypeRef& ty, bool doBind = true) {
            static Span sp;

            auto data = ty->cloneData();
            if (auto* te = data.opt_NamedFunction()) {
                if (te->def.is_Function() && te->def.as_Function() == nullptr) {
                    StaticTraitResolve resolve{crate};
                    resolve.setBothGenericsRaw(ms.mImplGenerics, ms.mItemGenerics);
                    MonomorphState unusedMs(crate.types);
                    const auto& v = resolve.getValue(sp, te->path, unusedMs, true);

                    TU_MATCH_HDRA( (v), {)
                    default:
                        TODO(sp, "Resolve external NamedFunction type - " << te->path << " : " << v.tagStr());
                        TU_ARMA(Function, e) {
                            te->def = e;
                        }
                        TU_ARMA(StructConstructor, e) {
                            te->def = e.s;
                        }
                        TU_ARMA(EnumConstructor, e) {
                            te->def = HIRTypeDataNamedFunctionTy::make_EnumConstructor({e.e, e.v});
                        }
                    }
                }
            }

            visitTypeData(data);
            ty = crate.types.intern(mv$(data));
        }

        void visitTypeImpl(HIRTypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.mType << " - from " << impl.srcModule);
            auto _ = this->ms.setImplGenerics(impl.mParams);

            const auto* mod = (impl.srcModule != HIRSimplePath() ? &this->ms.crate.getModByPath(Span(), impl.srcModule) : nullptr);
            if (mod) {
                ms.pushTraits(impl.srcModule, *mod);
            }
            HIRVisitor::visitTypeImpl(impl);
            if (mod) {
                ms.popTraits(*mod);
            }
        }

        void visitInherentType(HIRItemPath p, HIRTypeAlias& item) override {
            auto _ = this->ms.setItemGenerics(item.mParams);
            HIRVisitor::visitInherentType(p, item);
        }

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
            TRACE_FUNCTION_F("impl " << traitPath << " for " << impl.mType);
            auto _ = this->ms.setImplGenerics(impl.mParams);

            const auto* mod = (impl.srcModule != HIRSimplePath() ? &this->ms.crate.getModByPath(Span(), impl.srcModule) : nullptr);
            if (mod) {
                ms.pushTraits(impl.srcModule, *mod);
            }
            ms.traits.push_back(::std::make_pair(&traitPath, &this->ms.crate.getTraitByPath(Span(), traitPath)));
            HIRVisitor::visitTraitImpl(traitPath, impl);
            ms.traits.pop_back();
            if (mod) {
                ms.popTraits(*mod);
            }
        }

        void visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) override {
            TRACE_FUNCTION_F("impl " << traitPath << " for " << impl.mType << " { }");
            auto _ = this->ms.setImplGenerics(impl.mParams);

            const auto* mod = (impl.srcModule != HIRSimplePath() ? &this->ms.crate.getModByPath(Span(), impl.srcModule) : nullptr);
            if (mod) {
                ms.pushTraits(impl.srcModule, *mod);
            }
            HIRVisitor::visitMarkerImpl(traitPath, impl);
            if (mod) {
                ms.popTraits(*mod);
            }
        }

        void visitTrait(HIRItemPath p, HIRTrait& item) override {
            auto _ = this->ms.setImplGenerics(item.mParams);
            HIRVisitor::visitTrait(p, item);
        }

        void visitEnum(HIRItemPath p, HIREnum& item) override {
            auto _ = this->ms.setImplGenerics(item.mParams);
            HIRVisitor::visitEnum(p, item);
        }

        void visitStruct(HIRItemPath p, HIRStruct& item) override {
            auto _ = this->ms.setImplGenerics(item.mParams);
            HIRVisitor::visitStruct(p, item);
        }

        void visitUnion(HIRItemPath p, HIRUnion& item) override {
            auto _ = this->ms.setImplGenerics(item.mParams);
            HIRVisitor::visitUnion(p, item);
        }

        void visitFunction(HIRItemPath p, HIRFunction& item) override {
            auto _ = this->ms.setItemGenerics(item.mParams);
            HIRVisitor::visitFunction(p, item);
        }

        void visitStatic(HIRItemPath p, HIRStatic& item) override {
            HIRVisitor::visitStatic(p, item);
        }

        void visitConstant(HIRItemPath p, HIRConstant& item) override {
            auto _ = this->ms.setItemGenerics(item.mParams);
            HIRVisitor::visitConstant(p, item);
        }

        // Actual expressions
        void visitExpr(HIRExprPtr& expr) override {
            struct ExprVisitor: public HIRExprVisitorDef {
                VisitorPost& upperVisitor;

                ExprVisitor(VisitorPost& uv)
                    : HIRExprVisitorDef(uv.interner())
                    , upperVisitor(uv)
                {
                }

                void visitGenericPath(HIRVisitor::PathContext pc, HIRGenericPath& p) override {
                    upperVisitor.visitGenericPath(p, pc);
                }

                void visitType(HIRTypeRef& ty) override {
                    upperVisitor.visitTypeInner(ty, true);
                }

                void visitNodePtr(HIRExprNodeP& nodePtr) override {
                    upperVisitor.visitType(nodePtr->resType);
                    HIRExprVisitorDef::visitNodePtr(nodePtr);
                }

                void visit(HIRExprNodeLet& node) override {
                    upperVisitor.visitType(node.mType);
                    upperVisitor.visitPattern(node.pattern);
                    HIRExprVisitorDef::visit(node);
                }

                void visit(HIRExprNodeMatch& node) override {
                    for (auto& arm : node.arms) {
                        for (auto& pat : arm.patterns) {
                            upperVisitor.visitPattern(pat);
                        }
                        for (auto& g : arm.guards) {
                            upperVisitor.visitPattern(g.pat);
                        }
                    }
                    HIRExprVisitorDef::visit(node);
                }

                void visit(HIRExprNodePathValue& node) override {
                    upperVisitor.visitPath(node.mPath, HIRVisitor::PathContext::VALUE);
                }

                void visit(HIRExprNodeCallPath& node) override {
                    upperVisitor.visitPath(node.mPath, HIRVisitor::PathContext::VALUE);
                    HIRExprVisitorDef::visit(node);
                }

                void visit(HIRExprNodeCallMethod& node) override {
                    upperVisitor.visitPathParams(node.mParams);
                    HIRExprVisitorDef::visit(node);
                }

                void visit(HIRExprNodeStructLiteral& node) override {
                    upperVisitor.visitTypeInner(node.mType, false);

                    HIRExprVisitorDef::visit(node);
                }

                void visit(HIRExprNodeArraySized& node) override {
                    auto& as = node.mSize;
                    if (as.is_Unevaluated()) {
                        upperVisitor.visitConstgeneric(as.as_Unevaluated());
                    }
                    HIRExprVisitorDef::visit(node);
                }

                void visit(HIRExprNodeClosure& node) override {
                    upperVisitor.visitType(node.returnType);
                    for (auto& arg : node.mArgs) {
                        upperVisitor.visitPattern(arg.first);
                        upperVisitor.visitType(arg.second);
                    }
                    HIRExprVisitorDef::visit(node);
                }
            };

            for (auto& ty : expr.erasedTypes) {
                visitType(ty);
            }

            // Local expression
            if (expr.get() != nullptr) {
                ExprVisitor v{*this};
                (*expr).visit(v);
            }
            // External expression (has MIR)
            else if (auto* mir = expr.getExtMirMut()) {
                for (auto& ty : mir->locals) {
                    this->visitType(ty);
                }

                struct MirVisitor: public MIRVisitorMut {
                    VisitorPost& upperVisitor;

                    MirVisitor(VisitorPost& upperVisitor)
                        : upperVisitor(upperVisitor)
                    {
                    }

                    void visitType(HIRTypeRef& t) override {
                        upperVisitor.visitType(t);
                    }

                    void visitPath(HIRPath& p) override {
                        upperVisitor.visitPath(p, HIRVisitor::PathContext::VALUE);
                    }

                    bool visitLvalue(MIRLValue& lv, MIRValUsage u) override {
                        if (lv.root.is_Static()) {
                            upperVisitor.visitPath(lv.root.as_Static(), HIRVisitor::PathContext::VALUE);
                        }
                        return false;
                    }
                };

                MirVisitor mv(*this);
                for (auto& block : mir->blocks) {
                    for (auto& stmt : block.statements) {
                        mv.visitStmt(stmt);
                    }
                    mv.visitTerminator(block.terminator);
                }
            } else {
            }
        }
    };
}

void ConvertHIRBind(HIRCrate& crate) {
    {
        BindVisitor exp{crate};
        // Also visit extern crates to update their pointers
        for (auto& ec : crate.extCrates) {
            exp.visitCrate(*ec.second.mData);
        }
        exp.visitCrate(crate);
    }

    {
        VisitorPost v{crate};
        for (auto& ec : crate.extCrates) {
            v.visitCrate(*ec.second.mData);
        }
        v.visitCrate(crate);
    }

    // Populate supertrait list
    VisitorEnumSuperTraits(crate).visitCrate(crate);
}

HIRPathParams ConvertHIRCompleteAliasParams(HIRTypeInterner& types, const Span& sp, const HIRGenericParams& paramsDef, const HIRGenericPath& path, bool isExpr) {
    auto pp = path.mParams.clone();

    // Empty list, fill with ivars
    if (isExpr && pp.types.empty()) {
        while (pp.types.size() < paramsDef.types.size()) {
            pp.types.push_back(types.infer());
        }
    }
    if (isExpr && pp.values.empty()) {
        pp.values.resize(paramsDef.values.size());
    }

    // Shouldn't this error out if not in an expression?
    if (pp.mLifetimes.empty()) {
        pp.mLifetimes.resize(paramsDef.mLifetimes.size());
    }
    if (pp.mLifetimes.size() != paramsDef.mLifetimes.size()) {
        ERROR(sp, E0000, "Mismatched lifetime-generic count in " << path << ", expected " << paramsDef.mLifetimes.size() << " got " << pp.mLifetimes.size());
    }

    pp.types.reserve(paramsDef.types.size());
    while (pp.types.size() < paramsDef.types.size() && paramsDef.types[pp.types.size()].defaultValue != HIRTypeRef()) {
        auto monomorph = MonomorphStatePtr(types, nullptr, &pp, nullptr);
        pp.types.push_back(monomorph.monomorphType(sp, paramsDef.types[pp.types.size()].defaultValue));
    }
    if (pp.types.size() != paramsDef.types.size()) {
        ERROR(sp, E0000, "Mismatched type-generic count in " << path << ", expected " << paramsDef.types.size() << " got " << pp.types.size());
    }

    pp.values.reserve(paramsDef.values.size());
    while (pp.values.size() < paramsDef.values.size() && !paramsDef.values[pp.values.size()].defaultValue.is_Infer()) {
        auto monomorph = MonomorphStatePtr(types, nullptr, &pp, nullptr);
        pp.values.push_back(monomorph.monomorphConstgeneric(sp, paramsDef.values[pp.values.size()].defaultValue, false));
    }
    if (pp.values.size() != paramsDef.values.size()) {
        ERROR(sp, E0000, "Mismatched const-generic count in " << path << ", expected " << paramsDef.values.size() << " got " << pp.values.size());
    }

    return pp;
}

HIRTypeRef ConvertHIRExpandAliasesGetExpansionGP(const Span& sp, const HIRCrate& crate, const HIRGenericPath& path, bool isExpr) {
    const auto& ti = crate.getTypeitemByPath(sp, path.mPath);
    if (const auto* ep = ti.opt_TypeAlias()) {
        const auto& ta = *ep;
        DEBUG(path << " -> type " << ta.mParams.fmtArgs() << " = " << ta.mType);
        auto pp = ConvertHIRCompleteAliasParams(crate.types, sp, ta.mParams, path, isExpr);
        // Monomorphise the exapnded type using the created params
        auto ms = MonomorphStatePtr(crate.types, nullptr, &pp, nullptr);
        HIRTypeRef rv = ms.monomorphType(sp, ta.mType);
        DEBUG(path << " -> " << path.mPath << pp << " -> " << rv);
        return rv;
    }
    return crate.types.infer();
}

HIRTypeRef ConvertHIRExpandAliasesGetExpansion(const HIRCrate& crate, const HIRPath& path, bool isExpr) {
    static Span sp;
    TU_MATCH(HIRPath::Data, (path.mData), (e), (Generic, return ConvertHIRExpandAliasesGetExpansionGP(sp, crate, e, isExpr);), (UfcsInherent, DEBUG("TODO: Locate impl blocks for types - path=" << path);), (UfcsKnown, DEBUG("TODO: Locate impl blocks for traits on types - path=" << path);), (UfcsUnknown, DEBUG("TODO: Locate impl blocks for traits on types - path=" << path);))
    return crate.types.infer();
}

std::vector<HIRTraitPath> ConvertHIRExpandAliasesGetTraitExpansionGP(const Span& sp, const HIRCrate& crate, const HIRGenericPath& path, bool isExpr) {
    const auto& ti = crate.getTypeitemByPath(sp, path.mPath);
    if (const auto* ep = ti.opt_TraitAlias()) {
        const auto& ta = *ep;
        auto pp = ConvertHIRCompleteAliasParams(crate.types, sp, ta.mParams, path, isExpr);
        auto ms = MonomorphStatePtr(crate.types, nullptr, &pp, nullptr);
        std::vector<HIRTraitPath> rv;
        rv.reserve(ta.traits.size());
        for (const auto& exp : ta.traits) {
            rv.push_back(ms.monomorphTraitpath(sp, exp, false));
        }
        DEBUG(path << "\n -> " << path.mPath << pp << "\n -> {" << rv << "}");
        return rv;
    } else {
        return std::vector<HIRTraitPath>();
    }
}

std::vector<HIRTraitPath> ConvertHIRExpandAliasesGetTraitExpansion(const Span& sp, const HIRCrate& crate, /*const*/ HIRTraitPath& path, bool isExpr) {
    auto rv = ConvertHIRExpandAliasesGetTraitExpansionGP(sp, crate, path.mPath, isExpr);
    if (!rv.empty()) {
        if (!path.traitBounds.empty() || !path.typeBounds.empty()) {
            struct H {
                static bool containsTrait(const Span& sp, const HIRCrate& crate, const HIRGenericPath& path, const HIRGenericPath& desPath) {
                    if (path.mPath == desPath.mPath) {
                        return true;
                    }
                    const auto& ti = crate.getTypeitemByPath(sp, path.mPath);
                    if (const auto* t = ti.opt_Trait()) {
                        for (const auto& pt : t->parentTraits) {
                            if (containsTrait(sp, crate, pt.mPath, desPath)) {
                                return true;
                            }
                        }
                    } else if (const auto* t = ti.opt_TraitAlias()) {
                        for (const auto& pt : t->traits) {
                            if (containsTrait(sp, crate, pt.mPath, desPath)) {
                                return true;
                            }
                        }
                    } else {
                        BUG(sp, "Not a trait path " << path << ": " << ti.tagStr());
                    }
                    return false;
                }

                static HIRTraitPath& findEntry(const Span& sp, const HIRCrate& crate, const HIRGenericPath& desPath, ::std::vector<HIRTraitPath>& rv) {
                    for (auto& p : rv) {
                        if (containsTrait(sp, crate, p.mPath, desPath)) {
                            return p;
                        }
                    }
                    BUG(sp, "Unable to find a trait in expansion list for " << desPath);
                }
            };

            for (auto& tb : path.traitBounds) {
                auto& e = H::findEntry(sp, crate, tb.second.sourceTrait, rv);
                e.traitBounds.insert(std::make_pair(tb.first, std::move(tb.second)));
            }
            for (auto& tb : path.typeBounds) {
                auto& e = H::findEntry(sp, crate, tb.second.sourceTrait, rv);
                e.typeBounds.insert(std::make_pair(tb.first, std::move(tb.second)));
            }
        }
    }
    return rv;
}

class Expander: public HIRVisitor {
    const HIRCrate& crate;
    bool inExpr = false;
    const HIRTypeData* implType = nullptr;

public:
    Expander(const HIRCrate& crate)
        : HIRVisitor(nullptr, crate.types)
        , crate(crate)
    {
    }

    HIRTypeInterner& interner() const {
        return crate.types;
    }

    void expandTraitList(const Span& sp, ::std::vector<HIRTraitPath>& list) {
        for (auto it = list.begin(); it != list.end(); ++it) {
            auto n = ConvertHIRExpandAliasesGetTraitExpansion(sp, crate, *it, inExpr);
            if (!n.empty()) {
                it = list.erase(it);
                it = list.insert(it, std::make_move_iterator(n.begin()), std::make_move_iterator(n.end()));
                --it;
            }
        }
    }

    void visitType(HIRTypeRef& ty) override {
        static Span sp;

        if (ty->is_ErasedType() || ty->is_TraitObject()) {
            auto data = ty->cloneData();
            if (auto* e = data.opt_ErasedType()) {
                expandTraitList(sp, e->traits);
            } else if (auto* e = data.opt_TraitObject(); e->mTrait.mPath != HIRSimplePath()) {
                auto n = ConvertHIRExpandAliasesGetTraitExpansion(sp, crate, e->mTrait, inExpr);
                if (n.size() > 0) {
                    TODO(sp, "Expand trait alias in TraitObject? (markers only) - " << e->mTrait);
                }
            }
            ty = crate.types.intern(std::move(data));
        }

        HIRVisitor::visitType(ty);

        if (const auto* e = ty->opt_Path()) {
            HIRTypeRef newType = ConvertHIRExpandAliasesGetExpansion(crate, e->path, inExpr);
            // Keep trying to expand down the chain
            unsigned int numExp = 1;
            const unsigned int MAX_RECURSIVE_TYPE_EXPANSIONS = 100;
            while (numExp < MAX_RECURSIVE_TYPE_EXPANSIONS) {
                // NOTE: inner recurses
                HIRVisitor::visitType(newType);
                if (const auto* e = newType->opt_Path()) {
                    auto nt = ConvertHIRExpandAliasesGetExpansion(crate, e->path, inExpr);
                    if (nt->is_Infer()) {
                        break;
                    }
                    numExp++;
                    newType = mv$(nt);
                } else {
                    break;
                }
            }
            ASSERT_BUG(sp, numExp < MAX_RECURSIVE_TYPE_EXPANSIONS, "Recursion limit hit expanding " << ty << " (currently on " << newType << ")");
            if (!newType->is_Infer()) {
                DEBUG("Replacing " << ty << " with " << newType << " (" << numExp << " expansions)");
                ty = mv$(newType);
            }
        }
    }

    void visitTraitPath(HIRTraitPath& tp) override {
        static Span sp;
        // 1. Make sure that the trait path isn't pointing at an alias (should have been handled by the caller, which can expand to multiple items)
        ASSERT_BUG(sp, crate.getTypeitemByPath(sp, tp.mPath.mPath).is_Trait(), "Bad trait path - " << tp.mPath << " : " << crate.getTypeitemByPath(sp, tp.mPath.mPath).tagStr());
        // 2. Handle AtyBounds
        for (auto& tb : tp.traitBounds) {
            expandTraitList(sp, tb.second.traits);
        }

        // Finally. Recurse
        HIRVisitor::visitTraitPath(tp);
    }

    HIRPath expandAliasPath(const Span& sp, const HIRPath& path) {
        const unsigned int MAX_RECURSIVE_TYPE_EXPANSIONS = 100;

        // If the path is already generic and points at an enum variant, skip
        if (path.mData.is_Generic()) {
            const auto& gp = path.mData.as_Generic();
            if (gp.mPath.components().size() > 1 && crate.getTypeitemByPath(sp, gp.mPath, /*igncrate*/ false, /*ignlast*/ true).is_Enum()) {
                return HIRGenericPath();
            }
        }

        HIRPath rv = HIRGenericPath();
        const auto* cur = &path;

        unsigned int numExp = 0;
        do {
            auto ty = ConvertHIRExpandAliasesGetExpansion(crate, *cur, inExpr);
            if (ty->is_Infer()) {
                break;
            }
            if (!ty->is_Path()) {
                ERROR(sp, E0000, "Type alias referenced in generic path doesn't point to a path");
            }
            rv = ty->as_Path().path.clone();

            this->visitPath(rv, HIRVisitor::PathContext::TYPE);

            cur = &rv;
        } while (++numExp < MAX_RECURSIVE_TYPE_EXPANSIONS);
        ASSERT_BUG(sp, numExp < MAX_RECURSIVE_TYPE_EXPANSIONS, "Recursion limit expanding " << path << " (currently on " << *cur << ")");
        return mv$(rv);
    }

    HIRPattern::PathBinding visitPatternPathBinding(const Span& sp, HIRPath& path) {
        auto resizeTypeParams = [&](HIRPathParams& params, size_t size) {
            if (params.types.size() > size) {
                params.types.resize(size);
            }
            while (params.types.size() < size) {
                params.types.push_back(crate.types.infer());
            }
        };

        if (path.mData.is_UfcsUnknown()) {
            const auto& ty = path.mData.as_UfcsUnknown().type;
            const auto& name = path.mData.as_UfcsUnknown().item;

            const HIRGenericPath* gpP;
            if (ty->is_Generic() && ty->as_Generic().binding == GENERICSelf) {
                if (!implType) {
                    ERROR(sp, E0000, "Use of `Self` pattern outside of an impl block");
                }
                if (!TU_TEST1((*implType), Path, .path.mData.is_Generic())) {
                    ERROR(sp, E0000, "Use of `Self` pattern in non-struct impl block - " << implType);
                }
                gpP = &implType->as_Path().path.mData.as_Generic();
            } else {
                if (ty->is_Generic()) {
                    return HIRPattern::PathBinding();
                }
                if (!ty->is_Path()) {
                    ERROR(sp, E0000, "Expeted path in pattern binding, got " << ty);
                }
                if (!ty->as_Path().path.mData.is_Generic()) {
                    ERROR(sp, E0000, "Expeted generic path in pattern binding, got " << ty);
                }
                gpP = &ty->as_Path().path.mData.as_Generic();
            }
            const auto& gp = *gpP;
            const auto& ti = crate.getTypeitemByPath(sp, gp.mPath);
            if (!ti.is_Enum()) {
                ERROR(sp, E0000, "Expeted enum path in pattern binding, got " << ti.tagStr());
            }
            const auto& enm = ti.as_Enum();

            auto gp2 = gp.clone();
            gp2.mPath += name;
            gp2.mParams.mLifetimes.resize(enm.mParams.mLifetimes.size());
            resizeTypeParams(gp2.mParams, enm.mParams.types.size());
            gp2.mParams.values.resize(enm.mParams.values.size());

            auto idx = enm.findVariant(name);
            if (idx == ~0u) {
                TODO(sp, "Variant " << name << " not found in " << gp);
            }
            path = std::move(gp2);
            return HIRPattern::PathBinding::make_Enum({&enm, static_cast<unsigned>(idx)});
        }
        // `Self { ... }` patterns - Encoded as `<Self>::`
        if (path.mData.is_UfcsInherent()) {
            const auto& ty = path.mData.as_UfcsInherent().type;
            const auto& name = path.mData.as_UfcsInherent().item;
            ASSERT_BUG(sp, ty->is_Generic() && ty->as_Generic().binding == GENERICSelf, path);
            ASSERT_BUG(sp, name == "", path);
            if (!implType) {
                ERROR(sp, E0000, "Use of `Self` pattern outside of an impl block");
            }
            if (!TU_TEST1((*implType), Path, .path.mData.is_Generic())) {
                ERROR(sp, E0000, "Use of `Self` pattern in non-struct impl block - " << implType);
            }
            path = implType->as_Path().path.mData.as_Generic().clone();
            // Fall through for the resizing below
        }

        ASSERT_BUG(sp, path.mData.is_Generic(), path);
        auto& gp = path.mData.as_Generic();

        // TODO: Better error messages?
        if (gp.mPath.components().size() > 1) {
            const auto& ti = crate.getTypeitemByPath(sp, gp.mPath, false, /*ignore_last*/ true);
            if (ti.is_Enum()) {
                // Enum variant!
                const auto& enm = ti.as_Enum();

                gp.mParams.mLifetimes.resize(enm.mParams.mLifetimes.size());
                resizeTypeParams(gp.mParams, enm.mParams.types.size());
                gp.mParams.values.resize(enm.mParams.values.size());

                auto idx = ti.as_Enum().findVariant(gp.mPath.components().back());
                return HIRPattern::PathBinding::make_Enum({&enm, static_cast<unsigned>(idx)});
            }
        }

        const auto& ti = crate.getTypeitemByPath(sp, gp.mPath);
        if (ti.is_Union()) {
            const auto& unn = ti.as_Union();

            gp.mParams.mLifetimes.resize(unn.mParams.mLifetimes.size());
            resizeTypeParams(gp.mParams, unn.mParams.types.size());
            gp.mParams.values.resize(unn.mParams.values.size());

            return HIRPattern::PathBinding::make_Union(&unn);
        }

        ASSERT_BUG(sp, ti.is_Struct(), "Pattern path " << gp.mPath << " didn't point to a struct or union (" << ti.tagStr() << ")");
        const auto& str = ti.as_Struct();

        gp.mParams.mLifetimes.resize(str.mParams.mLifetimes.size());
        resizeTypeParams(gp.mParams, str.mParams.types.size());
        gp.mParams.values.resize(str.mParams.values.size());

        return HIRPattern::PathBinding::make_Struct(&str);
    }

    void visitPattern(HIRPattern& pat) override {
        static Span sp;

        HIRVisitor::visitPattern(pat);

        TU_MATCH_HDRA( (pat.mData), {)
        default:
            break;
            TU_ARMA(PathValue, e) {
                auto newPath = expandAliasPath(sp, e.path);
                if (newPath != HIRGenericPath()) {
                    DEBUG("Replacing " << e.path << " with " << newPath);
                    e.path = mv$(newPath);
                }
                e.binding = visitPatternPathBinding(sp, e.path);
            }
            TU_ARMA(PathTuple, e) {
                auto newPath = expandAliasPath(sp, e.path);
                if (newPath != HIRGenericPath()) {
                    DEBUG("Replacing " << e.path << " with " << newPath);
                    e.path = mv$(newPath);
                }
                e.binding = visitPatternPathBinding(sp, e.path);
            }
            TU_ARMA(PathNamed, e) {
                auto newPath = expandAliasPath(sp, e.path);
                if (newPath != HIRGenericPath()) {
                    DEBUG("Replacing " << e.path << " with " << newPath);
                    e.path = mv$(newPath);
                }
                e.binding = visitPatternPathBinding(sp, e.path);
                // TODO: If this is an empty/wildcard AND it's poiting at a value/tuple entry, change to PathValue/PathTuple
            }
        }
    }

    void visitParams(HIRGenericParams& params) override {
        for (auto it = params.bounds.begin(); it != params.bounds.end(); ++it) {
            static Span sp;
            if (auto* be = it->opt_TraitBound()) {
                auto n = ConvertHIRExpandAliasesGetTraitExpansion(sp, crate, be->trait, inExpr);
                if (!n.empty()) {
                    auto origType = std::move(be->type);
                    auto origHrtbs = std::move(be->hrtbs);
                    if (origHrtbs) {
                        visitParams(*origHrtbs);
                    }
                    visitType(origType);

                    it = params.bounds.erase(it);
                    for (auto& t : n) {
                        auto type = origType;
                        auto hrtbs = origHrtbs ? (&t == &n.back() ? std::move(origHrtbs) : box$(origHrtbs->clone())) : nullptr;
                        it = params.bounds.insert(it, HIRGenericBound::make_TraitBound({std::move(hrtbs), std::move(type), std::move(t)}));
                    }
                }
            }
        }
        HIRVisitor::visitParams(params);
    }

    void visitExpr(HIRExprPtr& expr) override {
        struct Visitor: public HIRExprVisitorDef {
            Expander& upperVisitor;

            Visitor(Expander& uv)
                : HIRExprVisitorDef(uv.interner())
                , upperVisitor(uv)
            {
            }

            void visitType(HIRTypeRef& ty) override {
                upperVisitor.visitType(ty);
            }

            void visitPattern(const Span& sp, HIRPattern& pat) override {
                upperVisitor.visitPattern(pat);
            }

            // Custom impl to visit the inner expression
            void visit(HIRExprNodeArraySized& node) override {
                auto& as = node.mSize;
                if (as.is_Unevaluated() && as.as_Unevaluated().is_Unevaluated()) {
                    upperVisitor.visitExpr(*as.as_Unevaluated().as_Unevaluated()->expr);
                }
                HIRExprVisitorDef::visit(node);
            }
        };

        if (expr.get() != nullptr) {
            auto old = inExpr;
            inExpr = true;

            Visitor v{*this};
            (*expr).visit(v);

            inExpr = old;
        }
    }

    void visitTraitAlias(HIRItemPath p, HIRTraitAlias& item) override {
        expandTraitList(Span(), item.traits);
        HIRVisitor::visitTraitAlias(p, item);
    }

    void visitTrait(HIRItemPath p, HIRTrait& item) override {
        expandTraitList(Span(), item.parentTraits);
        HIRVisitor::visitTrait(p, item);
    }

    void visitAssociatedtype(HIRItemPath p, HIRAssociatedType& item) override {
        expandTraitList(Span(), item.traitBounds);
        HIRVisitor::visitAssociatedtype(p, item);
    }

    void visitTypeImpl(HIRTypeImpl& impl) override {
        implType = impl.mType;
        HIRVisitor::visitTypeImpl(impl);
        implType = nullptr;
    }

    void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
        static Span sp;
        implType = impl.mType;
        HIRVisitor::visitTraitImpl(traitPath, impl);
        implType = nullptr;
    }

    void visitFunction(HIRItemPath p, HIRFunction& item) override {
        HIRVisitor::visitFunction(p, item);
        if (item.receiver == HIRFunction::Receiver::Custom) {
            ASSERT_BUG(Span(), item.receiverType, "Custom receiver without a receiver type");
            this->visitType(*item.receiverType);
        }
    }
};

class ExpanderSelf: public HIRVisitor {
    const HIRCrate& crate;
    const HIRTypeData* implType = nullptr;
    bool inExpr = false;

public:
    ExpanderSelf(const HIRCrate& crate, const HIRTypeData* implType = nullptr)
        : HIRVisitor(nullptr, crate.types)
        , crate(crate)
        , implType(implType)
    {
    }

    HIRTypeInterner& interner() const {
        return crate.types;
    }

    void visitType(HIRTypeRef& ty) override {
        HIRVisitor::visitType(ty);

        if (const auto* te = ty->opt_Generic()) {
            if (te->binding == GENERICSelf) {
                if (implType) {
                    DEBUG("Replace Self with " << implType);
                    ty = implType;
                } else {
                    // NOTE: Valid for `trait` definitions.
                    DEBUG("Self outside of an `impl` block");
                }
            }
        }
    }

    void visitExpr(HIRExprPtr& expr) override {
        struct Visitor: public HIRExprVisitorDef {
            ExpanderSelf& upperVisitor;

            Visitor(ExpanderSelf& uv)
                : HIRExprVisitorDef(uv.interner())
                , upperVisitor(uv)
            {
            }

            void visitType(HIRTypeRef& ty) override {
                upperVisitor.visitType(ty);
            }

            void visitPattern(const Span& sp, HIRPattern& pat) override {
                upperVisitor.visitPattern(pat);
            }

            // Custom impl to visit the inner expression
            void visit(HIRExprNodeArraySized& node) override {
                auto& as = node.mSize;
                if (as.is_Unevaluated() && as.as_Unevaluated().is_Unevaluated()) {
                    upperVisitor.visitExpr(*as.as_Unevaluated().as_Unevaluated()->expr);
                }
                HIRExprVisitorDef::visit(node);
            }
        };

        if (expr.get() != nullptr) {
            auto old = inExpr;
            inExpr = true;

            Visitor v{*this};
            (*expr).visit(v);

            inExpr = old;
        }
    }

    void visitEnum(HIRItemPath p, HIREnum& enm) override {
        HIRTypeRef ty = crate.types.path(HIRGenericPath(p.getSimplePath(), enm.mParams.makeNopParams(crate.types, 0)), &enm);
        implType = ty;
        HIRVisitor::visitEnum(p, enm);
        implType = nullptr;
    }

    void visitStruct(HIRItemPath p, HIRStruct& str) override {
        HIRTypeRef ty = crate.types.path(HIRGenericPath(p.getSimplePath(), str.mParams.makeNopParams(crate.types, 0)), &str);
        // HACK: If thre is a `#` in the path, it's en enum variant
        if (const auto* n = ::std::strchr(p.name, '#')) {
            if (n != p.name && n[1]) {
                auto path = p.getSimplePath();
                path.updateLastComponent(RcString::newInterned(p.name, n - p.name));
                const auto& enm = crate.getEnumByPath(Span(), path);
                ty = crate.types.path(HIRGenericPath(std::move(path), str.mParams.makeNopParams(crate.types, 0)), &enm);
            }
        }
        implType = ty;
        HIRVisitor::visitStruct(p, str);
        implType = nullptr;
    }

    void visitUnion(HIRItemPath p, HIRUnion& unn) override {
        HIRTypeRef ty = crate.types.path(HIRGenericPath(p.getSimplePath(), unn.mParams.makeNopParams(crate.types, 0)), &unn);
        implType = ty;
        HIRVisitor::visitUnion(p, unn);
        implType = nullptr;
    }

    void visitTypeImpl(HIRTypeImpl& impl) override {
        implType = impl.mType;
        HIRVisitor::visitTypeImpl(impl);
        implType = nullptr;
    }

    void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
        static Span sp;
        implType = impl.mType;
        HIRVisitor::visitTraitImpl(traitPath, impl);
        implType = nullptr;
    }
};

void ConvertHIRExpandAliases(HIRCrate& crate) {
    Expander exp{crate};
    exp.visitCrate(crate);
}

void ConvertHIRExpandAliasesSelf(HIRCrate& crate) {
    ExpanderSelf exp{crate};
    exp.visitCrate(crate);
}

void ConvertHIRExpandAliasesSelfExpr(const HIRCrate& crate, const HIRTypeData* implType, ::std::vector<::std::pair<HIRPattern, HIRTypeRef>>& args, HIRTypeRef& retTy, HIRExprPtr& expr) {
    ExpanderSelf exp{crate, implType};
    for (auto& arg : args) {
        exp.visitPattern(arg.first);
        exp.visitType(arg.second);
    }
    exp.visitType(retTy);
    exp.visitExpr(expr);
}

namespace {
    /// <summary>
    /// A class that acts like StaticTraitResolve, but only holds params
    /// </summary>
    struct MiniResolve {
        const HIRCrate& crate;
        const HIRTypeData* selfType = nullptr;
        const HIRGenericParams* mImplGenerics = nullptr;
        const HIRGenericParams* mItemGenerics = nullptr;

        MiniResolve(const HIRCrate& crate)
            : crate(crate)
        {
        }

        NullOnDrop<const HIRGenericParams> setImplGenerics(const HIRGenericParams& gps) {
            mImplGenerics = &gps;
            return NullOnDrop<const HIRGenericParams>(mImplGenerics);
        }

        NullOnDrop<const HIRGenericParams> setItemGenerics(const HIRGenericParams& gps) {
            mItemGenerics = &gps;
            return NullOnDrop<const HIRGenericParams>(mItemGenerics);
        }
    };

    class LifetimeVisitor: public HIRVisitor {
        HIRCrate& crate;
        MiniResolve mResolve;

        bool inExpr = false;
        bool createElided = false;
        HIRGenericParams* curParams = nullptr;
        unsigned curParamsLevel = 0;
        ::std::vector<const HIRLifetimeRef*> currentLifetime;
        /// The type of `Self` if we're in a by-value method
        const HIRTypeData* valueSelfType = nullptr;

        unsigned currentDepth = 0;
        std::vector<std::pair<unsigned, const HIRLifetimeRef*>> traitObjectRule;

    public:
        LifetimeVisitor(HIRCrate& crate)
            : HIRVisitor(nullptr, crate.types)
            , crate(crate)
            , mResolve(crate)
        {
        }

    private:
        struct SavedParams {
            LifetimeVisitor* parent;
            bool createElided;
            HIRGenericParams* curParams;
            unsigned curParamsLevel;

            SavedParams(LifetimeVisitor& parent)
                : parent(&parent)
                , createElided(parent.createElided)
                , curParams(parent.curParams)
                , curParamsLevel(parent.curParamsLevel)
            {
            }

            SavedParams(const SavedParams&) = delete;

            SavedParams(SavedParams&& x)
                : parent(x.parent)
                , createElided(x.createElided)
                , curParams(x.curParams)
                , curParamsLevel(x.curParamsLevel)
            {
                x.parent = nullptr;
            }

            ~SavedParams() {
                restore();
            }

            void restore() {
                if (parent) {
                    parent->createElided = createElided;
                    parent->curParams = curParams;
                    parent->curParamsLevel = curParamsLevel;
                    parent = nullptr;
                }
            }
        };

        SavedParams saveParams() {
            return SavedParams(*this);
        }

        void setParams(HIRGenericParams* params, unsigned level) {
            createElided = true;
            curParams = params;
            curParamsLevel = level;
        }

        SavedParams pushParams(HIRGenericParams& params, unsigned level) {
            auto rv = saveParams();
            setParams(&params, level);
            return rv;
        }

        SavedParams pushParams(HIRGenericParams* params, unsigned level) {
            auto rv = saveParams();
            setParams(params, level);
            return rv;
        }

        struct ExplicitInputLifetimeCollector: public HIRVisitor {
            std::vector<HIRLifetimeRef> lifetimes;
            unsigned int nestedHrtbDepth = 0;
            bool onlyCreated = false;
            unsigned int createdLevel = 0;
            unsigned int firstCreatedIndex = 0;

            ExplicitInputLifetimeCollector(HIRTypeInterner& types)
                : HIRVisitor(nullptr, types)
            {
            }

            ExplicitInputLifetimeCollector(HIRTypeInterner& types, unsigned int level, unsigned int firstIndex)
                : HIRVisitor(nullptr, types)
                , onlyCreated(true)
                , createdLevel(level)
                , firstCreatedIndex(firstIndex)
            {
            }

            void addLifetime(const HIRLifetimeRef& lft) {
                // Unknown/inferred input lifetimes are counted after the main
                // visitor creates their binder entries.
                if (lft.binding == HIRLifetimeRef::UNKNOWN || lft.binding == HIRLifetimeRef::INFER) {
                    return;
                }
                if (nestedHrtbDepth != 0 && lft.isHrl()) {
                    return;
                }
                if (onlyCreated && (!lft.isParam() || (lft.binding >> 8) != createdLevel || (lft.binding & 0xFF) < firstCreatedIndex)) {
                    return;
                }
                if (std::find(lifetimes.begin(), lifetimes.end(), lft) == lifetimes.end()) {
                    lifetimes.push_back(lft);
                }
            }

            void merge(const ExplicitInputLifetimeCollector& other) {
                for (const auto& lft : other.lifetimes) {
                    addLifetime(lft);
                }
            }

            void visitPathParams(HIRPathParams& pp) override {
                for (const auto& lft : pp.mLifetimes) {
                    addLifetime(lft);
                }
                HIRVisitor::visitPathParams(pp);
            }

            void visitTraitPath(HIRTraitPath& trait) override {
                const bool nestedHrtb = trait.hrtbs && !trait.hrtbs->mLifetimes.empty();
                nestedHrtbDepth += nestedHrtb;
                HIRVisitor::visitTraitPath(trait);
                nestedHrtbDepth -= nestedHrtb;
            }

            void visitType(HIRTypeRef& ty) override {
                // A nested function signature and an input-position impl Trait
                // each resolve elision in their own scope. Neither contributes
                // a candidate to the surrounding function parameter.
                if (ty->is_Function() || ty->is_ErasedType()) {
                    return;
                }
                if (const auto* e = ty->opt_Borrow()) {
                    addLifetime(e->lifetime);
                }
                if (const auto* e = ty->opt_TraitObject()) {
                    addLifetime(e->lifetime);
                }
                HIRVisitor::visitType(ty);
            }
        };

        struct InputLifetimeSelection {
            HIRLifetimeRef lifetime;
            bool hasLifetime = false;
            bool ambiguous = false;
            bool fromSelf = false;

            void addParameter(const ExplicitInputLifetimeCollector& parameter) {
                if (parameter.lifetimes.empty() || fromSelf || ambiguous) {
                    return;
                }
                if (parameter.lifetimes.size() != 1 || hasLifetime) {
                    hasLifetime = false;
                    ambiguous = true;
                    return;
                }
                lifetime = parameter.lifetimes.front();
                hasLifetime = true;
            }

            void setSelf(const ExplicitInputLifetimeCollector& selfLifetimes) {
                lifetime = HIRLifetimeRef();
                hasLifetime = false;
                ambiguous = selfLifetimes.lifetimes.size() > 1;
                fromSelf = !selfLifetimes.lifetimes.empty();
                if (selfLifetimes.lifetimes.size() == 1) {
                    lifetime = selfLifetimes.lifetimes.front();
                    hasLifetime = true;
                }
            }

            HIRLifetimeRef get() const {
                return hasLifetime && !ambiguous ? lifetime : HIRLifetimeRef();
            }
        };

        struct SelfLifetimeCollector: public HIRVisitor {
            HIRTypeInterner& types;
            const HIRTypeData* implSelf;
            ExplicitInputLifetimeCollector lifetimes;

            SelfLifetimeCollector(HIRTypeInterner& types, const HIRTypeData* implSelf)
                : HIRVisitor(nullptr, types)
                , types(types)
                , implSelf(implSelf)
                , lifetimes(types)
            {
            }

            bool containsSelf(HIRTypeRef ty) {
                struct SelfFinder: public HIRVisitor {
                    const HIRTypeData* implSelf;
                    bool found = false;

                    SelfFinder(HIRTypeInterner& types, const HIRTypeData* implSelf)
                        : HIRVisitor(nullptr, types)
                        , implSelf(implSelf)
                    {
                    }

                    void visitType(HIRTypeRef& ty) override {
                        if (ty == implSelf || (ty->is_Generic() && ty->as_Generic().binding == GENERICSelf)) {
                            found = true;
                            return;
                        }
                        HIRVisitor::visitType(ty);
                    }
                } finder(types, implSelf);

                finder.visitType(ty);
                return finder.found;
            }

            void visitType(HIRTypeRef& ty) override {
                if (const auto* borrow = ty->opt_Borrow()) {
                    if (containsSelf(borrow->inner)) {
                        lifetimes.addLifetime(borrow->lifetime);
                    }
                }
                HIRVisitor::visitType(ty);
            }
        };

    public:
        void visitLifetime(const Span& sp, HIRLifetimeRef& lft) {
            if (!lft.isParam()) {
                switch (lft.binding) {
                    case HIRLifetimeRef::STATIC: // 'static
                        break;
                    case HIRLifetimeRef::INFER: // '_
                        //TODO(sp, "Handle explicitly elided lifetimes");
                    case HIRLifetimeRef::UNKNOWN: // <none>
                        // If there's a current liftime (i.e. we're within a borrow), then use that
                        if (!currentLifetime.empty() && currentLifetime.back()) {
                            lft = *currentLifetime.back();
                            DEBUG("Use stack: " << lft);
                        }
                        // Otherwise, try to make a new one
                        else if (curParams && createElided) {
                            auto idx = curParams->mLifetimes.size();
                            curParams->mLifetimes.push_back(HIRLifetimeDef{RcString::newInterned(FMT("elided#" << idx))});
                            lft.binding = curParamsLevel * 256 + idx;
                            DEBUG("Create elided lifetime: " << lft << " " << curParams->mLifetimes.back().mName);
                        } else if (inExpr) {
                            // Allow
                        } else {
                            // TODO: Would error here, but there's places where it doesn't quite work.
                            // - E.g. `-> impl Foo` with no input lifetime
                            ERROR(sp, E0000, "Unspecified lifetime in outer context");
                        }
                        break;
                    default:
                        BUG(sp, "Unexpected lifetime binding - " << lft);
                }
            } else {
                // Add implicit bound
                if (curParams) {
                    if (!currentLifetime.empty() && currentLifetime.back() && currentLifetime.back()->isParam()) {
                        const auto& outer = *currentLifetime.back();
                        if (lft != outer && lft.asParam().group() < 2 // I.e. an impl or method param, not HRL or placeholder
                            && outer.asParam().group() < 2
                            // One of the two lifetimes must be from this block?
                            && (lft.asParam().group() == curParamsLevel || outer.asParam().group() == curParamsLevel)) {
                            // Add `'this: 'outer`
                            bool found = false;
                            // Only if not a duplicate
                            for (const auto& b : curParams->bounds) {
                                if (const auto* be = b.opt_Lifetime()) {
                                    if (be->test == lft && be->validFor == outer) {
                                        found = true;
                                        break;
                                    }
                                }
                            }
                            if (!found) {
                                DEBUG("Push bound " << lft << ": " << outer);
                                curParams->bounds.push_back(HIRGenericBound::make_Lifetime({lft, outer}));
                            }
                        }
                    } else {
                        if (currentLifetime.empty()) {
                        } else if (currentLifetime.back()) {
                        } else {
                        }
                    }
                }
            }
        }

        bool boundExists(const HIRLifetimeRef& test, const HIRLifetimeRef& validFor) const {
            if (mResolve.mImplGenerics) {
                for (const auto& b : mResolve.mImplGenerics->bounds) {
                    if (b.is_Lifetime()) {
                        DEBUG(b);
                    }
                    if (b.is_Lifetime() && b.as_Lifetime().test == test && b.as_Lifetime().validFor == validFor) {
                        return true;
                    }
                }
            }
            if (mResolve.mItemGenerics) {
                for (const auto& b : mResolve.mItemGenerics->bounds) {
                    if (b.is_Lifetime()) {
                        DEBUG(b);
                    }
                    if (b.is_Lifetime() && b.as_Lifetime().test == test && b.as_Lifetime().validFor == validFor) {
                        return true;
                    }
                }
            }
            return false;
        }

        void visitParams(HIRGenericParams& params) override {
            TRACE_FUNCTION_F(params.fmtArgs() << params.fmtBounds());
            for (auto& tps : params.types) {
                this->visitType(tps.defaultValue);
            }
            for (auto& val : params.values) {
                this->visitType(val.mType);
            }
            // The bounds list can grow as inferred lifetime bounds are added, so iterate manually and move the bound in/out to maintain pointer stability
            for (size_t i = 0; i < params.bounds.size(); i++) {
                auto bound = std::move(params.bounds[i]);
                params.bounds[i] = HIRGenericBound::make_Lifetime({HIRLifetimeRef::newStatic(), HIRLifetimeRef::newStatic()});
                visitGenericBound(bound);
                params.bounds[i] = std::move(bound);
            }
        }

        void visitGenericPath(HIRGenericPath& p, HIRVisitor::PathContext pc) override {
            const static Span sp;
            // Get the type definition and fill in omitted lifetimes
            const HIRGenericParams* gp = nullptr;
            if (p.mPath.components().size() > 1) {
                if (const auto* e = mResolve.crate.getTypeitemByPath(sp, p.mPath, false, true).opt_Enum()) {
                    gp = &e->mParams;
                }
            }
            if (!gp) {
                switch (pc) {
                    case HIRVisitor::PathContext::TYPE:
                    case HIRVisitor::PathContext::TRAIT: {
                        const auto& ti = mResolve.crate.getTypeitemByPath(sp, p.mPath);
                    TU_MATCH_HDRA( (ti), {)
                    TU_ARMA(Import, e) BUG(sp, "Unexpected reference to import - " << p);
                            TU_ARMA(Module, e) BUG(sp, "Unexpected reference to module - " << p);
                            TU_ARMA(TypeAlias, e) {
                                gp = &e.mParams;
                            }
                            TU_ARMA(TraitAlias, e) {
                                gp = &e.mParams;
                            }
                            TU_ARMA(ExternType, e) {
                                gp = nullptr;
                            }
                            TU_ARMA(Enum, e) {
                                gp = &e.mParams;
                            }
                            TU_ARMA(Struct, e) {
                                gp = &e.mParams;
                            }
                            TU_ARMA(Union, e) {
                                gp = &e.mParams;
                            }
                            TU_ARMA(Trait, e) {
                                gp = &e.mParams;
                            }
                    }
                    } break;
                    case HIRVisitor::PathContext::VALUE: {
                        const auto& vi = mResolve.crate.getValitemByPath(sp, p.mPath);
                    TU_MATCH_HDRA( (vi), { )
                    TU_ARMA(Import, e) BUG(sp, "Unexpected reference to import - " << p);
                            TU_ARMA(Constant, e) {
                                gp = nullptr;
                            }
                            TU_ARMA(Static, e) {
                                gp = nullptr;
                            }
                            TU_ARMA(Function, e) {
                                gp = &e.mParams;
                            }
                            TU_ARMA(StructConstant, e) {
                                gp = &mResolve.crate.getStructByPath(sp, e.ty).mParams;
                            }
                            TU_ARMA(StructConstructor, e) {
                                gp = &mResolve.crate.getStructByPath(sp, e.ty).mParams;
                            }
                    }
                    } break;
                }
            }
            if (p.mParams.mLifetimes.size() < (gp ? gp->mLifetimes.size() : 0) && currentLifetime.size() && currentLifetime.back()) {
                assert(gp); // Should be non-null because `.size()` is unsigned, and the above is `.size() < 0` if `gp` is null
                DEBUG(p);
                p.mParams.mLifetimes.resize(gp->mLifetimes.size());
                DEBUG(p);
            }
            HIRVisitor::visitGenericPath(p, pc);
        }

        void visitPathParams(HIRPathParams& pp) override {
            DEBUG(pp);
            static Span _sp;
            const Span& sp = _sp;

            for (auto& lft : pp.mLifetimes) {
                visitLifetime(sp, lft);
            }

            HIRVisitor::visitPathParams(pp);
        }

        void visitType(HIRTypeRef& ty) override {
            static Span _sp;
            const Span& sp = _sp;

            auto savedMTraitObjectRule = traitObjectRule.size();
            auto savedLiftimeDepth = currentLifetime.size();
            auto savedParams = saveParams();
            if (currentDepth == 0) {
                DEBUG("> " << ty);
            }
            currentDepth += 1;

            auto data = ty->cloneData();

            // Lifetime elision logic!

            if (auto* e = data.opt_Borrow()) {
                visitLifetime(sp, e->lifetime);
                currentLifetime.push_back(&e->lifetime);
                traitObjectRule.push_back(::std::make_pair(currentDepth, &e->lifetime));
            }
            if (auto* e = data.opt_Function()) {
                currentLifetime.push_back(nullptr);
                setParams(&e->hrls, GENERICHrtb);
                auto savedCreate = createElided;
                InputLifetimeSelection inputLifetimes;
                createElided = true;
                for (auto& t : e->argTypes) {
                    ExplicitInputLifetimeCollector parameterLifetimes(crate.types);
                    parameterLifetimes.visitType(t);
                    const auto firstElidedLifetimeIdx = e->hrls.mLifetimes.size();
                    this->visitType(t);
                    ExplicitInputLifetimeCollector createdLifetimes(crate.types, GENERICHrtb, firstElidedLifetimeIdx);
                    createdLifetimes.visitType(t);
                    parameterLifetimes.merge(createdLifetimes);
                    inputLifetimes.addParameter(parameterLifetimes);
                }
                createElided = false;
                HIRLifetimeRef outputLifetime = inputLifetimes.get();
                if (outputLifetime != HIRLifetimeRef()) {
                    currentLifetime.pop_back();
                    currentLifetime.push_back(&outputLifetime);
                }
                this->visitType(e->mRettype);
                currentLifetime.pop_back();
                createElided = savedCreate;
            }
            if (auto* e = data.opt_TraitObject()) {
                // TODO: Create? but what if it's not used?
                if (e->mTrait.hrtbs) {
                    currentLifetime.push_back(nullptr);
                    setParams(&*e->mTrait.hrtbs, GENERICHrtb);
                }

                // If neither of those rules apply, then the bounds on the trait are used:
                // - If the trait is defined with a single lifetime bound then that bound is used.
                // - If 'static is used for any lifetime bound then 'static is used.
                // - If the trait has no lifetime bounds, then the lifetime is inferred in expressions and is 'static outside of expressions.
                if (e->lifetime.binding == HIRLifetimeRef::INFER || e->lifetime.binding == HIRLifetimeRef::UNKNOWN) {
                    struct H {
                        const Span& sp;
                        const HIRCrate& crate;
                        std::vector<HIRLifetimeRef> lifetimes;

                        void visitTrait(const HIRSimplePath& p, const HIRPathParams& params) {
                            const auto& t = crate.getTraitByPath(sp, p);
                            DEBUG(p << " " << t.lifetime);
                            if (t.lifetime != HIRLifetimeRef()) {
                                if (t.lifetime == HIRLifetimeRef::newStatic()) {
                                    lifetimes.push_back(t.lifetime);
                                    // Early return on 'static, no need to check anything else
                                    return;
                                } else {
                                    // TODO: Parameters
                                }
                            }
                            // TODO: Monomorph? (for lifetime parameters)
                            for (const auto& st : t.parentTraits) {
                                visitTrait(st.mPath.mPath, st.mPath.mParams);
                            }
                        }
                    } h{sp, mResolve.crate};

                    if (e->mTrait.mPath.mPath != HIRSimplePath()) {
                        h.visitTrait(e->mTrait.mPath.mPath, e->mTrait.mPath.mParams);
                    }
                    std::sort(h.lifetimes.begin(), h.lifetimes.end());
                    auto newEnd = std::unique(h.lifetimes.begin(), h.lifetimes.end());
                    h.lifetimes.erase(newEnd, h.lifetimes.end());
                    if (h.lifetimes.empty()) {
                        // Apply normal elision rules?
                        DEBUG("TraitObject: No available bounds");
                    } else {
                        if (h.lifetimes.size() == 1 || h.lifetimes.back() == HIRLifetimeRef::newStatic()) {
                            DEBUG("TraitObject: Set lifetime " << h.lifetimes.front() << " from bounds");
                            e->lifetime = h.lifetimes.back();
                        } else {
                            // Error?
                            DEBUG("TraitObject: Multiple bounded lifetimes");
                        }
                    }
                }

                // https://doc.rust-lang.org/reference/lifetime-elision.html#default-trait-object-lifetimes
                // If the trait object is used as a type argument of a generic type then the containing type is first used to try to infer a bound.
                // - If there is a unique bound from the containing type then that is the default
                // - If there is more than one bound from the containing type then an explicit bound must be specified

                bool wasStaticRule = false;
                // If the lifetime is omitted, or '_
                // ... AND this is within prototype (not in an expression)
                if (
                    (e->lifetime.binding == HIRLifetimeRef::UNKNOWN /*|| e->m_lifetime.binding == HIR::LifetimeRef::INFER*/)
                    //&& m_cur_params
                    //&& m_create_elided    // In arguments
                    && !inExpr // Not in expression
                ) {
                    if (!traitObjectRule.empty()) {
                        DEBUG("TraitObject: cur=" << currentDepth << " back.first=" << traitObjectRule.back().first);
                        if (traitObjectRule.back().first == currentDepth - 1) {
                            if (traitObjectRule.back().second) {
                                const auto& lft = *traitObjectRule.back().second;
                                e->lifetime = lft;
                                wasStaticRule = (lft.binding == HIRLifetimeRef::STATIC);
                                DEBUG("TraitObject: Set lifetime " << e->lifetime << " - trait object rule");
                            }
                        }
                    }
                }
                if (
                    (wasStaticRule || e->lifetime.binding == HIRLifetimeRef::UNKNOWN /*|| e->m_lifetime.binding == HIR::LifetimeRef::INFER*/) && !inExpr // Not in expression
                ) {
                    // HACK: If the trait has a lifeime param, use that
                    if (!e->mTrait.hrtbs && e->mTrait.mPath.mParams.mLifetimes.size() == 1) {
                        e->lifetime = e->mTrait.mPath.mParams.mLifetimes[0];
                        DEBUG("TraitObject: Set to first/only lifetime param of data trait: " << e->lifetime);
                    }
                }
                // If there is no available rule (i.e. not in a borrow), and the lifetime was omitted (not just '_), then fill in 'static
                if (false && traitObjectRule.empty() && e->lifetime.binding == HIRLifetimeRef::UNKNOWN && !inExpr && !(createElided && curParams)) {
                    e->lifetime = HIRLifetimeRef::newStatic();
                    DEBUG("TraitObject: Set lifetime " << e->lifetime << " - hack");
                }
            }

            if (auto* e = data.opt_Path()) {
                // Expand default lifetime params
                if (auto* p = e->path.mData.opt_Generic()) {
                    const HIRTypeItem& ti = mResolve.crate.getTypeitemByPath(sp, p->mPath);
                    const HIRGenericParams* gp = nullptr;
                    TU_MATCH_HDRA( (ti), { )
                    TU_ARMA(Import, v) {
                            BUG(sp, "Unexpected import: " << p->mPath);
                        }
                        TU_ARMA(Module, v) {
                            BUG(sp, "Unexpected module: " << p->mPath);
                        }
                        TU_ARMA(TypeAlias, v) {
                            gp = &v.mParams;
                        }
                        TU_ARMA(TraitAlias, v) {
                            gp = &v.mParams;
                        }
                        TU_ARMA(ExternType, v) {
                            gp = nullptr;
                        }
                        TU_ARMA(Enum, v) {
                            gp = &v.mParams;
                        }
                        TU_ARMA(Struct, v) {
                            gp = &v.mParams;
                        }
                        TU_ARMA(Union, v) {
                            gp = &v.mParams;
                        }
                        TU_ARMA(Trait, v) {
                            gp = &v.mParams;
                        }
                    }
                    if(gp) {
                        p->mParams.mLifetimes.resize(gp->mLifetimes.size());

                        // Inherit bounds.
                        if (curParams) {
                            TRACE_FUNCTION_FR("INHERIT BOUNDS: " << *p, "INHERIT BOUNDS");
                            // Visit lifeitmes first - so they're un-elided
                            for (auto& l : p->mParams.mLifetimes) {
                                visitLifetime(sp, l);
                            }
                            // Then make a monomorph state, and find lifetime bounds
                            MonomorphStatePtr ms(crate.types, nullptr, &p->mParams, nullptr);
                            for (const auto& b : gp->bounds) {
                                TU_MATCH_HDRA((b), {)
                                TU_ARMA(Lifetime, be) {
                                        ASSERT_BUG(sp, be.test.isParam(), b);
                                        ASSERT_BUG(sp, be.validFor.binding != HIRLifetimeRef::UNKNOWN, b);
                                        curParams->bounds.push_back(HIRGenericBound::make_Lifetime({ms.monomorphLifetime(sp, be.test), ms.monomorphLifetime(sp, be.validFor)}));
                                        const auto& nbe = curParams->bounds.back().as_Lifetime();
                                        if (nbe.test.isParam()) {
                                            ASSERT_BUG(sp, nbe.test.isParam(), b << " -> " << curParams->bounds.back());
                                            ASSERT_BUG(sp, nbe.validFor.binding != HIRLifetimeRef::UNKNOWN, b << " -> " << curParams->bounds.back());
                                            if ((nbe.test.isParam() && nbe.test.asParam().group() == 3) || (nbe.validFor.isParam() && nbe.validFor.asParam().group() == 3)) {
                                                curParams->bounds.pop_back();
                                            } else {
                                                DEBUG("INHERIT " << curParams->bounds.back());
                                            }
                                        } else {
                                            // The monomorphised lifetime wasn't a parameter - had to be `'static` but not checking
                                            // - Remove the new bound, if it was bad then there should be an error later on?
                                            curParams->bounds.pop_back();
                                        }
                                    }
                                    TU_ARMA(TypeLifetime, be) {
                                        // TODO: Should type lifetimes be inferred too?
                                    }
                                    TU_ARMA(TraitBound, _be) {
                                    }
                                    TU_ARMA(TypeEquality, _be) {
                                    }
                                }
                            }
                        }
                    }

                    if( p->mParams.mLifetimes.size() == 0 ) {
                        // Mark such that contained trait objects use `'static`
                        static HIRLifetimeRef staticLifetime = HIRLifetimeRef::newStatic();
                        traitObjectRule.push_back(std::make_pair(currentDepth, &staticLifetime));
                    }
                    else if( p->mParams.mLifetimes.size() == 1 ) {
                        // Mark such that contained trait objects use this lifetime
                        traitObjectRule.push_back(std::make_pair(currentDepth, &p->mParams.mLifetimes[0]));
                    }
                    else {
                        // Mark such that contained trait objects require an explicit annotation
                        traitObjectRule.push_back(std::make_pair(currentDepth, nullptr));
                    }
                } else if (auto* p = e->path.mData.opt_UfcsKnown()) {
                    // Get trait, check if the type has ATCs
                    const auto& trait = mResolve.crate.getTraitByPath(sp, p->trait.mPath);
                    const auto& aty = trait.types.at(p->item);

                    if (p->params.mLifetimes.size() < aty.generics.mLifetimes.size()) //&& m_current_lifetime.size() && m_current_lifetime.back() )
                    {
                        p->params.mLifetimes.resize(aty.generics.mLifetimes.size());
                    }
                }
            }

            HIRVisitor::visitTypeData(data);

            savedParams.restore();
            while (currentLifetime.size() > savedLiftimeDepth) {
                currentLifetime.pop_back();
            }
            while (traitObjectRule.size() > savedMTraitObjectRule) {
                traitObjectRule.pop_back();
            }
            currentDepth -= 1;

            {
                bool pushed = false;
                if (currentLifetime.empty() || !currentLifetime.back()) {
                    // Push `'static` (if not in expression mode AND; this is a trait object OR we're not in arguments)
                    if (!inExpr) {
                        static HIRLifetimeRef staticLifetime = HIRLifetimeRef::newStatic();
                        if (!(curParams && createElided)) {
                            // In the return type, so we don't want to make a new parameter - push `'static`
                            currentLifetime.push_back(&staticLifetime);
                            pushed = true;
                        } else if (data.is_TraitObject() && data.as_TraitObject().lifetime == HIRLifetimeRef()) {
                            // `dyn Foo` as vs `dyn Foo+'_`
                            currentLifetime.push_back(&staticLifetime);
                            pushed = true;
                        }
                    }
                }
                if (auto* e = data.opt_TraitObject()) {
                    // TODO: The following are different
                    // - `fn foo(&self) -> Box<dyn Foo>`      -> `fn foo<'a>(&'a self) -> Box<dyn Foo + 'static>`
                    // - `fn foo(&self) -> Box<dyn Foo + '_>` -> `fn foo<'a>(&'a self) -> Box<dyn Foo + 'a>`
                    // BUT
                    // - `fn foo(&self) -> &dyn Foo` -> `fn foo<'a>(&'a self) -> &'a (dyn Foo + 'a)`
                    // - `fn foo(&self) -> &(dyn Foo + '_)` -> `fn foo<'a>(&'a self) -> &'a (dyn Foo + 'a)`
                    // TODO: What about in structs?

                    visitLifetime(sp, e->lifetime);
                    DEBUG("TraitObject: Final lifetime " << e->lifetime);
                }
                if (auto* e = data.opt_ErasedType()) {
                    // If in arguments, don't visit an omitted lifetime (so we don't add an elided lifetime for something that will be generic)
                    if ((!e->lifetimeBounds.empty() && e->lifetimeBounds.front().binding == HIRLifetimeRef::UNKNOWN) && (curParams && createElided)) {
                    } else {
                        for (auto& lft : e->lifetimeBounds) {
                            visitLifetime(sp, lft);
                        }
                    }

                    // For an erased type, check if there's a lifetime within any of the ATYs
                    // - If so, use that [citation needed]
                    // https://rust-lang.github.io/rfcs/1951-expand-impl-trait.html#scoping-for-type-and-lifetime-parameters
                    // Any mentioned lifetimes within the trait are considered as "captured"
                    // - So, enumerate the mentioned lifetimes and create a composite for it.

                    // TODO: Replace use of `m_lifetimes` with `m_use`

                    // Is there a `use<>` annotation?
                    switch (e->usePresent) {
                        case HIRTypeDataErasedType::Use::Present:
                            DEBUG("ErasedType use present");
                            break;
                        case HIRTypeDataErasedType::Use::Omitted2024:
                            // Add all in-scope generics
                            DEBUG("ErasedType use omitted: 2024Edition");
                            if (mResolve.mImplGenerics) {
                                auto p = mResolve.mImplGenerics->makeNopParams(crate.types, 0);
                                for (const auto& l : p.mLifetimes) {
                                    DEBUG("2024: add " << l);
                                    e->use.mLifetimes.push_back(l);
                                }
                            }
                            if (mResolve.mItemGenerics) {
                                auto p = mResolve.mItemGenerics->makeNopParams(crate.types, 1);
                                for (const auto& l : p.mLifetimes) {
                                    DEBUG("2024: add " << l);
                                    e->use.mLifetimes.push_back(l);
                                }
                            }
                            break;
                        case HIRTypeDataErasedType::Use::OmittedOld: {
                            DEBUG("ErasedType use omitted: Older Editions");

                            // If there is no lifetime assigned, then grab all mentioned lifetimes?
                            struct V: public HIRVisitor {
                                std::set<HIRLifetimeRef> lfts;

                                V(HIRTypeInterner& types)
                                    : HIRVisitor(nullptr, types)
                                {
                                }

                                void visitPathParams(HIRPathParams& pp) override {
                                    for (auto& lft : pp.mLifetimes) {
                                        addLifetime(lft);
                                    }

                                    HIRVisitor::visitPathParams(pp);
                                }

                                void addLifetime(const HIRLifetimeRef& lft) {
                                    if (lft.isHrl()) {
                                        // HRL - ignore
                                        return;
                                    }
                                    this->lfts.insert(lft);
                                }

                                void visitType(HIRTypeRef& ty) override {
                                    TRACE_FUNCTION_F(ty);
                                    if (const auto* tep = ty->opt_Borrow()) {
                                        addLifetime(tep->lifetime);
                                    }
                                    if (const auto* tep = ty->opt_Function()) {
                                        // Push HRLs?
                                        (void)tep;
                                    }
                                    if (const auto* tep = ty->opt_TraitObject()) {
                                        addLifetime(tep->lifetime);
                                        // Push HRLs?
                                    }
                                    if (const auto* tep = ty->opt_ErasedType()) {
                                        if (tep->lifetimeBounds.size() == 1 && tep->lifetimeBounds.front().binding == HIRLifetimeRef::UNKNOWN) {
                                            // Ignore unbound?
                                        } else {
                                            for (const auto& lft : tep->lifetimeBounds) {
                                                addLifetime(lft);
                                            }
                                        }
                                    }
                                    HIRVisitor::visitType(ty);
                                }
                            } v(crate.types);

                            // `data` is the lifetime-elided copy of `ty`.  Type nodes are
                            // immutable once interned, so walking `ty` here would inspect
                            // the pre-elision tree (and can reintroduce `'#omitted` from a
                            // parenthesised Fn bound).  Materialise the current copy before
                            // collecting the lifetimes captured by this opaque type.
                            auto elidedTy = crate.types.intern(data.cloneData());
                            v.visitType(elidedTy);
                            // TODO: In 2024 edition, these rules change
                            // - Before: generics/lifetimes not mentioned in the `impl Foo` are omitted
                            // - After: All included
                            // - Both: Unless there's a `use<Foo>` present

                            if (v.lfts.empty() && !(!currentLifetime.empty() && currentLifetime.back() && !pushed)) {
                                // If this is on a by-value method, then assume it captures `self` (and thus all contained liftimes)
                                // REF: rustc-1.90.0-src/compiler/rustc_data_structures/src/graph/linked_graph/mod.rs:278
                                if (valueSelfType) {
                                    DEBUG("Check Self: " << valueSelfType);
                                    auto selfType = valueSelfType;
                                    v.visitType(selfType);
                                }
                            }

                            // If there is a lifetime on the stack (that wasn't from a `'static` pushed above), then use it
                            if (v.lfts.empty() && !currentLifetime.empty() && currentLifetime.back() && !pushed) {
                                DEBUG("ErasedType: Use wrapping lifetime - " << *currentLifetime.back());
                                e->use.mLifetimes.push_back(*currentLifetime.back());
                            } else if (v.lfts.empty()) {
                                // No contained lifetimes, it's `'static`?
                                DEBUG("No inner lifetimes, will be `'static`");
                                e->use.mLifetimes.push_back(HIRLifetimeRef::newStatic());
                            } else if (v.lfts.size() == 1) {
                                // Easy, just assign this lifetime
                                DEBUG("ErasedType: Use contained lifetime " << *v.lfts.begin());
                                e->use.mLifetimes.push_back(*v.lfts.begin());
                            } else {
                                // If in arguments: Create a new input lifetime with a union of these lifetimes.
                                if (curParams && createElided) {
                                    e->use.mLifetimes.push_back(HIRLifetimeRef(curParamsLevel * 256 + curParams->mLifetimes.size()));
                                    curParams->mLifetimes.push_back(HIRLifetimeDef{});
                                    for (const auto& l : v.lfts) {
                                        curParams->bounds.push_back(HIRGenericBound::make_Lifetime({e->use.mLifetimes[0], l}));
                                    }
                                }
                                // In return: Save the list?
                                else if (curParams) {
                                    ASSERT_BUG(sp, e->use.mLifetimes.size() == 0, "");
                                    for (const auto& lft : v.lfts) {
                                        e->use.mLifetimes.push_back(lft);
                                    }
                                } else {
                                }
                            }
                        } break;
                    }
                    for (const auto& l : e->use.mLifetimes) {
                        ASSERT_BUG(sp, l.binding != HIRLifetimeRef::UNKNOWN, "Unbound lifetime? - " << l);
                    }

                    if (auto* ee = e->inner.opt_Alias()) {
                        if (ee->inner->path.crateName() != mResolve.crate.crateName) {
                            // Should be impossible, as these are fully expanded by the time they reach HIR serialisation
                        } else {
                            ASSERT_BUG(Span(), mResolve.mImplGenerics, "No impl generics for type " << ty);
                            ee->inner->generics.mLifetimes = mResolve.mImplGenerics->mLifetimes;
                            ee->params = ee->inner->generics.makeNopParams(crate.types, 0);
                        }
                    }
                }
                if (pushed) {
                    currentLifetime.pop_back();
                }
            }

            ty = crate.types.intern(mv$(data));

            if (currentDepth == 0) {
                DEBUG("< " << ty);
            }
        }

        void visitTraitPath(HIRTraitPath& tp) override {
            const Span sp;
            TRACE_FUNCTION_FR(tp, tp);

            if (tp.lifetimeElision) {
                assert(tp.hrtbs);
                tp.lifetimeElision = false;
                currentLifetime.push_back(nullptr);
                const auto firstElidedLifetimeIdx = tp.hrtbs->mLifetimes.size();
                std::vector<std::vector<HIRLifetimeRef>> explicitInputLifetimes;
                ASSERT_BUG(sp, tp.mPath.mParams.types.size() == 1 && tp.mPath.mParams.types.front()->is_Tuple(), "Parenthesised Fn arguments aren't a tuple: " << tp.mPath);
                for (auto input : *tp.mPath.mParams.types.front()->opt_Tuple()) {
                    ExplicitInputLifetimeCollector parameterLifetimes(crate.types);
                    parameterLifetimes.visitType(input);
                    explicitInputLifetimes.push_back(mv$(parameterLifetimes.lifetimes));
                }

                // Visit the trait args (as inputs)
                auto savedParams = pushParams(tp.hrtbs.get(), 3);

                this->visitGenericPath(tp.mPath, HIRVisitor::PathContext::TYPE);
                DEBUG(tp.mPath);
                if (tp.hrtbs) {
                    DEBUG("for " << tp.hrtbs->fmtArgs());
                }

                InputLifetimeSelection inputLifetimes;
                auto& inputs = *tp.mPath.mParams.types.front()->opt_Tuple();
                ASSERT_BUG(sp, inputs.size() == explicitInputLifetimes.size(), "Fn argument tuple changed size");
                for (size_t i = 0; i < inputs.size(); i++) {
                    auto input = inputs[i];
                    ExplicitInputLifetimeCollector parameterLifetimes(crate.types);
                    parameterLifetimes.lifetimes = mv$(explicitInputLifetimes[i]);
                    ExplicitInputLifetimeCollector createdLifetimes(crate.types, GENERICHrtb, firstElidedLifetimeIdx);
                    createdLifetimes.visitType(input);
                    parameterLifetimes.merge(createdLifetimes);
                    inputLifetimes.addParameter(parameterLifetimes);
                }
                HIRLifetimeRef lft = inputLifetimes.get();
                if (lft != HIRLifetimeRef()) {
                    currentLifetime.push_back(&lft);
                    for (auto& assoc : tp.typeBounds) {
                        this->visitGenericPath(assoc.second.sourceTrait, HIRVisitor::PathContext::TYPE);
                        this->visitPathParams(assoc.second.atyParams);
                        this->visitType(assoc.second.type);
                    }
                    for (auto& assoc : tp.traitBounds) {
                        this->visitGenericPath(assoc.second.sourceTrait, HIRVisitor::PathContext::TYPE);
                        this->visitPathParams(assoc.second.atyParams);
                        for (auto& trait : assoc.second.traits) {
                            this->visitTraitPath(trait);
                        }
                    }
                    currentLifetime.pop_back();
                }
                savedParams.restore();

                // Fix the source paths in ATYs
                const auto& trait = mResolve.crate.getTraitByPath(sp, tp.mPath.mPath);

                struct H {
                    const HIRCrate& crate;

                    H(const HIRCrate& crate)
                        : crate(crate)
                    {
                    }

                    bool enumSupertraits(const Span& sp, const HIRTrait& tr, const HIRGenericPath& trPath, ::std::function<bool(HIRGenericPath)> cb) {
                        const HIRTypeRef self = crate.types.self();
                        MonomorphStatePtr ms(crate.types, self, &trPath.mParams, nullptr);

                        if (tr.allParentTraits.size() > 0) {
                            // Externals will have this populated
                            for (const auto& supertrait : tr.allParentTraits) {
                                auto m = ms.monomorphGenericpath(sp, supertrait.mPath, false);
                                if (cb(std::move(m))) {
                                    return true;
                                }
                            }
                        } else {
                            // This runs before bind, so locals won't have the main list populated
                            for (const auto& pt : tr.parentTraits) {
                                auto m = ms.monomorphGenericpath(sp, pt.mPath, false);
                                DEBUG("- " << m);
                                if (enumSupertraits(sp, crate.getTraitByPath(sp, m.mPath), m, cb)) {
                                    return true;
                                }
                                if (cb(std::move(m))) {
                                    return true;
                                }
                            }
                            for (const auto& b : tr.mParams.bounds) {
                                if (!b.is_TraitBound()) {
                                    continue;
                                }
                                const auto& be = b.as_TraitBound();
                                if (be.type != self) {
                                    continue;
                                }
                                const auto& pt = be.trait;
                                if (pt.mPath.mPath == trPath.mPath) {
                                    continue;
                                }

                                auto m = ms.monomorphGenericpath(sp, pt.mPath, false);
                                DEBUG("- " << m);
                                if (enumSupertraits(sp, crate.getTraitByPath(sp, m.mPath), m, cb)) {
                                    return true;
                                }
                                if (cb(std::move(m))) {
                                    return true;
                                }
                            }
                        }
                        return false;
                    }
                } h(mResolve.crate);

                auto fixSource = [&](HIRGenericPath& gp, const RcString& name) {
                    DEBUG("[fix_source] >> " << gp);
                    if (gp.equalsIgnoringRegions(tp.mPath)) {
                        gp = tp.mPath.clone();
                        return;
                    }
                    if (h.enumSupertraits(sp, trait, tp.mPath, [&](HIRGenericPath m) {
                        DEBUG("[fix_source] ?? " << m);
                        if (m.equalsIgnoringRegions(gp)) {
                            gp = std::move(m);
                            return true;
                        }
                        return false;
                    })) {
                        return;
                    }
                    BUG(sp, "Failed to find " << gp << " in parent trait list of " << tp.mPath);
                };
                for (auto& assoc : tp.typeBounds) {
                    fixSource(assoc.second.sourceTrait, assoc.first);
                }
                for (auto& assoc : tp.traitBounds) {
                    fixSource(assoc.second.sourceTrait, assoc.first);
                }

                if (lft != HIRLifetimeRef()) {
                    currentLifetime.pop_back();
                    currentLifetime.push_back(&lft);
                }

                // Visit the rest (associated types mostly), using the output lifetime from above
                HIRVisitor::visitTraitPath(tp);

                currentLifetime.pop_back();
            } else {
                HIRVisitor::visitTraitPath(tp);
            }
        }

        void visitExpr(HIRExprPtr& ep) override {
            struct EV: public HIRExprVisitorDef {
                LifetimeVisitor& parent;

                EV(LifetimeVisitor& parent)
                    : HIRExprVisitorDef(parent.crate.types)
                    , parent(parent)
                {
                }

                void visitType(HIRTypeRef& ty) {
                    parent.visitType(ty);
                }
            } v{*this};

            auto s = inExpr;
            inExpr = true;
            if (ep) {
                ep->visit(v);
            }
            inExpr = s;
        }

        void visitTypeImpl(HIRTypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.mType);
            mResolve.selfType = impl.mType;
            auto _ = mResolve.setImplGenerics(/*impl.m_type,*/ impl.mParams);

            // Pre-visit so lifetime elision can work
            {
                auto _ = pushParams(impl.mParams, 0);
                this->visitType(impl.mType);
            }

            HIRVisitor::visitTypeImpl(impl);
        }

        void visitInherentType(HIRItemPath p, HIRTypeAlias& item) override {
            auto _ = mResolve.setItemGenerics(item.mParams);
            auto _2 = pushParams(item.mParams, 1);
            HIRVisitor::visitInherentType(p, item);
        }

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
            TRACE_FUNCTION_F("impl " << traitPath << impl.traitArgs << " for " << impl.mType);
            mResolve.selfType = impl.mType;
            auto _ = mResolve.setImplGenerics(/*impl.m_type,*/ impl.mParams);

            // Pre-visit so lifetime elision can work
            {
                auto _ = pushParams(impl.mParams, 0);
                this->visitType(impl.mType);
                this->visitPathParams(impl.traitArgs);
            }

            HIRVisitor::visitTraitImpl(traitPath, impl);
        }

        void visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) override {
            TRACE_FUNCTION_F("impl " << traitPath << impl.traitArgs << " for " << impl.mType << " { }");
            mResolve.selfType = impl.mType;
            auto _ = mResolve.setImplGenerics(/*impl.m_type,*/ impl.mParams);

            // Pre-visit so lifetime elision can work
            {
                auto _ = pushParams(impl.mParams, 0);
                this->visitType(impl.mType);
                this->visitPathParams(impl.traitArgs);
            }

            HIRVisitor::visitMarkerImpl(traitPath, impl);
        }

        void visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) override {
            auto _ = mResolve.setImplGenerics(/*impl.m_type,*/ item.mParams);
            HIRVisitor::visitTypeAlias(p, item);
        }

        void visitTrait(HIRItemPath p, HIRTrait& item) override {
            auto tySelf = crate.types.self();
            mResolve.selfType = tySelf;
            auto _ = mResolve.setImplGenerics(/*impl.m_type,*/ item.mParams);
            HIRVisitor::visitTrait(p, item);
        }

        void visitStruct(HIRItemPath p, HIRStruct& item) override {
            auto _ = mResolve.setImplGenerics(/*item.m_struct_markings.dst_type,*/ item.mParams);
            auto _2 = pushParams(item.mParams, 0);
            createElided = false;
            HIRVisitor::visitStruct(p, item);
        }

        void visitEnum(HIRItemPath p, HIREnum& item) override {
            auto _ = mResolve.setImplGenerics(/*MetadataType::None,*/ item.mParams);
            auto _2 = pushParams(item.mParams, 0);
            createElided = false;
            HIRVisitor::visitEnum(p, item);
        }

        void visitUnion(HIRItemPath p, HIRUnion& item) override {
            auto _ = mResolve.setImplGenerics(/*MetadataType::None,*/ item.mParams);
            auto _2 = pushParams(item.mParams, 0);
            createElided = false;
            HIRVisitor::visitUnion(p, item);
        }

        void visitConstant(HIRItemPath p, HIRConstant& item) override {
            auto lft = HIRLifetimeRef::newStatic();
            currentLifetime.push_back(&lft);
            visitType(item.mType);
            currentLifetime.pop_back(/*&lft*/);

            HIRVisitor::visitConstant(p, item);
        }

        void visitStatic(HIRItemPath p, HIRStatic& item) override {
            auto lft = HIRLifetimeRef::newStatic();
            currentLifetime.push_back(&lft);
            visitType(item.mType);
            currentLifetime.pop_back(/*&lft*/);

            HIRVisitor::visitStatic(p, item);
        }

        void visitFunction(HIRItemPath p, HIRFunction& item) override {
            TRACE_FUNCTION_F(p);
            auto _ = mResolve.setItemGenerics(item.mParams);
            // NOTE: Superfluous... except that it makes the params valid for the return type.
            visitParams(item.mParams);

            // TODO: Add lifetime bounds from argument types!
            // - While visiting the argument types, find path types and inherit the lifetime bounds

            // Visit arguments to get the input lifetimes
            auto savedParams = pushParams(item.mParams, 1);
            InputLifetimeSelection inputLifetimes;
            for (size_t i = 0; i < item.mArgs.size(); i++) {
                auto& arg = item.mArgs[i];
                TRACE_FUNCTION_FR("ARG " << arg, "ARG " << arg);
                ExplicitInputLifetimeCollector parameterLifetimes(crate.types);
                parameterLifetimes.visitType(arg.second);
                const auto firstElidedLifetimeIdx = item.mParams.mLifetimes.size();
                visitType(arg.second);
                ExplicitInputLifetimeCollector createdLifetimes(crate.types, GENERICItem, firstElidedLifetimeIdx);
                createdLifetimes.visitType(arg.second);
                parameterLifetimes.merge(createdLifetimes);

                if (i == 0 && item.receiver != HIRFunction::Receiver::Free) {
                    SelfLifetimeCollector selfLifetimes(crate.types, mResolve.selfType);
                    selfLifetimes.visitType(arg.second);
                    inputLifetimes.setSelf(selfLifetimes.lifetimes);
                } else {
                    inputLifetimes.addParameter(parameterLifetimes);
                }
            }
            createElided = false;

            // Get output lifetime
            HIRLifetimeRef elidedOutputLifetime = inputLifetimes.get();
            if (item.receiver != HIRFunction::Receiver::Free) {
                if (item.receiver == HIRFunction::Receiver::Value) {
                    valueSelfType = mResolve.selfType;
                }
            }
            if (elidedOutputLifetime != HIRLifetimeRef()) {
                DEBUG("Single input lifetime - " << elidedOutputLifetime);
            }
            // If present, set it (push to the stack)
            assert(currentLifetime.empty());
            if (elidedOutputLifetime != HIRLifetimeRef()) {
                currentLifetime.push_back(&elidedOutputLifetime);
            }

            // Visit return type (populates path for `impl Trait` in return position
            {
                TRACE_FUNCTION_FR("RET " << item.returnType, "RET " << item.returnType);
                visitType(item.returnType);
            }
            // - Unset params for the expression
            savedParams.restore();

            if (elidedOutputLifetime != HIRLifetimeRef()) {
                currentLifetime.pop_back();
            }
            assert(currentLifetime.empty());

            DEBUG("Output: " << item.mParams.fmtArgs() << item.mParams.fmtBounds());
            valueSelfType = nullptr;

            HIRVisitor::visitFunction(p, item);
        }
    };
}

void ConvertHIRLifetimeElision(HIRCrate& crate) {
    LifetimeVisitor v{crate};
    v.visitCrate(crate);
}

namespace {

    class MarkingsVisitor: public HIRVisitor {
        const HIRCrate& crate;
        const HIRSimplePath& mLangUnsize;
        const HIRSimplePath& mLangCoerceUnsized;
        const HIRSimplePath& mLangCopy;
        const HIRSimplePath& mLangDeref;
        const HIRSimplePath& mLangDrop;
        const HIRSimplePath& mLangPhantomData;

    public:
        MarkingsVisitor(const HIRCrate& crate)
            : HIRVisitor(nullptr, crate.types)
            , crate(crate)
            , mLangUnsize(crate.getLangItemPathOpt("unsize"))
            , mLangCoerceUnsized(crate.getLangItemPathOpt("coerce_unsized"))
            , mLangCopy(crate.getLangItemPathOpt("copy"))
            , mLangDeref(crate.getLangItemPathOpt("deref"))
            , mLangDrop(crate.getLangItemPathOpt("drop"))
            , mLangPhantomData(crate.getLangItemPathOpt("phantom_data"))
        {
        }

        void visitStruct(HIRItemPath ip, HIRStruct& str) override {
            HIRVisitor::visitStruct(ip, str);

            str.structMarkings.dstType = getStructDstType(str, str.mParams, {});
            if (str.structMarkings.dstType != HIRStructMarkings::DstType::None) {
                str.structMarkings.unsizedField = (str.mData.is_Tuple() ? str.mData.as_Tuple().size() - 1 : str.mData.as_Named().size() - 1);
            }

            // Rules:
            // - A type parameter must be ?Sized
            // - That type parameter must only be used as part of the last field, and only once
            // - If the final field isn't the parameter, it must also impl Unsize

            // HACK: Just determine what ?Sized parameter is controlling the sized-ness
            if (str.structMarkings.dstType == HIRStructMarkings::DstType::Possible) {
                auto& lastFieldTy = (str.mData.is_Tuple() ? str.mData.as_Tuple().back().ent : str.mData.as_Named().back().ty);
                for (size_t i = 0; i < str.mParams.types.size(); i++) {
                    const auto& param = str.mParams.types[i];
                    auto ty = crate.types.generic(param.mName, i);
                    if (!param.isSized) {
                        if (visitTyWith(lastFieldTy, [&](const auto& t) {
                            return t == ty;
                        })) {
                            ASSERT_BUG(Span(), str.structMarkings.unsizedParam == ~0u, "Multiple unsized params to " << ip);
                            str.structMarkings.unsizedParam = i;
                        }
                    }
                }
                ASSERT_BUG(Span(), str.structMarkings.unsizedParam != ~0u, "No unsized param for type " << ip);
                str.structMarkings.canUnsize = true;
            }
        }

        HIRStructMarkings::DstType getFieldDstType(const HIRTypeData* ty, const HIRGenericParams& innerDef, const HIRGenericParams& paramsDef, const HIRPathParams* params) {
            TRACE_FUNCTION_F("ty=" << ty);
            // If the type is generic, and the pointed-to parameters is ?Sized, record as needing unsize
            if (const auto* te = ty->opt_Generic()) {
                if (innerDef.types.at(te->binding).isSized == true) {
                    return HIRStructMarkings::DstType::None;
                } else if (params) {
                    // Look at the param. Check for generic (use params_def), slice/traitobject, or path (no mono)
                    return getFieldDstType(params->types.at(te->binding), paramsDef, paramsDef, nullptr);
                } else {
                    return HIRStructMarkings::DstType::Possible;
                }
            } else if (ty->is_Slice() || TU_TEST1((*ty), Primitive, == HIRCoreType::Str)) {
                return HIRStructMarkings::DstType::Slice;
            } else if (ty->is_TraitObject()) {
                return HIRStructMarkings::DstType::TraitObject;
            } else if (const auto* te = ty->opt_Path()) {
                // If the type is a struct, check it (recursively)
                if (!te->path.mData.is_Generic()) {
                    // Associated type, TODO: Check this better.
                    return HIRStructMarkings::DstType::None;
                } else if (te->binding.is_Struct()) {
                    const auto& paramsTpl = te->path.mData.as_Generic().mParams;
                    if (params && monomorphisePathparamsNeeded(paramsTpl)) {
                        static Span sp;
                        auto monomorphCb = MonomorphStatePtr(crate.types, nullptr, params, nullptr);
                        auto paramsMono = monomorphCb.monomorphPathParams(sp, paramsTpl, false);
                        return getStructDstType(*te->binding.as_Struct(), paramsDef, &paramsMono);
                    } else {
                        return getStructDstType(*te->binding.as_Struct(), innerDef, &paramsTpl);
                    }
                } else {
                    return HIRStructMarkings::DstType::None;
                }
            } else {
                return HIRStructMarkings::DstType::None;
            }
        }

        HIRStructMarkings::DstType getStructDstType(const HIRStruct& str, const HIRGenericParams& def, const HIRPathParams* params) {
        TU_MATCH_HDRA( (str.mData), {)
        TU_ARMA(Unit, se) {
                }
                TU_ARMA(Tuple, se) {
                    // TODO: Ensure that only the last field is ?Sized
                    if (se.size() > 0) {
                        return getFieldDstType(se.back().ent, str.mParams, def, params);
                    }
                }
                TU_ARMA(Named, se) {
                    // Check the last field in the struct.
                    // - If it is Sized, leave as-is (struct is marked as Sized)
                    // - If it is known unsized, record the type
                    // - If it is a ?Sized parameter, mark as possible and record index for MIR

                    // TODO: Ensure that only the last field is ?Sized
                    if (se.size() > 0) {
                        return getFieldDstType(se.back().ty, str.mParams, def, params);
                    }
                }
        }
        return HIRStructMarkings::DstType::None;
        }

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
            static Span sp;

            HIRVisitor::visitTraitImpl(traitPath, impl);

            if (impl.mType->is_Path()) {
                const auto& te = impl.mType->as_Path();
                const HIRTraitMarkings* markingsPtr = te.binding.getTraitMarkings();
                if (markingsPtr) {
                    HIRTraitMarkings& markings = *const_cast<HIRTraitMarkings*>(markingsPtr);
                    if (traitPath == mLangUnsize) {
                        DEBUG("Type " << impl.mType << " can Unsize");
                        ERROR(sp, E0000, "Unsize shouldn't be manually implemented");
                    } else if (traitPath == mLangDrop) {
                        // TODO: Check that there's only one impl, and that it covers the same set as the type.
                        markings.hasDropImpl = true;
                    } else if (traitPath == mLangCoerceUnsized) {
                        auto& structMarkings = const_cast<HIRStruct*>(te.binding.as_Struct())->structMarkings;
                        if (structMarkings.coerceUnsizedIndex != ~0u) {
                            ERROR(sp, E0000, "CoerceUnsized can only be implemented once per struct");
                        }

                        DEBUG("Type " << impl.mType << " can Coerce");
                        if (impl.traitArgs.types.size() != 1) {
                            ERROR(sp, E0000, "Unexpected number of arguments for CoerceUnsized");
                        }
                        const auto& dstTy = impl.traitArgs.types[0];
                        // Determine which field is the one that does the coerce
                        if (!te.binding.is_Struct()) {
                            ERROR(sp, E0000, "Cannot implement CoerceUnsized on non-structs");
                        }
                        if (!dstTy->is_Path()) {
                            ERROR(sp, E0000, "Cannot implement CoerceUnsized from non-structs");
                        }
                        const auto& dstTe = dstTy->as_Path();
                        if (!dstTe.binding.is_Struct()) {
                            ERROR(sp, E0000, "Cannot implement CoerceUnsized from non-structs");
                        }
                        if (dstTe.binding.as_Struct() != te.binding.as_Struct()) {
                            ERROR(sp, E0000, "CoerceUnsized can only be implemented between variants of the same struct");
                        }

                        // NOTES: (from IRC: eddyb)
                        // < eddyb> they're required that T and U are the same struct definition (with different type parameters) and exactly one field differs in type between T and U (ignoring PhantomData)
                        // < eddyb> Mutabah: I forgot to mention that the field that differs in type must also impl CoerceUnsized

                        // Determine the difference in monomorphised variants.
                        unsigned int field = ~0u;
                        const auto& str = te.binding.as_Struct();

                        auto monomorphCbL = MonomorphStatePtr(crate.types, nullptr, &dstTe.path.mData.as_Generic().mParams, nullptr);
                        auto monomorphCbR = MonomorphStatePtr(crate.types, nullptr, &te.path.mData.as_Generic().mParams, nullptr);

                    TU_MATCH_HDRA( (str->mData), {)
                    TU_ARMA(Unit, se) {
                            }
                            TU_ARMA(Tuple, se) {
                                for (unsigned int i = 0; i < se.size(); i++) {
                                    // If the data is PhantomData, ignore it.
                                    if (TU_TEST2((*se[i].ent), Path, .path.mData, Generic, .mPath == mLangPhantomData)) {
                                        continue;
                                    }
                                    if (monomorphiseTypeNeeded(se[i].ent)) {
                                        auto tyL = monomorphCbL.monomorphType(sp, se[i].ent, false);
                                        auto tyR = monomorphCbR.monomorphType(sp, se[i].ent, false);
                                        if (tyL != tyR) {
                                            if (field != ~0u) {
                                                ERROR(sp, E0000, "CoerceUnsized impls can only differ by one field");
                                            }
                                            field = i;
                                        }
                                    }
                                }
                            }
                            TU_ARMA(Named, se) {
                                for (unsigned int i = 0; i < se.size(); i++) {
                                    // If the data is PhantomData, ignore it.
                                    if (TU_TEST2((*se[i].ty), Path, .path.mData, Generic, .mPath == mLangPhantomData)) {
                                        continue;
                                    }
                                    if (monomorphiseTypeNeeded(se[i].ty)) {
                                        auto tyL = monomorphCbL.monomorphType(sp, se[i].ty, false);
                                        auto tyR = monomorphCbR.monomorphType(sp, se[i].ty, false);
                                        if (tyL != tyR) {
                                            if (field != ~0u) {
                                                ERROR(sp, E0000, "CoerceUnsized impls can only differ by one field");
                                            }
                                            field = i;
                                        }
                                    }
                                }
                            }
                    }
                    if( field == ~0u )
                        ERROR(sp, E0000, "CoerceUnsized requires a field to differ between source and destination");
                    structMarkings.coerceUnsizedIndex = field;
                    } else if (traitPath == mLangDeref) {
                        DEBUG("Type " << impl.mType << " can Deref");
                        markings.hasADeref = true;
                    } else if (traitPath == mLangCopy) {
                        DEBUG("Type " << impl.mType << " has a Copy impl");
                        markings.isCopy = true;
                    }
                    // TODO: Marker traits (with conditions)
                    else {
                    }
                }
            }
        }
    };

    class Visitor2: public HIRVisitor {
    public:
        explicit Visitor2(HIRTypeInterner& types)
            : HIRVisitor(nullptr, types)
        {
        }

        size_t getUnsizeParamIdx(const Span& sp, const HIRTypeData* pointee) const {
            if (const auto* te = pointee->opt_Generic()) {
                return te->binding;
            } else if (const auto* te = pointee->opt_Path()) {
                ASSERT_BUG(sp, te->binding.is_Struct(), "Pointer to non-Unsize type - " << pointee);
                const auto& ism = te->binding.as_Struct()->structMarkings;
                ASSERT_BUG(sp, ism.unsizedParam != ~0u, "Pointer to non-Unsize type - " << pointee);
                const auto& gp = te->path.mData.as_Generic();
                return getUnsizeParamIdx(sp, gp.mParams.types.at(ism.unsizedParam));
            } else {
                BUG(sp, "Pointer to non-Unsize type? - " << pointee);
            }
        }

        HIRStructMarkings::Coerce getCoerceType(const Span& sp, HIRItemPath ip, const HIRStruct& str, size_t& outParamIdx) const {
            if (str.structMarkings.coerceUnsizedIndex == ~0u) {
                return HIRStructMarkings::Coerce::None;
            }
            if (str.structMarkings.coerceUnsized != HIRStructMarkings::Coerce::None) {
                outParamIdx = str.structMarkings.coerceParam;
                return str.structMarkings.coerceUnsized;
            }

            const HIRTypeData* fieldTy = nullptr;
            TU_MATCHA((str.mData), (se), (Unit, ), (Tuple, fieldTy = se.at(str.structMarkings.coerceUnsizedIndex).ent;), (Named, fieldTy = se.at(str.structMarkings.coerceUnsizedIndex).ty;))
            assert(fieldTy);
        tryAgain:
            DEBUG("field_ty = " << fieldTy);

            if (const auto* te = fieldTy->opt_Path()) {
                ASSERT_BUG(sp, te->binding.is_Struct(), "CoerceUnsized impl differs on Path that isn't a struct - " << ip << " fld=" << fieldTy);
                const auto* istr = te->binding.as_Struct();
                const auto& gp = te->path.mData.as_Generic();

                size_t innerIdx = 0;
                auto innerType = getCoerceType(sp, {fieldTy}, *istr, innerIdx);
                ASSERT_BUG(sp, innerType != HIRStructMarkings::Coerce::None, "CoerceUnsized impl differs on a non-CoerceUnsized type - " << ip << " fld=" << fieldTy);

                const auto& paramTy = gp.mParams.types.at(innerIdx);
                switch (innerType) {
                    case HIRStructMarkings::Coerce::None:
                        throw "";
                    case HIRStructMarkings::Coerce::Passthrough:
                        // Recurse on the generic type.
                        fieldTy = paramTy;
                        goto tryAgain;
                    case HIRStructMarkings::Coerce::Pointer:
                        outParamIdx = getUnsizeParamIdx(sp, paramTy);
                        return HIRStructMarkings::Coerce::Pointer;
                }
            } else if (const auto* te = fieldTy->opt_Generic()) {
                outParamIdx = te->binding;
                return HIRStructMarkings::Coerce::Passthrough;
            } else if (const auto* te = fieldTy->opt_Pointer()) {
                outParamIdx = getUnsizeParamIdx(sp, te->inner);
                return HIRStructMarkings::Coerce::Pointer;
            } else if (const auto* te = fieldTy->opt_Borrow()) {
                outParamIdx = getUnsizeParamIdx(sp, te->inner);
                return HIRStructMarkings::Coerce::Pointer;
            } else {
                TODO(sp, "Handle CoerceUnsized type " << fieldTy);
            }
            BUG(sp, "Reached end of get_coerce_type - " << fieldTy);
        }

        void visitStruct(HIRItemPath ip, HIRStruct& str) override {
            static Span sp;

            auto& structMarkings = str.structMarkings;
            if (structMarkings.coerceUnsizedIndex == ~0u) {
                return;
            }

            size_t idx = 0;
            auto cut = getCoerceType(sp, ip, str, idx);
            structMarkings.coerceParam = idx;
            structMarkings.coerceUnsized = cut;
        }
    };

} // namespace

void ConvertHIRMarkings(HIRCrate& crate) {
    MarkingsVisitor exp{crate};
    exp.visitCrate(crate);

    // Visit again, visiting all structs and filling the coerce_unsized data
    Visitor2 exp2{crate.types};
    exp2.visitCrate(crate);
}

void expandTraitImplTypeDefaults(const HIRCrate& crate, const HIRSimplePath& traitPath, HIRTraitImpl& impl) {
    Span sp;
    const auto& trait = crate.getTraitByPath(sp, traitPath);
    auto ms = MonomorphStatePtr(crate.types, impl.mType, &impl.traitArgs, nullptr);

    while (impl.traitArgs.types.size() < trait.mParams.types.size()) {
        const auto& def = trait.mParams.types[impl.traitArgs.types.size()];
        auto ty = ms.monomorphType(sp, def.defaultValue);
        DEBUG("Add default trait arg " << ty << " from " << def.defaultValue);
        impl.traitArgs.types.push_back(mv$(ty));
    }
}

class UfcsVisitor: public HIRVisitor {
    const HIRCrate& crate;
    bool mVisitExprs;
    bool runEat;

    typedef ::std::vector<::std::pair<const HIRSimplePath*, const HIRTrait*>> tTraitImports;
    tTraitImports traits;

    StaticTraitResolve mResolve;
    bool mInTraitDef = false;
    const HIRTypeData* mCurrentType = nullptr;
    const HIRTrait* currentTrait = nullptr;
    const HIRItemPath* mCurrentTraitPath = nullptr;
    bool inExpr = false;
    HIRSimplePath curModPath;

public:
    UfcsVisitor(const HIRCrate& crate, bool visitExprs)
        : HIRVisitor(nullptr, crate.types)
        , crate(crate)
        , mVisitExprs(visitExprs)
        , runEat(visitExprs)
        , // Defaults to running when doing second-pass
        mResolve(crate)
    {
    }

    struct ModTraitsGuard {
        UfcsVisitor* v;
        tTraitImports oldImports;

        ModTraitsGuard(UfcsVisitor& v, tTraitImports oldImports)
            : v(&v)
            , oldImports(mv$(oldImports))
        {
        }

        ModTraitsGuard(ModTraitsGuard&& x)
            : v(x.v)
            , oldImports(mv$(x.oldImports))
        {
            x.v = nullptr;
        }

        ModTraitsGuard& operator=(ModTraitsGuard&&) = delete;

        ~ModTraitsGuard() {
            if (v) {
                DEBUG("Stack pop: " << this->v->traits.size() << " -> " << this->oldImports.size());
                this->v->traits = mv$(this->oldImports);
                v = nullptr;
            }
        }
    };

    ModTraitsGuard pushModTraits(HIRSimplePath path, const HIRModule& mod) {
        static Span sp;
        DEBUG("");
        ModTraitsGuard rv{*this, mv$(this->traits)};
        for (const auto& traitPath : mod.traits) {
            DEBUG("- " << traitPath);
            traits.push_back(::std::make_pair(&traitPath, &crate.getTraitByPath(sp, traitPath)));
        }
        curModPath = std::move(path);
        return rv;
    }

    void visitModule(HIRItemPath p, HIRModule& mod) override {
        auto _ = this->pushModTraits(p.getSimplePath(), mod);
        HIRVisitor::visitModule(p, mod);
    }

    void visitParams(HIRGenericParams& params) {
        TRACE_FUNCTION_F(params.fmtArgs() << params.fmtBounds());

        // Custom visitor to prevent running of EAT on type paramerter defaults
        auto savedRunEat = runEat;
        runEat = false;
        for (auto& tps : params.types) {
            this->visitType(tps.defaultValue);
        }
        runEat = savedRunEat;

        for (auto& bound : params.bounds) {
            visitGenericBound(bound);
        }

        // Re-populate the resolve index, as the above has changed them
        mResolve.prepIndexes(Span());
    }

    void visitUnion(HIRItemPath p, HIRUnion& item) override {
        auto _ = mResolve.setImplGenerics(MetadataType::None, item.mParams);
        auto ty = crate.types.path(HIRGenericPath(p.getSimplePath()), &item);
        mCurrentType = ty;
        HIRVisitor::visitUnion(p, item);
        mCurrentType = nullptr;
    }

    void visitStruct(HIRItemPath p, HIRStruct& item) override {
        auto _ = mResolve.setImplGenerics(item.structMarkings.dstType, item.mParams);
        auto ty = crate.types.path(HIRGenericPath(p.getSimplePath()), &item);
        mCurrentType = ty;
        HIRVisitor::visitStruct(p, item);
        mCurrentType = nullptr;
    }

    void visitEnum(HIRItemPath p, HIREnum& item) override {
        auto _ = mResolve.setImplGenerics(MetadataType::None, item.mParams);
        auto ty = crate.types.path(HIRGenericPath(p.getSimplePath()), &item);
        mCurrentType = ty;
        HIRVisitor::visitEnum(p, item);
        mCurrentType = nullptr;
    }

    void visitFunction(HIRItemPath p, HIRFunction& item) override {
        auto _ = mResolve.setItemGenerics(item.mParams);
        HIRVisitor::visitFunction(p, item);
    }

    void visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) override {
        // NOTE: Disabled, because generics in type aliases are never checked
        // Re-enabled to resolve a UFCS properly (1.90.0 libcore)
        auto _ = mResolve.setImplGenerics(MetadataType::Unknown, item.mParams);
        HIRVisitor::visitTypeAlias(p, item);
    }

    void visitTrait(HIRItemPath p, HIRTrait& trait) override {
        mInTraitDef = true;
        currentTrait = &trait;
        mCurrentTraitPath = &p;
        auto _ = mResolve.setImplGenerics(MetadataType::TraitObject, trait.mParams);
        HIRVisitor::visitTrait(p, trait);
        currentTrait = nullptr;
        mInTraitDef = false;
    }

    void visitTypeImpl(HIRTypeImpl& impl) override {
        TRACE_FUNCTION_F("impl" << impl.mParams.fmtArgs() << " " << impl.mType << " (mod=" << impl.srcModule << ")");
        auto _t = this->pushModTraits(impl.srcModule, this->crate.getModByPath(Span(), impl.srcModule));
        auto _g = mResolve.setImplGenerics(impl.mType, impl.mParams);
        mCurrentType = impl.mType;
        HIRVisitor::visitTypeImpl(impl);
        mCurrentType = nullptr;
    }

    void visitInherentType(HIRItemPath p, HIRTypeAlias& item) override {
        auto _ = mResolve.setItemGenerics(item.mParams);
        HIRVisitor::visitInherentType(p, item);
    }

    void visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) override {
        HIRItemPath p(impl.mType, traitPath, impl.traitArgs);
        TRACE_FUNCTION_F("impl" << impl.mParams.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << impl.mType << " (mod=" << impl.srcModule << ")");
        auto _t = this->pushModTraits(impl.srcModule, this->crate.getModByPath(Span(), impl.srcModule));
        auto _g = mResolve.setImplGenerics(impl.mType, impl.mParams);

        // TODO: Push a bound that `Self: ThisTrait`
        mCurrentType = impl.mType;
        currentTrait = &crate.getTraitByPath(Span(), traitPath);
        mCurrentTraitPath = &p;

        // The implemented trait is always in scope
        traits.push_back(::std::make_pair(&traitPath, currentTrait));
        HIRVisitor::visitMarkerImpl(traitPath, impl);
        traits.pop_back();

        currentTrait = nullptr;
        mCurrentType = nullptr;
    }

    void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
        HIRItemPath p(impl.mType, traitPath, impl.traitArgs);
        TRACE_FUNCTION_F("impl" << impl.mParams.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << impl.mType << " (mod=" << impl.srcModule << ")");
        auto _t = this->pushModTraits(impl.srcModule, this->crate.getModByPath(Span(), impl.srcModule));
        auto _g = mResolve.setImplGenerics(MetadataType::Unknown, impl.mParams);

        expandTraitImplTypeDefaults(crate, traitPath, impl);

        mCurrentType = impl.mType;
        currentTrait = &crate.getTraitByPath(Span(), traitPath);
        mCurrentTraitPath = &p;
        traits.push_back(::std::make_pair(&traitPath, currentTrait));

        this->visitType(impl.mType);
        mResolve.updateImplSelfMetadata(impl.mType);

        // TODO: Handle resolution of all items in m_resolve.m_type_equalities
        // - params might reference each other, so `set_item_generics` has to have been called
        // - But `m_type_equalities` can end up with non-resolved UFCS paths
        for (auto& e : mResolve.typeEqualities) {
            visitType(e.second.ty);
        }

        // The implemented trait is always in scope
        HIRVisitor::visitTraitImpl(traitPath, impl);
        traits.pop_back();

        currentTrait = nullptr;
        mCurrentType = nullptr;
    }

    void visitExpr(HIRExprPtr& expr) override {
        struct ExprVisitor: public HIRExprVisitorDef {
            UfcsVisitor& upperVisitor;
            HIRExprNodeP mReplacement;

            ExprVisitor(UfcsVisitor& uv)
                : HIRExprVisitorDef(uv.crate.types)
                , upperVisitor(uv)
            {
            }

            void visitType(HIRTypeRef& ty) override {
                upperVisitor.visitType(ty);
            }

            void visitPathParams(HIRPathParams& pp) override {
                upperVisitor.visitPathParams(pp);
            }

            void visitPath(HIRVisitor::PathContext pc, HIRPath& path) override {
                upperVisitor.visitPath(path, pc);
            }

            void visitPattern(const Span& sp, HIRPattern& pat) override {
                upperVisitor.visitPattern(pat);
            }

            void visitNodePtr(HIRExprNodeP& nodePtr) {
                HIRExprVisitorDef::visitNodePtr(nodePtr);
                if (mReplacement) {
                    mReplacement->resType = nodePtr->resType;
                    mReplacement.swap(nodePtr);
                    mReplacement.reset();
                }
            }

            // Custom to visit the inner expression
            void visit(HIRExprNodeArraySized& node) override {
                auto& as = node.mSize;
                if (as.is_Unevaluated()) {
                    upperVisitor.visitConstgeneric(as.as_Unevaluated());
                }
                HIRExprVisitorDef::visit(node);
            }

            // Custom visitor for enum/struct constructors
            void visit(HIRExprNodeCallPath& node) override {
                HIRExprVisitorDef::visit(node);
                const Span& sp = node.span();
                if (node.mPath.mData.is_Generic()) {
                    // If it points to an enum, rewrite
                    auto& gp = node.mPath.mData.as_Generic();
                    if (gp.mPath.components().size() > 1) {
                        const auto& ent = upperVisitor.crate.getTypeitemByPath(sp, gp.mPath, /*ign_crate*/ false, true);
                        if (ent.is_Enum() && ent.as_Enum().findVariant(gp.mPath.components().back()) != SIZE_MAX) {
                            // Rewrite!
                            mReplacement.reset(upperVisitor.crate.pool->make<HIRExprNodeTupleVariant>(sp, mv$(gp), /*is_struct*/ false, mv$(node.mArgs)));
                            DEBUG(&node << ": Replacing with TupleVariant " << mReplacement.get());
                            return;
                        }
                    }
                }

                // If this is pointing at a constant/static/associated constant, change to CallValue
                MonomorphState discard(upperVisitor.crate.types);
                auto v = upperVisitor.mResolve.getValue(node.span(), node.mPath, discard, true);
                if (v.is_Constant() || v.is_Static()) {
                    auto* valueNode = upperVisitor.crate.pool->make<HIRExprNodePathValue>(sp, std::move(node.mPath), v.is_Constant() ? HIRExprNodePathValue::Target::CONSTANT : v.is_Static() ? HIRExprNodePathValue::Target::STATIC : HIRExprNodePathValue::Target::UNKNOWN);
                    valueNode->resType = upperVisitor.crate.types.infer();
                    mReplacement.reset(upperVisitor.crate.pool->make<HIRExprNodeCallValue>(sp, HIRExprNodeP(valueNode), mv$(node.mArgs)));
                    DEBUG(&node << ": Replacing with CallValue " << mReplacement.get());
                    return;
                }
            }

            // Custom visitor for enum/struct constructors
            void visit(HIRExprNodePathValue& node) override {
                HIRExprVisitorDef::visit(node);
                const Span& sp = node.span();
                if (node.mPath.mData.is_Generic()) {
                    // If it points to an enum, set binding
                    auto& gp = node.mPath.mData.as_Generic();
                    if (gp.mPath.components().size() > 1) {
                        const auto& ent = upperVisitor.crate.getTypeitemByPath(sp, gp.mPath, /*ign_crate*/ false, true);
                        if (ent.is_Enum()) {
                            const auto& enm = ent.as_Enum();
                            auto idx = enm.findVariant(gp.mPath.components().back());
                            if (enm.mData.is_Value() || enm.mData.as_Data().at(idx).type == upperVisitor.crate.types.unit()) {
                                mReplacement.reset(upperVisitor.crate.pool->make<HIRExprNodeUnitVariant>(sp, mv$(gp), /*is_struct*/ false));
                                DEBUG(&node << ": Replacing with UnitVariant " << mReplacement.get());
                            } else {
                                node.target = HIRExprNodePathValue::ENUM_VAR_CONSTR;
                            }
                            return;
                        }
                    }

                    // TODO: Struct?
                }
            }
            void visit(HIRExprNodeStructLiteral& node) override {
                HIRExprVisitorDef::visit(node);
                const Span& sp = node.span();
                if (node.mType->is_Path() && node.mType->as_Path().path.mData.is_Generic()) {
                    // If it points to an enum, set binding
                    auto data = node.mType->cloneData();
                    auto& p = data.as_Path().path;
                    auto& gp = p.mData.as_Generic();
                    if (gp.mPath.components().size() > 1) {
                        const auto& ent = upperVisitor.crate.getTypeitemByPath(sp, gp.mPath, /*ign_crate*/ false, true);
                        if (ent.is_Enum()) {
                            DEBUG(&node << ": Tagging as an enum");
                            node.isStruct = false;
                            auto enumPath = std::move(gp);
                            auto varName = enumPath.mPath.popComponent();
                            auto enumTy = upperVisitor.crate.types.path(std::move(enumPath), &ent.as_Enum());
                            p = HIRPath(std::move(enumTy), std::move(varName));
                        }
                    }
                    node.mType = upperVisitor.crate.types.intern(std::move(data));
                }
            }

            // NOTE: Custom needed for trait scoping
            void visit(HIRExprNodeBlock& node) override {
                if (node.traits.size() == 0 && node.localMod.components().size() > 0) {
                    const auto& mod = upperVisitor.crate.getModByPath(node.span(), node.localMod);
                    for (const auto& traitPath : mod.traits) {
                        node.traits.push_back(::std::make_pair(&traitPath, &upperVisitor.crate.getTraitByPath(node.span(), traitPath)));
                    }
                }
                for (const auto& traitRef : node.traits) {
                    upperVisitor.traits.push_back(traitRef);
                }

                HIRExprVisitorDef::visit(node);

                for (unsigned int i = 0; i < node.traits.size(); i++) {
                    upperVisitor.traits.pop_back();
                }
            }
        };

        if (mVisitExprs && expr.get() != nullptr) {
            auto savedInExpr = inExpr;
            inExpr = true;
            ExprVisitor v{*this};
            (*expr).visit(v);
            inExpr = savedInExpr;
        }
    }

    bool locateTraitItemInBounds(HIRVisitor::PathContext pc, const HIRTypeData* tr, const HIRGenericParams& params, HIRPath::Data& pd) {
        static Span sp;
        for (const auto& b : params.bounds) {
            if (const auto* e = b.opt_TraitBound()) {
                DEBUG("- " << e->type << " : " << e->trait.mPath);
                // Bounds are keyed by the semantic HIR type. Binding
                // metadata and erased regions can differ depending on
                // which path was resolved first.
                if (e->type == tr || e->type->equalsIgnoringRegions(tr)) {
                    DEBUG(" - Match");
                    if (locateInTraitAndSet(pc, e->trait.mPath, crate.getTraitByPath(sp, e->trait.mPath.mPath), pd)) {
                        return true;
                    }
                }
            }
            // -
        }
        return false;
    }

    HIRPath::Data getUfcsKnown(HIRVisitor::PathContext pc, HIRPath::Data::Data_UfcsUnknown e, HIRGenericPath traitPathReal, const HIRTrait& trait) const {
        struct MonomorphEraseHrls: public Monomorphiser {
            explicit MonomorphEraseHrls(HIRTypeInterner& types)
                : Monomorphiser(types)
            {
            }

            HIRTypeRef getType(const Span& sp, const HIRGenericRef& g) const override {
                return types.generic(g.name, g.binding);
            }

            HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& g) const override {
                return g;
            }

            HIRLifetimeRef getLifetime(const Span& sp, const HIRGenericRef& g) const override {
                if (g.group() == 3) {
                    return HIRLifetimeRef();
                } else {
                    return HIRLifetimeRef(g.binding);
                }
            }
        };

        auto traitPath = MonomorphEraseHrls(crate.types).monomorphGenericpath(Span(), traitPathReal);
        if (pc == HIRVisitor::PathContext::TYPE) {
            // If the trait has missing type argumenst, replace them with the defaults
            // Get trait, check if the type has ATCs
            const auto& aty = trait.types.at(e.item);
            if (e.params.mLifetimes.size() < aty.generics.mLifetimes.size()) {
                e.params.mLifetimes.resize(aty.generics.mLifetimes.size());
            }
        }
        // TODO: Only do this when there's multiple options?
        if (inExpr) {
            for (auto& type : traitPath.mParams.types) {
                type = crate.types.infer();
            }
        }
        return HIRPath::Data::make_UfcsKnown({mv$(e.type), mv$(traitPath), mv$(e.item), mv$(e.params)});
    }

    static bool locateItemInTrait(HIRVisitor::PathContext pc, const HIRTrait& trait, HIRPath::Data& pd) {
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

    // Locate the item in `pd` and set `pd` to UfcsResolved if found
    // TODO: This code may end up generating paths without the type information they should contain
    // OR, generate paths with too much type information
    bool locateInTraitAndSet(HIRVisitor::PathContext pc, const HIRGenericPath& traitPath, const HIRTrait& trait, HIRPath::Data& pd) {
        TRACE_FUNCTION_F(traitPath);
        // TODO: Get the span from caller
        static Span _sp;
        const auto& sp = _sp;
        if (locateItemInTrait(pc, trait, pd)) {
            pd = getUfcsKnown(pc, mv$(pd.as_UfcsUnknown()), traitPath.clone(), trait);
            return true;
        }

        auto pp = traitPath.mParams.clone();
        while (pp.types.size() < trait.mParams.types.size()) {
            auto idx = pp.types.size();
            const auto& def = trait.mParams.types[idx].defaultValue;
            if (def->is_Infer()) {
                ERROR(sp, E0000, "");
            }
            if (def == crate.types.self()) {
                // TODO: This has to be the _exact_ same type, including future ivars.
                pp.types.push_back(pd.as_UfcsUnknown().type);
                continue;
            }
            TODO(sp, "Monomorphise default arg " << def << " for trait path " << traitPath);
        }

        auto monomorphCb = MonomorphStatePtr(crate.types, pd.as_UfcsUnknown().type, &pp, nullptr);
        HIRGenericPath parTraitPathTmp;
        auto monomorphGpIfNeeded = [&](const HIRGenericPath& tpl) -> const HIRGenericPath& {
            // NOTE: This doesn't monomorph if the parameter set is the same
            if (monomorphiseGenericpathNeeded(tpl) /*&& tpl.m_params != trait_path.m_params*/) {
                DEBUG("[monomorph_gp_if_needed] Monomorph tpl=" << tpl);
                return parTraitPathTmp = monomorphCb.monomorphGenericpath(sp, tpl, false /*no infer*/);
            } else {
                return tpl;
            }
        };

        // Search supertraits (recursively)
        static HIRGenericParams emptyGp;
        for (const auto& pt : trait.parentTraits) {
            auto _ = monomorphCb.pushHrb(pt.hrtbs ? *pt.hrtbs : emptyGp);
            const auto& parTraitPath = monomorphGpIfNeeded(pt.mPath);
            DEBUG("- Check " << parTraitPath);
            if (locateInTraitAndSet(pc, parTraitPath, *pt.traitPtr, pd)) {
                return true;
            }
        }
        for (const auto& pt : trait.allParentTraits) {
            auto _ = monomorphCb.pushHrb(pt.hrtbs ? *pt.hrtbs : emptyGp);
            const auto& parTraitPath = monomorphGpIfNeeded(pt.mPath);
            DEBUG("- Check (all) " << parTraitPath);
            if (locateItemInTrait(pc, *pt.traitPtr, pd)) {
                // TODO: Don't clone if this is from the temp.
                pd = getUfcsKnown(pc, mv$(pd.as_UfcsUnknown()), parTraitPath.clone(), *pt.traitPtr);
                return true;
            }
        }
        return false;
    }

    bool setFromTraitImpl(const Span& sp, HIRVisitor::PathContext pc, const HIRGenericPath& traitPath, const HIRTrait& trait, HIRPath::Data& pd) {
        auto& e = pd.as_UfcsUnknown();
        const auto& type = e.type;
        TRACE_FUNCTION_F("trait_path=" << traitPath << ", p=<" << type << " as _>::" << e.item);

        // TODO: This is VERY arbitary and possibly nowhere near what rustc does.
        // NOTE: `nullptr` passed for param count, as defaults are not yet expanded
        this->mResolve.findImpl(sp, traitPath.mPath, nullptr, type, [&](const auto& impl, bool fuzzy) -> bool {
            auto pp = impl.getTraitParams(crate.types);
            // Replace all placeholder parameters (group 2) with ivars (empty types)
            struct KillPlaceholders: public Monomorphiser {
                explicit KillPlaceholders(HIRTypeInterner& types)
                    : Monomorphiser(types)
                {
                }

                HIRTypeRef getType(const Span& sp, const HIRGenericRef& ty) const override {
                    if (ty.isPlaceholder()) {
                        return types.infer();
                    }
                    return types.generic(ty.name, ty.binding);
                }
                HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& val) const override {
                    return val.isPlaceholder() ? HIRConstGeneric() : HIRConstGeneric(val);
                }
                HIRLifetimeRef getLifetime(const Span& sp, const HIRGenericRef& g) const override {
                    return HIRLifetimeRef(g.binding);
                }
            };

            pp = KillPlaceholders(crate.types).monomorphPathParams(sp, pp, true);
            DEBUG("FOUND impl from " << impl);
            // If this has already found an option...
            if (auto* innerE = pd.opt_UfcsKnown()) {
                // Compare all path params, and set different params to _
                assert(pp.types.size() == innerE->trait.mParams.types.size());
                for (unsigned int i = 0; i < pp.types.size(); i++) {
                    auto& eTy = innerE->trait.mParams.types[i];
                    const auto& thisTy = pp.types[i];
                    if (eTy->is_Infer() && eTy->as_Infer().index == ~0u) {
                        // Already _, leave as is
                    } else if (eTy != thisTy) {
                        eTy = crate.types.infer();
                    } else {
                        // Equal, good
                    }
                }
            } else {
                DEBUG("pp = " << pp);
                // Otherwise, set to the current result.
                pd = getUfcsKnown(pc, mv$(e), HIRGenericPath(traitPath.mPath, mv$(pp)), trait);
            }
            return false;
        });
        return pd.is_UfcsKnown();
    }

    bool locateInTraitImplAndSet(const Span& sp, HIRVisitor::PathContext pc, const HIRGenericPath& traitPath, const HIRTrait& trait, HIRPath::Data& pd) {
        if (this->locateItemInTrait(pc, trait, pd)) {
            return setFromTraitImpl(sp, pc, traitPath, trait, pd);
        } else {
            DEBUG("- Item " << pd.as_UfcsUnknown().item << " not in trait " << traitPath.mPath);
        }

        // Search supertraits (recursively)
        // NOTE: This runs before "Resolve HIR Markings", so m_all_parent_traits can't be used exclusively
        for (const auto& pt : trait.parentTraits) {
            // TODO: Modify path parameters based on the current trait's params
            if (locateInTraitImplAndSet(sp, pc, pt.mPath, *pt.traitPtr, pd)) {
                return true;
            }
        }
        for (const auto& pt : trait.allParentTraits) {
            if (this->locateItemInTrait(pc, *pt.traitPtr, pd)) {
                // TODO: Modify path parameters based on the current trait's params
                return setFromTraitImpl(sp, pc, pt.mPath, *pt.traitPtr, pd);
            } else {
                DEBUG("- Item " << pd.as_UfcsUnknown().item << " not in trait " << traitPath.mPath);
            }
        }
        return false;
    }

    bool resolve_UfcsUnknown_inherent(const HIRSimplePath& visPath, const HIRPath& p, HIRVisitor::PathContext pc, HIRPath::Data& pd) {
        auto& e = pd.as_UfcsUnknown();
        TRACE_FUNCTION_F(e.type);
        return crate.findTypeImpls(e.type, HIRResolvePlaceholdersNop(), [&](const auto& impl) {
            DEBUG("- matched inherent impl" << impl.mParams.fmtArgs() << " " << impl.mType);
            // Search for item in this block
            switch (pc) {
                case HIRVisitor::PathContext::VALUE:
                    if (impl.methods.find(e.item) != impl.methods.end()) {
                        // HACK: Allow access to privates of `fmt:rt::Argument`
                        if (e.type->is_Path() && e.type->as_Path().path.mData.is_Generic() && e.type->as_Path().path.mData.as_Generic().mPath == crate.getLangItemPathOpt("format_argument")) {
                            // Allow
                        } else if (!impl.methods.at(e.item).publicity.isVisible(visPath)) {
                            DEBUG("Private");
                            return false;
                        }
                    } else if (impl.constants.find(e.item) != impl.constants.end()) {
                        if (!impl.constants.at(e.item).publicity.isVisible(visPath)) {
                            DEBUG("Private");
                            return false;
                        }
                    } else {
                        return false;
                    }
                    // Found it, just keep going (don't care about details here)
                    break;
                case HIRVisitor::PathContext::TRAIT:
                    return false;
                case HIRVisitor::PathContext::TYPE:
                    if (impl.types.find(e.item) == impl.types.end()) {
                        return false;
                    }
                    if (!impl.types.at(e.item).publicity.isVisible(visPath)) {
                        DEBUG("Private");
                        return false;
                    }
                    break;
            }

            auto newData = HIRPath::Data::make_UfcsInherent({mv$(e.type), mv$(e.item), mv$(e.params)});
            pd = mv$(newData);
            DEBUG("- Resolved, replace with " << p);
            return true;
        });
    }

    bool resolve_UfcsUnknown_trait(const HIRPath& p, HIRVisitor::PathContext pc, HIRPath::Data& pd) {
        static Span sp;
        auto& e = pd.as_UfcsUnknown();
        const bool collapseToSubtrait = crate.featureEnabled("supertrait_item_shadowing");
        ::std::vector<::std::pair<HIRSimplePath, HIRPath::Data>> candidates;
        DEBUG("m_traits.size() = " << traits.size());
        for (const auto& traitInfo : traits) {
            const auto& trait = *traitInfo.second;

            DEBUG(e.item << " in? " << *traitInfo.first);
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
            DEBUG("- Trying trait " << *traitInfo.first);

            auto traitPath = HIRGenericPath(*traitInfo.first);
            traitPath.mParams.types.reserve(trait.mParams.types.size());
            for (size_t i = 0; i < trait.mParams.types.size(); i++) {
                traitPath.mParams.types.push_back(crate.types.infer());
            }

            // TODO: If there's only one trait with this name, assume it's the correct one.

            // TODO: Search supertraits
            // TODO: Should impls be searched first, or item names?
            // - Item names add complexity, but impls are slower
            if (!collapseToSubtrait) {
                if (this->locateInTraitImplAndSet(sp, pc, mv$(traitPath), trait, pd)) {
                    return true;
                }
                continue;
            }

            auto candidateData = HIRPath::Data::make_UfcsUnknown({
                e.type,
                e.item,
                e.params.clone(),
            });
            if (this->locateInTraitImplAndSet(sp, pc, mv$(traitPath), trait, candidateData)) {
                candidates.push_back(::std::make_pair(*traitInfo.first, mv$(candidateData)));
            }
        }

        if (collapseToSubtrait && !candidates.empty()) {
            ::std::vector<HIRSimplePath> candidateTraits;
            candidateTraits.reserve(candidates.size());
            for (const auto& candidate : candidates) {
                candidateTraits.push_back(candidate.first);
            }
            if (const auto selected = crate.findMostSpecificTrait(sp, candidateTraits)) {
                pd = mv$(candidates[*selected].second);
                return true;
            }
        }
        return false;
    }

    void visitType(HIRTypeRef& ty) override {
        // TODO: Add a span parameter.
        static Span sp;

        HIRVisitor::visitType(ty);

        // TODO: If this an associated type, check for default trait params

        if (runEat) {
            TRACE_FUNCTION_FR(ty, ty);
            std::vector<HIRTypeRef> stack;
            if (ty->is_Path()) {
                stack.push_back(ty);
            }
            while (mResolve.expandAssociatedTypesSingle(sp, ty)) {
                if (::std::find(stack.begin(), stack.end(), ty) != stack.end()) {
                    ::std::sort(stack.begin(), stack.end());
                    DEBUG("Loop detected, picking " << ty);
                    ty = std::move(stack[0]);
                    HIRVisitor::visitType(ty);
                    break;
                }
                // NOTE: Only need to clone if this is a Path, as that's the only way we could loop again
                if (ty->is_Path()) {
                    stack.push_back(ty);
                }
                DEBUG("counter = " << stack.size());
                rewriteTyWith(crate.types, ty, [&](HIRTypeRef& rewritten, HIRTypeData& data) -> bool {
                    if (TU_TEST1(data, Generic, .isPlaceholder())) {
                        rewritten = crate.types.infer();
                    }
                    return false;
                });
                ASSERT_BUG(sp, stack.size() < 20, "Sanity limit exceeded when resolving UFCS in type " << ty);
                // Invoke a special version of EAT that only processes a single item.
                // - Keep recursing while this does replacements
                HIRVisitor::visitType(ty);
            }
        }
    }

    void visitConstgeneric(HIRConstGeneric& val) override {
        auto savedVisitExprs = mVisitExprs;
        mVisitExprs = true;
        HIRVisitor::visitConstgeneric(val);
        mVisitExprs = savedVisitExprs;
    }

    void visitPath(HIRPath& p, HIRVisitor::PathContext pc) override {
        static Span sp;

        if (auto* pe = p.mData.opt_UfcsKnown()) {
            // If the trait has missing type argumenst, replace them with the defaults
            auto& tp = pe->trait;
            const auto& trait = mResolve.crate.getTraitByPath(sp, tp.mPath);

            if (tp.mParams.types.size() < trait.mParams.types.size()) {
                //TODO(sp, "Defaults in UfcsKnown - " << p << " - " << tp.m_params << " vs " << trait.m_params.fmt_args());
                // TOOD: Where does this usually get expanded then?
            }
        }

        // TODO: Would like to remove this, but it's required still (for expressions)
        if (auto* pe = p.mData.opt_UfcsUnknown()) {
            auto& e = *pe;
            TRACE_FUNCTION_FR("UfcsUnknown - p=" << p, p);

            this->visitType(e.type);
            this->visitPathParams(e.params);

            // If processing a trait, and the type is 'Self', search for the type/method on the trait
            // - Explicitly encoded because `Self::Type` has a different meaning to `MyType::Type` (the latter will search bounds first)
            // - NOTE: Could be in an inherent block, where there's no trait
            if (/*m_current_type &&*/ currentTrait && e.type == crate.types.self()) {
                HIRGenericPath traitPath;
                if (mCurrentTraitPath->traitPath()) {
                    traitPath = HIRGenericPath(*mCurrentTraitPath->traitPath());
                    traitPath.mParams = mCurrentTraitPath->traitArgs()->clone();
                } else {
                    traitPath = HIRGenericPath(mCurrentTraitPath->getSimplePath());
                    traitPath.mParams = currentTrait->mParams.makeNopParams(crate.types, 0);
                }
                if (locateInTraitAndSet(pc, traitPath, *currentTrait, p.mData)) {
                    assert(!p.mData.is_UfcsUnknown());
                    // Success!
                    // - If in an expression (and not in a `trait` provided impl), clear the params
                    if (inExpr && !mInTraitDef) {
                        for (auto& t : p.mData.as_UfcsKnown().trait.mParams.types) {
                            t = crate.types.infer();
                        }
                    }
                    DEBUG("Found in Self (trait), p = " << p);
                    return;
                }
                DEBUG("- Item " << e.item << " not found in Self - ty=" << e.type);
            }

            // NOTE: Replace `Self` now
            // - Now that the only `Self`-specific logic is done, replace so the lookup code works.
            if (mCurrentType) {
                rewritePathTysWith(crate.types, p, [&](HIRTypeRef& t, HIRTypeData& data) -> bool {
                    if (data.is_Generic() && data.as_Generic().binding == GENERICSelf) {
                        t = mCurrentType;
                    }
                    return false;
                });
            }

            // Search for matching impls in current generic blocks
            if (mResolve.mItemGenerics != nullptr && locateTraitItemInBounds(pc, e.type, *mResolve.mItemGenerics, p.mData)) {
                DEBUG("Found in item params, p = " << p);
                assert(!p.mData.is_UfcsUnknown());
                return;
            }
            if (mResolve.mImplGenerics != nullptr && locateTraitItemInBounds(pc, e.type, *mResolve.mImplGenerics, p.mData)) {
                DEBUG("Found in impl params, p = " << p);
                assert(!p.mData.is_UfcsUnknown());
                return;
            }

            // `<dyn Trait>::item` can name an item supplied by a supertrait.
            // Resolve it from the trait object's principal trait before
            // looking for an implementation of the trait object type.
            if (const auto* traitObject = e.type->opt_TraitObject()) {
                const auto& principal = traitObject->mTrait;
                if (principal.traitPtr && locateInTraitAndSet(pc, principal.mPath, *principal.traitPtr, p.mData)) {
                    DEBUG("Found in trait object bounds, p = " << p);
                    assert(!p.mData.is_UfcsUnknown());
                    return;
                }
            }

            // TODO: Control ordering with a flag in UfcsUnknown
            // 1. Search for applicable inherent methods (COMES FIRST!)
            if (this->resolve_UfcsUnknown_inherent(curModPath, p, pc, p.mData)) {
                assert(!p.mData.is_UfcsUnknown());
                return;
            }
            assert(p.mData.is_UfcsUnknown());

            // If the type is the impl type, look for items AFTER generic lookup
            // TODO: Should this look up in-scope traits instead of hard-coding this hack?
            if (mCurrentType && currentTrait && e.type == mCurrentType) {
                HIRGenericPath traitPath;
                if (mCurrentTraitPath->traitPath()) {
                    traitPath = HIRGenericPath(*mCurrentTraitPath->traitPath());
                    traitPath.mParams = mCurrentTraitPath->traitArgs()->clone();
                } else {
                    traitPath = HIRGenericPath(mCurrentTraitPath->getSimplePath());
                    traitPath.mParams = currentTrait->mParams.makeNopParams(crate.types, 0);
                }

                if (locateInTraitAndSet(pc, traitPath, *currentTrait, p.mData)) {
                    assert(!p.mData.is_UfcsUnknown());
                    // Success!
                    if (inExpr) {
                        for (auto& t : p.mData.as_UfcsKnown().trait.mParams.types) {
                            t = crate.types.infer();
                        }
                    }
                    DEBUG("Found in Self (impl" << (inExpr ? " expr" : "") << "), p = " << p);
                    return;
                }
                DEBUG("- Item " << e.item << " not found in Self - ty=" << e.type);
            }

            // If the inner type is a UFCS of a known trait, then search traits on that type
            if (e.type->is_Path() && e.type->as_Path().path.mData.is_UfcsKnown()) {
                auto& innerPe = e.type->as_Path().path.mData.as_UfcsKnown();
                const auto& trait = crate.getTraitByPath(sp, innerPe.trait.mPath);
                const auto& atyDef = trait.types.at(innerPe.item);
                auto mstate = MonomorphStatePtr(crate.types, innerPe.type, &innerPe.trait.mParams, nullptr);
                for (const auto& t : atyDef.traitBounds) {
                    auto traitPath = mstate.monomorphGenericpath(sp, t.mPath, /*allow_infer*/ true);
                    DEBUG("Searching ATY bound: " << traitPath);
                    // Search within this (bounded) trait for the outer item
                    if (this->locateInTraitImplAndSet(sp, pc, mv$(traitPath), *t.traitPtr, p.mData)) {
                        assert(!p.mData.is_UfcsUnknown());
                        return;
                    }
                }
                DEBUG("- Item " << e.item << " not found in ATY bounds");
                // TODO: Search bounds with `where`?
            }

            // 2. Search all impls of in-scope traits for this method on this type
            if (this->resolve_UfcsUnknown_trait(p, pc, p.mData)) {
                assert(!p.mData.is_UfcsUnknown());
                return;
            }
            assert(p.mData.is_UfcsUnknown());
            DEBUG("e.type = " << e.type);

            // If the inner is an enum, look for an enum variant? (check context)
            if ((pc == HIRVisitor::PathContext::VALUE /*|| pc == HIR::Visitor::PathContext::PATTERN*/) && e.type->is_Path() && e.type->as_Path().binding.is_Enum()) {
                const auto& enm = *e.type->as_Path().binding.as_Enum();
                auto idx = enm.findVariant(e.item);
                if (idx != SIZE_MAX) {
                    DEBUG("Found variant " << e.type << " #" << idx);
                    if (enm.mData.is_Value() || !enm.mData.as_Data()[idx].isStruct) {
                        auto gp = e.type->as_Path().path.mData.as_Generic().clone();
                        gp.mPath += e.item;
                        if (e.params.hasParams()) {
                            ERROR(sp, E0000, "Type parameters on UFCS enum variant - " << p);
                        }
                        p = std::move(gp);
                        return;
                    } else {
                    }
                }
            }
            if (pc == HIRVisitor::PathContext::TYPE && e.type->is_Path() && e.type->as_Path().binding.is_Enum()) {
                const auto& enm = *e.type->as_Path().binding.as_Enum();
                auto idx = enm.findVariant(e.item);
                if (idx != SIZE_MAX) {
                    DEBUG("Found variant " << e.type << " #" << idx);
                    if (enm.mData.is_Data() && enm.mData.as_Data()[idx].isStruct) {
                        auto gp = e.type->as_Path().path.mData.as_Generic().clone();
                        gp.mPath += e.item;
                        if (e.params.hasParams()) {
                            ERROR(sp, E0000, "Type parameters on UFCS enum variant - " << p);
                        }
                        p = std::move(gp);
                        return;
                    } else {
                    }
                }
            }

            // Couldn't find it
            ERROR(sp, E0000, "Failed to find impl with '" << e.item << "' for " << e.type << " (in " << p << ")");
        } else {
            HIRVisitor::visitPath(p, pc);
        }
    }

    void visitPattern(HIRPattern& pat) override {
        static Span _sp = Span();
        const Span& sp = _sp;

        HIRVisitor::visitPattern(pat);

            TU_MATCH_HDRA( (pat.mData), {)
            default:
                break;
            TU_ARMA(Value, e) {
                this->visitPatternValue(sp, pat, e.val);
                if (e.val.is_Named() && e.val.as_Named().path.mData.is_Generic() && e.val.as_Named().path.mData.as_Generic().mPath.components().size() > 1) {
                    auto& gp = e.val.as_Named().path.mData.as_Generic();
                    if (const auto* enmP = crate.getTypeitemByPath(sp, gp.mPath, false, true).opt_Enum()) {
                        unsigned idx = enmP->findVariant(gp.mPath.components().back());
                        pat.mData = HIRPattern::Data::make_PathValue({mv$(gp), HIRPattern::PathBinding::make_Enum({enmP, idx})});
                    }
                }
            }
            TU_ARMA(Range, e) {
                if (e.start) {
                    this->visitPatternValue(sp, pat, *e.start);
                }
                if (e.end) {
                    this->visitPatternValue(sp, pat, *e.end);
                }
            }
            TU_ARMA(PathValue, e) {
                this->resolvePatternBinding(sp, e.path, e.binding);
            }
            TU_ARMA(PathTuple, e) {
                this->resolvePatternBinding(sp, e.path, e.binding);
            }
            TU_ARMA(PathNamed, e) {
                this->resolvePatternBinding(sp, e.path, e.binding);
            }
            }
    }

    void resolvePatternBinding(const Span& sp, HIRPath& path, HIRPattern::PathBinding& binding) {
        if (!binding.is_Unbound()) {
            return;
        }

        auto ty = crate.types.path(path.clone(), {});
        this->visitType(ty);
        ASSERT_BUG(sp, ty->is_Path(), "Pattern associated type didn't resolve to a path - " << ty);

        const auto& te = ty->as_Path();
        ASSERT_BUG(sp, te.path.mData.is_Generic(), "Pattern associated type didn't resolve to a generic path - " << ty);
        path = te.path.clone();

        if (te.binding.is_Struct()) {
            binding = HIRPattern::PathBinding::make_Struct(te.binding.as_Struct());
        } else if (te.binding.is_Union()) {
            binding = HIRPattern::PathBinding::make_Union(te.binding.as_Union());
        } else {
            ERROR(sp, E0000, "Pattern associated type didn't resolve to a struct or union - " << ty);
        }
    }

    void visitPatternValue(const Span& sp, const HIRPattern& pat, HIRPattern::Value& val) {
        TRACE_FUNCTION_F("pat=" << pat << ", val=" << val);
        if (auto* vep = val.opt_Named()) {
            auto& ve = *vep;
            TRACE_FUNCTION_F(ve.path);
                TU_MATCH_HDRA( (ve.path.mData), {)
                TU_ARMA(Generic, pe) {
                    // Already done
                }
                TU_ARMA(UfcsUnknown, pe) {
                    BUG(sp, "UfcsUnknown still in pattern value - " << pat);
                }
                TU_ARMA(UfcsInherent, pe) {
                    bool rv = crate.findTypeImpls(pe.type, HIRResolvePlaceholdersNop(), [&](const auto& impl) {
                        DEBUG("- matched inherent impl" << impl.mParams.fmtArgs() << " " << impl.mType);
                        // Search for item in this block
                        auto it = impl.constants.find(pe.item);
                        if (it != impl.constants.end()) {
                            ve.binding = &it->second.data;
                            return true;
                        }
                        return false;
                    });
                    if (!rv) {
                        ERROR(sp, E0000, "Constant " << ve.path << " couldn't be found");
                    }
                }
                TU_ARMA(UfcsKnown, pe) {
                    bool rv = this->mResolve.findImpl(sp, pe.trait.mPath, &pe.trait.mParams, pe.type, [&](const auto& impl, bool) {
                        if (!impl.mData.is_TraitImpl()) {
                            return true;
                        }
                        ve.binding = &impl.mData.as_TraitImpl().impl->constants.at(pe.item).data;
                        return true;
                    });
                    if (!rv) {
                        ERROR(sp, E0000, "Constant " << ve.path << " couldn't be found");
                    }
                }
                }
        }
    }
};

template <typename T>
void sortImplGroup(HIRCrate::ImplGroup<std::unique_ptr<T>>& ig, ::std::function<void(::std::ostream& os, const T&)> fmt) {
    auto newEnd = ::std::remove_if(ig.generic.begin(), ig.generic.end(), [&ig, &fmt](::std::unique_ptr<T>& tyImpl) {
        const auto& type = tyImpl->mType; // Using field accesses in templates feels so dirty
        const HIRSimplePath* path = type->getSortPath();

        if (path) {
            DEBUG(*path << " += " << FMT_CB(os, fmt(os, *tyImpl)));
            ig.named[*path].push_back(mv$(tyImpl));
        } else if (type->is_Path() || type->is_Generic()) {
            return false;
        } else {
            ig.nonNamed.push_back(mv$(tyImpl));
        }
        return true;
    });
    ig.generic.erase(newEnd, ig.generic.end());
}

// --- Indexing of trait impls ---
template <typename T>
void pushIndexImplGroupList(::std::vector<const T*>& dst, const ::std::vector<std::unique_ptr<T>>& src) {
    for (const auto& e : src) {
        dst.push_back(&*e);
    }
}

template <typename T>
void pushIndexImplGroup(HIRCrate::ImplGroup<const T*>& dst, const HIRCrate::ImplGroup<std::unique_ptr<T>>& src) {
    for (const auto& e : src.named) {
        pushIndexImplGroupList(dst.named[e.first], e.second);
    }
    pushIndexImplGroupList(dst.nonNamed, src.nonNamed);
    pushIndexImplGroupList(dst.generic, src.generic);
}

void pushIndexImpls(HIRCrate& dst, const HIRCrate& src) {
    pushIndexImplGroup(dst.allTypeImpls, src.typeImpls);
    for (const auto& ig : src.traitImpls) {
        pushIndexImplGroup(dst.allTraitImpls[ig.first], ig.second);
    }
    for (const auto& ig : src.markerImpls) {
        pushIndexImplGroup(dst.allMarkerImpls[ig.first], ig.second);
    }
}

// --- Indexing of inherent methods ---
void pushIndexInherentMethodsList(HIRInherentCache& icache, const HIRSimplePath& langBox, const ::std::vector<std::unique_ptr<HIRTypeImpl>>& src) {
    Span sp;
    for (const auto& ti : src) {
        const auto& impl = *ti;
        TRACE_FUNCTION_F("impl" << impl.mParams.fmtArgs() << " " << impl.mType);
        icache.insertAll(sp, impl, langBox);
    }
}

void pushIndexInherentMethods(HIRInherentCache& icache, const HIRSimplePath& langBox, const HIRCrate& src) {
    TRACE_FUNCTION_F("src = " << src.crateName);
    for (const auto& e : src.typeImpls.named) {
        pushIndexInherentMethodsList(icache, langBox, e.second);
    }
    pushIndexInherentMethodsList(icache, langBox, src.typeImpls.nonNamed);
    pushIndexInherentMethodsList(icache, langBox, src.typeImpls.generic);
}

void ConvertHIRResolveUFCSOuter(HIRCrate& crate) {
    for (auto& implGroup : crate.traitImpls) {
        auto expandList = [&](auto& implList) {
            for (auto& impl : implList) {
                expandTraitImplTypeDefaults(crate, implGroup.first, *impl);
            }
        };
        for (auto& named : implGroup.second.named) {
            expandList(named.second);
        }
        expandList(implGroup.second.nonNamed);
        expandList(implGroup.second.generic);
    }

    UfcsVisitor exp{crate, false};
    exp.visitCrate(crate);
}

void ConvertHIRResolveUFCS(HIRCrate& crate) {
    UfcsVisitor exp{crate, true};
    exp.visitCrate(crate);
}

void ConvertHIRResolveUFCSExpr(const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exprPtr) {
    TRACE_FUNCTION_F(ip);
    // Check innards but NOT the value
    UfcsVisitor exp{crate, true};
    exp.visitExpr(exprPtr);
}

void ConvertHIRResolveUFCSSortImpls(HIRCrate& crate) {
    // Sort impls!
    sortImplGroup<HIRTypeImpl>(crate.typeImpls, [](::std::ostream& os, const HIRTypeImpl& i) {
        os << "impl" << i.mParams.fmtArgs() << " " << i.mType;
    });
    DEBUG("Type impl counts: " << crate.typeImpls.named.size() << " path groups, " << crate.typeImpls.nonNamed.size() << " primitive, " << crate.typeImpls.generic.size() << " ungrouped");
    for (auto& implGroup : crate.traitImpls) {
        sortImplGroup<HIRTraitImpl>(implGroup.second, [&](::std::ostream& os, const HIRTraitImpl& i) {
            os << "impl" << i.mParams.fmtArgs() << " " << implGroup.first << i.traitArgs << " for " << i.mType;
        });
    }
    for (auto& implGroup : crate.markerImpls) {
        sortImplGroup<HIRMarkerImpl>(implGroup.second, [&](::std::ostream& os, const HIRMarkerImpl& i) {
            os << "impl" << i.mParams.fmtArgs() << " " << implGroup.first << i.traitArgs << " for " << i.mType << " {}";
        });
    }

    // Create indexes
    pushIndexImpls(crate, crate);
    for (const auto& ec : crate.extCrates) {
        pushIndexImpls(crate, *ec.second.mData);
    }

    {
        const auto& langBox = crate.getLangItemPathOpt("owned_box");
        pushIndexInherentMethods(crate.inherentMethodCache, langBox, crate);
        for (const auto& ec : crate.extCrates) {
            pushIndexInherentMethods(crate.inherentMethodCache, langBox, *ec.second.mData);
        }
    }
}
