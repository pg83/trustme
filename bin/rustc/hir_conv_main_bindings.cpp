#include "hir_conv_main_bindings.h"

#include "hir_conv_main_bindings.h"
#include "hir_visitor.h"
#include "hir_expr.h"
#include "mir_mir.h"
#include <algorithm> // std::find_if

#include "mir_helpers.h"

#include "hir_typeck_static.h"
#include "hir_typeck_expr_visit.h" // For ModuleState
#include "hir_expr_state.h"
#include "hir_hir.h"
#include "hir_typeck_common.h" // monomorphise_type_with
#include <std/mem/obj_pool.h>

void ConvertHIRBind(::HIR::Crate& crate);

namespace {

    enum class Target {
        TypeItem,
        Struct,
        Enum,
        EnumVariant,
    };

    const void* getTypePointer(const Span& sp, const ::HIR::Crate& crate, const ::HIR::SimplePath& path, Target t) {
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
                    TU_IFLET(::HIR::TypeItem, ti, Struct, e2, return &e2;)
                    else {
                        ERROR(sp, E0000, "Expected a struct at " << path << ", got a " << ti.tag_str());
                    }
                    break;
                case Target::Enum:
                    TU_IFLET(::HIR::TypeItem, ti, Enum, e2, return &e2;)
                    else {
                        ERROR(sp, E0000, "Expected a enum at " << path << ", got a " << ti.tag_str());
                    }
                    break;
            }
            throw "";
        }
    }

    void fixTypeParams(HIR::TypeInterner& types, const Span& sp, const ::HIR::GenericParams& params_def, ::HIR::PathParams& params) {
#if 1
        if (params.mLifetimes.size() == 0) {
            params.mLifetimes.resize(params_def.mLifetimes.size());
        }
        if (params.mLifetimes.size() != params_def.mLifetimes.size()) {
            ERROR(sp, E0000, "Incorrect lifetime param count, expected " << params_def.mLifetimes.size() << ", got " << params.mLifetimes.size());
        }

        if (params.types.size() == 0) {
            while (params.types.size() < params_def.types.size()) {
                params.types.push_back(types.infer());
            }
            // TODO: Optionally fill in the defaults?
        }
        if (params.types.size() != params_def.types.size()) {
            ERROR(sp, E0000, "Incorrect parameter count, expected " << params_def.types.size() << ", got " << params.types.size());
        }

        if (params.values.size() == 0) {
            params.values.resize(params_def.values.size());
        }
        if (params.values.size() != params_def.values.size()) {
            ERROR(sp, E0000, "Incorrect value parameter count, expected " << params_def.values.size() << ", got " << params.values.size());
        }
#endif
    }

    void fixParamCount(HIR::TypeInterner& types, const Span& sp, const ::HIR::GenericPath& path, const ::HIR::GenericParams& param_defs, ::HIR::PathParams& params, bool fillInfer = true, const ::HIR::TypeData* self_ty = nullptr) {
        TRACE_FUNCTION_FR(param_defs.fmtArgs() << " -> " << params << " (fill_infer=" << fillInfer << ")", params);
        if (params.mLifetimes.size() != param_defs.mLifetimes.size()) {
            if (params.mLifetimes.size() == 0 && fillInfer) {
                params.mLifetimes.resize(param_defs.mLifetimes.size());
            }
        }
        if (params.types.size() != param_defs.types.size()) {
            TRACE_FUNCTION_FR(path, params);

            if (params.types.size() == 0 && fillInfer) {
                while (params.types.size() < param_defs.types.size()) {
                    params.types.push_back(types.infer());
                }
            } else if (params.types.size() > param_defs.types.size()) {
                ERROR(sp, E0000, "Too many type parameters passed to " << path);
            } else {
                while (params.types.size() < param_defs.types.size()) {
                    const auto& typ = param_defs.types[params.types.size()];
                    if (typ.defaultValue->is_Infer()) {
                        ERROR(sp, E0000, "Omitted type parameter with no default in " << path);
                    } else {
                        // TODO: Does expanding defaults need a custom monomorphiser that can handle later defaults?
                        MonomorphStatePtr ms(types, self_ty, &params, nullptr);
                        auto ty = ms.monomorphType(sp, typ.defaultValue);
                        params.types.push_back(mv$(ty));
                    }
                }
            }
        }
        if (params.values.size() != param_defs.values.size()) {
            if (params.values.size() == 0 && fillInfer) {
                params.values.resize(param_defs.values.size());
            } else if (params.values.size() > param_defs.values.size()) {
                ERROR(sp, E0000, "Too many value parameters passed to " << path);
            } else {
                while (params.values.size() < param_defs.values.size()) {
                    const auto& val = param_defs.values[params.values.size()];
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

    class BindVisitor: public ::HIR::Visitor {
        const ::HIR::Crate& crate;

        typeck::ModuleState ms;

        struct CurMod {
            const ::HIR::Module* ptr;
            const ::HIR::ItemPath* path;
        } curModule;

        unsigned inExpr;

        ::HIR::ItemPath* fcnPath = nullptr;
        ::HIR::Function* fcnPtr = nullptr;
        unsigned int fcnErasedCount = 0;

    public:
        BindVisitor(const ::HIR::Crate& crate)
            : ::HIR::Visitor(nullptr, crate.types)
            , crate(crate)
            , ms(crate)
            , inExpr(0)
        {
            static ::HIR::ItemPath root_path("");
            curModule.ptr = &crate.rootModule;
            curModule.path = &root_path;
        }

        HIR::TypeInterner& interner() const { return crate.types; }

        void visit_module(::HIR::ItemPath p, ::HIR::Module& mod) override {
            auto parent_mod = curModule;
            curModule.ptr = &mod;
            curModule.path = &p;

            ms.push_traits(p, mod);
            ::HIR::Visitor::visit_module(p, mod);
            ms.pop_traits(mod);

            curModule = parent_mod;
        }

        void visit_trait_path(::HIR::TraitPath& p) override {
            static Span sp;
            p.traitPtr = &crate.getTraitByPath(sp, p.mPath.mPath);

            ::HIR::Visitor::visit_trait_path(p);
        }

        void visit_literal(const Span& sp, EncodedLiteral& lit) {
            for (auto& r : lit.relocations) {
                if (r.p) {
                    visit_path(*r.p, ::HIR::Visitor::PathContext::VALUE);
                }
            }
        }

        void visitPatternValue(const Span& sp, ::HIR::Pattern& pat, ::HIR::Pattern::Value& val) {
            bool isSingleValue = pat.mData.is_Value();

            if (auto* ve = val.opt_Named()) {
                if (auto* pe = ve->path.mData.opt_Generic()) {
                    const auto& path = pe->mPath;
                    const auto& pc = path.components().back();
                    const ::HIR::Module* mod = nullptr;
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
                            HIR::GenericPath path = std::move(*pe);
                            fixTypeParams(crate.types, sp, enm->mParams, path.mParams);
                            pat.mData = ::HIR::Pattern::Data::make_PathValue({mv$(path), ::HIR::Pattern::PathBinding::make_Enum({enm, static_cast<unsigned>(idx)})});
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
                            ERROR(sp, E0000, "Value pattern " << pat << " pointing to unexpected item type - " << it->second->ent.tag_str());
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
                                pat.mData = ::HIR::Pattern::Data::make_PathValue({mv$(path), &str});
                            }
                        }
                    }
                } else {
                    // NOTE: Defer until Resolve UFCS (saves duplicating logic)
                }
            }
        }

        void visit_pattern(::HIR::Pattern& pat) override {
            static Span _sp = Span();
            const Span& sp = _sp;

            ::HIR::Visitor::visit_pattern(pat);

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

        void visit_constgeneric(::HIR::ConstGeneric& value) override {
            HIR::Visitor::visit_constgeneric(value);
            if (auto* unevaluated = value.opt_Unevaluated()) {
                if (ms.implGenerics) {
                    (*unevaluated)->params_impl = ms.implGenerics->makeNopParams(crate.types, 0);
                }
                if (ms.itemGenerics) {
                    (*unevaluated)->params_item = ms.itemGenerics->makeNopParams(crate.types, 1);
                }
            }
        }

        void visit_params(::HIR::GenericParams& params) override {
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
                        const auto& trait = crate.getTraitByPath(sp, aty.second.source_trait.mPath);
                        fixParamCount(crate.types, sp, be->trait.mPath, trait.mParams, aty.second.source_trait.mParams, /*fill_infer=*/false, be->type);
                    }
                    for (auto& aty : be->trait.typeBounds) {
                        const auto& trait = crate.getTraitByPath(sp, aty.second.source_trait.mPath);
                        fixParamCount(crate.types, sp, be->trait.mPath, trait.mParams, aty.second.source_trait.mParams, /*fill_infer=*/false, be->type);
                    }
                }
            }

            ::HIR::Visitor::visit_params(params);
        }

        void visit_associatedtype(HIR::ItemPath p, ::HIR::AssociatedType& item) override {
            static Span sp;
            HIR::Visitor::visit_associatedtype(p, item);
            HIR::TypeRef ty = crate.types.path(p.getFullPath(), {});
            for (auto& bound : item.traitBounds) {
                const auto& trait = crate.getTraitByPath(sp, bound.mPath.mPath);
                fixParamCount(crate.types, sp, bound.mPath, trait.mParams, bound.mPath.mParams, /*fill_infer=*/false, ty);
            }
        }

        void visit_type(::HIR::TypeRef& ty) override {
            visit_type_inner(ty);
        }

        void visit_type_inner(::HIR::TypeRef& ty, bool doBind = true) {
            //TRACE_FUNCTION_F(ty);
            static Span sp;
            auto data = ty->cloneData();
            bool dataVisited = false;

            if (auto* e = data.opt_Path()) {
                TU_MATCH_HDRA( (e->path.mData), {)
                TU_ARMA(Generic, pe) {
                        if (!doBind) {
                            break;
                        }
                        const auto& item = *reinterpret_cast<const ::HIR::TypeItem*>(getTypePointer(sp, crate, pe.mPath, Target::TypeItem));
                        TU_MATCH_DEF(
                            ::HIR::TypeItem,
                            (item),
                            (e3),
                            (ERROR(sp, E0000, "Unexpected item type returned for " << pe.mPath << " - " << item.tag_str());),
                            (
                                TypeAlias, BUG(sp, "TypeAlias encountered after `Resolve Type Aliases` - " << ty);
                                // Assume it'll be filled out, with the correct binding
                            ),
                            (ExternType, e->binding = ::HIR::TypePathBinding::make_ExternType(&e3); DEBUG("- " << ty);),
                            (Struct, fixParamCount(crate.types, sp, pe, e3.mParams, pe.mParams, /*fill_infer=*/inExpr != 0); e->binding = ::HIR::TypePathBinding::make_Struct(&e3); DEBUG("- " << ty);),
                            (Union, fixParamCount(crate.types, sp, pe, e3.mParams, pe.mParams, /*fill_infer=*/inExpr != 0); e->binding = ::HIR::TypePathBinding::make_Union(&e3); DEBUG("- " << ty);),
                            (Enum, fixParamCount(crate.types, sp, pe, e3.mParams, pe.mParams, /*fill_infer=*/inExpr != 0); e->binding = ::HIR::TypePathBinding::make_Enum(&e3); DEBUG("- " << ty);),
                            (Trait,
                             // TODO: Should this reassign instead?
                             data = ::HIR::TypeData::make_TraitObject({::HIR::TraitPath{{}, mv$(pe), {}, {}}, {}, {}});)
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
                            e->binding = ::HIR::TypePathBinding::make_Opaque({});
                        } else if (pe.type->is_Generic()) {
                            // - Generic type, opaque resut. (TODO: Sometimes these are known - via generic bounds)
                            e->binding = ::HIR::TypePathBinding::make_Opaque({});
                        } else {
                            //bool found = find_impl(sp, m_crate, pe.trait.m_path, pe.trait.m_params, *pe.type, [&](const auto& impl_params, const auto& impl) {
                            //    DEBUG("TODO");
                            //    return false;
                            //    });
                            //if( found ) {
                            //}
                            //TODO(sp, "Resolve known UfcsKnown - " << ty);
                        }
                    }
                }
            } else if (auto* te = data.opt_ErasedType()) {
                HIR::TypeRef ty_eself = crate.types.generic("ErasedSelf", GENERICErasedSelf);
                for (auto& t : te->traits) {
                    const auto& trait = crate.getTraitByPath(sp, t.mPath.mPath);
                    fixParamCount(crate.types, sp, t.mPath, trait.mParams, t.mPath.mParams, /*fill_infer=*/inExpr, ty_eself);
                }

                if (auto* ee = te->inner.opt_Fcn()) {
                    DEBUG("Set origin of ErasedType - " << ty);
                    // If not, figure out what to do with it

                    // If the function path is set, we're processing the return type of a function
                    // - Add this to the list of erased types associated with the function
                    if (ee->origin != HIR::SimplePath()) {
                        // Already set, somehow (maybe we're visiting the function after expansion)
                    } else if (fcnPath) {
                        assert(fcnPtr);
                        DEBUG(*fcnPath << " " << fcnErasedCount);

                        ::HIR::PathParams params = fcnPtr->mParams.makeNopParams(crate.types, 1);
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
                                e2.impl_params = ms.implGenerics->makeNopParams(crate.types, 0);
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
                        visit_type_data(data);
                        dataVisited = true;

                        size_t idx = fcnPtr->mParams.types.size();
                        auto name = RcString::newInterned(FMT("erased$" << idx));
                        DEBUG("-> " << name);
                        auto newTy = crate.types.generic(name, 256 + idx);
                        fcnPtr->mParams.types.push_back({name, crate.types.infer(), te->isSized});
                        for (auto& trait : te->traits) {
                            struct M: MonomorphiserNop {
                                const HIR::TypeData* newTy;

                                M(HIR::TypeInterner& types, const HIR::TypeData* ty)
                                    : MonomorphiserNop(types)
                                    , newTy(ty)
                                {
                                }

                                ::HIR::TypeRef getType(const Span& sp, const ::HIR::GenericRef& ty) const override {
                                    if (ty.binding == GENERICErasedSelf) {
                                        return newTy;
                                    }
                                    return types.generic(ty.name, ty.binding);
                                }
                            } m{crate.types, newTy};

                            // TODO: Monomorph the trait to replace `Self` with this generic?
                            // - Except, that should it be?
                            fcnPtr->mParams.bounds.push_back(::HIR::GenericBound::make_TraitBound({nullptr, newTy, m.monomorphTraitpath(sp, trait, false)}));
                        }
                        for (const auto& lft : te->lifetimeBounds) {
                            fcnPtr->mParams.bounds.push_back(::HIR::GenericBound::make_TypeLifetime({newTy, lft}));
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
                if (te->mTrait.mPath.mPath != HIR::SimplePath()) {
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
                visit_type_data(data);
            }
            ty = crate.types.intern(mv$(data));
        }

        void visit_type_impl(::HIR::TypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.mType << " - from " << impl.srcModule);
            auto _ = this->ms.set_impl_generics(impl.mParams);

            auto modIp = ::HIR::ItemPath(impl.srcModule);
            const auto* mod = (impl.srcModule != ::HIR::SimplePath() ? &this->ms.crate.getModByPath(Span(), impl.srcModule) : nullptr);
            if (mod) {
                ms.push_traits(impl.srcModule, *mod);
                curModule.ptr = mod;
                curModule.path = &modIp;
            }
            ::HIR::Visitor::visit_type_impl(impl);
            if (mod) {
                ms.pop_traits(*mod);
            }
        }

        void visit_inherent_type(::HIR::ItemPath p, ::HIR::TypeAlias& item) override {
            auto _ = this->ms.set_item_generics(item.mParams);
            ::HIR::Visitor::visit_inherent_type(p, item);
        }

        void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
            TRACE_FUNCTION_F("impl " << trait_path << " for " << impl.mType);
            auto trait_gpath = ::HIR::GenericPath(trait_path, impl.traitArgs.clone());
            auto _0 = this->ms.set_current_trait_impl(impl);
            auto _1 = this->ms.set_current_trait(trait_gpath);
            auto _ = this->ms.set_impl_generics(impl.mParams);

            auto modIp = ::HIR::ItemPath(impl.srcModule);
            const auto* mod = (impl.srcModule != ::HIR::SimplePath() ? &this->ms.crate.getModByPath(Span(), impl.srcModule) : nullptr);
            if (mod) {
                ms.push_traits(impl.srcModule, *mod);
                curModule.ptr = mod;
                curModule.path = &modIp;
            }
            ms.traits.push_back(::std::make_pair(&trait_path, &this->ms.crate.getTraitByPath(Span(), trait_path)));
            ::HIR::Visitor::visit_trait_impl(trait_path, impl);
            ms.traits.pop_back();
            if (mod) {
                ms.pop_traits(*mod);
            }
        }

        void visit_marker_impl(const ::HIR::SimplePath& trait_path, ::HIR::MarkerImpl& impl) override {
            TRACE_FUNCTION_F("impl " << trait_path << " for " << impl.mType << " { }");
            auto _ = this->ms.set_impl_generics(impl.mParams);

            auto modIp = ::HIR::ItemPath(impl.srcModule);
            const auto* mod = (impl.srcModule != ::HIR::SimplePath() ? &this->ms.crate.getModByPath(Span(), impl.srcModule) : nullptr);
            if (mod) {
                ms.push_traits(impl.srcModule, *mod);
                curModule.ptr = mod;
                curModule.path = &modIp;
            }
            ::HIR::Visitor::visit_marker_impl(trait_path, impl);
            if (mod) {
                ms.pop_traits(*mod);
            }
        }

        void visit_trait(::HIR::ItemPath p, ::HIR::Trait& item) override {
            auto _ = this->ms.set_impl_generics(item.mParams);
            ::HIR::Visitor::visit_trait(p, item);
        }

        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override {
            auto _ = this->ms.set_impl_generics(item.mParams);
            ::HIR::Visitor::visit_enum(p, item);
        }

        void visit_struct(::HIR::ItemPath p, ::HIR::Struct& item) override {
            auto _ = this->ms.set_impl_generics(item.mParams);
            ::HIR::Visitor::visit_struct(p, item);
        }

        void visit_union(::HIR::ItemPath p, ::HIR::Union& item) override {
            auto _ = this->ms.set_impl_generics(item.mParams);
            ::HIR::Visitor::visit_union(p, item);
        }

        void visit_function(::HIR::ItemPath p, ::HIR::Function& item) override {
            auto _ = this->ms.set_item_generics(item.mParams);
            fcnPtr = &item;

            // Visit arguments
            // - Used to convert `impl Trait` in argument position into generics
            // - Done first so the path in return-position `impl Trait` is valid
            //m_cur_params = &item.m_params;
            //m_cur_params_level = 1;
            for (auto& arg : item.mArgs) {
                TRACE_FUNCTION_F("ARG " << arg);
                visit_type(arg.second);
            }
            //m_cur_params = nullptr;

            // Visit return type (populates path for `impl Trait` in return position
            fcnPath = &p;
            fcnErasedCount = 0;
            {
                TRACE_FUNCTION_F("RET " << item.returnType);
                visit_type(item.returnType);
            }
            fcnPath = nullptr;
            fcnPtr = nullptr;

            ::HIR::Visitor::visit_function(p, item);
        }

        void visit_static(::HIR::ItemPath p, ::HIR::Static& item) override {
            //auto _ = this->m_ms.set_item_generics(item.m_params);
            ::HIR::Visitor::visit_static(p, item);
            visit_literal(Span(), item.valueRes);
        }

        void visit_constant(::HIR::ItemPath p, ::HIR::Constant& item) override {
            auto _ = this->ms.set_item_generics(item.mParams);
            ::HIR::Visitor::visit_constant(p, item);
            visit_literal(Span(), item.valueRes);
        }

        // Actual expressions
        void visit_expr(::HIR::ExprPtr& expr) override {
            struct ExprVisitor: public ::HIR::ExprVisitorDef {
                BindVisitor& upper_visitor;

                ExprVisitor(BindVisitor& uv)
                    : ::HIR::ExprVisitorDef(uv.interner())
                    , upper_visitor(uv)
                {
                }

                void visit_generic_path(::HIR::Visitor::PathContext pc, ::HIR::GenericPath& p) override {
                    upper_visitor.visit_generic_path(p, pc);
                }

                void visit_type(::HIR::TypeRef& ty) override {
                    upper_visitor.visit_type_inner(ty, true);
                }

                void visit_node_ptr(::HIR::ExprNodeP& nodePtr) override {
                    upper_visitor.visit_type(nodePtr->resType);
                    ::HIR::ExprVisitorDef::visit_node_ptr(nodePtr);
                }

                void visit(::HIR::ExprNodeLet& node) override {
                    upper_visitor.visit_type(node.mType);
                    upper_visitor.visit_pattern(node.pattern);
                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNodeMatch& node) override {
                    for (auto& arm : node.arms) {
                        for (auto& pat : arm.patterns) {
                            upper_visitor.visit_pattern(pat);
                        }
                        for (auto& g : arm.guards) {
                            upper_visitor.visit_pattern(g.pat);
                        }
                    }
                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNodePathValue& node) override {
                    upper_visitor.visit_path(node.mPath, ::HIR::Visitor::PathContext::VALUE);
                }

                void visit(::HIR::ExprNodeCallPath& node) override {
                    upper_visitor.visit_path(node.mPath, ::HIR::Visitor::PathContext::VALUE);
                    ::HIR::ExprVisitorDef::visit(node);

                    // #[rustc_legacy_const_generics] - A backwards compatability hack added between 1.39 and 1.54 to be backwards compatible with the x86 intrinsics
                    // - Rewrites some literal arguments into const generics
                    if (auto* e = node.mPath.mData.opt_Generic()) {
                        auto& fcn = upper_visitor.crate.getFunctionByPath(node.span(), e->mPath);
                        if (!fcn.markings.rustc_legacy_const_generics.empty()) {
                            if (node.mArgs.size() == fcn.mArgs.size()) {
                                // Acceptable
                            } else if (node.mArgs.size() == fcn.mArgs.size() + fcn.markings.rustc_legacy_const_generics.size()) {
                                for (auto idx : fcn.markings.rustc_legacy_const_generics) {
                                    auto& argNode = node.mArgs.at(idx);
                                    assert(argNode);
                                    // TODO: Check that the expression is a valid const (no locals referenced, no function calls?)
                                    // - Allow: Arithmatic, casts, literals
                                    //if( !cast<const HIR::ExprNodeLiteral>(arg_node.get()) )
                                    //    ERROR(arg_node->span(), E0000, "Argument " << idx << " must be a literal for #[rustc_legacy_const_generics] tagged function");
                                    HIR::ExprPtr ep{std::move(argNode)};
                                    e->mParams.values.push_back(HIR::ConstGeneric(std::make_unique<HIR::ConstGenericUnevaluated>(std::move(ep))));
                                    // - Visit to ensure that the expr state gets filled
                                    upper_visitor.visit_constgeneric(e->mParams.values.back());
                                }
                                auto newEnd = std::remove_if(node.mArgs.begin(), node.mArgs.end(), [](const HIR::ExprNodeP& np) {
                                    return !np;
                                });
                                node.mArgs.erase(newEnd, node.mArgs.end());
                            } else {
                                // Will error downstream
                            }
                        }
                    }
                }

                void visit(::HIR::ExprNodeCallMethod& node) override {
                    upper_visitor.visit_path_params(node.mParams);
                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNodeStructLiteral& node) override {
                    upper_visitor.visit_type_inner(node.mType, false);

                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNodeArraySized& node) override {
                    auto& as = node.mSize;
                    if (as.is_Unevaluated()) {
                        upper_visitor.visit_constgeneric(as.as_Unevaluated());
                    }
                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNodeClosure& node) override {
                    upper_visitor.visit_type(node.returnType);
                    for (auto& arg : node.mArgs) {
                        upper_visitor.visit_pattern(arg.first);
                        upper_visitor.visit_type(arg.second);
                    }
                    ::HIR::ExprVisitorDef::visit(node);
                }
            };

            for (auto& ty : expr.erasedTypes) {
                visit_type(ty);
            }

            // Set up the module state
            {
                expr.state = ::HIR::ExprStatePtr(crate.pool, ::HIR::ExprState(crate.types, *curModule.ptr, curModule.path->getSimplePath()));
                expr.state->traits = ms.traits; // TODO: Only obtain the current module's set
                expr.state->implGenerics = ms.implGenerics;
                expr.state->itemGenerics = ms.itemGenerics;
                expr.state->currentTraitImpl = ms.currentTraitImpl;
                if (ms.currentTrait) {
                    expr.state->currentTraitPath = ms.currentTrait->mPath;
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
                    this->visit_type(ty);
                }

                struct MirVisitor: public ::MIR::visit::VisitorMut {
                    BindVisitor& upper_visitor;

                    MirVisitor(BindVisitor& upper_visitor)
                        : upper_visitor(upper_visitor)
                    {
                    }

                    void visit_type(::HIR::TypeRef& t) override {
                        upper_visitor.visit_type(t);
                    }

                    void visit_path(::HIR::Path& p) override {
                        upper_visitor.visit_path(p, ::HIR::Visitor::PathContext::VALUE);
                    }

                    bool visit_lvalue(::MIR::LValue& lv, ::MIR::visit::ValUsage u) override {
                        if (lv.root.is_Static()) {
                            upper_visitor.visit_path(lv.root.as_Static(), ::HIR::Visitor::PathContext::VALUE);
                        }
                        return false;
                    }
                };

                MirVisitor mv(*this);
                for (auto& block : mir->blocks) {
                    for (auto& stmt : block.statements) {
                        mv.visit_stmt(stmt);
                    }
                    mv.visit_terminator(block.terminator);
                }
            } else {
            }
        }
    };

    class VisitorEnumSuperTraits: public ::HIR::Visitor {
        const ::HIR::Crate& crate;

    public:
        VisitorEnumSuperTraits(const ::HIR::Crate& crate)
            : ::HIR::Visitor(nullptr, crate.types)
            , crate(crate)
        {
        }

        void visit_trait(::HIR::ItemPath ip, ::HIR::Trait& tr) override {
            static Span sp;
            TRACE_FUNCTION_F(ip);
            const auto ty_self = crate.types.self();

            // Enumerate supertraits and save for later stages
            struct Enumerate {
                HIR::TypeInterner& types;
                HIR::TypeRef ty_self;
                ::std::vector<::HIR::TraitPath> supertraits;
                ::std::vector<const ::HIR::TraitPath*> tp_stack;

                Enumerate(HIR::TypeInterner& types, HIR::TypeRef ty_self)
                    : types(types), ty_self(ty_self) {}

                void enumSupertraitsIn(const ::HIR::Trait& tr, ::HIR::TraitPath path) {
                    TRACE_FUNCTION_F(path);
                    tp_stack.push_back(&path);
                    auto& params = path.mPath.mParams;

                    // Fill defaulted parameters.
                    // NOTE: Doesn't do much error checking.
                    fixParamCount(types, sp, path.mPath, tr.mParams, path.mPath.mParams, false, ty_self);

                    auto monomorphCb = MonomorphStatePtr(types, ty_self, &params, nullptr);
                    auto monomorphTp = [&](const HIR::TraitPath& tp) -> HIR::TraitPath {
                        // TODO: if `path.m_path` has HRLs, then this needs HRLs (only if the HRLs get used?)
                        if ((tp.hrtbs && !tp.hrtbs->is_empty()) && (path.hrtbs && !path.hrtbs->is_empty())) {
                            // TODO: How to determine which to use?
                            // - May need to combine them.
                            TODO(sp, "Trait path and outer path both have HRLs, how to handle?");
                            return monomorphCb.monomorphTraitpath(sp, tp, false);
                        } else if (path.hrtbs && !path.hrtbs->is_empty()) {
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
                            if (be.type != ty_self) {
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
                    ::HIR::TraitPath outPath;
                    outPath.hrtbs = mv$(path.hrtbs);
                    outPath.mPath = mv$(path.mPath);
                    outPath.traitPtr = &tr;
                    fillTypeAliases(outPath);
                    // TODO: HRLs?
                    supertraits.push_back(std::move(outPath));
                    // Fill aliases from this path too
                    for (auto& st : supertraits) {
                        for (auto& tb : path.typeBounds) {
                            if (tb.second.source_trait == st.mPath) {
                                DEBUG("Add TypeBound: " << tb.first << " = " << tb.second.type);
                                st.typeBounds.insert(std::make_pair(tb.first, std::move(tb.second)));
                            }
                        }
                        for (auto& tb : path.traitBounds) {
                            if (tb.second.source_trait == st.mPath) {
                                DEBUG("Add TraitBound: " << tb.first << ": " << tb.second.traits);
                                st.traitBounds.insert(std::make_pair(tb.first, std::move(tb.second)));
                            }
                        }
                    }
                    tp_stack.pop_back();
                }

                void fillTypeAliases(HIR::TraitPath& outPath) const {
                    const HIR::Trait& tr = *outPath.traitPtr;
                    // - Locate associated types for this trait
                    for (const auto& ty : tr.types) {
                        if (outPath.typeBounds.count(ty.first) == 0) {
                            const HIR::TypeData* found = nullptr;

                            for (auto oit = tp_stack.rbegin(); oit != tp_stack.rend(); ++oit) {
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
                                outPath.typeBounds.insert(::std::make_pair(ty.first, ::HIR::TraitPath::AtyEqual{outPath.mPath.clone(), {}, found}));
                            }
                        }

                        if (outPath.traitBounds.count(ty.first) == 0) {
                            std::vector<HIR::TraitPath> traits;
                            for (auto oit = tp_stack.rbegin(); oit != tp_stack.rend(); ++oit) {
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
                                outPath.traitBounds.insert(::std::make_pair(ty.first, ::HIR::TraitPath::AtyBound{outPath.mPath.clone(), {}, mv$(traits)}));
                            }
                        }
                    }
                }
            };

            auto this_path = ip.getSimplePath();
            this_path.update_crate_name(crate.crateName);

            Enumerate e{crate.types, ty_self};
            for (const auto& pt : tr.parentTraits) {
                e.enumSupertraitsIn(*pt.traitPtr, pt.clone());
            }
            for (const auto& b : tr.mParams.bounds) {
                if (!b.is_TraitBound()) {
                    continue;
                }
                const auto& be = b.as_TraitBound();
                if (be.type != ty_self) {
                    continue;
                }
                const auto& pt = be.trait;

                // TODO: Remove this along with the from_ast.cpp hack
                if (pt.mPath.mPath == this_path) {
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

    class VisitorPost: public ::HIR::Visitor {
        const ::HIR::Crate& crate;

        typeck::ModuleState ms;

    public:
        VisitorPost(const ::HIR::Crate& crate)
            : ::HIR::Visitor(nullptr, crate.types)
            , crate(crate)
            , ms(crate)
        {
        }


        HIR::TypeInterner& interner() const { return crate.types; }

        void visit_module(::HIR::ItemPath p, ::HIR::Module& mod) override {
            ms.push_traits(p, mod);
            ::HIR::Visitor::visit_module(p, mod);
            ms.pop_traits(mod);
        }

        void visit_type(::HIR::TypeRef& ty) override {
            visit_type_inner(ty);
        }

        void visit_type_inner(::HIR::TypeRef& ty, bool doBind = true) {
            //TRACE_FUNCTION_F(ty);
            static Span sp;

            auto data = ty->cloneData();
            if (auto* te = data.opt_NamedFunction()) {
                if (te->def.is_Function() && te->def.as_Function() == nullptr) {
                    StaticTraitResolve resolve{crate};
                    resolve.set_both_generics_raw(ms.implGenerics, ms.itemGenerics);
                    MonomorphState unused_ms(crate.types);
                    const auto& v = resolve.getValue(sp, te->path, unused_ms, true);

                    TU_MATCH_HDRA( (v), {)
                    default:
                        TODO(sp, "Resolve external NamedFunction type - " << te->path << " : " << v.tag_str());
                        TU_ARMA(Function, e) {
                            te->def = e;
                        }
                        TU_ARMA(StructConstructor, e) {
                            te->def = e.s;
                        }
                        TU_ARMA(EnumConstructor, e) {
                            te->def = ::HIR::TypeDataNamedFunctionTy::make_EnumConstructor({e.e, e.v});
                        }
                    }
                }
            }

            visit_type_data(data);
            ty = crate.types.intern(mv$(data));
        }

        void visit_type_impl(::HIR::TypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.mType << " - from " << impl.srcModule);
            auto _ = this->ms.set_impl_generics(impl.mParams);

            const auto* mod = (impl.srcModule != ::HIR::SimplePath() ? &this->ms.crate.getModByPath(Span(), impl.srcModule) : nullptr);
            if (mod) {
                ms.push_traits(impl.srcModule, *mod);
            }
            ::HIR::Visitor::visit_type_impl(impl);
            if (mod) {
                ms.pop_traits(*mod);
            }
        }

        void visit_inherent_type(::HIR::ItemPath p, ::HIR::TypeAlias& item) override {
            auto _ = this->ms.set_item_generics(item.mParams);
            ::HIR::Visitor::visit_inherent_type(p, item);
        }

        void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
            TRACE_FUNCTION_F("impl " << trait_path << " for " << impl.mType);
            auto _ = this->ms.set_impl_generics(impl.mParams);

            const auto* mod = (impl.srcModule != ::HIR::SimplePath() ? &this->ms.crate.getModByPath(Span(), impl.srcModule) : nullptr);
            if (mod) {
                ms.push_traits(impl.srcModule, *mod);
            }
            ms.traits.push_back(::std::make_pair(&trait_path, &this->ms.crate.getTraitByPath(Span(), trait_path)));
            ::HIR::Visitor::visit_trait_impl(trait_path, impl);
            ms.traits.pop_back();
            if (mod) {
                ms.pop_traits(*mod);
            }
        }

        void visit_marker_impl(const ::HIR::SimplePath& trait_path, ::HIR::MarkerImpl& impl) override {
            TRACE_FUNCTION_F("impl " << trait_path << " for " << impl.mType << " { }");
            auto _ = this->ms.set_impl_generics(impl.mParams);

            const auto* mod = (impl.srcModule != ::HIR::SimplePath() ? &this->ms.crate.getModByPath(Span(), impl.srcModule) : nullptr);
            if (mod) {
                ms.push_traits(impl.srcModule, *mod);
            }
            ::HIR::Visitor::visit_marker_impl(trait_path, impl);
            if (mod) {
                ms.pop_traits(*mod);
            }
        }

        void visit_trait(::HIR::ItemPath p, ::HIR::Trait& item) override {
            auto _ = this->ms.set_impl_generics(item.mParams);
            ::HIR::Visitor::visit_trait(p, item);
        }

        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override {
            auto _ = this->ms.set_impl_generics(item.mParams);
            ::HIR::Visitor::visit_enum(p, item);
        }

        void visit_struct(::HIR::ItemPath p, ::HIR::Struct& item) override {
            auto _ = this->ms.set_impl_generics(item.mParams);
            ::HIR::Visitor::visit_struct(p, item);
        }

        void visit_union(::HIR::ItemPath p, ::HIR::Union& item) override {
            auto _ = this->ms.set_impl_generics(item.mParams);
            ::HIR::Visitor::visit_union(p, item);
        }

        void visit_function(::HIR::ItemPath p, ::HIR::Function& item) override {
            auto _ = this->ms.set_item_generics(item.mParams);
            ::HIR::Visitor::visit_function(p, item);
        }

        void visit_static(::HIR::ItemPath p, ::HIR::Static& item) override {
            //auto _ = this->m_ms.set_item_generics(item.m_params);
            ::HIR::Visitor::visit_static(p, item);
        }

        void visit_constant(::HIR::ItemPath p, ::HIR::Constant& item) override {
            auto _ = this->ms.set_item_generics(item.mParams);
            ::HIR::Visitor::visit_constant(p, item);
        }

        // Actual expressions
        void visit_expr(::HIR::ExprPtr& expr) override {
            struct ExprVisitor: public ::HIR::ExprVisitorDef {
                VisitorPost& upper_visitor;

                ExprVisitor(VisitorPost& uv)
                    : ::HIR::ExprVisitorDef(uv.interner())
                    , upper_visitor(uv)
                {
                }

                void visit_generic_path(::HIR::Visitor::PathContext pc, ::HIR::GenericPath& p) override {
                    upper_visitor.visit_generic_path(p, pc);
                }

                void visit_type(::HIR::TypeRef& ty) override {
                    upper_visitor.visit_type_inner(ty, true);
                }

                void visit_node_ptr(::HIR::ExprNodeP& nodePtr) override {
                    upper_visitor.visit_type(nodePtr->resType);
                    ::HIR::ExprVisitorDef::visit_node_ptr(nodePtr);
                }

                void visit(::HIR::ExprNodeLet& node) override {
                    upper_visitor.visit_type(node.mType);
                    upper_visitor.visit_pattern(node.pattern);
                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNodeMatch& node) override {
                    for (auto& arm : node.arms) {
                        for (auto& pat : arm.patterns) {
                            upper_visitor.visit_pattern(pat);
                        }
                        for (auto& g : arm.guards) {
                            upper_visitor.visit_pattern(g.pat);
                        }
                    }
                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNodePathValue& node) override {
                    upper_visitor.visit_path(node.mPath, ::HIR::Visitor::PathContext::VALUE);
                }

                void visit(::HIR::ExprNodeCallPath& node) override {
                    upper_visitor.visit_path(node.mPath, ::HIR::Visitor::PathContext::VALUE);
                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNodeCallMethod& node) override {
                    upper_visitor.visit_path_params(node.mParams);
                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNodeStructLiteral& node) override {
                    upper_visitor.visit_type_inner(node.mType, false);

                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNodeArraySized& node) override {
                    auto& as = node.mSize;
                    if (as.is_Unevaluated()) {
                        upper_visitor.visit_constgeneric(as.as_Unevaluated());
                    }
                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNodeClosure& node) override {
                    upper_visitor.visit_type(node.returnType);
                    for (auto& arg : node.mArgs) {
                        upper_visitor.visit_pattern(arg.first);
                        upper_visitor.visit_type(arg.second);
                    }
                    ::HIR::ExprVisitorDef::visit(node);
                }
            };

            for (auto& ty : expr.erasedTypes) {
                visit_type(ty);
            }

            // Local expression
            if (expr.get() != nullptr) {
                ExprVisitor v{*this};
                (*expr).visit(v);
            }
            // External expression (has MIR)
            else if (auto* mir = expr.getExtMirMut()) {
                for (auto& ty : mir->locals) {
                    this->visit_type(ty);
                }

                struct MirVisitor: public ::MIR::visit::VisitorMut {
                    VisitorPost& upper_visitor;

                    MirVisitor(VisitorPost& upper_visitor)
                        : upper_visitor(upper_visitor)
                    {
                    }

                    void visit_type(::HIR::TypeRef& t) override {
                        upper_visitor.visit_type(t);
                    }

                    void visit_path(::HIR::Path& p) override {
                        upper_visitor.visit_path(p, ::HIR::Visitor::PathContext::VALUE);
                    }

                    bool visit_lvalue(::MIR::LValue& lv, ::MIR::visit::ValUsage u) override {
                        if (lv.root.is_Static()) {
                            upper_visitor.visit_path(lv.root.as_Static(), ::HIR::Visitor::PathContext::VALUE);
                        }
                        return false;
                    }
                };

                MirVisitor mv(*this);
                for (auto& block : mir->blocks) {
                    for (auto& stmt : block.statements) {
                        mv.visit_stmt(stmt);
                    }
                    mv.visit_terminator(block.terminator);
                }
            } else {
            }
        }
    };
}

void ConvertHIRBind(::HIR::Crate& crate) {
    {
        BindVisitor exp{crate};
        // Also visit extern crates to update their pointers
        for (auto& ec : crate.extCrates) {
            exp.visit_crate(*ec.second.mData);
        }
        exp.visit_crate(crate);
    }

    {
        VisitorPost v{crate};
        for (auto& ec : crate.extCrates) {
            v.visit_crate(*ec.second.mData);
        }
        v.visit_crate(crate);
    }

    // Populate supertrait list
    VisitorEnumSuperTraits(crate).visit_crate(crate);
}


HIR::PathParams ConvertHIRCompleteAliasParams(HIR::TypeInterner& types, const Span& sp, const ::HIR::GenericParams& params_def, const ::HIR::GenericPath& path, bool isExpr) {
    auto pp = path.mParams.clone();

    // Empty list, fill with ivars
    if (isExpr && pp.types.empty()) {
        while (pp.types.size() < params_def.types.size()) {
            pp.types.push_back(types.infer());
        }
    }
    if (isExpr && pp.values.empty()) {
        pp.values.resize(params_def.values.size());
    }

    // Shouldn't this error out if not in an expression?
    if (pp.mLifetimes.empty()) {
        pp.mLifetimes.resize(params_def.mLifetimes.size());
    }
    if (pp.mLifetimes.size() != params_def.mLifetimes.size()) {
        ERROR(sp, E0000, "Mismatched lifetime-generic count in " << path
            << ", expected " << params_def.mLifetimes.size() << " got " << pp.mLifetimes.size());
    }

    pp.types.reserve(params_def.types.size());
    while (pp.types.size() < params_def.types.size() && params_def.types[pp.types.size()].defaultValue != ::HIR::TypeRef()) {
        auto monomorph = MonomorphStatePtr(types, nullptr, &pp, nullptr);
        pp.types.push_back(monomorph.monomorphType(sp, params_def.types[pp.types.size()].defaultValue));
    }
    if (pp.types.size() != params_def.types.size()) {
        ERROR(sp, E0000, "Mismatched type-generic count in " << path << ", expected " << params_def.types.size() << " got " << pp.types.size());
    }

    pp.values.reserve(params_def.values.size());
    while (pp.values.size() < params_def.values.size() && !params_def.values[pp.values.size()].defaultValue.is_Infer()) {
        auto monomorph = MonomorphStatePtr(types, nullptr, &pp, nullptr);
        pp.values.push_back(monomorph.monomorphConstgeneric(sp, params_def.values[pp.values.size()].defaultValue, false));
    }
    if (pp.values.size() != params_def.values.size()) {
        ERROR(sp, E0000, "Mismatched const-generic count in " << path << ", expected " << params_def.values.size() << " got " << pp.values.size());
    }

    return pp;
}

::HIR::TypeRef ConvertHIRExpandAliasesGetExpansionGP(const Span& sp, const ::HIR::Crate& crate, const ::HIR::GenericPath& path, bool isExpr) {
    const auto& ti = crate.getTypeitemByPath(sp, path.mPath);
    if (const auto* ep = ti.opt_TypeAlias()) {
        const auto& ta = *ep;
        DEBUG(path << " -> type " << ta.mParams.fmtArgs() << " = " << ta.mType);
        auto pp = ConvertHIRCompleteAliasParams(crate.types, sp, ta.mParams, path, isExpr);
        // Monomorphise the exapnded type using the created params
        auto ms = MonomorphStatePtr(crate.types, nullptr, &pp, nullptr);
        HIR::TypeRef rv = ms.monomorphType(sp, ta.mType);
        DEBUG(path << " -> " << path.mPath << pp << " -> " << rv);
        return rv;
    }
    return crate.types.infer();
}

::HIR::TypeRef ConvertHIRExpandAliasesGetExpansion(const ::HIR::Crate& crate, const ::HIR::Path& path, bool isExpr) {
    static Span sp;
    TU_MATCH(::HIR::Path::Data, (path.mData), (e), (Generic, return ConvertHIRExpandAliasesGetExpansionGP(sp, crate, e, isExpr);), (UfcsInherent, DEBUG("TODO: Locate impl blocks for types - path=" << path);), (UfcsKnown, DEBUG("TODO: Locate impl blocks for traits on types - path=" << path);), (UfcsUnknown, DEBUG("TODO: Locate impl blocks for traits on types - path=" << path);))
    return crate.types.infer();
}

std::vector<HIR::TraitPath> ConvertHIRExpandAliasesGetTraitExpansionGP(const Span& sp, const ::HIR::Crate& crate, const HIR::GenericPath& path, bool isExpr) {
    const auto& ti = crate.getTypeitemByPath(sp, path.mPath);
    if (const auto* ep = ti.opt_TraitAlias()) {
        const auto& ta = *ep;
        auto pp = ConvertHIRCompleteAliasParams(crate.types, sp, ta.mParams, path, isExpr);
        auto ms = MonomorphStatePtr(crate.types, nullptr, &pp, nullptr);
        std::vector<HIR::TraitPath> rv;
        rv.reserve(ta.traits.size());
        for (const auto& exp : ta.traits) {
            rv.push_back(ms.monomorphTraitpath(sp, exp, false));
        }
        DEBUG(path << "\n -> " << path.mPath << pp << "\n -> {" << rv << "}");
        return rv;
    } else {
        return std::vector<HIR::TraitPath>();
    }
}

std::vector<HIR::TraitPath> ConvertHIRExpandAliasesGetTraitExpansion(const Span& sp, const ::HIR::Crate& crate, /*const*/ HIR::TraitPath& path, bool isExpr) {
    auto rv = ConvertHIRExpandAliasesGetTraitExpansionGP(sp, crate, path.mPath, isExpr);
    if (!rv.empty()) {
        if (!path.traitBounds.empty() || !path.typeBounds.empty()) {
            struct H {
                static bool containsTrait(const Span& sp, const HIR::Crate& crate, const HIR::GenericPath& path, const HIR::GenericPath& desPath) {
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
                        BUG(sp, "Not a trait path " << path << ": " << ti.tag_str());
                    }
                    return false;
                }

                static HIR::TraitPath& findEntry(const Span& sp, const HIR::Crate& crate, const HIR::GenericPath& desPath, ::std::vector<::HIR::TraitPath>& rv) {
                    for (auto& p : rv) {
                        if (containsTrait(sp, crate, p.mPath, desPath)) {
                            return p;
                        }
                    }
                    BUG(sp, "Unable to find a trait in expansion list for " << desPath);
                }
            };

            for (auto& tb : path.traitBounds) {
                auto& e = H::findEntry(sp, crate, tb.second.source_trait, rv);
                e.traitBounds.insert(std::make_pair(tb.first, std::move(tb.second)));
            }
            for (auto& tb : path.typeBounds) {
                auto& e = H::findEntry(sp, crate, tb.second.source_trait, rv);
                e.typeBounds.insert(std::make_pair(tb.first, std::move(tb.second)));
            }
        }
    }
    return rv;
}

class Expander: public ::HIR::Visitor {
    const ::HIR::Crate& crate;
    bool inExpr = false;
    const ::HIR::TypeData* implType = nullptr;

public:
    Expander(const ::HIR::Crate& crate)
        : ::HIR::Visitor(nullptr, crate.types)
        , crate(crate)
    {
    }

    HIR::TypeInterner& interner() const { return crate.types; }

    void expandTraitList(const Span& sp, ::std::vector<HIR::TraitPath>& list) {
        for (auto it = list.begin(); it != list.end(); ++it) {
            auto n = ConvertHIRExpandAliasesGetTraitExpansion(sp, crate, *it, inExpr);
            if (!n.empty()) {
                it = list.erase(it);
                it = list.insert(it, std::make_move_iterator(n.begin()), std::make_move_iterator(n.end()));
                --it;
            }
        }
    }

    void visit_type(::HIR::TypeRef& ty) override {
        static Span sp;

        if (ty->is_ErasedType() || ty->is_TraitObject()) {
            auto data = ty->cloneData();
            if (auto* e = data.opt_ErasedType()) {
                expandTraitList(sp, e->traits);
            } else if (auto* e = data.opt_TraitObject(); e->mTrait.mPath != HIR::SimplePath()) {
                auto n = ConvertHIRExpandAliasesGetTraitExpansion(sp, crate, e->mTrait, inExpr);
                if (n.size() > 0) {
                    TODO(sp, "Expand trait alias in TraitObject? (markers only) - " << e->mTrait);
                }
            }
            ty = crate.types.intern(std::move(data));
        }

        ::HIR::Visitor::visit_type(ty);

        if (const auto* e = ty->opt_Path()) {
            ::HIR::TypeRef new_type = ConvertHIRExpandAliasesGetExpansion(crate, e->path, inExpr);
            // Keep trying to expand down the chain
            unsigned int numExp = 1;
            const unsigned int MAX_RECURSIVE_TYPE_EXPANSIONS = 100;
            while (numExp < MAX_RECURSIVE_TYPE_EXPANSIONS) {
                // NOTE: inner recurses
                ::HIR::Visitor::visit_type(new_type);
                if (const auto* e = new_type->opt_Path()) {
                    auto nt = ConvertHIRExpandAliasesGetExpansion(crate, e->path, inExpr);
                    if (nt->is_Infer()) {
                        break;
                    }
                    numExp++;
                    new_type = mv$(nt);
                } else {
                    break;
                }
            }
            ASSERT_BUG(sp, numExp < MAX_RECURSIVE_TYPE_EXPANSIONS, "Recursion limit hit expanding " << ty << " (currently on " << new_type << ")");
            if (!new_type->is_Infer()) {
                DEBUG("Replacing " << ty << " with " << new_type << " (" << numExp << " expansions)");
                ty = mv$(new_type);
            }
        }
    }

    void visit_trait_path(::HIR::TraitPath& tp) override {
        static Span sp;
        // 1. Make sure that the trait path isn't pointing at an alias (should have been handled by the caller, which can expand to multiple items)
        ASSERT_BUG(sp, crate.getTypeitemByPath(sp, tp.mPath.mPath).is_Trait(), "Bad trait path - " << tp.mPath << " : " << crate.getTypeitemByPath(sp, tp.mPath.mPath).tag_str());
        // 2. Handle AtyBounds
        for (auto& tb : tp.traitBounds) {
            expandTraitList(sp, tb.second.traits);
        }

        // Finally. Recurse
        ::HIR::Visitor::visit_trait_path(tp);
    }

    ::HIR::Path expandAliasPath(const Span& sp, const ::HIR::Path& path) {
        const unsigned int MAX_RECURSIVE_TYPE_EXPANSIONS = 100;

        // If the path is already generic and points at an enum variant, skip
        if (path.mData.is_Generic()) {
            const auto& gp = path.mData.as_Generic();
            if (gp.mPath.components().size() > 1 && crate.getTypeitemByPath(sp, gp.mPath, /*igncrate*/ false, /*ignlast*/ true).is_Enum()) {
                return ::HIR::GenericPath();
            }
        }

        ::HIR::Path rv = ::HIR::GenericPath();
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

            this->visit_path(rv, ::HIR::Visitor::PathContext::TYPE);

            cur = &rv;
        } while (++numExp < MAX_RECURSIVE_TYPE_EXPANSIONS);
        ASSERT_BUG(sp, numExp < MAX_RECURSIVE_TYPE_EXPANSIONS, "Recursion limit expanding " << path << " (currently on " << *cur << ")");
        return mv$(rv);
    }

    ::HIR::Pattern::PathBinding visitPatternPathBinding(const Span& sp, ::HIR::Path& path) {
        auto resize_type_params = [&](::HIR::PathParams& params, size_t size) {
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

            const HIR::GenericPath* gpP;
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
                    return ::HIR::Pattern::PathBinding();
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
                ERROR(sp, E0000, "Expeted enum path in pattern binding, got " << ti.tag_str());
            }
            const auto& enm = ti.as_Enum();

            auto gp2 = gp.clone();
            gp2.mPath += name;
            gp2.mParams.mLifetimes.resize(enm.mParams.mLifetimes.size());
            resize_type_params(gp2.mParams, enm.mParams.types.size());
            gp2.mParams.values.resize(enm.mParams.values.size());

            auto idx = enm.findVariant(name);
            if (idx == ~0u) {
                TODO(sp, "Variant " << name << " not found in " << gp);
            }
            path = std::move(gp2);
            return ::HIR::Pattern::PathBinding::make_Enum({&enm, static_cast<unsigned>(idx)});
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
                resize_type_params(gp.mParams, enm.mParams.types.size());
                gp.mParams.values.resize(enm.mParams.values.size());

                auto idx = ti.as_Enum().findVariant(gp.mPath.components().back());
                return ::HIR::Pattern::PathBinding::make_Enum({&enm, static_cast<unsigned>(idx)});
            }
        }

        const auto& ti = crate.getTypeitemByPath(sp, gp.mPath);
        if (ti.is_Union()) {
            const auto& unn = ti.as_Union();

            gp.mParams.mLifetimes.resize(unn.mParams.mLifetimes.size());
            resize_type_params(gp.mParams, unn.mParams.types.size());
            gp.mParams.values.resize(unn.mParams.values.size());

            return ::HIR::Pattern::PathBinding::make_Union(&unn);
        }

        ASSERT_BUG(sp, ti.is_Struct(), "Pattern path " << gp.mPath << " didn't point to a struct or union (" << ti.tag_str() << ")");
        const auto& str = ti.as_Struct();

        gp.mParams.mLifetimes.resize(str.mParams.mLifetimes.size());
        resize_type_params(gp.mParams, str.mParams.types.size());
        gp.mParams.values.resize(str.mParams.values.size());

        return ::HIR::Pattern::PathBinding::make_Struct(&str);
    }

    void visit_pattern(::HIR::Pattern& pat) override {
        static Span sp;

        ::HIR::Visitor::visit_pattern(pat);

        TU_MATCH_HDRA( (pat.mData), {)
        default:
            break;
            TU_ARMA(PathValue, e) {
                auto newPath = expandAliasPath(sp, e.path);
                if (newPath != ::HIR::GenericPath()) {
                    DEBUG("Replacing " << e.path << " with " << newPath);
                    e.path = mv$(newPath);
                }
                e.binding = visitPatternPathBinding(sp, e.path);
            }
            TU_ARMA(PathTuple, e) {
                auto newPath = expandAliasPath(sp, e.path);
                if (newPath != ::HIR::GenericPath()) {
                    DEBUG("Replacing " << e.path << " with " << newPath);
                    e.path = mv$(newPath);
                }
                e.binding = visitPatternPathBinding(sp, e.path);
            }
            TU_ARMA(PathNamed, e) {
                auto newPath = expandAliasPath(sp, e.path);
                if (newPath != ::HIR::GenericPath()) {
                    DEBUG("Replacing " << e.path << " with " << newPath);
                    e.path = mv$(newPath);
                }
                e.binding = visitPatternPathBinding(sp, e.path);
                // TODO: If this is an empty/wildcard AND it's poiting at a value/tuple entry, change to PathValue/PathTuple
            }
        }
    }

    void visit_params(::HIR::GenericParams& params) override {
        for (auto it = params.bounds.begin(); it != params.bounds.end(); ++it) {
            static Span sp;
            if (auto* be = it->opt_TraitBound()) {
                auto n = ConvertHIRExpandAliasesGetTraitExpansion(sp, crate, be->trait, inExpr);
                if (!n.empty()) {
                    auto origType = std::move(be->type);
                    auto origHrtbs = std::move(be->hrtbs);
                    if (origHrtbs) {
                        visit_params(*origHrtbs);
                    }
                    visit_type(origType);

                    it = params.bounds.erase(it);
                    for (auto& t : n) {
                        auto type = origType;
                        auto hrtbs = origHrtbs ? (&t == &n.back() ? std::move(origHrtbs) : box$(origHrtbs->clone())) : nullptr;
                        it = params.bounds.insert(it, HIR::GenericBound::make_TraitBound({std::move(hrtbs), std::move(type), std::move(t)}));
                    }
                }
            }
        }
        ::HIR::Visitor::visit_params(params);
    }

    void visit_expr(::HIR::ExprPtr& expr) override {
        struct Visitor: public ::HIR::ExprVisitorDef {
            Expander& upper_visitor;

            Visitor(Expander& uv)
                : ::HIR::ExprVisitorDef(uv.interner())
                , upper_visitor(uv)
            {
            }

            void visit_type(::HIR::TypeRef& ty) override {
                upper_visitor.visit_type(ty);
            }

            void visit_pattern(const Span& sp, ::HIR::Pattern& pat) override {
                upper_visitor.visit_pattern(pat);
            }

            // Custom impl to visit the inner expression
            void visit(::HIR::ExprNodeArraySized& node) override {
                auto& as = node.mSize;
                if (as.is_Unevaluated() && as.as_Unevaluated().is_Unevaluated()) {
                    upper_visitor.visit_expr(*as.as_Unevaluated().as_Unevaluated()->expr);
                }
                ::HIR::ExprVisitorDef::visit(node);
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

    void visit_trait_alias(::HIR::ItemPath p, ::HIR::TraitAlias& item) override {
        //Span    sp(p);
        expandTraitList(Span(), item.traits);
        ::HIR::Visitor::visit_trait_alias(p, item);
    }

    void visit_trait(::HIR::ItemPath p, ::HIR::Trait& item) override {
        //Span    sp(p);
        expandTraitList(Span(), item.parentTraits);
        ::HIR::Visitor::visit_trait(p, item);
    }

    void visit_associatedtype(::HIR::ItemPath p, ::HIR::AssociatedType& item) override {
        //Span    sp(p);
        expandTraitList(Span(), item.traitBounds);
        ::HIR::Visitor::visit_associatedtype(p, item);
    }

    void visit_type_impl(::HIR::TypeImpl& impl) override {
        implType = impl.mType;
        ::HIR::Visitor::visit_type_impl(impl);
        implType = nullptr;
    }

    void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
        static Span sp;
        implType = impl.mType;
        ::HIR::Visitor::visit_trait_impl(trait_path, impl);
        implType = nullptr;
    }

    void visit_function(HIR::ItemPath p, ::HIR::Function& item) override {
        ::HIR::Visitor::visit_function(p, item);
        if (item.receiver == HIR::Function::Receiver::Custom) {
            //DEBUG("Updating reciever from " << item.m_receiver_type << " to " << item.m_args.at(0).second);
            //item.m_receiver_type = item.m_args.at(0).second.clone();
            ASSERT_BUG(Span(), item.receiverType, "Custom receiver without a receiver type");
            this->visit_type(*item.receiverType);
        }
    }
};

class ExpanderSelf: public ::HIR::Visitor {
    const ::HIR::Crate& crate;
    const ::HIR::TypeData* implType = nullptr;
    bool inExpr = false;

public:
    ExpanderSelf(const ::HIR::Crate& crate, const ::HIR::TypeData* impl_type = nullptr)
        : ::HIR::Visitor(nullptr, crate.types)
        , crate(crate)
        , implType(impl_type)
    {
    }


    HIR::TypeInterner& interner() const { return crate.types; }

    void visit_type(::HIR::TypeRef& ty) override {
        ::HIR::Visitor::visit_type(ty);

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

    void visit_expr(::HIR::ExprPtr& expr) override {
        struct Visitor: public ::HIR::ExprVisitorDef {
            ExpanderSelf& upper_visitor;

            Visitor(ExpanderSelf& uv)
                : ::HIR::ExprVisitorDef(uv.interner())
                , upper_visitor(uv)
            {
            }

            void visit_type(::HIR::TypeRef& ty) override {
                upper_visitor.visit_type(ty);
            }

            void visit_pattern(const Span& sp, ::HIR::Pattern& pat) override {
                upper_visitor.visit_pattern(pat);
            }

            // Custom impl to visit the inner expression
            void visit(::HIR::ExprNodeArraySized& node) override {
                auto& as = node.mSize;
                if (as.is_Unevaluated() && as.as_Unevaluated().is_Unevaluated()) {
                    upper_visitor.visit_expr(*as.as_Unevaluated().as_Unevaluated()->expr);
                }
                ::HIR::ExprVisitorDef::visit(node);
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

    void visit_enum(HIR::ItemPath p, ::HIR::Enum& enm) override {
        HIR::TypeRef ty = crate.types.path(HIR::GenericPath(p.getSimplePath(), enm.mParams.makeNopParams(crate.types, 0)), &enm);
        implType = ty;
        ::HIR::Visitor::visit_enum(p, enm);
        implType = nullptr;
    }

    void visit_struct(HIR::ItemPath p, ::HIR::Struct& str) override {
        HIR::TypeRef ty = crate.types.path(HIR::GenericPath(p.getSimplePath(), str.mParams.makeNopParams(crate.types, 0)), &str);
        // HACK: If thre is a `#` in the path, it's en enum variant
        if (const auto* n = ::std::strchr(p.name, '#')) {
            if (n != p.name && n[1]) {
                auto path = p.getSimplePath();
                path.update_last_component(RcString::newInterned(p.name, n - p.name));
                const auto& enm = crate.getEnumByPath(Span(), path);
                ty = crate.types.path(HIR::GenericPath(std::move(path), str.mParams.makeNopParams(crate.types, 0)), &enm);
            }
        }
        implType = ty;
        ::HIR::Visitor::visit_struct(p, str);
        implType = nullptr;
    }

    void visit_union(HIR::ItemPath p, ::HIR::Union& unn) override {
        HIR::TypeRef ty = crate.types.path(HIR::GenericPath(p.getSimplePath(), unn.mParams.makeNopParams(crate.types, 0)), &unn);
        implType = ty;
        ::HIR::Visitor::visit_union(p, unn);
        implType = nullptr;
    }

    void visit_type_impl(::HIR::TypeImpl& impl) override {
        implType = impl.mType;
        ::HIR::Visitor::visit_type_impl(impl);
        implType = nullptr;
    }

    void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
        static Span sp;
        implType = impl.mType;
        ::HIR::Visitor::visit_trait_impl(trait_path, impl);
        implType = nullptr;
    }
};

void ConvertHIRExpandAliases(::HIR::Crate& crate) {
    Expander exp{crate};
    exp.visit_crate(crate);
}

void ConvertHIRExpandAliasesSelf(::HIR::Crate& crate) {
    ExpanderSelf exp{crate};
    exp.visit_crate(crate);
}

void ConvertHIRExpandAliasesSelfExpr(
    const ::HIR::Crate& crate,
    const ::HIR::TypeData* impl_type,
    ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>>& args,
    ::HIR::TypeRef& ret_ty,
    ::HIR::ExprPtr& expr
    )
{
    ExpanderSelf exp{crate, impl_type};
    for (auto& arg : args) {
        exp.visit_pattern(arg.first);
        exp.visit_type(arg.second);
    }
    exp.visit_type(ret_ty);
    exp.visit_expr(expr);
}


namespace {
    /// <summary>
    /// A class that acts like StaticTraitResolve, but only holds params
    /// </summary>
    struct MiniResolve {
        const HIR::Crate& crate;
        const HIR::TypeData* selfType = nullptr;
        const HIR::GenericParams* implGenerics = nullptr;
        const HIR::GenericParams* itemGenerics = nullptr;

        MiniResolve(const HIR::Crate& crate)
            : crate(crate)
        {
        }

        NullOnDrop<const ::HIR::GenericParams> set_impl_generics(const ::HIR::GenericParams& gps) {
            implGenerics = &gps;
            return NullOnDrop<const ::HIR::GenericParams>(implGenerics);
        }

        NullOnDrop<const ::HIR::GenericParams> set_item_generics(const ::HIR::GenericParams& gps) {
            itemGenerics = &gps;
            return NullOnDrop<const ::HIR::GenericParams>(itemGenerics);
        }
    };

    class LifetimeVisitor: public ::HIR::Visitor {
        ::HIR::Crate& crate;
        MiniResolve mResolve;

        bool inExpr = false;
        bool createElided = false;
        ::HIR::GenericParams* curParams = nullptr;
        unsigned curParamsLevel = 0;
        ::std::vector<const ::HIR::LifetimeRef*> currentLifetime;
        /// The type of `Self` if we're in a by-value method
        const ::HIR::TypeData* valueSelfType = nullptr;

        unsigned currentDepth = 0;
        std::vector<std::pair<unsigned, const ::HIR::LifetimeRef*>> traitObjectRule;

    public:
        LifetimeVisitor(::HIR::Crate& crate)
            : HIR::Visitor(nullptr, crate.types)
            , crate(crate)
            , mResolve(crate)
        {
        }

    private:
        struct SavedParams {
            LifetimeVisitor* parent;
            bool createElided;
            ::HIR::GenericParams* curParams;
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

        SavedParams save_params() {
            return SavedParams(*this);
        }

        void set_params(::HIR::GenericParams* params, unsigned level) {
            createElided = true;
            curParams = params;
            curParamsLevel = level;
        }

        SavedParams push_params(::HIR::GenericParams& params, unsigned level) {
            auto rv = save_params();
            set_params(&params, level);
            return rv;
        }

        SavedParams push_params(::HIR::GenericParams* params, unsigned level) {
            auto rv = save_params();
            set_params(params, level);
            return rv;
        }

        struct ExplicitInputLifetimeCollector: public HIR::Visitor {
            std::vector<HIR::LifetimeRef> lifetimes;
            unsigned int nestedHrtbDepth = 0;
            bool onlyCreated = false;
            unsigned int createdLevel = 0;
            unsigned int firstCreatedIndex = 0;

            ExplicitInputLifetimeCollector(HIR::TypeInterner& types)
                : HIR::Visitor(nullptr, types)
            {
            }

            ExplicitInputLifetimeCollector(HIR::TypeInterner& types, unsigned int level, unsigned int firstIndex)
                : HIR::Visitor(nullptr, types)
                , onlyCreated(true)
                , createdLevel(level)
                , firstCreatedIndex(firstIndex)
            {
            }

            void addLifetime(const HIR::LifetimeRef& lft) {
                // Unknown/inferred input lifetimes are counted after the main
                // visitor creates their binder entries.
                if (lft.binding == HIR::LifetimeRef::UNKNOWN || lft.binding == HIR::LifetimeRef::INFER) {
                    return;
                }
                if (nestedHrtbDepth != 0 && lft.isHrl()) {
                    return;
                }
                if (onlyCreated && (!lft.isParam()
                        || (lft.binding >> 8) != createdLevel
                        || (lft.binding & 0xFF) < firstCreatedIndex)) {
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

            void visit_path_params(HIR::PathParams& pp) override {
                for (const auto& lft : pp.mLifetimes) {
                    addLifetime(lft);
                }
                HIR::Visitor::visit_path_params(pp);
            }

            void visit_trait_path(HIR::TraitPath& trait) override {
                const bool nestedHrtb = trait.hrtbs && !trait.hrtbs->mLifetimes.empty();
                nestedHrtbDepth += nestedHrtb;
                HIR::Visitor::visit_trait_path(trait);
                nestedHrtbDepth -= nestedHrtb;
            }

            void visit_type(HIR::TypeRef& ty) override {
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
                HIR::Visitor::visit_type(ty);
            }
        };

        struct InputLifetimeSelection {
            HIR::LifetimeRef lifetime;
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

            void set_self(const ExplicitInputLifetimeCollector& self_lifetimes) {
                lifetime = HIR::LifetimeRef();
                hasLifetime = false;
                ambiguous = self_lifetimes.lifetimes.size() > 1;
                fromSelf = !self_lifetimes.lifetimes.empty();
                if (self_lifetimes.lifetimes.size() == 1) {
                    lifetime = self_lifetimes.lifetimes.front();
                    hasLifetime = true;
                }
            }

            HIR::LifetimeRef get() const {
                return hasLifetime && !ambiguous ? lifetime : HIR::LifetimeRef();
            }
        };

        struct SelfLifetimeCollector: public HIR::Visitor {
            HIR::TypeInterner& types;
            const HIR::TypeData* implSelf;
            ExplicitInputLifetimeCollector lifetimes;

            SelfLifetimeCollector(HIR::TypeInterner& types, const HIR::TypeData* implSelf)
                : HIR::Visitor(nullptr, types)
                , types(types)
                , implSelf(implSelf)
                , lifetimes(types)
            {
            }

            bool containsSelf(HIR::TypeRef ty) {
                struct SelfFinder: public HIR::Visitor {
                    const HIR::TypeData* implSelf;
                    bool found = false;

                    SelfFinder(HIR::TypeInterner& types, const HIR::TypeData* implSelf)
                        : HIR::Visitor(nullptr, types)
                        , implSelf(implSelf)
                    {
                    }

                    void visit_type(HIR::TypeRef& ty) override {
                        if (ty == implSelf || (ty->is_Generic() && ty->as_Generic().binding == GENERICSelf)) {
                            found = true;
                            return;
                        }
                        HIR::Visitor::visit_type(ty);
                    }
                } finder(types, implSelf);
                finder.visit_type(ty);
                return finder.found;
            }

            void visit_type(HIR::TypeRef& ty) override {
                if (const auto* borrow = ty->opt_Borrow()) {
                    if (containsSelf(borrow->inner)) {
                        lifetimes.addLifetime(borrow->lifetime);
                    }
                }
                HIR::Visitor::visit_type(ty);
            }
        };

    public:
        void visit_lifetime(const Span& sp, HIR::LifetimeRef& lft) {
            if (!lft.isParam()) {
                switch (lft.binding) {
                    case HIR::LifetimeRef::STATIC: // 'static
                        break;
                    case HIR::LifetimeRef::INFER: // '_
                        //TODO(sp, "Handle explicitly elided lifetimes");
                        //break;
                    case HIR::LifetimeRef::UNKNOWN: // <none>
                        // If there's a current liftime (i.e. we're within a borrow), then use that
                        if (!currentLifetime.empty() && currentLifetime.back()) {
                            lft = *currentLifetime.back();
                            DEBUG("Use stack: " << lft);
                        }
                        // Otherwise, try to make a new one
                        else if (curParams && createElided) {
                            auto idx = curParams->mLifetimes.size();
                            curParams->mLifetimes.push_back(HIR::LifetimeDef{RcString::newInterned(FMT("elided#" << idx))});
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
                        //DEBUG("maybe add " << lft << ": " << outer);
                        if (lft != outer && lft.asParam().group() < 2 // I.e. an impl or method param, not HRL or placeholder
                            && outer.asParam().group() < 2
                            // One of the two lifetimes must be from this block?
                            && (lft.asParam().group() == curParamsLevel || outer.asParam().group() == curParamsLevel)) {
                            // Add `'this: 'outer`
                            bool found = false;
                            // Only if not a duplicate
                            for (const auto& b : curParams->bounds) {
                                if (const auto* be = b.opt_Lifetime()) {
                                    if (be->test == lft && be->valid_for == outer) {
                                        found = true;
                                        break;
                                    }
                                }
                            }
                            if (!found) {
                                DEBUG("Push bound " << lft << ": " << outer);
                                curParams->bounds.push_back(::HIR::GenericBound::make_Lifetime({lft, outer}));
                            }
                        }
                    } else {
                        if (currentLifetime.empty()) {
                        } else if (currentLifetime.back()) {
                            //DEBUG("No bound " << lft << ": " << *m_current_lifetime.back());
                        } else {
                            //DEBUG("No bound " << lft << ": nullptr");
                        }
                    }
                }
            }
        }

        bool boundExists(const HIR::LifetimeRef& test, const HIR::LifetimeRef& valid_for) const {
            if (mResolve.implGenerics) {
                for (const auto& b : mResolve.implGenerics->bounds) {
                    if (b.is_Lifetime()) {
                        DEBUG(b);
                    }
                    if (b.is_Lifetime() && b.as_Lifetime().test == test && b.as_Lifetime().valid_for == valid_for) {
                        return true;
                    }
                }
            }
            if (mResolve.itemGenerics) {
                for (const auto& b : mResolve.itemGenerics->bounds) {
                    if (b.is_Lifetime()) {
                        DEBUG(b);
                    }
                    if (b.is_Lifetime() && b.as_Lifetime().test == test && b.as_Lifetime().valid_for == valid_for) {
                        return true;
                    }
                }
            }
            return false;
        }

        void visit_params(::HIR::GenericParams& params) override {
            TRACE_FUNCTION_F(params.fmtArgs() << params.fmtBounds());
            for (auto& tps : params.types) {
                this->visit_type(tps.defaultValue);
            }
            for (auto& val : params.values) {
                this->visit_type(val.mType);
            }
            // The bounds list can grow as inferred lifetime bounds are added, so iterate manually and move the bound in/out to maintain pointer stability
            for (size_t i = 0; i < params.bounds.size(); i++) {
                auto bound = std::move(params.bounds[i]);
                params.bounds[i] = HIR::GenericBound::make_Lifetime({HIR::LifetimeRef::new_static(), HIR::LifetimeRef::new_static()});
                visit_generic_bound(bound);
                params.bounds[i] = std::move(bound);
            }
        }

        void visit_generic_path(::HIR::GenericPath& p, ::HIR::Visitor::PathContext pc) override {
            const static Span sp;
            // Get the type definition and fill in omitted lifetimes
            const HIR::GenericParams* gp = nullptr;
            if (p.mPath.components().size() > 1) {
                if (const auto* e = mResolve.crate.getTypeitemByPath(sp, p.mPath, false, true).opt_Enum()) {
                    gp = &e->mParams;
                }
            }
            if (!gp) {
                switch (pc) {
                    case HIR::Visitor::PathContext::TYPE:
                    case HIR::Visitor::PathContext::TRAIT: {
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
                    case HIR::Visitor::PathContext::VALUE: {
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
            HIR::Visitor::visit_generic_path(p, pc);
        }

        void visit_path_params(::HIR::PathParams& pp) override {
            DEBUG(pp);
            static Span _sp;
            const Span& sp = _sp;

            for (auto& lft : pp.mLifetimes) {
                visit_lifetime(sp, lft);
            }

            HIR::Visitor::visit_path_params(pp);
        }

        void visit_type(::HIR::TypeRef& ty) override {
            static Span _sp;
            const Span& sp = _sp;

            auto saved_m_trait_object_rule = traitObjectRule.size();
            auto saved_liftime_depth = currentLifetime.size();
            auto saved_params = save_params();
            if (currentDepth == 0) {
                DEBUG("> " << ty);
            }
            currentDepth += 1;

            auto data = ty->cloneData();

            // Lifetime elision logic!

            if (auto* e = data.opt_Borrow()) {
                visit_lifetime(sp, e->lifetime);
                currentLifetime.push_back(&e->lifetime);
                traitObjectRule.push_back(::std::make_pair(currentDepth, &e->lifetime));
            }
            if (auto* e = data.opt_Function()) {
                currentLifetime.push_back(nullptr);
                set_params(&e->hrls, HIR::GENERICHrtb);
                auto saved_create = createElided;
                InputLifetimeSelection inputLifetimes;
                createElided = true;
                for (auto& t : e->argTypes) {
                    ExplicitInputLifetimeCollector parameter_lifetimes(crate.types);
                    parameter_lifetimes.visit_type(t);
                    const auto firstElidedLifetimeIdx = e->hrls.mLifetimes.size();
                    this->visit_type(t);
                    ExplicitInputLifetimeCollector createdLifetimes(
                        crate.types, HIR::GENERICHrtb, firstElidedLifetimeIdx);
                    createdLifetimes.visit_type(t);
                    parameter_lifetimes.merge(createdLifetimes);
                    inputLifetimes.addParameter(parameter_lifetimes);
                }
                createElided = false;
                HIR::LifetimeRef output_lifetime = inputLifetimes.get();
                if (output_lifetime != HIR::LifetimeRef()) {
                    currentLifetime.pop_back();
                    currentLifetime.push_back(&output_lifetime);
                }
                this->visit_type(e->mRettype);
                currentLifetime.pop_back();
                createElided = saved_create;
            }
            if (auto* e = data.opt_TraitObject()) {
                // TODO: Create? but what if it's not used?
                if (e->mTrait.hrtbs) {
                    currentLifetime.push_back(nullptr);
                    set_params(&*e->mTrait.hrtbs, HIR::GENERICHrtb);
                }

                // If neither of those rules apply, then the bounds on the trait are used:
                // - If the trait is defined with a single lifetime bound then that bound is used.
                // - If 'static is used for any lifetime bound then 'static is used.
                // - If the trait has no lifetime bounds, then the lifetime is inferred in expressions and is 'static outside of expressions.
                if (e->lifetime.binding == HIR::LifetimeRef::INFER || e->lifetime.binding == HIR::LifetimeRef::UNKNOWN) {
                    struct H {
                        const Span& sp;
                        const HIR::Crate& crate;
                        std::vector<HIR::LifetimeRef> lifetimes;

                        void visit_trait(const HIR::SimplePath& p, const HIR::PathParams& params) {
                            const auto& t = crate.getTraitByPath(sp, p);
                            DEBUG(p << " " << t.lifetime);
                            if (t.lifetime != HIR::LifetimeRef()) {
                                if (t.lifetime == HIR::LifetimeRef::new_static()) {
                                    lifetimes.push_back(t.lifetime);
                                    // Early return on 'static, no need to check anything else
                                    return;
                                } else {
                                    // TODO: Parameters
                                }
                            }
                            // TODO: Monomorph? (for lifetime parameters)
                            for (const auto& st : t.parentTraits) {
                                visit_trait(st.mPath.mPath, st.mPath.mParams);
                            }
                        }
                    } h{sp, mResolve.crate};

                    if (e->mTrait.mPath.mPath != HIR::SimplePath()) {
                        h.visit_trait(e->mTrait.mPath.mPath, e->mTrait.mPath.mParams);
                    }
                    std::sort(h.lifetimes.begin(), h.lifetimes.end());
                    auto newEnd = std::unique(h.lifetimes.begin(), h.lifetimes.end());
                    h.lifetimes.erase(newEnd, h.lifetimes.end());
                    if (h.lifetimes.empty()) {
                        // Apply normal elision rules?
                        DEBUG("TraitObject: No available bounds");
                    } else {
                        if (h.lifetimes.size() == 1 || h.lifetimes.back() == HIR::LifetimeRef::new_static()) {
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

                bool was_static_rule = false;
                // If the lifetime is omitted, or '_
                // ... AND this is within prototype (not in an expression)
                if (
                    (e->lifetime.binding == HIR::LifetimeRef::UNKNOWN /*|| e->m_lifetime.binding == HIR::LifetimeRef::INFER*/)
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
                                was_static_rule = (lft.binding == HIR::LifetimeRef::STATIC);
                                DEBUG("TraitObject: Set lifetime " << e->lifetime << " - trait object rule");
                            }
                        }
                    }
                }
                if (
                    (was_static_rule || e->lifetime.binding == HIR::LifetimeRef::UNKNOWN /*|| e->m_lifetime.binding == HIR::LifetimeRef::INFER*/) && !inExpr // Not in expression
                ) {
                    // HACK: If the trait has a lifeime param, use that
                    if (!e->mTrait.hrtbs && e->mTrait.mPath.mParams.mLifetimes.size() == 1) {
                        e->lifetime = e->mTrait.mPath.mParams.mLifetimes[0];
                        DEBUG("TraitObject: Set to first/only lifetime param of data trait: " << e->lifetime);
                    }
                }
                // If there is no available rule (i.e. not in a borrow), and the lifetime was omitted (not just '_), then fill in 'static
                if (false && traitObjectRule.empty() && e->lifetime.binding == HIR::LifetimeRef::UNKNOWN && !inExpr && !(createElided && curParams)) {
                    e->lifetime = HIR::LifetimeRef::new_static();
                    DEBUG("TraitObject: Set lifetime " << e->lifetime << " - hack");
                }
            }

            if (auto* e = data.opt_Path()) {
                // Expand default lifetime params
                if (auto* p = e->path.mData.opt_Generic()) {
                    const HIR::TypeItem& ti = mResolve.crate.getTypeitemByPath(sp, p->mPath);
                    const HIR::GenericParams* gp = nullptr;
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
                                visit_lifetime(sp, l);
                            }
                            // Then make a monomorph state, and find lifetime bounds
                            MonomorphStatePtr ms(crate.types, nullptr, &p->mParams, nullptr);
                            for (const auto& b : gp->bounds) {
                                TU_MATCH_HDRA((b), {)
                                TU_ARMA(Lifetime, be) {
                                        ASSERT_BUG(sp, be.test.isParam(), b);
                                        ASSERT_BUG(sp, be.valid_for.binding != HIR::LifetimeRef::UNKNOWN, b);
                                        curParams->bounds.push_back(HIR::GenericBound::make_Lifetime({ms.monomorphLifetime(sp, be.test), ms.monomorphLifetime(sp, be.valid_for)}));
                                        const auto& nbe = curParams->bounds.back().as_Lifetime();
                                        if (nbe.test.isParam()) {
                                            ASSERT_BUG(sp, nbe.test.isParam(), b << " -> " << curParams->bounds.back());
                                            ASSERT_BUG(sp, nbe.valid_for.binding != HIR::LifetimeRef::UNKNOWN, b << " -> " << curParams->bounds.back());
                                            if ((nbe.test.isParam() && nbe.test.asParam().group() == 3) || (nbe.valid_for.isParam() && nbe.valid_for.asParam().group() == 3)) {
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
                        static ::HIR::LifetimeRef static_lifetime = ::HIR::LifetimeRef::new_static();
                        traitObjectRule.push_back(std::make_pair(currentDepth, &static_lifetime));
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

            ::HIR::Visitor::visit_type_data(data);

            saved_params.restore();
            while (currentLifetime.size() > saved_liftime_depth) {
                currentLifetime.pop_back();
            }
            while (traitObjectRule.size() > saved_m_trait_object_rule) {
                traitObjectRule.pop_back();
            }
            currentDepth -= 1;

            {
                bool pushed = false;
                if (currentLifetime.empty() || !currentLifetime.back()) {
                    // Push `'static` (if not in expression mode AND; this is a trait object OR we're not in arguments)
                    if (!inExpr) {
                        static HIR::LifetimeRef static_lifetime = HIR::LifetimeRef::new_static();
                        if (!(curParams && createElided)) {
                            // In the return type, so we don't want to make a new parameter - push `'static`
                            currentLifetime.push_back(&static_lifetime);
                            pushed = true;
                        } else if (data.is_TraitObject() && data.as_TraitObject().lifetime == HIR::LifetimeRef()) {
                            // `dyn Foo` as vs `dyn Foo+'_`
                            currentLifetime.push_back(&static_lifetime);
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

                    visit_lifetime(sp, e->lifetime);
                    DEBUG("TraitObject: Final lifetime " << e->lifetime);
                }
                if (auto* e = data.opt_ErasedType()) {
                    // If in arguments, don't visit an omitted lifetime (so we don't add an elided lifetime for something that will be generic)
                    if ((!e->lifetimeBounds.empty() && e->lifetimeBounds.front().binding == HIR::LifetimeRef::UNKNOWN) && (curParams && createElided)) {
                    } else {
                        for (auto& lft : e->lifetimeBounds) {
                            visit_lifetime(sp, lft);
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
                        case ::HIR::TypeDataErasedType::Use::Present:
                            DEBUG("ErasedType use present");
                            break;
                        case ::HIR::TypeDataErasedType::Use::Omitted2024:
                            // Add all in-scope generics
                            DEBUG("ErasedType use omitted: 2024Edition");
                            if (mResolve.implGenerics) {
                                auto p = mResolve.implGenerics->makeNopParams(crate.types, 0);
                                for (const auto& l : p.mLifetimes) {
                                    DEBUG("2024: add " << l);
                                    e->use.mLifetimes.push_back(l);
                                }
                            }
                            if (mResolve.itemGenerics) {
                                auto p = mResolve.itemGenerics->makeNopParams(crate.types, 1);
                                for (const auto& l : p.mLifetimes) {
                                    DEBUG("2024: add " << l);
                                    e->use.mLifetimes.push_back(l);
                                }
                            }
                            break;
                        case ::HIR::TypeDataErasedType::Use::OmittedOld: {
                            DEBUG("ErasedType use omitted: Older Editions");

                            // If there is no lifetime assigned, then grab all mentioned lifetimes?
                            struct V: public HIR::Visitor {
                                std::set<HIR::LifetimeRef> lfts;

                                V(HIR::TypeInterner& types)
                                    : HIR::Visitor(nullptr, types)
                                {
                                }

                                void visit_path_params(HIR::PathParams& pp) override {
                                    for (auto& lft : pp.mLifetimes) {
                                        addLifetime(lft);
                                    }

                                    HIR::Visitor::visit_path_params(pp);
                                }

                                void addLifetime(const HIR::LifetimeRef& lft) {
                                    if (lft.isHrl()) {
                                        // HRL - ignore
                                        return;
                                    }
                                    this->lfts.insert(lft);
                                }

                                void visit_type(HIR::TypeRef& ty) override {
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
                                        if (tep->lifetimeBounds.size() == 1 && tep->lifetimeBounds.front().binding == HIR::LifetimeRef::UNKNOWN) {
                                            // Ignore unbound?
                                        } else {
                                            for (const auto& lft : tep->lifetimeBounds) {
                                                addLifetime(lft);
                                            }
                                        }
                                    }
                                    HIR::Visitor::visit_type(ty);
                                }
                            } v(crate.types);

                            // `data` is the lifetime-elided copy of `ty`.  Type nodes are
                            // immutable once interned, so walking `ty` here would inspect
                            // the pre-elision tree (and can reintroduce `'#omitted` from a
                            // parenthesised Fn bound).  Materialise the current copy before
                            // collecting the lifetimes captured by this opaque type.
                            auto elidedTy = crate.types.intern(data.cloneData());
                            v.visit_type(elidedTy);
                            // TODO: In 2024 edition, these rules change
                            // - Before: generics/lifetimes not mentioned in the `impl Foo` are omitted
                            // - After: All included
                            // - Both: Unless there's a `use<Foo>` present

                            if (v.lfts.empty() && !(!currentLifetime.empty() && currentLifetime.back() && !pushed)) {
                                // If this is on a by-value method, then assume it captures `self` (and thus all contained liftimes)
                                // REF: rustc-1.90.0-src/compiler/rustc_data_structures/src/graph/linked_graph/mod.rs:278
                                if (valueSelfType) {
                                    DEBUG("Check Self: " << valueSelfType);
                                    auto self_type = valueSelfType;
                                    v.visit_type(self_type);
                                }
                            }

                            // If there is a lifetime on the stack (that wasn't from a `'static` pushed above), then use it
                            if (v.lfts.empty() && !currentLifetime.empty() && currentLifetime.back() && !pushed) {
                                DEBUG("ErasedType: Use wrapping lifetime - " << *currentLifetime.back());
                                e->use.mLifetimes.push_back(*currentLifetime.back());
                            } else if (v.lfts.empty()) {
                                // No contained lifetimes, it's `'static`?
                                DEBUG("No inner lifetimes, will be `'static`");
                                e->use.mLifetimes.push_back(HIR::LifetimeRef::new_static());
                            } else if (v.lfts.size() == 1) {
                                // Easy, just assign this lifetime
                                DEBUG("ErasedType: Use contained lifetime " << *v.lfts.begin());
                                e->use.mLifetimes.push_back(*v.lfts.begin());
                            } else {
                                // If in arguments: Create a new input lifetime with a union of these lifetimes.
                                if (curParams && createElided) {
                                    e->use.mLifetimes.push_back(HIR::LifetimeRef(curParamsLevel * 256 + curParams->mLifetimes.size()));
                                    curParams->mLifetimes.push_back(HIR::LifetimeDef{});
                                    for (const auto& l : v.lfts) {
                                        curParams->bounds.push_back(HIR::GenericBound::make_Lifetime({e->use.mLifetimes[0], l}));
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
                        ASSERT_BUG(sp, l.binding != HIR::LifetimeRef::UNKNOWN, "Unbound lifetime? - " << l);
                    }

                    if (auto* ee = e->inner.opt_Alias()) {
                        if (ee->inner->path.crate_name() != mResolve.crate.crateName) {
                            // Should be impossible, as these are fully expanded by the time they reach HIR serialisation
                        } else {
                            ASSERT_BUG(Span(), mResolve.implGenerics, "No impl generics for type " << ty);
                            ee->inner->generics.mLifetimes = mResolve.implGenerics->mLifetimes;
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

        void visit_trait_path(::HIR::TraitPath& tp) override {
            const Span sp;
            TRACE_FUNCTION_FR(tp, tp);

            if (tp.lifetimeElision) {
                assert(tp.hrtbs);
                tp.lifetimeElision = false;
                currentLifetime.push_back(nullptr);
                const auto firstElidedLifetimeIdx = tp.hrtbs->mLifetimes.size();
                std::vector<std::vector<HIR::LifetimeRef>> explicitInputLifetimes;
                ASSERT_BUG(sp, tp.mPath.mParams.types.size() == 1
                    && tp.mPath.mParams.types.front()->is_Tuple(),
                    "Parenthesised Fn arguments aren't a tuple: " << tp.mPath);
                for (auto input : *tp.mPath.mParams.types.front()->opt_Tuple()) {
                    ExplicitInputLifetimeCollector parameter_lifetimes(crate.types);
                    parameter_lifetimes.visit_type(input);
                    explicitInputLifetimes.push_back(mv$(parameter_lifetimes.lifetimes));
                }

                // Visit the trait args (as inputs)
                auto saved_params = push_params(tp.hrtbs.get(), 3);

                this->visit_generic_path(tp.mPath, ::HIR::Visitor::PathContext::TYPE);
                DEBUG(tp.mPath);
                if (tp.hrtbs) {
                    DEBUG("for " << tp.hrtbs->fmtArgs());
                }

                InputLifetimeSelection inputLifetimes;
                auto& inputs = *tp.mPath.mParams.types.front()->opt_Tuple();
                ASSERT_BUG(sp, inputs.size() == explicitInputLifetimes.size(), "Fn argument tuple changed size");
                for (size_t i = 0; i < inputs.size(); i++) {
                    auto input = inputs[i];
                    ExplicitInputLifetimeCollector parameter_lifetimes(crate.types);
                    parameter_lifetimes.lifetimes = mv$(explicitInputLifetimes[i]);
                    ExplicitInputLifetimeCollector createdLifetimes(
                        crate.types, HIR::GENERICHrtb, firstElidedLifetimeIdx);
                    createdLifetimes.visit_type(input);
                    parameter_lifetimes.merge(createdLifetimes);
                    inputLifetimes.addParameter(parameter_lifetimes);
                }
                HIR::LifetimeRef lft = inputLifetimes.get();
                if (lft != HIR::LifetimeRef()) {
                    currentLifetime.push_back(&lft);
                    for (auto& assoc : tp.typeBounds) {
                        this->visit_generic_path(assoc.second.source_trait, ::HIR::Visitor::PathContext::TYPE);
                        this->visit_path_params(assoc.second.atyParams);
                        this->visit_type(assoc.second.type);
                    }
                    for (auto& assoc : tp.traitBounds) {
                        this->visit_generic_path(assoc.second.source_trait, ::HIR::Visitor::PathContext::TYPE);
                        this->visit_path_params(assoc.second.atyParams);
                        for (auto& trait : assoc.second.traits) {
                            this->visit_trait_path(trait);
                        }
                    }
                    currentLifetime.pop_back();
                }
                saved_params.restore();

                // Fix the source paths in ATYs
                const auto& trait = mResolve.crate.getTraitByPath(sp, tp.mPath.mPath);

                struct H {
                    const HIR::Crate& crate;

                    H(const HIR::Crate& crate)
                        : crate(crate)
                    {
                    }

                    bool enumSupertraits(const Span& sp, const HIR::Trait& tr, const HIR::GenericPath& tr_path, ::std::function<bool(HIR::GenericPath)> cb) {
                        const HIR::TypeRef self = crate.types.self();
                        MonomorphStatePtr ms(crate.types, self, &tr_path.mParams, nullptr);

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
                                if (pt.mPath.mPath == tr_path.mPath) {
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

                auto fixSource = [&](HIR::GenericPath& gp, const RcString& name) {
                    //fix_path(gp);
                    DEBUG("[fix_source] >> " << gp);
                    if (gp.equalsIgnoringRegions(tp.mPath)) {
                        gp = tp.mPath.clone();
                        return;
                    }
                    if (h.enumSupertraits(sp, trait, tp.mPath, [&](HIR::GenericPath m) {
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
                    fixSource(assoc.second.source_trait, assoc.first);
                }
                for (auto& assoc : tp.traitBounds) {
                    fixSource(assoc.second.source_trait, assoc.first);
                }

                if (lft != HIR::LifetimeRef()) {
                    currentLifetime.pop_back();
                    currentLifetime.push_back(&lft);
                }

                // Visit the rest (associated types mostly), using the output lifetime from above
                ::HIR::Visitor::visit_trait_path(tp);

                currentLifetime.pop_back();
            } else {
                ::HIR::Visitor::visit_trait_path(tp);
            }
        }

        void visit_expr(::HIR::ExprPtr& ep) override {
            struct EV: public HIR::ExprVisitorDef {
                LifetimeVisitor& parent;

                EV(LifetimeVisitor& parent)
                    : HIR::ExprVisitorDef(parent.crate.types)
                    , parent(parent)
                {
                }

                void visit_type(HIR::TypeRef& ty) {
                    parent.visit_type(ty);
                }
            } v{*this};

            auto s = inExpr;
            inExpr = true;
            if (ep) {
                ep->visit(v);
            }
            inExpr = s;
        }

        void visit_type_impl(::HIR::TypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.mType);
            mResolve.selfType = impl.mType;
            auto _ = mResolve.set_impl_generics(/*impl.m_type,*/ impl.mParams);

            // Pre-visit so lifetime elision can work
            {
                auto _ = push_params(impl.mParams, 0);
                this->visit_type(impl.mType);
            }

            ::HIR::Visitor::visit_type_impl(impl);
        }

        void visit_inherent_type(::HIR::ItemPath p, ::HIR::TypeAlias& item) override {
            auto _ = mResolve.set_item_generics(item.mParams);
            auto _2 = push_params(item.mParams, 1);
            ::HIR::Visitor::visit_inherent_type(p, item);
        }

        void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
            TRACE_FUNCTION_F("impl " << trait_path << impl.traitArgs << " for " << impl.mType);
            mResolve.selfType = impl.mType;
            auto _ = mResolve.set_impl_generics(/*impl.m_type,*/ impl.mParams);

            // Pre-visit so lifetime elision can work
            {
                auto _ = push_params(impl.mParams, 0);
                this->visit_type(impl.mType);
                this->visit_path_params(impl.traitArgs);
            }

            ::HIR::Visitor::visit_trait_impl(trait_path, impl);
        }

        void visit_marker_impl(const ::HIR::SimplePath& trait_path, ::HIR::MarkerImpl& impl) override {
            TRACE_FUNCTION_F("impl " << trait_path << impl.traitArgs << " for " << impl.mType << " { }");
            mResolve.selfType = impl.mType;
            auto _ = mResolve.set_impl_generics(/*impl.m_type,*/ impl.mParams);

            // Pre-visit so lifetime elision can work
            {
                auto _ = push_params(impl.mParams, 0);
                this->visit_type(impl.mType);
                this->visit_path_params(impl.traitArgs);
            }

            ::HIR::Visitor::visit_marker_impl(trait_path, impl);
        }

        void visit_type_alias(::HIR::ItemPath p, ::HIR::TypeAlias& item) override {
            auto _ = mResolve.set_impl_generics(/*impl.m_type,*/ item.mParams);
            ::HIR::Visitor::visit_type_alias(p, item);
        }

        void visit_trait(::HIR::ItemPath p, ::HIR::Trait& item) override {
            auto ty_self = crate.types.self();
            mResolve.selfType = ty_self;
            auto _ = mResolve.set_impl_generics(/*impl.m_type,*/ item.mParams);
            ::HIR::Visitor::visit_trait(p, item);
        }

        void visit_struct(::HIR::ItemPath p, ::HIR::Struct& item) override {
            auto _ = mResolve.set_impl_generics(/*item.m_struct_markings.dst_type,*/ item.mParams);
            auto _2 = push_params(item.mParams, 0);
            createElided = false;
            ::HIR::Visitor::visit_struct(p, item);
        }

        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override {
            auto _ = mResolve.set_impl_generics(/*MetadataType::None,*/ item.mParams);
            auto _2 = push_params(item.mParams, 0);
            createElided = false;
            ::HIR::Visitor::visit_enum(p, item);
        }

        void visit_union(::HIR::ItemPath p, ::HIR::Union& item) override {
            auto _ = mResolve.set_impl_generics(/*MetadataType::None,*/ item.mParams);
            auto _2 = push_params(item.mParams, 0);
            createElided = false;
            ::HIR::Visitor::visit_union(p, item);
        }

        void visit_constant(::HIR::ItemPath p, ::HIR::Constant& item) override {
            auto lft = HIR::LifetimeRef::new_static();
            currentLifetime.push_back(&lft);
            visit_type(item.mType);
            currentLifetime.pop_back(/*&lft*/);

            ::HIR::Visitor::visit_constant(p, item);
        }

        void visit_static(::HIR::ItemPath p, ::HIR::Static& item) override {
            auto lft = HIR::LifetimeRef::new_static();
            currentLifetime.push_back(&lft);
            visit_type(item.mType);
            currentLifetime.pop_back(/*&lft*/);

            ::HIR::Visitor::visit_static(p, item);
        }

        void visit_function(::HIR::ItemPath p, ::HIR::Function& item) override {
            TRACE_FUNCTION_F(p);
            auto _ = mResolve.set_item_generics(item.mParams);
            // NOTE: Superfluous... except that it makes the params valid for the return type.
            visit_params(item.mParams);

            // TODO: Add lifetime bounds from argument types!
            // - While visiting the argument types, find path types and inherit the lifetime bounds

            // Visit arguments to get the input lifetimes
            auto saved_params = push_params(item.mParams, 1);
            InputLifetimeSelection inputLifetimes;
            for (size_t i = 0; i < item.mArgs.size(); i++) {
                auto& arg = item.mArgs[i];
                TRACE_FUNCTION_FR("ARG " << arg, "ARG " << arg);
                ExplicitInputLifetimeCollector parameter_lifetimes(crate.types);
                parameter_lifetimes.visit_type(arg.second);
                const auto firstElidedLifetimeIdx = item.mParams.mLifetimes.size();
                visit_type(arg.second);
                ExplicitInputLifetimeCollector createdLifetimes(
                    crate.types, HIR::GENERICItem, firstElidedLifetimeIdx);
                createdLifetimes.visit_type(arg.second);
                parameter_lifetimes.merge(createdLifetimes);

                if (i == 0 && item.receiver != HIR::Function::Receiver::Free) {
                    SelfLifetimeCollector self_lifetimes(crate.types, mResolve.selfType);
                    self_lifetimes.visit_type(arg.second);
                    inputLifetimes.set_self(self_lifetimes.lifetimes);
                } else {
                    inputLifetimes.addParameter(parameter_lifetimes);
                }
            }
            createElided = false;

            // Get output lifetime
            HIR::LifetimeRef elidedOutputLifetime = inputLifetimes.get();
            if (item.receiver != HIR::Function::Receiver::Free) {
                if (item.receiver == HIR::Function::Receiver::Value) {
                    valueSelfType = mResolve.selfType;
                }
            }
            if (elidedOutputLifetime != HIR::LifetimeRef()) {
                DEBUG("Single input lifetime - " << elidedOutputLifetime);
            }
            // If present, set it (push to the stack)
            assert(currentLifetime.empty());
            if (elidedOutputLifetime != HIR::LifetimeRef()) {
                currentLifetime.push_back(&elidedOutputLifetime);
            }

            // Visit return type (populates path for `impl Trait` in return position
            {
                TRACE_FUNCTION_FR("RET " << item.returnType, "RET " << item.returnType);
                visit_type(item.returnType);
            }
            // - Unset params for the expression
            saved_params.restore();

            if (elidedOutputLifetime != HIR::LifetimeRef()) {
                currentLifetime.pop_back();
            }
            assert(currentLifetime.empty());

            DEBUG("Output: " << item.mParams.fmtArgs() << item.mParams.fmtBounds());
            valueSelfType = nullptr;

            ::HIR::Visitor::visit_function(p, item);
        }
    };
}

void ConvertHIRLifetimeElision(::HIR::Crate& crate) {
    LifetimeVisitor v{crate};
    v.visit_crate(crate);
}



namespace {

    class MarkingsVisitor: public ::HIR::Visitor {
        const ::HIR::Crate& crate;
        const ::HIR::SimplePath& mLangUnsize;
        const ::HIR::SimplePath& m_lang_CoerceUnsized;
        const ::HIR::SimplePath& mLangCopy;
        const ::HIR::SimplePath& mLangDeref;
        const ::HIR::SimplePath& mLangDrop;
        const ::HIR::SimplePath& mLangPhantomData;

    public:
        MarkingsVisitor(const ::HIR::Crate& crate)
            : ::HIR::Visitor(nullptr, crate.types)
            , crate(crate)
            , mLangUnsize(crate.getLangItemPathOpt("unsize"))
            , m_lang_CoerceUnsized(crate.getLangItemPathOpt("coerce_unsized"))
            , mLangCopy(crate.getLangItemPathOpt("copy"))
            , mLangDeref(crate.getLangItemPathOpt("deref"))
            , mLangDrop(crate.getLangItemPathOpt("drop"))
            , mLangPhantomData(crate.getLangItemPathOpt("phantom_data"))
        {
        }

        void visit_struct(::HIR::ItemPath ip, ::HIR::Struct& str) override {
            ::HIR::Visitor::visit_struct(ip, str);

            str.structMarkings.dst_type = getStructDstType(str, str.mParams, {});
            if (str.structMarkings.dst_type != ::HIR::StructMarkings::DstType::None) {
                str.structMarkings.unsized_field = (str.mData.is_Tuple() ? str.mData.as_Tuple().size() - 1 : str.mData.as_Named().size() - 1);
            }

            // Rules:
            // - A type parameter must be ?Sized
            // - That type parameter must only be used as part of the last field, and only once
            // - If the final field isn't the parameter, it must also impl Unsize

            // HACK: Just determine what ?Sized parameter is controlling the sized-ness
            if (str.structMarkings.dst_type == ::HIR::StructMarkings::DstType::Possible) {
                auto& lastFieldTy = (str.mData.is_Tuple() ? str.mData.as_Tuple().back().ent : str.mData.as_Named().back().ty);
                for (size_t i = 0; i < str.mParams.types.size(); i++) {
                    const auto& param = str.mParams.types[i];
                    auto ty = crate.types.generic(param.mName, i);
                    if (!param.isSized) {
                        if (visit_ty_with(lastFieldTy, [&](const auto& t) {
                            return t == ty;
                        })) {
                            ASSERT_BUG(Span(), str.structMarkings.unsized_param == ~0u, "Multiple unsized params to " << ip);
                            str.structMarkings.unsized_param = i;
                        }
                    }
                }
                ASSERT_BUG(Span(), str.structMarkings.unsized_param != ~0u, "No unsized param for type " << ip);
                str.structMarkings.canUnsize = true;
            }
        }

        ::HIR::StructMarkings::DstType getFieldDstType(const ::HIR::TypeData* ty, const ::HIR::GenericParams& innerDef, const ::HIR::GenericParams& params_def, const ::HIR::PathParams* params) {
            TRACE_FUNCTION_F("ty=" << ty);
            // If the type is generic, and the pointed-to parameters is ?Sized, record as needing unsize
            if (const auto* te = ty->opt_Generic()) {
                if (innerDef.types.at(te->binding).isSized == true) {
                    return ::HIR::StructMarkings::DstType::None;
                } else if (params) {
                    // Look at the param. Check for generic (use params_def), slice/traitobject, or path (no mono)
                    return getFieldDstType(params->types.at(te->binding), params_def, params_def, nullptr);
                } else {
                    return ::HIR::StructMarkings::DstType::Possible;
                }
            } else if (ty->is_Slice() || TU_TEST1((*ty), Primitive, == HIR::CoreType::Str)) {
                return ::HIR::StructMarkings::DstType::Slice;
            } else if (ty->is_TraitObject()) {
                return ::HIR::StructMarkings::DstType::TraitObject;
            } else if (const auto* te = ty->opt_Path()) {
                // If the type is a struct, check it (recursively)
                if (!te->path.mData.is_Generic()) {
                    // Associated type, TODO: Check this better.
                    return ::HIR::StructMarkings::DstType::None;
                } else if (te->binding.is_Struct()) {
                    const auto& params_tpl = te->path.mData.as_Generic().mParams;
                    if (params && monomorphisePathparamsNeeded(params_tpl)) {
                        static Span sp;
                        auto monomorphCb = MonomorphStatePtr(crate.types, nullptr, params, nullptr);
                        auto params_mono = monomorphCb.monomorphPathParams(sp, params_tpl, false);
                        return getStructDstType(*te->binding.as_Struct(), params_def, &params_mono);
                    } else {
                        return getStructDstType(*te->binding.as_Struct(), innerDef, &params_tpl);
                    }
                } else {
                    return ::HIR::StructMarkings::DstType::None;
                }
            } else {
                return ::HIR::StructMarkings::DstType::None;
            }
        }

        ::HIR::StructMarkings::DstType getStructDstType(const ::HIR::Struct& str, const ::HIR::GenericParams& def, const ::HIR::PathParams* params) {
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
        return ::HIR::StructMarkings::DstType::None;
        }

        void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
            static Span sp;

            ::HIR::Visitor::visit_trait_impl(trait_path, impl);

            if (impl.mType->is_Path()) {
                const auto& te = impl.mType->as_Path();
                const ::HIR::TraitMarkings* markingsPtr = te.binding.getTraitMarkings();
                if (markingsPtr) {
                    ::HIR::TraitMarkings& markings = *const_cast<::HIR::TraitMarkings*>(markingsPtr);
                    if (trait_path == mLangUnsize) {
                        DEBUG("Type " << impl.mType << " can Unsize");
                        ERROR(sp, E0000, "Unsize shouldn't be manually implemented");
                    } else if (trait_path == mLangDrop) {
                        // TODO: Check that there's only one impl, and that it covers the same set as the type.
                        markings.hasDropImpl = true;
                    } else if (trait_path == m_lang_CoerceUnsized) {
                        auto& struct_markings = const_cast<::HIR::Struct*>(te.binding.as_Struct())->structMarkings;
                        if (struct_markings.coerceUnsizedIndex != ~0u) {
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
                                        auto ty_l = monomorphCbL.monomorphType(sp, se[i].ent, false);
                                        auto ty_r = monomorphCbR.monomorphType(sp, se[i].ent, false);
                                        if (ty_l != ty_r) {
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
                                        auto ty_l = monomorphCbL.monomorphType(sp, se[i].ty, false);
                                        auto ty_r = monomorphCbR.monomorphType(sp, se[i].ty, false);
                                        if (ty_l != ty_r) {
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
                    struct_markings.coerceUnsizedIndex = field;
                    } else if (trait_path == mLangDeref) {
                        DEBUG("Type " << impl.mType << " can Deref");
                        markings.hasADeref = true;
                    } else if (trait_path == mLangCopy) {
                        DEBUG("Type " << impl.mType << " has a Copy impl");
                        markings.is_copy = true;
                    }
                    // TODO: Marker traits (with conditions)
                    else {
                    }
                }
            }
        }
    };

    class Visitor2: public ::HIR::Visitor {
    public:
        explicit Visitor2(::HIR::TypeInterner& types)
            : ::HIR::Visitor(nullptr, types)
        {
        }

        size_t getUnsizeParamIdx(const Span& sp, const ::HIR::TypeData* pointee) const {
            if (const auto* te = pointee->opt_Generic()) {
                return te->binding;
            } else if (const auto* te = pointee->opt_Path()) {
                ASSERT_BUG(sp, te->binding.is_Struct(), "Pointer to non-Unsize type - " << pointee);
                const auto& ism = te->binding.as_Struct()->structMarkings;
                ASSERT_BUG(sp, ism.unsized_param != ~0u, "Pointer to non-Unsize type - " << pointee);
                const auto& gp = te->path.mData.as_Generic();
                return getUnsizeParamIdx(sp, gp.mParams.types.at(ism.unsized_param));
            } else {
                BUG(sp, "Pointer to non-Unsize type? - " << pointee);
            }
        }

        ::HIR::StructMarkings::Coerce getCoerceType(const Span& sp, ::HIR::ItemPath ip, const ::HIR::Struct& str, size_t& outParamIdx) const {
            if (str.structMarkings.coerceUnsizedIndex == ~0u) {
                return ::HIR::StructMarkings::Coerce::None;
            }
            if (str.structMarkings.coerceUnsized != ::HIR::StructMarkings::Coerce::None) {
                outParamIdx = str.structMarkings.coerceParam;
                return str.structMarkings.coerceUnsized;
            }

            const ::HIR::TypeData* fieldTy = nullptr;
            TU_MATCHA((str.mData), (se), (Unit, ), (Tuple, fieldTy = se.at(str.structMarkings.coerceUnsizedIndex).ent;), (Named, fieldTy = se.at(str.structMarkings.coerceUnsizedIndex).ty;))
            assert(fieldTy);
        try_again:
            DEBUG("field_ty = " << fieldTy);

            if (const auto* te = fieldTy->opt_Path()) {
                ASSERT_BUG(sp, te->binding.is_Struct(), "CoerceUnsized impl differs on Path that isn't a struct - " << ip << " fld=" << fieldTy);
                const auto* istr = te->binding.as_Struct();
                const auto& gp = te->path.mData.as_Generic();

                size_t innerIdx = 0;
                auto innerType = getCoerceType(sp, {fieldTy}, *istr, innerIdx);
                ASSERT_BUG(sp, innerType != ::HIR::StructMarkings::Coerce::None, "CoerceUnsized impl differs on a non-CoerceUnsized type - " << ip << " fld=" << fieldTy);

                const auto& param_ty = gp.mParams.types.at(innerIdx);
                switch (innerType) {
                    case ::HIR::StructMarkings::Coerce::None:
                        throw "";
                    case ::HIR::StructMarkings::Coerce::Passthrough:
                        // Recurse on the generic type.
                        fieldTy = param_ty;
                        goto try_again;
                    case ::HIR::StructMarkings::Coerce::Pointer:
                        outParamIdx = getUnsizeParamIdx(sp, param_ty);
                        return ::HIR::StructMarkings::Coerce::Pointer;
                }
            } else if (const auto* te = fieldTy->opt_Generic()) {
                outParamIdx = te->binding;
                return ::HIR::StructMarkings::Coerce::Passthrough;
            } else if (const auto* te = fieldTy->opt_Pointer()) {
                outParamIdx = getUnsizeParamIdx(sp, te->inner);
                return ::HIR::StructMarkings::Coerce::Pointer;
            } else if (const auto* te = fieldTy->opt_Borrow()) {
                outParamIdx = getUnsizeParamIdx(sp, te->inner);
                return ::HIR::StructMarkings::Coerce::Pointer;
            } else {
                TODO(sp, "Handle CoerceUnsized type " << fieldTy);
            }
            BUG(sp, "Reached end of get_coerce_type - " << fieldTy);
        }

        void visit_struct(::HIR::ItemPath ip, ::HIR::Struct& str) override {
            static Span sp;

            auto& struct_markings = str.structMarkings;
            if (struct_markings.coerceUnsizedIndex == ~0u) {
                return;
            }

            size_t idx = 0;
            auto cut = getCoerceType(sp, ip, str, idx);
            struct_markings.coerceParam = idx;
            struct_markings.coerceUnsized = cut;
        }
    };

} // namespace

void ConvertHIRMarkings(::HIR::Crate& crate) {
    MarkingsVisitor exp{crate};
    exp.visit_crate(crate);

    // Visit again, visiting all structs and filling the coerce_unsized data
    Visitor2 exp2{crate.types};
    exp2.visit_crate(crate);
}


namespace resolve_ufcs {
    void expandTraitImplTypeDefaults(const ::HIR::Crate& crate, const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) {
        Span sp;
        const auto& trait = crate.getTraitByPath(sp, trait_path);
        auto ms = MonomorphStatePtr(crate.types, impl.mType, &impl.traitArgs, nullptr);

        while (impl.traitArgs.types.size() < trait.mParams.types.size()) {
            const auto& def = trait.mParams.types[impl.traitArgs.types.size()];
            auto ty = ms.monomorphType(sp, def.defaultValue);
            DEBUG("Add default trait arg " << ty << " from " << def.defaultValue);
            impl.traitArgs.types.push_back(mv$(ty));
        }
    }

    class UfcsVisitor: public ::HIR::Visitor {
        const ::HIR::Crate& crate;
        bool mVisitExprs;
        bool runEat;

        typedef ::std::vector<::std::pair<const ::HIR::SimplePath*, const ::HIR::Trait*>> t_trait_imports;
        t_trait_imports traits;

        StaticTraitResolve mResolve;
        bool mInTraitDef = false;
        const ::HIR::TypeData* mCurrentType = nullptr;
        const ::HIR::Trait* currentTrait = nullptr;
        const ::HIR::ItemPath* currentTraitPath = nullptr;
        bool inExpr = false;
        HIR::SimplePath curModPath;

    public:
        UfcsVisitor(const ::HIR::Crate& crate, bool visit_exprs)
            : ::HIR::Visitor(nullptr, crate.types)
            , crate(crate)
            , mVisitExprs(visit_exprs)
            , runEat(visit_exprs)
            , // Defaults to running when doing second-pass
            mResolve(crate)
        {
        }

        struct ModTraitsGuard {
            UfcsVisitor* v;
            t_trait_imports oldImports;

            ModTraitsGuard(UfcsVisitor& v, t_trait_imports oldImports)
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

        ModTraitsGuard push_mod_traits(HIR::SimplePath path, const ::HIR::Module& mod) {
            static Span sp;
            DEBUG("");
            ModTraitsGuard rv{*this, mv$(this->traits)};
            for (const auto& trait_path : mod.traits) {
                DEBUG("- " << trait_path);
                traits.push_back(::std::make_pair(&trait_path, &crate.getTraitByPath(sp, trait_path)));
            }
            curModPath = std::move(path);
            return rv;
        }

        void visit_module(::HIR::ItemPath p, ::HIR::Module& mod) override {
            auto _ = this->push_mod_traits(p.getSimplePath(), mod);
            ::HIR::Visitor::visit_module(p, mod);
        }

        void visit_params(::HIR::GenericParams& params) {
            TRACE_FUNCTION_F(params.fmtArgs() << params.fmtBounds());

            // Custom visitor to prevent running of EAT on type paramerter defaults
            auto saved_run_eat = runEat;
            runEat = false;
            for (auto& tps : params.types) {
                this->visit_type(tps.defaultValue);
            }
            runEat = saved_run_eat;

            for (auto& bound : params.bounds) {
                visit_generic_bound(bound);
            }

            // Re-populate the resolve index, as the above has changed them
            mResolve.prep_indexes(Span());
        }

        void visit_union(::HIR::ItemPath p, ::HIR::Union& item) override {
            auto _ = mResolve.set_impl_generics(MetadataType::None, item.mParams);
            auto ty = crate.types.path(HIR::GenericPath(p.getSimplePath()), &item);
            mCurrentType = ty;
            ::HIR::Visitor::visit_union(p, item);
            mCurrentType = nullptr;
        }

        void visit_struct(::HIR::ItemPath p, ::HIR::Struct& item) override {
            auto _ = mResolve.set_impl_generics(item.structMarkings.dst_type, item.mParams);
            auto ty = crate.types.path(HIR::GenericPath(p.getSimplePath()), &item);
            mCurrentType = ty;
            ::HIR::Visitor::visit_struct(p, item);
            mCurrentType = nullptr;
        }

        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override {
            auto _ = mResolve.set_impl_generics(MetadataType::None, item.mParams);
            auto ty = crate.types.path(HIR::GenericPath(p.getSimplePath()), &item);
            mCurrentType = ty;
            ::HIR::Visitor::visit_enum(p, item);
            mCurrentType = nullptr;
        }

        void visit_function(::HIR::ItemPath p, ::HIR::Function& item) override {
            auto _ = mResolve.set_item_generics(item.mParams);
            ::HIR::Visitor::visit_function(p, item);
        }

        void visit_type_alias(::HIR::ItemPath p, ::HIR::TypeAlias& item) override {
            // NOTE: Disabled, because generics in type aliases are never checked
            // Re-enabled to resolve a UFCS properly (1.90.0 libcore)
            auto _ = mResolve.set_impl_generics(MetadataType::Unknown, item.mParams);
            ::HIR::Visitor::visit_type_alias(p, item);
        }

        void visit_trait(::HIR::ItemPath p, ::HIR::Trait& trait) override {
            //TRACE_FUNCTION_F("impl" << impl.m_params.fmt_args() << " " << impl.m_type << " (mod=" << impl.m_src_module << ")");
            mInTraitDef = true;
            currentTrait = &trait;
            currentTraitPath = &p;
            //auto _ = m_resolve.set_cur_trait(p, trait);
            auto _ = mResolve.set_impl_generics(MetadataType::TraitObject, trait.mParams);
            ::HIR::Visitor::visit_trait(p, trait);
            currentTrait = nullptr;
            mInTraitDef = false;
        }

        void visit_type_impl(::HIR::TypeImpl& impl) override {
            TRACE_FUNCTION_F("impl" << impl.mParams.fmtArgs() << " " << impl.mType << " (mod=" << impl.srcModule << ")");
            auto _t = this->push_mod_traits(impl.srcModule, this->crate.getModByPath(Span(), impl.srcModule));
            auto _g = mResolve.set_impl_generics(impl.mType, impl.mParams);
            mCurrentType = impl.mType;
            ::HIR::Visitor::visit_type_impl(impl);
            mCurrentType = nullptr;
        }

        void visit_inherent_type(::HIR::ItemPath p, ::HIR::TypeAlias& item) override {
            auto _ = mResolve.set_item_generics(item.mParams);
            ::HIR::Visitor::visit_inherent_type(p, item);
        }

        void visit_marker_impl(const ::HIR::SimplePath& trait_path, ::HIR::MarkerImpl& impl) override {
            ::HIR::ItemPath p(impl.mType, trait_path, impl.traitArgs);
            TRACE_FUNCTION_F("impl" << impl.mParams.fmtArgs() << " " << trait_path << impl.traitArgs << " for " << impl.mType << " (mod=" << impl.srcModule << ")");
            auto _t = this->push_mod_traits(impl.srcModule, this->crate.getModByPath(Span(), impl.srcModule));
            auto _g = mResolve.set_impl_generics(impl.mType, impl.mParams);

            // TODO: Push a bound that `Self: ThisTrait`
            mCurrentType = impl.mType;
            currentTrait = &crate.getTraitByPath(Span(), trait_path);
            currentTraitPath = &p;

            // The implemented trait is always in scope
            traits.push_back(::std::make_pair(&trait_path, currentTrait));
            ::HIR::Visitor::visit_marker_impl(trait_path, impl);
            traits.pop_back();

            currentTrait = nullptr;
            mCurrentType = nullptr;
        }

        void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
            ::HIR::ItemPath p(impl.mType, trait_path, impl.traitArgs);
            TRACE_FUNCTION_F("impl" << impl.mParams.fmtArgs() << " " << trait_path << impl.traitArgs << " for " << impl.mType << " (mod=" << impl.srcModule << ")");
            auto _t = this->push_mod_traits(impl.srcModule, this->crate.getModByPath(Span(), impl.srcModule));
            auto _g = mResolve.set_impl_generics(MetadataType::Unknown, impl.mParams);

            expandTraitImplTypeDefaults(crate, trait_path, impl);

            mCurrentType = impl.mType;
            currentTrait = &crate.getTraitByPath(Span(), trait_path);
            currentTraitPath = &p;
            traits.push_back(::std::make_pair(&trait_path, currentTrait));

            this->visit_type(impl.mType);
            mResolve.update_impl_self_metadata(impl.mType);

            // TODO: Handle resolution of all items in m_resolve.m_type_equalities
            // - params might reference each other, so `set_item_generics` has to have been called
            // - But `m_type_equalities` can end up with non-resolved UFCS paths
            for (auto& e : mResolve.typeEqualities) {
                visit_type(e.second.ty);
            }

            // The implemented trait is always in scope
            ::HIR::Visitor::visit_trait_impl(trait_path, impl);
            traits.pop_back();

            currentTrait = nullptr;
            mCurrentType = nullptr;
        }

        void visit_expr(::HIR::ExprPtr& expr) override {
            struct ExprVisitor: public ::HIR::ExprVisitorDef {
                UfcsVisitor& upper_visitor;
                ::HIR::ExprNodeP mReplacement;

                ExprVisitor(UfcsVisitor& uv)
                    : ::HIR::ExprVisitorDef(uv.crate.types)
                    , upper_visitor(uv)
                {
                }

                void visit_type(::HIR::TypeRef& ty) override {
                    upper_visitor.visit_type(ty);
                }

                void visit_path_params(::HIR::PathParams& pp) override {
                    upper_visitor.visit_path_params(pp);
                }

                void visit_path(::HIR::Visitor::PathContext pc, ::HIR::Path& path) override {
                    upper_visitor.visit_path(path, pc);
                }

                void visit_pattern(const Span& sp, ::HIR::Pattern& pat) override {
                    upper_visitor.visit_pattern(pat);
                }

                void visit_node_ptr(::HIR::ExprNodeP& nodePtr) {
                    ::HIR::ExprVisitorDef::visit_node_ptr(nodePtr);
                    if (mReplacement) {
                        mReplacement->resType = nodePtr->resType;
                        mReplacement.swap(nodePtr);
                        mReplacement.reset();
                    }
                }

                // Custom to visit the inner expression
                void visit(::HIR::ExprNodeArraySized& node) override {
                    auto& as = node.mSize;
                    if (as.is_Unevaluated()) {
                        upper_visitor.visit_constgeneric(as.as_Unevaluated());
                    }
                    ::HIR::ExprVisitorDef::visit(node);
                }

                // Custom visitor for enum/struct constructors
                void visit(::HIR::ExprNodeCallPath& node) override {
                    ::HIR::ExprVisitorDef::visit(node);
                    const Span& sp = node.span();
                    if (node.mPath.mData.is_Generic()) {
                        // If it points to an enum, rewrite
                        auto& gp = node.mPath.mData.as_Generic();
                        if (gp.mPath.components().size() > 1) {
                            const auto& ent = upper_visitor.crate.getTypeitemByPath(sp, gp.mPath, /*ign_crate*/ false, true);
                            if (ent.is_Enum() && ent.as_Enum().findVariant(gp.mPath.components().back()) != SIZE_MAX) {
                                // Rewrite!
                                mReplacement.reset(upper_visitor.crate.pool->make<::HIR::ExprNodeTupleVariant>(sp, mv$(gp), /*is_struct*/ false, mv$(node.mArgs)));
                                DEBUG(&node << ": Replacing with TupleVariant " << mReplacement.get());
                                return;
                            }
                        }
                    }

                    // If this is pointing at a constant/static/associated constant, change to CallValue
                    MonomorphState discard(upper_visitor.crate.types);
                    auto v = upper_visitor.mResolve.getValue(node.span(), node.mPath, discard, true);
                    if (v.is_Constant() || v.is_Static()) {
                        auto* value_node = upper_visitor.crate.pool->make<HIR::ExprNodePathValue>(sp, std::move(node.mPath), v.is_Constant() ? ::HIR::ExprNodePathValue::Target::CONSTANT : v.is_Static() ? ::HIR::ExprNodePathValue::Target::STATIC : ::HIR::ExprNodePathValue::Target::UNKNOWN);
                        value_node->resType = upper_visitor.crate.types.infer();
                        mReplacement.reset(upper_visitor.crate.pool->make<::HIR::ExprNodeCallValue>(sp, ::HIR::ExprNodeP(value_node), mv$(node.mArgs)));
                        DEBUG(&node << ": Replacing with CallValue " << mReplacement.get());
                        return;
                    }
                }

                // Custom visitor for enum/struct constructors
                void visit(::HIR::ExprNodePathValue& node) override {
                    ::HIR::ExprVisitorDef::visit(node);
                    const Span& sp = node.span();
                    if (node.mPath.mData.is_Generic()) {
                        // If it points to an enum, set binding
                        auto& gp = node.mPath.mData.as_Generic();
                        if (gp.mPath.components().size() > 1) {
                            const auto& ent = upper_visitor.crate.getTypeitemByPath(sp, gp.mPath, /*ign_crate*/ false, true);
                            if (ent.is_Enum()) {
                                const auto& enm = ent.as_Enum();
                                auto idx = enm.findVariant(gp.mPath.components().back());
                                if (enm.mData.is_Value() || enm.mData.as_Data().at(idx).type == upper_visitor.crate.types.unit()) {
                                    mReplacement.reset(upper_visitor.crate.pool->make<::HIR::ExprNodeUnitVariant>(sp, mv$(gp), /*is_struct*/ false));
                                    DEBUG(&node << ": Replacing with UnitVariant " << mReplacement.get());
                                } else {
                                    node.target = ::HIR::ExprNodePathValue::ENUM_VAR_CONSTR;
                                }
                                return;
                            }
                        }

                        // TODO: Struct?
                    }
                }
#if 1
                void visit(::HIR::ExprNodeStructLiteral& node) override {
                    ::HIR::ExprVisitorDef::visit(node);
                    const Span& sp = node.span();
                    if (node.mType->is_Path() && node.mType->as_Path().path.mData.is_Generic()) {
                        // If it points to an enum, set binding
                        auto data = node.mType->cloneData();
                        auto& p = data.as_Path().path;
                        auto& gp = p.mData.as_Generic();
                        if (gp.mPath.components().size() > 1) {
                            const auto& ent = upper_visitor.crate.getTypeitemByPath(sp, gp.mPath, /*ign_crate*/ false, true);
                            if (ent.is_Enum()) {
                                DEBUG(&node << ": Tagging as an enum");
                                node.isStruct = false;
                                auto enumPath = std::move(gp);
                                auto var_name = enumPath.mPath.pop_component();
                                auto enumTy = upper_visitor.crate.types.path(std::move(enumPath), &ent.as_Enum());
                                p = ::HIR::Path(std::move(enumTy), std::move(var_name));
                            }
                        }
                        node.mType = upper_visitor.crate.types.intern(std::move(data));
                    }
                }
#endif

                // NOTE: Custom needed for trait scoping
                void visit(::HIR::ExprNodeBlock& node) override {
                    if (node.traits.size() == 0 && node.localMod.components().size() > 0) {
                        const auto& mod = upper_visitor.crate.getModByPath(node.span(), node.localMod);
                        for (const auto& trait_path : mod.traits) {
                            node.traits.push_back(::std::make_pair(&trait_path, &upper_visitor.crate.getTraitByPath(node.span(), trait_path)));
                        }
                    }
                    for (const auto& trait_ref : node.traits) {
                        upper_visitor.traits.push_back(trait_ref);
                    }

                    ::HIR::ExprVisitorDef::visit(node);

                    for (unsigned int i = 0; i < node.traits.size(); i++) {
                        upper_visitor.traits.pop_back();
                    }
                }
            };

            if (mVisitExprs && expr.get() != nullptr) {
                auto saved_in_expr = inExpr;
                inExpr = true;
                ExprVisitor v{*this};
                (*expr).visit(v);
                inExpr = saved_in_expr;
            }
        }

        bool locateTraitItemInBounds(::HIR::Visitor::PathContext pc, const ::HIR::TypeData* tr, const ::HIR::GenericParams& params, ::HIR::Path::Data& pd) {
            static Span sp;
            //const auto& name = pd.as_UfcsUnknown().item;
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

        ::HIR::Path::Data getUfcsKnown(::HIR::Visitor::PathContext pc, ::HIR::Path::Data::Data_UfcsUnknown e, ::HIR::GenericPath trait_path_real, const ::HIR::Trait& trait) const {
            struct MonomorphEraseHrls: public Monomorphiser {
                explicit MonomorphEraseHrls(HIR::TypeInterner& types): Monomorphiser(types) {}

                ::HIR::TypeRef getType(const Span& sp, const ::HIR::GenericRef& g) const override {
                    return types.generic(g.name, g.binding);
                }

                ::HIR::ConstGeneric getValue(const Span& sp, const ::HIR::GenericRef& g) const override {
                    return g;
                }

                ::HIR::LifetimeRef getLifetime(const Span& sp, const ::HIR::GenericRef& g) const override {
                    if (g.group() == 3) {
                        return HIR::LifetimeRef();
                    } else {
                        return HIR::LifetimeRef(g.binding);
                    }
                }
            };

            auto trait_path = MonomorphEraseHrls(crate.types).monomorphGenericpath(Span(), trait_path_real);
            if (pc == HIR::Visitor::PathContext::TYPE) {
                // If the trait has missing type argumenst, replace them with the defaults
                // Get trait, check if the type has ATCs
                const auto& aty = trait.types.at(e.item);
                if (e.params.mLifetimes.size() < aty.generics.mLifetimes.size()) {
                    e.params.mLifetimes.resize(aty.generics.mLifetimes.size());
                }
            }
            // TODO: Only do this when there's multiple options?
            if (inExpr) {
                for (auto& type : trait_path.mParams.types) {
                    type = crate.types.infer();
                }
            }
            return ::HIR::Path::Data::make_UfcsKnown({mv$(e.type), mv$(trait_path), mv$(e.item), mv$(e.params)});
        }

        static bool locateItemInTrait(::HIR::Visitor::PathContext pc, const ::HIR::Trait& trait, ::HIR::Path::Data& pd) {
            const auto& e = pd.as_UfcsUnknown();

            switch (pc) {
                case ::HIR::Visitor::PathContext::VALUE:
                    if (trait.values.find(e.item) != trait.values.end()) {
                        return true;
                    }
                    break;
                case ::HIR::Visitor::PathContext::TRAIT:
                    break;
                case ::HIR::Visitor::PathContext::TYPE:
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
        bool locateInTraitAndSet(::HIR::Visitor::PathContext pc, const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait, ::HIR::Path::Data& pd) {
            TRACE_FUNCTION_F(trait_path);
            // TODO: Get the span from caller
            static Span _sp;
            const auto& sp = _sp;
            if (locateItemInTrait(pc, trait, pd)) {
                pd = getUfcsKnown(pc, mv$(pd.as_UfcsUnknown()), trait_path.clone(), trait);
                return true;
            }

            auto pp = trait_path.mParams.clone();
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
                TODO(sp, "Monomorphise default arg " << def << " for trait path " << trait_path);
            }

            auto monomorphCb = MonomorphStatePtr(crate.types, pd.as_UfcsUnknown().type, &pp, nullptr);
            ::HIR::GenericPath par_trait_path_tmp;
            auto monomorphGpIfNeeded = [&](const ::HIR::GenericPath& tpl) -> const ::HIR::GenericPath& {
                // NOTE: This doesn't monomorph if the parameter set is the same
                if (monomorphiseGenericpathNeeded(tpl) /*&& tpl.m_params != trait_path.m_params*/) {
                    DEBUG("[monomorph_gp_if_needed] Monomorph tpl=" << tpl);
                    return par_trait_path_tmp = monomorphCb.monomorphGenericpath(sp, tpl, false /*no infer*/);
                } else {
                    return tpl;
                }
            };

            // Search supertraits (recursively)
            static HIR::GenericParams emptyGp;
            for (const auto& pt : trait.parentTraits) {
                auto _ = monomorphCb.push_hrb(pt.hrtbs ? *pt.hrtbs : emptyGp);
                const auto& par_trait_path = monomorphGpIfNeeded(pt.mPath);
                DEBUG("- Check " << par_trait_path);
                if (locateInTraitAndSet(pc, par_trait_path, *pt.traitPtr, pd)) {
                    return true;
                }
            }
            for (const auto& pt : trait.allParentTraits) {
                auto _ = monomorphCb.push_hrb(pt.hrtbs ? *pt.hrtbs : emptyGp);
                const auto& par_trait_path = monomorphGpIfNeeded(pt.mPath);
                DEBUG("- Check (all) " << par_trait_path);
                if (locateItemInTrait(pc, *pt.traitPtr, pd)) {
                    // TODO: Don't clone if this is from the temp.
                    pd = getUfcsKnown(pc, mv$(pd.as_UfcsUnknown()), par_trait_path.clone(), *pt.traitPtr);
                    return true;
                }
            }
            return false;
        }

        bool set_from_trait_impl(const Span& sp, ::HIR::Visitor::PathContext pc, const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait, ::HIR::Path::Data& pd) {
            auto& e = pd.as_UfcsUnknown();
            const auto& type = e.type;
            TRACE_FUNCTION_F("trait_path=" << trait_path << ", p=<" << type << " as _>::" << e.item);

            // TODO: This is VERY arbitary and possibly nowhere near what rustc does.
            // NOTE: `nullptr` passed for param count, as defaults are not yet expanded
            this->mResolve.findImpl(sp, trait_path.mPath, nullptr, type, [&](const auto& impl, bool fuzzy) -> bool {
                auto pp = impl.getTraitParams(crate.types);
                // Replace all placeholder parameters (group 2) with ivars (empty types)
                struct KillPlaceholders: public Monomorphiser {
                    explicit KillPlaceholders(HIR::TypeInterner& types): Monomorphiser(types) {}

                    ::HIR::TypeRef getType(const Span& sp, const ::HIR::GenericRef& ty) const override {
                        if (ty.is_placeholder()) {
                            return types.infer();
                        }
                        return types.generic(ty.name, ty.binding);
                    }
                    ::HIR::ConstGeneric getValue(const Span& sp, const ::HIR::GenericRef& val) const override {
                        return val.is_placeholder()
                            ? ::HIR::ConstGeneric()
                            : ::HIR::ConstGeneric(val);
                    }
                    ::HIR::LifetimeRef getLifetime(const Span& sp, const ::HIR::GenericRef& g) const override {
                        return HIR::LifetimeRef(g.binding);
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
                        const auto& this_ty = pp.types[i];
                        if (eTy->is_Infer() && eTy->as_Infer().index == ~0u) {
                            // Already _, leave as is
                        } else if (eTy != this_ty) {
                            eTy = crate.types.infer();
                        } else {
                            // Equal, good
                        }
                    }
                } else {
                    DEBUG("pp = " << pp);
                    // Otherwise, set to the current result.
                    pd = getUfcsKnown(pc, mv$(e), ::HIR::GenericPath(trait_path.mPath, mv$(pp)), trait);
                }
                return false;
            });
            return pd.is_UfcsKnown();
        }

        bool locateInTraitImplAndSet(const Span& sp, ::HIR::Visitor::PathContext pc, const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait, ::HIR::Path::Data& pd) {
            if (this->locateItemInTrait(pc, trait, pd)) {
                return set_from_trait_impl(sp, pc, trait_path, trait, pd);
            } else {
                DEBUG("- Item " << pd.as_UfcsUnknown().item << " not in trait " << trait_path.mPath);
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
                    return set_from_trait_impl(sp, pc, pt.mPath, *pt.traitPtr, pd);
                } else {
                    DEBUG("- Item " << pd.as_UfcsUnknown().item << " not in trait " << trait_path.mPath);
                }
            }
            return false;
        }

        bool resolve_UfcsUnknown_inherent(const ::HIR::SimplePath& vis_path, const ::HIR::Path& p, ::HIR::Visitor::PathContext pc, ::HIR::Path::Data& pd) {
            auto& e = pd.as_UfcsUnknown();
            TRACE_FUNCTION_F(e.type);
            return crate.findTypeImpls(e.type, HIR::ResolvePlaceholdersNop(), [&](const auto& impl) {
                DEBUG("- matched inherent impl" << impl.mParams.fmtArgs() << " " << impl.mType);
                // Search for item in this block
                switch (pc) {
                    case ::HIR::Visitor::PathContext::VALUE:
                        if (impl.methods.find(e.item) != impl.methods.end()) {
                            // HACK: Allow access to privates of `fmt:rt::Argument`
                            if (e.type->is_Path() && e.type->as_Path().path.mData.is_Generic() && e.type->as_Path().path.mData.as_Generic().mPath == crate.getLangItemPathOpt("format_argument")) {
                                // Allow
                            } else if (!impl.methods.at(e.item).publicity.isVisible(vis_path)) {
                                DEBUG("Private");
                                return false;
                            }
                        } else if (impl.constants.find(e.item) != impl.constants.end()) {
                            if (!impl.constants.at(e.item).publicity.isVisible(vis_path)) {
                                DEBUG("Private");
                                return false;
                            }
                        } else {
                            return false;
                        }
                        // Found it, just keep going (don't care about details here)
                        break;
                    case ::HIR::Visitor::PathContext::TRAIT:
                        return false;
                    case ::HIR::Visitor::PathContext::TYPE:
                        if (impl.types.find(e.item) == impl.types.end()) {
                            return false;
                        }
                        if (!impl.types.at(e.item).publicity.isVisible(vis_path)) {
                            DEBUG("Private");
                            return false;
                        }
                        break;
                }

                auto newData = ::HIR::Path::Data::make_UfcsInherent({mv$(e.type), mv$(e.item), mv$(e.params)});
                pd = mv$(newData);
                DEBUG("- Resolved, replace with " << p);
                return true;
            });
        }

        bool resolve_UfcsUnknown_trait(const ::HIR::Path& p, ::HIR::Visitor::PathContext pc, ::HIR::Path::Data& pd) {
            static Span sp;
            auto& e = pd.as_UfcsUnknown();
            const bool collapseToSubtrait = crate.featureEnabled("supertrait_item_shadowing");
            ::std::vector<::std::pair<::HIR::SimplePath, ::HIR::Path::Data>> candidates;
            DEBUG("m_traits.size() = " << traits.size());
            for (const auto& trait_info : traits) {
                const auto& trait = *trait_info.second;

                DEBUG(e.item << " in? " << *trait_info.first);
                switch (pc) {
                    case ::HIR::Visitor::PathContext::VALUE:
                        if (trait.values.find(e.item) == trait.values.end()) {
                            continue;
                        }
                        break;
                    case ::HIR::Visitor::PathContext::TRAIT:
                    case ::HIR::Visitor::PathContext::TYPE:
                        if (trait.types.find(e.item) == trait.types.end()) {
                            continue;
                        }
                        break;
                }
                DEBUG("- Trying trait " << *trait_info.first);

                auto trait_path = ::HIR::GenericPath(*trait_info.first);
                trait_path.mParams.types.reserve(trait.mParams.types.size());
                for (size_t i = 0; i < trait.mParams.types.size(); i++) {
                    trait_path.mParams.types.push_back(crate.types.infer());
                }

                // TODO: If there's only one trait with this name, assume it's the correct one.

                // TODO: Search supertraits
                // TODO: Should impls be searched first, or item names?
                // - Item names add complexity, but impls are slower
                if (!collapseToSubtrait) {
                    if (this->locateInTraitImplAndSet(sp, pc, mv$(trait_path), trait, pd)) {
                        return true;
                    }
                    continue;
                }

                auto candidateData = ::HIR::Path::Data::make_UfcsUnknown({
                    e.type,
                    e.item,
                    e.params.clone(),
                });
                if (this->locateInTraitImplAndSet(sp, pc, mv$(trait_path), trait, candidateData)) {
                    candidates.push_back(::std::make_pair(*trait_info.first, mv$(candidateData)));
                }
            }

            if (collapseToSubtrait && !candidates.empty()) {
                ::std::vector<::HIR::SimplePath> candidateTraits;
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

        void visit_type(::HIR::TypeRef& ty) override {
            // TODO: Add a span parameter.
            static Span sp;

            ::HIR::Visitor::visit_type(ty);

            // TODO: If this an associated type, check for default trait params

            if (runEat) {
                TRACE_FUNCTION_FR(ty, ty);
                std::vector<HIR::TypeRef> stack;
                if (ty->is_Path()) {
                    stack.push_back(ty);
                }
                while (mResolve.expandAssociatedTypesSingle(sp, ty)) {
                    if (::std::find(stack.begin(), stack.end(), ty) != stack.end()) {
                        ::std::sort(stack.begin(), stack.end());
                        DEBUG("Loop detected, picking " << ty);
                        ty = std::move(stack[0]);
                        ::HIR::Visitor::visit_type(ty);
                        break;
                    }
                    // NOTE: Only need to clone if this is a Path, as that's the only way we could loop again
                    if (ty->is_Path()) {
                        stack.push_back(ty);
                    }
                    DEBUG("counter = " << stack.size());
                    //ASSERT_BUG(sp, !visit_ty_with(ty, [&](const HIR::TypeData* ty)->bool { return TU_TEST1(ty.data(), Generic, .is_placeholder()); }), "Encountered placeholder - " << ty);
                    rewrite_ty_with(crate.types, ty, [&](HIR::TypeRef& rewritten, HIR::TypeData& data) -> bool {
                        if (TU_TEST1(data, Generic, .is_placeholder())) {
                            rewritten = crate.types.infer();
                        }
                        return false;
                    });
                    ASSERT_BUG(sp, stack.size() < 20, "Sanity limit exceeded when resolving UFCS in type " << ty);
                    // Invoke a special version of EAT that only processes a single item.
                    // - Keep recursing while this does replacements
                    ::HIR::Visitor::visit_type(ty);
                }
            }
        }

        void visit_constgeneric(::HIR::ConstGeneric& val) override {
            auto saved_visit_exprs = mVisitExprs;
            mVisitExprs = true;
            ::HIR::Visitor::visit_constgeneric(val);
            mVisitExprs = saved_visit_exprs;
        }

        void visit_path(::HIR::Path& p, ::HIR::Visitor::PathContext pc) override {
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

                this->visit_type(e.type);
                this->visit_path_params(e.params);

                // If processing a trait, and the type is 'Self', search for the type/method on the trait
                // - Explicitly encoded because `Self::Type` has a different meaning to `MyType::Type` (the latter will search bounds first)
                // - NOTE: Could be in an inherent block, where there's no trait
                if (/*m_current_type &&*/ currentTrait && e.type == crate.types.self()) {
                    ::HIR::GenericPath trait_path;
                    if (currentTraitPath->trait_path()) {
                        trait_path = ::HIR::GenericPath(*currentTraitPath->trait_path());
                        trait_path.mParams = currentTraitPath->trait_args()->clone();
                    } else {
                        trait_path = ::HIR::GenericPath(currentTraitPath->getSimplePath());
                        trait_path.mParams = currentTrait->mParams.makeNopParams(crate.types, 0);
                    }
                    if (locateInTraitAndSet(pc, trait_path, *currentTrait, p.mData)) {
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
                    rewrite_path_tys_with(crate.types, p, [&](HIR::TypeRef& t, HIR::TypeData& data) -> bool {
                        if (data.is_Generic() && data.as_Generic().binding == GENERICSelf) {
                            t = mCurrentType;
                        }
                        return false;
                    });
                }

                // Search for matching impls in current generic blocks
                if (mResolve.itemGenerics != nullptr && locateTraitItemInBounds(pc, e.type, *mResolve.itemGenerics, p.mData)) {
                    DEBUG("Found in item params, p = " << p);
                    assert(!p.mData.is_UfcsUnknown());
                    return;
                }
                if (mResolve.implGenerics != nullptr && locateTraitItemInBounds(pc, e.type, *mResolve.implGenerics, p.mData)) {
                    DEBUG("Found in impl params, p = " << p);
                    assert(!p.mData.is_UfcsUnknown());
                    return;
                }

                // `<dyn Trait>::item` can name an item supplied by a supertrait.
                // Resolve it from the trait object's principal trait before
                // looking for an implementation of the trait object type.
                if (const auto* trait_object = e.type->opt_TraitObject()) {
                    const auto& principal = trait_object->mTrait;
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
                    ::HIR::GenericPath trait_path;
                    if (currentTraitPath->trait_path()) {
                        trait_path = ::HIR::GenericPath(*currentTraitPath->trait_path());
                        trait_path.mParams = currentTraitPath->trait_args()->clone();
                    } else {
                        trait_path = ::HIR::GenericPath(currentTraitPath->getSimplePath());
                        trait_path.mParams = currentTrait->mParams.makeNopParams(crate.types, 0);
                    }

                    if (locateInTraitAndSet(pc, trait_path, *currentTrait, p.mData)) {
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
                        auto trait_path = mstate.monomorphGenericpath(sp, t.mPath, /*allow_infer*/ true);
                        DEBUG("Searching ATY bound: " << trait_path);
                        // Search within this (bounded) trait for the outer item
                        if (this->locateInTraitImplAndSet(sp, pc, mv$(trait_path), *t.traitPtr, p.mData)) {
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
                if ((pc == HIR::Visitor::PathContext::VALUE /*|| pc == HIR::Visitor::PathContext::PATTERN*/) && e.type->is_Path() && e.type->as_Path().binding.is_Enum()) {
                    const auto& enm = *e.type->as_Path().binding.as_Enum();
                    auto idx = enm.findVariant(e.item);
                    if (idx != SIZE_MAX) {
                        DEBUG("Found variant " << e.type << " #" << idx);
                        if (enm.mData.is_Value() || !enm.mData.as_Data()[idx].is_struct) {
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
                if (pc == HIR::Visitor::PathContext::TYPE && e.type->is_Path() && e.type->as_Path().binding.is_Enum()) {
                    const auto& enm = *e.type->as_Path().binding.as_Enum();
                    auto idx = enm.findVariant(e.item);
                    if (idx != SIZE_MAX) {
                        DEBUG("Found variant " << e.type << " #" << idx);
                        if (enm.mData.is_Data() && enm.mData.as_Data()[idx].is_struct) {
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
                ::HIR::Visitor::visit_path(p, pc);
            }
        }

        void visit_pattern(::HIR::Pattern& pat) override {
            static Span _sp = Span();
            const Span& sp = _sp;

            ::HIR::Visitor::visit_pattern(pat);

            TU_MATCH_HDRA( (pat.mData), {)
            default:
                break;
                TU_ARMA(Value, e) {
                    this->visitPatternValue(sp, pat, e.val);
                    if (e.val.is_Named() && e.val.as_Named().path.mData.is_Generic() && e.val.as_Named().path.mData.as_Generic().mPath.components().size() > 1) {
                        auto& gp = e.val.as_Named().path.mData.as_Generic();
                        if (const auto* enmP = crate.getTypeitemByPath(sp, gp.mPath, false, true).opt_Enum()) {
                            unsigned idx = enmP->findVariant(gp.mPath.components().back());
                            pat.mData = ::HIR::Pattern::Data::make_PathValue({mv$(gp), ::HIR::Pattern::PathBinding::make_Enum({enmP, idx})});
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
                    this->resolve_pattern_binding(sp, e.path, e.binding);
                }
                TU_ARMA(PathTuple, e) {
                    this->resolve_pattern_binding(sp, e.path, e.binding);
                }
                TU_ARMA(PathNamed, e) {
                    this->resolve_pattern_binding(sp, e.path, e.binding);
                }
            }
        }

        void resolve_pattern_binding(const Span& sp, ::HIR::Path& path, ::HIR::Pattern::PathBinding& binding) {
            if (!binding.is_Unbound()) {
                return;
            }

            auto ty = crate.types.path(path.clone(), {});
            this->visit_type(ty);
            ASSERT_BUG(sp, ty->is_Path(), "Pattern associated type didn't resolve to a path - " << ty);

            const auto& te = ty->as_Path();
            ASSERT_BUG(sp, te.path.mData.is_Generic(), "Pattern associated type didn't resolve to a generic path - " << ty);
            path = te.path.clone();

            if (te.binding.is_Struct()) {
                binding = ::HIR::Pattern::PathBinding::make_Struct(te.binding.as_Struct());
            } else if (te.binding.is_Union()) {
                binding = ::HIR::Pattern::PathBinding::make_Union(te.binding.as_Union());
            } else {
                ERROR(sp, E0000, "Pattern associated type didn't resolve to a struct or union - " << ty);
            }
        }

        void visitPatternValue(const Span& sp, const ::HIR::Pattern& pat, ::HIR::Pattern::Value& val) {
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
                        bool rv = crate.findTypeImpls(pe.type, HIR::ResolvePlaceholdersNop(), [&](const auto& impl) {
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
    void sort_impl_group(::HIR::Crate::ImplGroup<std::unique_ptr<T>>& ig, ::std::function<void(::std::ostream& os, const T&)> fmt) {
        auto newEnd = ::std::remove_if(ig.generic.begin(), ig.generic.end(), [&ig, &fmt](::std::unique_ptr<T>& ty_impl) {
            const auto& type = ty_impl->mType; // Using field accesses in templates feels so dirty
            const ::HIR::SimplePath* path = type->getSortPath();

            if (path) {
                DEBUG(*path << " += " << FMT_CB(os, fmt(os, *ty_impl)));
                ig.named[*path].push_back(mv$(ty_impl));
            } else if (type->is_Path() || type->is_Generic()) {
                return false;
            } else {
                ig.nonNamed.push_back(mv$(ty_impl));
            }
            return true;
        });
        ig.generic.erase(newEnd, ig.generic.end());
    }

    // --- Indexing of trait impls ---
    template <typename T>
    void push_index_impl_group_list(::std::vector<const T*>& dst, const ::std::vector<std::unique_ptr<T>>& src) {
        for (const auto& e : src) {
            dst.push_back(&*e);
        }
    }

    template <typename T>
    void push_index_impl_group(::HIR::Crate::ImplGroup<const T*>& dst, const ::HIR::Crate::ImplGroup<std::unique_ptr<T>>& src) {
        for (const auto& e : src.named) {
            push_index_impl_group_list(dst.named[e.first], e.second);
        }
        push_index_impl_group_list(dst.nonNamed, src.nonNamed);
        push_index_impl_group_list(dst.generic, src.generic);
    }

    void push_index_impls(::HIR::Crate& dst, const ::HIR::Crate& src) {
        push_index_impl_group(dst.allTypeImpls, src.typeImpls);
        for (const auto& ig : src.traitImpls) {
            push_index_impl_group(dst.allTraitImpls[ig.first], ig.second);
        }
        for (const auto& ig : src.markerImpls) {
            push_index_impl_group(dst.allMarkerImpls[ig.first], ig.second);
        }
    }

    // --- Indexing of inherent methods ---
    void push_index_inherent_methods_list(::HIR::InherentCache& icache, const HIR::SimplePath& langBox, const ::std::vector<std::unique_ptr<HIR::TypeImpl>>& src) {
        Span sp;
        for (const auto& ti : src) {
            const auto& impl = *ti;
            TRACE_FUNCTION_F("impl" << impl.mParams.fmtArgs() << " " << impl.mType);
            icache.insertAll(sp, impl, langBox);
        }
    }

    void push_index_inherent_methods(::HIR::InherentCache& icache, const HIR::SimplePath& langBox, const ::HIR::Crate& src) {
        TRACE_FUNCTION_F("src = " << src.crateName);
        for (const auto& e : src.typeImpls.named) {
            push_index_inherent_methods_list(icache, langBox, e.second);
        }
        push_index_inherent_methods_list(icache, langBox, src.typeImpls.nonNamed);
        push_index_inherent_methods_list(icache, langBox, src.typeImpls.generic);
    }
} // namespace ""

using namespace resolve_ufcs;

void ConvertHIRResolveUFCSOuter(::HIR::Crate& crate) {
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
    exp.visit_crate(crate);
}

void ConvertHIRResolveUFCS(::HIR::Crate& crate) {
    UfcsVisitor exp{crate, true};
    exp.visit_crate(crate);
}

void ConvertHIRResolveUFCSExpr(const ::HIR::Crate& crate, const ::HIR::ItemPath& ip, ::HIR::ExprPtr& expr_ptr) {
    TRACE_FUNCTION_F(ip);
    // Check innards but NOT the value
    UfcsVisitor exp{crate, true};
    exp.visit_expr(expr_ptr);
}

void ConvertHIRResolveUFCSSortImpls(::HIR::Crate& crate) {
    // Sort impls!
    sort_impl_group<HIR::TypeImpl>(crate.typeImpls, [](::std::ostream& os, const HIR::TypeImpl& i) {
        os << "impl" << i.mParams.fmtArgs() << " " << i.mType;
    });
    DEBUG("Type impl counts: " << crate.typeImpls.named.size() << " path groups, " << crate.typeImpls.nonNamed.size() << " primitive, " << crate.typeImpls.generic.size() << " ungrouped");
    for (auto& implGroup : crate.traitImpls) {
        sort_impl_group<HIR::TraitImpl>(implGroup.second, [&](::std::ostream& os, const HIR::TraitImpl& i) {
            os << "impl" << i.mParams.fmtArgs() << " " << implGroup.first << i.traitArgs << " for " << i.mType;
        });
    }
    for (auto& implGroup : crate.markerImpls) {
        sort_impl_group<HIR::MarkerImpl>(implGroup.second, [&](::std::ostream& os, const HIR::MarkerImpl& i) {
            os << "impl" << i.mParams.fmtArgs() << " " << implGroup.first << i.traitArgs << " for " << i.mType << " {}";
        });
    }

    // Create indexes
    push_index_impls(crate, crate);
    for (const auto& ec : crate.extCrates) {
        push_index_impls(crate, *ec.second.mData);
    }

    {
        const auto& langBox = crate.getLangItemPathOpt("owned_box");
        push_index_inherent_methods(crate.inherentMethodCache, langBox, crate);
        for (const auto& ec : crate.extCrates) {
            push_index_inherent_methods(crate.inherentMethodCache, langBox, *ec.second.mData);
        }
    }
}
