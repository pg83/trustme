#include "hir_visitor.h"

#include "hir_hir.h"
#include "hir_typeck_static.h"

HIRVisitor::~HIRVisitor() {
}

namespace {
    template <typename T>
    void visitImpls(HIRCrate::ImplGroup<std::unique_ptr<T>>& g, ::std::function<void(T&)> cb) {
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

void HIRVisitor::visitCrate(HIRCrate& crate) {
    this->visitModule(HIRItemPath(crate.crateName), crate.mRootModule);

    visitImpls<HIRTypeImpl>(crate.typeImpls, [&](HIRTypeImpl& tyImpl) {
        this->visitTypeImpl(tyImpl);
    });
    for (auto& implGroup : crate.traitImpls) {
        visitImpls<HIRTraitImpl>(implGroup.second, [&](HIRTraitImpl& tyImpl) {
            this->visitTraitImpl(implGroup.first, tyImpl);
        });
    }
    for (auto& implGroup : crate.markerImpls) {
        visitImpls<HIRMarkerImpl>(implGroup.second, [&](HIRMarkerImpl& tyImpl) {
            this->visitMarkerImpl(implGroup.first, tyImpl);
        });
    }
}

void HIRVisitor::visitModule(HIRItemPath p, HIRModule& mod) {
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
    for (auto& item : mod.globalAsm) {
        this->visitGlobalAssembly(item);
    }
}

void HIRVisitor::visitGlobalAssembly(HIRGlobalAssembly& item) {
    for (auto& operand : item.operands) {
        TU_MATCH_HDRA((operand), {)
        TU_ARMA(Const, value) {
                this->visitConstgeneric(value.value);
            }
            TU_ARMA(Sym, path) {
                this->visitPath(path, PathContext::VALUE);
            }
        }
    }
}

void HIRVisitor::visitTypeImpl(HIRTypeImpl& impl) {
    HIRItemPath p{impl.mType};
    TRACE_FUNCTION_F("impl.m_type=" << impl.mType);
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::Unknown, impl.mParams);
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
    if (resolve_) {
        resolve_->clearImplGenerics();
    }
}

void HIRVisitor::visitInherentType(HIRItemPath p, HIRTypeAlias& item) {
    TRACE_FUNCTION_F(p);
    if (resolve_) {
        resolve_->setItemGenericsRaw(item.mParams);
    }
    this->visitParams(item.mParams);
    this->visitType(item.mType);
    if (resolve_) {
        resolve_->clearItemGenerics();
    }
}

void HIRVisitor::visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) {
    HIRItemPath p(impl.mType, traitPath, impl.traitArgs);
    TRACE_FUNCTION_F("impl" << impl.mParams.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << impl.mType);
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::Unknown, impl.mParams);
    }
    this->visitParams(impl.mParams);
    // Visit trait arguments through GenericPath so path-context checks and rewrites are shared.
    {
        HIRGenericPath gp{traitPath, mv$(impl.traitArgs)};
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
    if (resolve_) {
        resolve_->clearImplGenerics();
    }
}

void HIRVisitor::visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) {
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::Unknown, impl.mParams);
    }
    this->visitParams(impl.mParams);
    this->visitPathParams(impl.traitArgs);
    this->visitType(impl.mType);
    if (resolve_) {
        resolve_->clearImplGenerics();
    }
}

void HIRVisitor::visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) {
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::Unknown, item.mParams);
    }
    this->visitParams(item.mParams);
    this->visitType(item.mType);
    if (resolve_) {
        resolve_->clearImplGenerics();
    }
}

void HIRVisitor::visitTraitAlias(HIRItemPath p, HIRTraitAlias& item) {
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::Unknown, item.mParams);
    }
    this->visitParams(item.mParams);
    for (auto& p : item.traits) {
        this->visitTraitPath(p);
    }
    if (resolve_) {
        resolve_->clearImplGenerics();
    }
}

void HIRVisitor::visitTrait(HIRItemPath p, HIRTrait& item) {
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::Unknown, item.mParams);
    }
    auto traitSp = p.getSimplePath();
    auto traitPp = item.mParams.makeNopParams(typeInterner(), 0);
    const HIRTypeRef tySelf = typeInterner().self();
    HIRItemPath traitIp(tySelf, traitSp, traitPp);
    TRACE_FUNCTION;

    this->visitParams(item.mParams);
    for (auto& par : item.parentTraits) {
        this->visitTraitPath(par);
    }
    for (auto& par : item.allParentTraits) {
        this->visitTraitPath(par);
    }
    for (auto& i : item.types) {
        auto itemPath = HIRItemPath(traitIp, i.first.c_str());
        DEBUG("type " << i.first);
        this->visitAssociatedtype(itemPath, i.second);
    }
    for (auto& i : item.values) {
        auto itemPath = HIRItemPath(traitIp, i.first.c_str());
        TU_MATCH(
            HIRTraitValueItem,
            (i.second),
            (e),
            //(None, ),
            (Constant, DEBUG("constant " << i.first); this->visitConstant(itemPath, e);),
            (Static, DEBUG("static " << i.first); this->visitStatic(itemPath, e);),
            (Function, DEBUG("method " << i.first); this->visitFunction(itemPath, e);)
        )
    }
    if (resolve_) {
        resolve_->clearImplGenerics();
    }
}

void HIRVisitor::visitStruct(HIRItemPath p, HIRStruct& item) {
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::Unknown, item.mParams);
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
                if (field.defaultValue) {
                    this->visitGenericPath(*field.defaultValue, PathContext::VALUE);
                }
            }
        }
    }
    if( resolve_ ) {
        resolve_->clearImplGenerics();
    }
}

void HIRVisitor::visitEnum(HIRItemPath p, HIREnum& item) {
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::None, item.mParams);
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
    if( resolve_ ) {
        resolve_->clearImplGenerics();
    }
}

void HIRVisitor::visitUnion(HIRItemPath p, HIRUnion& item) {
    TRACE_FUNCTION_F(p);
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::Unknown, item.mParams);
    }
    this->visitParams(item.mParams);
    for (auto& var : item.mVariants) {
        this->visitType(var.ty);
        assert(!var.defaultValue);
    }
    if (resolve_) {
        resolve_->clearImplGenerics();
    }
}

void HIRVisitor::visitAssociatedtype(HIRItemPath p, HIRAssociatedType& item) {
    TRACE_FUNCTION_F(p);
    for (auto& bound : item.traitBounds) {
        this->visitTraitPath(bound);
    }
    this->visitType(item.defaultValue);
}

void HIRVisitor::visitFunction(HIRItemPath p, HIRFunction& item) {
    TRACE_FUNCTION_F(p);
    if (resolve_) {
        resolve_->setItemGenericsRaw(item.mParams);
    }
    this->visitParams(item.mParams);
    for (auto& arg : item.mArgs) {
        this->visitPattern(arg.first);
        this->visitType(arg.second);
    }
    this->visitType(item.returnType);
    if (item.traitReturnType) {
        this->visitType(*item.traitReturnType);
    }
    this->visitExpr(item.mCode);
    if (resolve_) {
        resolve_->clearItemGenerics();
    }
}

void HIRVisitor::visitStatic(HIRItemPath p, HIRStatic& item) {
    TRACE_FUNCTION_F(p);
    if (resolve_) {
        resolve_->setItemGenericsRaw(item.mParams);
    }
    this->visitType(item.mType);
    this->visitExpr(item.mValue);
    if (resolve_) {
        resolve_->clearItemGenerics();
    }
}

void HIRVisitor::visitConstant(HIRItemPath p, HIRConstant& item) {
    TRACE_FUNCTION_F(p);
    if (resolve_) {
        resolve_->setItemGenericsRaw(item.mParams);
    }
    this->visitParams(item.mParams);
    this->visitType(item.mType);
    this->visitExpr(item.mValue);
    if (resolve_) {
        resolve_->clearItemGenerics();
    }
}

void HIRVisitor::visitParams(HIRGenericParams& params) {
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

void HIRVisitor::visitGenericBound(HIRGenericBound& bound) {
    TU_MATCH_HDRA((bound), {)
    TU_ARMA(TraitBound, e) {
            this->visitType(e.type);
            this->visitTraitPath(e.trait);
        }
        //    }
        TU_ARMA(TypeEquality, e) {
            this->visitType(e.type);
            this->visitType(e.otherType);
        }
    }
}

void HIRVisitor::visitType(HIRTypeRef& ty) {
    assert(ty);
    auto data = ty->cloneData();
    visitTypeData(data);
    ty = typeInterner().intern(mv$(data));
}

void HIRVisitor::visitTypeData(HIRTypeData& data) {
    TU_MATCH_HDRA( (data), {)
    TU_ARMA(Infer, e) {
        }
        TU_ARMA(Diverge, e) {
        }
        TU_ARMA(Primitive, e) {
        }
        TU_ARMA(Path, e) {
            this->visitPath(e.path, HIRVisitor::PathContext::TYPE);
        }
        TU_ARMA(Generic, e) {
        }
        TU_ARMA(TraitObject, e) {
            if (e.mTrait.mPath != HIRSimplePath()) {
                this->visitTraitPath(e.mTrait);
            }
            for (auto& trait : e.markers) {
                this->visitGenericPath(trait, HIRVisitor::PathContext::TYPE);
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
                    if (ee.origin != HIRSimplePath()) {
                        this->visitPath(ee.origin, HIRVisitor::PathContext::VALUE);
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
        TU_ARMA(Pattern, e) {
            this->visitType(e.inner);
            for (auto& range : e.pattern.alternatives) {
                if (range.hasStart) this->visitConstgeneric(range.start);
                if (range.hasEnd) this->visitConstgeneric(range.end);
            }
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
            this->visitPath(e.path, HIRVisitor::PathContext::VALUE);
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

void HIRVisitor::visitConstgeneric(HIRConstGeneric& v) {
    if (auto* unevaluated = v.opt_Unevaluated()) {
        if ((*unevaluated)->selfType) {
            this->visitType((*unevaluated)->selfType);
        }
        this->visitPathParams((*unevaluated)->paramsImpl);
        this->visitPathParams((*unevaluated)->paramsItem);
        this->visitExpr(*(*unevaluated)->expr);
    }
}

void HIRVisitor::visitPattern(HIRPattern& pat) {
    TU_MATCH_HDRA( (pat.mData), {)
    TU_ARMA(Any, e) {
        }
        TU_ARMA(Box, e) {
            this->visitPattern(*e.sub);
        }
        TU_ARMA(Deref, e) {
            if (e.targetType) this->visitType(e.targetType);
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
            this->visitPath(e.path, HIRVisitor::PathContext::VALUE);
        }
        TU_ARMA(PathTuple, e) {
            this->visitPath(e.path, HIRVisitor::PathContext::VALUE);
            for (auto& subpat : e.leading) {
                this->visitPattern(subpat);
            }
            for (auto& subpat : e.trailing) {
                this->visitPattern(subpat);
            }
        }
        TU_ARMA(PathNamed, e) {
            this->visitPath(e.path, HIRVisitor::PathContext::TYPE);
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

void HIRVisitor::visitPatternVal(HIRPattern::Value& val) {
    TU_MATCH(HIRPattern::Value, (val), (e), (Integer, ), (Float, ), (String, ), (ByteString, ), (Named, this->visitPath(e.path, HIRVisitor::PathContext::VALUE);))
}

void HIRVisitor::visitTraitPath(HIRTraitPath& p) {
    this->visitGenericPath(p.mPath, HIRVisitor::PathContext::TYPE);
    for (auto& assoc : p.typeBounds) {
        this->visitGenericPath(assoc.second.sourceTrait, HIRVisitor::PathContext::TYPE);
        this->visitType(assoc.second.type);
    }
    for (auto& assoc : p.traitBounds) {
        this->visitGenericPath(assoc.second.sourceTrait, HIRVisitor::PathContext::TYPE);
        for (auto& trait : assoc.second.traits) {
            this->visitTraitPath(trait);
        }
    }
}

void HIRVisitor::visitPath(HIRPath& p, HIRVisitor::PathContext pc) {
    TU_MATCH_HDRA( (p.mData), {)
    TU_ARMA(Generic, e) {
            this->visitGenericPath(e, pc);
        }
        TU_ARMA(UfcsInherent, e) {
            this->visitType(e.type);
            this->visitPathParams(e.params);
            this->visitPathParams(e.implParams);
        }
        TU_ARMA(UfcsKnown, e) {
            this->visitType(e.type);
            this->visitGenericPath(e.trait, HIRVisitor::PathContext::TYPE);
            this->visitPathParams(e.params);
        }
        TU_ARMA(UfcsUnknown, e) {
            this->visitType(e.type);
            this->visitPathParams(e.params);
        }
    }
}

void HIRVisitor::visitPathParams(HIRPathParams& p) {
    for (auto& ty : p.types) {
        this->visitType(ty);
    }
    for (auto& v : p.values) {
        visitConstgeneric(v);
    }
}

void HIRVisitor::visitGenericPath(HIRGenericPath& p, HIRVisitor::PathContext /*pc*/) {
    this->visitPathParams(p.mParams);
}

void HIRVisitor::visitExpr(HIRExprPtr& exp) {
    // Do nothing, leave expression stuff for user
    for (auto& t : exp.erasedTypes) {
        visitType(t);
    }
    for (auto& t : exp.mBindings) {
        visitType(t);
    }
}

HIRVisitor::HIRVisitor(::StaticTraitResolve* resolve, HIRTypeInterner& types)
    : resolve_(resolve)
    , types(types)
{
}
