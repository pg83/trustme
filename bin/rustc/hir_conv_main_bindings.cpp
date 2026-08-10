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

void ConvertHIR_Bind(::HIR::Crate& crate);

namespace {

    enum class Target {
        TypeItem,
        Struct,
        Enum,
        EnumVariant,
    };

    const void* get_type_pointer(const Span& sp, const ::HIR::Crate& crate, const ::HIR::SimplePath& path, Target t) {
        if (t == Target::EnumVariant) {
            return &crate.get_typeitem_by_path(sp, path, /*ignore_crate_name=*/false, /*ignore_last_node=*/true).as_Enum();
        } else {
            const auto& ti = crate.get_typeitem_by_path(sp, path);
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

    void fix_type_params(HIR::TypeInterner& types, const Span& sp, const ::HIR::GenericParams& params_def, ::HIR::PathParams& params) {
#if 1
        if (params.m_lifetimes.size() == 0) {
            params.m_lifetimes.resize(params_def.m_lifetimes.size());
        }
        if (params.m_lifetimes.size() != params_def.m_lifetimes.size()) {
            ERROR(sp, E0000, "Incorrect lifetime param count, expected " << params_def.m_lifetimes.size() << ", got " << params.m_lifetimes.size());
        }

        if (params.m_types.size() == 0) {
            while (params.m_types.size() < params_def.m_types.size()) {
                params.m_types.push_back(types.infer());
            }
            // TODO: Optionally fill in the defaults?
        }
        if (params.m_types.size() != params_def.m_types.size()) {
            ERROR(sp, E0000, "Incorrect parameter count, expected " << params_def.m_types.size() << ", got " << params.m_types.size());
        }

        if (params.m_values.size() == 0) {
            params.m_values.resize(params_def.m_values.size());
        }
        if (params.m_values.size() != params_def.m_values.size()) {
            ERROR(sp, E0000, "Incorrect value parameter count, expected " << params_def.m_values.size() << ", got " << params.m_values.size());
        }
#endif
    }

    void fix_param_count(HIR::TypeInterner& types, const Span& sp, const ::HIR::GenericPath& path, const ::HIR::GenericParams& param_defs, ::HIR::PathParams& params, bool fill_infer = true, const ::HIR::TypeRef* self_ty = nullptr) {
        TRACE_FUNCTION_FR(param_defs.fmt_args() << " -> " << params << " (fill_infer=" << fill_infer << ")", params);
        if (params.m_lifetimes.size() != param_defs.m_lifetimes.size()) {
            if (params.m_lifetimes.size() == 0 && fill_infer) {
                params.m_lifetimes.resize(param_defs.m_lifetimes.size());
            }
        }
        if (params.m_types.size() != param_defs.m_types.size()) {
            TRACE_FUNCTION_FR(path, params);

            if (params.m_types.size() == 0 && fill_infer) {
                while (params.m_types.size() < param_defs.m_types.size()) {
                    params.m_types.push_back(types.infer());
                }
            } else if (params.m_types.size() > param_defs.m_types.size()) {
                ERROR(sp, E0000, "Too many type parameters passed to " << path);
            } else {
                while (params.m_types.size() < param_defs.m_types.size()) {
                    const auto& typ = param_defs.m_types[params.m_types.size()];
                    if (typ.m_default->is_Infer()) {
                        ERROR(sp, E0000, "Omitted type parameter with no default in " << path);
                    } else {
                        // TODO: Does expanding defaults need a custom monomorphiser that can handle later defaults?
                        MonomorphStatePtr ms(types, self_ty, &params, nullptr);
                        auto ty = ms.monomorph_type(sp, typ.m_default);
                        params.m_types.push_back(mv$(ty));
                    }
                }
            }
        }
        if (params.m_values.size() != param_defs.m_values.size()) {
            if (params.m_values.size() == 0 && fill_infer) {
                params.m_values.resize(param_defs.m_values.size());
            } else if (params.m_values.size() > param_defs.m_values.size()) {
                ERROR(sp, E0000, "Too many value parameters passed to " << path);
            } else {
                while (params.m_values.size() < param_defs.m_values.size()) {
                    const auto& val = param_defs.m_values[params.m_values.size()];
                    if (val.m_default.is_Infer()) {
                        ERROR(sp, E0000, "Omitted value parameter with no default in " << path);
                    } else {
                        // TODO: Anything to be worried about with Unevaluated?, it may not have had its params set yet
                        params.m_values.push_back(val.m_default.clone());
                    }
                }
            }
        }
    }

    class BindVisitor: public ::HIR::Visitor {
        const ::HIR::Crate& m_crate;

        typeck::ModuleState m_ms;

        struct CurMod {
            const ::HIR::Module* ptr;
            const ::HIR::ItemPath* path;
        } m_cur_module;

        unsigned m_in_expr;

        ::HIR::ItemPath* m_fcn_path = nullptr;
        ::HIR::Function* m_fcn_ptr = nullptr;
        unsigned int m_fcn_erased_count = 0;

    public:
        BindVisitor(const ::HIR::Crate& crate)
            : ::HIR::Visitor(nullptr, crate.m_types)
            , m_crate(crate)
            , m_ms(crate)
            , m_in_expr(0)
        {
            static ::HIR::ItemPath root_path("");
            m_cur_module.ptr = &crate.m_root_module;
            m_cur_module.path = &root_path;
        }

        HIR::TypeInterner& interner() const { return m_crate.m_types; }

        void visit_module(::HIR::ItemPath p, ::HIR::Module& mod) override {
            auto parent_mod = m_cur_module;
            m_cur_module.ptr = &mod;
            m_cur_module.path = &p;

            m_ms.push_traits(p, mod);
            ::HIR::Visitor::visit_module(p, mod);
            m_ms.pop_traits(mod);

            m_cur_module = parent_mod;
        }

        void visit_trait_path(::HIR::TraitPath& p) override {
            static Span sp;
            p.m_trait_ptr = &m_crate.get_trait_by_path(sp, p.m_path.m_path);

            ::HIR::Visitor::visit_trait_path(p);
        }

        void visit_literal(const Span& sp, EncodedLiteral& lit) {
            for (auto& r : lit.relocations) {
                if (r.p) {
                    visit_path(*r.p, ::HIR::Visitor::PathContext::VALUE);
                }
            }
        }

        void visit_pattern_Value(const Span& sp, ::HIR::Pattern& pat, ::HIR::Pattern::Value& val) {
            bool is_single_value = pat.m_data.is_Value();

            if (auto* ve = val.opt_Named()) {
                if (auto* pe = ve->path.m_data.opt_Generic()) {
                    const auto& path = pe->m_path;
                    const auto& pc = path.components().back();
                    const ::HIR::Module* mod = nullptr;
                    if (path.components().size() == 1) {
                        mod = &m_crate.get_mod_by_path(sp, path, true);
                    } else {
                        const auto& ti = m_crate.get_typeitem_by_path(sp, path, /*ignore_crate_name=*/false, /*ignore_last_node=*/true);
                        if (const auto& enm = ti.opt_Enum()) {
                            if (!is_single_value) {
                                ERROR(sp, E0000, "Enum variant in range pattern - " << pat);
                            }

                            // Enum variant
                            auto idx = enm->find_variant(pc);
                            if (idx == SIZE_MAX) {
                                BUG(sp, "'" << pc << "' isn't a variant in path " << path);
                            }
                            HIR::GenericPath path = std::move(*pe);
                            fix_type_params(m_crate.m_types, sp, enm->m_params, path.m_params);
                            pat.m_data = ::HIR::Pattern::Data::make_PathValue({mv$(path), ::HIR::Pattern::PathBinding::make_Enum({enm, static_cast<unsigned>(idx)})});
                        } else if ((mod = ti.opt_Module())) {
                            mod = &ti.as_Module();
                        } else {
                            BUG(sp, "Node " << path.components().size() - 2 << " of path " << ve->path << " wasn't a module");
                        }
                    }

                    if (mod) {
                        auto it = mod->m_value_items.find(path.components().back());
                        if (it == mod->m_value_items.end()) {
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
                                const auto& str = mod->m_mod_items.find(pc)->second->ent.as_Struct();
                                // Convert into a dedicated pattern type
                                if (!is_single_value) {
                                    ERROR(sp, E0000, "Struct in range pattern - " << pat);
                                }
                                auto path = mv$(*pe);
                                fix_type_params(m_crate.m_types, sp, str.m_params, path.m_params);
                                pat.m_data = ::HIR::Pattern::Data::make_PathValue({mv$(path), &str});
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

            TU_MATCH_HDRA( (pat.m_data), {)
            default:
                // Nothing
            TU_ARMA(Value, e) {
                    this->visit_pattern_Value(sp, pat, e.val);
                }
                TU_ARMA(Range, e) {
                    if (e.start) {
                        this->visit_pattern_Value(sp, pat, *e.start);
                    }
                    if (e.end) {
                        this->visit_pattern_Value(sp, pat, *e.end);
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
                if (m_ms.m_impl_generics) {
                    (*unevaluated)->params_impl = m_ms.m_impl_generics->make_nop_params(m_crate.m_types, 0);
                }
                if (m_ms.m_item_generics) {
                    (*unevaluated)->params_item = m_ms.m_item_generics->make_nop_params(m_crate.m_types, 1);
                }
            }
        }

        void visit_params(::HIR::GenericParams& params) override {
            static Span sp;
            for (auto& bound : params.m_bounds) {
                if (auto* be = bound.opt_TraitBound()) {
                    {
                        const auto& trait = m_crate.get_trait_by_path(sp, be->trait.m_path.m_path);
                        fix_param_count(m_crate.m_types, sp, be->trait.m_path, trait.m_params, be->trait.m_path.m_params, /*fill_infer=*/false, &be->type);
                    }
                    // Also ensure that the defaults are filled in the source traits
                    // - Is there a better solution to this? It feels like it would give the wrong answer (filling defaults incorrectly)
                    for (auto& aty : be->trait.m_type_bounds) {
                        const auto& trait = m_crate.get_trait_by_path(sp, aty.second.source_trait.m_path);
                        fix_param_count(m_crate.m_types, sp, be->trait.m_path, trait.m_params, aty.second.source_trait.m_params, /*fill_infer=*/false, &be->type);
                    }
                    for (auto& aty : be->trait.m_type_bounds) {
                        const auto& trait = m_crate.get_trait_by_path(sp, aty.second.source_trait.m_path);
                        fix_param_count(m_crate.m_types, sp, be->trait.m_path, trait.m_params, aty.second.source_trait.m_params, /*fill_infer=*/false, &be->type);
                    }
                }
            }

            ::HIR::Visitor::visit_params(params);
        }

        void visit_associatedtype(HIR::ItemPath p, ::HIR::AssociatedType& item) override {
            static Span sp;
            HIR::Visitor::visit_associatedtype(p, item);
            HIR::TypeRef ty = m_crate.m_types.path(p.get_full_path(), {});
            for (auto& bound : item.m_trait_bounds) {
                const auto& trait = m_crate.get_trait_by_path(sp, bound.m_path.m_path);
                fix_param_count(m_crate.m_types, sp, bound.m_path, trait.m_params, bound.m_path.m_params, /*fill_infer=*/false, &ty);
            }
        }

        void visit_type(::HIR::TypeRef& ty) override {
            visit_type_inner(ty);
        }

        void visit_type_inner(::HIR::TypeRef& ty, bool do_bind = true) {
            //TRACE_FUNCTION_F(ty);
            static Span sp;
            auto data = ty->clone_data();
            bool data_visited = false;

            if (auto* e = data.opt_Path()) {
                TU_MATCH_HDRA( (e->path.m_data), {)
                TU_ARMA(Generic, pe) {
                        if (!do_bind) {
                            break;
                        }
                        const auto& item = *reinterpret_cast<const ::HIR::TypeItem*>(get_type_pointer(sp, m_crate, pe.m_path, Target::TypeItem));
                        TU_MATCH_DEF(
                            ::HIR::TypeItem,
                            (item),
                            (e3),
                            (ERROR(sp, E0000, "Unexpected item type returned for " << pe.m_path << " - " << item.tag_str());),
                            (
                                TypeAlias, BUG(sp, "TypeAlias encountered after `Resolve Type Aliases` - " << ty);
                                // Assume it'll be filled out, with the correct binding
                            ),
                            (ExternType, e->binding = ::HIR::TypePathBinding::make_ExternType(&e3); DEBUG("- " << ty);),
                            (Struct, fix_param_count(m_crate.m_types, sp, pe, e3.m_params, pe.m_params, /*fill_infer=*/m_in_expr != 0); e->binding = ::HIR::TypePathBinding::make_Struct(&e3); DEBUG("- " << ty);),
                            (Union, fix_param_count(m_crate.m_types, sp, pe, e3.m_params, pe.m_params, /*fill_infer=*/m_in_expr != 0); e->binding = ::HIR::TypePathBinding::make_Union(&e3); DEBUG("- " << ty);),
                            (Enum, fix_param_count(m_crate.m_types, sp, pe, e3.m_params, pe.m_params, /*fill_infer=*/m_in_expr != 0); e->binding = ::HIR::TypePathBinding::make_Enum(&e3); DEBUG("- " << ty);),
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
                        const auto& trait = m_crate.get_trait_by_path(sp, pe.trait.m_path);
                        fix_param_count(m_crate.m_types, sp, pe.trait, trait.m_params, pe.trait.m_params, /*fill_infer=*/false, &pe.type);

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
                HIR::TypeRef ty_eself = m_crate.m_types.generic("ErasedSelf", GENERIC_ErasedSelf);
                for (auto& t : te->m_traits) {
                    const auto& trait = m_crate.get_trait_by_path(sp, t.m_path.m_path);
                    fix_param_count(m_crate.m_types, sp, t.m_path, trait.m_params, t.m_path.m_params, /*fill_infer=*/m_in_expr, &ty_eself);
                }

                if (auto* ee = te->m_inner.opt_Fcn()) {
                    DEBUG("Set origin of ErasedType - " << ty);
                    // If not, figure out what to do with it

                    // If the function path is set, we're processing the return type of a function
                    // - Add this to the list of erased types associated with the function
                    if (ee->m_origin != HIR::SimplePath()) {
                        // Already set, somehow (maybe we're visiting the function after expansion)
                    } else if (m_fcn_path) {
                        assert(m_fcn_ptr);
                        DEBUG(*m_fcn_path << " " << m_fcn_erased_count);

                        ::HIR::PathParams params = m_fcn_ptr->m_params.make_nop_params(m_crate.m_types, 1);
                        // Populate with function path
                        ee->m_origin = m_fcn_path->get_full_path();
                        TU_MATCH_HDRA( (ee->m_origin.m_data), {)
                        TU_ARMA(Generic, e2) {
                                e2.m_params = mv$(params);
                            }
                            TU_ARMA(UfcsInherent, e2) {
                                e2.params = mv$(params);
                                // Impl params, just directly references the parameters.
                                // - Downstream monomorph will fix that
                                e2.impl_params = m_ms.m_impl_generics->make_nop_params(m_crate.m_types, 0);
                            }
                            TU_ARMA(UfcsKnown, e2) {
                                e2.params = mv$(params);
                            }
                            TU_ARMA(UfcsUnknown, e2) {
                                throw "";
                            }
                        }
                        ee->m_index = m_fcn_erased_count++;
                    }
                    // If the function _pointer_ is set (but not the path), then we're in the function arguments
                    // - Add a un-namable generic parameter (TODO: Prevent this from being explicitly set when called)
                    else if (m_fcn_ptr) {
                        // Visit inner first, to handle nested
                        visit_type_data(data);
                        data_visited = true;

                        size_t idx = m_fcn_ptr->m_params.m_types.size();
                        auto name = RcString::new_interned(FMT("erased$" << idx));
                        DEBUG("-> " << name);
                        auto new_ty = m_crate.m_types.generic(name, 256 + idx);
                        m_fcn_ptr->m_params.m_types.push_back({name, m_crate.m_types.infer(), te->m_is_sized});
                        for (auto& trait : te->m_traits) {
                            struct M: MonomorphiserNop {
                                const HIR::TypeRef& new_ty;

                                M(HIR::TypeInterner& types, const HIR::TypeRef& ty)
                                    : MonomorphiserNop(types)
                                    , new_ty(ty)
                                {
                                }

                                ::HIR::TypeRef get_type(const Span& sp, const ::HIR::GenericRef& ty) const override {
                                    if (ty.binding == GENERIC_ErasedSelf) {
                                        return new_ty;
                                    }
                                    return m_types.generic(ty.name, ty.binding);
                                }
                            } m{m_crate.m_types, new_ty};

                            // TODO: Monomorph the trait to replace `Self` with this generic?
                            // - Except, that should it be?
                            m_fcn_ptr->m_params.m_bounds.push_back(::HIR::GenericBound::make_TraitBound({nullptr, new_ty, m.monomorph_traitpath(sp, trait, false)}));
                        }
                        for (const auto& lft : te->m_lifetime_bounds) {
                            m_fcn_ptr->m_params.m_bounds.push_back(::HIR::GenericBound::make_TypeLifetime({new_ty, lft}));
                        }
                        ty = ::std::move(new_ty);
                        return;
                    } else {
                        // TODO: If we're in a top-level `type`, then it must be used as the return type of a function.
                        // https://rust-lang.github.io/rfcs/2515-type_alias_impl_trait.html#type-alias
                        ERROR(sp, E0000, "Use of an erased type outside of a function return - " << ty);
                    }
                }
            } else if (auto* te = data.opt_TraitObject()) {
                if (te->m_trait.m_path.m_path != HIR::SimplePath()) {
                    const auto& trait = m_crate.get_trait_by_path(sp, te->m_trait.m_path.m_path);
                    fix_param_count(m_crate.m_types, sp, te->m_trait.m_path, trait.m_params, te->m_trait.m_path.m_params, /*fill_infer=*/m_in_expr, nullptr);
                }
                for (auto& m : te->m_markers) {
                    const auto& trait = m_crate.get_trait_by_path(sp, m.m_path);
                    fix_param_count(m_crate.m_types, sp, m, trait.m_params, m.m_params, /*fill_infer=*/m_in_expr, nullptr);
                }
                DEBUG("- " << ty);
            }

            if (!data_visited) {
                visit_type_data(data);
            }
            ty = m_crate.m_types.intern(mv$(data));
        }

        void visit_type_impl(::HIR::TypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.m_type << " - from " << impl.m_src_module);
            auto _ = this->m_ms.set_impl_generics(impl.m_params);

            auto mod_ip = ::HIR::ItemPath(impl.m_src_module);
            const auto* mod = (impl.m_src_module != ::HIR::SimplePath() ? &this->m_ms.m_crate.get_mod_by_path(Span(), impl.m_src_module) : nullptr);
            if (mod) {
                m_ms.push_traits(impl.m_src_module, *mod);
                m_cur_module.ptr = mod;
                m_cur_module.path = &mod_ip;
            }
            ::HIR::Visitor::visit_type_impl(impl);
            if (mod) {
                m_ms.pop_traits(*mod);
            }
        }

        void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
            TRACE_FUNCTION_F("impl " << trait_path << " for " << impl.m_type);
            auto trait_gpath = ::HIR::GenericPath(trait_path, impl.m_trait_args.clone());
            auto _0 = this->m_ms.set_current_trait_impl(impl);
            auto _1 = this->m_ms.set_current_trait(trait_gpath);
            auto _ = this->m_ms.set_impl_generics(impl.m_params);

            auto mod_ip = ::HIR::ItemPath(impl.m_src_module);
            const auto* mod = (impl.m_src_module != ::HIR::SimplePath() ? &this->m_ms.m_crate.get_mod_by_path(Span(), impl.m_src_module) : nullptr);
            if (mod) {
                m_ms.push_traits(impl.m_src_module, *mod);
                m_cur_module.ptr = mod;
                m_cur_module.path = &mod_ip;
            }
            m_ms.m_traits.push_back(::std::make_pair(&trait_path, &this->m_ms.m_crate.get_trait_by_path(Span(), trait_path)));
            ::HIR::Visitor::visit_trait_impl(trait_path, impl);
            m_ms.m_traits.pop_back();
            if (mod) {
                m_ms.pop_traits(*mod);
            }
        }

        void visit_marker_impl(const ::HIR::SimplePath& trait_path, ::HIR::MarkerImpl& impl) override {
            TRACE_FUNCTION_F("impl " << trait_path << " for " << impl.m_type << " { }");
            auto _ = this->m_ms.set_impl_generics(impl.m_params);

            auto mod_ip = ::HIR::ItemPath(impl.m_src_module);
            const auto* mod = (impl.m_src_module != ::HIR::SimplePath() ? &this->m_ms.m_crate.get_mod_by_path(Span(), impl.m_src_module) : nullptr);
            if (mod) {
                m_ms.push_traits(impl.m_src_module, *mod);
                m_cur_module.ptr = mod;
                m_cur_module.path = &mod_ip;
            }
            ::HIR::Visitor::visit_marker_impl(trait_path, impl);
            if (mod) {
                m_ms.pop_traits(*mod);
            }
        }

        void visit_trait(::HIR::ItemPath p, ::HIR::Trait& item) override {
            auto _ = this->m_ms.set_impl_generics(item.m_params);
            ::HIR::Visitor::visit_trait(p, item);
        }

        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override {
            auto _ = this->m_ms.set_impl_generics(item.m_params);
            ::HIR::Visitor::visit_enum(p, item);
        }

        void visit_struct(::HIR::ItemPath p, ::HIR::Struct& item) override {
            auto _ = this->m_ms.set_impl_generics(item.m_params);
            ::HIR::Visitor::visit_struct(p, item);
        }

        void visit_union(::HIR::ItemPath p, ::HIR::Union& item) override {
            auto _ = this->m_ms.set_impl_generics(item.m_params);
            ::HIR::Visitor::visit_union(p, item);
        }

        void visit_function(::HIR::ItemPath p, ::HIR::Function& item) override {
            auto _ = this->m_ms.set_item_generics(item.m_params);
            m_fcn_ptr = &item;

            // Visit arguments
            // - Used to convert `impl Trait` in argument position into generics
            // - Done first so the path in return-position `impl Trait` is valid
            //m_cur_params = &item.m_params;
            //m_cur_params_level = 1;
            for (auto& arg : item.m_args) {
                TRACE_FUNCTION_F("ARG " << arg);
                visit_type(arg.second);
            }
            //m_cur_params = nullptr;

            // Visit return type (populates path for `impl Trait` in return position
            m_fcn_path = &p;
            m_fcn_erased_count = 0;
            {
                TRACE_FUNCTION_F("RET " << item.m_return);
                visit_type(item.m_return);
            }
            m_fcn_path = nullptr;
            m_fcn_ptr = nullptr;

            ::HIR::Visitor::visit_function(p, item);
        }

        void visit_static(::HIR::ItemPath p, ::HIR::Static& item) override {
            //auto _ = this->m_ms.set_item_generics(item.m_params);
            ::HIR::Visitor::visit_static(p, item);
            visit_literal(Span(), item.m_value_res);
        }

        void visit_constant(::HIR::ItemPath p, ::HIR::Constant& item) override {
            auto _ = this->m_ms.set_item_generics(item.m_params);
            ::HIR::Visitor::visit_constant(p, item);
            visit_literal(Span(), item.m_value_res);
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

                void visit_node_ptr(::HIR::ExprNodeP& node_ptr) override {
                    upper_visitor.visit_type(node_ptr->m_res_type);
                    ::HIR::ExprVisitorDef::visit_node_ptr(node_ptr);
                }

                void visit(::HIR::ExprNode_Let& node) override {
                    upper_visitor.visit_type(node.m_type);
                    upper_visitor.visit_pattern(node.m_pattern);
                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNode_Match& node) override {
                    for (auto& arm : node.m_arms) {
                        for (auto& pat : arm.m_patterns) {
                            upper_visitor.visit_pattern(pat);
                        }
                        for (auto& g : arm.m_guards) {
                            upper_visitor.visit_pattern(g.pat);
                        }
                    }
                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNode_PathValue& node) override {
                    upper_visitor.visit_path(node.m_path, ::HIR::Visitor::PathContext::VALUE);
                }

                void visit(::HIR::ExprNode_CallPath& node) override {
                    upper_visitor.visit_path(node.m_path, ::HIR::Visitor::PathContext::VALUE);
                    ::HIR::ExprVisitorDef::visit(node);

                    // #[rustc_legacy_const_generics] - A backwards compatability hack added between 1.39 and 1.54 to be backwards compatible with the x86 intrinsics
                    // - Rewrites some literal arguments into const generics
                    if (auto* e = node.m_path.m_data.opt_Generic()) {
                        auto& fcn = upper_visitor.m_crate.get_function_by_path(node.span(), e->m_path);
                        if (!fcn.m_markings.rustc_legacy_const_generics.empty()) {
                            if (node.m_args.size() == fcn.m_args.size()) {
                                // Acceptable
                            } else if (node.m_args.size() == fcn.m_args.size() + fcn.m_markings.rustc_legacy_const_generics.size()) {
                                for (auto idx : fcn.m_markings.rustc_legacy_const_generics) {
                                    auto& arg_node = node.m_args.at(idx);
                                    assert(arg_node);
                                    // TODO: Check that the expression is a valid const (no locals referenced, no function calls?)
                                    // - Allow: Arithmatic, casts, literals
                                    //if( !dynamic_cast<const HIR::ExprNode_Literal*>(arg_node.get()) )
                                    //    ERROR(arg_node->span(), E0000, "Argument " << idx << " must be a literal for #[rustc_legacy_const_generics] tagged function");
                                    HIR::ExprPtr ep{std::move(arg_node)};
                                    e->m_params.m_values.push_back(HIR::ConstGeneric(std::make_unique<HIR::ConstGeneric_Unevaluated>(std::move(ep))));
                                    // - Visit to ensure that the expr state gets filled
                                    upper_visitor.visit_constgeneric(e->m_params.m_values.back());
                                }
                                auto new_end = std::remove_if(node.m_args.begin(), node.m_args.end(), [](const HIR::ExprNodeP& np) {
                                    return !np;
                                });
                                node.m_args.erase(new_end, node.m_args.end());
                            } else {
                                // Will error downstream
                            }
                        }
                    }
                }

                void visit(::HIR::ExprNode_CallMethod& node) override {
                    upper_visitor.visit_path_params(node.m_params);
                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNode_StructLiteral& node) override {
                    upper_visitor.visit_type_inner(node.m_type, false);

                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNode_ArraySized& node) override {
                    auto& as = node.m_size;
                    if (as.is_Unevaluated()) {
                        upper_visitor.visit_constgeneric(as.as_Unevaluated());
                    }
                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNode_Closure& node) override {
                    upper_visitor.visit_type(node.m_return);
                    for (auto& arg : node.m_args) {
                        upper_visitor.visit_pattern(arg.first);
                        upper_visitor.visit_type(arg.second);
                    }
                    ::HIR::ExprVisitorDef::visit(node);
                }
            };

            for (auto& ty : expr.m_erased_types) {
                visit_type(ty);
            }

            // Set up the module state
            {
                expr.m_state = ::HIR::ExprStatePtr(m_crate.m_pool, ::HIR::ExprState(m_crate.m_types, *m_cur_module.ptr, m_cur_module.path->get_simple_path()));
                expr.m_state->m_traits = m_ms.m_traits; // TODO: Only obtain the current module's set
                expr.m_state->m_impl_generics = m_ms.m_impl_generics;
                expr.m_state->m_item_generics = m_ms.m_item_generics;
                expr.m_state->m_current_trait_impl = m_ms.m_current_trait_impl;
                if (m_ms.m_current_trait) {
                    expr.m_state->m_current_trait_path = m_ms.m_current_trait->m_path;
                }
            }

            // Local expression
            if (expr.get() != nullptr) {
                // TODO: Disable type param defaults for this scope
                this->m_in_expr++;

                ExprVisitor v{*this};
                (*expr).visit(v);

                this->m_in_expr--;
            }
            // External expression (has MIR)
            else if (auto* mir = expr.get_ext_mir_mut()) {
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
                        if (lv.m_root.is_Static()) {
                            upper_visitor.visit_path(lv.m_root.as_Static(), ::HIR::Visitor::PathContext::VALUE);
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

    class Visitor_EnumSuperTraits: public ::HIR::Visitor {
        const ::HIR::Crate& m_crate;

    public:
        Visitor_EnumSuperTraits(const ::HIR::Crate& m_crate)
            : ::HIR::Visitor(nullptr, m_crate.m_types)
            , m_crate(m_crate)
        {
        }

        void visit_trait(::HIR::ItemPath ip, ::HIR::Trait& tr) override {
            static Span sp;
            TRACE_FUNCTION_F(ip);
            const auto ty_self = m_crate.m_types.self();

            // Enumerate supertraits and save for later stages
            struct Enumerate {
                HIR::TypeInterner& types;
                HIR::TypeRef ty_self;
                ::std::vector<::HIR::TraitPath> supertraits;
                ::std::vector<const ::HIR::TraitPath*> tp_stack;

                Enumerate(HIR::TypeInterner& types, HIR::TypeRef ty_self)
                    : types(types), ty_self(ty_self) {}

                void enum_supertraits_in(const ::HIR::Trait& tr, ::HIR::TraitPath path) {
                    TRACE_FUNCTION_F(path);
                    tp_stack.push_back(&path);
                    auto& params = path.m_path.m_params;

                    // Fill defaulted parameters.
                    // NOTE: Doesn't do much error checking.
                    fix_param_count(types, sp, path.m_path, tr.m_params, path.m_path.m_params, false, &ty_self);

                    auto monomorph_cb = MonomorphStatePtr(types, &ty_self, &params, nullptr);
                    auto monomorph_tp = [&](const HIR::TraitPath& tp) -> HIR::TraitPath {
                        // TODO: if `path.m_path` has HRLs, then this needs HRLs (only if the HRLs get used?)
                        if ((tp.m_hrtbs && !tp.m_hrtbs->is_empty()) && (path.m_hrtbs && !path.m_hrtbs->is_empty())) {
                            // TODO: How to determine which to use?
                            // - May need to combine them.
                            TODO(sp, "Trait path and outer path both have HRLs, how to handle?");
                            return monomorph_cb.monomorph_traitpath(sp, tp, false);
                        } else if (path.m_hrtbs && !path.m_hrtbs->is_empty()) {
                            auto rv = monomorph_cb.monomorph_traitpath(sp, tp, false);
                            rv.m_hrtbs = box$(path.m_hrtbs->clone());
                            return rv;
                        } else {
                            return monomorph_cb.monomorph_traitpath(sp, tp, false);
                        }
                    };
                    if (tr.m_all_parent_traits.size() > 0) {
                        for (const auto& pt : tr.m_all_parent_traits) {
                            supertraits.push_back(monomorph_tp(pt));
                            fill_type_aliases(supertraits.back());
                        }
                    } else {
                        // Recurse into parent traits
                        for (const auto& pt : tr.m_parent_traits) {
                            enum_supertraits_in(*pt.m_trait_ptr, monomorph_tp(pt));
                        }
                        // - Bound parent traits
                        for (const auto& b : tr.m_params.m_bounds) {
                            if (!b.is_TraitBound()) {
                                continue;
                            }
                            const auto& be = b.as_TraitBound();
                            if (be.type != ty_self) {
                                continue;
                            }
                            const auto& pt = be.trait;
                            if (pt.m_path.m_path == path.m_path.m_path) {
                                continue;
                            }

                            enum_supertraits_in(*pt.m_trait_ptr, monomorph_tp(pt));
                        }
                    }

                    // Build output path.
                    ::HIR::TraitPath out_path;
                    out_path.m_hrtbs = mv$(path.m_hrtbs);
                    out_path.m_path = mv$(path.m_path);
                    out_path.m_trait_ptr = &tr;
                    fill_type_aliases(out_path);
                    // TODO: HRLs?
                    supertraits.push_back(std::move(out_path));
                    // Fill aliases from this path too
                    for (auto& st : supertraits) {
                        for (auto& tb : path.m_type_bounds) {
                            if (tb.second.source_trait == st.m_path) {
                                DEBUG("Add TypeBound: " << tb.first << " = " << tb.second.type);
                                st.m_type_bounds.insert(std::make_pair(tb.first, std::move(tb.second)));
                            }
                        }
                        for (auto& tb : path.m_trait_bounds) {
                            if (tb.second.source_trait == st.m_path) {
                                DEBUG("Add TraitBound: " << tb.first << ": " << tb.second.traits);
                                st.m_trait_bounds.insert(std::make_pair(tb.first, std::move(tb.second)));
                            }
                        }
                    }
                    tp_stack.pop_back();
                }

                void fill_type_aliases(HIR::TraitPath& out_path) const {
                    const HIR::Trait& tr = *out_path.m_trait_ptr;
                    // - Locate associated types for this trait
                    for (const auto& ty : tr.m_types) {
                        if (out_path.m_type_bounds.count(ty.first) == 0) {
                            const HIR::TypeRef* found = nullptr;

                            for (auto oit = tp_stack.rbegin(); oit != tp_stack.rend(); ++oit) {
                                auto it = (*oit)->m_type_bounds.find(ty.first);
                                if (it != (*oit)->m_type_bounds.end()) {
                                    // TODO: Check the source trait
                                    found = &it->second.type;
                                    break;
                                }
                            }
                            // TODO: What if there's multiple?
                            DEBUG(ty.first << " = " << (found ? *found : nullptr));

                            if (found) {
                                out_path.m_type_bounds.insert(::std::make_pair(ty.first, ::HIR::TraitPath::AtyEqual{out_path.m_path.clone(), {}, *found}));
                            }
                        }

                        if (out_path.m_trait_bounds.count(ty.first) == 0) {
                            std::vector<HIR::TraitPath> traits;
                            for (auto oit = tp_stack.rbegin(); oit != tp_stack.rend(); ++oit) {
                                auto it = (*oit)->m_trait_bounds.find(ty.first);
                                if (it != (*oit)->m_trait_bounds.end()) {
                                    // TODO: Check the source trait
                                    for (const auto& t : it->second.traits) {
                                        traits.push_back(t.clone());
                                    }
                                }
                            }
                            DEBUG(ty.first << ": " << traits);
                            if (!traits.empty()) {
                                out_path.m_trait_bounds.insert(::std::make_pair(ty.first, ::HIR::TraitPath::AtyBound{out_path.m_path.clone(), {}, mv$(traits)}));
                            }
                        }
                    }
                }
            };

            auto this_path = ip.get_simple_path();
            this_path.update_crate_name(m_crate.m_crate_name);

            Enumerate e{m_crate.m_types, ty_self};
            for (const auto& pt : tr.m_parent_traits) {
                e.enum_supertraits_in(*pt.m_trait_ptr, pt.clone());
            }
            for (const auto& b : tr.m_params.m_bounds) {
                if (!b.is_TraitBound()) {
                    continue;
                }
                const auto& be = b.as_TraitBound();
                if (be.type != ty_self) {
                    continue;
                }
                const auto& pt = be.trait;

                // TODO: Remove this along with the from_ast.cpp hack
                if (pt.m_path.m_path == this_path) {
                    // TODO: Should this restrict based on the parameters
                    continue;
                }

                e.enum_supertraits_in(*be.trait.m_trait_ptr, be.trait.clone());
            }

            ::std::sort(e.supertraits.begin(), e.supertraits.end());
            DEBUG("supertraits = " << e.supertraits);
            if (e.supertraits.size() > 0) {
                bool dedeup_done = false;
                auto prev = e.supertraits.begin();
                for (auto it = e.supertraits.begin() + 1; it != e.supertraits.end();) {
                    if (prev->m_path == it->m_path) {
                        DEBUG("MERGE:");
                        DEBUG("- " << *prev);
                        DEBUG("- " << *it);
                        for (auto& e : it->m_type_bounds) {
                            if (prev->m_type_bounds.count(e.first)) {
                                ASSERT_BUG(sp, prev->m_type_bounds[e.first].type == e.second.type, "TODO: Handle mismatched type bounds in merging supertrait ATY bounds: " << e.first << " =\n " << prev->m_type_bounds[e.first] << "\n " << e.second.type);
                            }
                            prev->m_type_bounds.insert(std::move(e));
                        }
                        for (auto& e : it->m_trait_bounds) {
                            if (prev->m_trait_bounds.count(e.first)) {
                                TODO(sp, "Merge trait bounds (and make sure to check the source trait)");
                            }
                            prev->m_trait_bounds.insert(std::move(e));
                        }
                        DEBUG("= " << *prev);
                        it = e.supertraits.erase(it);
                        dedeup_done = true;
                    } else {
                        ++it;
                        ++prev;
                    }
                }
                if (dedeup_done) {
                    DEBUG("supertraits dd = " << e.supertraits);
                }
            }
            tr.m_all_parent_traits = std::move(e.supertraits);
        }
    };

    class Visitor_Post: public ::HIR::Visitor {
        const ::HIR::Crate& m_crate;

        typeck::ModuleState m_ms;

    public:
        Visitor_Post(const ::HIR::Crate& crate)
            : ::HIR::Visitor(nullptr, crate.m_types)
            , m_crate(crate)
            , m_ms(crate)
        {
        }


        HIR::TypeInterner& interner() const { return m_crate.m_types; }

        void visit_module(::HIR::ItemPath p, ::HIR::Module& mod) override {
            m_ms.push_traits(p, mod);
            ::HIR::Visitor::visit_module(p, mod);
            m_ms.pop_traits(mod);
        }

        void visit_type(::HIR::TypeRef& ty) override {
            visit_type_inner(ty);
        }

        void visit_type_inner(::HIR::TypeRef& ty, bool do_bind = true) {
            //TRACE_FUNCTION_F(ty);
            static Span sp;

            auto data = ty->clone_data();
            if (auto* te = data.opt_NamedFunction()) {
                if (te->def.is_Function() && te->def.as_Function() == nullptr) {
                    StaticTraitResolve resolve{m_crate};
                    resolve.set_both_generics_raw(m_ms.m_impl_generics, m_ms.m_item_generics);
                    MonomorphState unused_ms(m_crate.m_types);
                    const auto& v = resolve.get_value(sp, te->path, unused_ms, true);

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
                            te->def = ::HIR::TypeData_NamedFunction_Ty::make_EnumConstructor({e.e, e.v});
                        }
                    }
                }
            }

            visit_type_data(data);
            ty = m_crate.m_types.intern(mv$(data));
        }

        void visit_type_impl(::HIR::TypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.m_type << " - from " << impl.m_src_module);
            auto _ = this->m_ms.set_impl_generics(impl.m_params);

            const auto* mod = (impl.m_src_module != ::HIR::SimplePath() ? &this->m_ms.m_crate.get_mod_by_path(Span(), impl.m_src_module) : nullptr);
            if (mod) {
                m_ms.push_traits(impl.m_src_module, *mod);
            }
            ::HIR::Visitor::visit_type_impl(impl);
            if (mod) {
                m_ms.pop_traits(*mod);
            }
        }

        void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
            TRACE_FUNCTION_F("impl " << trait_path << " for " << impl.m_type);
            auto _ = this->m_ms.set_impl_generics(impl.m_params);

            const auto* mod = (impl.m_src_module != ::HIR::SimplePath() ? &this->m_ms.m_crate.get_mod_by_path(Span(), impl.m_src_module) : nullptr);
            if (mod) {
                m_ms.push_traits(impl.m_src_module, *mod);
            }
            m_ms.m_traits.push_back(::std::make_pair(&trait_path, &this->m_ms.m_crate.get_trait_by_path(Span(), trait_path)));
            ::HIR::Visitor::visit_trait_impl(trait_path, impl);
            m_ms.m_traits.pop_back();
            if (mod) {
                m_ms.pop_traits(*mod);
            }
        }

        void visit_marker_impl(const ::HIR::SimplePath& trait_path, ::HIR::MarkerImpl& impl) override {
            TRACE_FUNCTION_F("impl " << trait_path << " for " << impl.m_type << " { }");
            auto _ = this->m_ms.set_impl_generics(impl.m_params);

            const auto* mod = (impl.m_src_module != ::HIR::SimplePath() ? &this->m_ms.m_crate.get_mod_by_path(Span(), impl.m_src_module) : nullptr);
            if (mod) {
                m_ms.push_traits(impl.m_src_module, *mod);
            }
            ::HIR::Visitor::visit_marker_impl(trait_path, impl);
            if (mod) {
                m_ms.pop_traits(*mod);
            }
        }

        void visit_trait(::HIR::ItemPath p, ::HIR::Trait& item) override {
            auto _ = this->m_ms.set_impl_generics(item.m_params);
            ::HIR::Visitor::visit_trait(p, item);
        }

        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override {
            auto _ = this->m_ms.set_impl_generics(item.m_params);
            ::HIR::Visitor::visit_enum(p, item);
        }

        void visit_struct(::HIR::ItemPath p, ::HIR::Struct& item) override {
            auto _ = this->m_ms.set_impl_generics(item.m_params);
            ::HIR::Visitor::visit_struct(p, item);
        }

        void visit_union(::HIR::ItemPath p, ::HIR::Union& item) override {
            auto _ = this->m_ms.set_impl_generics(item.m_params);
            ::HIR::Visitor::visit_union(p, item);
        }

        void visit_function(::HIR::ItemPath p, ::HIR::Function& item) override {
            auto _ = this->m_ms.set_item_generics(item.m_params);
            ::HIR::Visitor::visit_function(p, item);
        }

        void visit_static(::HIR::ItemPath p, ::HIR::Static& item) override {
            //auto _ = this->m_ms.set_item_generics(item.m_params);
            ::HIR::Visitor::visit_static(p, item);
        }

        void visit_constant(::HIR::ItemPath p, ::HIR::Constant& item) override {
            auto _ = this->m_ms.set_item_generics(item.m_params);
            ::HIR::Visitor::visit_constant(p, item);
        }

        // Actual expressions
        void visit_expr(::HIR::ExprPtr& expr) override {
            struct ExprVisitor: public ::HIR::ExprVisitorDef {
                Visitor_Post& upper_visitor;

                ExprVisitor(Visitor_Post& uv)
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

                void visit_node_ptr(::HIR::ExprNodeP& node_ptr) override {
                    upper_visitor.visit_type(node_ptr->m_res_type);
                    ::HIR::ExprVisitorDef::visit_node_ptr(node_ptr);
                }

                void visit(::HIR::ExprNode_Let& node) override {
                    upper_visitor.visit_type(node.m_type);
                    upper_visitor.visit_pattern(node.m_pattern);
                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNode_Match& node) override {
                    for (auto& arm : node.m_arms) {
                        for (auto& pat : arm.m_patterns) {
                            upper_visitor.visit_pattern(pat);
                        }
                        for (auto& g : arm.m_guards) {
                            upper_visitor.visit_pattern(g.pat);
                        }
                    }
                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNode_PathValue& node) override {
                    upper_visitor.visit_path(node.m_path, ::HIR::Visitor::PathContext::VALUE);
                }

                void visit(::HIR::ExprNode_CallPath& node) override {
                    upper_visitor.visit_path(node.m_path, ::HIR::Visitor::PathContext::VALUE);
                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNode_CallMethod& node) override {
                    upper_visitor.visit_path_params(node.m_params);
                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNode_StructLiteral& node) override {
                    upper_visitor.visit_type_inner(node.m_type, false);

                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNode_ArraySized& node) override {
                    auto& as = node.m_size;
                    if (as.is_Unevaluated()) {
                        upper_visitor.visit_constgeneric(as.as_Unevaluated());
                    }
                    ::HIR::ExprVisitorDef::visit(node);
                }

                void visit(::HIR::ExprNode_Closure& node) override {
                    upper_visitor.visit_type(node.m_return);
                    for (auto& arg : node.m_args) {
                        upper_visitor.visit_pattern(arg.first);
                        upper_visitor.visit_type(arg.second);
                    }
                    ::HIR::ExprVisitorDef::visit(node);
                }
            };

            for (auto& ty : expr.m_erased_types) {
                visit_type(ty);
            }

            // Local expression
            if (expr.get() != nullptr) {
                ExprVisitor v{*this};
                (*expr).visit(v);
            }
            // External expression (has MIR)
            else if (auto* mir = expr.get_ext_mir_mut()) {
                for (auto& ty : mir->locals) {
                    this->visit_type(ty);
                }

                struct MirVisitor: public ::MIR::visit::VisitorMut {
                    Visitor_Post& upper_visitor;

                    MirVisitor(Visitor_Post& upper_visitor)
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
                        if (lv.m_root.is_Static()) {
                            upper_visitor.visit_path(lv.m_root.as_Static(), ::HIR::Visitor::PathContext::VALUE);
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

void ConvertHIR_Bind(::HIR::Crate& crate) {
    {
        BindVisitor exp{crate};
        // Also visit extern crates to update their pointers
        for (auto& ec : crate.m_ext_crates) {
            exp.visit_crate(*ec.second.m_data);
        }
        exp.visit_crate(crate);
    }

    {
        Visitor_Post v{crate};
        for (auto& ec : crate.m_ext_crates) {
            v.visit_crate(*ec.second.m_data);
        }
        v.visit_crate(crate);
    }

    // Populate supertrait list
    Visitor_EnumSuperTraits(crate).visit_crate(crate);
}

#include "hir_conv_main_bindings.h"
#include "hir_hir.h"
#include "hir_expr.h"
#include "hir_visitor.h"
#include "hir_typeck_common.h" // monomorphise_type_with

HIR::PathParams ConvertHIR_CompleteAliasParams(HIR::TypeInterner& types, const Span& sp, const ::HIR::GenericParams& params_def, const ::HIR::GenericPath& path, bool is_expr) {
    auto pp = path.m_params.clone();

    // Empty list, fill with ivars
    if (is_expr && pp.m_types.empty()) {
        while (pp.m_types.size() < params_def.m_types.size()) {
            pp.m_types.push_back(types.infer());
        }
    }
    if (is_expr && pp.m_values.empty()) {
        pp.m_values.resize(params_def.m_values.size());
    }

    // Shouldn't this error out if not in an expression?
    if (pp.m_lifetimes.empty()) {
        pp.m_lifetimes.resize(params_def.m_lifetimes.size());
    }
    if (pp.m_lifetimes.size() != params_def.m_lifetimes.size()) {
        ERROR(sp, E0000, "Mismatched lifetime-generic count in " << path
            << ", expected " << params_def.m_lifetimes.size() << " got " << pp.m_lifetimes.size());
    }

    pp.m_types.reserve(params_def.m_types.size());
    while (pp.m_types.size() < params_def.m_types.size() && params_def.m_types[pp.m_types.size()].m_default != ::HIR::TypeRef()) {
        auto monomorph = MonomorphStatePtr(types, nullptr, &pp, nullptr);
        pp.m_types.push_back(monomorph.monomorph_type(sp, params_def.m_types[pp.m_types.size()].m_default));
    }
    if (pp.m_types.size() != params_def.m_types.size()) {
        ERROR(sp, E0000, "Mismatched type-generic count in " << path << ", expected " << params_def.m_types.size() << " got " << pp.m_types.size());
    }

    pp.m_values.reserve(params_def.m_values.size());
    while (pp.m_values.size() < params_def.m_values.size() && !params_def.m_values[pp.m_values.size()].m_default.is_Infer()) {
        auto monomorph = MonomorphStatePtr(types, nullptr, &pp, nullptr);
        pp.m_values.push_back(monomorph.monomorph_constgeneric(sp, params_def.m_values[pp.m_values.size()].m_default, false));
    }
    if (pp.m_values.size() != params_def.m_values.size()) {
        ERROR(sp, E0000, "Mismatched const-generic count in " << path << ", expected " << params_def.m_values.size() << " got " << pp.m_values.size());
    }

    return pp;
}

::HIR::TypeRef ConvertHIR_ExpandAliases_GetExpansion_GP(const Span& sp, const ::HIR::Crate& crate, const ::HIR::GenericPath& path, bool is_expr) {
    const auto& ti = crate.get_typeitem_by_path(sp, path.m_path);
    if (const auto* ep = ti.opt_TypeAlias()) {
        const auto& ta = *ep;
        DEBUG(path << " -> type " << ta.m_params.fmt_args() << " = " << ta.m_type);
        auto pp = ConvertHIR_CompleteAliasParams(crate.m_types, sp, ta.m_params, path, is_expr);
        // Monomorphise the exapnded type using the created params
        auto ms = MonomorphStatePtr(crate.m_types, nullptr, &pp, nullptr);
        HIR::TypeRef rv = ms.monomorph_type(sp, ta.m_type);
        DEBUG(path << " -> " << path.m_path << pp << " -> " << rv);
        return rv;
    }
    return crate.m_types.infer();
}

::HIR::TypeRef ConvertHIR_ExpandAliases_GetExpansion(const ::HIR::Crate& crate, const ::HIR::Path& path, bool is_expr) {
    static Span sp;
    TU_MATCH(::HIR::Path::Data, (path.m_data), (e), (Generic, return ConvertHIR_ExpandAliases_GetExpansion_GP(sp, crate, e, is_expr);), (UfcsInherent, DEBUG("TODO: Locate impl blocks for types - path=" << path);), (UfcsKnown, DEBUG("TODO: Locate impl blocks for traits on types - path=" << path);), (UfcsUnknown, DEBUG("TODO: Locate impl blocks for traits on types - path=" << path);))
    return crate.m_types.infer();
}

std::vector<HIR::TraitPath> ConvertHIR_ExpandAliases_GetTraitExpansion_GP(const Span& sp, const ::HIR::Crate& crate, const HIR::GenericPath& path, bool is_expr) {
    const auto& ti = crate.get_typeitem_by_path(sp, path.m_path);
    if (const auto* ep = ti.opt_TraitAlias()) {
        const auto& ta = *ep;
        auto pp = ConvertHIR_CompleteAliasParams(crate.m_types, sp, ta.m_params, path, is_expr);
        auto ms = MonomorphStatePtr(crate.m_types, nullptr, &pp, nullptr);
        std::vector<HIR::TraitPath> rv;
        rv.reserve(ta.m_traits.size());
        for (const auto& exp : ta.m_traits) {
            rv.push_back(ms.monomorph_traitpath(sp, exp, false));
        }
        DEBUG(path << "\n -> " << path.m_path << pp << "\n -> {" << rv << "}");
        return rv;
    } else {
        return std::vector<HIR::TraitPath>();
    }
}

std::vector<HIR::TraitPath> ConvertHIR_ExpandAliases_GetTraitExpansion(const Span& sp, const ::HIR::Crate& crate, /*const*/ HIR::TraitPath& path, bool is_expr) {
    auto rv = ConvertHIR_ExpandAliases_GetTraitExpansion_GP(sp, crate, path.m_path, is_expr);
    if (!rv.empty()) {
        if (!path.m_trait_bounds.empty() || !path.m_type_bounds.empty()) {
            struct H {
                static bool contains_trait(const Span& sp, const HIR::Crate& crate, const HIR::GenericPath& path, const HIR::GenericPath& des_path) {
                    if (path.m_path == des_path.m_path) {
                        return true;
                    }
                    const auto& ti = crate.get_typeitem_by_path(sp, path.m_path);
                    if (const auto* t = ti.opt_Trait()) {
                        for (const auto& pt : t->m_parent_traits) {
                            if (contains_trait(sp, crate, pt.m_path, des_path)) {
                                return true;
                            }
                        }
                    } else if (const auto* t = ti.opt_TraitAlias()) {
                        for (const auto& pt : t->m_traits) {
                            if (contains_trait(sp, crate, pt.m_path, des_path)) {
                                return true;
                            }
                        }
                    } else {
                        BUG(sp, "Not a trait path " << path << ": " << ti.tag_str());
                    }
                    return false;
                }

                static HIR::TraitPath& find_entry(const Span& sp, const HIR::Crate& crate, const HIR::GenericPath& des_path, ::std::vector<::HIR::TraitPath>& rv) {
                    for (auto& p : rv) {
                        if (contains_trait(sp, crate, p.m_path, des_path)) {
                            return p;
                        }
                    }
                    BUG(sp, "Unable to find a trait in expansion list for " << des_path);
                }
            };

            for (auto& tb : path.m_trait_bounds) {
                auto& e = H::find_entry(sp, crate, tb.second.source_trait, rv);
                e.m_trait_bounds.insert(std::make_pair(tb.first, std::move(tb.second)));
            }
            for (auto& tb : path.m_type_bounds) {
                auto& e = H::find_entry(sp, crate, tb.second.source_trait, rv);
                e.m_type_bounds.insert(std::make_pair(tb.first, std::move(tb.second)));
            }
        }
    }
    return rv;
}

class Expander: public ::HIR::Visitor {
    const ::HIR::Crate& m_crate;
    bool m_in_expr = false;
    const ::HIR::TypeRef* m_impl_type = nullptr;

public:
    Expander(const ::HIR::Crate& crate)
        : ::HIR::Visitor(nullptr, crate.m_types)
        , m_crate(crate)
    {
    }

    HIR::TypeInterner& interner() const { return m_crate.m_types; }

    void expand_trait_list(const Span& sp, ::std::vector<HIR::TraitPath>& list) {
        for (auto it = list.begin(); it != list.end(); ++it) {
            auto n = ConvertHIR_ExpandAliases_GetTraitExpansion(sp, m_crate, *it, m_in_expr);
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
            auto data = ty->clone_data();
            if (auto* e = data.opt_ErasedType()) {
                expand_trait_list(sp, e->m_traits);
            } else if (auto* e = data.opt_TraitObject(); e->m_trait.m_path != HIR::SimplePath()) {
                auto n = ConvertHIR_ExpandAliases_GetTraitExpansion(sp, m_crate, e->m_trait, m_in_expr);
                if (n.size() > 0) {
                    TODO(sp, "Expand trait alias in TraitObject? (markers only) - " << e->m_trait);
                }
            }
            ty = m_crate.m_types.intern(std::move(data));
        }

        ::HIR::Visitor::visit_type(ty);

        if (const auto* e = ty->opt_Path()) {
            ::HIR::TypeRef new_type = ConvertHIR_ExpandAliases_GetExpansion(m_crate, e->path, m_in_expr);
            // Keep trying to expand down the chain
            unsigned int num_exp = 1;
            const unsigned int MAX_RECURSIVE_TYPE_EXPANSIONS = 100;
            while (num_exp < MAX_RECURSIVE_TYPE_EXPANSIONS) {
                // NOTE: inner recurses
                ::HIR::Visitor::visit_type(new_type);
                if (const auto* e = new_type->opt_Path()) {
                    auto nt = ConvertHIR_ExpandAliases_GetExpansion(m_crate, e->path, m_in_expr);
                    if (nt->is_Infer()) {
                        break;
                    }
                    num_exp++;
                    new_type = mv$(nt);
                } else {
                    break;
                }
            }
            ASSERT_BUG(sp, num_exp < MAX_RECURSIVE_TYPE_EXPANSIONS, "Recursion limit hit expanding " << ty << " (currently on " << new_type << ")");
            if (!new_type->is_Infer()) {
                DEBUG("Replacing " << ty << " with " << new_type << " (" << num_exp << " expansions)");
                ty = mv$(new_type);
            }
        }
    }

    void visit_trait_path(::HIR::TraitPath& tp) override {
        static Span sp;
        // 1. Make sure that the trait path isn't pointing at an alias (should have been handled by the caller, which can expand to multiple items)
        ASSERT_BUG(sp, m_crate.get_typeitem_by_path(sp, tp.m_path.m_path).is_Trait(), "Bad trait path - " << tp.m_path << " : " << m_crate.get_typeitem_by_path(sp, tp.m_path.m_path).tag_str());
        // 2. Handle AtyBounds
        for (auto& tb : tp.m_trait_bounds) {
            expand_trait_list(sp, tb.second.traits);
        }

        // Finally. Recurse
        ::HIR::Visitor::visit_trait_path(tp);
    }

    ::HIR::Path expand_alias_path(const Span& sp, const ::HIR::Path& path) {
        const unsigned int MAX_RECURSIVE_TYPE_EXPANSIONS = 100;

        // If the path is already generic and points at an enum variant, skip
        if (path.m_data.is_Generic()) {
            const auto& gp = path.m_data.as_Generic();
            if (gp.m_path.components().size() > 1 && m_crate.get_typeitem_by_path(sp, gp.m_path, /*igncrate*/ false, /*ignlast*/ true).is_Enum()) {
                return ::HIR::GenericPath();
            }
        }

        ::HIR::Path rv = ::HIR::GenericPath();
        const auto* cur = &path;

        unsigned int num_exp = 0;
        do {
            auto ty = ConvertHIR_ExpandAliases_GetExpansion(m_crate, *cur, m_in_expr);
            if (ty->is_Infer()) {
                break;
            }
            if (!ty->is_Path()) {
                ERROR(sp, E0000, "Type alias referenced in generic path doesn't point to a path");
            }
            rv = ty->as_Path().path.clone();

            this->visit_path(rv, ::HIR::Visitor::PathContext::TYPE);

            cur = &rv;
        } while (++num_exp < MAX_RECURSIVE_TYPE_EXPANSIONS);
        ASSERT_BUG(sp, num_exp < MAX_RECURSIVE_TYPE_EXPANSIONS, "Recursion limit expanding " << path << " (currently on " << *cur << ")");
        return mv$(rv);
    }

    ::HIR::Pattern::PathBinding visit_pattern_PathBinding(const Span& sp, ::HIR::Path& path) {
        auto resize_type_params = [&](::HIR::PathParams& params, size_t size) {
            if (params.m_types.size() > size) {
                params.m_types.resize(size);
            }
            while (params.m_types.size() < size) {
                params.m_types.push_back(m_crate.m_types.infer());
            }
        };

        if (path.m_data.is_UfcsUnknown()) {
            const auto& ty = path.m_data.as_UfcsUnknown().type;
            const auto& name = path.m_data.as_UfcsUnknown().item;

            const HIR::GenericPath* gp_p;
            if (ty->is_Generic() && ty->as_Generic().binding == GENERIC_Self) {
                if (!m_impl_type) {
                    ERROR(sp, E0000, "Use of `Self` pattern outside of an impl block");
                }
                if (!TU_TEST1((**m_impl_type), Path, .path.m_data.is_Generic())) {
                    ERROR(sp, E0000, "Use of `Self` pattern in non-struct impl block - " << *m_impl_type);
                }
                gp_p = &(*m_impl_type)->as_Path().path.m_data.as_Generic();
            } else {
                if (ty->is_Generic()) {
                    return ::HIR::Pattern::PathBinding();
                }
                if (!ty->is_Path()) {
                    ERROR(sp, E0000, "Expeted path in pattern binding, got " << ty);
                }
                if (!ty->as_Path().path.m_data.is_Generic()) {
                    ERROR(sp, E0000, "Expeted generic path in pattern binding, got " << ty);
                }
                gp_p = &ty->as_Path().path.m_data.as_Generic();
            }
            const auto& gp = *gp_p;
            const auto& ti = m_crate.get_typeitem_by_path(sp, gp.m_path);
            if (!ti.is_Enum()) {
                ERROR(sp, E0000, "Expeted enum path in pattern binding, got " << ti.tag_str());
            }
            const auto& enm = ti.as_Enum();

            auto gp2 = gp.clone();
            gp2.m_path += name;
            gp2.m_params.m_lifetimes.resize(enm.m_params.m_lifetimes.size());
            resize_type_params(gp2.m_params, enm.m_params.m_types.size());
            gp2.m_params.m_values.resize(enm.m_params.m_values.size());

            auto idx = enm.find_variant(name);
            if (idx == ~0u) {
                TODO(sp, "Variant " << name << " not found in " << gp);
            }
            path = std::move(gp2);
            return ::HIR::Pattern::PathBinding::make_Enum({&enm, static_cast<unsigned>(idx)});
        }
        // `Self { ... }` patterns - Encoded as `<Self>::`
        if (path.m_data.is_UfcsInherent()) {
            const auto& ty = path.m_data.as_UfcsInherent().type;
            const auto& name = path.m_data.as_UfcsInherent().item;
            ASSERT_BUG(sp, ty->is_Generic() && ty->as_Generic().binding == GENERIC_Self, path);
            ASSERT_BUG(sp, name == "", path);
            if (!m_impl_type) {
                ERROR(sp, E0000, "Use of `Self` pattern outside of an impl block");
            }
            if (!TU_TEST1((**m_impl_type), Path, .path.m_data.is_Generic())) {
                ERROR(sp, E0000, "Use of `Self` pattern in non-struct impl block - " << *m_impl_type);
            }
            path = (*m_impl_type)->as_Path().path.m_data.as_Generic().clone();
            // Fall through for the resizing below
        }

        ASSERT_BUG(sp, path.m_data.is_Generic(), path);
        auto& gp = path.m_data.as_Generic();

        // TODO: Better error messages?
        if (gp.m_path.components().size() > 1) {
            const auto& ti = m_crate.get_typeitem_by_path(sp, gp.m_path, false, /*ignore_last*/ true);
            if (ti.is_Enum()) {
                // Enum variant!
                const auto& enm = ti.as_Enum();

                gp.m_params.m_lifetimes.resize(enm.m_params.m_lifetimes.size());
                resize_type_params(gp.m_params, enm.m_params.m_types.size());
                gp.m_params.m_values.resize(enm.m_params.m_values.size());

                auto idx = ti.as_Enum().find_variant(gp.m_path.components().back());
                return ::HIR::Pattern::PathBinding::make_Enum({&enm, static_cast<unsigned>(idx)});
            }
        }

        const auto& ti = m_crate.get_typeitem_by_path(sp, gp.m_path);
        if (ti.is_Union()) {
            const auto& unn = ti.as_Union();

            gp.m_params.m_lifetimes.resize(unn.m_params.m_lifetimes.size());
            resize_type_params(gp.m_params, unn.m_params.m_types.size());
            gp.m_params.m_values.resize(unn.m_params.m_values.size());

            return ::HIR::Pattern::PathBinding::make_Union(&unn);
        }

        ASSERT_BUG(sp, ti.is_Struct(), "Pattern path " << gp.m_path << " didn't point to a struct or union (" << ti.tag_str() << ")");
        const auto& str = ti.as_Struct();

        gp.m_params.m_lifetimes.resize(str.m_params.m_lifetimes.size());
        resize_type_params(gp.m_params, str.m_params.m_types.size());
        gp.m_params.m_values.resize(str.m_params.m_values.size());

        return ::HIR::Pattern::PathBinding::make_Struct(&str);
    }

    void visit_pattern(::HIR::Pattern& pat) override {
        static Span sp;

        ::HIR::Visitor::visit_pattern(pat);

        TU_MATCH_HDRA( (pat.m_data), {)
        default:
            break;
            TU_ARMA(PathValue, e) {
                auto new_path = expand_alias_path(sp, e.path);
                if (new_path != ::HIR::GenericPath()) {
                    DEBUG("Replacing " << e.path << " with " << new_path);
                    e.path = mv$(new_path);
                }
                e.binding = visit_pattern_PathBinding(sp, e.path);
            }
            TU_ARMA(PathTuple, e) {
                auto new_path = expand_alias_path(sp, e.path);
                if (new_path != ::HIR::GenericPath()) {
                    DEBUG("Replacing " << e.path << " with " << new_path);
                    e.path = mv$(new_path);
                }
                e.binding = visit_pattern_PathBinding(sp, e.path);
            }
            TU_ARMA(PathNamed, e) {
                auto new_path = expand_alias_path(sp, e.path);
                if (new_path != ::HIR::GenericPath()) {
                    DEBUG("Replacing " << e.path << " with " << new_path);
                    e.path = mv$(new_path);
                }
                e.binding = visit_pattern_PathBinding(sp, e.path);
                // TODO: If this is an empty/wildcard AND it's poiting at a value/tuple entry, change to PathValue/PathTuple
            }
        }
    }

    void visit_params(::HIR::GenericParams& params) override {
        for (auto it = params.m_bounds.begin(); it != params.m_bounds.end(); ++it) {
            static Span sp;
            if (auto* be = it->opt_TraitBound()) {
                auto n = ConvertHIR_ExpandAliases_GetTraitExpansion(sp, m_crate, be->trait, m_in_expr);
                if (!n.empty()) {
                    auto orig_type = std::move(be->type);
                    auto orig_hrtbs = std::move(be->hrtbs);
                    if (orig_hrtbs) {
                        visit_params(*orig_hrtbs);
                    }
                    visit_type(orig_type);

                    it = params.m_bounds.erase(it);
                    for (auto& t : n) {
                        auto type = orig_type;
                        auto hrtbs = orig_hrtbs ? (&t == &n.back() ? std::move(orig_hrtbs) : box$(orig_hrtbs->clone())) : nullptr;
                        it = params.m_bounds.insert(it, HIR::GenericBound::make_TraitBound({std::move(hrtbs), std::move(type), std::move(t)}));
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
            void visit(::HIR::ExprNode_ArraySized& node) override {
                auto& as = node.m_size;
                if (as.is_Unevaluated() && as.as_Unevaluated().is_Unevaluated()) {
                    upper_visitor.visit_expr(*as.as_Unevaluated().as_Unevaluated()->expr);
                }
                ::HIR::ExprVisitorDef::visit(node);
            }
        };

        if (expr.get() != nullptr) {
            auto old = m_in_expr;
            m_in_expr = true;

            Visitor v{*this};
            (*expr).visit(v);

            m_in_expr = old;
        }
    }

    void visit_trait_alias(::HIR::ItemPath p, ::HIR::TraitAlias& item) override {
        //Span    sp(p);
        expand_trait_list(Span(), item.m_traits);
        ::HIR::Visitor::visit_trait_alias(p, item);
    }

    void visit_trait(::HIR::ItemPath p, ::HIR::Trait& item) override {
        //Span    sp(p);
        expand_trait_list(Span(), item.m_parent_traits);
        ::HIR::Visitor::visit_trait(p, item);
    }

    void visit_associatedtype(::HIR::ItemPath p, ::HIR::AssociatedType& item) override {
        //Span    sp(p);
        expand_trait_list(Span(), item.m_trait_bounds);
        ::HIR::Visitor::visit_associatedtype(p, item);
    }

    void visit_type_impl(::HIR::TypeImpl& impl) override {
        m_impl_type = &impl.m_type;
        ::HIR::Visitor::visit_type_impl(impl);
        m_impl_type = nullptr;
    }

    void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
        static Span sp;
        m_impl_type = &impl.m_type;
        ::HIR::Visitor::visit_trait_impl(trait_path, impl);
        m_impl_type = nullptr;
    }

    void visit_function(HIR::ItemPath p, ::HIR::Function& item) override {
        ::HIR::Visitor::visit_function(p, item);
        if (item.m_receiver == HIR::Function::Receiver::Custom) {
            //DEBUG("Updating reciever from " << item.m_receiver_type << " to " << item.m_args.at(0).second);
            //item.m_receiver_type = item.m_args.at(0).second.clone();
            ASSERT_BUG(Span(), item.m_receiver_type, "Custom receiver without a receiver type");
            this->visit_type(*item.m_receiver_type);
        }
    }
};

class Expander_Self: public ::HIR::Visitor {
    const ::HIR::Crate& m_crate;
    const ::HIR::TypeRef* m_impl_type = nullptr;
    bool m_in_expr = false;

public:
    Expander_Self(const ::HIR::Crate& crate, const ::HIR::TypeRef* impl_type = nullptr)
        : ::HIR::Visitor(nullptr, crate.m_types)
        , m_crate(crate)
        , m_impl_type(impl_type)
    {
    }


    HIR::TypeInterner& interner() const { return m_crate.m_types; }

    void visit_type(::HIR::TypeRef& ty) override {
        ::HIR::Visitor::visit_type(ty);

        if (const auto* te = ty->opt_Generic()) {
            if (te->binding == GENERIC_Self) {
                if (m_impl_type) {
                    DEBUG("Replace Self with " << *m_impl_type);
                    ty = *m_impl_type;
                } else {
                    // NOTE: Valid for `trait` definitions.
                    DEBUG("Self outside of an `impl` block");
                }
            }
        }
    }

    void visit_expr(::HIR::ExprPtr& expr) override {
        struct Visitor: public ::HIR::ExprVisitorDef {
            Expander_Self& upper_visitor;

            Visitor(Expander_Self& uv)
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
            void visit(::HIR::ExprNode_ArraySized& node) override {
                auto& as = node.m_size;
                if (as.is_Unevaluated() && as.as_Unevaluated().is_Unevaluated()) {
                    upper_visitor.visit_expr(*as.as_Unevaluated().as_Unevaluated()->expr);
                }
                ::HIR::ExprVisitorDef::visit(node);
            }
        };

        if (expr.get() != nullptr) {
            auto old = m_in_expr;
            m_in_expr = true;

            Visitor v{*this};
            (*expr).visit(v);

            m_in_expr = old;
        }
    }

    void visit_enum(HIR::ItemPath p, ::HIR::Enum& enm) override {
        HIR::TypeRef ty = m_crate.m_types.path(HIR::GenericPath(p.get_simple_path(), enm.m_params.make_nop_params(m_crate.m_types, 0)), &enm);
        m_impl_type = &ty;
        ::HIR::Visitor::visit_enum(p, enm);
        m_impl_type = nullptr;
    }

    void visit_struct(HIR::ItemPath p, ::HIR::Struct& str) override {
        HIR::TypeRef ty = m_crate.m_types.path(HIR::GenericPath(p.get_simple_path(), str.m_params.make_nop_params(m_crate.m_types, 0)), &str);
        // HACK: If thre is a `#` in the path, it's en enum variant
        if (const auto* n = ::std::strchr(p.name, '#')) {
            if (n != p.name && n[1]) {
                auto path = p.get_simple_path();
                path.update_last_component(RcString::new_interned(p.name, n - p.name));
                const auto& enm = m_crate.get_enum_by_path(Span(), path);
                ty = m_crate.m_types.path(HIR::GenericPath(std::move(path), str.m_params.make_nop_params(m_crate.m_types, 0)), &enm);
            }
        }
        m_impl_type = &ty;
        ::HIR::Visitor::visit_struct(p, str);
        m_impl_type = nullptr;
    }

    void visit_union(HIR::ItemPath p, ::HIR::Union& unn) override {
        HIR::TypeRef ty = m_crate.m_types.path(HIR::GenericPath(p.get_simple_path(), unn.m_params.make_nop_params(m_crate.m_types, 0)), &unn);
        m_impl_type = &ty;
        ::HIR::Visitor::visit_union(p, unn);
        m_impl_type = nullptr;
    }

    void visit_type_impl(::HIR::TypeImpl& impl) override {
        m_impl_type = &impl.m_type;
        ::HIR::Visitor::visit_type_impl(impl);
        m_impl_type = nullptr;
    }

    void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
        static Span sp;
        m_impl_type = &impl.m_type;
        ::HIR::Visitor::visit_trait_impl(trait_path, impl);
        m_impl_type = nullptr;
    }
};

void ConvertHIR_ExpandAliases(::HIR::Crate& crate) {
    Expander exp{crate};
    exp.visit_crate(crate);
}

void ConvertHIR_ExpandAliases_Self(::HIR::Crate& crate) {
    Expander_Self exp{crate};
    exp.visit_crate(crate);
}

void ConvertHIR_ExpandAliases_Self_Expr(
    const ::HIR::Crate& crate,
    const ::HIR::TypeRef& impl_type,
    ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>>& args,
    ::HIR::TypeRef& ret_ty,
    ::HIR::ExprPtr& expr
    )
{
    Expander_Self exp{crate, &impl_type};
    for (auto& arg : args) {
        exp.visit_pattern(arg.first);
        exp.visit_type(arg.second);
    }
    exp.visit_type(ret_ty);
    exp.visit_expr(expr);
}

#include "hir_hir.h"
#include "hir_visitor.h"
#include "hir_typeck_static.h"
#include "hir_expr.h" // ExprVisitor
#include "hir_conv_main_bindings.h"

namespace {
    /// <summary>
    /// A class that acts like StaticTraitResolve, but only holds params
    /// </summary>
    struct MiniResolve {
        const HIR::Crate& m_crate;
        const HIR::TypeRef* m_self_type = nullptr;
        const HIR::GenericParams* m_impl_generics = nullptr;
        const HIR::GenericParams* m_item_generics = nullptr;

        MiniResolve(const HIR::Crate& crate)
            : m_crate(crate)
        {
        }

        NullOnDrop<const ::HIR::GenericParams> set_impl_generics(const ::HIR::GenericParams& gps) {
            m_impl_generics = &gps;
            return NullOnDrop<const ::HIR::GenericParams>(m_impl_generics);
        }

        NullOnDrop<const ::HIR::GenericParams> set_item_generics(const ::HIR::GenericParams& gps) {
            m_item_generics = &gps;
            return NullOnDrop<const ::HIR::GenericParams>(m_item_generics);
        }
    };

    class LifetimeVisitor: public ::HIR::Visitor {
        ::HIR::Crate& crate;
        MiniResolve m_resolve;

        bool m_in_expr = false;
        bool m_create_elided = false;
        ::HIR::GenericParams* m_cur_params = nullptr;
        unsigned m_cur_params_level = 0;
        ::std::vector<const ::HIR::LifetimeRef*> m_current_lifetime;
        /// The type of `Self` if we're in a by-value method
        const ::HIR::TypeRef* m_value_self_type = nullptr;

        unsigned m_current_depth = 0;
        std::vector<std::pair<unsigned, const ::HIR::LifetimeRef*>> m_trait_object_rule;

    public:
        LifetimeVisitor(::HIR::Crate& crate)
            : HIR::Visitor(nullptr, crate.m_types)
            , crate(crate)
            , m_resolve(crate)
        {
        }

    private:
        struct SavedParams {
            LifetimeVisitor* parent;
            bool m_create_elided;
            ::HIR::GenericParams* m_cur_params;
            unsigned m_cur_params_level;

            SavedParams(LifetimeVisitor& parent)
                : parent(&parent)
                , m_create_elided(parent.m_create_elided)
                , m_cur_params(parent.m_cur_params)
                , m_cur_params_level(parent.m_cur_params_level)
            {
            }

            SavedParams(const SavedParams&) = delete;

            SavedParams(SavedParams&& x)
                : parent(x.parent)
                , m_create_elided(x.m_create_elided)
                , m_cur_params(x.m_cur_params)
                , m_cur_params_level(x.m_cur_params_level)
            {
                x.parent = nullptr;
            }

            ~SavedParams() {
                restore();
            }

            void restore() {
                if (parent) {
                    parent->m_create_elided = m_create_elided;
                    parent->m_cur_params = m_cur_params;
                    parent->m_cur_params_level = m_cur_params_level;
                    parent = nullptr;
                }
            }
        };

        SavedParams save_params() {
            return SavedParams(*this);
        }

        void set_params(::HIR::GenericParams* params, unsigned level) {
            m_create_elided = true;
            m_cur_params = params;
            m_cur_params_level = level;
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

    public:
        void visit_lifetime(const Span& sp, HIR::LifetimeRef& lft) {
            if (!lft.is_param()) {
                switch (lft.binding) {
                    case HIR::LifetimeRef::STATIC: // 'static
                        break;
                    case HIR::LifetimeRef::INFER: // '_
                        //TODO(sp, "Handle explicitly elided lifetimes");
                        //break;
                    case HIR::LifetimeRef::UNKNOWN: // <none>
                        // If there's a current liftime (i.e. we're within a borrow), then use that
                        if (!m_current_lifetime.empty() && m_current_lifetime.back()) {
                            lft = *m_current_lifetime.back();
                            DEBUG("Use stack: " << lft);
                        }
                        // Otherwise, try to make a new one
                        else if (m_cur_params && m_create_elided) {
                            auto idx = m_cur_params->m_lifetimes.size();
                            m_cur_params->m_lifetimes.push_back(HIR::LifetimeDef{RcString::new_interned(FMT("elided#" << idx))});
                            lft.binding = m_cur_params_level * 256 + idx;
                            DEBUG("Create elided lifetime: " << lft << " " << m_cur_params->m_lifetimes.back().m_name);
                        } else if (m_in_expr) {
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
                if (m_cur_params) {
                    if (!m_current_lifetime.empty() && m_current_lifetime.back() && m_current_lifetime.back()->is_param()) {
                        const auto& outer = *m_current_lifetime.back();
                        //DEBUG("maybe add " << lft << ": " << outer);
                        if (lft != outer && lft.as_param().group() < 2 // I.e. an impl or method param, not HRL or placeholder
                            && outer.as_param().group() < 2
                            // One of the two lifetimes must be from this block?
                            && (lft.as_param().group() == m_cur_params_level || outer.as_param().group() == m_cur_params_level)) {
                            // Add `'this: 'outer`
                            bool found = false;
                            // Only if not a duplicate
                            for (const auto& b : m_cur_params->m_bounds) {
                                if (const auto* be = b.opt_Lifetime()) {
                                    if (be->test == lft && be->valid_for == outer) {
                                        found = true;
                                        break;
                                    }
                                }
                            }
                            if (!found) {
                                DEBUG("Push bound " << lft << ": " << outer);
                                m_cur_params->m_bounds.push_back(::HIR::GenericBound::make_Lifetime({lft, outer}));
                            }
                        }
                    } else {
                        if (m_current_lifetime.empty()) {
                        } else if (m_current_lifetime.back()) {
                            //DEBUG("No bound " << lft << ": " << *m_current_lifetime.back());
                        } else {
                            //DEBUG("No bound " << lft << ": nullptr");
                        }
                    }
                }
            }
        }

        bool bound_exists(const HIR::LifetimeRef& test, const HIR::LifetimeRef& valid_for) const {
            if (m_resolve.m_impl_generics) {
                for (const auto& b : m_resolve.m_impl_generics->m_bounds) {
                    if (b.is_Lifetime()) {
                        DEBUG(b);
                    }
                    if (b.is_Lifetime() && b.as_Lifetime().test == test && b.as_Lifetime().valid_for == valid_for) {
                        return true;
                    }
                }
            }
            if (m_resolve.m_item_generics) {
                for (const auto& b : m_resolve.m_item_generics->m_bounds) {
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
            TRACE_FUNCTION_F(params.fmt_args() << params.fmt_bounds());
            for (auto& tps : params.m_types) {
                this->visit_type(tps.m_default);
            }
            for (auto& val : params.m_values) {
                this->visit_type(val.m_type);
            }
            // The bounds list can grow as inferred lifetime bounds are added, so iterate manually and move the bound in/out to maintain pointer stability
            for (size_t i = 0; i < params.m_bounds.size(); i++) {
                auto bound = std::move(params.m_bounds[i]);
                params.m_bounds[i] = HIR::GenericBound::make_Lifetime({HIR::LifetimeRef::new_static(), HIR::LifetimeRef::new_static()});
                visit_generic_bound(bound);
                params.m_bounds[i] = std::move(bound);
            }
        }

        void visit_generic_path(::HIR::GenericPath& p, ::HIR::Visitor::PathContext pc) override {
            const static Span sp;
            // Get the type definition and fill in omitted lifetimes
            const HIR::GenericParams* gp = nullptr;
            if (p.m_path.components().size() > 1) {
                if (const auto* e = m_resolve.m_crate.get_typeitem_by_path(sp, p.m_path, false, true).opt_Enum()) {
                    gp = &e->m_params;
                }
            }
            if (!gp) {
                switch (pc) {
                    case HIR::Visitor::PathContext::TYPE:
                    case HIR::Visitor::PathContext::TRAIT: {
                        const auto& ti = m_resolve.m_crate.get_typeitem_by_path(sp, p.m_path);
                    TU_MATCH_HDRA( (ti), {)
                    TU_ARMA(Import, e) BUG(sp, "Unexpected reference to import - " << p);
                            TU_ARMA(Module, e) BUG(sp, "Unexpected reference to module - " << p);
                            TU_ARMA(TypeAlias, e) {
                                gp = &e.m_params;
                            }
                            TU_ARMA(TraitAlias, e) {
                                gp = &e.m_params;
                            }
                            TU_ARMA(ExternType, e) {
                                gp = nullptr;
                            }
                            TU_ARMA(Enum, e) {
                                gp = &e.m_params;
                            }
                            TU_ARMA(Struct, e) {
                                gp = &e.m_params;
                            }
                            TU_ARMA(Union, e) {
                                gp = &e.m_params;
                            }
                            TU_ARMA(Trait, e) {
                                gp = &e.m_params;
                            }
                    }
                    } break;
                    case HIR::Visitor::PathContext::VALUE: {
                        const auto& vi = m_resolve.m_crate.get_valitem_by_path(sp, p.m_path);
                    TU_MATCH_HDRA( (vi), { )
                    TU_ARMA(Import, e) BUG(sp, "Unexpected reference to import - " << p);
                            TU_ARMA(Constant, e) {
                                gp = nullptr;
                            }
                            TU_ARMA(Static, e) {
                                gp = nullptr;
                            }
                            TU_ARMA(Function, e) {
                                gp = &e.m_params;
                            }
                            TU_ARMA(StructConstant, e) {
                                gp = &m_resolve.m_crate.get_struct_by_path(sp, e.ty).m_params;
                            }
                            TU_ARMA(StructConstructor, e) {
                                gp = &m_resolve.m_crate.get_struct_by_path(sp, e.ty).m_params;
                            }
                    }
                    } break;
                }
            }
            if (p.m_params.m_lifetimes.size() < (gp ? gp->m_lifetimes.size() : 0) && m_current_lifetime.size() && m_current_lifetime.back()) {
                assert(gp); // Should be non-null because `.size()` is unsigned, and the above is `.size() < 0` if `gp` is null
                DEBUG(p);
                p.m_params.m_lifetimes.resize(gp->m_lifetimes.size());
                DEBUG(p);
            }
            HIR::Visitor::visit_generic_path(p, pc);
        }

        void visit_path_params(::HIR::PathParams& pp) override {
            DEBUG(pp);
            static Span _sp;
            const Span& sp = _sp;

            for (auto& lft : pp.m_lifetimes) {
                visit_lifetime(sp, lft);
            }

            HIR::Visitor::visit_path_params(pp);
        }

        void visit_type(::HIR::TypeRef& ty) override {
            static const HIR::LifetimeRef lft_hrtb = ::HIR::LifetimeRef(HIR::GenericRef(RcString(), HIR::GENERIC_Hrtb, 0).binding);
            static Span _sp;
            const Span& sp = _sp;

            auto saved_m_trait_object_rule = m_trait_object_rule.size();
            auto saved_liftime_depth = m_current_lifetime.size();
            auto saved_params = save_params();
            if (m_current_depth == 0) {
                DEBUG("> " << ty);
            }
            m_current_depth += 1;

            auto data = ty->clone_data();

            // Lifetime elision logic!

            if (auto* e = data.opt_Borrow()) {
                visit_lifetime(sp, e->lifetime);
                m_current_lifetime.push_back(&e->lifetime);
                m_trait_object_rule.push_back(::std::make_pair(m_current_depth, &e->lifetime));
            }
            if (auto* e = data.opt_Function()) {
                m_current_lifetime.push_back(nullptr);
                set_params(&e->hrls, HIR::GENERIC_Hrtb);
                auto saved_create = m_create_elided;
                m_create_elided = true;
                for (auto& t : e->m_arg_types) {
                    this->visit_type(t);
                }
                m_create_elided = false;
                if (e->hrls.m_lifetimes.size() == 1) {
                    m_current_lifetime.pop_back();
                    m_current_lifetime.push_back(&lft_hrtb);
                }
                this->visit_type(e->m_rettype);
                m_create_elided = saved_create;
            }
            if (auto* e = data.opt_TraitObject()) {
                // TODO: Create? but what if it's not used?
                if (e->m_trait.m_hrtbs) {
                    m_current_lifetime.push_back(nullptr);
                    set_params(&*e->m_trait.m_hrtbs, HIR::GENERIC_Hrtb);
                }

                // If neither of those rules apply, then the bounds on the trait are used:
                // - If the trait is defined with a single lifetime bound then that bound is used.
                // - If 'static is used for any lifetime bound then 'static is used.
                // - If the trait has no lifetime bounds, then the lifetime is inferred in expressions and is 'static outside of expressions.
                if (e->m_lifetime.binding == HIR::LifetimeRef::INFER || e->m_lifetime.binding == HIR::LifetimeRef::UNKNOWN) {
                    struct H {
                        const Span& sp;
                        const HIR::Crate& crate;
                        std::vector<HIR::LifetimeRef> lifetimes;

                        void visit_trait(const HIR::SimplePath& p, const HIR::PathParams& params) {
                            const auto& t = crate.get_trait_by_path(sp, p);
                            DEBUG(p << " " << t.m_lifetime);
                            if (t.m_lifetime != HIR::LifetimeRef()) {
                                if (t.m_lifetime == HIR::LifetimeRef::new_static()) {
                                    lifetimes.push_back(t.m_lifetime);
                                    // Early return on 'static, no need to check anything else
                                    return;
                                } else {
                                    // TODO: Parameters
                                }
                            }
                            // TODO: Monomorph? (for lifetime parameters)
                            for (const auto& st : t.m_parent_traits) {
                                visit_trait(st.m_path.m_path, st.m_path.m_params);
                            }
                        }
                    } h{sp, m_resolve.m_crate};

                    if (e->m_trait.m_path.m_path != HIR::SimplePath()) {
                        h.visit_trait(e->m_trait.m_path.m_path, e->m_trait.m_path.m_params);
                    }
                    std::sort(h.lifetimes.begin(), h.lifetimes.end());
                    auto new_end = std::unique(h.lifetimes.begin(), h.lifetimes.end());
                    h.lifetimes.erase(new_end, h.lifetimes.end());
                    if (h.lifetimes.empty()) {
                        // Apply normal elision rules?
                        DEBUG("TraitObject: No available bounds");
                    } else {
                        if (h.lifetimes.size() == 1 || h.lifetimes.back() == HIR::LifetimeRef::new_static()) {
                            DEBUG("TraitObject: Set lifetime " << h.lifetimes.front() << " from bounds");
                            e->m_lifetime = h.lifetimes.back();
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
                    (e->m_lifetime.binding == HIR::LifetimeRef::UNKNOWN /*|| e->m_lifetime.binding == HIR::LifetimeRef::INFER*/)
                    //&& m_cur_params
                    //&& m_create_elided    // In arguments
                    && !m_in_expr // Not in expression
                ) {
                    if (!m_trait_object_rule.empty()) {
                        DEBUG("TraitObject: cur=" << m_current_depth << " back.first=" << m_trait_object_rule.back().first);
                        if (m_trait_object_rule.back().first == m_current_depth - 1) {
                            if (m_trait_object_rule.back().second) {
                                const auto& lft = *m_trait_object_rule.back().second;
                                e->m_lifetime = lft;
                                was_static_rule = (lft.binding == HIR::LifetimeRef::STATIC);
                                DEBUG("TraitObject: Set lifetime " << e->m_lifetime << " - trait object rule");
                            }
                        }
                    }
                }
                if (
                    (was_static_rule || e->m_lifetime.binding == HIR::LifetimeRef::UNKNOWN /*|| e->m_lifetime.binding == HIR::LifetimeRef::INFER*/) && !m_in_expr // Not in expression
                ) {
                    // HACK: If the trait has a lifeime param, use that
                    if (!e->m_trait.m_hrtbs && e->m_trait.m_path.m_params.m_lifetimes.size() == 1) {
                        e->m_lifetime = e->m_trait.m_path.m_params.m_lifetimes[0];
                        DEBUG("TraitObject: Set to first/only lifetime param of data trait: " << e->m_lifetime);
                    }
                }
                // If there is no available rule (i.e. not in a borrow), and the lifetime was omitted (not just '_), then fill in 'static
                if (false && m_trait_object_rule.empty() && e->m_lifetime.binding == HIR::LifetimeRef::UNKNOWN && !m_in_expr && !(m_create_elided && m_cur_params)) {
                    e->m_lifetime = HIR::LifetimeRef::new_static();
                    DEBUG("TraitObject: Set lifetime " << e->m_lifetime << " - hack");
                }
            }

            if (auto* e = data.opt_Path()) {
                // Expand default lifetime params
                if (auto* p = e->path.m_data.opt_Generic()) {
                    const HIR::TypeItem& ti = m_resolve.m_crate.get_typeitem_by_path(sp, p->m_path);
                    const HIR::GenericParams* gp = nullptr;
                    TU_MATCH_HDRA( (ti), { )
                    TU_ARMA(Import, v) {
                            BUG(sp, "Unexpected import: " << p->m_path);
                        }
                        TU_ARMA(Module, v) {
                            BUG(sp, "Unexpected module: " << p->m_path);
                        }
                        TU_ARMA(TypeAlias, v) {
                            gp = &v.m_params;
                        }
                        TU_ARMA(TraitAlias, v) {
                            gp = &v.m_params;
                        }
                        TU_ARMA(ExternType, v) {
                            gp = nullptr;
                        }
                        TU_ARMA(Enum, v) {
                            gp = &v.m_params;
                        }
                        TU_ARMA(Struct, v) {
                            gp = &v.m_params;
                        }
                        TU_ARMA(Union, v) {
                            gp = &v.m_params;
                        }
                        TU_ARMA(Trait, v) {
                            gp = &v.m_params;
                        }
                    }
                    if(gp) {
                        p->m_params.m_lifetimes.resize(gp->m_lifetimes.size());

                        // Inherit bounds.
                        if (m_cur_params) {
                            TRACE_FUNCTION_FR("INHERIT BOUNDS: " << *p, "INHERIT BOUNDS");
                            // Visit lifeitmes first - so they're un-elided
                            for (auto& l : p->m_params.m_lifetimes) {
                                visit_lifetime(sp, l);
                            }
                            // Then make a monomorph state, and find lifetime bounds
                            MonomorphStatePtr ms(crate.m_types, nullptr, &p->m_params, nullptr);
                            for (const auto& b : gp->m_bounds) {
                                TU_MATCH_HDRA((b), {)
                                TU_ARMA(Lifetime, be) {
                                        ASSERT_BUG(sp, be.test.is_param(), b);
                                        ASSERT_BUG(sp, be.valid_for.binding != HIR::LifetimeRef::UNKNOWN, b);
                                        m_cur_params->m_bounds.push_back(HIR::GenericBound::make_Lifetime({ms.monomorph_lifetime(sp, be.test), ms.monomorph_lifetime(sp, be.valid_for)}));
                                        const auto& nbe = m_cur_params->m_bounds.back().as_Lifetime();
                                        if (nbe.test.is_param()) {
                                            ASSERT_BUG(sp, nbe.test.is_param(), b << " -> " << m_cur_params->m_bounds.back());
                                            ASSERT_BUG(sp, nbe.valid_for.binding != HIR::LifetimeRef::UNKNOWN, b << " -> " << m_cur_params->m_bounds.back());
                                            if ((nbe.test.is_param() && nbe.test.as_param().group() == 3) || (nbe.valid_for.is_param() && nbe.valid_for.as_param().group() == 3)) {
                                                m_cur_params->m_bounds.pop_back();
                                            } else {
                                                DEBUG("INHERIT " << m_cur_params->m_bounds.back());
                                            }
                                        } else {
                                            // The monomorphised lifetime wasn't a parameter - had to be `'static` but not checking
                                            // - Remove the new bound, if it was bad then there should be an error later on?
                                            m_cur_params->m_bounds.pop_back();
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

                    if( p->m_params.m_lifetimes.size() == 0 ) {
                        // Mark such that contained trait objects use `'static`
                        static ::HIR::LifetimeRef static_lifetime = ::HIR::LifetimeRef::new_static();
                        m_trait_object_rule.push_back(std::make_pair(m_current_depth, &static_lifetime));
                    }
                    else if( p->m_params.m_lifetimes.size() == 1 ) {
                        // Mark such that contained trait objects use this lifetime
                        m_trait_object_rule.push_back(std::make_pair(m_current_depth, &p->m_params.m_lifetimes[0]));
                    }
                    else {
                        // Mark such that contained trait objects require an explicit annotation
                        m_trait_object_rule.push_back(std::make_pair(m_current_depth, nullptr));
                    }
                } else if (auto* p = e->path.m_data.opt_UfcsKnown()) {
                    // Get trait, check if the type has ATCs
                    const auto& trait = m_resolve.m_crate.get_trait_by_path(sp, p->trait.m_path);
                    const auto& aty = trait.m_types.at(p->item);

                    if (p->params.m_lifetimes.size() < aty.m_generics.m_lifetimes.size()) //&& m_current_lifetime.size() && m_current_lifetime.back() )
                    {
                        p->params.m_lifetimes.resize(aty.m_generics.m_lifetimes.size());
                    }
                }
            }

            ::HIR::Visitor::visit_type_data(data);

            saved_params.restore();
            while (m_current_lifetime.size() > saved_liftime_depth) {
                m_current_lifetime.pop_back();
            }
            while (m_trait_object_rule.size() > saved_m_trait_object_rule) {
                m_trait_object_rule.pop_back();
            }
            m_current_depth -= 1;

            {
                bool pushed = false;
                if (m_current_lifetime.empty() || !m_current_lifetime.back()) {
                    // Push `'static` (if not in expression mode AND; this is a trait object OR we're not in arguments)
                    if (!m_in_expr) {
                        static HIR::LifetimeRef static_lifetime = HIR::LifetimeRef::new_static();
                        if (!(m_cur_params && m_create_elided)) {
                            // In the return type, so we don't want to make a new parameter - push `'static`
                            m_current_lifetime.push_back(&static_lifetime);
                            pushed = true;
                        } else if (data.is_TraitObject() && data.as_TraitObject().m_lifetime == HIR::LifetimeRef()) {
                            // `dyn Foo` as vs `dyn Foo+'_`
                            m_current_lifetime.push_back(&static_lifetime);
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

                    visit_lifetime(sp, e->m_lifetime);
                    DEBUG("TraitObject: Final lifetime " << e->m_lifetime);
                }
                if (auto* e = data.opt_ErasedType()) {
                    // If in arguments, don't visit an omitted lifetime (so we don't add an elided lifetime for something that will be generic)
                    if ((!e->m_lifetime_bounds.empty() && e->m_lifetime_bounds.front().binding == HIR::LifetimeRef::UNKNOWN) && (m_cur_params && m_create_elided)) {
                    } else {
                        for (auto& lft : e->m_lifetime_bounds) {
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
                    switch (e->m_use_present) {
                        case ::HIR::TypeData_ErasedType::Use::Present:
                            DEBUG("ErasedType use present");
                            break;
                        case ::HIR::TypeData_ErasedType::Use::Omitted2024:
                            // Add all in-scope generics
                            DEBUG("ErasedType use omitted: 2024Edition");
                            if (m_resolve.m_impl_generics) {
                                auto p = m_resolve.m_impl_generics->make_nop_params(crate.m_types, 0);
                                for (const auto& l : p.m_lifetimes) {
                                    DEBUG("2024: add " << l);
                                    e->m_use.m_lifetimes.push_back(l);
                                }
                            }
                            if (m_resolve.m_item_generics) {
                                auto p = m_resolve.m_item_generics->make_nop_params(crate.m_types, 1);
                                for (const auto& l : p.m_lifetimes) {
                                    DEBUG("2024: add " << l);
                                    e->m_use.m_lifetimes.push_back(l);
                                }
                            }
                            break;
                        case ::HIR::TypeData_ErasedType::Use::OmittedOld: {
                            DEBUG("ErasedType use omitted: Older Editions");

                            // If there is no lifetime assigned, then grab all mentioned lifetimes?
                            struct V: public HIR::Visitor {
                                std::set<HIR::LifetimeRef> lfts;

                                V(HIR::TypeInterner& types)
                                    : HIR::Visitor(nullptr, types)
                                {
                                }

                                void visit_path_params(HIR::PathParams& pp) override {
                                    for (auto& lft : pp.m_lifetimes) {
                                        add_lifetime(lft);
                                    }

                                    HIR::Visitor::visit_path_params(pp);
                                }

                                void add_lifetime(const HIR::LifetimeRef& lft) {
                                    if (lft.is_hrl()) {
                                        // HRL - ignore
                                        return;
                                    }
                                    this->lfts.insert(lft);
                                }

                                void visit_type(HIR::TypeRef& ty) override {
                                    TRACE_FUNCTION_F(ty);
                                    if (const auto* tep = ty->opt_Borrow()) {
                                        add_lifetime(tep->lifetime);
                                    }
                                    if (const auto* tep = ty->opt_Function()) {
                                        // Push HRLs?
                                        (void)tep;
                                    }
                                    if (const auto* tep = ty->opt_TraitObject()) {
                                        add_lifetime(tep->m_lifetime);
                                        // Push HRLs?
                                    }
                                    if (const auto* tep = ty->opt_ErasedType()) {
                                        if (tep->m_lifetime_bounds.size() == 1 && tep->m_lifetime_bounds.front().binding == HIR::LifetimeRef::UNKNOWN) {
                                            // Ignore unbound?
                                        } else {
                                            for (const auto& lft : tep->m_lifetime_bounds) {
                                                add_lifetime(lft);
                                            }
                                        }
                                    }
                                    HIR::Visitor::visit_type(ty);
                                }
                            } v(crate.m_types);

                            // `data` is the lifetime-elided copy of `ty`.  Type nodes are
                            // immutable once interned, so walking `ty` here would inspect
                            // the pre-elision tree (and can reintroduce `'#omitted` from a
                            // parenthesised Fn bound).  Materialise the current copy before
                            // collecting the lifetimes captured by this opaque type.
                            auto elided_ty = crate.m_types.intern(data.clone_data());
                            v.visit_type(elided_ty);
                            // TODO: In 2024 edition, these rules change
                            // - Before: generics/lifetimes not mentioned in the `impl Foo` are omitted
                            // - After: All included
                            // - Both: Unless there's a `use<Foo>` present

                            if (v.lfts.empty() && !(!m_current_lifetime.empty() && m_current_lifetime.back() && !pushed)) {
                                // If this is on a by-value method, then assume it captures `self` (and thus all contained liftimes)
                                // REF: rustc-1.90.0-src/compiler/rustc_data_structures/src/graph/linked_graph/mod.rs:278
                                if (m_value_self_type) {
                                    DEBUG("Check Self: " << *m_value_self_type);
                                    auto self_type = *m_value_self_type;
                                    v.visit_type(self_type);
                                }
                            }

                            // If there is a lifetime on the stack (that wasn't from a `'static` pushed above), then use it
                            if (v.lfts.empty() && !m_current_lifetime.empty() && m_current_lifetime.back() && !pushed) {
                                DEBUG("ErasedType: Use wrapping lifetime - " << *m_current_lifetime.back());
                                e->m_use.m_lifetimes.push_back(*m_current_lifetime.back());
                            } else if (v.lfts.empty()) {
                                // No contained lifetimes, it's `'static`?
                                DEBUG("No inner lifetimes, will be `'static`");
                                e->m_use.m_lifetimes.push_back(HIR::LifetimeRef::new_static());
                            } else if (v.lfts.size() == 1) {
                                // Easy, just assign this lifetime
                                DEBUG("ErasedType: Use contained lifetime " << *v.lfts.begin());
                                e->m_use.m_lifetimes.push_back(*v.lfts.begin());
                            } else {
                                // If in arguments: Create a new input lifetime with a union of these lifetimes.
                                if (m_cur_params && m_create_elided) {
                                    e->m_use.m_lifetimes.push_back(HIR::LifetimeRef(m_cur_params_level * 256 + m_cur_params->m_lifetimes.size()));
                                    m_cur_params->m_lifetimes.push_back(HIR::LifetimeDef{});
                                    for (const auto& l : v.lfts) {
                                        m_cur_params->m_bounds.push_back(HIR::GenericBound::make_Lifetime({e->m_use.m_lifetimes[0], l}));
                                    }
                                }
                                // In return: Save the list?
                                else if (m_cur_params) {
                                    ASSERT_BUG(sp, e->m_use.m_lifetimes.size() == 0, "");
                                    for (const auto& lft : v.lfts) {
                                        e->m_use.m_lifetimes.push_back(lft);
                                    }
                                } else {
                                }
                            }
                        } break;
                    }
                    for (const auto& l : e->m_use.m_lifetimes) {
                        ASSERT_BUG(sp, l.binding != HIR::LifetimeRef::UNKNOWN, "Unbound lifetime? - " << l);
                    }

                    if (auto* ee = e->m_inner.opt_Alias()) {
                        if (ee->inner->path.crate_name() != m_resolve.m_crate.m_crate_name) {
                            // Should be impossible, as these are fully expanded by the time they reach HIR serialisation
                        } else {
                            ASSERT_BUG(Span(), m_resolve.m_impl_generics, "No impl generics for type " << ty);
                            ee->inner->generics.m_lifetimes = m_resolve.m_impl_generics->m_lifetimes;
                            ee->params = ee->inner->generics.make_nop_params(crate.m_types, 0);
                        }
                    }
                }
                if (pushed) {
                    m_current_lifetime.pop_back();
                }
            }

            ty = crate.m_types.intern(mv$(data));

            if (m_current_depth == 0) {
                DEBUG("< " << ty);
            }
        }

        void visit_trait_path(::HIR::TraitPath& tp) override {
            const Span sp;
            TRACE_FUNCTION_FR(tp, tp);

            auto has_apply_elision = [](::HIR::TraitPath& tp, bool& created_hrls) -> bool {
                bool was_paren_trait_object = tp.m_hrtbs && tp.m_hrtbs->m_lifetimes.size() >= 1 && tp.m_hrtbs->m_lifetimes.back().m_name == "#apply_elision";
                created_hrls = false;
                if (was_paren_trait_object) {
                    if (!tp.m_hrtbs) {
                        tp.m_hrtbs = std::make_unique<HIR::GenericParams>();
                        created_hrls = true;
                    }
                    if (was_paren_trait_object) {
                        tp.m_hrtbs->m_lifetimes.pop_back();
                    }
                    return true;
                } else {
                    return false;
                }
            };

            // Handle a hack from lowering pass added when the path is `Foo()`
            bool created_hrls = false;
            if (has_apply_elision(tp, created_hrls)) {
                m_current_lifetime.push_back(nullptr);

                // Visit the trait args (as inputs)
                auto saved_params = push_params(tp.m_hrtbs.get(), 3);

                this->visit_generic_path(tp.m_path, ::HIR::Visitor::PathContext::TYPE);
                DEBUG(tp.m_path);
                if (tp.m_hrtbs) {
                    DEBUG("for " << tp.m_hrtbs->fmt_args());
                }

#if 1
                HIR::LifetimeRef lft;
                if (tp.m_hrtbs && tp.m_hrtbs->m_lifetimes.size() == 1) {
                    lft = HIR::LifetimeRef(/*tp.m_hrtbs->m_lifetimes[0].m_name,*/ 3 * 256 + 0);
                }
                if (lft == HIR::LifetimeRef()) {
                    // If there wasn't an elided lifetime in the input, then get a lifetime from that as the output.
                    // - If there's only one lifetime in `tp.m_path`, use that
                    struct V: public HIR::Visitor {
                        HIR::LifetimeRef out;
                        unsigned n_found = 0;

                        V(HIR::TypeInterner& types)
                            : HIR::Visitor(nullptr, types)
                        {
                        }

                        void add_lifetime(const HIR::LifetimeRef& lft) {
                            if (lft.is_hrl()) {
                                // HRL - ignore
                                return;
                            }
                            n_found += 1;
                            out = lft;
                        }

                        void visit_path_params(HIR::PathParams& pp) override {
                            for (auto& lft : pp.m_lifetimes) {
                                add_lifetime(lft);
                            }

                            HIR::Visitor::visit_path_params(pp);
                        }

                        void visit_type(HIR::TypeRef& ty) override {
                            if (const auto* tep = ty->opt_Borrow()) {
                                add_lifetime(tep->lifetime);
                            }
                            if (const auto* tep = ty->opt_Function()) {
                                // Push HRLs?
                                (void)tep;
                            }
                            if (const auto* tep = ty->opt_TraitObject()) {
                                add_lifetime(tep->m_lifetime);
                                // Push HRLs?
                            }
                            if (auto* tep = ty->opt_ErasedType()) {
                                for (const auto& lft : tep->m_lifetime_bounds) {
                                    add_lifetime(lft);
                                }
                            }
                            HIR::Visitor::visit_type(ty);
                        }
                    } v(crate.m_types);

                    v.visit_path_params(tp.m_path.m_params);
                    if (v.n_found == 1) {
                        lft = v.out;
                    }
                }
                if (lft != HIR::LifetimeRef()) {
                    m_current_lifetime.push_back(&lft);
                    for (auto& assoc : tp.m_type_bounds) {
                        this->visit_generic_path(assoc.second.source_trait, ::HIR::Visitor::PathContext::TYPE);
                        this->visit_path_params(assoc.second.aty_params);
                        this->visit_type(assoc.second.type);
                    }
                    for (auto& assoc : tp.m_trait_bounds) {
                        this->visit_generic_path(assoc.second.source_trait, ::HIR::Visitor::PathContext::TYPE);
                        this->visit_path_params(assoc.second.aty_params);
                        for (auto& trait : assoc.second.traits) {
                            this->visit_trait_path(trait);
                        }
                    }
                    m_current_lifetime.pop_back();
                }
#endif

                saved_params.restore();

                // Fix the source paths in ATYs
                const auto& trait = m_resolve.m_crate.get_trait_by_path(sp, tp.m_path.m_path);

                struct H {
                    const HIR::Crate& m_crate;

                    H(const HIR::Crate& crate)
                        : m_crate(crate)
                    {
                    }

                    bool enum_supertraits(const Span& sp, const HIR::Trait& tr, const HIR::GenericPath& tr_path, ::std::function<bool(HIR::GenericPath)> cb) {
                        const HIR::TypeRef self = m_crate.m_types.self();
                        MonomorphStatePtr ms(m_crate.m_types, &self, &tr_path.m_params, nullptr);

                        if (tr.m_all_parent_traits.size() > 0) {
                            // Externals will have this populated
                            for (const auto& supertrait : tr.m_all_parent_traits) {
                                auto m = ms.monomorph_genericpath(sp, supertrait.m_path, false);
                                if (cb(std::move(m))) {
                                    return true;
                                }
                            }
                        } else {
                            // This runs before bind, so locals won't have the main list populated
                            for (const auto& pt : tr.m_parent_traits) {
                                auto m = ms.monomorph_genericpath(sp, pt.m_path, false);
                                DEBUG("- " << m);
                                if (enum_supertraits(sp, m_crate.get_trait_by_path(sp, m.m_path), m, cb)) {
                                    return true;
                                }
                                if (cb(std::move(m))) {
                                    return true;
                                }
                            }
                            for (const auto& b : tr.m_params.m_bounds) {
                                if (!b.is_TraitBound()) {
                                    continue;
                                }
                                const auto& be = b.as_TraitBound();
                                if (be.type != self) {
                                    continue;
                                }
                                const auto& pt = be.trait;
                                if (pt.m_path.m_path == tr_path.m_path) {
                                    continue;
                                }

                                auto m = ms.monomorph_genericpath(sp, pt.m_path, false);
                                DEBUG("- " << m);
                                if (enum_supertraits(sp, m_crate.get_trait_by_path(sp, m.m_path), m, cb)) {
                                    return true;
                                }
                                if (cb(std::move(m))) {
                                    return true;
                                }
                            }
                        }
                        return false;
                    }
                } h(m_resolve.m_crate);

                auto fix_source = [&](HIR::GenericPath& gp, const RcString& name) {
                    //fix_path(gp);
                    DEBUG("[fix_source] >> " << gp);
                    // NOTE: The HRLs of this path have been edited! (they were `<'#apply_elision,>`, now blank)
                    if (gp.equals_ignoring_regions(tp.m_path)) {
                        gp = tp.m_path.clone();
                        return;
                    }
                    if (h.enum_supertraits(sp, trait, tp.m_path, [&](HIR::GenericPath m) {
                        DEBUG("[fix_source] ?? " << m);
                        if (m.equals_ignoring_regions(gp)) {
                            gp = std::move(m);
                            return true;
                        }
                        return false;
                    })) {
                        return;
                    }
                    BUG(sp, "Failed to find " << gp << " in parent trait list of " << tp.m_path);
                };
                for (auto& assoc : tp.m_type_bounds) {
                    fix_source(assoc.second.source_trait, assoc.first);
                }
                for (auto& assoc : tp.m_trait_bounds) {
                    fix_source(assoc.second.source_trait, assoc.first);
                }

                // Set the output lifetime (if present)
                auto output_lifetime = HIR::LifetimeRef(3 * 256 + 0);
                if (tp.m_hrtbs->m_lifetimes.size() == 1) {
                    m_current_lifetime.pop_back();
                    m_current_lifetime.push_back(&output_lifetime);
                } else {
                    // No output lifetime
                }

                // Visit the rest (associated types mostly), using the output lifetime from above
                ::HIR::Visitor::visit_trait_path(tp);

                m_current_lifetime.pop_back();

                if (created_hrls && tp.m_hrtbs->is_empty()) {
                    tp.m_hrtbs.reset();
                }
            } else {
                ::HIR::Visitor::visit_trait_path(tp);
            }
        }

        void visit_expr(::HIR::ExprPtr& ep) override {
            struct EV: public HIR::ExprVisitorDef {
                LifetimeVisitor& parent;

                EV(LifetimeVisitor& parent)
                    : HIR::ExprVisitorDef(parent.crate.m_types)
                    , parent(parent)
                {
                }

                void visit_type(HIR::TypeRef& ty) {
                    parent.visit_type(ty);
                }
            } v{*this};

            auto s = m_in_expr;
            m_in_expr = true;
            if (ep) {
                ep->visit(v);
            }
            m_in_expr = s;
        }

        void visit_type_impl(::HIR::TypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.m_type);
            m_resolve.m_self_type = &impl.m_type;
            auto _ = m_resolve.set_impl_generics(/*impl.m_type,*/ impl.m_params);

            // Pre-visit so lifetime elision can work
            {
                auto _ = push_params(impl.m_params, 0);
                this->visit_type(impl.m_type);
            }

            ::HIR::Visitor::visit_type_impl(impl);
        }

        void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
            TRACE_FUNCTION_F("impl " << trait_path << impl.m_trait_args << " for " << impl.m_type);
            m_resolve.m_self_type = &impl.m_type;
            auto _ = m_resolve.set_impl_generics(/*impl.m_type,*/ impl.m_params);

            // Pre-visit so lifetime elision can work
            {
                auto _ = push_params(impl.m_params, 0);
                this->visit_type(impl.m_type);
                this->visit_path_params(impl.m_trait_args);
            }

            ::HIR::Visitor::visit_trait_impl(trait_path, impl);
        }

        void visit_marker_impl(const ::HIR::SimplePath& trait_path, ::HIR::MarkerImpl& impl) override {
            TRACE_FUNCTION_F("impl " << trait_path << impl.m_trait_args << " for " << impl.m_type << " { }");
            m_resolve.m_self_type = &impl.m_type;
            auto _ = m_resolve.set_impl_generics(/*impl.m_type,*/ impl.m_params);

            // Pre-visit so lifetime elision can work
            {
                auto _ = push_params(impl.m_params, 0);
                this->visit_type(impl.m_type);
                this->visit_path_params(impl.m_trait_args);
            }

            ::HIR::Visitor::visit_marker_impl(trait_path, impl);
        }

        void visit_type_alias(::HIR::ItemPath p, ::HIR::TypeAlias& item) override {
            auto _ = m_resolve.set_impl_generics(/*impl.m_type,*/ item.m_params);
            ::HIR::Visitor::visit_type_alias(p, item);
        }

        void visit_trait(::HIR::ItemPath p, ::HIR::Trait& item) override {
            auto ty_self = crate.m_types.self();
            m_resolve.m_self_type = &ty_self;
            auto _ = m_resolve.set_impl_generics(/*impl.m_type,*/ item.m_params);
            ::HIR::Visitor::visit_trait(p, item);
        }

        void visit_struct(::HIR::ItemPath p, ::HIR::Struct& item) override {
            auto _ = m_resolve.set_impl_generics(/*item.m_struct_markings.dst_type,*/ item.m_params);
            auto _2 = push_params(item.m_params, 0);
            m_create_elided = false;
            ::HIR::Visitor::visit_struct(p, item);
        }

        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override {
            auto _ = m_resolve.set_impl_generics(/*MetadataType::None,*/ item.m_params);
            auto _2 = push_params(item.m_params, 0);
            m_create_elided = false;
            ::HIR::Visitor::visit_enum(p, item);
        }

        void visit_union(::HIR::ItemPath p, ::HIR::Union& item) override {
            auto _ = m_resolve.set_impl_generics(/*MetadataType::None,*/ item.m_params);
            auto _2 = push_params(item.m_params, 0);
            m_create_elided = false;
            ::HIR::Visitor::visit_union(p, item);
        }

        void visit_constant(::HIR::ItemPath p, ::HIR::Constant& item) override {
            auto lft = HIR::LifetimeRef::new_static();
            m_current_lifetime.push_back(&lft);
            visit_type(item.m_type);
            m_current_lifetime.pop_back(/*&lft*/);

            ::HIR::Visitor::visit_constant(p, item);
        }

        void visit_static(::HIR::ItemPath p, ::HIR::Static& item) override {
            auto lft = HIR::LifetimeRef::new_static();
            m_current_lifetime.push_back(&lft);
            visit_type(item.m_type);
            m_current_lifetime.pop_back(/*&lft*/);

            ::HIR::Visitor::visit_static(p, item);
        }

        void visit_function(::HIR::ItemPath p, ::HIR::Function& item) override {
            TRACE_FUNCTION_F(p);
            auto _ = m_resolve.set_item_generics(item.m_params);
            // NOTE: Superfluous... except that it makes the params valid for the return type.
            visit_params(item.m_params);

            auto first_elided_lifetime_idx = item.m_params.m_lifetimes.size();

            // TODO: Add lifetime bounds from argument types!
            // - While visiting the argument types, find path types and inherit the lifetime bounds

            // Visit arguments to get the input lifetimes
            auto saved_params = push_params(item.m_params, 1);
            for (auto& arg : item.m_args) {
                TRACE_FUNCTION_FR("ARG " << arg, "ARG " << arg);
                visit_type(arg.second);
            }
            m_create_elided = false;

            // Get output lifetime
            // - Try `&self`'s lifetime (if it was an elided lifetime)
            HIR::LifetimeRef elided_output_lifetime;
            if (item.m_receiver != HIR::Function::Receiver::Free) {
                if (const auto* b = item.m_args[0].second->opt_Borrow()) {
                    // If this was an elided lifetime.
                    if (b->lifetime.is_param() && (b->lifetime.binding >> 8) == 1 && (b->lifetime.binding & 0xFF) >= first_elided_lifetime_idx) {
                        elided_output_lifetime = b->lifetime;
                        DEBUG("Elided 'self");
                    }
                    // Also allow 'static self (see lazy_static 1.0.2)
                    if (b->lifetime.binding == HIR::LifetimeRef::STATIC) {
                        elided_output_lifetime = b->lifetime;
                        DEBUG("Static 'self");
                    }
                    // OR, just always use `'self` if present
                    if (true) {
                        DEBUG("'self specified");
                        elided_output_lifetime = b->lifetime;
                    }
                }
                if (item.m_receiver == HIR::Function::Receiver::Value) {
                    m_value_self_type = m_resolve.m_self_type;
                }
            }
            // - OR, look for only one elided lifetime
            if (elided_output_lifetime == HIR::LifetimeRef()) {
                if (item.m_params.m_lifetimes.size() == first_elided_lifetime_idx + 1) {
                    elided_output_lifetime = HIR::LifetimeRef(256 + first_elided_lifetime_idx);
                    DEBUG("Elided 'only");
                }
            }
            if (elided_output_lifetime == HIR::LifetimeRef()) {
                if (item.m_params.m_lifetimes.size() == 1) {
                    elided_output_lifetime = HIR::LifetimeRef(256 + 0);
                    DEBUG("Elided 'single");
                }
            }
            // TODO: Search for an explicit lifetime in the input, and use that if there was only one?
            if (elided_output_lifetime == HIR::LifetimeRef()) {
                struct V: public HIR::Visitor {
                    HIR::LifetimeRef out;
                    unsigned n_found = 0;

                    V(HIR::TypeInterner& types)
                        : HIR::Visitor(nullptr, types)
                    {
                    }

                    void add_lifetime(const HIR::LifetimeRef& lft) {
                        if (lft.is_hrl()) {
                            // HRL - ignore
                            return;
                        }
                        n_found += 1;
                        out = lft;
                    }

                    void visit_path_params(HIR::PathParams& pp) override {
                        for (auto& lft : pp.m_lifetimes) {
                            add_lifetime(lft);
                        }

                        HIR::Visitor::visit_path_params(pp);
                    }

                    void visit_type(HIR::TypeRef& ty) override {
                        if (const auto* tep = ty->opt_Borrow()) {
                            add_lifetime(tep->lifetime);
                        }
                        if (const auto* tep = ty->opt_Function()) {
                            // Push HRLs?
                            (void)tep;
                        }
                        if (const auto* tep = ty->opt_TraitObject()) {
                            add_lifetime(tep->m_lifetime);
                            // Push HRLs?
                        }
                        if (const auto* tep = ty->opt_ErasedType()) {
                            for (const auto& lft : tep->m_lifetime_bounds) {
                                add_lifetime(lft);
                            }
                        }
                        HIR::Visitor::visit_type(ty);
                    }
                } v(crate.m_types);

                for (auto& a : item.m_args) {
                    v.visit_type(a.second);
                }
                if (v.n_found == 1 && v.out != HIR::LifetimeRef::new_static()) {
                    elided_output_lifetime = v.out;
                    DEBUG("Explicit 'single (recurse) - " << elided_output_lifetime);
                }
            }
            if (elided_output_lifetime == HIR::LifetimeRef()) {
                // TODO: If the only argument is a `'static`, use that? (or if there's only one borrow in the arguments, use that)
                if (item.m_args.size() == 1 && item.m_args.front().second->is_Borrow()) {
                    elided_output_lifetime = item.m_args.front().second->as_Borrow().lifetime;
                    DEBUG("Explicit 'single - " << elided_output_lifetime);
                }
            }
            // If present, set it (push to the stack)
            assert(m_current_lifetime.empty());
            if (elided_output_lifetime != HIR::LifetimeRef()) {
                m_current_lifetime.push_back(&elided_output_lifetime);
            }

            // Visit return type (populates path for `impl Trait` in return position
            {
                TRACE_FUNCTION_FR("RET " << item.m_return, "RET " << item.m_return);
                visit_type(item.m_return);
            }
            // - Unset params for the expression
            saved_params.restore();

            if (elided_output_lifetime != HIR::LifetimeRef()) {
                m_current_lifetime.pop_back();
            }
            assert(m_current_lifetime.empty());

            DEBUG("Output: " << item.m_params.fmt_args() << item.m_params.fmt_bounds());
            m_value_self_type = nullptr;

            ::HIR::Visitor::visit_function(p, item);
        }
    };
}

void ConvertHIR_LifetimeElision(::HIR::Crate& crate) {
    LifetimeVisitor v{crate};
    v.visit_crate(crate);
}

#include "hir_conv_main_bindings.h"
#include "hir_visitor.h"
#include "hir_expr.h"
#include <algorithm> // std::find_if

#include "hir_typeck_static.h"

namespace {

    class MarkingsVisitor: public ::HIR::Visitor {
        const ::HIR::Crate& m_crate;
        const ::HIR::SimplePath& m_lang_Unsize;
        const ::HIR::SimplePath& m_lang_CoerceUnsized;
        const ::HIR::SimplePath& m_lang_Copy;
        const ::HIR::SimplePath& m_lang_Deref;
        const ::HIR::SimplePath& m_lang_Drop;
        const ::HIR::SimplePath& m_lang_PhantomData;

    public:
        MarkingsVisitor(const ::HIR::Crate& crate)
            : ::HIR::Visitor(nullptr, crate.m_types)
            , m_crate(crate)
            , m_lang_Unsize(crate.get_lang_item_path_opt("unsize"))
            , m_lang_CoerceUnsized(crate.get_lang_item_path_opt("coerce_unsized"))
            , m_lang_Copy(crate.get_lang_item_path_opt("copy"))
            , m_lang_Deref(crate.get_lang_item_path_opt("deref"))
            , m_lang_Drop(crate.get_lang_item_path_opt("drop"))
            , m_lang_PhantomData(crate.get_lang_item_path_opt("phantom_data"))
        {
        }

        void visit_struct(::HIR::ItemPath ip, ::HIR::Struct& str) override {
            ::HIR::Visitor::visit_struct(ip, str);

            str.m_struct_markings.dst_type = get_struct_dst_type(str, str.m_params, {});
            if (str.m_struct_markings.dst_type != ::HIR::StructMarkings::DstType::None) {
                str.m_struct_markings.unsized_field = (str.m_data.is_Tuple() ? str.m_data.as_Tuple().size() - 1 : str.m_data.as_Named().size() - 1);
            }

            // Rules:
            // - A type parameter must be ?Sized
            // - That type parameter must only be used as part of the last field, and only once
            // - If the final field isn't the parameter, it must also impl Unsize

            // HACK: Just determine what ?Sized parameter is controlling the sized-ness
            if (str.m_struct_markings.dst_type == ::HIR::StructMarkings::DstType::Possible) {
                auto& last_field_ty = (str.m_data.is_Tuple() ? str.m_data.as_Tuple().back().ent : str.m_data.as_Named().back().ty);
                for (size_t i = 0; i < str.m_params.m_types.size(); i++) {
                    const auto& param = str.m_params.m_types[i];
                    auto ty = m_crate.m_types.generic(param.m_name, i);
                    if (!param.m_is_sized) {
                        if (visit_ty_with(last_field_ty, [&](const auto& t) {
                            return t == ty;
                        })) {
                            ASSERT_BUG(Span(), str.m_struct_markings.unsized_param == ~0u, "Multiple unsized params to " << ip);
                            str.m_struct_markings.unsized_param = i;
                        }
                    }
                }
                ASSERT_BUG(Span(), str.m_struct_markings.unsized_param != ~0u, "No unsized param for type " << ip);
                str.m_struct_markings.can_unsize = true;
            }
        }

        ::HIR::StructMarkings::DstType get_field_dst_type(const ::HIR::TypeRef& ty, const ::HIR::GenericParams& inner_def, const ::HIR::GenericParams& params_def, const ::HIR::PathParams* params) {
            TRACE_FUNCTION_F("ty=" << ty);
            // If the type is generic, and the pointed-to parameters is ?Sized, record as needing unsize
            if (const auto* te = ty->opt_Generic()) {
                if (inner_def.m_types.at(te->binding).m_is_sized == true) {
                    return ::HIR::StructMarkings::DstType::None;
                } else if (params) {
                    // Look at the param. Check for generic (use params_def), slice/traitobject, or path (no mono)
                    return get_field_dst_type(params->m_types.at(te->binding), params_def, params_def, nullptr);
                } else {
                    return ::HIR::StructMarkings::DstType::Possible;
                }
            } else if (ty->is_Slice() || TU_TEST1((*ty), Primitive, == HIR::CoreType::Str)) {
                return ::HIR::StructMarkings::DstType::Slice;
            } else if (ty->is_TraitObject()) {
                return ::HIR::StructMarkings::DstType::TraitObject;
            } else if (const auto* te = ty->opt_Path()) {
                // If the type is a struct, check it (recursively)
                if (!te->path.m_data.is_Generic()) {
                    // Associated type, TODO: Check this better.
                    return ::HIR::StructMarkings::DstType::None;
                } else if (te->binding.is_Struct()) {
                    const auto& params_tpl = te->path.m_data.as_Generic().m_params;
                    if (params && monomorphise_pathparams_needed(params_tpl)) {
                        static Span sp;
                        auto monomorph_cb = MonomorphStatePtr(m_crate.m_types, nullptr, params, nullptr);
                        auto params_mono = monomorph_cb.monomorph_path_params(sp, params_tpl, false);
                        return get_struct_dst_type(*te->binding.as_Struct(), params_def, &params_mono);
                    } else {
                        return get_struct_dst_type(*te->binding.as_Struct(), inner_def, &params_tpl);
                    }
                } else {
                    return ::HIR::StructMarkings::DstType::None;
                }
            } else {
                return ::HIR::StructMarkings::DstType::None;
            }
        }

        ::HIR::StructMarkings::DstType get_struct_dst_type(const ::HIR::Struct& str, const ::HIR::GenericParams& def, const ::HIR::PathParams* params) {
        TU_MATCH_HDRA( (str.m_data), {)
        TU_ARMA(Unit, se) {
                }
                TU_ARMA(Tuple, se) {
                    // TODO: Ensure that only the last field is ?Sized
                    if (se.size() > 0) {
                        return get_field_dst_type(se.back().ent, str.m_params, def, params);
                    }
                }
                TU_ARMA(Named, se) {
                    // Check the last field in the struct.
                    // - If it is Sized, leave as-is (struct is marked as Sized)
                    // - If it is known unsized, record the type
                    // - If it is a ?Sized parameter, mark as possible and record index for MIR

                    // TODO: Ensure that only the last field is ?Sized
                    if (se.size() > 0) {
                        return get_field_dst_type(se.back().ty, str.m_params, def, params);
                    }
                }
        }
        return ::HIR::StructMarkings::DstType::None;
        }

        void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
            static Span sp;

            ::HIR::Visitor::visit_trait_impl(trait_path, impl);

            if (impl.m_type->is_Path()) {
                const auto& te = impl.m_type->as_Path();
                const ::HIR::TraitMarkings* markings_ptr = te.binding.get_trait_markings();
                if (markings_ptr) {
                    ::HIR::TraitMarkings& markings = *const_cast<::HIR::TraitMarkings*>(markings_ptr);
                    if (trait_path == m_lang_Unsize) {
                        DEBUG("Type " << impl.m_type << " can Unsize");
                        ERROR(sp, E0000, "Unsize shouldn't be manually implemented");
                    } else if (trait_path == m_lang_Drop) {
                        // TODO: Check that there's only one impl, and that it covers the same set as the type.
                        markings.has_drop_impl = true;
                    } else if (trait_path == m_lang_CoerceUnsized) {
                        auto& struct_markings = const_cast<::HIR::Struct*>(te.binding.as_Struct())->m_struct_markings;
                        if (struct_markings.coerce_unsized_index != ~0u) {
                            ERROR(sp, E0000, "CoerceUnsized can only be implemented once per struct");
                        }

                        DEBUG("Type " << impl.m_type << " can Coerce");
                        if (impl.m_trait_args.m_types.size() != 1) {
                            ERROR(sp, E0000, "Unexpected number of arguments for CoerceUnsized");
                        }
                        const auto& dst_ty = impl.m_trait_args.m_types[0];
                        // Determine which field is the one that does the coerce
                        if (!te.binding.is_Struct()) {
                            ERROR(sp, E0000, "Cannot implement CoerceUnsized on non-structs");
                        }
                        if (!dst_ty->is_Path()) {
                            ERROR(sp, E0000, "Cannot implement CoerceUnsized from non-structs");
                        }
                        const auto& dst_te = dst_ty->as_Path();
                        if (!dst_te.binding.is_Struct()) {
                            ERROR(sp, E0000, "Cannot implement CoerceUnsized from non-structs");
                        }
                        if (dst_te.binding.as_Struct() != te.binding.as_Struct()) {
                            ERROR(sp, E0000, "CoerceUnsized can only be implemented between variants of the same struct");
                        }

                        // NOTES: (from IRC: eddyb)
                        // < eddyb> they're required that T and U are the same struct definition (with different type parameters) and exactly one field differs in type between T and U (ignoring PhantomData)
                        // < eddyb> Mutabah: I forgot to mention that the field that differs in type must also impl CoerceUnsized

                        // Determine the difference in monomorphised variants.
                        unsigned int field = ~0u;
                        const auto& str = te.binding.as_Struct();

                        auto monomorph_cb_l = MonomorphStatePtr(m_crate.m_types, nullptr, &dst_te.path.m_data.as_Generic().m_params, nullptr);
                        auto monomorph_cb_r = MonomorphStatePtr(m_crate.m_types, nullptr, &te.path.m_data.as_Generic().m_params, nullptr);

                    TU_MATCH_HDRA( (str->m_data), {)
                    TU_ARMA(Unit, se) {
                            }
                            TU_ARMA(Tuple, se) {
                                for (unsigned int i = 0; i < se.size(); i++) {
                                    // If the data is PhantomData, ignore it.
                                    if (TU_TEST2((*se[i].ent), Path, .path.m_data, Generic, .m_path == m_lang_PhantomData)) {
                                        continue;
                                    }
                                    if (monomorphise_type_needed(se[i].ent)) {
                                        auto ty_l = monomorph_cb_l.monomorph_type(sp, se[i].ent, false);
                                        auto ty_r = monomorph_cb_r.monomorph_type(sp, se[i].ent, false);
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
                                    if (TU_TEST2((*se[i].ty), Path, .path.m_data, Generic, .m_path == m_lang_PhantomData)) {
                                        continue;
                                    }
                                    if (monomorphise_type_needed(se[i].ty)) {
                                        auto ty_l = monomorph_cb_l.monomorph_type(sp, se[i].ty, false);
                                        auto ty_r = monomorph_cb_r.monomorph_type(sp, se[i].ty, false);
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
                    struct_markings.coerce_unsized_index = field;
                    } else if (trait_path == m_lang_Deref) {
                        DEBUG("Type " << impl.m_type << " can Deref");
                        markings.has_a_deref = true;
                    } else if (trait_path == m_lang_Copy) {
                        DEBUG("Type " << impl.m_type << " has a Copy impl");
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

        size_t get_unsize_param_idx(const Span& sp, const ::HIR::TypeRef& pointee) const {
            if (const auto* te = pointee->opt_Generic()) {
                return te->binding;
            } else if (const auto* te = pointee->opt_Path()) {
                ASSERT_BUG(sp, te->binding.is_Struct(), "Pointer to non-Unsize type - " << pointee);
                const auto& ism = te->binding.as_Struct()->m_struct_markings;
                ASSERT_BUG(sp, ism.unsized_param != ~0u, "Pointer to non-Unsize type - " << pointee);
                const auto& gp = te->path.m_data.as_Generic();
                return get_unsize_param_idx(sp, gp.m_params.m_types.at(ism.unsized_param));
            } else {
                BUG(sp, "Pointer to non-Unsize type? - " << pointee);
            }
        }

        ::HIR::StructMarkings::Coerce get_coerce_type(const Span& sp, ::HIR::ItemPath ip, const ::HIR::Struct& str, size_t& out_param_idx) const {
            if (str.m_struct_markings.coerce_unsized_index == ~0u) {
                return ::HIR::StructMarkings::Coerce::None;
            }
            if (str.m_struct_markings.coerce_unsized != ::HIR::StructMarkings::Coerce::None) {
                out_param_idx = str.m_struct_markings.coerce_param;
                return str.m_struct_markings.coerce_unsized;
            }

            const ::HIR::TypeRef* field_ty = nullptr;
            TU_MATCHA((str.m_data), (se), (Unit, ), (Tuple, field_ty = &se.at(str.m_struct_markings.coerce_unsized_index).ent;), (Named, field_ty = &se.at(str.m_struct_markings.coerce_unsized_index).ty;))
            assert(field_ty);
        try_again:
            DEBUG("field_ty = " << *field_ty);

            if (const auto* te = (*field_ty)->opt_Path()) {
                ASSERT_BUG(sp, te->binding.is_Struct(), "CoerceUnsized impl differs on Path that isn't a struct - " << ip << " fld=" << *field_ty);
                const auto* istr = te->binding.as_Struct();
                const auto& gp = te->path.m_data.as_Generic();

                size_t inner_idx = 0;
                auto inner_type = get_coerce_type(sp, {*field_ty}, *istr, inner_idx);
                ASSERT_BUG(sp, inner_type != ::HIR::StructMarkings::Coerce::None, "CoerceUnsized impl differs on a non-CoerceUnsized type - " << ip << " fld=" << *field_ty);

                const auto& param_ty = gp.m_params.m_types.at(inner_idx);
                switch (inner_type) {
                    case ::HIR::StructMarkings::Coerce::None:
                        throw "";
                    case ::HIR::StructMarkings::Coerce::Passthrough:
                        // Recurse on the generic type.
                        field_ty = &param_ty;
                        goto try_again;
                    case ::HIR::StructMarkings::Coerce::Pointer:
                        out_param_idx = get_unsize_param_idx(sp, param_ty);
                        return ::HIR::StructMarkings::Coerce::Pointer;
                }
            } else if (const auto* te = (*field_ty)->opt_Generic()) {
                out_param_idx = te->binding;
                return ::HIR::StructMarkings::Coerce::Passthrough;
            } else if (const auto* te = (*field_ty)->opt_Pointer()) {
                out_param_idx = get_unsize_param_idx(sp, te->inner);
                return ::HIR::StructMarkings::Coerce::Pointer;
            } else if (const auto* te = (*field_ty)->opt_Borrow()) {
                out_param_idx = get_unsize_param_idx(sp, te->inner);
                return ::HIR::StructMarkings::Coerce::Pointer;
            } else {
                TODO(sp, "Handle CoerceUnsized type " << *field_ty);
            }
            BUG(sp, "Reached end of get_coerce_type - " << *field_ty);
        }

        void visit_struct(::HIR::ItemPath ip, ::HIR::Struct& str) override {
            static Span sp;

            auto& struct_markings = str.m_struct_markings;
            if (struct_markings.coerce_unsized_index == ~0u) {
                return;
            }

            size_t idx = 0;
            auto cut = get_coerce_type(sp, ip, str, idx);
            struct_markings.coerce_param = idx;
            struct_markings.coerce_unsized = cut;
        }
    };

} // namespace

void ConvertHIR_Markings(::HIR::Crate& crate) {
    MarkingsVisitor exp{crate};
    exp.visit_crate(crate);

    // Visit again, visiting all structs and filling the coerce_unsized data
    Visitor2 exp2{crate.m_types};
    exp2.visit_crate(crate);
}

#include "hir_conv_main_bindings.h"
#include "hir_hir.h"
#include "hir_expr.h"
#include "hir_visitor.h"
#include "hir_typeck_static.h"
#include <std/mem/obj_pool.h>
#include <algorithm> // std::remove_if

namespace resolve_ufcs {
    void expand_trait_impl_type_defaults(const ::HIR::Crate& crate, const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) {
        Span sp;
        const auto& trait = crate.get_trait_by_path(sp, trait_path);
        auto ms = MonomorphStatePtr(crate.m_types, &impl.m_type, &impl.m_trait_args, nullptr);

        while (impl.m_trait_args.m_types.size() < trait.m_params.m_types.size()) {
            const auto& def = trait.m_params.m_types[impl.m_trait_args.m_types.size()];
            auto ty = ms.monomorph_type(sp, def.m_default);
            DEBUG("Add default trait arg " << ty << " from " << def.m_default);
            impl.m_trait_args.m_types.push_back(mv$(ty));
        }
    }

    class UfcsVisitor: public ::HIR::Visitor {
        const ::HIR::Crate& m_crate;
        bool m_visit_exprs;
        bool m_run_eat;

        typedef ::std::vector<::std::pair<const ::HIR::SimplePath*, const ::HIR::Trait*>> t_trait_imports;
        t_trait_imports m_traits;

        StaticTraitResolve m_resolve;
        bool m_in_trait_def = false;
        const ::HIR::TypeRef* m_current_type = nullptr;
        const ::HIR::Trait* m_current_trait = nullptr;
        const ::HIR::ItemPath* m_current_trait_path = nullptr;
        bool m_in_expr = false;
        HIR::SimplePath m_cur_mod_path;

    public:
        UfcsVisitor(const ::HIR::Crate& crate, bool visit_exprs)
            : ::HIR::Visitor(nullptr, crate.m_types)
            , m_crate(crate)
            , m_visit_exprs(visit_exprs)
            , m_run_eat(visit_exprs)
            , // Defaults to running when doing second-pass
            m_resolve(crate)
        {
        }

        struct ModTraitsGuard {
            UfcsVisitor* v;
            t_trait_imports old_imports;

            ModTraitsGuard(UfcsVisitor& v, t_trait_imports old_imports)
                : v(&v)
                , old_imports(mv$(old_imports))
            {
            }

            ModTraitsGuard(ModTraitsGuard&& x)
                : v(x.v)
                , old_imports(mv$(x.old_imports))
            {
                x.v = nullptr;
            }

            ModTraitsGuard& operator=(ModTraitsGuard&&) = delete;

            ~ModTraitsGuard() {
                if (v) {
                    DEBUG("Stack pop: " << this->v->m_traits.size() << " -> " << this->old_imports.size());
                    this->v->m_traits = mv$(this->old_imports);
                    v = nullptr;
                }
            }
        };

        ModTraitsGuard push_mod_traits(HIR::SimplePath path, const ::HIR::Module& mod) {
            static Span sp;
            DEBUG("");
            ModTraitsGuard rv{*this, mv$(this->m_traits)};
            for (const auto& trait_path : mod.m_traits) {
                DEBUG("- " << trait_path);
                m_traits.push_back(::std::make_pair(&trait_path, &m_crate.get_trait_by_path(sp, trait_path)));
            }
            m_cur_mod_path = std::move(path);
            return rv;
        }

        void visit_module(::HIR::ItemPath p, ::HIR::Module& mod) override {
            auto _ = this->push_mod_traits(p.get_simple_path(), mod);
            ::HIR::Visitor::visit_module(p, mod);
        }

        void visit_params(::HIR::GenericParams& params) {
            TRACE_FUNCTION_F(params.fmt_args() << params.fmt_bounds());

            // Custom visitor to prevent running of EAT on type paramerter defaults
            auto saved_run_eat = m_run_eat;
            m_run_eat = false;
            for (auto& tps : params.m_types) {
                this->visit_type(tps.m_default);
            }
            m_run_eat = saved_run_eat;

            for (auto& bound : params.m_bounds) {
                visit_generic_bound(bound);
            }

            // Re-populate the resolve index, as the above has changed them
            m_resolve.prep_indexes(Span());
        }

        void visit_union(::HIR::ItemPath p, ::HIR::Union& item) override {
            auto _ = m_resolve.set_impl_generics(MetadataType::None, item.m_params);
            auto ty = m_crate.m_types.path(HIR::GenericPath(p.get_simple_path()), &item);
            m_current_type = &ty;
            ::HIR::Visitor::visit_union(p, item);
            m_current_type = nullptr;
        }

        void visit_struct(::HIR::ItemPath p, ::HIR::Struct& item) override {
            auto _ = m_resolve.set_impl_generics(item.m_struct_markings.dst_type, item.m_params);
            auto ty = m_crate.m_types.path(HIR::GenericPath(p.get_simple_path()), &item);
            m_current_type = &ty;
            ::HIR::Visitor::visit_struct(p, item);
            m_current_type = nullptr;
        }

        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override {
            auto _ = m_resolve.set_impl_generics(MetadataType::None, item.m_params);
            auto ty = m_crate.m_types.path(HIR::GenericPath(p.get_simple_path()), &item);
            m_current_type = &ty;
            ::HIR::Visitor::visit_enum(p, item);
            m_current_type = nullptr;
        }

        void visit_function(::HIR::ItemPath p, ::HIR::Function& item) override {
            auto _ = m_resolve.set_item_generics(item.m_params);
            ::HIR::Visitor::visit_function(p, item);
        }

        void visit_type_alias(::HIR::ItemPath p, ::HIR::TypeAlias& item) override {
            // NOTE: Disabled, because generics in type aliases are never checked
            // Re-enabled to resolve a UFCS properly (1.90.0 libcore)
            auto _ = m_resolve.set_item_generics(item.m_params);
            ::HIR::Visitor::visit_type_alias(p, item);
        }

        void visit_trait(::HIR::ItemPath p, ::HIR::Trait& trait) override {
            //TRACE_FUNCTION_F("impl" << impl.m_params.fmt_args() << " " << impl.m_type << " (mod=" << impl.m_src_module << ")");
            m_in_trait_def = true;
            m_current_trait = &trait;
            m_current_trait_path = &p;
            //auto _ = m_resolve.set_cur_trait(p, trait);
            auto _ = m_resolve.set_impl_generics(MetadataType::TraitObject, trait.m_params);
            ::HIR::Visitor::visit_trait(p, trait);
            m_current_trait = nullptr;
            m_in_trait_def = false;
        }

        void visit_type_impl(::HIR::TypeImpl& impl) override {
            TRACE_FUNCTION_F("impl" << impl.m_params.fmt_args() << " " << impl.m_type << " (mod=" << impl.m_src_module << ")");
            auto _t = this->push_mod_traits(impl.m_src_module, this->m_crate.get_mod_by_path(Span(), impl.m_src_module));
            auto _g = m_resolve.set_impl_generics(impl.m_type, impl.m_params);
            m_current_type = &impl.m_type;
            ::HIR::Visitor::visit_type_impl(impl);
            m_current_type = nullptr;
        }

        void visit_marker_impl(const ::HIR::SimplePath& trait_path, ::HIR::MarkerImpl& impl) override {
            ::HIR::ItemPath p(impl.m_type, trait_path, impl.m_trait_args);
            TRACE_FUNCTION_F("impl" << impl.m_params.fmt_args() << " " << trait_path << impl.m_trait_args << " for " << impl.m_type << " (mod=" << impl.m_src_module << ")");
            auto _t = this->push_mod_traits(impl.m_src_module, this->m_crate.get_mod_by_path(Span(), impl.m_src_module));
            auto _g = m_resolve.set_impl_generics(impl.m_type, impl.m_params);

            // TODO: Push a bound that `Self: ThisTrait`
            m_current_type = &impl.m_type;
            m_current_trait = &m_crate.get_trait_by_path(Span(), trait_path);
            m_current_trait_path = &p;

            // The implemented trait is always in scope
            m_traits.push_back(::std::make_pair(&trait_path, m_current_trait));
            ::HIR::Visitor::visit_marker_impl(trait_path, impl);
            m_traits.pop_back();

            m_current_trait = nullptr;
            m_current_type = nullptr;
        }

        void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
            ::HIR::ItemPath p(impl.m_type, trait_path, impl.m_trait_args);
            TRACE_FUNCTION_F("impl" << impl.m_params.fmt_args() << " " << trait_path << impl.m_trait_args << " for " << impl.m_type << " (mod=" << impl.m_src_module << ")");
            auto _t = this->push_mod_traits(impl.m_src_module, this->m_crate.get_mod_by_path(Span(), impl.m_src_module));
            auto _g = m_resolve.set_impl_generics(MetadataType::Unknown, impl.m_params);

            expand_trait_impl_type_defaults(m_crate, trait_path, impl);

            m_current_type = &impl.m_type;
            m_current_trait = &m_crate.get_trait_by_path(Span(), trait_path);
            m_current_trait_path = &p;
            m_traits.push_back(::std::make_pair(&trait_path, m_current_trait));

            this->visit_type(impl.m_type);
            m_resolve.update_impl_self_metadata(impl.m_type);

            // TODO: Handle resolution of all items in m_resolve.m_type_equalities
            // - params might reference each other, so `set_item_generics` has to have been called
            // - But `m_type_equalities` can end up with non-resolved UFCS paths
            for (auto& e : m_resolve.m_type_equalities) {
                visit_type(e.second.ty);
            }

            // The implemented trait is always in scope
            ::HIR::Visitor::visit_trait_impl(trait_path, impl);
            m_traits.pop_back();

            m_current_trait = nullptr;
            m_current_type = nullptr;
        }

        void visit_expr(::HIR::ExprPtr& expr) override {
            struct ExprVisitor: public ::HIR::ExprVisitorDef {
                UfcsVisitor& upper_visitor;
                ::HIR::ExprNodeP m_replacement;

                ExprVisitor(UfcsVisitor& uv)
                    : ::HIR::ExprVisitorDef(uv.m_crate.m_types)
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

                void visit_node_ptr(::HIR::ExprNodeP& node_ptr) {
                    ::HIR::ExprVisitorDef::visit_node_ptr(node_ptr);
                    if (m_replacement) {
                        m_replacement->m_res_type = node_ptr->m_res_type;
                        m_replacement.swap(node_ptr);
                        m_replacement.reset();
                    }
                }

                // Custom to visit the inner expression
                void visit(::HIR::ExprNode_ArraySized& node) override {
                    auto& as = node.m_size;
                    if (as.is_Unevaluated()) {
                        upper_visitor.visit_constgeneric(as.as_Unevaluated());
                    }
                    ::HIR::ExprVisitorDef::visit(node);
                }

                // Custom visitor for enum/struct constructors
                void visit(::HIR::ExprNode_CallPath& node) override {
                    ::HIR::ExprVisitorDef::visit(node);
                    const Span& sp = node.span();
                    if (node.m_path.m_data.is_Generic()) {
                        // If it points to an enum, rewrite
                        auto& gp = node.m_path.m_data.as_Generic();
                        if (gp.m_path.components().size() > 1) {
                            const auto& ent = upper_visitor.m_crate.get_typeitem_by_path(sp, gp.m_path, /*ign_crate*/ false, true);
                            if (ent.is_Enum() && ent.as_Enum().find_variant(gp.m_path.components().back()) != SIZE_MAX) {
                                // Rewrite!
                                m_replacement.reset(upper_visitor.m_crate.m_pool->make<::HIR::ExprNode_TupleVariant>(sp, mv$(gp), /*is_struct*/ false, mv$(node.m_args)));
                                DEBUG(&node << ": Replacing with TupleVariant " << m_replacement.get());
                                return;
                            }
                        }
                    }

                    // If this is pointing at a constant/static/associated constant, change to CallValue
                    MonomorphState discard(upper_visitor.m_crate.m_types);
                    auto v = upper_visitor.m_resolve.get_value(node.span(), node.m_path, discard, true);
                    if (v.is_Constant() || v.is_Static()) {
                        auto* value_node = upper_visitor.m_crate.m_pool->make<HIR::ExprNode_PathValue>(sp, std::move(node.m_path), v.is_Constant() ? ::HIR::ExprNode_PathValue::Target::CONSTANT : v.is_Static() ? ::HIR::ExprNode_PathValue::Target::STATIC : ::HIR::ExprNode_PathValue::Target::UNKNOWN);
                        value_node->m_res_type = upper_visitor.m_crate.m_types.infer();
                        m_replacement.reset(upper_visitor.m_crate.m_pool->make<::HIR::ExprNode_CallValue>(sp, ::HIR::ExprNodeP(value_node), mv$(node.m_args)));
                        DEBUG(&node << ": Replacing with CallValue " << m_replacement.get());
                        return;
                    }
                }

                // Custom visitor for enum/struct constructors
                void visit(::HIR::ExprNode_PathValue& node) override {
                    ::HIR::ExprVisitorDef::visit(node);
                    const Span& sp = node.span();
                    if (node.m_path.m_data.is_Generic()) {
                        // If it points to an enum, set binding
                        auto& gp = node.m_path.m_data.as_Generic();
                        if (gp.m_path.components().size() > 1) {
                            const auto& ent = upper_visitor.m_crate.get_typeitem_by_path(sp, gp.m_path, /*ign_crate*/ false, true);
                            if (ent.is_Enum()) {
                                const auto& enm = ent.as_Enum();
                                auto idx = enm.find_variant(gp.m_path.components().back());
                                if (enm.m_data.is_Value() || enm.m_data.as_Data().at(idx).type == upper_visitor.m_crate.m_types.unit()) {
                                    m_replacement.reset(upper_visitor.m_crate.m_pool->make<::HIR::ExprNode_UnitVariant>(sp, mv$(gp), /*is_struct*/ false));
                                    DEBUG(&node << ": Replacing with UnitVariant " << m_replacement.get());
                                } else {
                                    node.m_target = ::HIR::ExprNode_PathValue::ENUM_VAR_CONSTR;
                                }
                                return;
                            }
                        }

                        // TODO: Struct?
                    }
                }
#if 1
                void visit(::HIR::ExprNode_StructLiteral& node) override {
                    ::HIR::ExprVisitorDef::visit(node);
                    const Span& sp = node.span();
                    if (node.m_type->is_Path() && node.m_type->as_Path().path.m_data.is_Generic()) {
                        // If it points to an enum, set binding
                        auto data = node.m_type->clone_data();
                        auto& p = data.as_Path().path;
                        auto& gp = p.m_data.as_Generic();
                        if (gp.m_path.components().size() > 1) {
                            const auto& ent = upper_visitor.m_crate.get_typeitem_by_path(sp, gp.m_path, /*ign_crate*/ false, true);
                            if (ent.is_Enum()) {
                                DEBUG(&node << ": Tagging as an enum");
                                node.m_is_struct = false;
                                auto enum_path = std::move(gp);
                                auto var_name = enum_path.m_path.pop_component();
                                auto enum_ty = upper_visitor.m_crate.m_types.path(std::move(enum_path), &ent.as_Enum());
                                p = ::HIR::Path(std::move(enum_ty), std::move(var_name));
                            }
                        }
                        node.m_type = upper_visitor.m_crate.m_types.intern(std::move(data));
                    }
                }
#endif

                // NOTE: Custom needed for trait scoping
                void visit(::HIR::ExprNode_Block& node) override {
                    if (node.m_traits.size() == 0 && node.m_local_mod.components().size() > 0) {
                        const auto& mod = upper_visitor.m_crate.get_mod_by_path(node.span(), node.m_local_mod);
                        for (const auto& trait_path : mod.m_traits) {
                            node.m_traits.push_back(::std::make_pair(&trait_path, &upper_visitor.m_crate.get_trait_by_path(node.span(), trait_path)));
                        }
                    }
                    for (const auto& trait_ref : node.m_traits) {
                        upper_visitor.m_traits.push_back(trait_ref);
                    }

                    ::HIR::ExprVisitorDef::visit(node);

                    for (unsigned int i = 0; i < node.m_traits.size(); i++) {
                        upper_visitor.m_traits.pop_back();
                    }
                }
            };

            if (m_visit_exprs && expr.get() != nullptr) {
                auto saved_in_expr = m_in_expr;
                m_in_expr = true;
                ExprVisitor v{*this};
                (*expr).visit(v);
                m_in_expr = saved_in_expr;
            }
        }

        bool locate_trait_item_in_bounds(::HIR::Visitor::PathContext pc, const ::HIR::TypeRef& tr, const ::HIR::GenericParams& params, ::HIR::Path::Data& pd) {
            static Span sp;
            //const auto& name = pd.as_UfcsUnknown().item;
            for (const auto& b : params.m_bounds) {
                if (const auto* e = b.opt_TraitBound()) {
                    DEBUG("- " << e->type << " : " << e->trait.m_path);
                    // Bounds are keyed by the semantic HIR type. Binding
                    // metadata and erased regions can differ depending on
                    // which path was resolved first.
                    if (e->type == tr || e->type->equals_ignoring_regions(tr)) {
                        DEBUG(" - Match");
                        if (locate_in_trait_and_set(pc, e->trait.m_path, m_crate.get_trait_by_path(sp, e->trait.m_path.m_path), pd)) {
                            return true;
                        }
                    }
                }
                // -
            }
            return false;
        }

        ::HIR::Path::Data get_ufcs_known(::HIR::Visitor::PathContext pc, ::HIR::Path::Data::Data_UfcsUnknown e, ::HIR::GenericPath trait_path_real, const ::HIR::Trait& trait) const {
            struct MonomorphEraseHrls: public Monomorphiser {
                explicit MonomorphEraseHrls(HIR::TypeInterner& types): Monomorphiser(types) {}

                ::HIR::TypeRef get_type(const Span& sp, const ::HIR::GenericRef& g) const override {
                    return m_types.generic(g.name, g.binding);
                }

                ::HIR::ConstGeneric get_value(const Span& sp, const ::HIR::GenericRef& g) const override {
                    return g;
                }

                ::HIR::LifetimeRef get_lifetime(const Span& sp, const ::HIR::GenericRef& g) const override {
                    if (g.group() == 3) {
                        return HIR::LifetimeRef();
                    } else {
                        return HIR::LifetimeRef(g.binding);
                    }
                }
            };

            auto trait_path = MonomorphEraseHrls(m_crate.m_types).monomorph_genericpath(Span(), trait_path_real);
            if (pc == HIR::Visitor::PathContext::TYPE) {
                // If the trait has missing type argumenst, replace them with the defaults
                // Get trait, check if the type has ATCs
                const auto& aty = trait.m_types.at(e.item);
                if (e.params.m_lifetimes.size() < aty.m_generics.m_lifetimes.size()) {
                    e.params.m_lifetimes.resize(aty.m_generics.m_lifetimes.size());
                }
            }
            // TODO: Only do this when there's multiple options?
            if (m_in_expr) {
                for (auto& type : trait_path.m_params.m_types) {
                    type = m_crate.m_types.infer();
                }
            }
            return ::HIR::Path::Data::make_UfcsKnown({mv$(e.type), mv$(trait_path), mv$(e.item), mv$(e.params)});
        }

        static bool locate_item_in_trait(::HIR::Visitor::PathContext pc, const ::HIR::Trait& trait, ::HIR::Path::Data& pd) {
            const auto& e = pd.as_UfcsUnknown();

            switch (pc) {
                case ::HIR::Visitor::PathContext::VALUE:
                    if (trait.m_values.find(e.item) != trait.m_values.end()) {
                        return true;
                    }
                    break;
                case ::HIR::Visitor::PathContext::TRAIT:
                    break;
                case ::HIR::Visitor::PathContext::TYPE:
                    if (trait.m_types.find(e.item) != trait.m_types.end()) {
                        return true;
                    }
                    break;
            }
            return false;
        }

        // Locate the item in `pd` and set `pd` to UfcsResolved if found
        // TODO: This code may end up generating paths without the type information they should contain
        // OR, generate paths with too much type information
        bool locate_in_trait_and_set(::HIR::Visitor::PathContext pc, const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait, ::HIR::Path::Data& pd) {
            TRACE_FUNCTION_F(trait_path);
            // TODO: Get the span from caller
            static Span _sp;
            const auto& sp = _sp;
            if (locate_item_in_trait(pc, trait, pd)) {
                pd = get_ufcs_known(pc, mv$(pd.as_UfcsUnknown()), trait_path.clone(), trait);
                return true;
            }

            auto pp = trait_path.m_params.clone();
            while (pp.m_types.size() < trait.m_params.m_types.size()) {
                auto idx = pp.m_types.size();
                const auto& def = trait.m_params.m_types[idx].m_default;
                if (def->is_Infer()) {
                    ERROR(sp, E0000, "");
                }
                if (def == m_crate.m_types.self()) {
                    // TODO: This has to be the _exact_ same type, including future ivars.
                    pp.m_types.push_back(pd.as_UfcsUnknown().type);
                    continue;
                }
                TODO(sp, "Monomorphise default arg " << def << " for trait path " << trait_path);
            }

            auto monomorph_cb = MonomorphStatePtr(m_crate.m_types, &pd.as_UfcsUnknown().type, &pp, nullptr);
            ::HIR::GenericPath par_trait_path_tmp;
            auto monomorph_gp_if_needed = [&](const ::HIR::GenericPath& tpl) -> const ::HIR::GenericPath& {
                // NOTE: This doesn't monomorph if the parameter set is the same
                if (monomorphise_genericpath_needed(tpl) /*&& tpl.m_params != trait_path.m_params*/) {
                    DEBUG("[monomorph_gp_if_needed] Monomorph tpl=" << tpl);
                    return par_trait_path_tmp = monomorph_cb.monomorph_genericpath(sp, tpl, false /*no infer*/);
                } else {
                    return tpl;
                }
            };

            // Search supertraits (recursively)
            static HIR::GenericParams empty_gp;
            for (const auto& pt : trait.m_parent_traits) {
                auto _ = monomorph_cb.push_hrb(pt.m_hrtbs ? *pt.m_hrtbs : empty_gp);
                const auto& par_trait_path = monomorph_gp_if_needed(pt.m_path);
                DEBUG("- Check " << par_trait_path);
                if (locate_in_trait_and_set(pc, par_trait_path, *pt.m_trait_ptr, pd)) {
                    return true;
                }
            }
            for (const auto& pt : trait.m_all_parent_traits) {
                auto _ = monomorph_cb.push_hrb(pt.m_hrtbs ? *pt.m_hrtbs : empty_gp);
                const auto& par_trait_path = monomorph_gp_if_needed(pt.m_path);
                DEBUG("- Check (all) " << par_trait_path);
                if (locate_item_in_trait(pc, *pt.m_trait_ptr, pd)) {
                    // TODO: Don't clone if this is from the temp.
                    pd = get_ufcs_known(pc, mv$(pd.as_UfcsUnknown()), par_trait_path.clone(), *pt.m_trait_ptr);
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
            this->m_resolve.find_impl(sp, trait_path.m_path, nullptr, type, [&](const auto& impl, bool fuzzy) -> bool {
                auto pp = impl.get_trait_params(m_crate.m_types);
                // Replace all placeholder parameters (group 2) with ivars (empty types)
                struct KillPlaceholders: public Monomorphiser {
                    explicit KillPlaceholders(HIR::TypeInterner& types): Monomorphiser(types) {}

                    ::HIR::TypeRef get_type(const Span& sp, const ::HIR::GenericRef& ty) const override {
                        if (ty.is_placeholder()) {
                            return m_types.infer();
                        }
                        return m_types.generic(ty.name, ty.binding);
                    }
                    ::HIR::ConstGeneric get_value(const Span& sp, const ::HIR::GenericRef& val) const override {
                        return val;
                    }
                    ::HIR::LifetimeRef get_lifetime(const Span& sp, const ::HIR::GenericRef& g) const override {
                        return HIR::LifetimeRef(g.binding);
                    }
                };

                pp = KillPlaceholders(m_crate.m_types).monomorph_path_params(sp, pp, true);
                DEBUG("FOUND impl from " << impl);
                // If this has already found an option...
                if (auto* inner_e = pd.opt_UfcsKnown()) {
                    // Compare all path params, and set different params to _
                    assert(pp.m_types.size() == inner_e->trait.m_params.m_types.size());
                    for (unsigned int i = 0; i < pp.m_types.size(); i++) {
                        auto& e_ty = inner_e->trait.m_params.m_types[i];
                        const auto& this_ty = pp.m_types[i];
                        if (e_ty->is_Infer() && e_ty->as_Infer().index == ~0u) {
                            // Already _, leave as is
                        } else if (e_ty != this_ty) {
                            e_ty = m_crate.m_types.infer();
                        } else {
                            // Equal, good
                        }
                    }
                } else {
                    DEBUG("pp = " << pp);
                    // Otherwise, set to the current result.
                    pd = get_ufcs_known(pc, mv$(e), ::HIR::GenericPath(trait_path.m_path, mv$(pp)), trait);
                }
                return false;
            });
            return pd.is_UfcsKnown();
        }

        bool locate_in_trait_impl_and_set(const Span& sp, ::HIR::Visitor::PathContext pc, const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait, ::HIR::Path::Data& pd) {
            if (this->locate_item_in_trait(pc, trait, pd)) {
                return set_from_trait_impl(sp, pc, trait_path, trait, pd);
            } else {
                DEBUG("- Item " << pd.as_UfcsUnknown().item << " not in trait " << trait_path.m_path);
            }

            // Search supertraits (recursively)
            // NOTE: This runs before "Resolve HIR Markings", so m_all_parent_traits can't be used exclusively
            for (const auto& pt : trait.m_parent_traits) {
                // TODO: Modify path parameters based on the current trait's params
                if (locate_in_trait_impl_and_set(sp, pc, pt.m_path, *pt.m_trait_ptr, pd)) {
                    return true;
                }
            }
            for (const auto& pt : trait.m_all_parent_traits) {
                if (this->locate_item_in_trait(pc, *pt.m_trait_ptr, pd)) {
                    // TODO: Modify path parameters based on the current trait's params
                    return set_from_trait_impl(sp, pc, pt.m_path, *pt.m_trait_ptr, pd);
                } else {
                    DEBUG("- Item " << pd.as_UfcsUnknown().item << " not in trait " << trait_path.m_path);
                }
            }
            return false;
        }

        bool resolve_UfcsUnknown_inherent(const ::HIR::SimplePath& vis_path, const ::HIR::Path& p, ::HIR::Visitor::PathContext pc, ::HIR::Path::Data& pd) {
            auto& e = pd.as_UfcsUnknown();
            TRACE_FUNCTION_F(e.type);
            return m_crate.find_type_impls(e.type, HIR::ResolvePlaceholdersNop(), [&](const auto& impl) {
                DEBUG("- matched inherent impl" << impl.m_params.fmt_args() << " " << impl.m_type);
                // Search for item in this block
                switch (pc) {
                    case ::HIR::Visitor::PathContext::VALUE:
                        if (impl.m_methods.find(e.item) != impl.m_methods.end()) {
                            // HACK: Allow access to privates of `fmt:rt::Argument`
                            if (e.type->is_Path() && e.type->as_Path().path.m_data.is_Generic() && e.type->as_Path().path.m_data.as_Generic().m_path == m_crate.get_lang_item_path_opt("format_argument")) {
                                // Allow
                            } else if (!impl.m_methods.at(e.item).publicity.is_visible(vis_path)) {
                                DEBUG("Private");
                                return false;
                            }
                        } else if (impl.m_constants.find(e.item) != impl.m_constants.end()) {
                            if (!impl.m_constants.at(e.item).publicity.is_visible(vis_path)) {
                                DEBUG("Private");
                                return false;
                            }
                        } else {
                            return false;
                        }
                        // Found it, just keep going (don't care about details here)
                        break;
                    case ::HIR::Visitor::PathContext::TRAIT:
                    case ::HIR::Visitor::PathContext::TYPE:
                        return false;
                }

                auto new_data = ::HIR::Path::Data::make_UfcsInherent({mv$(e.type), mv$(e.item), mv$(e.params)});
                pd = mv$(new_data);
                DEBUG("- Resolved, replace with " << p);
                return true;
            });
        }

        bool resolve_UfcsUnknown_trait(const ::HIR::Path& p, ::HIR::Visitor::PathContext pc, ::HIR::Path::Data& pd) {
            static Span sp;
            auto& e = pd.as_UfcsUnknown();
            const bool collapse_to_subtrait = m_crate.feature_enabled("supertrait_item_shadowing");
            ::std::vector<::std::pair<::HIR::SimplePath, ::HIR::Path::Data>> candidates;
            DEBUG("m_traits.size() = " << m_traits.size());
            for (const auto& trait_info : m_traits) {
                const auto& trait = *trait_info.second;

                DEBUG(e.item << " in? " << *trait_info.first);
                switch (pc) {
                    case ::HIR::Visitor::PathContext::VALUE:
                        if (trait.m_values.find(e.item) == trait.m_values.end()) {
                            continue;
                        }
                        break;
                    case ::HIR::Visitor::PathContext::TRAIT:
                    case ::HIR::Visitor::PathContext::TYPE:
                        if (trait.m_types.find(e.item) == trait.m_types.end()) {
                            continue;
                        }
                        break;
                }
                DEBUG("- Trying trait " << *trait_info.first);

                auto trait_path = ::HIR::GenericPath(*trait_info.first);
                trait_path.m_params.m_types.reserve(trait.m_params.m_types.size());
                for (size_t i = 0; i < trait.m_params.m_types.size(); i++) {
                    trait_path.m_params.m_types.push_back(m_crate.m_types.infer());
                }

                // TODO: If there's only one trait with this name, assume it's the correct one.

                // TODO: Search supertraits
                // TODO: Should impls be searched first, or item names?
                // - Item names add complexity, but impls are slower
                if (!collapse_to_subtrait) {
                    if (this->locate_in_trait_impl_and_set(sp, pc, mv$(trait_path), trait, pd)) {
                        return true;
                    }
                    continue;
                }

                auto candidate_data = ::HIR::Path::Data::make_UfcsUnknown({
                    e.type,
                    e.item,
                    e.params.clone(),
                });
                if (this->locate_in_trait_impl_and_set(sp, pc, mv$(trait_path), trait, candidate_data)) {
                    candidates.push_back(::std::make_pair(*trait_info.first, mv$(candidate_data)));
                }
            }

            if (collapse_to_subtrait && !candidates.empty()) {
                ::std::vector<::HIR::SimplePath> candidate_traits;
                candidate_traits.reserve(candidates.size());
                for (const auto& candidate : candidates) {
                    candidate_traits.push_back(candidate.first);
                }
                if (const auto selected = m_crate.find_most_specific_trait(sp, candidate_traits)) {
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

            if (m_run_eat) {
                TRACE_FUNCTION_FR(ty, ty);
                std::vector<HIR::TypeRef> stack;
                if (ty->is_Path()) {
                    stack.push_back(ty);
                }
                while (m_resolve.expand_associated_types_single(sp, ty)) {
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
                    //ASSERT_BUG(sp, !visit_ty_with(ty, [&](const HIR::TypeRef& ty)->bool { return TU_TEST1(ty.data(), Generic, .is_placeholder()); }), "Encountered placeholder - " << ty);
                    rewrite_ty_with(m_crate.m_types, ty, [&](HIR::TypeRef& rewritten, HIR::TypeData& data) -> bool {
                        if (TU_TEST1(data, Generic, .is_placeholder())) {
                            rewritten = m_crate.m_types.infer();
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
            auto saved_visit_exprs = m_visit_exprs;
            m_visit_exprs = true;
            ::HIR::Visitor::visit_constgeneric(val);
            m_visit_exprs = saved_visit_exprs;
        }

        void visit_path(::HIR::Path& p, ::HIR::Visitor::PathContext pc) override {
            static Span sp;

            if (auto* pe = p.m_data.opt_UfcsKnown()) {
                // If the trait has missing type argumenst, replace them with the defaults
                auto& tp = pe->trait;
                const auto& trait = m_resolve.m_crate.get_trait_by_path(sp, tp.m_path);

                if (tp.m_params.m_types.size() < trait.m_params.m_types.size()) {
                    //TODO(sp, "Defaults in UfcsKnown - " << p << " - " << tp.m_params << " vs " << trait.m_params.fmt_args());
                    // TOOD: Where does this usually get expanded then?
                }
            }

            // TODO: Would like to remove this, but it's required still (for expressions)
            if (auto* pe = p.m_data.opt_UfcsUnknown()) {
                auto& e = *pe;
                TRACE_FUNCTION_FR("UfcsUnknown - p=" << p, p);

                this->visit_type(e.type);
                this->visit_path_params(e.params);

                // If processing a trait, and the type is 'Self', search for the type/method on the trait
                // - Explicitly encoded because `Self::Type` has a different meaning to `MyType::Type` (the latter will search bounds first)
                // - NOTE: Could be in an inherent block, where there's no trait
                if (/*m_current_type &&*/ m_current_trait && e.type == m_crate.m_types.self()) {
                    ::HIR::GenericPath trait_path;
                    if (m_current_trait_path->trait_path()) {
                        trait_path = ::HIR::GenericPath(*m_current_trait_path->trait_path());
                        trait_path.m_params = m_current_trait_path->trait_args()->clone();
                    } else {
                        trait_path = ::HIR::GenericPath(m_current_trait_path->get_simple_path());
                        trait_path.m_params = m_current_trait->m_params.make_nop_params(m_crate.m_types, 0);
                    }
                    if (locate_in_trait_and_set(pc, trait_path, *m_current_trait, p.m_data)) {
                        assert(!p.m_data.is_UfcsUnknown());
                        // Success!
                        // - If in an expression (and not in a `trait` provided impl), clear the params
                        if (m_in_expr && !m_in_trait_def) {
                            for (auto& t : p.m_data.as_UfcsKnown().trait.m_params.m_types) {
                                t = m_crate.m_types.infer();
                            }
                        }
                        DEBUG("Found in Self (trait), p = " << p);
                        return;
                    }
                    DEBUG("- Item " << e.item << " not found in Self - ty=" << e.type);
                }

                // NOTE: Replace `Self` now
                // - Now that the only `Self`-specific logic is done, replace so the lookup code works.
                if (m_current_type) {
                    rewrite_path_tys_with(m_crate.m_types, p, [&](HIR::TypeRef& t, HIR::TypeData& data) -> bool {
                        if (data.is_Generic() && data.as_Generic().binding == GENERIC_Self) {
                            t = *m_current_type;
                        }
                        return false;
                    });
                }

                // Search for matching impls in current generic blocks
                if (m_resolve.m_item_generics != nullptr && locate_trait_item_in_bounds(pc, e.type, *m_resolve.m_item_generics, p.m_data)) {
                    DEBUG("Found in item params, p = " << p);
                    assert(!p.m_data.is_UfcsUnknown());
                    return;
                }
                if (m_resolve.m_impl_generics != nullptr && locate_trait_item_in_bounds(pc, e.type, *m_resolve.m_impl_generics, p.m_data)) {
                    DEBUG("Found in impl params, p = " << p);
                    assert(!p.m_data.is_UfcsUnknown());
                    return;
                }

                // `<dyn Trait>::item` can name an item supplied by a supertrait.
                // Resolve it from the trait object's principal trait before
                // looking for an implementation of the trait object type.
                if (const auto* trait_object = e.type->opt_TraitObject()) {
                    const auto& principal = trait_object->m_trait;
                    if (principal.m_trait_ptr && locate_in_trait_and_set(pc, principal.m_path, *principal.m_trait_ptr, p.m_data)) {
                        DEBUG("Found in trait object bounds, p = " << p);
                        assert(!p.m_data.is_UfcsUnknown());
                        return;
                    }
                }

                // TODO: Control ordering with a flag in UfcsUnknown
                // 1. Search for applicable inherent methods (COMES FIRST!)
                if (this->resolve_UfcsUnknown_inherent(m_cur_mod_path, p, pc, p.m_data)) {
                    assert(!p.m_data.is_UfcsUnknown());
                    return;
                }
                assert(p.m_data.is_UfcsUnknown());

                // If the type is the impl type, look for items AFTER generic lookup
                // TODO: Should this look up in-scope traits instead of hard-coding this hack?
                if (m_current_type && m_current_trait && e.type == *m_current_type) {
                    ::HIR::GenericPath trait_path;
                    if (m_current_trait_path->trait_path()) {
                        trait_path = ::HIR::GenericPath(*m_current_trait_path->trait_path());
                        trait_path.m_params = m_current_trait_path->trait_args()->clone();
                    } else {
                        trait_path = ::HIR::GenericPath(m_current_trait_path->get_simple_path());
                        trait_path.m_params = m_current_trait->m_params.make_nop_params(m_crate.m_types, 0);
                    }

                    if (locate_in_trait_and_set(pc, trait_path, *m_current_trait, p.m_data)) {
                        assert(!p.m_data.is_UfcsUnknown());
                        // Success!
                        if (m_in_expr) {
                            for (auto& t : p.m_data.as_UfcsKnown().trait.m_params.m_types) {
                                t = m_crate.m_types.infer();
                            }
                        }
                        DEBUG("Found in Self (impl" << (m_in_expr ? " expr" : "") << "), p = " << p);
                        return;
                    }
                    DEBUG("- Item " << e.item << " not found in Self - ty=" << e.type);
                }

                // If the inner type is a UFCS of a known trait, then search traits on that type
                if (e.type->is_Path() && e.type->as_Path().path.m_data.is_UfcsKnown()) {
                    auto& inner_pe = e.type->as_Path().path.m_data.as_UfcsKnown();
                    const auto& trait = m_crate.get_trait_by_path(sp, inner_pe.trait.m_path);
                    const auto& aty_def = trait.m_types.at(inner_pe.item);
                    auto mstate = MonomorphStatePtr(m_crate.m_types, &inner_pe.type, &inner_pe.trait.m_params, nullptr);
                    for (const auto& t : aty_def.m_trait_bounds) {
                        auto trait_path = mstate.monomorph_genericpath(sp, t.m_path, /*allow_infer*/ true);
                        DEBUG("Searching ATY bound: " << trait_path);
                        // Search within this (bounded) trait for the outer item
                        if (this->locate_in_trait_impl_and_set(sp, pc, mv$(trait_path), *t.m_trait_ptr, p.m_data)) {
                            assert(!p.m_data.is_UfcsUnknown());
                            return;
                        }
                    }
                    DEBUG("- Item " << e.item << " not found in ATY bounds");
                    // TODO: Search bounds with `where`?
                }

                // 2. Search all impls of in-scope traits for this method on this type
                if (this->resolve_UfcsUnknown_trait(p, pc, p.m_data)) {
                    assert(!p.m_data.is_UfcsUnknown());
                    return;
                }
                assert(p.m_data.is_UfcsUnknown());
                DEBUG("e.type = " << e.type);

                // If the inner is an enum, look for an enum variant? (check context)
                if ((pc == HIR::Visitor::PathContext::VALUE /*|| pc == HIR::Visitor::PathContext::PATTERN*/) && e.type->is_Path() && e.type->as_Path().binding.is_Enum()) {
                    const auto& enm = *e.type->as_Path().binding.as_Enum();
                    auto idx = enm.find_variant(e.item);
                    if (idx != SIZE_MAX) {
                        DEBUG("Found variant " << e.type << " #" << idx);
                        if (enm.m_data.is_Value() || !enm.m_data.as_Data()[idx].is_struct) {
                            auto gp = e.type->as_Path().path.m_data.as_Generic().clone();
                            gp.m_path += e.item;
                            if (e.params.has_params()) {
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
                    auto idx = enm.find_variant(e.item);
                    if (idx != SIZE_MAX) {
                        DEBUG("Found variant " << e.type << " #" << idx);
                        if (enm.m_data.is_Data() && enm.m_data.as_Data()[idx].is_struct) {
                            auto gp = e.type->as_Path().path.m_data.as_Generic().clone();
                            gp.m_path += e.item;
                            if (e.params.has_params()) {
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

            TU_MATCH_HDRA( (pat.m_data), {)
            default:
                break;
                TU_ARMA(Value, e) {
                    this->visit_pattern_Value(sp, pat, e.val);
                    if (e.val.is_Named() && e.val.as_Named().path.m_data.is_Generic() && e.val.as_Named().path.m_data.as_Generic().m_path.components().size() > 1) {
                        auto& gp = e.val.as_Named().path.m_data.as_Generic();
                        if (const auto* enm_p = m_crate.get_typeitem_by_path(sp, gp.m_path, false, true).opt_Enum()) {
                            unsigned idx = enm_p->find_variant(gp.m_path.components().back());
                            pat.m_data = ::HIR::Pattern::Data::make_PathValue({mv$(gp), ::HIR::Pattern::PathBinding::make_Enum({enm_p, idx})});
                        }
                    }
                }
                TU_ARMA(Range, e) {
                    if (e.start) {
                        this->visit_pattern_Value(sp, pat, *e.start);
                    }
                    if (e.end) {
                        this->visit_pattern_Value(sp, pat, *e.end);
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

            auto ty = m_crate.m_types.path(path.clone(), {});
            this->visit_type(ty);
            ASSERT_BUG(sp, ty->is_Path(), "Pattern associated type didn't resolve to a path - " << ty);

            const auto& te = ty->as_Path();
            ASSERT_BUG(sp, te.path.m_data.is_Generic(), "Pattern associated type didn't resolve to a generic path - " << ty);
            path = te.path.clone();

            if (te.binding.is_Struct()) {
                binding = ::HIR::Pattern::PathBinding::make_Struct(te.binding.as_Struct());
            } else if (te.binding.is_Union()) {
                binding = ::HIR::Pattern::PathBinding::make_Union(te.binding.as_Union());
            } else {
                ERROR(sp, E0000, "Pattern associated type didn't resolve to a struct or union - " << ty);
            }
        }

        void visit_pattern_Value(const Span& sp, const ::HIR::Pattern& pat, ::HIR::Pattern::Value& val) {
            TRACE_FUNCTION_F("pat=" << pat << ", val=" << val);
            if (auto* vep = val.opt_Named()) {
                auto& ve = *vep;
                TRACE_FUNCTION_F(ve.path);
                TU_MATCH_HDRA( (ve.path.m_data), {)
                TU_ARMA(Generic, pe) {
                        // Already done
                    }
                    TU_ARMA(UfcsUnknown, pe) {
                        BUG(sp, "UfcsUnknown still in pattern value - " << pat);
                    }
                    TU_ARMA(UfcsInherent, pe) {
                        bool rv = m_crate.find_type_impls(pe.type, HIR::ResolvePlaceholdersNop(), [&](const auto& impl) {
                            DEBUG("- matched inherent impl" << impl.m_params.fmt_args() << " " << impl.m_type);
                            // Search for item in this block
                            auto it = impl.m_constants.find(pe.item);
                            if (it != impl.m_constants.end()) {
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
                        bool rv = this->m_resolve.find_impl(sp, pe.trait.m_path, &pe.trait.m_params, pe.type, [&](const auto& impl, bool) {
                            if (!impl.m_data.is_TraitImpl()) {
                                return true;
                            }
                            ve.binding = &impl.m_data.as_TraitImpl().impl->m_constants.at(pe.item).data;
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
        auto new_end = ::std::remove_if(ig.generic.begin(), ig.generic.end(), [&ig, &fmt](::std::unique_ptr<T>& ty_impl) {
            const auto& type = ty_impl->m_type; // Using field accesses in templates feels so dirty
            const ::HIR::SimplePath* path = type->get_sort_path();

            if (path) {
                DEBUG(*path << " += " << FMT_CB(os, fmt(os, *ty_impl)));
                ig.named[*path].push_back(mv$(ty_impl));
            } else if (type->is_Path() || type->is_Generic()) {
                return false;
            } else {
                ig.non_named.push_back(mv$(ty_impl));
            }
            return true;
        });
        ig.generic.erase(new_end, ig.generic.end());
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
        push_index_impl_group_list(dst.non_named, src.non_named);
        push_index_impl_group_list(dst.generic, src.generic);
    }

    void push_index_impls(::HIR::Crate& dst, const ::HIR::Crate& src) {
        push_index_impl_group(dst.m_all_type_impls, src.m_type_impls);
        for (const auto& ig : src.m_trait_impls) {
            push_index_impl_group(dst.m_all_trait_impls[ig.first], ig.second);
        }
        for (const auto& ig : src.m_marker_impls) {
            push_index_impl_group(dst.m_all_marker_impls[ig.first], ig.second);
        }
    }

    // --- Indexing of inherent methods ---
    void push_index_inherent_methods_list(::HIR::InherentCache& icache, const HIR::SimplePath& lang_Box, const ::std::vector<std::unique_ptr<HIR::TypeImpl>>& src) {
        Span sp;
        for (const auto& ti : src) {
            const auto& impl = *ti;
            TRACE_FUNCTION_F("impl" << impl.m_params.fmt_args() << " " << impl.m_type);
            icache.insert_all(sp, impl, lang_Box);
        }
    }

    void push_index_inherent_methods(::HIR::InherentCache& icache, const HIR::SimplePath& lang_Box, const ::HIR::Crate& src) {
        TRACE_FUNCTION_F("src = " << src.m_crate_name);
        for (const auto& e : src.m_type_impls.named) {
            push_index_inherent_methods_list(icache, lang_Box, e.second);
        }
        push_index_inherent_methods_list(icache, lang_Box, src.m_type_impls.non_named);
        push_index_inherent_methods_list(icache, lang_Box, src.m_type_impls.generic);
    }
} // namespace ""

using namespace resolve_ufcs;

void ConvertHIR_ResolveUFCS_Outer(::HIR::Crate& crate) {
    for (auto& impl_group : crate.m_trait_impls) {
        auto expand_list = [&](auto& impl_list) {
            for (auto& impl : impl_list) {
                expand_trait_impl_type_defaults(crate, impl_group.first, *impl);
            }
        };
        for (auto& named : impl_group.second.named) {
            expand_list(named.second);
        }
        expand_list(impl_group.second.non_named);
        expand_list(impl_group.second.generic);
    }

    UfcsVisitor exp{crate, false};
    exp.visit_crate(crate);
}

void ConvertHIR_ResolveUFCS(::HIR::Crate& crate) {
    UfcsVisitor exp{crate, true};
    exp.visit_crate(crate);
}

void ConvertHIR_ResolveUFCS_Expr(const ::HIR::Crate& crate, const ::HIR::ItemPath& ip, ::HIR::ExprPtr& expr_ptr) {
    TRACE_FUNCTION_F(ip);
    // Check innards but NOT the value
    UfcsVisitor exp{crate, true};
    exp.visit_expr(expr_ptr);
}

void ConvertHIR_ResolveUFCS_SortImpls(::HIR::Crate& crate) {
    // Sort impls!
    sort_impl_group<HIR::TypeImpl>(crate.m_type_impls, [](::std::ostream& os, const HIR::TypeImpl& i) {
        os << "impl" << i.m_params.fmt_args() << " " << i.m_type;
    });
    DEBUG("Type impl counts: " << crate.m_type_impls.named.size() << " path groups, " << crate.m_type_impls.non_named.size() << " primitive, " << crate.m_type_impls.generic.size() << " ungrouped");
    for (auto& impl_group : crate.m_trait_impls) {
        sort_impl_group<HIR::TraitImpl>(impl_group.second, [&](::std::ostream& os, const HIR::TraitImpl& i) {
            os << "impl" << i.m_params.fmt_args() << " " << impl_group.first << i.m_trait_args << " for " << i.m_type;
        });
    }
    for (auto& impl_group : crate.m_marker_impls) {
        sort_impl_group<HIR::MarkerImpl>(impl_group.second, [&](::std::ostream& os, const HIR::MarkerImpl& i) {
            os << "impl" << i.m_params.fmt_args() << " " << impl_group.first << i.m_trait_args << " for " << i.m_type << " {}";
        });
    }

    // Create indexes
    push_index_impls(crate, crate);
    for (const auto& ec : crate.m_ext_crates) {
        push_index_impls(crate, *ec.second.m_data);
    }

    {
        const auto& lang_Box = crate.get_lang_item_path_opt("owned_box");
        push_index_inherent_methods(crate.m_inherent_method_cache, lang_Box, crate);
        for (const auto& ec : crate.m_ext_crates) {
            push_index_inherent_methods(crate.m_inherent_method_cache, lang_Box, *ec.second.m_data);
        }
    }
}
