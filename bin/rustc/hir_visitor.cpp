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
    this->visitModule(HIRItemPath(crate.crateName), crate.rootModule);

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
        switch (item.tag()) {
            case HIRTypeItem::TAG_Import: {
                break;
            }
            case HIRTypeItem::TAG_Module: {
                auto& e = item.as_Module();
                TRACE_FUNCTION_F("mod " << name);
                this->visitModule(p + name, e);
                break;
            }
            case HIRTypeItem::TAG_TypeAlias: {
                auto& e = item.as_TypeAlias();
                TRACE_FUNCTION_F("type " << name);
                this->visitTypeAlias(p + name, e);
                break;
            }
            case HIRTypeItem::TAG_TraitAlias: {
                auto& e = item.as_TraitAlias();
                TRACE_FUNCTION_F("trait (alias) " << name);
                this->visitTraitAlias(p + name, e);
                break;
            }
            case HIRTypeItem::TAG_ExternType: {
                TRACE_FUNCTION_F("extern type " << name);
                break;
            }
            case HIRTypeItem::TAG_Enum: {
                auto& e = item.as_Enum();
                TRACE_FUNCTION_F("enum " << name);
                this->visitEnum(p + name, e);
                break;
            }
            case HIRTypeItem::TAG_Struct: {
                auto& e = item.as_Struct();
                TRACE_FUNCTION_F("struct " << name);
                this->visitStruct(p + name, e);
                break;
            }
            case HIRTypeItem::TAG_Union: {
                auto& e = item.as_Union();
                TRACE_FUNCTION_F("union " << name);
                this->visitUnion(p + name, e);
                break;
            }
            case HIRTypeItem::TAG_Trait: {
                auto& e = item.as_Trait();
                TRACE_FUNCTION_F("trait " << name);
                this->visitTrait(p + name, e);
                break;
            }
        }
    }
    for (auto& named : mod.valueItems) {
        const auto& name = named.first;
        auto& item = named.second->ent;
        switch (item.tag()) {
            case HIRValueItem::TAG_Import: {
                // SimplePath - no visitor
                break;
            }
            case HIRValueItem::TAG_Constant: {
                auto& e = item.as_Constant();
                DEBUG("const " << name);
                this->visitConstant(p + name, e);
                break;
            }
            case HIRValueItem::TAG_Static: {
                auto& e = item.as_Static();
                DEBUG("static " << name);
                this->visitStatic(p + name, e);
                break;
            }
            case HIRValueItem::TAG_StructConstant: {
                // Just a path
                break;
            }
            case HIRValueItem::TAG_Function: {
                auto& e = item.as_Function();
                DEBUG("fn " << name);
                this->visitFunction(p + name, e);
                break;
            }
            case HIRValueItem::TAG_StructConstructor: {
                // Just a path
                break;
            }
        }
    }
    for (auto& item : mod.globalAsm) {
        this->visitGlobalAssembly(item);
    }
}

void HIRVisitor::visitGlobalAssembly(HIRGlobalAssembly& item) {
    for (auto& operand : item.operands) {
        switch (operand.tag()) {
            case HIRGlobalAsmOperand::TAG_Const: {
                auto& value = operand.as_Const();
                this->visitConstgeneric(value.value);
                break;
            }
            case HIRGlobalAsmOperand::TAG_Sym: {
                auto& path = operand.as_Sym();
                this->visitPath(path, PathContext::VALUE);
                break;
            }
        }
    }
}

void HIRVisitor::visitTypeImpl(HIRTypeImpl& impl) {
    HIRItemPath p{impl.type};
    TRACE_FUNCTION_F("impl.m_type=" << impl.type);
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::Unknown, impl.params);
    }
    this->visitParams(impl.params);
    this->visitType(impl.type);

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
        resolve_->setItemGenericsRaw(item.params);
    }
    this->visitParams(item.params);
    this->visitType(item.type);
    if (resolve_) {
        resolve_->clearItemGenerics();
    }
}

void HIRVisitor::visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) {
    HIRItemPath p(impl.type, traitPath, impl.traitArgs);
    TRACE_FUNCTION_F("impl" << impl.params.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << impl.type);
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::Unknown, impl.params);
    }
    this->visitParams(impl.params);
    // Visit trait arguments through GenericPath so path-context checks and rewrites are shared.
    {
        HIRGenericPath gp{traitPath, mv$(impl.traitArgs)};
        this->visitGenericPath(gp, PathContext::TRAIT);
        impl.traitArgs = mv$(gp.params);
    }
    this->visitType(impl.type);

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
        resolve_->setImplGenericsRaw(MetadataType::Unknown, impl.params);
    }
    this->visitParams(impl.params);
    this->visitPathParams(impl.traitArgs);
    this->visitType(impl.type);
    if (resolve_) {
        resolve_->clearImplGenerics();
    }
}

void HIRVisitor::visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) {
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::Unknown, item.params);
    }
    this->visitParams(item.params);
    this->visitType(item.type);
    if (resolve_) {
        resolve_->clearImplGenerics();
    }
}

void HIRVisitor::visitTraitAlias(HIRItemPath p, HIRTraitAlias& item) {
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::Unknown, item.params);
    }
    this->visitParams(item.params);
    for (auto& p : item.traits) {
        this->visitTraitPath(p);
    }
    if (resolve_) {
        resolve_->clearImplGenerics();
    }
}

void HIRVisitor::visitTrait(HIRItemPath p, HIRTrait& item) {
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::Unknown, item.params);
    }
    auto traitSp = p.getSimplePath();
    auto traitPp = item.params.makeNopParams(typeInterner(), 0);
    const HIRTypeRef tySelf = typeInterner().self();
    HIRItemPath traitIp(tySelf, traitSp, traitPp);
    TRACE_FUNCTION;

    this->visitParams(item.params);
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
        switch (i.second.tag()) {
            //(None, ),
            case HIRTraitValueItem::TAG_Constant: {
                auto& e = i.second.as_Constant();
                DEBUG("constant " << i.first); this->visitConstant(itemPath, e);
                break;
            }
            case HIRTraitValueItem::TAG_Static: {
                auto& e = i.second.as_Static();
                DEBUG("static " << i.first); this->visitStatic(itemPath, e);
                break;
            }
            case HIRTraitValueItem::TAG_Function: {
                auto& e = i.second.as_Function();
                DEBUG("method " << i.first); this->visitFunction(itemPath, e);
                break;
            }
        }
    }
    if (resolve_) {
        resolve_->clearImplGenerics();
    }
}

void HIRVisitor::visitStruct(HIRItemPath p, HIRStruct& item) {
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::Unknown, item.params);
    }
    this->visitParams(item.params);
    switch (item.data.tag()) {
        case HIRStructData::TAG_Unit: {
            break;
        }
        case HIRStructData::TAG_Tuple: {
            auto& e = item.data.as_Tuple();
            for (auto& ty : e) {
                this->visitType(ty.ent);
            }
            break;
        }
        case HIRStructData::TAG_Named: {
            auto& e = item.data.as_Named();
            for (auto& field : e) {
                this->visitType(field.ty);
                if (field.defaultValue) {
                    this->visitGenericPath(*field.defaultValue, PathContext::VALUE);
                }
            }
            break;
        }
    }
    if( resolve_ ) {
        resolve_->clearImplGenerics();
    }
}

void HIRVisitor::visitEnum(HIRItemPath p, HIREnum& item) {
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::None, item.params);
    }
    this->visitParams(item.params);
    switch (item.data.tag()) {
        case HIREnumClass::TAG_Value: {
            auto& e = item.data.as_Value();
            for (auto& var : e.variants) {
                this->visitExpr(var.expr);
            }
            break;
        }
        case HIREnumClass::TAG_Data: {
            auto& e = item.data.as_Data();
            for (auto& var : e) {
                this->visitType(var.type);
                this->visitExpr(var.discriminantExpr);
            }
            break;
        }
    }
    if( resolve_ ) {
        resolve_->clearImplGenerics();
    }
}

void HIRVisitor::visitUnion(HIRItemPath p, HIRUnion& item) {
    TRACE_FUNCTION_F(p);
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::Unknown, item.params);
    }
    this->visitParams(item.params);
    for (auto& var : item.variants) {
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
        resolve_->setItemGenericsRaw(item.params);
    }
    this->visitParams(item.params);
    for (auto& arg : item.args) {
        this->visitPattern(arg.first);
        this->visitType(arg.second);
    }
    this->visitType(item.returnType);
    if (item.traitReturnType) {
        this->visitType(*item.traitReturnType);
    }
    this->visitExpr(item.code);
    if (resolve_) {
        resolve_->clearItemGenerics();
    }
}

void HIRVisitor::visitStatic(HIRItemPath p, HIRStatic& item) {
    TRACE_FUNCTION_F(p);
    if (resolve_) {
        resolve_->setItemGenericsRaw(item.params);
    }
    this->visitType(item.type);
    this->visitExpr(item.value);
    if (resolve_) {
        resolve_->clearItemGenerics();
    }
}

void HIRVisitor::visitConstant(HIRItemPath p, HIRConstant& item) {
    TRACE_FUNCTION_F(p);
    if (resolve_) {
        resolve_->setItemGenericsRaw(item.params);
    }
    this->visitParams(item.params);
    this->visitType(item.type);
    this->visitExpr(item.value);
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
        this->visitType(val.type);
        this->visitConstgeneric(val.defaultValue);
    }
    for (auto& bound : params.bounds) {
        visitGenericBound(bound);
    }
}

void HIRVisitor::visitGenericBound(HIRGenericBound& bound) {
    switch (bound.tag()) {
        case HIRGenericBound::TAG_TraitBound: {
            auto& e = bound.as_TraitBound();
            this->visitType(e.type);
            this->visitTraitPath(e.trait);
            break;
        }
        case HIRGenericBound::TAG_TypeEquality: {
            auto& e = bound.as_TypeEquality();
            this->visitType(e.type);
            this->visitType(e.otherType);
            break;
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
    switch (data.tag()) {
        case HIRTypeData::TAG_Infer: {
            break;
        }
        case HIRTypeData::TAG_Diverge: {
            break;
        }
        case HIRTypeData::TAG_Primitive: {
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = data.as_Path();
            this->visitPath(e.path, HIRVisitor::PathContext::TYPE);
            break;
        }
        case HIRTypeData::TAG_Generic: {
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& e = data.as_TraitObject();
            if (e.trait.path != HIRSimplePath()) {
                this->visitTraitPath(e.trait);
            }
            for (auto& trait : e.markers) {
                this->visitGenericPath(trait, HIRVisitor::PathContext::TYPE);
            }
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            auto& e = data.as_ErasedType();
            switch (e.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Known: {
                    auto& ee = e.inner.as_Known();
                    this->visitType(ee);
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Alias: {
                    auto& ee = e.inner.as_Alias();
                    this->visitPathParams(ee.params);
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    auto& ee = e.inner.as_Fcn();
                    if (ee.origin != HIRSimplePath()) {
                        this->visitPath(ee.origin, HIRVisitor::PathContext::VALUE);
                    }
                    break;
                }
            }
            this->visitPathParams(e.use);
            for(auto& trait : e.traits) {
                    this->visitTraitPath(trait);
            }
            break;
        }
        case HIRTypeData::TAG_Array: {
            auto& e = data.as_Array();
            this->visitType(e.inner);
            if (auto* se = e.size.opt_Unevaluated()) {
                this->visitConstgeneric(*se);
            }
            break;
        }
        case HIRTypeData::TAG_Slice: {
            auto& e = data.as_Slice();
            this->visitType(e.inner);
            break;
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = data.as_Pattern();
            this->visitType(e.inner);
            for (auto& range : e.pattern.alternatives) {
                if (range.hasStart) this->visitConstgeneric(range.start);
                if (range.hasEnd) this->visitConstgeneric(range.end);
            }
            break;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = data.as_Tuple();
            for (auto& t : e) {
                this->visitType(t);
            }
            break;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = data.as_Borrow();
            this->visitType(e.inner);
            break;
        }
        case HIRTypeData::TAG_Pointer: {
            auto& e = data.as_Pointer();
            this->visitType(e.inner);
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            auto& e = data.as_NamedFunction();
            this->visitPath(e.path, HIRVisitor::PathContext::VALUE);
            break;
        }
        case HIRTypeData::TAG_Function: {
            auto& e = data.as_Function();
            for (auto& t : e.argTypes) {
                this->visitType(t);
            }
            this->visitType(e.rettype);
            break;
        }
        case HIRTypeData::TAG_NodeType: {
            break;
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
    switch (pat.data.tag()) {
        case HIRPatternData::TAG_Any: {
            break;
        }
        case HIRPatternData::TAG_Box: {
            auto& e = pat.data.as_Box();
            this->visitPattern(*e.sub);
            break;
        }
        case HIRPatternData::TAG_Deref: {
            auto& e = pat.data.as_Deref();
            if (e.targetType) this->visitType(e.targetType);
            this->visitPattern(*e.sub);
            break;
        }
        case HIRPatternData::TAG_Ref: {
            auto& e = pat.data.as_Ref();
            this->visitPattern(*e.sub);
            break;
        }
        case HIRPatternData::TAG_Tuple: {
            auto& e = pat.data.as_Tuple();
            for (auto& subpat : e.subPatterns) {
                this->visitPattern(subpat);
            }
            break;
        }
        case HIRPatternData::TAG_SplitTuple: {
            auto& e = pat.data.as_SplitTuple();
            for (auto& subpat : e.leading) {
                this->visitPattern(subpat);
            }
            for (auto& subpat : e.trailing) {
                this->visitPattern(subpat);
            }
            break;
        }
        case HIRPatternData::TAG_PathValue: {
            auto& e = pat.data.as_PathValue();
            this->visitPath(e.path, HIRVisitor::PathContext::VALUE);
            break;
        }
        case HIRPatternData::TAG_PathTuple: {
            auto& e = pat.data.as_PathTuple();
            this->visitPath(e.path, HIRVisitor::PathContext::VALUE);
            for (auto& subpat : e.leading) {
                this->visitPattern(subpat);
            }
            for (auto& subpat : e.trailing) {
                this->visitPattern(subpat);
            }
            break;
        }
        case HIRPatternData::TAG_PathNamed: {
            auto& e = pat.data.as_PathNamed();
            this->visitPath(e.path, HIRVisitor::PathContext::TYPE);
            for (auto& sp : e.subPatterns) {
                this->visitPattern(sp.second);
            }
            break;
        }
        case HIRPatternData::TAG_Value: {
            auto& e = pat.data.as_Value();
            this->visitPatternVal(e.val);
            break;
        }
        case HIRPatternData::TAG_Range: {
            auto& e = pat.data.as_Range();
            if (e.start) {
                this->visitPatternVal(*e.start);
            }
            if (e.end) {
                this->visitPatternVal(*e.end);
            }
            break;
        }
        case HIRPatternData::TAG_Slice: {
            auto& e = pat.data.as_Slice();
            for (auto& sp : e.subPatterns) {
                this->visitPattern(sp);
            }
            break;
        }
        case HIRPatternData::TAG_SplitSlice: {
            auto& e = pat.data.as_SplitSlice();
            for (auto& sp : e.leading) {
                this->visitPattern(sp);
            }
            for (auto& sp : e.trailing) {
                this->visitPattern(sp);
            }
            break;
        }
        case HIRPatternData::TAG_Or: {
            auto& e = pat.data.as_Or();
            for (auto& sp : e) {
                this->visitPattern(sp);
            }
            break;
        }
    }
}

void HIRVisitor::visitPatternVal(HIRPattern::Value& val) {
    switch (val.tag()) {
        case HIRPattern::Value::TAG_Integer: {
            break;
        }
        case HIRPattern::Value::TAG_Float: {
            break;
        }
        case HIRPattern::Value::TAG_String: {
            break;
        }
        case HIRPattern::Value::TAG_ByteString: {
            break;
        }
        case HIRPattern::Value::TAG_Named: {
            auto& e = val.as_Named();
            this->visitPath(e.path, HIRVisitor::PathContext::VALUE);
            break;
        }
    }
}

void HIRVisitor::visitTraitPath(HIRTraitPath& p) {
    this->visitGenericPath(p.path, HIRVisitor::PathContext::TYPE);
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
    switch (p.data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& e = p.data.as_Generic();
            this->visitGenericPath(e, pc);
            break;
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& e = p.data.as_UfcsInherent();
            this->visitType(e.type);
            this->visitPathParams(e.params);
            this->visitPathParams(e.implParams);
            break;
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& e = p.data.as_UfcsKnown();
            this->visitType(e.type);
            this->visitGenericPath(e.trait, HIRVisitor::PathContext::TYPE);
            this->visitPathParams(e.params);
            break;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            auto& e = p.data.as_UfcsUnknown();
            this->visitType(e.type);
            this->visitPathParams(e.params);
            break;
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
    this->visitPathParams(p.params);
}

void HIRVisitor::visitExpr(HIRExprPtr& exp) {
    // Do nothing, leave expression stuff for user
    for (auto& t : exp.erasedTypes) {
        visitType(t);
    }
    for (auto& t : exp.bindings) {
        visitType(t);
    }
}

HIRVisitor::HIRVisitor(::StaticTraitResolve* resolve, HIRTypeInterner& types)
    : resolve_(resolve)
    , types(types)
{
}
