#pragma once

#include "hir_expr_ptr.h"

class RcString;
class TypeRef;
struct Span;

class ASTExprNode;
class ASTPath;
struct ASTPathParams;
class ASTPattern;

class HIRCrate;
class HIRGenericPath;
class HIRPath;
struct HIRPathParams;
struct HIRPattern;
struct HIRSimplePath;

enum class FromASTPathClass {
    Type,
    Value,
    Macro,
};

extern HIRExprPtr LowerHIRExprNode(const ASTExprNode& e);
extern HIRPath LowerHIRPath(const Span& sp, const ASTPath& path, FromASTPathClass pc);
extern HIRGenericPath LowerHIRGenericPath(const Span& sp, const ASTPath& path, FromASTPathClass pc, bool allowAssoc = false);
extern HIRSimplePath LowerHIRSimplePath(const Span& sp, const ASTPath& path, FromASTPathClass pc, bool allowFinalGeneric = false);
extern HIRPathParams LowerHIRPathParams(const Span& sp, const ASTPathParams& srcParams, bool allowAssoc);
extern HIRTypeRef LowerHIRType(const ::TypeRef& ty);
extern HIRPattern LowerHIRPattern(const ASTPattern& pat);

extern RcString gCoreCrate;
extern RcString gCrateName;
extern HIRCrate* gCratePtr;
