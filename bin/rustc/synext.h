#pragma once

#include "common.h"
#include "synext_macro.h"
#include "synext_decorator.h"

struct WireBoard;

ASTExprNodeP ExpandParseAndExpandExprVal(const ASTCrate& crate, const ASTModule& mod, TokenStream& lex);
void ExpandBareExpr(const WireBoard& wb, const ASTCrate& crate, const ASTModule& mod, ASTExprNodeP& node);
void ExpandExportMacroRules(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, ASTModule& mod, const RcString& name);
