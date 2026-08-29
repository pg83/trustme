#pragma once

#include "hir_expr_ptr.h"

class RcString;
struct ASTType;
struct Span;

class ASTExprNode;
class ASTCrate;
class ASTPath;
struct ASTPathParams;
class ASTPattern;

class HIRCrate;
struct WireBoard;
class HIRGenericPath;
class HIRPath;
struct HIRPathParams;
struct HIRPattern;
struct HIRSimplePath;

namespace stl {
    class ObjPool;
}

enum class FromASTPathClass {
    Type,
    Value,
    Macro,
};

HIRExprPtr LowerHIRExprNode(const ASTExprNode& e);
HIRPath LowerHIRPath(const Span& sp, const ASTPath& path, FromASTPathClass pc);
HIRGenericPath LowerHIRGenericPath(const Span& sp, const ASTPath& path, FromASTPathClass pc, bool allowAssoc = false);
HIRSimplePath LowerHIRSimplePath(const Span& sp, const ASTPath& path, FromASTPathClass pc, bool allowFinalGeneric = false);
HIRPathParams LowerHIRPathParams(const Span& sp, const ASTPathParams& srcParams, bool allowAssoc);
HIRTypeRef LowerHIRType(::ASTType* ty);
HIRPattern LowerHIRPattern(const ASTPattern& pat);
HIRCrate* LowerHIRFromAST(const WireBoard& wb, stl::ObjPool* pool, ASTCrate& crate);
