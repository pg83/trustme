#include "hir_conv_main_bindings.h"
#include "hir_conv_main_bindings.h"

#include "hir_hir.h"
#include "mir_mir.h"
#include "hir_expr.h"
#include "wire_board.h"
#include "hir_visitor.h"
#include "mir_helpers.h"
#include "hir_expr_state.h"
#include "hir_typeck_common.h" // monomorphise_type_with
#include "hir_typeck_static.h"
#include "hir_inherent_cache.h"
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
        const ::std::vector<HIRSimplePath>* defineOpaque = nullptr;
        unsigned int fcnErasedCount = 0;

    public:
        BindVisitor(const WireBoard& wb)
            : HIRVisitor(nullptr, wb.crate->types)
            , crate(*wb.crate)
            , ms(wb)
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
                             data = HIRTypeData::make_TraitObject({HIRTraitPath{mv$(pe), {}, {}}, {}});)
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
                            fcnPtr->mParams.bounds.push_back(HIRGenericBound::make_TraitBound({newTy, m.monomorphTraitpath(sp, trait, false)}));
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

        void visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) override {
            auto _ = this->ms.setImplGenerics(item.mParams);
            HIRVisitor::visitTypeAlias(p, item);
        }

        void visitFunction(HIRItemPath p, HIRFunction& item) override {
            auto _ = this->ms.setItemGenerics(item.mParams);
            fcnPtr = &item;
            defineOpaque = &item.defineOpaque;

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
            defineOpaque = nullptr;
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
            if (!expr.state) {
                expr.state = HIRExprStatePtr(crate.pool, HIRExprState(crate.types, *curModule.ptr, curModule.path->getSimplePath()));
                expr.state->traits = ms.traits; // TODO: Only obtain the current module's set
                expr.state->mImplGenerics = ms.mImplGenerics;
                expr.state->mItemGenerics = ms.mItemGenerics;
                expr.state->currentTraitImpl = ms.currentTraitImpl;
                if (ms.currentTrait) {
                    expr.state->mCurrentTraitPath = ms.currentTrait->mPath;
                }
                if (defineOpaque) {
                    expr.state->defineOpaque = *defineOpaque;
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
                        return monomorphCb.monomorphTraitpath(sp, tp, false);
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
        VisitorPost(const WireBoard& wb)
            : HIRVisitor(nullptr, wb.crate->types)
            , crate(*wb.crate)
            , ms(wb)
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
                    StaticTraitResolve resolve{ms.wb};
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

        void visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) override {
            auto _ = this->ms.setImplGenerics(item.mParams);
            HIRVisitor::visitTypeAlias(p, item);
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

void ConvertHIRBind(const WireBoard& wb, HIRCrate& crate) {
    {
        BindVisitor exp{wb};
        // Also visit extern crates to update their pointers
        for (auto& ec : crate.extCrates) {
            exp.visitCrate(*ec.second.mData);
        }
        exp.visitCrate(crate);
    }

    {
        VisitorPost v{wb};
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

                resizeTypeParams(gp.mParams, enm.mParams.types.size());
                gp.mParams.values.resize(enm.mParams.values.size());

                auto idx = ti.as_Enum().findVariant(gp.mPath.components().back());
                return HIRPattern::PathBinding::make_Enum({&enm, static_cast<unsigned>(idx)});
            }
        }

        const auto& ti = crate.getTypeitemByPath(sp, gp.mPath);
        if (ti.is_Union()) {
            const auto& unn = ti.as_Union();

            resizeTypeParams(gp.mParams, unn.mParams.types.size());
            gp.mParams.values.resize(unn.mParams.values.size());

            return HIRPattern::PathBinding::make_Union(&unn);
        }

        ASSERT_BUG(sp, ti.is_Struct(), "Pattern path " << gp.mPath << " didn't point to a struct or union (" << ti.tagStr() << ")");
        const auto& str = ti.as_Struct();

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
                    visitType(origType);

                    it = params.bounds.erase(it);
                    for (auto& t : n) {
                        auto type = origType;
                        it = params.bounds.insert(it, HIRGenericBound::make_TraitBound({std::move(type), std::move(t)}));
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

// Alias expansion clones unevaluated consts before the normal binding pass.
// Attach the alias definition's identity arguments before those clones exist.
class AliasConstGenericParamBinder: public HIRVisitor {
    const HIRGenericParams* implParams = nullptr;

    struct Guard {
        AliasConstGenericParamBinder& binder;
        const HIRGenericParams* old;

        Guard(AliasConstGenericParamBinder& binder, const HIRGenericParams& value)
            : binder(binder)
            , old(binder.implParams)
        {
            binder.implParams = &value;
        }

        ~Guard() {
            binder.implParams = old;
        }
    };

public:
    explicit AliasConstGenericParamBinder(HIRTypeInterner& types)
        : HIRVisitor(nullptr, types)
    {
    }

    void visitConstgeneric(HIRConstGeneric& value) override {
        if (auto* unevaluated = value.opt_Unevaluated()) {
            if (implParams && !(*unevaluated)->paramsImpl.hasParams()) {
                (*unevaluated)->paramsImpl = implParams->makeNopParams(typeInterner(), 0);
            }
        }
        HIRVisitor::visitConstgeneric(value);
    }

    void visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) override {
        Guard guard(*this, item.mParams);
        HIRVisitor::visitTypeAlias(p, item);
    }

    void visitTraitAlias(HIRItemPath p, HIRTraitAlias& item) override {
        Guard guard(*this, item.mParams);
        HIRVisitor::visitTraitAlias(p, item);
    }
};

void ConvertHIRExpandAliases(HIRCrate& crate) {
    AliasConstGenericParamBinder(crate.types).visitCrate(crate);
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
    UfcsVisitor(const WireBoard& wb, bool visitExprs)
        : HIRVisitor(nullptr, wb.crate->types)
        , crate(*wb.crate)
        , mVisitExprs(visitExprs)
        , runEat(visitExprs)
        , // Defaults to running when doing second-pass
        mResolve(wb)
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
        mResolve.forEachTypeEquality([&](HIRTypeRef& ty) {
            visitType(ty);
        });

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
        auto traitPath = traitPathReal.clone();
        if (pc == HIRVisitor::PathContext::TYPE) {
            // If the trait has missing type argumenst, replace them with the defaults
            // Get trait, check if the type has ATCs
            const auto& aty = trait.types.at(e.item);
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
            const auto& parTraitPath = monomorphGpIfNeeded(pt.mPath);
            DEBUG("- Check " << parTraitPath);
            if (locateInTraitAndSet(pc, parTraitPath, *pt.traitPtr, pd)) {
                return true;
            }
        }
        for (const auto& pt : trait.allParentTraits) {
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
            const auto& trait = mResolve.hirCrate().getTraitByPath(sp, tp.mPath);

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
            if (mResolve.itemGenericsPtr() != nullptr && locateTraitItemInBounds(pc, e.type, *mResolve.itemGenericsPtr(), p.mData)) {
                DEBUG("Found in item params, p = " << p);
                assert(!p.mData.is_UfcsUnknown());
                return;
            }
            if (mResolve.implGenericsPtr() != nullptr && locateTraitItemInBounds(pc, e.type, *mResolve.implGenericsPtr(), p.mData)) {
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

void ConvertHIRResolveUFCSOuter(const WireBoard& wb, HIRCrate& crate) {
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

    UfcsVisitor exp{wb, false};
    exp.visitCrate(crate);
}

void ConvertHIRResolveUFCS(const WireBoard& wb, HIRCrate& crate) {
    UfcsVisitor exp{wb, true};
    exp.visitCrate(crate);
}

void ConvertHIRResolveUFCSExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exprPtr) {
    TRACE_FUNCTION_F(ip);
    // Check innards but NOT the value
    UfcsVisitor exp{wb, true};
    exp.visitExpr(exprPtr);
}

void ConvertHIRResolveUFCSSortImpls(WireBoard& wb, HIRCrate& crate) {
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
        pushIndexInherentMethods(*wb.inherentMethods, langBox, crate);
        for (const auto& ec : crate.extCrates) {
            pushIndexInherentMethods(*wb.inherentMethods, langBox, *ec.second.mData);
        }
    }
}
