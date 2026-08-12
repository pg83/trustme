#pragma once

#include "common.h" // for LList
#include "synext_macro.h"
#include "synext_decorator.h"

struct WireBoard;

extern ASTExprNodeP ExpandParseAndExpandExprVal(const ASTCrate& crate, const ASTModule& mod, TokenStream& lex);
extern void ExpandBareExpr(const WireBoard& wb, const ASTCrate& crate, const ASTModule& mod, ASTExprNodeP& node);
