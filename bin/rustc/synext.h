#pragma once

#include "common.h"
#include "synext_macro.h"
#include "synext_decorator.h"

struct WireBoard;

void ExpandExportMacroRules(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, ASTModule& mod, const RcString& name);
