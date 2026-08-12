#include "hir_visitor.h"
#include "hir_hir.h"
#include "hir_typeck_static.h"

::HIR::Visitor::~Visitor() {
}

namespace {
    template <typename T>
    void visit_impls(::HIR::Crate::ImplGroup<std::unique_ptr<T>>& g, ::std::function<void(T&)> cb) {
        for (auto& impl_group : g.named) {
            for (auto& impl : impl_group.second) {
                cb(*impl);
            }
        }
        for (auto& impl : g.non_named) {
            cb(*impl);
        }
        for (auto& impl : g.generic) {
            cb(*impl);
        }
    }
}

void ::HIR::Visitor::visit_crate(::HIR::Crate& crate) {
    this->visit_module(::HIR::ItemPath(crate.crateName), crate.rootModule);

    visit_impls<::HIR::TypeImpl>(crate.typeImpls, [&](::HIR::TypeImpl& ty_impl) {
        this->visit_type_impl(ty_impl);
    });
    for (auto& impl_group : crate.traitImpls) {
        visit_impls<::HIR::TraitImpl>(impl_group.second, [&](::HIR::TraitImpl& ty_impl) {
            this->visit_trait_impl(impl_group.first, ty_impl);
        });
    }
    for (auto& impl_group : crate.markerImpls) {
        visit_impls<::HIR::MarkerImpl>(impl_group.second, [&](::HIR::MarkerImpl& ty_impl) {
            this->visit_marker_impl(impl_group.first, ty_impl);
        });
    }
}

void ::HIR::Visitor::visit_module(::HIR::ItemPath p, ::HIR::Module& mod) {
    TRACE_FUNCTION_FR(p, p);
    for (auto& named : mod.modItems) {
        const auto& name = named.first;
        auto& item = named.second->ent;
        TU_MATCH_HDRA( (item), {)
        TU_ARMA(Import, e) {
            }
            TU_ARMA(Module, e) {
                TRACE_FUNCTION_F("mod " << name);
                this->visit_module(p + name, e);
            }
            TU_ARMA(TypeAlias, e) {
                TRACE_FUNCTION_F("type " << name);
                this->visit_type_alias(p + name, e);
            }
            TU_ARMA(TraitAlias, e) {
                TRACE_FUNCTION_F("trait (alias) " << name);
                this->visit_trait_alias(p + name, e);
            }
            TU_ARMA(ExternType, e) {
                TRACE_FUNCTION_F("extern type " << name);
            }
            TU_ARMA(Enum, e) {
                TRACE_FUNCTION_F("enum " << name);
                this->visit_enum(p + name, e);
            }
            TU_ARMA(Struct, e) {
                TRACE_FUNCTION_F("struct " << name);
                this->visit_struct(p + name, e);
            }
            TU_ARMA(Union, e) {
                TRACE_FUNCTION_F("union " << name);
                this->visit_union(p + name, e);
            }
            TU_ARMA(Trait, e) {
                TRACE_FUNCTION_F("trait " << name);
                this->visit_trait(p + name, e);
            }
        }
    }
    for (auto& named : mod.valueItems) {
        const auto& name = named.first;
        auto& item = named.second->ent;
        TU_MATCH_HDRA( (item), {)
        TU_ARMA(Import, e) {
                // SimplePath - no visitor
            }
            TU_ARMA(Constant, e) {
                DEBUG("const " << name);
                this->visit_constant(p + name, e);
            }
            TU_ARMA(Static, e) {
                DEBUG("static " << name);
                this->visit_static(p + name, e);
            }
            TU_ARMA(StructConstant, e) {
                // Just a path
            }
            TU_ARMA(Function, e) {
                DEBUG("fn " << name);
                this->visit_function(p + name, e);
            }
            TU_ARMA(StructConstructor, e) {
                // Just a path
            }
        }
    }
}

void ::HIR::Visitor::visit_type_impl(::HIR::TypeImpl& impl) {
    ::HIR::ItemPath p{impl.mType};
    TRACE_FUNCTION_F("impl.m_type=" << impl.mType);
    if (mResolve) {
        mResolve->set_impl_generics_raw(MetadataType::Unknown, impl.mParams);
    }
    this->visit_params(impl.mParams);
    this->visit_type(impl.mType);

    for (auto& method : impl.methods) {
        DEBUG("method " << method.first);
        this->visit_function(p + method.first, method.second.data);
    }
    for (auto& ent : impl.constants) {
        DEBUG("const " << ent.first);
        this->visit_constant(p + ent.first, ent.second.data);
    }
    for (auto& ent : impl.types) {
        DEBUG("type " << ent.first);
        this->visit_inherent_type(p + ent.first, ent.second.data);
    }
    if (mResolve) {
        mResolve->clear_impl_generics();
    }
}

void ::HIR::Visitor::visit_inherent_type(ItemPath p, ::HIR::TypeAlias& item) {
    TRACE_FUNCTION_F(p);
    if (mResolve) {
        mResolve->set_item_generics_raw(item.mParams);
    }
    this->visit_params(item.mParams);
    this->visit_type(item.mType);
    if (mResolve) {
        mResolve->clear_item_generics();
    }
}

void ::HIR::Visitor::visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) {
    ::HIR::ItemPath p(impl.mType, trait_path, impl.traitArgs);
    TRACE_FUNCTION_F("impl" << impl.mParams.fmt_args() << " " << trait_path << impl.traitArgs << " for " << impl.mType);
    if (mResolve) {
        mResolve->set_impl_generics_raw(MetadataType::Unknown, impl.mParams);
    }
    this->visit_params(impl.mParams);
    // Visit trait arguments through GenericPath so path-context checks and rewrites are shared.
    {
        ::HIR::GenericPath gp{trait_path, mv$(impl.traitArgs)};
        this->visit_generic_path(gp, PathContext::TRAIT);
        impl.traitArgs = mv$(gp.mParams);
    }
    this->visit_type(impl.mType);

    for (auto& ent : impl.methods) {
        DEBUG("method " << ent.first);
        this->visit_function(p + ent.first, ent.second.data);
    }
    for (auto& ent : impl.constants) {
        DEBUG("const " << ent.first);
        this->visit_constant(p + ent.first, ent.second.data);
    }
    for (auto& ent : impl.statics) {
        DEBUG("static " << ent.first);
        this->visit_static(p + ent.first, ent.second.data);
    }
    for (auto& ent : impl.types) {
        TRACE_FUNCTION_F("type " << ent.first << " = " << ent.second.data);
        this->visit_type(ent.second.data);
    }
    if (mResolve) {
        mResolve->clear_impl_generics();
    }
}

void ::HIR::Visitor::visit_marker_impl(const ::HIR::SimplePath& trait_path, ::HIR::MarkerImpl& impl) {
    if (mResolve) {
        mResolve->set_impl_generics_raw(MetadataType::Unknown, impl.mParams);
    }
    this->visit_params(impl.mParams);
    this->visit_path_params(impl.traitArgs);
    this->visit_type(impl.mType);
    if (mResolve) {
        mResolve->clear_impl_generics();
    }
}

void ::HIR::Visitor::visit_type_alias(::HIR::ItemPath p, ::HIR::TypeAlias& item) {
    if (mResolve) {
        mResolve->set_impl_generics_raw(MetadataType::Unknown, item.mParams);
    }
    this->visit_params(item.mParams);
    this->visit_type(item.mType);
    if (mResolve) {
        mResolve->clear_impl_generics();
    }
}

void ::HIR::Visitor::visit_trait_alias(::HIR::ItemPath p, ::HIR::TraitAlias& item) {
    if (mResolve) {
        mResolve->set_impl_generics_raw(MetadataType::Unknown, item.mParams);
    }
    this->visit_params(item.mParams);
    for (auto& p : item.traits) {
        this->visit_trait_path(p);
    }
    if (mResolve) {
        mResolve->clear_impl_generics();
    }
}

void ::HIR::Visitor::visit_trait(::HIR::ItemPath p, ::HIR::Trait& item) {
    if (mResolve) {
        mResolve->set_impl_generics_raw(MetadataType::Unknown, item.mParams);
    }
    auto trait_sp = p.get_simple_path();
    auto trait_pp = item.mParams.make_nop_params(type_interner(), 0);
    const HIR::TypeRef tySelf = type_interner().self();
    ItemPath trait_ip(tySelf, trait_sp, trait_pp);
    TRACE_FUNCTION;

    this->visit_params(item.mParams);
    for (auto& par : item.parentTraits) {
        this->visit_trait_path(par);
    }
    for (auto& par : item.allParentTraits) {
        this->visit_trait_path(par);
    }
    for (auto& i : item.types) {
        auto item_path = ::HIR::ItemPath(trait_ip, i.first.c_str());
        DEBUG("type " << i.first);
        this->visit_associatedtype(item_path, i.second);
    }
    for (auto& i : item.values) {
        auto item_path = ::HIR::ItemPath(trait_ip, i.first.c_str());
        TU_MATCH(
            ::HIR::TraitValueItem,
            (i.second),
            (e),
            //(None, ),
            (Constant, DEBUG("constant " << i.first); this->visit_constant(item_path, e);),
            (Static, DEBUG("static " << i.first); this->visit_static(item_path, e);),
            (Function, DEBUG("method " << i.first); this->visit_function(item_path, e);)
        )
    }
    if (mResolve) {
        mResolve->clear_impl_generics();
    }
}

void ::HIR::Visitor::visit_struct(::HIR::ItemPath p, ::HIR::Struct& item) {
    if (mResolve) {
        mResolve->set_impl_generics_raw(MetadataType::Unknown, item.mParams);
    }
    this->visit_params(item.mParams);
    TU_MATCH_HDRA( (item.mData), {)
    TU_ARMA(Unit, e) {
        }
        TU_ARMA(Tuple, e) {
            for (auto& ty : e) {
                this->visit_type(ty.ent);
            }
        }
        TU_ARMA(Named, e) {
            for (auto& field : e) {
                this->visit_type(field.ty);
                if (field.default_value) {
                    this->visit_generic_path(*field.default_value, PathContext::VALUE);
                }
            }
        }
    }
    if( mResolve ) {
        mResolve->clear_impl_generics();
    }
}

void ::HIR::Visitor::visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) {
    if (mResolve) {
        mResolve->set_impl_generics_raw(MetadataType::None, item.mParams);
    }
    this->visit_params(item.mParams);
    TU_MATCH_HDRA( (item.mData), {)
    TU_ARMA(Value, e) {
            for (auto& var : e.variants) {
                this->visit_expr(var.expr);
            }
        }
        TU_ARMA(Data, e) {
            for (auto& var : e) {
                this->visit_type(var.type);
                this->visit_expr(var.discriminant_expr);
            }
        }
    }
    if( mResolve ) {
        mResolve->clear_impl_generics();
    }
}

void ::HIR::Visitor::visit_union(::HIR::ItemPath p, ::HIR::Union& item) {
    TRACE_FUNCTION_F(p);
    if (mResolve) {
        mResolve->set_impl_generics_raw(MetadataType::Unknown, item.mParams);
    }
    this->visit_params(item.mParams);
    for (auto& var : item.mVariants) {
        this->visit_type(var.ty);
        assert(!var.default_value);
    }
    if (mResolve) {
        mResolve->clear_impl_generics();
    }
}

void ::HIR::Visitor::visit_associatedtype(ItemPath p, ::HIR::AssociatedType& item) {
    TRACE_FUNCTION_F(p);
    for (auto& bound : item.traitBounds) {
        this->visit_trait_path(bound);
    }
    this->visit_type(item.defaultValue);
}

void ::HIR::Visitor::visit_function(::HIR::ItemPath p, ::HIR::Function& item) {
    TRACE_FUNCTION_F(p);
    if (mResolve) {
        mResolve->set_item_generics_raw(item.mParams);
    }
    this->visit_params(item.mParams);
    for (auto& arg : item.mArgs) {
        this->visit_pattern(arg.first);
        this->visit_type(arg.second);
    }
    this->visit_type(item.returnType);
    this->visit_expr(item.mCode);
    if (mResolve) {
        mResolve->clear_item_generics();
    }
}

void ::HIR::Visitor::visit_static(::HIR::ItemPath p, ::HIR::Static& item) {
    TRACE_FUNCTION_F(p);
    if (mResolve) {
        mResolve->set_item_generics_raw(item.mParams);
    }
    this->visit_type(item.mType);
    this->visit_expr(item.mValue);
    if (mResolve) {
        mResolve->clear_item_generics();
    }
}

void ::HIR::Visitor::visit_constant(::HIR::ItemPath p, ::HIR::Constant& item) {
    TRACE_FUNCTION_F(p);
    if (mResolve) {
        mResolve->set_item_generics_raw(item.mParams);
    }
    this->visit_params(item.mParams);
    this->visit_type(item.mType);
    this->visit_expr(item.mValue);
    if (mResolve) {
        mResolve->clear_item_generics();
    }
}

void ::HIR::Visitor::visit_params(::HIR::GenericParams& params) {
    TRACE_FUNCTION_F(params.fmt_args() << params.fmt_bounds());
    for (auto& tps : params.types) {
        this->visit_type(tps.defaultValue);
    }
    for (auto& val : params.values) {
        this->visit_type(val.mType);
        this->visit_constgeneric(val.defaultValue);
    }
    for (auto& bound : params.bounds) {
        visit_generic_bound(bound);
    }
}

void ::HIR::Visitor::visit_generic_bound(::HIR::GenericBound& bound) {
    TU_MATCH_HDRA((bound), {)
    TU_ARMA(Lifetime, e) {
        }
        TU_ARMA(TypeLifetime, e) {
            this->visit_type(e.type);
        }
        TU_ARMA(TraitBound, e) {
            this->visit_type(e.type);
            this->visit_trait_path(e.trait);
        }
        //TU_ARMA(NotTrait, e) {
        //    this->visit_type(e.type);
        //    this->visit_trait_path(e.trait);
        //    }
        TU_ARMA(TypeEquality, e) {
            this->visit_type(e.type);
            this->visit_type(e.other_type);
        }
    }
}

void ::HIR::Visitor::visit_type(::HIR::TypeRef& ty) {
    assert(ty);
    auto data = ty->clone_data();
    visit_type_data(data);
    ty = type_interner().intern(mv$(data));
}

void ::HIR::Visitor::visit_type_data(::HIR::TypeData& data) {
    TU_MATCH_HDRA( (data), {)
    TU_ARMA(Infer, e) {
        }
        TU_ARMA(Diverge, e) {
        }
        TU_ARMA(Primitive, e) {
        }
        TU_ARMA(Path, e) {
            this->visit_path(e.path, ::HIR::Visitor::PathContext::TYPE);
        }
        TU_ARMA(Generic, e) {
        }
        TU_ARMA(TraitObject, e) {
            if (e.mTrait.mPath != ::HIR::SimplePath()) {
                this->visit_trait_path(e.mTrait);
            }
            for (auto& trait : e.markers) {
                this->visit_generic_path(trait, ::HIR::Visitor::PathContext::TYPE);
            }
        }
        TU_ARMA(ErasedType, e) {
        TU_MATCH_HDRA( (e.inner), {)
        TU_ARMA(Known, ee) {
                    this->visit_type(ee);
                }
                TU_ARMA(Alias, ee) {
                    this->visit_path_params(ee.params);
                }
                TU_ARMA(Fcn, ee) {
                    if (ee.origin != ::HIR::SimplePath()) {
                        this->visit_path(ee.origin, ::HIR::Visitor::PathContext::VALUE);
                    }
                }
        }
        this->visit_path_params(e.use);
        for(auto& trait : e.traits) {
                this->visit_trait_path(trait);
        }
        }
        TU_ARMA(Array, e) {
            this->visit_type(e.inner);
            if (auto* se = e.size.opt_Unevaluated()) {
                this->visit_constgeneric(*se);
            }
        }
        TU_ARMA(Slice, e) {
            this->visit_type(e.inner);
        }
        TU_ARMA(Tuple, e) {
            for (auto& t : e) {
                this->visit_type(t);
            }
        }
        TU_ARMA(Borrow, e) {
            this->visit_type(e.inner);
        }
        TU_ARMA(Pointer, e) {
            this->visit_type(e.inner);
        }
        TU_ARMA(NamedFunction, e) {
            this->visit_path(e.path, ::HIR::Visitor::PathContext::VALUE);
        }
        TU_ARMA(Function, e) {
            for (auto& t : e.argTypes) {
                this->visit_type(t);
            }
            this->visit_type(e.mRettype);
        }
        TU_ARMA(NodeType, e) {
        }
    }
}

void ::HIR::Visitor::visit_constgeneric(::HIR::ConstGeneric& v) {
    if (v.is_Unevaluated()) {
        this->visit_expr(*v.as_Unevaluated()->expr);
    }
}

void ::HIR::Visitor::visit_pattern(::HIR::Pattern& pat) {
    TU_MATCH_HDRA( (pat.mData), {)
    TU_ARMA(Any, e) {
        }
        TU_ARMA(Box, e) {
            this->visit_pattern(*e.sub);
        }
        TU_ARMA(Ref, e) {
            this->visit_pattern(*e.sub);
        }
        TU_ARMA(Tuple, e) {
            for (auto& subpat : e.sub_patterns) {
                this->visit_pattern(subpat);
            }
        }
        TU_ARMA(SplitTuple, e) {
            for (auto& subpat : e.leading) {
                this->visit_pattern(subpat);
            }
            for (auto& subpat : e.trailing) {
                this->visit_pattern(subpat);
            }
        }
        TU_ARMA(PathValue, e) {
            this->visit_path(e.path, ::HIR::Visitor::PathContext::VALUE);
        }
        TU_ARMA(PathTuple, e) {
            this->visit_path(e.path, ::HIR::Visitor::PathContext::VALUE);
            for (auto& subpat : e.leading) {
                this->visit_pattern(subpat);
            }
            for (auto& subpat : e.trailing) {
                this->visit_pattern(subpat);
            }
        }
        TU_ARMA(PathNamed, e) {
            this->visit_path(e.path, ::HIR::Visitor::PathContext::TYPE);
            for (auto& sp : e.sub_patterns) {
                this->visit_pattern(sp.second);
            }
        }
        TU_ARMA(Value, e) {
            this->visit_pattern_val(e.val);
        }
        TU_ARMA(Range, e) {
            if (e.start) {
                this->visit_pattern_val(*e.start);
            }
            if (e.end) {
                this->visit_pattern_val(*e.end);
            }
        }
        TU_ARMA(Slice, e) {
            for (auto& sp : e.sub_patterns) {
                this->visit_pattern(sp);
            }
        }
        TU_ARMA(SplitSlice, e) {
            for (auto& sp : e.leading) {
                this->visit_pattern(sp);
            }
            for (auto& sp : e.trailing) {
                this->visit_pattern(sp);
            }
        }
        TU_ARMA(Or, e) {
            for (auto& sp : e) {
                this->visit_pattern(sp);
            }
        }
    }
}

void ::HIR::Visitor::visit_pattern_val(::HIR::Pattern::Value& val) {
    TU_MATCH(::HIR::Pattern::Value, (val), (e), (Integer, ), (Float, ), (String, ), (ByteString, ), (Named, this->visit_path(e.path, ::HIR::Visitor::PathContext::VALUE);))
}

void ::HIR::Visitor::visit_trait_path(::HIR::TraitPath& p) {
    this->visit_generic_path(p.mPath, ::HIR::Visitor::PathContext::TYPE);
    for (auto& assoc : p.typeBounds) {
        this->visit_generic_path(assoc.second.source_trait, ::HIR::Visitor::PathContext::TYPE);
        this->visit_type(assoc.second.type);
    }
    for (auto& assoc : p.traitBounds) {
        this->visit_generic_path(assoc.second.source_trait, ::HIR::Visitor::PathContext::TYPE);
        for (auto& trait : assoc.second.traits) {
            this->visit_trait_path(trait);
        }
    }
}

void ::HIR::Visitor::visit_path(::HIR::Path& p, ::HIR::Visitor::PathContext pc) {
    TU_MATCH_HDRA( (p.mData), {)
    TU_ARMA(Generic, e) {
            this->visit_generic_path(e, pc);
        }
        TU_ARMA(UfcsInherent, e) {
            this->visit_type(e.type);
            this->visit_path_params(e.params);
            this->visit_path_params(e.impl_params);
        }
        TU_ARMA(UfcsKnown, e) {
            this->visit_type(e.type);
            this->visit_generic_path(e.trait, ::HIR::Visitor::PathContext::TYPE);
            this->visit_path_params(e.params);
        }
        TU_ARMA(UfcsUnknown, e) {
            this->visit_type(e.type);
            this->visit_path_params(e.params);
        }
    }
}

void ::HIR::Visitor::visit_path_params(::HIR::PathParams& p) {
    for (auto& ty : p.types) {
        this->visit_type(ty);
    }
    for (auto& v : p.values) {
        visit_constgeneric(v);
    }
}

void ::HIR::Visitor::visit_generic_path(::HIR::GenericPath& p, ::HIR::Visitor::PathContext /*pc*/) {
    this->visit_path_params(p.mParams);
}

void ::HIR::Visitor::visit_expr(::HIR::ExprPtr& exp) {
    // Do nothing, leave expression stuff for user
    for (auto& t : exp.erasedTypes) {
        visit_type(t);
    }
    for (auto& t : exp.mBindings) {
        visit_type(t);
    }
}

namespace HIR {

Visitor::Visitor(::StaticTraitResolve* resolve, TypeInterner& types)
    : mResolve(resolve)
    , types(types) {
}
}
