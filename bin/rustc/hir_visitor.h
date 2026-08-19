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

    // - Type Items
    virtual void visitTypeAlias(HIRItemPath p, HIRTypeAlias& item);
    virtual void visitTraitAlias(HIRItemPath p, HIRTraitAlias& item);
    virtual void visitTrait(HIRItemPath p, HIRTrait& item);
    virtual void visitStruct(HIRItemPath p, HIRStruct& item);
    virtual void visitEnum(HIRItemPath p, HIREnum& item);
    virtual void visitUnion(HIRItemPath p, HIRUnion& item);
    virtual void visitAssociatedtype(HIRItemPath p, HIRAssociatedType& item);
    // - Value Items
    virtual void visitFunction(HIRItemPath p, HIRFunction& item);
    virtual void visitStatic(HIRItemPath p, HIRStatic& item);
    virtual void visitConstant(HIRItemPath p, HIRConstant& item);

    // - Misc
    virtual void visitParams(HIRGenericParams& params);
    virtual void visitGenericBound(HIRGenericBound& bound);
    virtual void visitPattern(HIRPattern& pat);
    virtual void visitPatternVal(HIRPattern::Value& val);

    enum class PathContext {
        TYPE,
        TRAIT,

        VALUE,
    };

    // -- Types are interned and immutable, so a type transformation is a
    // pure function: visitType returns the canonical type after this pass.
    // An unchanged type is returned as-is; pointer identity is the change
    // signal, produced by the one place that knows - the pass itself. The
    // base implementation recurses into child types and rebuilds a node
    // only when a child came back different.
    //
    // The mutable visit hooks below stay for OWNED structures (item
    // signatures, bounds, expressions). They are NOT called for structures
    // embedded in interned types: a pass that rewrites paths or params
    // inside types overrides visitType and says so explicitly.
    [[nodiscard]] virtual HIRTypeRef visitType(HIRTypeRef ty);

    void updateType(HIRTypeRef& ty) {
        ty = visitType(ty);
    }

    // For passes that rebuild a node: run the owned-structure hooks over
    // every child of a working (not yet interned) HIRTypeData. Only for
    // code that has already decided it is changing the node.
    void visitTypeDataChildren(HIRTypeData& data);

    // For passes whose owned-structure hooks must also see the structures
    // embedded in types: rebuild the node through the hooks.
    [[nodiscard]] HIRTypeRef visitTypeViaHooks(HIRTypeRef ty) {
        auto data = ty->cloneData();
        visitTypeDataChildren(data);
        return typeInterner().intern(mv$(data));
    }

    // Dispatch helper for such passes: hook-carrying node kinds go through
    // the owned hooks, everything else through the plain recursion.
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
