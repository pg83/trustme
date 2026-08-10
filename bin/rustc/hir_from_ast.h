/*
 * MRustC - Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * hir/from_ast.h
 * - Shared code definitions for constructing the HIR from AST
 */
#pragma once

#include "hir_expr_ptr.h"

class RcString;
class TypeRef;
struct Span;

namespace AST {
    class ExprNode;
    class Path;
    struct PathParams;
    class Pattern;
}

namespace HIR {
    class Crate;
    class GenericPath;
    class Path;
    struct PathParams;
    struct Pattern;
    struct SimplePath;
}

enum class FromAST_PathClass {
    Type,
    Value,
    Macro,
};

extern ::HIR::ExprPtr LowerHIR_ExprNode(const ::AST::ExprNode& e);
extern ::HIR::Path LowerHIR_Path(const Span& sp, const ::AST::Path& path, FromAST_PathClass pc);
extern ::HIR::GenericPath LowerHIR_GenericPath(const Span& sp, const ::AST::Path& path, FromAST_PathClass pc, bool allow_assoc = false);
extern ::HIR::SimplePath LowerHIR_SimplePath(const Span& sp, const ::AST::Path& path, FromAST_PathClass pc, bool allow_final_generic = false);
extern ::HIR::PathParams LowerHIR_PathParams(const Span& sp, const ::AST::PathParams& src_params, bool allow_assoc);
extern ::HIR::TypeRef LowerHIR_Type(const ::TypeRef& ty);
extern ::HIR::Pattern LowerHIR_Pattern(const ::AST::Pattern& pat);

extern RcString g_core_crate;
extern RcString g_crate_name;
extern ::HIR::Crate* g_crate_ptr;
