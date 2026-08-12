#include "hir_visitor.h"
#include "hir_hir.h"
#include "hir_typeck_static.h"

::HIR::Visitor::~Visitor() {
}

namespace {
    template <typename T>
    void visitImpls(::HIR::Crate::ImplGroup<std::unique_ptr<T>>& g, ::std::function<void(T&)> cb) {
        for (auto& implGroup : g.named) {
            for (auto& impl : implGroup.second) {
                cb(*impl);
            }
        }
        for (auto& impl : g.nonNamed) {
            cb(*impl);
        }
        for (auto& impl : g.generic) {
            cb(*impl);
        }
    }
}

void ::HIR::Visitor::visitCrate(::HIR::Crate& crate) {
    this->visitModule(::HIR::ItemPath(crate.crateName), crate.rootModule);

    visitImpls<::HIR::TypeImpl>(crate.typeImpls, [&](::HIR::TypeImpl& tyImpl) {
        this->visitTypeImpl(tyImpl);
    });
    for (auto& implGroup : crate.traitImpls) {
        visitImpls<::HIR::TraitImpl>(implGroup.second, [&](::HIR::TraitImpl& tyImpl) {
            this->visitTraitImpl(implGroup.first, tyImpl);
        });
    }
    for (auto& implGroup : crate.markerImpls) {
        visitImpls<::HIR::MarkerImpl>(implGroup.second, [&](::HIR::MarkerImpl& tyImpl) {
            this->visitMarkerImpl(implGroup.first, tyImpl);
        });
    }
}

void ::HIR::Visitor::visitModule(::HIR::ItemPath p, ::HIR::Module& mod) {
    TRACE_FUNCTION_FR(p, p);
    for (auto& named : mod.modItems) {
        const auto& name = named.first;
        auto& item = named.second->ent;
        TU_MATCH_HDRA( (item), {)
        TU_ARMA(Import, e) {
            }
            TU_ARMA(Module, e) {
                TRACE_FUNCTION_F("mod " << name);
                this->visitModule(p + name, e);
            }
            TU_ARMA(TypeAlias, e) {
                TRACE_FUNCTION_F("type " << name);
                this->visitTypeAlias(p + name, e);
            }
            TU_ARMA(TraitAlias, e) {
                TRACE_FUNCTION_F("trait (alias) " << name);
                this->visitTraitAlias(p + name, e);
            }
            TU_ARMA(ExternType, e) {
                TRACE_FUNCTION_F("extern type " << name);
            }
            TU_ARMA(Enum, e) {
                TRACE_FUNCTION_F("enum " << name);
                this->visitEnum(p + name, e);
            }
            TU_ARMA(Struct, e) {
                TRACE_FUNCTION_F("struct " << name);
                this->visitStruct(p + name, e);
            }
            TU_ARMA(Union, e) {
                TRACE_FUNCTION_F("union " << name);
                this->visitUnion(p + name, e);
            }
            TU_ARMA(Trait, e) {
                TRACE_FUNCTION_F("trait " << name);
                this->visitTrait(p + name, e);
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
                this->visitConstant(p + name, e);
            }
            TU_ARMA(Static, e) {
                DEBUG("static " << name);
                this->visitStatic(p + name, e);
            }
            TU_ARMA(StructConstant, e) {
                // Just a path
            }
            TU_ARMA(Function, e) {
                DEBUG("fn " << name);
                this->visitFunction(p + name, e);
            }
            TU_ARMA(StructConstructor, e) {
                // Just a path
            }
        }
    }
}

void ::HIR::Visitor::visitTypeImpl(::HIR::TypeImpl& impl) {
    ::HIR::ItemPath p{impl.mType};
    TRACE_FUNCTION_F("impl.m_type=" << impl.mType);
    if (mResolve) {
        mResolve->setImplGenericsRaw(MetadataType::Unknown, impl.mParams);
    }
    this->visitParams(impl.mParams);
    this->visitType(impl.mType);

    for (auto& method : impl.methods) {
        DEBUG("method " << method.first);
        this->visitFunction(p + method.first, method.second.data);
    }
    for (auto& ent : impl.constants) {
        DEBUG("const " << ent.first);
        this->visitConstant(p + ent.first, ent.second.data);
    }
    for (auto& ent : impl.types) {
        DEBUG("type " << ent.first);
        this->visitInherentType(p + ent.first, ent.second.data);
    }
    if (mResolve) {
        mResolve->clearImplGenerics();
    }
}

void ::HIR::Visitor::visitInherentType(ItemPath p, ::HIR::TypeAlias& item) {
    TRACE_FUNCTION_F(p);
    if (mResolve) {
        mResolve->setItemGenericsRaw(item.mParams);
    }
    this->visitParams(item.mParams);
    this->visitType(item.mType);
    if (mResolve) {
        mResolve->clearItemGenerics();
    }
}

void ::HIR::Visitor::visitTraitImpl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) {
    ::HIR::ItemPath p(impl.mType, trait_path, impl.traitArgs);
    TRACE_FUNCTION_F("impl" << impl.mParams.fmtArgs() << " " << trait_path << impl.traitArgs << " for " << impl.mType);
    if (mResolve) {
        mResolve->setImplGenericsRaw(MetadataType::Unknown, impl.mParams);
    }
    this->visitParams(impl.mParams);
    // Visit trait arguments through GenericPath so path-context checks and rewrites are shared.
    {
        ::HIR::GenericPath gp{trait_path, mv$(impl.traitArgs)};
        this->visitGenericPath(gp, PathContext::TRAIT);
        impl.traitArgs = mv$(gp.mParams);
    }
    this->visitType(impl.mType);

    for (auto& ent : impl.methods) {
        DEBUG("method " << ent.first);
        this->visitFunction(p + ent.first, ent.second.data);
    }
    for (auto& ent : impl.constants) {
        DEBUG("const " << ent.first);
        this->visitConstant(p + ent.first, ent.second.data);
    }
    for (auto& ent : impl.statics) {
        DEBUG("static " << ent.first);
        this->visitStatic(p + ent.first, ent.second.data);
    }
    for (auto& ent : impl.types) {
        TRACE_FUNCTION_F("type " << ent.first << " = " << ent.second.data);
        this->visitType(ent.second.data);
    }
    if (mResolve) {
        mResolve->clearImplGenerics();
    }
}

void ::HIR::Visitor::visitMarkerImpl(const ::HIR::SimplePath& trait_path, ::HIR::MarkerImpl& impl) {
    if (mResolve) {
        mResolve->setImplGenericsRaw(MetadataType::Unknown, impl.mParams);
    }
    this->visitParams(impl.mParams);
    this->visitPathParams(impl.traitArgs);
    this->visitType(impl.mType);
    if (mResolve) {
        mResolve->clearImplGenerics();
    }
}

void ::HIR::Visitor::visitTypeAlias(::HIR::ItemPath p, ::HIR::TypeAlias& item) {
    if (mResolve) {
        mResolve->setImplGenericsRaw(MetadataType::Unknown, item.mParams);
    }
    this->visitParams(item.mParams);
    this->visitType(item.mType);
    if (mResolve) {
        mResolve->clearImplGenerics();
    }
}

void ::HIR::Visitor::visitTraitAlias(::HIR::ItemPath p, ::HIR::TraitAlias& item) {
    if (mResolve) {
        mResolve->setImplGenericsRaw(MetadataType::Unknown, item.mParams);
    }
    this->visitParams(item.mParams);
    for (auto& p : item.traits) {
        this->visitTraitPath(p);
    }
    if (mResolve) {
        mResolve->clearImplGenerics();
    }
}

void ::HIR::Visitor::visitTrait(::HIR::ItemPath p, ::HIR::Trait& item) {
    if (mResolve) {
        mResolve->setImplGenericsRaw(MetadataType::Unknown, item.mParams);
    }
    auto traitSp = p.getSimplePath();
    auto traitPp = item.mParams.makeNopParams(type_interner(), 0);
    const HIR::TypeRef tySelf = type_interner().self();
    ItemPath traitIp(tySelf, traitSp, traitPp);
    TRACE_FUNCTION;

    this->visitParams(item.mParams);
    for (auto& par : item.parentTraits) {
        this->visitTraitPath(par);
    }
    for (auto& par : item.allParentTraits) {
        this->visitTraitPath(par);
    }
    for (auto& i : item.types) {
        auto itemPath = ::HIR::ItemPath(traitIp, i.first.c_str());
        DEBUG("type " << i.first);
        this->visitAssociatedtype(itemPath, i.second);
    }
    for (auto& i : item.values) {
        auto itemPath = ::HIR::ItemPath(traitIp, i.first.c_str());
        TU_MATCH(
            ::HIR::TraitValueItem,
            (i.second),
            (e),
            //(None, ),
            (Constant, DEBUG("constant " << i.first); this->visitConstant(itemPath, e);),
            (Static, DEBUG("static " << i.first); this->visitStatic(itemPath, e);),
            (Function, DEBUG("method " << i.first); this->visitFunction(itemPath, e);)
        )
    }
    if (mResolve) {
        mResolve->clearImplGenerics();
    }
}

void ::HIR::Visitor::visitStruct(::HIR::ItemPath p, ::HIR::Struct& item) {
    if (mResolve) {
        mResolve->setImplGenericsRaw(MetadataType::Unknown, item.mParams);
    }
    this->visitParams(item.mParams);
    TU_MATCH_HDRA( (item.mData), {)
    TU_ARMA(Unit, e) {
        }
        TU_ARMA(Tuple, e) {
            for (auto& ty : e) {
                this->visitType(ty.ent);
            }
        }
        TU_ARMA(Named, e) {
            for (auto& field : e) {
                this->visitType(field.ty);
                if (field.default_value) {
                    this->visitGenericPath(*field.default_value, PathContext::VALUE);
                }
            }
        }
    }
    if( mResolve ) {
        mResolve->clearImplGenerics();
    }
}

void ::HIR::Visitor::visitEnum(::HIR::ItemPath p, ::HIR::Enum& item) {
    if (mResolve) {
        mResolve->setImplGenericsRaw(MetadataType::None, item.mParams);
    }
    this->visitParams(item.mParams);
    TU_MATCH_HDRA( (item.mData), {)
    TU_ARMA(Value, e) {
            for (auto& var : e.variants) {
                this->visitExpr(var.expr);
            }
        }
        TU_ARMA(Data, e) {
            for (auto& var : e) {
                this->visitType(var.type);
                this->visitExpr(var.discriminantExpr);
            }
        }
    }
    if( mResolve ) {
        mResolve->clearImplGenerics();
    }
}

void ::HIR::Visitor::visitUnion(::HIR::ItemPath p, ::HIR::Union& item) {
    TRACE_FUNCTION_F(p);
    if (mResolve) {
        mResolve->setImplGenericsRaw(MetadataType::Unknown, item.mParams);
    }
    this->visitParams(item.mParams);
    for (auto& var : item.mVariants) {
        this->visitType(var.ty);
        assert(!var.default_value);
    }
    if (mResolve) {
        mResolve->clearImplGenerics();
    }
}

void ::HIR::Visitor::visitAssociatedtype(ItemPath p, ::HIR::AssociatedType& item) {
    TRACE_FUNCTION_F(p);
    for (auto& bound : item.traitBounds) {
        this->visitTraitPath(bound);
    }
    this->visitType(item.defaultValue);
}

void ::HIR::Visitor::visitFunction(::HIR::ItemPath p, ::HIR::Function& item) {
    TRACE_FUNCTION_F(p);
    if (mResolve) {
        mResolve->setItemGenericsRaw(item.mParams);
    }
    this->visitParams(item.mParams);
    for (auto& arg : item.mArgs) {
        this->visitPattern(arg.first);
        this->visitType(arg.second);
    }
    this->visitType(item.returnType);
    this->visitExpr(item.mCode);
    if (mResolve) {
        mResolve->clearItemGenerics();
    }
}

void ::HIR::Visitor::visitStatic(::HIR::ItemPath p, ::HIR::Static& item) {
    TRACE_FUNCTION_F(p);
    if (mResolve) {
        mResolve->setItemGenericsRaw(item.mParams);
    }
    this->visitType(item.mType);
    this->visitExpr(item.mValue);
    if (mResolve) {
        mResolve->clearItemGenerics();
    }
}

void ::HIR::Visitor::visitConstant(::HIR::ItemPath p, ::HIR::Constant& item) {
    TRACE_FUNCTION_F(p);
    if (mResolve) {
        mResolve->setItemGenericsRaw(item.mParams);
    }
    this->visitParams(item.mParams);
    this->visitType(item.mType);
    this->visitExpr(item.mValue);
    if (mResolve) {
        mResolve->clearItemGenerics();
    }
}

void ::HIR::Visitor::visitParams(::HIR::GenericParams& params) {
    TRACE_FUNCTION_F(params.fmtArgs() << params.fmtBounds());
    for (auto& tps : params.types) {
        this->visitType(tps.defaultValue);
    }
    for (auto& val : params.values) {
        this->visitType(val.mType);
        this->visitConstgeneric(val.defaultValue);
    }
    for (auto& bound : params.bounds) {
        visitGenericBound(bound);
    }
}

void ::HIR::Visitor::visitGenericBound(::HIR::GenericBound& bound) {
    TU_MATCH_HDRA((bound), {)
    TU_ARMA(Lifetime, e) {
        }
        TU_ARMA(TypeLifetime, e) {
            this->visitType(e.type);
        }
        TU_ARMA(TraitBound, e) {
            this->visitType(e.type);
            this->visitTraitPath(e.trait);
        }
        //TU_ARMA(NotTrait, e) {
        //    this->visit_type(e.type);
        //    this->visit_trait_path(e.trait);
        //    }
        TU_ARMA(TypeEquality, e) {
            this->visitType(e.type);
            this->visitType(e.otherType);
        }
    }
}

void ::HIR::Visitor::visitType(::HIR::TypeRef& ty) {
    assert(ty);
    auto data = ty->cloneData();
    visitTypeData(data);
    ty = type_interner().intern(mv$(data));
}

void ::HIR::Visitor::visitTypeData(::HIR::TypeData& data) {
    TU_MATCH_HDRA( (data), {)
    TU_ARMA(Infer, e) {
        }
        TU_ARMA(Diverge, e) {
        }
        TU_ARMA(Primitive, e) {
        }
        TU_ARMA(Path, e) {
            this->visitPath(e.path, ::HIR::Visitor::PathContext::TYPE);
        }
        TU_ARMA(Generic, e) {
        }
        TU_ARMA(TraitObject, e) {
            if (e.mTrait.mPath != ::HIR::SimplePath()) {
                this->visitTraitPath(e.mTrait);
            }
            for (auto& trait : e.markers) {
                this->visitGenericPath(trait, ::HIR::Visitor::PathContext::TYPE);
            }
        }
        TU_ARMA(ErasedType, e) {
        TU_MATCH_HDRA( (e.inner), {)
        TU_ARMA(Known, ee) {
                    this->visitType(ee);
                }
                TU_ARMA(Alias, ee) {
                    this->visitPathParams(ee.params);
                }
                TU_ARMA(Fcn, ee) {
                    if (ee.origin != ::HIR::SimplePath()) {
                        this->visitPath(ee.origin, ::HIR::Visitor::PathContext::VALUE);
                    }
                }
        }
        this->visitPathParams(e.use);
        for(auto& trait : e.traits) {
                this->visitTraitPath(trait);
        }
        }
        TU_ARMA(Array, e) {
            this->visitType(e.inner);
            if (auto* se = e.size.opt_Unevaluated()) {
                this->visitConstgeneric(*se);
            }
        }
        TU_ARMA(Slice, e) {
            this->visitType(e.inner);
        }
        TU_ARMA(Tuple, e) {
            for (auto& t : e) {
                this->visitType(t);
            }
        }
        TU_ARMA(Borrow, e) {
            this->visitType(e.inner);
        }
        TU_ARMA(Pointer, e) {
            this->visitType(e.inner);
        }
        TU_ARMA(NamedFunction, e) {
            this->visitPath(e.path, ::HIR::Visitor::PathContext::VALUE);
        }
        TU_ARMA(Function, e) {
            for (auto& t : e.argTypes) {
                this->visitType(t);
            }
            this->visitType(e.mRettype);
        }
        TU_ARMA(NodeType, e) {
        }
    }
}

void ::HIR::Visitor::visitConstgeneric(::HIR::ConstGeneric& v) {
    if (v.is_Unevaluated()) {
        this->visitExpr(*v.as_Unevaluated()->expr);
    }
}

void ::HIR::Visitor::visitPattern(::HIR::Pattern& pat) {
    TU_MATCH_HDRA( (pat.mData), {)
    TU_ARMA(Any, e) {
        }
        TU_ARMA(Box, e) {
            this->visitPattern(*e.sub);
        }
        TU_ARMA(Ref, e) {
            this->visitPattern(*e.sub);
        }
        TU_ARMA(Tuple, e) {
            for (auto& subpat : e.subPatterns) {
                this->visitPattern(subpat);
            }
        }
        TU_ARMA(SplitTuple, e) {
            for (auto& subpat : e.leading) {
                this->visitPattern(subpat);
            }
            for (auto& subpat : e.trailing) {
                this->visitPattern(subpat);
            }
        }
        TU_ARMA(PathValue, e) {
            this->visitPath(e.path, ::HIR::Visitor::PathContext::VALUE);
        }
        TU_ARMA(PathTuple, e) {
            this->visitPath(e.path, ::HIR::Visitor::PathContext::VALUE);
            for (auto& subpat : e.leading) {
                this->visitPattern(subpat);
            }
            for (auto& subpat : e.trailing) {
                this->visitPattern(subpat);
            }
        }
        TU_ARMA(PathNamed, e) {
            this->visitPath(e.path, ::HIR::Visitor::PathContext::TYPE);
            for (auto& sp : e.subPatterns) {
                this->visitPattern(sp.second);
            }
        }
        TU_ARMA(Value, e) {
            this->visitPatternVal(e.val);
        }
        TU_ARMA(Range, e) {
            if (e.start) {
                this->visitPatternVal(*e.start);
            }
            if (e.end) {
                this->visitPatternVal(*e.end);
            }
        }
        TU_ARMA(Slice, e) {
            for (auto& sp : e.subPatterns) {
                this->visitPattern(sp);
            }
        }
        TU_ARMA(SplitSlice, e) {
            for (auto& sp : e.leading) {
                this->visitPattern(sp);
            }
            for (auto& sp : e.trailing) {
                this->visitPattern(sp);
            }
        }
        TU_ARMA(Or, e) {
            for (auto& sp : e) {
                this->visitPattern(sp);
            }
        }
    }
}

void ::HIR::Visitor::visitPatternVal(::HIR::Pattern::Value& val) {
    TU_MATCH(::HIR::Pattern::Value, (val), (e), (Integer, ), (Float, ), (String, ), (ByteString, ), (Named, this->visitPath(e.path, ::HIR::Visitor::PathContext::VALUE);))
}

void ::HIR::Visitor::visitTraitPath(::HIR::TraitPath& p) {
    this->visitGenericPath(p.mPath, ::HIR::Visitor::PathContext::TYPE);
    for (auto& assoc : p.typeBounds) {
        this->visitGenericPath(assoc.second.sourceTrait, ::HIR::Visitor::PathContext::TYPE);
        this->visitType(assoc.second.type);
    }
    for (auto& assoc : p.traitBounds) {
        this->visitGenericPath(assoc.second.sourceTrait, ::HIR::Visitor::PathContext::TYPE);
        for (auto& trait : assoc.second.traits) {
            this->visitTraitPath(trait);
        }
    }
}

void ::HIR::Visitor::visitPath(::HIR::Path& p, ::HIR::Visitor::PathContext pc) {
    TU_MATCH_HDRA( (p.mData), {)
    TU_ARMA(Generic, e) {
            this->visitGenericPath(e, pc);
        }
        TU_ARMA(UfcsInherent, e) {
            this->visitType(e.type);
            this->visitPathParams(e.params);
            this->visitPathParams(e.impl_params);
        }
        TU_ARMA(UfcsKnown, e) {
            this->visitType(e.type);
            this->visitGenericPath(e.trait, ::HIR::Visitor::PathContext::TYPE);
            this->visitPathParams(e.params);
        }
        TU_ARMA(UfcsUnknown, e) {
            this->visitType(e.type);
            this->visitPathParams(e.params);
        }
    }
}

void ::HIR::Visitor::visitPathParams(::HIR::PathParams& p) {
    for (auto& ty : p.types) {
        this->visitType(ty);
    }
    for (auto& v : p.values) {
        visitConstgeneric(v);
    }
}

void ::HIR::Visitor::visitGenericPath(::HIR::GenericPath& p, ::HIR::Visitor::PathContext /*pc*/) {
    this->visitPathParams(p.mParams);
}

void ::HIR::Visitor::visitExpr(::HIR::ExprPtr& exp) {
    // Do nothing, leave expression stuff for user
    for (auto& t : exp.erasedTypes) {
        visitType(t);
    }
    for (auto& t : exp.mBindings) {
        visitType(t);
    }
}

namespace HIR {

Visitor::Visitor(::StaticTraitResolve* resolve, TypeInterner& types)
    : mResolve(resolve)
    , types(types) {
}
}
