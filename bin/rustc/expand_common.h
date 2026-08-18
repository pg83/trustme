#pragma once

#include "ast_attrs.h"
#include "macro_rules_macro_rules.h"

class HIRProcMacro;

class ASTCrate;
struct WireBoard;
class ASTModule;
class ASTPath;
class ExpandProcMacro;
class ExpandDecorator;

// Definitions generated from expand_common.tu.
#include "expand_common_tu.h"
extern MacroRef ExpandLookupMacro(const Span& miSpan, const WireBoard& wb, const ASTCrate& crate, LList<const ASTModule*> modstack, const ASTAttributeName& path);
extern MacroRef ExpandLookupMacro(const Span& miSpan, const WireBoard& wb, const ASTCrate& crate, LList<const ASTModule*> modstack, const ASTPath& path);

extern ExpandProcMacro* ExpandFindProcMacro(const RcString& name);
extern ExpandDecorator* ExpandFindDecorator(const RcString& name);
