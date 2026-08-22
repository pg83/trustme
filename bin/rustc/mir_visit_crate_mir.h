struct WireBoard;
#pragma once

#include "hir_visitor.h"
#include "hir_typeck_static.h"

struct MIRExprCallback {
    virtual void visit(const StaticTraitResolve& resolve, const HIRItemPath& ip, HIRExprPtr& expr, const HIRFunction::argsT& args, const HIRTypeData* retType) = 0;
};

template <typename F>
struct MIRExprCb final: MIRExprCallback {
    F f;

    explicit MIRExprCb(F f)
        : f(f)
    {
    }

    void visit(const StaticTraitResolve& resolve, const HIRItemPath& ip, HIRExprPtr& expr, const HIRFunction::argsT& args, const HIRTypeData* retType) override {
        f(resolve, ip, expr, args, retType);
    }
};

class MIROuterVisitor: public HIRVisitor {
private:
    StaticTraitResolve resolve_;
    MIRExprCallback& cb;

public:
    MIROuterVisitor(const WireBoard& wb, const HIRCrate& crate, MIRExprCallback& cb);

    void visitExpr(HIRExprPtr& exp) override;

    [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override;
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
