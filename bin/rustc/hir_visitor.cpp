#include "hir_visitor.h"

#include "hir_hir.h"
#include "hir_typeck_static.h"

HIRVisitor::~HIRVisitor() {
}

namespace {
    template <typename T, typename F>
    void visitImpls(HIRCrate::ImplGroup<std::unique_ptr<T>>& g, F cb) {
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

    bool walkTypesInConstgeneric(HIRVisitor& v, const HIRConstGeneric& c, HIRConstGeneric& out);
    bool walkTypesInPathParams(HIRVisitor& v, const HIRPathParams& p, HIRPathParams& out);
    bool walkTypesInGenericPath(HIRVisitor& v, const HIRGenericPath& p, HIRGenericPath& out);
    bool walkTypesInPath(HIRVisitor& v, const HIRPath& p, HIRPath& out);
    bool walkTypesInTraitPath(HIRVisitor& v, const HIRTraitPath& p, HIRTraitPath& out);

    bool walkTypesInPathParams(HIRVisitor& v, const HIRPathParams& p, HIRPathParams& out) {
        for (size_t i = 0; i < p.types.size(); i++) {
            auto nt = v.visitType(p.types[i]);
            if (nt != p.types[i]) {
                out = p.clone();
                out.types[i] = nt;
                for (size_t j = i + 1; j < out.types.size(); j++) {
                    out.types[j] = v.visitType(out.types[j]);
                }
                for (auto& val : out.values) {
                    HIRConstGeneric nv;
                    if (walkTypesInConstgeneric(v, val, nv)) {
                        val = mv$(nv);
                    }
                }
                return true;
            }
        }
        for (size_t i = 0; i < p.values.size(); i++) {
            HIRConstGeneric nv;
            if (walkTypesInConstgeneric(v, p.values[i], nv)) {
                out = p.clone();
                out.values[i] = mv$(nv);
                for (size_t j = i + 1; j < out.values.size(); j++) {
                    HIRConstGeneric nv2;
                    if (walkTypesInConstgeneric(v, out.values[j], nv2)) {
                        out.values[j] = mv$(nv2);
                    }
                }
                return true;
            }
        }
        return false;
    }

    bool walkTypesInConstgeneric(HIRVisitor& v, const HIRConstGeneric& c, HIRConstGeneric& out) {
        const auto* unevaluated = c.opt_Unevaluated();
        if (!unevaluated) {
            return false;
        }
        const auto& u = **unevaluated;
        auto nself = u.selfType ? v.visitType(u.selfType) : nullptr;
        HIRPathParams nimpl;
        HIRPathParams nitem;
        bool cimpl = walkTypesInPathParams(v, u.paramsImpl, nimpl);
        bool citem = walkTypesInPathParams(v, u.paramsItem, nitem);
        v.visitExpr(*u.expr);
        if (nself == u.selfType && !cimpl && !citem) {
            return false;
        }
        out = HIRConstGeneric(std::make_unique<HIRConstGenericUnevaluated>(u.clone()));
        auto& ou = *out.as_Unevaluated();
        ou.selfType = nself;
        if (cimpl) {
            ou.paramsImpl = mv$(nimpl);
        }
        if (citem) {
            ou.paramsItem = mv$(nitem);
        }
        return true;
    }

    bool walkTypesInGenericPath(HIRVisitor& v, const HIRGenericPath& p, HIRGenericPath& out) {
        HIRPathParams np;
        if (!walkTypesInPathParams(v, p.params, np)) {
            return false;
        }
        out = HIRGenericPath(p.path, mv$(np));
        return true;
    }

    bool walkTypesInPath(HIRVisitor& v, const HIRPath& p, HIRPath& out) {
        switch (p.data.tag()) {
            case HIRPathData::TAG_Generic: {
                HIRGenericPath ng;
                if (!walkTypesInGenericPath(v, p.data.as_Generic(), ng)) {
                    return false;
                }
                out = HIRPath(mv$(ng));
                return true;
            }
            case HIRPathData::TAG_UfcsInherent: {
                const auto& e = p.data.as_UfcsInherent();
                auto ntype = v.visitType(e.type);
                HIRPathParams nparams;
                HIRPathParams nimplParams;
                bool cparams = walkTypesInPathParams(v, e.params, nparams);
                bool cimpl = walkTypesInPathParams(v, e.implParams, nimplParams);
                if (ntype == e.type && !cparams && !cimpl) {
                    return false;
                }
                out = p.clone();
                auto& oe = out.data.as_UfcsInherent();
                oe.type = ntype;
                if (cparams) {
                    oe.params = mv$(nparams);
                }
                if (cimpl) {
                    oe.implParams = mv$(nimplParams);
                }
                return true;
            }
            case HIRPathData::TAG_UfcsKnown: {
                const auto& e = p.data.as_UfcsKnown();
                auto ntype = v.visitType(e.type);
                HIRGenericPath ntrait;
                HIRPathParams nparams;
                bool ctrait = walkTypesInGenericPath(v, e.trait, ntrait);
                bool cparams = walkTypesInPathParams(v, e.params, nparams);
                if (ntype == e.type && !ctrait && !cparams) {
                    return false;
                }
                out = p.clone();
                auto& oe = out.data.as_UfcsKnown();
                oe.type = ntype;
                if (ctrait) {
                    oe.trait = mv$(ntrait);
                }
                if (cparams) {
                    oe.params = mv$(nparams);
                }
                return true;
            }
            case HIRPathData::TAG_UfcsUnknown: {
                const auto& e = p.data.as_UfcsUnknown();
                auto ntype = v.visitType(e.type);
                HIRPathParams nparams;
                bool cparams = walkTypesInPathParams(v, e.params, nparams);
                if (ntype == e.type && !cparams) {
                    return false;
                }
                out = p.clone();
                auto& oe = out.data.as_UfcsUnknown();
                oe.type = ntype;
                if (cparams) {
                    oe.params = mv$(nparams);
                }
                return true;
            }
        }
        return false;
    }

    bool walkTypesInTraitPath(HIRVisitor& v, const HIRTraitPath& p, HIRTraitPath& out) {
        bool changed = false;
        auto ensureOut = [&]() {
            if (!changed) {
                out = p.clone();
                changed = true;
            }
        };
        {
            HIRGenericPath ng;
            if (walkTypesInGenericPath(v, p.path, ng)) {
                ensureOut();
                out.path = mv$(ng);
            }
        }
        for (const auto& assoc : p.typeBounds) {
            HIRGenericPath ns;
            if (walkTypesInGenericPath(v, assoc.second.sourceTrait, ns)) {
                ensureOut();
                out.typeBounds.at(assoc.first).sourceTrait = mv$(ns);
            }
            HIRPathParams np;
            if (walkTypesInPathParams(v, assoc.second.atyParams, np)) {
                ensureOut();
                out.typeBounds.at(assoc.first).atyParams = mv$(np);
            }
            auto nt = v.visitType(assoc.second.type);
            if (nt != assoc.second.type) {
                ensureOut();
                out.typeBounds.at(assoc.first).type = nt;
            }
        }
        for (const auto& assoc : p.traitBounds) {
            HIRGenericPath ns;
            if (walkTypesInGenericPath(v, assoc.second.sourceTrait, ns)) {
                ensureOut();
                out.traitBounds.at(assoc.first).sourceTrait = mv$(ns);
            }
            HIRPathParams np;
            if (walkTypesInPathParams(v, assoc.second.atyParams, np)) {
                ensureOut();
                out.traitBounds.at(assoc.first).atyParams = mv$(np);
            }
            for (size_t i = 0; i < assoc.second.traits.size(); i++) {
                HIRTraitPath ntp;
                if (walkTypesInTraitPath(v, assoc.second.traits[i], ntp)) {
                    ensureOut();
                    out.traitBounds.at(assoc.first).traits[i] = mv$(ntp);
                }
            }
        }
        return changed;
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
    for (auto& named : mod.modItems) {
        const auto& name = named.first;
        auto& item = named.second->ent;
        switch (item.tag()) {
            case HIRTypeItem::TAG_Import: {
                break;
            }
            case HIRTypeItem::TAG_Module: {
                auto& e = item.as_Module();
                this->visitModule(p + name, e);
                break;
            }
            case HIRTypeItem::TAG_TypeAlias: {
                auto& e = item.as_TypeAlias();
                this->visitTypeAlias(p + name, e);
                break;
            }
            case HIRTypeItem::TAG_TraitAlias: {
                auto& e = item.as_TraitAlias();
                this->visitTraitAlias(p + name, e);
                break;
            }
            case HIRTypeItem::TAG_ExternType: {
                break;
            }
            case HIRTypeItem::TAG_Enum: {
                auto& e = item.as_Enum();
                this->visitEnum(p + name, e);
                break;
            }
            case HIRTypeItem::TAG_Struct: {
                auto& e = item.as_Struct();
                this->visitStruct(p + name, e);
                break;
            }
            case HIRTypeItem::TAG_Union: {
                auto& e = item.as_Union();
                this->visitUnion(p + name, e);
                break;
            }
            case HIRTypeItem::TAG_Trait: {
                auto& e = item.as_Trait();
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
                break;
            }
            case HIRValueItem::TAG_Constant: {
                auto& e = *item.as_Constant();
                this->visitConstant(p + name, e);
                break;
            }
            case HIRValueItem::TAG_Static: {
                auto& e = *item.as_Static();
                this->visitStatic(p + name, e);
                break;
            }
            case HIRValueItem::TAG_StructConstant: {
                break;
            }
            case HIRValueItem::TAG_Function: {
                auto& e = *item.as_Function();
                this->visitFunction(p + name, e);
                break;
            }
            case HIRValueItem::TAG_StructConstructor: {
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
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::Unknown, impl.params);
    }
    this->visitParams(impl.params);
    updateType(impl.type);

    for (auto& method : impl.methods) {
        this->visitFunction(p + method.first, method.second.data);
    }
    for (auto& ent : impl.constants) {
        this->visitConstant(p + ent.first, ent.second.data);
    }
    for (auto& ent : impl.types) {
        this->visitInherentType(p + ent.first, ent.second.data);
    }
    if (resolve_) {
        resolve_->clearImplGenerics();
    }
}

void HIRVisitor::visitInherentType(HIRItemPath p, HIRTypeAlias& item) {
    if (resolve_) {
        resolve_->setItemGenericsRaw(item.params);
    }
    this->visitParams(item.params);
    updateType(item.type);
    if (resolve_) {
        resolve_->clearItemGenerics();
    }
}

void HIRVisitor::visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) {
    HIRItemPath p(impl.type, traitPath, impl.traitArgs);
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::Unknown, impl.params);
    }
    this->visitParams(impl.params);
    {
        HIRGenericPath gp{traitPath, mv$(impl.traitArgs)};
        this->visitGenericPath(gp, PathContext::TRAIT);
        impl.traitArgs = mv$(gp.params);
    }
    updateType(impl.type);

    for (auto& ent : impl.methods) {
        this->visitFunction(p + ent.first, ent.second.data);
    }
    for (auto& ent : impl.constants) {
        this->visitConstant(p + ent.first, ent.second.data);
    }
    for (auto& ent : impl.statics) {
        this->visitStatic(p + ent.first, ent.second.data);
    }
    for (auto& ent : impl.types) {
        updateType(ent.second.data);
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
    {
        HIRGenericPath gp{traitPath, mv$(impl.traitArgs)};
        this->visitGenericPath(gp, PathContext::TRAIT);
        impl.traitArgs = mv$(gp.params);
    }
    updateType(impl.type);
    if (resolve_) {
        resolve_->clearImplGenerics();
    }
}

void HIRVisitor::visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) {
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::Unknown, item.params);
    }
    this->visitParams(item.params);
    updateType(item.type);
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

    this->visitParams(item.params);
    for (auto& par : item.parentTraits) {
        this->visitTraitPath(par);
    }
    for (auto& par : item.allParentTraits) {
        this->visitTraitPath(par);
    }
    for (auto& i : item.types) {
        auto itemPath = HIRItemPath(traitIp, i.first.c_str());
        this->visitAssociatedtype(itemPath, i.second);
    }
    for (auto& i : item.values) {
        auto itemPath = HIRItemPath(traitIp, i.first.c_str());
        switch (i.second.tag()) {
            case HIRTraitValueItem::TAG_Constant: {
                auto& e = i.second.as_Constant();
                this->visitConstant(itemPath, e);
                break;
            }
            case HIRTraitValueItem::TAG_Static: {
                auto& e = i.second.as_Static();
                this->visitStatic(itemPath, e);
                break;
            }
            case HIRTraitValueItem::TAG_Function: {
                auto& e = i.second.as_Function();
                this->visitFunction(itemPath, e);
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
                updateType(ty.ent);
            }
            break;
        }
        case HIRStructData::TAG_Named: {
            auto& e = item.data.as_Named();
            for (auto& field : e) {
                updateType(field.ty);
                if (field.defaultValue) {
                    this->visitGenericPath(*field.defaultValue, PathContext::VALUE);
                }
            }
            break;
        }
    }
    if (resolve_) {
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
                updateType(var.type);
                this->visitExpr(var.discriminantExpr);
            }
            break;
        }
    }
    if (resolve_) {
        resolve_->clearImplGenerics();
    }
}

void HIRVisitor::visitUnion(HIRItemPath p, HIRUnion& item) {
    if (resolve_) {
        resolve_->setImplGenericsRaw(MetadataType::Unknown, item.params);
    }
    this->visitParams(item.params);
    for (auto& var : item.variants) {
        updateType(var.ty);
        BUG_ASSERT(!var.defaultValue);
    }
    if (resolve_) {
        resolve_->clearImplGenerics();
    }
}

void HIRVisitor::visitAssociatedtype(HIRItemPath p, HIRAssociatedType& item) {
    for (auto& bound : item.traitBounds) {
        this->visitTraitPath(bound);
    }
    updateType(item.defaultValue);
}

void HIRVisitor::visitFunction(HIRItemPath p, HIRFunction& item) {
    if (resolve_) {
        resolve_->setItemGenericsRaw(item.params);
    }
    this->visitParams(item.params);
    for (auto& arg : item.args) {
        this->visitPattern(arg.first);
        updateType(arg.second);
    }
    updateType(item.returnType);
    if (item.traitReturnType) {
        updateType(*item.traitReturnType);
    }
    this->visitExpr(item.code);
    if (resolve_) {
        resolve_->clearItemGenerics();
    }
}

void HIRVisitor::visitStatic(HIRItemPath p, HIRStatic& item) {
    if (resolve_) {
        resolve_->setItemGenericsRaw(item.params);
    }
    updateType(item.type);
    this->visitExpr(item.value);
    if (resolve_) {
        resolve_->clearItemGenerics();
    }
}

void HIRVisitor::visitConstant(HIRItemPath p, HIRConstant& item) {
    if (resolve_) {
        resolve_->setItemGenericsRaw(item.params);
    }
    this->visitParams(item.params);
    updateType(item.type);
    this->visitExpr(item.value);
    if (resolve_) {
        resolve_->clearItemGenerics();
    }
}

void HIRVisitor::visitParams(HIRGenericParams& params) {
    for (auto& tps : params.types) {
        updateType(tps.defaultValue);
    }
    for (auto& val : params.values) {
        updateType(val.type);
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
            updateType(e.type);
            this->visitTraitPath(e.trait);
            break;
        }
        case HIRGenericBound::TAG_TypeEquality: {
            auto& e = bound.as_TypeEquality();
            updateType(e.type);
            updateType(e.otherType);
            break;
        }
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
            if (e.targetType) {
                updateType(e.targetType);
            }
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

HIRTypeRef HIRVisitor::visitType(HIRTypeRef ty) {
    BUG_ASSERT(ty);
    switch (ty->tag()) {
        case HIRTypeData::TAG_Infer:
        case HIRTypeData::TAG_Diverge:
        case HIRTypeData::TAG_Primitive:
        case HIRTypeData::TAG_Generic:
        case HIRTypeData::TAG_NodeType:
            return ty;
        case HIRTypeData::TAG_Path: {
            const auto& e = ty->as_Path();
            auto np = HIRPath(HIRSimplePath());
            if (!walkTypesInPath(*this, e.path, np)) {
                return ty;
            }
            auto data = ty->cloneData();
            data.as_Path().path = mv$(np);
            return typeInterner().intern(mv$(data));
        }
        case HIRTypeData::TAG_TraitObject: {
            const auto& e = ty->as_TraitObject();
            HIRTraitPath ntrait;
            bool ctrait = e.trait.path != HIRSimplePath() && walkTypesInTraitPath(*this, e.trait, ntrait);
            HIRGenericPath nmarker;
            size_t markerIdx = e.markers.size();
            for (size_t i = 0; i < e.markers.size(); i++) {
                if (walkTypesInGenericPath(*this, e.markers[i], nmarker)) {
                    markerIdx = i;
                    break;
                }
            }
            if (!ctrait && markerIdx == e.markers.size()) {
                return ty;
            }
            auto data = ty->cloneData();
            auto& ne = data.as_TraitObject();
            if (ctrait) {
                ne.trait = mv$(ntrait);
            }
            if (markerIdx < ne.markers.size()) {
                ne.markers[markerIdx] = mv$(nmarker);
                for (size_t j = markerIdx + 1; j < ne.markers.size(); j++) {
                    HIRGenericPath nm;
                    if (walkTypesInGenericPath(*this, ne.markers[j], nm)) {
                        ne.markers[j] = mv$(nm);
                    }
                }
            }
            return typeInterner().intern(mv$(data));
        }
        case HIRTypeData::TAG_ErasedType: {
            const auto& e = ty->as_ErasedType();
            bool cinner = false;
            HIRTypeRef ninner = nullptr;
            HIRPathParams naliasParams;
            auto norigin = HIRPath(HIRSimplePath());
            switch (e.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Known: {
                    ninner = visitType(e.inner.as_Known());
                    cinner = ninner != e.inner.as_Known();
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Alias: {
                    cinner = walkTypesInPathParams(*this, e.inner.as_Alias().params, naliasParams);
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    const auto& ee = e.inner.as_Fcn();
                    if (ee.origin != HIRSimplePath()) {
                        cinner = walkTypesInPath(*this, ee.origin, norigin);
                    }
                    break;
                }
            }
            HIRPathParams nuse;
            bool cuse = walkTypesInPathParams(*this, e.use, nuse);
            HIRTraitPath ntrait;
            size_t traitIdx = e.traits.size();
            for (size_t i = 0; i < e.traits.size(); i++) {
                if (walkTypesInTraitPath(*this, e.traits[i], ntrait)) {
                    traitIdx = i;
                    break;
                }
            }
            if (!cinner && !cuse && traitIdx == e.traits.size()) {
                return ty;
            }
            auto data = ty->cloneData();
            auto& ne = data.as_ErasedType();
            if (cinner) {
                switch (ne.inner.tag()) {
                    case TypeDataErasedTypeInner::TAG_Known:
                        ne.inner.as_Known() = ninner;
                        break;
                    case TypeDataErasedTypeInner::TAG_Alias:
                        ne.inner.as_Alias().params = mv$(naliasParams);
                        break;
                    case TypeDataErasedTypeInner::TAG_Fcn:
                        ne.inner.as_Fcn().origin = mv$(norigin);
                        break;
                }
            }
            if (cuse) {
                ne.use = mv$(nuse);
            }
            if (traitIdx < ne.traits.size()) {
                ne.traits[traitIdx] = mv$(ntrait);
                for (size_t j = traitIdx + 1; j < ne.traits.size(); j++) {
                    HIRTraitPath nt;
                    if (walkTypesInTraitPath(*this, ne.traits[j], nt)) {
                        ne.traits[j] = mv$(nt);
                    }
                }
            }
            return typeInterner().intern(mv$(data));
        }
        case HIRTypeData::TAG_Array: {
            const auto& e = ty->as_Array();
            auto ninner = visitType(e.inner);
            HIRConstGeneric nsize;
            const auto* se = e.size.opt_Unevaluated();
            bool csize = se && walkTypesInConstgeneric(*this, *se, nsize);
            if (ninner == e.inner && !csize) {
                return ty;
            }
            auto data = ty->cloneData();
            auto& ne = data.as_Array();
            ne.inner = ninner;
            if (csize) {
                ne.size.as_Unevaluated() = mv$(nsize);
            }
            return typeInterner().intern(mv$(data));
        }
        case HIRTypeData::TAG_Slice: {
            auto ninner = visitType(ty->as_Slice().inner);
            if (ninner == ty->as_Slice().inner) {
                return ty;
            }
            auto data = ty->cloneData();
            data.as_Slice().inner = ninner;
            return typeInterner().intern(mv$(data));
        }
        case HIRTypeData::TAG_Pattern: {
            const auto& e = ty->as_Pattern();
            auto ninner = visitType(e.inner);
            HIRConstGeneric nc;
            size_t rangeIdx = ~size_t(0);
            bool rangeStart = false;
            for (size_t i = 0; i < e.pattern.alternatives.size() && rangeIdx == ~size_t(0); i++) {
                const auto& range = e.pattern.alternatives[i];
                if (range.hasStart && walkTypesInConstgeneric(*this, range.start, nc)) {
                    rangeIdx = i;
                    rangeStart = true;
                } else if (range.hasEnd && walkTypesInConstgeneric(*this, range.end, nc)) {
                    rangeIdx = i;
                }
            }
            if (ninner == e.inner && rangeIdx == ~size_t(0)) {
                return ty;
            }
            auto data = ty->cloneData();
            auto& ne = data.as_Pattern();
            ne.inner = ninner;
            if (rangeIdx != ~size_t(0)) {
                auto& range = ne.pattern.alternatives[rangeIdx];
                (rangeStart ? range.start : range.end) = mv$(nc);
                HIRConstGeneric nc2;
                if (rangeStart && range.hasEnd && walkTypesInConstgeneric(*this, range.end, nc2)) {
                    range.end = mv$(nc2);
                }
                for (size_t j = rangeIdx + 1; j < ne.pattern.alternatives.size(); j++) {
                    auto& r = ne.pattern.alternatives[j];
                    if (r.hasStart && walkTypesInConstgeneric(*this, r.start, nc2)) {
                        r.start = mv$(nc2);
                    }
                    if (r.hasEnd && walkTypesInConstgeneric(*this, r.end, nc2)) {
                        r.end = mv$(nc2);
                    }
                }
            }
            return typeInterner().intern(mv$(data));
        }
        case HIRTypeData::TAG_Tuple: {
            const auto& e = ty->as_Tuple();
            for (size_t i = 0; i < e.size(); i++) {
                auto nt = visitType(e[i]);
                if (nt != e[i]) {
                    auto data = ty->cloneData();
                    auto& ne = data.as_Tuple();
                    ne[i] = nt;
                    for (size_t j = i + 1; j < ne.size(); j++) {
                        ne[j] = visitType(ne[j]);
                    }
                    return typeInterner().intern(mv$(data));
                }
            }
            return ty;
        }
        case HIRTypeData::TAG_Borrow: {
            auto ninner = visitType(ty->as_Borrow().inner);
            if (ninner == ty->as_Borrow().inner) {
                return ty;
            }
            auto data = ty->cloneData();
            data.as_Borrow().inner = ninner;
            return typeInterner().intern(mv$(data));
        }
        case HIRTypeData::TAG_Pointer: {
            auto ninner = visitType(ty->as_Pointer().inner);
            if (ninner == ty->as_Pointer().inner) {
                return ty;
            }
            auto data = ty->cloneData();
            data.as_Pointer().inner = ninner;
            return typeInterner().intern(mv$(data));
        }
        case HIRTypeData::TAG_NamedFunction: {
            const auto& e = ty->as_NamedFunction();
            auto np = HIRPath(HIRSimplePath());
            if (!walkTypesInPath(*this, e.path, np)) {
                return ty;
            }
            auto data = ty->cloneData();
            data.as_NamedFunction().path = mv$(np);
            return typeInterner().intern(mv$(data));
        }
        case HIRTypeData::TAG_Function: {
            const auto& e = ty->as_Function();
            auto nret = visitType(e.rettype);
            size_t argIdx = e.argTypes.size();
            HIRTypeRef narg = nullptr;
            for (size_t i = 0; i < e.argTypes.size(); i++) {
                narg = visitType(e.argTypes[i]);
                if (narg != e.argTypes[i]) {
                    argIdx = i;
                    break;
                }
            }
            if (nret == e.rettype && argIdx == e.argTypes.size()) {
                return ty;
            }
            auto data = ty->cloneData();
            auto& ne = data.as_Function();
            ne.rettype = nret;
            if (argIdx < ne.argTypes.size()) {
                ne.argTypes[argIdx] = narg;
                for (size_t j = argIdx + 1; j < ne.argTypes.size(); j++) {
                    ne.argTypes[j] = visitType(ne.argTypes[j]);
                }
            }
            return typeInterner().intern(mv$(data));
        }
    }
    return ty;
}

void HIRVisitor::visitTypeDataChildren(HIRTypeData& data) {
    switch (data.tag()) {
        case HIRTypeData::TAG_Infer:
        case HIRTypeData::TAG_Diverge:
        case HIRTypeData::TAG_Primitive:
        case HIRTypeData::TAG_Generic:
        case HIRTypeData::TAG_NodeType:
            break;
        case HIRTypeData::TAG_Path: {
            auto& e = data.as_Path();
            this->visitPath(e.path, HIRVisitor::PathContext::TYPE);
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
                    updateType(e.inner.as_Known());
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Alias: {
                    this->visitPathParams(e.inner.as_Alias().params);
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
            for (auto& trait : e.traits) {
                this->visitTraitPath(trait);
            }
            break;
        }
        case HIRTypeData::TAG_Array: {
            auto& e = data.as_Array();
            updateType(e.inner);
            if (auto* se = e.size.opt_Unevaluated()) {
                this->visitConstgeneric(*se);
            }
            break;
        }
        case HIRTypeData::TAG_Slice: {
            updateType(data.as_Slice().inner);
            break;
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = data.as_Pattern();
            updateType(e.inner);
            for (auto& range : e.pattern.alternatives) {
                if (range.hasStart) {
                    this->visitConstgeneric(range.start);
                }
                if (range.hasEnd) {
                    this->visitConstgeneric(range.end);
                }
            }
            break;
        }
        case HIRTypeData::TAG_Tuple: {
            for (auto& t : data.as_Tuple()) {
                updateType(t);
            }
            break;
        }
        case HIRTypeData::TAG_Borrow: {
            updateType(data.as_Borrow().inner);
            break;
        }
        case HIRTypeData::TAG_Pointer: {
            updateType(data.as_Pointer().inner);
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
                updateType(t);
            }
            updateType(e.rettype);
            break;
        }
    }
}

void HIRVisitor::visitConstgeneric(HIRConstGeneric& c) {
    if (auto* unevaluated = c.opt_Unevaluated()) {
        if ((*unevaluated)->selfType) {
            updateType((*unevaluated)->selfType);
        }
        this->visitPathParams((*unevaluated)->paramsImpl);
        this->visitPathParams((*unevaluated)->paramsItem);
        this->visitExpr(*(*unevaluated)->expr);
    }
}

void HIRVisitor::visitTraitPath(HIRTraitPath& p) {
    this->visitGenericPath(p.path, HIRVisitor::PathContext::TRAIT);
    for (auto& assoc : p.typeBounds) {
        this->visitGenericPath(assoc.second.sourceTrait, HIRVisitor::PathContext::TRAIT);
        this->visitPathParams(assoc.second.atyParams);
        updateType(assoc.second.type);
    }
    for (auto& assoc : p.traitBounds) {
        this->visitGenericPath(assoc.second.sourceTrait, HIRVisitor::PathContext::TRAIT);
        this->visitPathParams(assoc.second.atyParams);
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
            updateType(e.type);
            this->visitPathParams(e.params);
            this->visitPathParams(e.implParams);
            break;
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& e = p.data.as_UfcsKnown();
            updateType(e.type);
            this->visitGenericPath(e.trait, HIRVisitor::PathContext::TYPE);
            this->visitPathParams(e.params);
            break;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            auto& e = p.data.as_UfcsUnknown();
            updateType(e.type);
            this->visitPathParams(e.params);
            break;
        }
    }
}

void HIRVisitor::visitPathParams(HIRPathParams& p) {
    for (auto& ty : p.types) {
        updateType(ty);
    }
    for (auto& v : p.values) {
        visitConstgeneric(v);
    }
}

void HIRVisitor::visitGenericPath(HIRGenericPath& p, HIRVisitor::PathContext /*pc*/) {
    this->visitPathParams(p.params);
}

void HIRVisitor::visitExpr(HIRExprPtr& exp) {
    for (auto& t : exp.erasedTypes) {
        updateType(t);
    }
    for (auto& t : exp.bindings) {
        updateType(t);
    }
}

HIRVisitor::HIRVisitor(::StaticTraitResolve* resolve, HIRTypeInterner& types)
    : resolve_(resolve)
    , types(types)
{
}
