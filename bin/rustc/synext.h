#pragma once

#include "common.h" // for LList
#include "synext_decorator.h"
#include "synext_macro.h"

extern ::AST::ExprNodeP ExpandParseAndExpandExprVal(const ::AST::Crate& crate, const AST::Module& mod, TokenStream& lex);
extern void ExpandBareExpr(const ::AST::Crate& crate, const AST::Module& mod, AST::ExprNodeP& node);
