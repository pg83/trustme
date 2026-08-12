#pragma once

#include "hir_expr_ptr.h"

class RcString;
class TypeRef;
struct Span;

    class ASTExprNode;
    class ASTPath;
    struct ASTPathParams;
    class ASTPattern;

namespace HIR {
    class Crate;
    class GenericPath;
    class Path;
    struct PathParams;
    struct Pattern;
    struct SimplePath;
}

enum class FromASTPathClass {
    Type,
    Value,
    Macro,
};

extern ::HIR::ExprPtr LowerHIRExprNode(const ASTExprNode& e);
extern ::HIR::Path LowerHIRPath(const Span& sp, const ASTPath& path, FromASTPathClass pc);
extern ::HIR::GenericPath LowerHIRGenericPath(const Span& sp, const ASTPath& path, FromASTPathClass pc, bool allowAssoc = false);
extern ::HIR::SimplePath LowerHIRSimplePath(const Span& sp, const ASTPath& path, FromASTPathClass pc, bool allowFinalGeneric = false);
extern ::HIR::PathParams LowerHIRPathParams(const Span& sp, const ASTPathParams& srcParams, bool allowAssoc);
extern ::HIR::TypeRef LowerHIRType(const ::TypeRef& ty);
extern ::HIR::Pattern LowerHIRPattern(const ASTPattern& pat);

extern RcString gCoreCrate;
extern RcString gCrateName;
extern ::HIR::Crate* gCratePtr;
