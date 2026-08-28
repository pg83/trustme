#pragma once

#include "hir_hir.h"
#include "hir_item_path.h"

class StaticTraitResolve;

// TODO: Split into Visitor and ItemVisitor
class HIRVisitor {
    StaticTraitResolve* resolve_;
    HIRTypeInterner& types;

public:
    HIRVisitor(::StaticTraitResolve* resolve, HIRTypeInterner& types);

    virtual ~HIRVisitor();

protected:
    HIRTypeInterner& typeInterner() const {
        return types;
    }

public:
    virtual void visitCrate(HIRCrate& crate);

    virtual void visitModule(HIRItemPath p, HIRModule& mod);
    virtual void visitGlobalAssembly(HIRGlobalAssembly& item);

    virtual void visitTypeImpl(HIRTypeImpl& impl);
    virtual void visitInherentType(HIRItemPath p, HIRTypeAlias& item);
    virtual void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl);
    virtual void visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl);

    virtual void visitTypeAlias(HIRItemPath p, HIRTypeAlias& item);
    virtual void visitTraitAlias(HIRItemPath p, HIRTraitAlias& item);
    virtual void visitTrait(HIRItemPath p, HIRTrait& item);
    virtual void visitStruct(HIRItemPath p, HIRStruct& item);
    virtual void visitEnum(HIRItemPath p, HIREnum& item);
    virtual void visitUnion(HIRItemPath p, HIRUnion& item);
    virtual void visitAssociatedtype(HIRItemPath p, HIRAssociatedType& item);

    virtual void visitFunction(HIRItemPath p, HIRFunction& item);
    virtual void visitStatic(HIRItemPath p, HIRStatic& item);
    virtual void visitConstant(HIRItemPath p, HIRConstant& item);

    virtual void visitParams(HIRGenericParams& params);
    virtual void visitGenericBound(HIRGenericBound& bound);
    virtual void visitPattern(HIRPattern& pat);
    virtual void visitPatternVal(HIRPattern::Value& val);

    enum class PathContext {
        TYPE,
        TRAIT,

        VALUE,
    };

    [[nodiscard]] virtual HIRTypeRef visitType(HIRTypeRef ty);

    void updateType(HIRTypeRef& ty) {
        ty = visitType(ty);
    }

    void visitTypeDataChildren(HIRTypeData& data);

    [[nodiscard]] HIRTypeRef visitTypeViaHooks(HIRTypeRef ty) {
        auto data = ty->cloneData();
        visitTypeDataChildren(data);
        return typeInterner().intern(mv$(data));
    }

    [[nodiscard]] HIRTypeRef visitTypeDefaultViaHooks(HIRTypeRef ty) {
        switch (ty->tag()) {
            case HIRTypeData::TAG_Path:
            case HIRTypeData::TAG_TraitObject:
            case HIRTypeData::TAG_ErasedType:
            case HIRTypeData::TAG_Array:
            case HIRTypeData::TAG_Pattern:
            case HIRTypeData::TAG_NamedFunction:
                return visitTypeViaHooks(ty);
            default:
                return HIRVisitor::visitType(ty);
        }
    }

    virtual void visitTraitPath(HIRTraitPath& p);
    virtual void visitPath(HIRPath& p, PathContext);
    virtual void visitPathParams(HIRPathParams& p);
    virtual void visitGenericPath(HIRGenericPath& p, PathContext);
    virtual void visitConstgeneric(HIRConstGeneric& c);

    virtual void visitExpr(HIRExprPtr& exp);
};
