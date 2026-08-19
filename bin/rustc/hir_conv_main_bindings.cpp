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
                    if (ti.is_Struct()) {
                        auto& e2 = ti.as_Struct();
                        return &e2;
                    }
                    else {
                        ERROR(sp, E0000, "Expected a struct at " << path << ", got a " << ti.tagStr());
                    }
                    break;
                case Target::Enum:
                    if (ti.is_Enum()) {
                        auto& e2 = ti.as_Enum();
                        return &e2;
                    }
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
                // `_` is written the same way for a type and for a const
                // argument (`Foo<_>` where `Foo` takes `const N: usize`), so a
                // surplus placeholder belongs in the value list.
                while (params.types.size() > paramDefs.types.size()
                    && params.values.size() < paramDefs.values.size()
                    && params.types.back()->is_Infer()) {
                    params.types.pop_back();
                    params.values.push_back(HIRConstGeneric::make_Infer({}));
                }
                if (params.types.size() > paramDefs.types.size()) {
                    ERROR(sp, E0000, "Too many type parameters passed to " << path);
                }
            }
            if (params.types.size() < paramDefs.types.size()) {
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
                    } else if (val.defaultValue.is_Unevaluated()) {
                        // An unevaluated default carries the item's own generic
                        // arguments, which are exactly the ones still being
                        // filled here. Constant evaluation resolves it later,
                        // in a context that has them.
                        params.values.push_back(val.defaultValue.clone());
                    } else {
                        MonomorphStatePtr ms(types, selfTy, &params, nullptr);
                        params.values.push_back(ms.monomorphConstgeneric(sp, val.defaultValue, false));
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
        bool inImplTraitBinding = false;
        HIRTypeRef selfType = nullptr;

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
            curModule.ptr = &crate.rootModule;
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
            p.traitPtr = &crate.getTraitByPath(sp, p.path.path);

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
            bool isSingleValue = pat.data.is_Value();

            if (auto* ve = val.opt_Named()) {
                if (auto* pe = ve->path.data.opt_Generic()) {
                    const auto& path = pe->path;
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
                            fixTypeParams(crate.types, sp, enm->params, path.params);
                            pat.data = HIRPattern::Data::make_PathValue({mv$(path), HIRPattern::PathBinding::make_Enum({enm, static_cast<unsigned>(idx)})});
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
                        switch (it->second->ent.tag()) {
default:
                            ERROR(sp, E0000, "Value pattern " << pat << " pointing to unexpected item type - " << it->second->ent.tagStr());
                            case HIRValueItem::TAG_Constant: {
                                auto& e2 = it->second->ent.as_Constant();
                                // Store reference to this item for later use
                                ve->binding = &e2;
                                break;
                            }
                            case HIRValueItem::TAG_StructConstant: {
                                const auto& str = mod->modItems.find(pc)->second->ent.as_Struct();
                                // Convert into a dedicated pattern type
                                if (!isSingleValue) {
                                    ERROR(sp, E0000, "Struct in range pattern - " << pat);
                                }
                                auto path = mv$(*pe);
                                fixTypeParams(crate.types, sp, str.params, path.params);
                                pat.data = HIRPattern::Data::make_PathValue({mv$(path), &str});
                                break;
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

            switch (pat.data.tag()) {
default:
                // Nothing
                break;
                case HIRPatternData::TAG_Value: {
                    auto& e = pat.data.as_Value();
                    this->visitPatternValue(sp, pat, e.val);
                    break;
                }
                case HIRPatternData::TAG_Range: {
                    auto& e = pat.data.as_Range();
                    if (e.start) {
                        this->visitPatternValue(sp, pat, *e.start);
                    }
                    if (e.end) {
                        this->visitPatternValue(sp, pat, *e.end);
                    }
                    break;
                }
                case HIRPatternData::TAG_PathValue: {
                    break;
                }
                case HIRPatternData::TAG_PathTuple: {
                    break;
                }
                case HIRPatternData::TAG_PathNamed: {
                    break;
                }
            }
        }

        void visitConstgeneric(HIRConstGeneric& value) override {
            HIRVisitor::visitConstgeneric(value);
            if (auto* unevaluated = value.opt_Unevaluated()) {
                (*unevaluated)->selfType = selfType;
                if (ms.implGenerics) {
                    (*unevaluated)->paramsImpl = ms.implGenerics->makeNopParams(crate.types, 0);
                }
                if (ms.itemGenerics) {
                    (*unevaluated)->paramsItem = ms.itemGenerics->makeNopParams(crate.types, 1);
                }
            }
        }

        void visitParams(HIRGenericParams& params) override {
            static Span sp;
            for (auto& bound : params.bounds) {
                if (auto* be = bound.opt_TraitBound()) {
                    {
                        const auto& trait = crate.getTraitByPath(sp, be->trait.path.path);
                        fixParamCount(crate.types, sp, be->trait.path, trait.params, be->trait.path.params, /*fill_infer=*/false, be->type);
                    }
                    // Also ensure that the defaults are filled in the source traits
                    // - Is there a better solution to this? It feels like it would give the wrong answer (filling defaults incorrectly)
                    for (auto& aty : be->trait.typeBounds) {
                        const auto& trait = crate.getTraitByPath(sp, aty.second.sourceTrait.path);
                        fixParamCount(crate.types, sp, be->trait.path, trait.params, aty.second.sourceTrait.params, /*fill_infer=*/false, be->type);
                    }
                    for (auto& aty : be->trait.typeBounds) {
                        const auto& trait = crate.getTraitByPath(sp, aty.second.sourceTrait.path);
                        fixParamCount(crate.types, sp, be->trait.path, trait.params, aty.second.sourceTrait.params, /*fill_infer=*/false, be->type);
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
                const auto& trait = crate.getTraitByPath(sp, bound.path.path);
                fixParamCount(crate.types, sp, bound.path, trait.params, bound.path.params, /*fill_infer=*/false, ty);
            }
        }

        [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override {
            return visitTypeInner(ty);
        }

        [[nodiscard]] HIRTypeRef visitTypeInner(HIRTypeRef ty, bool doBind = true) {
            static Span sp;
            // Nodes this pass rewrites are handled below; every other
            // carrier kind still needs its embedded values to reach the
            // visitConstgeneric hook (which records generic bindings).
            if (!ty->is_Path() && !ty->is_TraitObject() && !ty->is_ErasedType()) {
                return visitTypeDefaultViaHooks(ty);
            }
            auto data = ty->cloneData();
            bool dataVisited = false;

            if (auto* e = data.opt_Path()) {
                switch (e->path.data.tag()) {
                    case HIRPathData::TAG_Generic: {
                        auto& pe = e->path.data.as_Generic();
                        if (!doBind) {
                            break;
                        }
                        const auto& item = *reinterpret_cast<const HIRTypeItem*>(getTypePointer(sp, crate, pe.path, Target::TypeItem));
                        switch (item.tag()) {
                            case HIRTypeItem::TAG_TypeAlias: {
                                BUG(sp, "TypeAlias encountered after `Resolve Type Aliases` - " << ty);
                                // Assume it'll be filled out, with the correct binding
                                break;
                            }
                            case HIRTypeItem::TAG_ExternType: {
                                auto& e3 = item.as_ExternType();
                                e->binding = HIRTypePathBinding::make_ExternType(&e3); DEBUG("- " << ty);
                                break;
                            }
                            case HIRTypeItem::TAG_Struct: {
                                auto& e3 = item.as_Struct();
                                fixParamCount(crate.types, sp, pe, e3.params, pe.params, /*fill_infer=*/inExpr != 0); e->binding = HIRTypePathBinding::make_Struct(&e3); DEBUG("- " << ty);
                                break;
                            }
                            case HIRTypeItem::TAG_Union: {
                                auto& e3 = item.as_Union();
                                fixParamCount(crate.types, sp, pe, e3.params, pe.params, /*fill_infer=*/inExpr != 0); e->binding = HIRTypePathBinding::make_Union(&e3); DEBUG("- " << ty);
                                break;
                            }
                            case HIRTypeItem::TAG_Enum: {
                                auto& e3 = item.as_Enum();
                                fixParamCount(crate.types, sp, pe, e3.params, pe.params, /*fill_infer=*/inExpr != 0); e->binding = HIRTypePathBinding::make_Enum(&e3); DEBUG("- " << ty);
                                break;
                            }
                            case HIRTypeItem::TAG_Trait: {
                                // TODO: Should this reassign instead?
                                data = HIRTypeData::make_TraitObject({HIRTraitPath{mv$(pe), {}, {}}, {}});
                                break;
                            }
                            default: {
                                ERROR(sp, E0000, "Unexpected item type returned for " << pe.path << " - " << item.tagStr());
                                break;
                            }
                        }
                        break;
                    }
                    case HIRPathData::TAG_UfcsUnknown: {
                        //TODO(sp, "Should UfcsKnown be encountered here?");
                        break;
                    }
                    case HIRPathData::TAG_UfcsInherent: {
                        break;
                    }
                    case HIRPathData::TAG_UfcsKnown: {
                        auto& pe = e->path.data.as_UfcsKnown();
                        const auto& trait = crate.getTraitByPath(sp, pe.trait.path);
                        fixParamCount(crate.types, sp, pe.trait, trait.params, pe.trait.params, /*fill_infer=*/false, pe.type);

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
                        break;
                    }
                }
            } else if (auto* te = data.opt_ErasedType()) {
                HIRTypeRef tyEself = crate.types.generic("ErasedSelf", GENERICErasedSelf);
                for (auto& t : te->traits) {
                    const auto& trait = crate.getTraitByPath(sp, t.path.path);
                    fixParamCount(crate.types, sp, t.path, trait.params, t.path.params, /*fill_infer=*/inExpr, tyEself);
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

                        HIRPathParams params = fcnPtr->params.makeNopParams(crate.types, 1);
                        // Populate with function path
                        ee->origin = fcnPath->getFullPath();
                        switch (ee->origin.data.tag()) {
                            case HIRPathData::TAG_Generic: {
                                auto& e2 = ee->origin.data.as_Generic();
                                e2.params = mv$(params);
                                break;
                            }
                            case HIRPathData::TAG_UfcsInherent: {
                                auto& e2 = ee->origin.data.as_UfcsInherent();
                                e2.params = mv$(params);
                                // Impl params, just directly references the parameters.
                                // - Downstream monomorph will fix that
                                e2.implParams = ms.implGenerics->makeNopParams(crate.types, 0);
                                break;
                            }
                            case HIRPathData::TAG_UfcsKnown: {
                                auto& e2 = ee->origin.data.as_UfcsKnown();
                                e2.params = mv$(params);
                                break;
                            }
                            case HIRPathData::TAG_UfcsUnknown: {
                                throw "";
                            }
                        }
                        ee->index = fcnErasedCount++;
                    }
                    // If the function _pointer_ is set (but not the path), then we're in the function arguments
                    // - Add a un-namable generic parameter (TODO: Prevent this from being explicitly set when called)
                    else if (fcnPtr) {
                        // Visit inner first, to handle nested
                        visitTypeDataChildren(data);
                        dataVisited = true;

                        size_t idx = fcnPtr->params.types.size();
                        auto name = RcString::newInterned(FMT("erased$" << idx));
                        DEBUG("-> " << name);
                        auto newTy = crate.types.generic(name, 256 + idx);
                        fcnPtr->params.types.push_back({name, crate.types.infer(), te->isSized});
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
                            fcnPtr->params.bounds.push_back(HIRGenericBound::make_TraitBound({newTy, m.monomorphTraitpath(sp, trait, false)}));
                        }
                        return newTy;
                    } else if (inImplTraitBinding) {
                        // `impl Trait` in a local binding is an inference type
                        // constrained by these bounds, not an opaque type with
                        // an identity. Type checking lowers it to ivars.
                    } else {
                        // TODO: If we're in a top-level `type`, then it must be used as the return type of a function.
                        // https://rust-lang.github.io/rfcs/2515-type_alias_impl_trait.html#type-alias
                        ERROR(sp, E0000, "Use of an erased type outside of a function return - " << ty);
                    }
                }
            } else if (auto* te = data.opt_TraitObject()) {
                if (te->trait.path.path != HIRSimplePath()) {
                    const auto& trait = crate.getTraitByPath(sp, te->trait.path.path);
                    fixParamCount(crate.types, sp, te->trait.path, trait.params, te->trait.path.params, /*fill_infer=*/inExpr, nullptr);
                }
                for (auto& m : te->markers) {
                    const auto& trait = crate.getTraitByPath(sp, m.path);
                    fixParamCount(crate.types, sp, m, trait.params, m.params, /*fill_infer=*/inExpr, nullptr);
                }
                DEBUG("- " << ty);
            }

            if (!dataVisited) {
                visitTypeDataChildren(data);
            }
            return crate.types.intern(mv$(data));
        }

        void visitTypeImpl(HIRTypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.type << " - from " << impl.srcModule);
            auto _ = this->ms.setImplGenerics(impl.params);
            const auto oldSelfType = selfType;
            selfType = impl.type;

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
            selfType = oldSelfType;
        }

        void visitInherentType(HIRItemPath p, HIRTypeAlias& item) override {
            auto _ = this->ms.setItemGenerics(item.params);
            HIRVisitor::visitInherentType(p, item);
        }

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
            TRACE_FUNCTION_F("impl " << traitPath << " for " << impl.type);
            auto traitGpath = HIRGenericPath(traitPath, impl.traitArgs.clone());
            auto _0 = this->ms.setCurrentTraitImpl(impl);
            auto _1 = this->ms.setCurrentTrait(traitGpath);
            auto _ = this->ms.setImplGenerics(impl.params);
            const auto oldSelfType = selfType;
            selfType = impl.type;

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
            selfType = oldSelfType;
        }

        void visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) override {
            TRACE_FUNCTION_F("impl " << traitPath << " for " << impl.type << " { }");
            auto _ = this->ms.setImplGenerics(impl.params);
            const auto oldSelfType = selfType;
            selfType = impl.type;

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
            selfType = oldSelfType;
        }

        void visitTrait(HIRItemPath p, HIRTrait& item) override {
            auto _ = this->ms.setImplGenerics(item.params);
            const auto oldSelfType = selfType;
            selfType = crate.types.self();
            HIRVisitor::visitTrait(p, item);
            selfType = oldSelfType;
        }

        void visitEnum(HIRItemPath p, HIREnum& item) override {
            auto _ = this->ms.setImplGenerics(item.params);
            HIRVisitor::visitEnum(p, item);
        }

        void visitStruct(HIRItemPath p, HIRStruct& item) override {
            auto _ = this->ms.setImplGenerics(item.params);
            HIRVisitor::visitStruct(p, item);
        }

        void visitUnion(HIRItemPath p, HIRUnion& item) override {
            auto _ = this->ms.setImplGenerics(item.params);
            HIRVisitor::visitUnion(p, item);
        }

        void visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) override {
            auto _ = this->ms.setImplGenerics(item.params);
            HIRVisitor::visitTypeAlias(p, item);
        }

        void visitFunction(HIRItemPath p, HIRFunction& item) override {
            auto _ = this->ms.setItemGenerics(item.params);
            fcnPtr = &item;
            defineOpaque = &item.defineOpaque;

            // Visit arguments
            // - Used to convert `impl Trait` in argument position into generics
            // - Done first so the path in return-position `impl Trait` is valid
            for (auto& arg : item.args) {
                TRACE_FUNCTION_F("ARG " << arg);
                arg.second = visitType(arg.second);
            }

            // Visit return type (populates path for `impl Trait` in return position
            fcnPath = &p;
            fcnErasedCount = 0;
            {
                TRACE_FUNCTION_F("RET " << item.returnType);
                item.returnType = visitType(item.returnType);
            }

            // Lowering an async function stores its declared return type both
            // as Future::Output and as the async block's return type. Binding
            // replaces the former with an interned type carrying the RPIT
            // identity, so keep the latter on that exact same type.
            if (auto* asyncBlock = cast<HIRExprNodeAsyncBlock>(item.code.get())) {
                if (const auto* erased = item.returnType->opt_ErasedType()) {
                    for (const auto& trait : erased->traits) {
                        const auto output = trait.typeBounds.find(RcString::newInterned("Output"));
                        if (output != trait.typeBounds.end()) {
                            asyncBlock->returnType = output->second.type;
                            break;
                        }
                    }
                }
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
            auto _ = this->ms.setItemGenerics(item.params);
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
                    ty = upperVisitor.visitTypeInner(ty, true);
                }

                void visitNodePtr(HIRExprNodeP& nodePtr) override {
                    // A node a desugaring built has no result type until type
                    // checking gives it one.
                    if (nodePtr->resType != HIRTypeRef()) {
                        nodePtr->resType = upperVisitor.visitType(nodePtr->resType);
                    }
                    HIRExprVisitorDef::visitNodePtr(nodePtr);
                }

                void visit(HIRExprNodeLet& node) override {
                    const bool saved = upperVisitor.inImplTraitBinding;
                    upperVisitor.inImplTraitBinding = true;
                    node.type = upperVisitor.visitType(node.type);
                    upperVisitor.visitPattern(node.pattern);
                    HIRExprVisitorDef::visit(node);
                    upperVisitor.inImplTraitBinding = saved;
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
                    upperVisitor.visitPath(node.path, HIRVisitor::PathContext::VALUE);
                }

                void visit(HIRExprNodeCallPath& node) override {
                    upperVisitor.visitPath(node.path, HIRVisitor::PathContext::VALUE);
                    HIRExprVisitorDef::visit(node);

                    // #[rustc_legacy_const_generics] - A backwards compatability hack added between 1.39 and 1.54 to be backwards compatible with the x86 intrinsics
                    // - Rewrites some literal arguments into const generics
                    if (auto* e = node.path.data.opt_Generic()) {
                        auto& fcn = upperVisitor.crate.getFunctionByPath(node.span(), e->path);
                        if (!fcn.markings.rustcLegacyConstGenerics.empty()) {
                            if (node.args.size() == fcn.args.size()) {
                                // Acceptable
                            } else if (node.args.size() == fcn.args.size() + fcn.markings.rustcLegacyConstGenerics.size()) {
                                for (auto idx : fcn.markings.rustcLegacyConstGenerics) {
                                    auto& argNode = node.args.at(idx);
                                    assert(argNode);
                                    // TODO: Check that the expression is a valid const (no locals referenced, no function calls?)
                                    // - Allow: Arithmatic, casts, literals
                                    //if( !cast<const HIR::ExprNodeLiteral>(arg_node.get()) )
                                    HIRExprPtr ep{std::move(argNode)};
                                    e->params.values.push_back(HIRConstGeneric(std::make_unique<HIRConstGenericUnevaluated>(std::move(ep))));
                                    // - Visit to ensure that the expr state gets filled
                                    upperVisitor.visitConstgeneric(e->params.values.back());
                                }
                                auto newEnd = std::remove_if(node.args.begin(), node.args.end(), [](const HIRExprNodeP& np) {
                                    return !np;
                                });
                                node.args.erase(newEnd, node.args.end());
                            } else {
                                // Will error downstream
                            }
                        }
                    }
                }

                void visit(HIRExprNodeCallMethod& node) override {
                    upperVisitor.visitPathParams(node.params);
                    HIRExprVisitorDef::visit(node);
                }

                void visit(HIRExprNodeStructLiteral& node) override {
                    node.type = upperVisitor.visitTypeInner(node.type, false);

                    HIRExprVisitorDef::visit(node);
                }

                void visit(HIRExprNodeArraySized& node) override {
                    auto& as = node.size;
                    if (as.is_Unevaluated()) {
                        upperVisitor.visitConstgeneric(as.as_Unevaluated());
                    }
                    HIRExprVisitorDef::visit(node);
                }

                void visit(HIRExprNodeClosure& node) override {
                    node.returnType = upperVisitor.visitType(node.returnType);
                    for (auto& arg : node.args) {
                        upperVisitor.visitPattern(arg.first);
                        arg.second = upperVisitor.visitType(arg.second);
                    }
                    HIRExprVisitorDef::visit(node);
                }
            };

            for (auto& ty : expr.erasedTypes) {
                ty = visitType(ty);
            }

            // Set up the module state
            if (!expr.state) {
                expr.state = HIRExprStatePtr(crate.pool, HIRExprState(crate.types, *curModule.ptr, curModule.path->getSimplePath()));
                expr.state->traits = ms.traits; // TODO: Only obtain the current module's set
                expr.state->implGenerics = ms.implGenerics;
                expr.state->itemGenerics = ms.itemGenerics;
                expr.state->currentTraitImpl = ms.currentTraitImpl;
                if (ms.currentTrait) {
                    expr.state->currentTraitPath = ms.currentTrait->path;
                }
                if (defineOpaque) {
                    expr.state->defineOpaque = *defineOpaque;
                } else if (!expr.defineOpaque.empty()) {
                    expr.state->defineOpaque = expr.defineOpaque;
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
                    updateType(ty);
                }

                struct MirVisitor: public MIRVisitorMut {
                    BindVisitor& upperVisitor;

                    MirVisitor(BindVisitor& upperVisitor)
                        : upperVisitor(upperVisitor)
                    {
                    }

                    void visitType(HIRTypeRef& t) override {
                        t = upperVisitor.visitType(t);
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
                    auto& params = path.path.params;

                    // Fill defaulted parameters.
                    // NOTE: Doesn't do much error checking.
                    fixParamCount(types, sp, path.path, tr.params, path.path.params, false, tySelf);

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
                        for (const auto& b : tr.params.bounds) {
                            if (!b.is_TraitBound()) {
                                continue;
                            }
                            const auto& be = b.as_TraitBound();
                            if (be.type != tySelf) {
                                continue;
                            }
                            const auto& pt = be.trait;
                            if (pt.path.path == path.path.path) {
                                continue;
                            }

                            enumSupertraitsIn(*pt.traitPtr, monomorphTp(pt));
                        }
                    }

                    // Build output path.
                    HIRTraitPath outPath;
                    outPath.path = mv$(path.path);
                    outPath.traitPtr = &tr;
                    fillTypeAliases(outPath);
                    // TODO: HRLs?
                    supertraits.push_back(std::move(outPath));
                    // Fill aliases from this path too
                    for (auto& st : supertraits) {
                        for (auto& tb : path.typeBounds) {
                            if (tb.second.sourceTrait == st.path) {
                                DEBUG("Add TypeBound: " << tb.first << " = " << tb.second.type);
                                st.typeBounds.insert(std::make_pair(tb.first, std::move(tb.second)));
                            }
                        }
                        for (auto& tb : path.traitBounds) {
                            if (tb.second.sourceTrait == st.path) {
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
                                outPath.typeBounds.insert(::std::make_pair(ty.first, HIRTraitPath::AtyEqual{outPath.path.clone(), {}, found}));
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
                                outPath.traitBounds.insert(::std::make_pair(ty.first, HIRTraitPath::AtyBound{outPath.path.clone(), {}, mv$(traits)}));
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
            for (const auto& b : tr.params.bounds) {
                if (!b.is_TraitBound()) {
                    continue;
                }
                const auto& be = b.as_TraitBound();
                if (be.type != tySelf) {
                    continue;
                }
                const auto& pt = be.trait;

                // TODO: Remove this along with the from_ast.cpp hack
                if (pt.path.path == thisPath) {
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
                    if (prev->path == it->path) {
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

        [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override {
            return visitTypeInner(ty);
        }

        [[nodiscard]] HIRTypeRef visitTypeInner(HIRTypeRef ty, bool doBind = true) {
            static Span sp;

            // Only NamedFunction nodes are rewritten here; everything else
            // is plain recursion.
            if (!ty->is_NamedFunction()) {
                return HIRVisitor::visitType(ty);
            }
            auto data = ty->cloneData();
            if (auto* te = data.opt_NamedFunction()) {
                if (te->def.is_Function() && te->def.as_Function() == nullptr) {
                    StaticTraitResolve resolve{ms.wb};
                    resolve.setBothGenericsRaw(ms.implGenerics, ms.itemGenerics);
                    MonomorphState unusedMs(crate.types);
                    const auto& v = resolve.getValue(sp, te->path, unusedMs, true);

                    switch (v.tag()) {
default:
                        TODO(sp, "Resolve external NamedFunction type - " << te->path << " : " << v.tagStr());
                        case TypeckValuePtr::TAG_Function: {
                            auto& e = v.as_Function();
                            te->def = e;
                            break;
                        }
                        case TypeckValuePtr::TAG_StructConstructor: {
                            auto& e = v.as_StructConstructor();
                            te->def = e.s;
                            break;
                        }
                        case TypeckValuePtr::TAG_EnumConstructor: {
                            auto& e = v.as_EnumConstructor();
                            te->def = HIRTypeDataNamedFunctionTy::make_EnumConstructor({e.e, e.v});
                            break;
                        }
                    }
                }
            }

            visitTypeDataChildren(data);
            return crate.types.intern(mv$(data));
        }

        void visitTypeImpl(HIRTypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.type << " - from " << impl.srcModule);
            auto _ = this->ms.setImplGenerics(impl.params);

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
            auto _ = this->ms.setItemGenerics(item.params);
            HIRVisitor::visitInherentType(p, item);
        }

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
            TRACE_FUNCTION_F("impl " << traitPath << " for " << impl.type);
            auto _ = this->ms.setImplGenerics(impl.params);

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
            TRACE_FUNCTION_F("impl " << traitPath << " for " << impl.type << " { }");
            auto _ = this->ms.setImplGenerics(impl.params);

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
            auto _ = this->ms.setImplGenerics(item.params);
            HIRVisitor::visitTrait(p, item);
        }

        void visitEnum(HIRItemPath p, HIREnum& item) override {
            auto _ = this->ms.setImplGenerics(item.params);
            HIRVisitor::visitEnum(p, item);
        }

        void visitStruct(HIRItemPath p, HIRStruct& item) override {
            auto _ = this->ms.setImplGenerics(item.params);
            HIRVisitor::visitStruct(p, item);
        }

        void visitUnion(HIRItemPath p, HIRUnion& item) override {
            auto _ = this->ms.setImplGenerics(item.params);
            HIRVisitor::visitUnion(p, item);
        }

        void visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) override {
            auto _ = this->ms.setImplGenerics(item.params);
            HIRVisitor::visitTypeAlias(p, item);
        }

        void visitFunction(HIRItemPath p, HIRFunction& item) override {
            auto _ = this->ms.setItemGenerics(item.params);
            HIRVisitor::visitFunction(p, item);
        }

        void visitStatic(HIRItemPath p, HIRStatic& item) override {
            HIRVisitor::visitStatic(p, item);
        }

        void visitConstant(HIRItemPath p, HIRConstant& item) override {
            auto _ = this->ms.setItemGenerics(item.params);
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
                    ty = upperVisitor.visitTypeInner(ty, true);
                }

                void visitNodePtr(HIRExprNodeP& nodePtr) override {
                    // A node a desugaring built has no result type until type
                    // checking gives it one.
                    if (nodePtr->resType != HIRTypeRef()) {
                        nodePtr->resType = upperVisitor.visitType(nodePtr->resType);
                    }
                    HIRExprVisitorDef::visitNodePtr(nodePtr);
                }

                void visit(HIRExprNodeLet& node) override {
                    node.type = upperVisitor.visitType(node.type);
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
                    upperVisitor.visitPath(node.path, HIRVisitor::PathContext::VALUE);
                }

                void visit(HIRExprNodeCallPath& node) override {
                    upperVisitor.visitPath(node.path, HIRVisitor::PathContext::VALUE);
                    HIRExprVisitorDef::visit(node);
                }

                void visit(HIRExprNodeCallMethod& node) override {
                    upperVisitor.visitPathParams(node.params);
                    HIRExprVisitorDef::visit(node);
                }

                void visit(HIRExprNodeStructLiteral& node) override {
                    node.type = upperVisitor.visitTypeInner(node.type, false);

                    HIRExprVisitorDef::visit(node);
                }

                void visit(HIRExprNodeArraySized& node) override {
                    auto& as = node.size;
                    if (as.is_Unevaluated()) {
                        upperVisitor.visitConstgeneric(as.as_Unevaluated());
                    }
                    HIRExprVisitorDef::visit(node);
                }

                void visit(HIRExprNodeClosure& node) override {
                    node.returnType = upperVisitor.visitType(node.returnType);
                    for (auto& arg : node.args) {
                        upperVisitor.visitPattern(arg.first);
                        arg.second = upperVisitor.visitType(arg.second);
                    }
                    HIRExprVisitorDef::visit(node);
                }
            };

            for (auto& ty : expr.erasedTypes) {
                ty = visitType(ty);
            }

            // Local expression
            if (expr.get() != nullptr) {
                ExprVisitor v{*this};
                (*expr).visit(v);
            }
            // External expression (has MIR)
            else if (auto* mir = expr.getExtMirMut()) {
                for (auto& ty : mir->locals) {
                    updateType(ty);
                }

                struct MirVisitor: public MIRVisitorMut {
                    VisitorPost& upperVisitor;

                    MirVisitor(VisitorPost& upperVisitor)
                        : upperVisitor(upperVisitor)
                    {
                    }

                    void visitType(HIRTypeRef& t) override {
                        t = upperVisitor.visitType(t);
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
            exp.visitCrate(*ec.second.data);
        }
        exp.visitCrate(crate);
    }

    {
        VisitorPost v{wb};
        for (auto& ec : crate.extCrates) {
            v.visitCrate(*ec.second.data);
        }
        v.visitCrate(crate);
    }

    // Populate supertrait list
    VisitorEnumSuperTraits(crate).visitCrate(crate);
}

HIRPathParams ConvertHIRCompleteAliasParams(HIRTypeInterner& types, const Span& sp, const HIRGenericParams& paramsDef, const HIRGenericPath& path, bool isExpr) {
    auto pp = path.params.clone();

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

HIRTypeRef ConvertHIRExpandTypeAlias(const Span& sp, const HIRCrate& crate, const HIRGenericPath& path, bool isExpr) {
    const auto& ti = crate.getTypeitemByPath(sp, path.path);
    if (const auto* ep = ti.opt_TypeAlias()) {
        const auto& ta = *ep;
        DEBUG(path << " -> type " << ta.params.fmtArgs() << " = " << ta.type);
        auto pp = ConvertHIRCompleteAliasParams(crate.types, sp, ta.params, path, isExpr);
        // Monomorphise the exapnded type using the created params
        auto ms = MonomorphStatePtr(crate.types, nullptr, &pp, nullptr);
        HIRTypeRef rv = ms.monomorphType(sp, ta.type);
        DEBUG(path << " -> " << path.path << pp << " -> " << rv);
        return rv;
    }
    return crate.types.infer();
}

HIRTypeRef ConvertHIRExpandAliasesGetExpansion(const HIRCrate& crate, const HIRPath& path, bool isExpr) {
    static Span sp;
    switch (path.data.tag()) {
        case HIRPath::Data::TAG_Generic: {
            auto& e = path.data.as_Generic();
            return ConvertHIRExpandTypeAlias(sp, crate, e, isExpr);
        }
        case HIRPath::Data::TAG_UfcsInherent: {
            DEBUG("TODO: Locate impl blocks for types - path=" << path);
            break;
        }
        case HIRPath::Data::TAG_UfcsKnown: {
            DEBUG("TODO: Locate impl blocks for traits on types - path=" << path);
            break;
        }
        case HIRPath::Data::TAG_UfcsUnknown: {
            DEBUG("TODO: Locate impl blocks for traits on types - path=" << path);
            break;
        }
    }
    return crate.types.infer();
}

std::vector<HIRTraitPath> ConvertHIRExpandAliasesGetTraitExpansionGP(const Span& sp, const HIRCrate& crate, const HIRGenericPath& path, bool isExpr) {
    const auto& ti = crate.getTypeitemByPath(sp, path.path);
    if (const auto* ep = ti.opt_TraitAlias()) {
        const auto& ta = *ep;
        auto pp = ConvertHIRCompleteAliasParams(crate.types, sp, ta.params, path, isExpr);
        auto ms = MonomorphStatePtr(crate.types, nullptr, &pp, nullptr);
        std::vector<HIRTraitPath> rv;
        rv.reserve(ta.traits.size());
        for (const auto& exp : ta.traits) {
            rv.push_back(ms.monomorphTraitpath(sp, exp, false));
        }
        DEBUG(path << "\n -> " << path.path << pp << "\n -> {" << rv << "}");
        return rv;
    } else {
        return std::vector<HIRTraitPath>();
    }
}

std::vector<HIRTraitPath> ConvertHIRExpandAliasesGetTraitExpansion(const Span& sp, const HIRCrate& crate, /*const*/ HIRTraitPath& path, bool isExpr) {
    auto rv = ConvertHIRExpandAliasesGetTraitExpansionGP(sp, crate, path.path, isExpr);
    if (!rv.empty()) {
        if (!path.traitBounds.empty() || !path.typeBounds.empty()) {
            struct H {
                static bool containsTrait(const Span& sp, const HIRCrate& crate, const HIRGenericPath& path, const HIRGenericPath& desPath) {
                    if (path.path == desPath.path) {
                        return true;
                    }
                    const auto& ti = crate.getTypeitemByPath(sp, path.path);
                    if (const auto* t = ti.opt_Trait()) {
                        for (const auto& pt : t->parentTraits) {
                            if (containsTrait(sp, crate, pt.path, desPath)) {
                                return true;
                            }
                        }
                    } else if (const auto* t = ti.opt_TraitAlias()) {
                        for (const auto& pt : t->traits) {
                            if (containsTrait(sp, crate, pt.path, desPath)) {
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
                        if (containsTrait(sp, crate, p.path, desPath)) {
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
        for (auto it = list.begin(); it != list.end();) {
            // An alias that names no trait (`trait S = ?Sized;`) leaves the list
            // shorter, so what it expanded to is what decides where to continue.
            if (!crate.getTypeitemByPath(sp, it->path.path).is_TraitAlias()) {
                ++it;
                continue;
            }
            auto n = ConvertHIRExpandAliasesGetTraitExpansion(sp, crate, *it, inExpr);
            it = list.erase(it);
            it = list.insert(it, std::make_move_iterator(n.begin()), std::make_move_iterator(n.end()));
        }
    }

    [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override {
        static Span sp;

        if (ty->is_ErasedType() || ty->is_TraitObject()) {
            auto data = ty->cloneData();
            if (auto* e = data.opt_ErasedType()) {
                expandTraitList(sp, e->traits);
            } else if (auto* e = data.opt_TraitObject(); e->trait.path != HIRSimplePath()) {
                // A marker in a trait object may be an alias too, and an alias
                // that names no trait at all (`trait S = ?Sized;`) adds nothing.
                for (auto it = e->markers.begin(); it != e->markers.end();) {
                    if (!crate.getTypeitemByPath(sp, it->path).is_TraitAlias()) {
                        ++it;
                        continue;
                    }
                    auto n = ConvertHIRExpandAliasesGetTraitExpansionGP(sp, crate, *it, inExpr);
                    it = e->markers.erase(it);
                    for (auto& expanded : n) {
                        ASSERT_BUG(sp, expanded.traitBounds.empty() && expanded.typeBounds.empty(), "Trait alias with bounds used as a marker - " << expanded);
                        it = e->markers.insert(it, mv$(expanded.path));
                        ++it;
                    }
                }
                if (crate.getTypeitemByPath(sp, e->trait.path.path).is_TraitAlias()) {
                    auto n = ConvertHIRExpandAliasesGetTraitExpansion(sp, crate, e->trait, inExpr);
                    if (!n.empty()) {
                        e->trait = mv$(n.front());
                        for (size_t i = 1; i < n.size(); i++) {
                            ASSERT_BUG(sp, n[i].traitBounds.empty() && n[i].typeBounds.empty(), "Trait alias with bounds used as a marker - " << n[i]);
                            e->markers.push_back(mv$(n[i].path));
                        }
                    } else if (!e->markers.empty()) {
                        // The alias named nothing, so the object's principal
                        // trait is whatever followed it.
                        e->trait = HIRTraitPath{HIRGenericPath(mv$(e->markers.front())), {}, {}};
                        e->markers.erase(e->markers.begin());
                    } else {
                        // Nothing but markers, which the vtable enumerator knows
                        // by the empty path.
                        e->trait = HIRTraitPath();
                    }
                }
            }
            ty = crate.types.intern(std::move(data));
        }
        ty = HIRVisitor::visitType(ty);

        if (const auto* e = ty->opt_Path()) {
            HIRTypeRef newType = ConvertHIRExpandAliasesGetExpansion(crate, e->path, inExpr);
            // Keep trying to expand down the chain
            unsigned int numExp = 1;
            const unsigned int MAX_RECURSIVE_TYPE_EXPANSIONS = 100;
            while (numExp < MAX_RECURSIVE_TYPE_EXPANSIONS) {
                // NOTE: inner recurses
                newType = HIRVisitor::visitType(newType);
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
        return ty;
    }

    void visitTraitPath(HIRTraitPath& tp) override {
        static Span sp;
        // 1. Make sure that the trait path isn't pointing at an alias (should have been handled by the caller, which can expand to multiple items)
        ASSERT_BUG(sp, crate.getTypeitemByPath(sp, tp.path.path).is_Trait(), "Bad trait path - " << tp.path << " : " << crate.getTypeitemByPath(sp, tp.path.path).tagStr());
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
        if (path.data.is_Generic()) {
            const auto& gp = path.data.as_Generic();
            if (gp.path.components().size() > 1 && crate.getTypeitemByPath(sp, gp.path, /*igncrate*/ false, /*ignlast*/ true).is_Enum()) {
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

        if (path.data.is_UfcsUnknown()) {
            const auto& ty = path.data.as_UfcsUnknown().type;
            const auto& name = path.data.as_UfcsUnknown().item;

            const HIRGenericPath* gpP;
            if (ty->is_Generic() && ty->as_Generic().binding == GENERICSelf) {
                if (!implType) {
                    ERROR(sp, E0000, "Use of `Self` pattern outside of an impl block");
                }
                if (!((*implType).is_Path() && ((*implType).as_Path().path.data.is_Generic()))) {
                    ERROR(sp, E0000, "Use of `Self` pattern in non-struct impl block - " << implType);
                }
                gpP = &implType->as_Path().path.data.as_Generic();
            } else {
                if (ty->is_Generic()) {
                    return HIRPattern::PathBinding();
                }
                if (!ty->is_Path()) {
                    ERROR(sp, E0000, "Expeted path in pattern binding, got " << ty);
                }
                if (!ty->as_Path().path.data.is_Generic()) {
                    ERROR(sp, E0000, "Expeted generic path in pattern binding, got " << ty);
                }
                gpP = &ty->as_Path().path.data.as_Generic();
            }
            const auto& gp = *gpP;
            const auto& ti = crate.getTypeitemByPath(sp, gp.path);
            if (!ti.is_Enum()) {
                ERROR(sp, E0000, "Expeted enum path in pattern binding, got " << ti.tagStr());
            }
            const auto& enm = ti.as_Enum();

            auto gp2 = gp.clone();
            gp2.path += name;
            resizeTypeParams(gp2.params, enm.params.types.size());
            gp2.params.values.resize(enm.params.values.size());

            auto idx = enm.findVariant(name);
            if (idx == ~0u) {
                TODO(sp, "Variant " << name << " not found in " << gp);
            }
            path = std::move(gp2);
            return HIRPattern::PathBinding::make_Enum({&enm, static_cast<unsigned>(idx)});
        }
        // `Self { ... }` patterns - Encoded as `<Self>::`
        if (path.data.is_UfcsInherent()) {
            const auto& ty = path.data.as_UfcsInherent().type;
            const auto& name = path.data.as_UfcsInherent().item;
            ASSERT_BUG(sp, ty->is_Generic() && ty->as_Generic().binding == GENERICSelf, path);
            ASSERT_BUG(sp, name == "", path);
            if (!implType) {
                ERROR(sp, E0000, "Use of `Self` pattern outside of an impl block");
            }
            if (!((*implType).is_Path() && ((*implType).as_Path().path.data.is_Generic()))) {
                ERROR(sp, E0000, "Use of `Self` pattern in non-struct impl block - " << implType);
            }
            path = implType->as_Path().path.data.as_Generic().clone();
            // Fall through for the resizing below
        }

        // `<Foo as A>::Assoc { .. }` matches the struct the associated type
        // resolves to, so the projection is replaced by that type.
        if (const auto* ufcs = path.data.opt_UfcsKnown()) {
            const HIRTypeData* revealed = nullptr;
            crate.findTraitImpls(ufcs->trait.path, ufcs->type, HIRResolvePlaceholdersNop(), [&](const HIRTraitImpl& impl) {
                auto it = impl.types.find(ufcs->item);
                if (it == impl.types.end()) {
                    return false;
                }
                revealed = it->second.data;
                return true;
            });
            if (!revealed || !revealed->is_Path() || !revealed->as_Path().path.data.is_Generic()) {
                ERROR(sp, E0000, "Expected a struct behind the associated type in a pattern, got " << path);
            }
            path = revealed->as_Path().path.data.as_Generic().clone();
        }

        ASSERT_BUG(sp, path.data.is_Generic(), path);
        auto& gp = path.data.as_Generic();

        // TODO: Better error messages?
        if (gp.path.components().size() > 1) {
            const auto& ti = crate.getTypeitemByPath(sp, gp.path, false, /*ignore_last*/ true);
            if (ti.is_Enum()) {
                // Enum variant!
                const auto& enm = ti.as_Enum();

                resizeTypeParams(gp.params, enm.params.types.size());
                gp.params.values.resize(enm.params.values.size());

                auto idx = ti.as_Enum().findVariant(gp.path.components().back());
                return HIRPattern::PathBinding::make_Enum({&enm, static_cast<unsigned>(idx)});
            }
        }

        const auto& ti = crate.getTypeitemByPath(sp, gp.path);
        if (ti.is_Union()) {
            const auto& unn = ti.as_Union();

            resizeTypeParams(gp.params, unn.params.types.size());
            gp.params.values.resize(unn.params.values.size());

            return HIRPattern::PathBinding::make_Union(&unn);
        }

        ASSERT_BUG(sp, ti.is_Struct(), "Pattern path " << gp.path << " didn't point to a struct or union (" << ti.tagStr() << ")");
        const auto& str = ti.as_Struct();

        resizeTypeParams(gp.params, str.params.types.size());
        gp.params.values.resize(str.params.values.size());

        return HIRPattern::PathBinding::make_Struct(&str);
    }

    void visitPattern(HIRPattern& pat) override {
        static Span sp;

        HIRVisitor::visitPattern(pat);

        switch (pat.data.tag()) {
default:
            break;
            case HIRPatternData::TAG_PathValue: {
                auto& e = pat.data.as_PathValue();
                auto newPath = expandAliasPath(sp, e.path);
                if (newPath != HIRGenericPath()) {
                    DEBUG("Replacing " << e.path << " with " << newPath);
                    e.path = mv$(newPath);
                }
                e.binding = visitPatternPathBinding(sp, e.path);
                break;
            }
            case HIRPatternData::TAG_PathTuple: {
                auto& e = pat.data.as_PathTuple();
                auto newPath = expandAliasPath(sp, e.path);
                if (newPath != HIRGenericPath()) {
                    DEBUG("Replacing " << e.path << " with " << newPath);
                    e.path = mv$(newPath);
                }
                e.binding = visitPatternPathBinding(sp, e.path);
                break;
            }
            case HIRPatternData::TAG_PathNamed: {
                auto& e = pat.data.as_PathNamed();
                auto newPath = expandAliasPath(sp, e.path);
                if (newPath != HIRGenericPath()) {
                    DEBUG("Replacing " << e.path << " with " << newPath);
                    e.path = mv$(newPath);
                }
                e.binding = visitPatternPathBinding(sp, e.path);
                // TODO: If this is an empty/wildcard AND it's poiting at a value/tuple entry, change to PathValue/PathTuple
                break;
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
                    origType = visitType(origType);

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
                ty = upperVisitor.visitType(ty);
            }

            void visitPattern(const Span& sp, HIRPattern& pat) override {
                upperVisitor.visitPattern(pat);
            }

            // Custom impl to visit the inner expression
            void visit(HIRExprNodeArraySized& node) override {
                auto& as = node.size;
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
        implType = impl.type;
        HIRVisitor::visitTypeImpl(impl);
        implType = nullptr;
    }

    void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
        static Span sp;
        implType = impl.type;
        HIRVisitor::visitTraitImpl(traitPath, impl);
        implType = nullptr;
    }

    void visitFunction(HIRItemPath p, HIRFunction& item) override {
        HIRVisitor::visitFunction(p, item);
        if (item.receiver == HIRFunction::Receiver::Custom) {
            ASSERT_BUG(Span(), item.receiverType, "Custom receiver without a receiver type");
            *item.receiverType = this->visitType(*item.receiverType);
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

    [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override {
        ty = HIRVisitor::visitType(ty);

        if (const auto* te = ty->opt_Generic()) {
            if (te->binding == GENERICSelf) {
                if (implType) {
                    DEBUG("Replace Self with " << implType);
                    return implType;
                } else {
                    // NOTE: Valid for `trait` definitions.
                    DEBUG("Self outside of an `impl` block");
                }
            }
        }
        return ty;
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
                ty = upperVisitor.visitType(ty);
            }

            void visitPattern(const Span& sp, HIRPattern& pat) override {
                upperVisitor.visitPattern(pat);
            }

            // Custom impl to visit the inner expression
            void visit(HIRExprNodeArraySized& node) override {
                auto& as = node.size;
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
        HIRTypeRef ty = crate.types.path(HIRGenericPath(p.getSimplePath(), enm.params.makeNopParams(crate.types, 0)), &enm);
        implType = ty;
        HIRVisitor::visitEnum(p, enm);
        implType = nullptr;
    }

    void visitStruct(HIRItemPath p, HIRStruct& str) override {
        HIRTypeRef ty = crate.types.path(HIRGenericPath(p.getSimplePath(), str.params.makeNopParams(crate.types, 0)), &str);
        // HACK: If thre is a `#` in the path, it's en enum variant
        if (const auto* n = ::std::strchr(p.name, '#')) {
            if (n != p.name && n[1]) {
                auto path = p.getSimplePath();
                path.updateLastComponent(RcString::newInterned(p.name, n - p.name));
                const auto& enm = crate.getEnumByPath(Span(), path);
                ty = crate.types.path(HIRGenericPath(std::move(path), str.params.makeNopParams(crate.types, 0)), &enm);
            }
        }
        implType = ty;
        HIRVisitor::visitStruct(p, str);
        implType = nullptr;
    }

    void visitUnion(HIRItemPath p, HIRUnion& unn) override {
        HIRTypeRef ty = crate.types.path(HIRGenericPath(p.getSimplePath(), unn.params.makeNopParams(crate.types, 0)), &unn);
        implType = ty;
        HIRVisitor::visitUnion(p, unn);
        implType = nullptr;
    }

    void visitTypeImpl(HIRTypeImpl& impl) override {
        implType = impl.type;
        HIRVisitor::visitTypeImpl(impl);
        implType = nullptr;
    }

    void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
        static Span sp;
        implType = impl.type;
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

    [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override {
        // Values embedded in types must reach the hook above.
        return visitTypeDefaultViaHooks(ty);
    }

    void visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) override {
        Guard guard(*this, item.params);
        HIRVisitor::visitTypeAlias(p, item);
    }

    void visitTraitAlias(HIRItemPath p, HIRTraitAlias& item) override {
        Guard guard(*this, item.params);
        HIRVisitor::visitTraitAlias(p, item);
    }
};

void ConvertHIRExpandAliases(HIRCrate& crate) {
    AliasConstGenericParamBinder(crate.types).visitCrate(crate);
    Expander exp{crate};
    exp.visitCrate(crate);
}

namespace {
    class ReceiverValidator {
        HIRCrate& crate;
        StaticTraitResolve resolve;
        const HIRTypeData* implType = nullptr;

        bool replaceImplTypeWithSelf(HIRTypeRef& ty) {
            if (ty == crate.types.self()) {
                return true;
            }
            if (ty == implType) {
                ty = crate.types.self();
                return true;
            }
            if (ty->is_Generic()) {
                // `fn f(self: T)`: the bound on `T` is what makes it a receiver.
                return true;
            }
            if (const auto* path = ty->opt_Path()) {
                const auto* generic = path->path.data.opt_Generic();
                if (!generic) {
                    return false;
                }
                if (generic->params.types.empty()) {
                    // A receiver that names no type reaches `Self` through its
                    // `Receiver`/`Deref` impl; there is nothing to rewrite.
                    return true;
                }
                auto data = ty->cloneData();
                auto& inner = data.as_Path().path.data.as_Generic().params.types[0];
                if (!replaceImplTypeWithSelf(inner)) {
                    return false;
                }
                ty = crate.types.intern(mv$(data));
                return true;
            }
            if (const auto* borrow = ty->opt_Borrow()) {
                auto inner = borrow->inner;
                if (!replaceImplTypeWithSelf(inner)) {
                    return false;
                }
                ty = crate.types.borrow(borrow->type, inner);
                return true;
            }
            if (const auto* pointer = ty->opt_Pointer()) {
                auto inner = pointer->inner;
                if (!replaceImplTypeWithSelf(inner)) {
                    return false;
                }
                ty = crate.types.pointer(pointer->type, inner);
                return true;
            }
            return false;
        }

        bool needsProjectionValidation(const HIRFunction& item) const {
            if (item.receiver != HIRFunction::Receiver::Custom) {
                return false;
            }
            ASSERT_BUG(Span(), item.receiverType, "Custom receiver without a receiver type");
            return visitTyWith(*item.receiverType, [](const HIRTypeData* ty) {
                const auto* path = ty->opt_Path();
                return path && path->path.data.is_UfcsKnown();
            });
        }

        void validateFunction(HIRFunction& item) {
            if (item.receiver == HIRFunction::Receiver::Custom) {
                ASSERT_BUG(Span(), item.receiverType, "Custom receiver without a receiver type");
                ASSERT_BUG(Span(), !item.args.empty(), "Custom receiver without arguments");

                auto receiverType = *item.receiverType;
                if (needsProjectionValidation(item)) {
                    auto itemGenerics = resolve.setItemGenerics(item.params);
                    resolve.expandAssociatedTypes(Span(), receiverType);
                }
                if (!replaceImplTypeWithSelf(receiverType)) {
                    ERROR(Span(), E0000, "Unknown receiver type - " << *item.receiverType);
                }
                item.receiverType = receiverType;
                item.args.front().second = receiverType;
            }
        }

        void validateTrait(HIRTrait& trait) {
            const auto needsValidation = ::std::any_of(trait.values.begin(), trait.values.end(), [&](const auto& value) {
                const auto* function = value.second.opt_Function();
                return function && needsProjectionValidation(*function);
            });
            if (!needsValidation) {
                return;
            }
            const auto* oldImplType = implType;
            implType = crate.types.self();
            auto implGenerics = resolve.setImplGenerics(MetadataType::Unknown, trait.params);
            for (auto& value : trait.values) {
                if (auto* function = value.second.opt_Function()) {
                    validateFunction(*function);
                }
            }
            implType = oldImplType;
        }

        void validateModule(HIRModule& module) {
            for (auto& item : module.modItems) {
                if (auto* submodule = item.second->ent.opt_Module()) {
                    validateModule(*submodule);
                } else if (auto* trait = item.second->ent.opt_Trait()) {
                    validateTrait(*trait);
                }
            }
        }

        template <typename Impl, typename Callback>
        void forEachImpl(HIRCrate::ImplGroup<::std::unique_ptr<Impl>>& group, Callback callback) {
            for (auto& named : group.named) {
                for (auto& impl : named.second) {
                    callback(*impl);
                }
            }
            for (auto& impl : group.nonNamed) {
                callback(*impl);
            }
            for (auto& impl : group.generic) {
                callback(*impl);
            }
        }

    public:
        ReceiverValidator(const WireBoard& wb, HIRCrate& crate)
            : crate(crate)
            , resolve(wb)
        {
        }

        void validate() {
            validateModule(crate.rootModule);
            forEachImpl(crate.typeImpls, [&](HIRTypeImpl& impl) {
                const auto needsValidation = ::std::any_of(impl.methods.begin(), impl.methods.end(), [&](const auto& method) {
                    return needsProjectionValidation(method.second.data);
                });
                if (!needsValidation) {
                    return;
                }
                const auto* oldImplType = implType;
                implType = impl.type;
                auto implGenerics = resolve.setImplGenerics(MetadataType::Unknown, impl.params);
                for (auto& method : impl.methods) {
                    validateFunction(method.second.data);
                }
                implType = oldImplType;
            });
            for (auto& traitImpls : crate.traitImpls) {
                forEachImpl(traitImpls.second, [&](HIRTraitImpl& impl) {
                    const auto needsValidation = ::std::any_of(impl.methods.begin(), impl.methods.end(), [&](const auto& method) {
                        return needsProjectionValidation(method.second.data);
                    });
                    if (!needsValidation) {
                        return;
                    }
                    const auto* oldImplType = implType;
                    implType = impl.type;
                    auto implGenerics = resolve.setImplGenerics(MetadataType::Unknown, impl.params);
                    for (auto& method : impl.methods) {
                        validateFunction(method.second.data);
                    }
                    implType = oldImplType;
                });
            }
        }
    };
}

void ConvertHIRValidateReceivers(const WireBoard& wb, HIRCrate& crate) {
    ReceiverValidator(wb, crate).validate();
}

void ConvertHIRExpandAliasesSelf(HIRCrate& crate) {
    ExpanderSelf exp{crate};
    exp.visitCrate(crate);
}

void ConvertHIRExpandAliasesSelfExpr(const HIRCrate& crate, const HIRTypeData* implType, ::std::vector<::std::pair<HIRPattern, HIRTypeRef>>& args, HIRTypeRef& retTy, HIRExprPtr& expr) {
    ExpanderSelf exp{crate, implType};
    for (auto& arg : args) {
        exp.visitPattern(arg.first);
        arg.second = exp.visitType(arg.second);
    }
    retTy = exp.visitType(retTy);
    exp.visitExpr(expr);
}

namespace {

    class MarkingsVisitor: public HIRVisitor {
        const HIRCrate& crate;
        const HIRSimplePath& langUnsize_;
        const HIRSimplePath& langCoerceUnsized_;
        const HIRSimplePath& langCopy_;
        const HIRSimplePath& langDeref_;
        const HIRSimplePath& langDrop_;
        const HIRSimplePath& langPhantomData_;

    public:
        MarkingsVisitor(const HIRCrate& crate)
            : HIRVisitor(nullptr, crate.types)
            , crate(crate)
            , langUnsize_(crate.getLangItemPathOpt("unsize"))
            , langCoerceUnsized_(crate.getLangItemPathOpt("coerce_unsized"))
            , langCopy_(crate.getLangItemPathOpt("copy"))
            , langDeref_(crate.getLangItemPathOpt("deref"))
            , langDrop_(crate.getLangItemPathOpt("drop"))
            , langPhantomData_(crate.getLangItemPathOpt("phantom_data"))
        {
        }

        void visitStruct(HIRItemPath ip, HIRStruct& str) override {
            HIRVisitor::visitStruct(ip, str);

            str.structMarkings.dstType = getStructDstType(str, str.params, {});
            if (str.structMarkings.dstType != HIRStructMarkings::DstType::None) {
                str.structMarkings.unsizedField = (str.data.is_Tuple() ? str.data.as_Tuple().size() - 1 : str.data.as_Named().size() - 1);
            }

            // Rules:
            // - A type parameter must be ?Sized
            // - That type parameter must only be used as part of the last field, and only once
            // - If the final field isn't the parameter, it must also impl Unsize

            // HACK: Just determine what ?Sized parameter is controlling the sized-ness
            if (str.structMarkings.dstType == HIRStructMarkings::DstType::Possible) {
                auto& lastFieldTy = (str.data.is_Tuple() ? str.data.as_Tuple().back().ent : str.data.as_Named().back().ty);
                for (size_t i = 0; i < str.params.types.size(); i++) {
                    const auto& param = str.params.types[i];
                    auto ty = crate.types.generic(param.name, i);
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
            } else if (ty->is_Slice() || ((*ty).is_Primitive() && ((*ty).as_Primitive() == HIRCoreType::Str))) {
                return HIRStructMarkings::DstType::Slice;
            } else if (ty->is_TraitObject()) {
                return HIRStructMarkings::DstType::TraitObject;
            } else if (const auto* te = ty->opt_Path()) {
                // If the type is a struct, check it (recursively)
                if (!te->path.data.is_Generic()) {
                    // Associated type, TODO: Check this better.
                    return HIRStructMarkings::DstType::None;
                } else if (te->binding.is_Struct()) {
                    const auto& paramsTpl = te->path.data.as_Generic().params;
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
        switch (str.data.tag()) {
            case HIRStructData::TAG_Unit: {
                break;
            }
            case HIRStructData::TAG_Tuple: {
                auto& se = str.data.as_Tuple();
                // TODO: Ensure that only the last field is ?Sized
                if (se.size() > 0) {
                    return getFieldDstType(se.back().ent, str.params, def, params);
                }
                break;
            }
            case HIRStructData::TAG_Named: {
                auto& se = str.data.as_Named();
                // Check the last field in the struct.
                // - If it is Sized, leave as-is (struct is marked as Sized)
                // - If it is known unsized, record the type
                // - If it is a ?Sized parameter, mark as possible and record index for MIR

                // TODO: Ensure that only the last field is ?Sized
                if (se.size() > 0) {
                    return getFieldDstType(se.back().ty, str.params, def, params);
                }
                break;
            }
        }
        return HIRStructMarkings::DstType::None;
        }

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
            static Span sp;

            HIRVisitor::visitTraitImpl(traitPath, impl);

            if (impl.type->is_Path()) {
                const auto& te = impl.type->as_Path();
                const HIRTraitMarkings* markingsPtr = te.binding.getTraitMarkings();
                if (markingsPtr) {
                    HIRTraitMarkings& markings = *const_cast<HIRTraitMarkings*>(markingsPtr);
                    if (traitPath == langUnsize_) {
                        DEBUG("Type " << impl.type << " can Unsize");
                        ERROR(sp, E0000, "Unsize shouldn't be manually implemented");
                    } else if (traitPath == langDrop_) {
                        // TODO: Check that there's only one impl, and that it covers the same set as the type.
                        markings.hasDropImpl = true;
                        markings.hasConstDropImpl = markings.hasConstDropImpl || impl.isConst;
                    } else if (traitPath == langCoerceUnsized_) {
                        auto& structMarkings = const_cast<HIRStruct*>(te.binding.as_Struct())->structMarkings;
                        if (structMarkings.coerceUnsizedIndex != ~0u) {
                            ERROR(sp, E0000, "CoerceUnsized can only be implemented once per struct");
                        }

                        DEBUG("Type " << impl.type << " can Coerce");
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

                        auto monomorphCbL = MonomorphStatePtr(crate.types, nullptr, &dstTe.path.data.as_Generic().params, nullptr);
                        auto monomorphCbR = MonomorphStatePtr(crate.types, nullptr, &te.path.data.as_Generic().params, nullptr);

                    switch (str->data.tag()) {
                        case HIRStructData::TAG_Unit: {
                            break;
                        }
                        case HIRStructData::TAG_Tuple: {
                            auto& se = str->data.as_Tuple();
                            for (unsigned int i = 0; i < se.size(); i++) {
                                // If the data is PhantomData, ignore it.
                                if (((*se[i].ent).is_Path() && (*se[i].ent).as_Path().path.data.is_Generic() && (*se[i].ent).as_Path().path.data.as_Generic().path == langPhantomData_)) {
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
                            break;
                        }
                        case HIRStructData::TAG_Named: {
                            auto& se = str->data.as_Named();
                            for (unsigned int i = 0; i < se.size(); i++) {
                                // If the data is PhantomData, ignore it.
                                if (((*se[i].ty).is_Path() && (*se[i].ty).as_Path().path.data.is_Generic() && (*se[i].ty).as_Path().path.data.as_Generic().path == langPhantomData_)) {
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
                            break;
                        }
                    }
                    if( field == ~0u )
                        ERROR(sp, E0000, "CoerceUnsized requires a field to differ between source and destination");
                    structMarkings.coerceUnsizedIndex = field;
                    } else if (traitPath == langDeref_) {
                        DEBUG("Type " << impl.type << " can Deref");
                        markings.hasADeref = true;
                    } else if (traitPath == langCopy_) {
                        DEBUG("Type " << impl.type << " has a Copy impl");
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
                const auto& gp = te->path.data.as_Generic();
                return getUnsizeParamIdx(sp, gp.params.types.at(ism.unsizedParam));
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
            switch (str.data.tag()) {
                case HIRStructData::TAG_Unit: {
                    break;
                }
                case HIRStructData::TAG_Tuple: {
                    auto& se = str.data.as_Tuple();
                    fieldTy = se.at(str.structMarkings.coerceUnsizedIndex).ent;
                    break;
                }
                case HIRStructData::TAG_Named: {
                    auto& se = str.data.as_Named();
                    fieldTy = se.at(str.structMarkings.coerceUnsizedIndex).ty;
                    break;
                }
            }
            assert(fieldTy);
        tryAgain:
            DEBUG("field_ty = " << fieldTy);

            if (const auto* te = fieldTy->opt_Path()) {
                ASSERT_BUG(sp, te->binding.is_Struct(), "CoerceUnsized impl differs on Path that isn't a struct - " << ip << " fld=" << fieldTy);
                const auto* istr = te->binding.as_Struct();
                const auto& gp = te->path.data.as_Generic();

                size_t innerIdx = 0;
                auto innerType = getCoerceType(sp, {fieldTy}, *istr, innerIdx);
                ASSERT_BUG(sp, innerType != HIRStructMarkings::Coerce::None, "CoerceUnsized impl differs on a non-CoerceUnsized type - " << ip << " fld=" << fieldTy);

                const auto& paramTy = gp.params.types.at(innerIdx);
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
    auto ms = MonomorphStatePtr(crate.types, impl.type, &impl.traitArgs, nullptr);

    while (impl.traitArgs.types.size() < trait.params.types.size()) {
        const auto& def = trait.params.types[impl.traitArgs.types.size()];
        auto ty = ms.monomorphType(sp, def.defaultValue);
        DEBUG("Add default trait arg " << ty << " from " << def.defaultValue);
        impl.traitArgs.types.push_back(mv$(ty));
    }
}

class UfcsVisitor: public HIRVisitor {
    const HIRCrate& crate;
    bool visitExprs_;
    bool runEat;

    typedef ::std::vector<::std::pair<const HIRSimplePath*, const HIRTrait*>> tTraitImports;
    tTraitImports traits;

    StaticTraitResolve resolve_;
    bool inTraitDef_ = false;
    const HIRTypeData* currentType_ = nullptr;
    const HIRTrait* currentTrait = nullptr;
    const HIRItemPath* currentTraitPath_ = nullptr;
    bool inExpr = false;
    HIRSimplePath curModPath;

public:
    UfcsVisitor(const WireBoard& wb, bool visitExprs)
        : HIRVisitor(nullptr, wb.crate->types)
        , crate(*wb.crate)
        , visitExprs_(visitExprs)
        , runEat(visitExprs)
        , // Defaults to running when doing second-pass
        resolve_(wb)
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
            tps.defaultValue = this->visitType(tps.defaultValue);
        }
        runEat = savedRunEat;

        for (auto& bound : params.bounds) {
            visitGenericBound(bound);
        }

        // Re-populate the resolve index, as the above has changed them
        resolve_.prepIndexes(Span());
    }

    void visitUnion(HIRItemPath p, HIRUnion& item) override {
        auto _ = resolve_.setImplGenerics(MetadataType::None, item.params);
        auto ty = crate.types.path(HIRGenericPath(p.getSimplePath()), &item);
        currentType_ = ty;
        HIRVisitor::visitUnion(p, item);
        currentType_ = nullptr;
    }

    void visitStruct(HIRItemPath p, HIRStruct& item) override {
        auto _ = resolve_.setImplGenerics(item.structMarkings.dstType, item.params);
        auto ty = crate.types.path(HIRGenericPath(p.getSimplePath()), &item);
        currentType_ = ty;
        HIRVisitor::visitStruct(p, item);
        currentType_ = nullptr;
    }

    void visitEnum(HIRItemPath p, HIREnum& item) override {
        auto _ = resolve_.setImplGenerics(MetadataType::None, item.params);
        auto ty = crate.types.path(HIRGenericPath(p.getSimplePath()), &item);
        currentType_ = ty;
        HIRVisitor::visitEnum(p, item);
        currentType_ = nullptr;
    }

    void visitFunction(HIRItemPath p, HIRFunction& item) override {
        auto _ = resolve_.setItemGenerics(item.params);
        HIRVisitor::visitFunction(p, item);
    }

    void visitConstant(HIRItemPath p, HIRConstant& item) override {
        auto _ = resolve_.setItemGenerics(item.params);
        HIRVisitor::visitConstant(p, item);
    }

    void visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) override {
        // NOTE: Disabled, because generics in type aliases are never checked
        // Re-enabled to resolve a UFCS properly (1.90.0 libcore)
        auto _ = resolve_.setImplGenerics(MetadataType::Unknown, item.params);
        HIRVisitor::visitTypeAlias(p, item);
    }

    void visitTrait(HIRItemPath p, HIRTrait& trait) override {
        inTraitDef_ = true;
        currentTrait = &trait;
        currentTraitPath_ = &p;
        auto _ = resolve_.setImplGenerics(MetadataType::TraitObject, trait.params);
        HIRVisitor::visitTrait(p, trait);
        currentTrait = nullptr;
        inTraitDef_ = false;
    }

    void visitTraitAlias(HIRItemPath p, HIRTraitAlias& item) override {
        // The alias's own parameters are in scope in the traits it names.
        auto _ = resolve_.setImplGenerics(MetadataType::Unknown, item.params);
        HIRVisitor::visitTraitAlias(p, item);
    }

    void visitTypeImpl(HIRTypeImpl& impl) override {
        TRACE_FUNCTION_F("impl" << impl.params.fmtArgs() << " " << impl.type << " (mod=" << impl.srcModule << ")");
        auto _t = this->pushModTraits(impl.srcModule, this->crate.getModByPath(Span(), impl.srcModule));
        auto _g = resolve_.setImplGenerics(MetadataType::Unknown, impl.params);
        currentType_ = impl.type;

        impl.type = this->visitType(impl.type);
        resolve_.updateImplSelfMetadata(impl.type);

        HIRVisitor::visitTypeImpl(impl);
        currentType_ = nullptr;
    }

    void visitInherentType(HIRItemPath p, HIRTypeAlias& item) override {
        auto _ = resolve_.setItemGenerics(item.params);
        HIRVisitor::visitInherentType(p, item);
    }

    void visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) override {
        HIRItemPath p(impl.type, traitPath, impl.traitArgs);
        TRACE_FUNCTION_F("impl" << impl.params.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << impl.type << " (mod=" << impl.srcModule << ")");
        auto _t = this->pushModTraits(impl.srcModule, this->crate.getModByPath(Span(), impl.srcModule));
        auto _g = resolve_.setImplGenerics(impl.type, impl.params);

        // TODO: Push a bound that `Self: ThisTrait`
        currentType_ = impl.type;
        currentTrait = &crate.getTraitByPath(Span(), traitPath);
        currentTraitPath_ = &p;

        // The implemented trait is always in scope
        traits.push_back(::std::make_pair(&traitPath, currentTrait));

        impl.type = this->visitType(impl.type);
        resolve_.updateImplSelfMetadata(impl.type);

        HIRVisitor::visitMarkerImpl(traitPath, impl);
        traits.pop_back();

        currentTrait = nullptr;
        currentType_ = nullptr;
    }

    void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
        HIRItemPath p(impl.type, traitPath, impl.traitArgs);
        TRACE_FUNCTION_F("impl" << impl.params.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << impl.type << " (mod=" << impl.srcModule << ")");
        auto _t = this->pushModTraits(impl.srcModule, this->crate.getModByPath(Span(), impl.srcModule));
        auto _g = resolve_.setImplGenerics(MetadataType::Unknown, impl.params);

        expandTraitImplTypeDefaults(crate, traitPath, impl);

        currentType_ = impl.type;
        currentTrait = &crate.getTraitByPath(Span(), traitPath);
        currentTraitPath_ = &p;
        traits.push_back(::std::make_pair(&traitPath, currentTrait));

        impl.type = this->visitType(impl.type);
        resolve_.updateImplSelfMetadata(impl.type);

        // TODO: Handle resolution of all items in m_resolve.m_type_equalities
        // - params might reference each other, so `set_item_generics` has to have been called
        // - But `m_type_equalities` can end up with non-resolved UFCS paths
        resolve_.forEachTypeEquality([&](HIRTypeRef& ty) {
            ty = visitType(ty);
        });

        // The implemented trait is always in scope
        HIRVisitor::visitTraitImpl(traitPath, impl);
        traits.pop_back();

        currentTrait = nullptr;
        currentType_ = nullptr;
    }

    void visitExpr(HIRExprPtr& expr) override {
        struct ExprVisitor: public HIRExprVisitorDef {
            UfcsVisitor& upperVisitor;
            HIRExprNodeP replacement;

            ExprVisitor(UfcsVisitor& uv)
                : HIRExprVisitorDef(uv.crate.types)
                , upperVisitor(uv)
            {
            }

            void visitType(HIRTypeRef& ty) override {
                ty = upperVisitor.visitType(ty);
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
                if (replacement) {
                    replacement->resType = nodePtr->resType;
                    replacement.swap(nodePtr);
                    replacement.reset();
                }
            }

            // Custom to visit the inner expression
            void visit(HIRExprNodeArraySized& node) override {
                auto& as = node.size;
                if (as.is_Unevaluated()) {
                    upperVisitor.visitConstgeneric(as.as_Unevaluated());
                }
                HIRExprVisitorDef::visit(node);
            }

            // Custom visitor for enum/struct constructors
            void visit(HIRExprNodeCallPath& node) override {
                HIRExprVisitorDef::visit(node);
                const Span& sp = node.span();
                if (node.path.data.is_Generic()) {
                    // If it points to an enum, rewrite
                    auto& gp = node.path.data.as_Generic();
                    if (gp.path.components().size() > 1) {
                        const auto& ent = upperVisitor.crate.getTypeitemByPath(sp, gp.path, /*ign_crate*/ false, true);
                        if (ent.is_Enum() && ent.as_Enum().findVariant(gp.path.components().back()) != SIZE_MAX) {
                            // Rewrite!
                            replacement.reset(upperVisitor.crate.pool->make<HIRExprNodeTupleVariant>(sp, mv$(gp), /*is_struct*/ false, mv$(node.args)));
                            DEBUG(&node << ": Replacing with TupleVariant " << replacement.get());
                            return;
                        }
                    }
                }

                // If this is pointing at a constant/static/associated constant, change to CallValue
                MonomorphState discard(upperVisitor.crate.types);
                auto v = upperVisitor.resolve_.getValue(node.span(), node.path, discard, true);
                if (v.is_Constant() || v.is_Static()) {
                    auto* valueNode = upperVisitor.crate.pool->make<HIRExprNodePathValue>(sp, std::move(node.path), v.is_Constant() ? HIRExprNodePathValue::Target::CONSTANT : v.is_Static() ? HIRExprNodePathValue::Target::STATIC : HIRExprNodePathValue::Target::UNKNOWN);
                    valueNode->resType = upperVisitor.crate.types.infer();
                    replacement.reset(upperVisitor.crate.pool->make<HIRExprNodeCallValue>(sp, HIRExprNodeP(valueNode), mv$(node.args)));
                    DEBUG(&node << ": Replacing with CallValue " << replacement.get());
                    return;
                }
            }

            // Custom visitor for enum/struct constructors
            void visit(HIRExprNodePathValue& node) override {
                HIRExprVisitorDef::visit(node);
                const Span& sp = node.span();
                if (node.path.data.is_Generic()) {
                    // If it points to an enum, set binding
                    auto& gp = node.path.data.as_Generic();
                    if (gp.path.components().size() > 1) {
                        const auto& ent = upperVisitor.crate.getTypeitemByPath(sp, gp.path, /*ign_crate*/ false, true);
                        if (ent.is_Enum()) {
                            const auto& enm = ent.as_Enum();
                            auto idx = enm.findVariant(gp.path.components().back());
                            if (enm.data.is_Value() || enm.data.as_Data().at(idx).type == upperVisitor.crate.types.unit()) {
                                replacement.reset(upperVisitor.crate.pool->make<HIRExprNodeUnitVariant>(sp, mv$(gp), /*is_struct*/ false));
                                DEBUG(&node << ": Replacing with UnitVariant " << replacement.get());
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
                if (node.type->is_Path() && node.type->as_Path().path.data.is_Generic()) {
                    // If it points to an enum, set binding
                    auto data = node.type->cloneData();
                    auto& p = data.as_Path().path;
                    auto& gp = p.data.as_Generic();
                    if (gp.path.components().size() > 1) {
                        const auto& ent = upperVisitor.crate.getTypeitemByPath(sp, gp.path, /*ign_crate*/ false, true);
                        if (ent.is_Enum()) {
                            DEBUG(&node << ": Tagging as an enum");
                            node.isStruct = false;
                            auto enumPath = std::move(gp);
                            auto varName = enumPath.path.popComponent();
                            auto enumTy = upperVisitor.crate.types.path(std::move(enumPath), &ent.as_Enum());
                            p = HIRPath(std::move(enumTy), std::move(varName));
                        }
                    }
                    node.type = upperVisitor.crate.types.intern(std::move(data));
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

        if (visitExprs_ && expr.get() != nullptr) {
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
                DEBUG("- " << e->type << " : " << e->trait.path);
                // Bounds are keyed by the semantic HIR type. Binding
                // metadata and erased regions can differ depending on
                // which path was resolved first.
                if (e->type == tr || e->type->equalsIgnoringRegions(tr)) {
                    DEBUG(" - Match");
                    if (locateInTraitAndSet(pc, e->trait.path, crate.getTraitByPath(sp, e->trait.path.path), pd)) {
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
            for (auto& type : traitPath.params.types) {
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

        auto pp = traitPath.params.clone();
        while (pp.types.size() < trait.params.types.size()) {
            auto idx = pp.types.size();
            const auto& def = trait.params.types[idx].defaultValue;
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
            const auto& parTraitPath = monomorphGpIfNeeded(pt.path);
            DEBUG("- Check " << parTraitPath);
            if (locateInTraitAndSet(pc, parTraitPath, *pt.traitPtr, pd)) {
                return true;
            }
        }
        for (const auto& pt : trait.allParentTraits) {
            const auto& parTraitPath = monomorphGpIfNeeded(pt.path);
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
        this->resolve_.findImpl(sp, traitPath.path, nullptr, type, [&](const auto& impl, bool fuzzy) -> bool {
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
                assert(pp.types.size() == innerE->trait.params.types.size());
                for (unsigned int i = 0; i < pp.types.size(); i++) {
                    auto& eTy = innerE->trait.params.types[i];
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
                pd = getUfcsKnown(pc, mv$(e), HIRGenericPath(traitPath.path, mv$(pp)), trait);
            }
            return false;
        });
        return pd.is_UfcsKnown();
    }

    bool locateInTraitImplAndSet(const Span& sp, HIRVisitor::PathContext pc, const HIRGenericPath& traitPath, const HIRTrait& trait, HIRPath::Data& pd) {
        if (this->locateItemInTrait(pc, trait, pd)) {
            return setFromTraitImpl(sp, pc, traitPath, trait, pd);
        } else {
            DEBUG("- Item " << pd.as_UfcsUnknown().item << " not in trait " << traitPath.path);
        }

        // Search supertraits (recursively)
        // NOTE: This runs before "Resolve HIR Markings", so m_all_parent_traits can't be used exclusively
        for (const auto& pt : trait.parentTraits) {
            // TODO: Modify path parameters based on the current trait's params
            if (locateInTraitImplAndSet(sp, pc, pt.path, *pt.traitPtr, pd)) {
                return true;
            }
        }
        for (const auto& pt : trait.allParentTraits) {
            if (this->locateItemInTrait(pc, *pt.traitPtr, pd)) {
                // TODO: Modify path parameters based on the current trait's params
                return setFromTraitImpl(sp, pc, pt.path, *pt.traitPtr, pd);
            } else {
                DEBUG("- Item " << pd.as_UfcsUnknown().item << " not in trait " << traitPath.path);
            }
        }
        return false;
    }

    bool resolve_UfcsUnknown_inherent(const HIRSimplePath& visPath, const HIRPath& p, HIRVisitor::PathContext pc, HIRPath::Data& pd) {
        auto& e = pd.as_UfcsUnknown();
        TRACE_FUNCTION_F(e.type);
        return crate.findTypeImpls(e.type, HIRResolvePlaceholdersNop(), [&](const auto& impl) {
            DEBUG("- matched inherent impl" << impl.params.fmtArgs() << " " << impl.type);
            // Search for item in this block
            switch (pc) {
                case HIRVisitor::PathContext::VALUE:
                    if (impl.methods.find(e.item) != impl.methods.end()) {
                        // HACK: Allow access to privates of `fmt:rt::Argument`
                        if (e.type->is_Path() && e.type->as_Path().path.data.is_Generic() && e.type->as_Path().path.data.as_Generic().path == crate.getLangItemPathOpt("format_argument")) {
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
            traitPath.params.types.reserve(trait.params.types.size());
            for (size_t i = 0; i < trait.params.types.size(); i++) {
                traitPath.params.types.push_back(crate.types.infer());
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

    [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override {
        // TODO: Add a span parameter.
        static Span sp;

        // This pass rewrites paths and resolves expressions inside const
        // generics, and those live inside types too: the path-bearing node
        // kinds go through a working copy and the owned-structure hooks
        // (including this class's visitPath/visitConstgeneric overrides);
        // everything else is plain recursion.
        switch (ty->tag()) {
            case HIRTypeData::TAG_Path:
            case HIRTypeData::TAG_TraitObject:
            case HIRTypeData::TAG_ErasedType:
            case HIRTypeData::TAG_Array:
            case HIRTypeData::TAG_Pattern:
            case HIRTypeData::TAG_NamedFunction: {
                auto data = ty->cloneData();
                visitTypeDataChildren(data);
                ty = crate.types.intern(mv$(data));
                break;
            }
            default: {
                ty = HIRVisitor::visitType(ty);
                break;
            }
        }

        // TODO: If this an associated type, check for default trait params

        if (runEat) {
            TRACE_FUNCTION_FR(ty, ty);
            std::vector<HIRTypeRef> stack;
            if (ty->is_Path()) {
                stack.push_back(ty);
            }
            while (resolve_.expandAssociatedTypesSingle(sp, ty)) {
                if (::std::find(stack.begin(), stack.end(), ty) != stack.end()) {
                    ::std::sort(stack.begin(), stack.end());
                    DEBUG("Loop detected, picking " << ty);
                    ty = std::move(stack[0]);
                    ty = HIRVisitor::visitType(ty);
                    break;
                }
                // NOTE: Only need to clone if this is a Path, as that's the only way we could loop again
                if (ty->is_Path()) {
                    stack.push_back(ty);
                }
                DEBUG("counter = " << stack.size());
                rewriteTyWith(crate.types, ty, [&](HIRTypeRef& rewritten, HIRTypeData& data) -> bool {
                    if ((data.is_Generic() && (data.as_Generic().isPlaceholder()))) {
                        rewritten = crate.types.infer();
                    }
                    return false;
                });
                ASSERT_BUG(sp, stack.size() < 20, "Sanity limit exceeded when resolving UFCS in type " << ty);
                // Invoke a special version of EAT that only processes a single item.
                // - Keep recursing while this does replacements
                ty = HIRVisitor::visitType(ty);
            }
        }
        return ty;
    }

    void visitConstgeneric(HIRConstGeneric& val) override {
        auto savedVisitExprs = visitExprs_;
        visitExprs_ = true;
        HIRVisitor::visitConstgeneric(val);
        visitExprs_ = savedVisitExprs;
    }

    void visitPath(HIRPath& p, HIRVisitor::PathContext pc) override {
        static Span sp;

        if (auto* pe = p.data.opt_UfcsKnown()) {
            // If the trait has missing type argumenst, replace them with the defaults
            auto& tp = pe->trait;
            const auto& trait = resolve_.hirCrate().getTraitByPath(sp, tp.path);

            if (tp.params.types.size() < trait.params.types.size()) {
                //TODO(sp, "Defaults in UfcsKnown - " << p << " - " << tp.m_params << " vs " << trait.m_params.fmt_args());
                // TOOD: Where does this usually get expanded then?
            }
        }

        // TODO: Would like to remove this, but it's required still (for expressions)
        if (auto* pe = p.data.opt_UfcsUnknown()) {
            auto& e = *pe;
            TRACE_FUNCTION_FR("UfcsUnknown - p=" << p, p);

            updateType(e.type);
            this->visitPathParams(e.params);

            // If processing a trait, and the type is 'Self', search for the type/method on the trait
            // - Explicitly encoded because `Self::Type` has a different meaning to `MyType::Type` (the latter will search bounds first)
            // - NOTE: Could be in an inherent block, where there's no trait
            if (/*m_current_type &&*/ currentTrait && e.type == crate.types.self()) {
                HIRGenericPath traitPath;
                if (currentTraitPath_->traitPath()) {
                    traitPath = HIRGenericPath(*currentTraitPath_->traitPath());
                    traitPath.params = currentTraitPath_->traitArgs()->clone();
                } else {
                    traitPath = HIRGenericPath(currentTraitPath_->getSimplePath());
                    traitPath.params = currentTrait->params.makeNopParams(crate.types, 0);
                }
                if (locateInTraitAndSet(pc, traitPath, *currentTrait, p.data)) {
                    assert(!p.data.is_UfcsUnknown());
                    // Success!
                    // - If in an expression (and not in a `trait` provided impl), clear the params
                    if (inExpr && !inTraitDef_) {
                        for (auto& t : p.data.as_UfcsKnown().trait.params.types) {
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
            if (currentType_) {
                rewritePathTysWith(crate.types, p, [&](HIRTypeRef& t, HIRTypeData& data) -> bool {
                    if (data.is_Generic() && data.as_Generic().binding == GENERICSelf) {
                        t = currentType_;
                    }
                    return false;
                });
            }

            // Search for matching impls in current generic blocks
            if (resolve_.itemGenericsPtr() != nullptr && locateTraitItemInBounds(pc, e.type, *resolve_.itemGenericsPtr(), p.data)) {
                DEBUG("Found in item params, p = " << p);
                assert(!p.data.is_UfcsUnknown());
                return;
            }
            if (resolve_.implGenericsPtr() != nullptr && locateTraitItemInBounds(pc, e.type, *resolve_.implGenericsPtr(), p.data)) {
                DEBUG("Found in impl params, p = " << p);
                assert(!p.data.is_UfcsUnknown());
                return;
            }

            // `<dyn Trait>::item` can name an item supplied by a supertrait.
            // Resolve it from the trait object's principal trait before
            // looking for an implementation of the trait object type.
            if (const auto* traitObject = e.type->opt_TraitObject()) {
                const auto& principal = traitObject->trait;
                if (principal.traitPtr && locateInTraitAndSet(pc, principal.path, *principal.traitPtr, p.data)) {
                    DEBUG("Found in trait object bounds, p = " << p);
                    assert(!p.data.is_UfcsUnknown());
                    return;
                }
            }

            // TODO: Control ordering with a flag in UfcsUnknown
            // 1. Search for applicable inherent methods (COMES FIRST!)
            if (this->resolve_UfcsUnknown_inherent(curModPath, p, pc, p.data)) {
                assert(!p.data.is_UfcsUnknown());
                return;
            }
            assert(p.data.is_UfcsUnknown());

            // If the type is the impl type, look for items AFTER generic lookup
            // TODO: Should this look up in-scope traits instead of hard-coding this hack?
            if (currentType_ && currentTrait && e.type == currentType_) {
                HIRGenericPath traitPath;
                if (currentTraitPath_->traitPath()) {
                    traitPath = HIRGenericPath(*currentTraitPath_->traitPath());
                    traitPath.params = currentTraitPath_->traitArgs()->clone();
                } else {
                    traitPath = HIRGenericPath(currentTraitPath_->getSimplePath());
                    traitPath.params = currentTrait->params.makeNopParams(crate.types, 0);
                }

                if (locateInTraitAndSet(pc, traitPath, *currentTrait, p.data)) {
                    assert(!p.data.is_UfcsUnknown());
                    // Success!
                    if (inExpr) {
                        for (auto& t : p.data.as_UfcsKnown().trait.params.types) {
                            t = crate.types.infer();
                        }
                    }
                    DEBUG("Found in Self (impl" << (inExpr ? " expr" : "") << "), p = " << p);
                    return;
                }
                DEBUG("- Item " << e.item << " not found in Self - ty=" << e.type);
            }

            // If the inner type is a UFCS of a known trait, then search traits on that type
            if (e.type->is_Path() && e.type->as_Path().path.data.is_UfcsKnown()) {
                auto& innerPe = e.type->as_Path().path.data.as_UfcsKnown();
                const auto& trait = crate.getTraitByPath(sp, innerPe.trait.path);
                const auto& atyDef = trait.types.at(innerPe.item);
                auto mstate = MonomorphStatePtr(crate.types, innerPe.type, &innerPe.trait.params, nullptr);
                for (const auto& t : atyDef.traitBounds) {
                    auto traitPath = mstate.monomorphGenericpath(sp, t.path, /*allow_infer*/ true);
                    DEBUG("Searching ATY bound: " << traitPath);
                    // Search within this (bounded) trait for the outer item
                    if (this->locateInTraitImplAndSet(sp, pc, mv$(traitPath), *t.traitPtr, p.data)) {
                        assert(!p.data.is_UfcsUnknown());
                        return;
                    }
                }
                DEBUG("- Item " << e.item << " not found in ATY bounds");
                // TODO: Search bounds with `where`?
            }

            // 2. Search all impls of in-scope traits for this method on this type
            if (this->resolve_UfcsUnknown_trait(p, pc, p.data)) {
                assert(!p.data.is_UfcsUnknown());
                return;
            }
            assert(p.data.is_UfcsUnknown());
            DEBUG("e.type = " << e.type);

            // If the inner is an enum, look for an enum variant? (check context)
            if ((pc == HIRVisitor::PathContext::VALUE /*|| pc == HIR::Visitor::PathContext::PATTERN*/) && e.type->is_Path() && e.type->as_Path().binding.is_Enum()) {
                const auto& enm = *e.type->as_Path().binding.as_Enum();
                auto idx = enm.findVariant(e.item);
                if (idx != SIZE_MAX) {
                    DEBUG("Found variant " << e.type << " #" << idx);
                    if (enm.data.is_Value() || !enm.data.as_Data()[idx].isStruct) {
                        auto gp = e.type->as_Path().path.data.as_Generic().clone();
                        gp.path += e.item;
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
                    if (enm.data.is_Data() && enm.data.as_Data()[idx].isStruct) {
                        auto gp = e.type->as_Path().path.data.as_Generic().clone();
                        gp.path += e.item;
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

            switch (pat.data.tag()) {
default:
                break;
                case HIRPatternData::TAG_Value: {
                    auto& e = pat.data.as_Value();
                    this->visitPatternValue(sp, pat, e.val);
                    if (e.val.is_Named() && e.val.as_Named().path.data.is_Generic() && e.val.as_Named().path.data.as_Generic().path.components().size() > 1) {
                        auto& gp = e.val.as_Named().path.data.as_Generic();
                        if (const auto* enmP = crate.getTypeitemByPath(sp, gp.path, false, true).opt_Enum()) {
                            unsigned idx = enmP->findVariant(gp.path.components().back());
                            pat.data = HIRPattern::Data::make_PathValue({mv$(gp), HIRPattern::PathBinding::make_Enum({enmP, idx})});
                        }
                    }
                    break;
                }
                case HIRPatternData::TAG_Range: {
                    auto& e = pat.data.as_Range();
                    if (e.start) {
                        this->visitPatternValue(sp, pat, *e.start);
                    }
                    if (e.end) {
                        this->visitPatternValue(sp, pat, *e.end);
                    }
                    break;
                }
                case HIRPatternData::TAG_PathValue: {
                    auto& e = pat.data.as_PathValue();
                    this->resolvePatternBinding(sp, e.path, e.binding);
                    break;
                }
                case HIRPatternData::TAG_PathTuple: {
                    auto& e = pat.data.as_PathTuple();
                    this->resolvePatternBinding(sp, e.path, e.binding);
                    break;
                }
                case HIRPatternData::TAG_PathNamed: {
                    auto& e = pat.data.as_PathNamed();
                    this->resolvePatternBinding(sp, e.path, e.binding);
                    break;
                }
            }
    }

    void resolvePatternBinding(const Span& sp, HIRPath& path, HIRPattern::PathBinding& binding) {
        if (!binding.is_Unbound()) {
            return;
        }

        auto ty = crate.types.path(path.clone(), {});
        ty = this->visitType(ty);
        ASSERT_BUG(sp, ty->is_Path(), "Pattern associated type didn't resolve to a path - " << ty);

        const auto& te = ty->as_Path();
        ASSERT_BUG(sp, te.path.data.is_Generic(), "Pattern associated type didn't resolve to a generic path - " << ty);
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
                switch (ve.path.data.tag()) {
                    case HIRPathData::TAG_Generic: {
                        // Already done
                        break;
                    }
                    case HIRPathData::TAG_UfcsUnknown: {
                        BUG(sp, "UfcsUnknown still in pattern value - " << pat);
                        break;
                    }
                    case HIRPathData::TAG_UfcsInherent: {
                        auto& pe = ve.path.data.as_UfcsInherent();
                        bool rv = crate.findTypeImpls(pe.type, HIRResolvePlaceholdersNop(), [&](const auto& impl) {
                            DEBUG("- matched inherent impl" << impl.params.fmtArgs() << " " << impl.type);
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
                        break;
                    }
                    case HIRPathData::TAG_UfcsKnown: {
                        // The pattern's expected type participates in selecting `Self` for a
                        // trait-associated constant.  Keep the trait declaration here instead of
                        // committing to the first fuzzy impl before expression type checking.
                        MonomorphState params(crate.types);
                        auto value = resolve_.getValue(sp, ve.path, params, /*signatureOnly=*/true);
                        if (const auto* constant = value.opt_Constant()) {
                            ve.binding = *constant;
                        } else {
                            ERROR(sp, E0000, "Constant " << ve.path << " couldn't be found");
                        }
                        break;
                    }
                }
        }
    }
};

template <typename T>
void sortImplGroup(HIRCrate::ImplGroup<std::unique_ptr<T>>& ig, ::std::function<void(::std::ostream& os, const T&)> fmt) {
    auto newEnd = ::std::remove_if(ig.generic.begin(), ig.generic.end(), [&ig, &fmt](::std::unique_ptr<T>& tyImpl) {
        const auto& type = tyImpl->type; // Using field accesses in templates feels so dirty
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
        TRACE_FUNCTION_F("impl" << impl.params.fmtArgs() << " " << impl.type);
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
        os << "impl" << i.params.fmtArgs() << " " << i.type;
    });
    DEBUG("Type impl counts: " << crate.typeImpls.named.size() << " path groups, " << crate.typeImpls.nonNamed.size() << " primitive, " << crate.typeImpls.generic.size() << " ungrouped");
    for (auto& implGroup : crate.traitImpls) {
        sortImplGroup<HIRTraitImpl>(implGroup.second, [&](::std::ostream& os, const HIRTraitImpl& i) {
            os << "impl" << i.params.fmtArgs() << " " << implGroup.first << i.traitArgs << " for " << i.type;
        });
    }
    for (auto& implGroup : crate.markerImpls) {
        sortImplGroup<HIRMarkerImpl>(implGroup.second, [&](::std::ostream& os, const HIRMarkerImpl& i) {
            os << "impl" << i.params.fmtArgs() << " " << implGroup.first << i.traitArgs << " for " << i.type << " {}";
        });
    }

    // Create indexes
    pushIndexImpls(crate, crate);
    for (const auto& ec : crate.extCrates) {
        pushIndexImpls(crate, *ec.second.data);
    }

}

void ConvertHIRIndexInherentMethods(const WireBoard& wb, const HIRCrate& crate) {
    const auto& langBox = crate.getLangItemPathOpt("owned_box");
    pushIndexInherentMethods(*wb.inherentMethods, langBox, crate);
    for (const auto& ec : crate.extCrates) {
        pushIndexInherentMethods(*wb.inherentMethods, langBox, *ec.second.data);
    }
}
