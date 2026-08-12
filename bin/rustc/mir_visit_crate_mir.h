#pragma once

#include "hir_visitor.h"
#include "hir_typeck_static.h"

class MIROuterVisitor: public HIRVisitor {
public:
    typedef ::std::function<void(const StaticTraitResolve& resolve, const HIRItemPath& ip, HIRExprPtr& expr, const HIRFunction::argsT& args, const HIRTypeData* retType)> cbT;

private:
    StaticTraitResolve mResolve;
    cbT cb;

public:
    MIROuterVisitor(const HIRCrate& crate, cbT cb);

    void visitExpr(HIRExprPtr& exp) override;

    void visitType(HIRTypeRef& ty) override;
    void visitConstgeneric(HIRConstGeneric& value) override;

    // ------
    // Code-containing items
    // ------
    void visitFunction(HIRItemPath p, HIRFunction& item) override;
    void visitStatic(HIRItemPath p, HIRStatic& item) override;
    void visitConstant(HIRItemPath p, HIRConstant& item) override;
    void visitStruct(HIRItemPath p, HIRStruct& item) override;
    void visitUnion(HIRItemPath p, HIRUnion& item) override;
    void visitEnum(HIRItemPath p, HIREnum& item) override;

    // Boilerplate
    void visitTrait(HIRItemPath p, HIRTrait& item) override;
    void visitTypeImpl(HIRTypeImpl& impl) override;
    void visitInherentType(HIRItemPath p, HIRTypeAlias& item) override;
    void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override;
};
