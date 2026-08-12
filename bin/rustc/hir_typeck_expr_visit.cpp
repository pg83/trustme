#include "hir_typeck_expr_visit.h"
#include "hir_hir.h"
#include "hir_expr.h"
#include "hir_visitor.h"
#include "hir_expr_state.h"

void TypecheckCode(const typeck::ModuleState& ms, t_args& args, const ::HIR::TypeData* result_type, ::HIR::ExprPtr& expr) {
    if (expr.state->stage < ::HIR::ExprState::Stage::Typecheck) {
        //Typecheck_Code_Simple(ms, args, result_type, expr);
        TypecheckCodeCS(ms, args, result_type, expr);
        expr.state->stage = ::HIR::ExprState::Stage::Typecheck;
    }
}

namespace typeck {
    void ModuleState::prepare_from_path(const ::HIR::ItemPath& ip) {
        static Span sp;
        ASSERT_BUG(sp, ip.parent, "prepare_from_path with too-short path - " << ip);

        struct H {
            static const ::HIR::Module& getModForIp(const ::HIR::Crate& crate, const ::HIR::ItemPath& ip) {
                if (ip.parent) {
                    const auto& mod = H::getModForIp(crate, *ip.parent);
                    return mod.modItems.at(ip.name)->ent.as_Module();
                } else {
                    assert(ip.crate_name);
                    return (ip.crate_name[0] ? crate.extCrates.at(ip.crate_name).mData->rootModule : crate.rootModule);
                }
            }

            static void addTraitsFromMod(ModuleState& ms, const ::HIR::Module& mod) {
                // In-scope traits.
                ms.traits.clear();
                for (const auto& tp : mod.traits) {
                    const auto& trait = ms.crate.getTraitByPath(sp, tp);
                    ms.traits.push_back(::std::make_pair(&tp, &trait));
                }
            }
        };

        if (ip.parent->trait && ip.parent->ty) {
            // Trait impl
            TODO(sp, "prepare_from_path - Trait impl " << ip);
        } else if (ip.parent->trait) {
            // Trait definition
            //const auto& trait_mod = H::get_mod_for_ip(m_crate, *ip.parent->trait->parent);
            //const auto& trait = trait_mod.m_mod_items.at(ip.parent->trait->name).ent.as_Trait();
            const auto& trait = crate.getTraitByPath(sp, *ip.parent->trait);
            const auto& item = trait.values.at(ip.name);
            TU_MATCH_HDRA( (item), { )
            TU_ARMA(Function, e) {
                    itemGenerics = &e.mParams;
                }
                TU_ARMA(Constant, e) {
                    itemGenerics = &e.mParams;
                }
                TU_ARMA(Static, e) {
                    itemGenerics = nullptr;
                }
            }
        } else if (ip.parent->ty) {
            // Inherent impl
            TODO(sp, "prepare_from_path - Type impl " << ip);
        } else {
            // Namespace path
            const auto& mod = H::getModForIp(crate, *ip.parent);
            H::addTraitsFromMod(*this, mod);
            const auto& item = mod.valueItems.at(ip.name)->ent;
            implGenerics = nullptr;
            TU_MATCH_HDRA( (item), { )
            TU_ARMA(Constant, e) {
                    itemGenerics = &e.mParams;
                }
                TU_ARMA(Static, e) {
                    //m_item_generics = &e.m_params;
                }
                TU_ARMA(Function, e) {
                    itemGenerics = &e.mParams;
                }
                TU_ARMA(StructConstant, _e) BUG(sp, ip << " is StructConstant");
                TU_ARMA(StructConstructor, _e) BUG(sp, ip << " is StructConstructor");
                TU_ARMA(Import, _e) BUG(sp, ip << " is Import");
            }
        }
    }
} // namespace typeck

namespace {

    class OuterVisitor: public ::HIR::Visitor {
        ::typeck::ModuleState ms;

    public:
        OuterVisitor(::HIR::Crate& crate)
            : ::HIR::Visitor(nullptr, crate.types)
            , ms(crate)
        {
        }

    public:
        void visit_module(::HIR::ItemPath p, ::HIR::Module& mod) override {
            ms.push_traits(p, mod);
            ::HIR::Visitor::visit_module(p, mod);
            ms.pop_traits(mod);
        }

        // NOTE: This is left here to ensure that any expressions that aren't handled by higher code cause a failure
        void visit_expr(::HIR::ExprPtr& exp) override {
            if (exp.mir) {
                return;
            }
            BUG(exp->mSpan, "Reached expression");
        }

        void visit_trait(::HIR::ItemPath p, ::HIR::Trait& item) override {
            HIR::GenericPath trait_gpath;
            trait_gpath.mPath = p.getSimplePath();
            for (size_t i = 0; i < item.mParams.types.size(); i++) {
                trait_gpath.mParams.types.push_back(ms.crate.types.generic(item.mParams.types[i].mName, i));
            }
            for (size_t i = 0; i < item.mParams.values.size(); i++) {
                trait_gpath.mParams.values.push_back(HIR::GenericRef(item.mParams.values[i].mName, i));
            }
            auto _1 = this->ms.set_current_trait(trait_gpath);
            auto _ = this->ms.set_impl_generics(item.mParams);
            ::HIR::Visitor::visit_trait(p, item);
        }

        void visit_type_impl(::HIR::TypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.mType);
            auto _ = this->ms.set_impl_generics(impl.mParams);

            const auto& mod = this->ms.crate.getModByPath(Span(), impl.srcModule);
            ms.push_traits(impl.srcModule, mod);
            ::HIR::Visitor::visit_type_impl(impl);
            ms.pop_traits(mod);
        }

        void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
            TRACE_FUNCTION_F("impl " << trait_path << impl.traitArgs << " for " << impl.mType);
            auto trait_gpath = ::HIR::GenericPath(trait_path, impl.traitArgs.clone());
            auto _0 = this->ms.set_current_trait_impl(impl);
            auto _1 = this->ms.set_current_trait(trait_gpath);
            auto _ = this->ms.set_impl_generics(impl.mParams);

            const auto& mod = this->ms.crate.getModByPath(Span(), impl.srcModule);
            ms.push_traits(impl.srcModule, mod);
            ms.traits.push_back(::std::make_pair(&trait_path, &this->ms.crate.getTraitByPath(Span(), trait_path)));
            ::HIR::Visitor::visit_trait_impl(trait_path, impl);
            ms.traits.pop_back();
            ms.pop_traits(mod);
        }

        void visit_marker_impl(const ::HIR::SimplePath& trait_path, ::HIR::MarkerImpl& impl) override {
            TRACE_FUNCTION_F("impl " << trait_path << " for " << impl.mType << " { }");
            auto _ = this->ms.set_impl_generics(impl.mParams);

            const auto& mod = this->ms.crate.getModByPath(Span(), impl.srcModule);
            ms.push_traits(impl.srcModule, mod);
            ::HIR::Visitor::visit_marker_impl(trait_path, impl);
            ms.pop_traits(mod);
        }

        void visit_type(::HIR::TypeRef& ty) override {
            if (ty->is_Array()) {
                auto data = ty->cloneData();
                auto& e = data.as_Array();
                this->visit_type(e.inner);
                DEBUG("Array size " << ty);
                t_args tmp;
                if (auto* se = e.size.opt_Unevaluated()) {
                    if (se->is_Unevaluated()) {
                        TypecheckCode(ms, tmp, ms.crate.types.primitive(::HIR::CoreType::Usize), *se->as_Unevaluated()->expr);
                    }
                }
                ty = ms.crate.types.intern(std::move(data));
            } else {
                ::HIR::Visitor::visit_type(ty);
            }
        }

        // ------
        // Code-containing items
        // ------
        void visit_function(::HIR::ItemPath p, ::HIR::Function& item) override {
            auto _ = this->ms.set_item_generics(item.mParams);
            if (item.mCode) {
                DEBUG("Function code " << p);
                TypecheckCode(ms, item.mArgs, item.returnType, item.mCode);
            } else {
                DEBUG("Function code " << p << " (none)");
            }
        }

        void visit_static(::HIR::ItemPath p, ::HIR::Static& item) override {
            //auto _ = this->m_ms.set_item_generics(item.m_params);
            if (item.mValue) {
                DEBUG("Static value " << p);
                t_args tmp;
                TypecheckCode(ms, tmp, item.mType, item.mValue);
            }
        }

        void visit_constant(::HIR::ItemPath p, ::HIR::Constant& item) override {
            auto _ = this->ms.set_item_generics(item.mParams);
            if (item.mValue) {
                DEBUG("Const value " << p);
                t_args tmp;
                TypecheckCode(ms, tmp, item.mType, item.mValue);
            }
        }

        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override {
            auto _ = this->ms.set_item_generics(item.mParams);

            if (auto* e = item.mData.opt_Value()) {
                auto enumType = ::HIR::Enum::getReprType(item.tagRepr);

                for (auto& var : e->variants) {
                    DEBUG("Enum value " << p << " - " << var.name);
                    if (var.expr) {
                        t_args tmp;
                        TypecheckCode(ms, tmp, ms.crate.types.primitive(enumType), var.expr);
                    }
                }
            }
        }
    };
}

void TypecheckExpressions(::HIR::Crate& crate) {
    OuterVisitor visitor{crate};
    visitor.visit_crate(crate);
}

namespace typeck {

ModuleState::ModuleState(const ::HIR::Crate& crate)
    : crate(crate)
    , currentTrait(nullptr)
    , currentTraitImpl(nullptr)
    , implGenerics(nullptr)
    , itemGenerics(nullptr) {
}
ModuleState::NullOnDrop<const ::HIR::GenericPath> ModuleState::set_current_trait(const ::HIR::GenericPath& p) {
    assert(!currentTrait);
    currentTrait = &p;
    return NullOnDrop<const ::HIR::GenericPath>(currentTrait);
}
ModuleState::NullOnDrop<const ::HIR::TraitImpl> ModuleState::set_current_trait_impl(const ::HIR::TraitImpl& impl) {
    assert(!currentTraitImpl);
    currentTraitImpl = &impl;
    return NullOnDrop<const ::HIR::TraitImpl>(currentTraitImpl);
}
ModuleState::NullOnDrop<const ::HIR::GenericParams> ModuleState::set_impl_generics(const ::HIR::GenericParams& gps) {
    assert(!implGenerics);
    implGenerics = &gps;
    return NullOnDrop<const ::HIR::GenericParams>(implGenerics);
}
ModuleState::NullOnDrop<const ::HIR::GenericParams> ModuleState::set_item_generics(const ::HIR::GenericParams& gps) {
    assert(!itemGenerics);
    itemGenerics = &gps;
    return NullOnDrop<const ::HIR::GenericParams>(itemGenerics);
}
void ModuleState::push_traits(::HIR::ItemPath p, const ::HIR::Module& mod) {
    auto sp = Span();
    modPaths.push_back(p.getSimplePath());
    DEBUG("Module has " << mod.traits.size() << " in-scope traits");
    // - Push a NULL entry to prevent parent module import lists being searched
    traits.push_back(::std::make_pair(nullptr, nullptr));
    for (const auto& trait_path : mod.traits) {
        DEBUG("Push " << trait_path);
        traits.push_back(::std::make_pair(&trait_path, &this->crate.getTraitByPath(sp, trait_path)));
    }
}
void ModuleState::pop_traits(const ::HIR::Module& mod) {
    DEBUG("Module has " << mod.traits.size() << " in-scope traits");
    for (unsigned int i = 0; i < mod.traits.size(); i++) {
        traits.pop_back();
    }
    traits.pop_back();
    modPaths.pop_back();
}
}
