#pragma once

#include "common.h" // for LList
#include "synext_decorator.h"
#include "synext_macro.h"

extern ::AST::ExprNodeP Expand_ParseAndExpand_ExprVal(const ::AST::Crate& crate, const AST::Module& mod, TokenStream& lex);
extern void Expand_BareExpr(const ::AST::Crate& crate, const AST::Module& mod, AST::ExprNodeP& node);
