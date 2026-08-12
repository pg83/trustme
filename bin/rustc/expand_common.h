#pragma once

//
//
//
#include "ast_attrs.h"
#include "macro_rules_macro_rules.h"

namespace HIR {
    class ProcMacro;
}

    class ASTCrate;
    class ASTModule;
    class ASTPath;
class ExpandProcMacro;
class ExpandDecorator;

TAGGED_UNION_EX(MacroRef, (), None, ((None, struct {}), (MacroRules, const MacroRules*), (BuiltinProcMacro, /*const*/ ExpandProcMacro*), (ExternalProcMacro, const HIR::ProcMacro*)), (), (), (MacroRef clone() const {
                    switch (tag()) {
                        case TAGDEAD:
                            abort();
                        case TAG_None:
                            return make_None({});
                        case TAG_MacroRules:
                            return as_MacroRules();
                        case TAG_BuiltinProcMacro:
                            return as_BuiltinProcMacro();
                        case TAG_ExternalProcMacro:
                            return as_ExternalProcMacro();
                    }
                    abort();
                }));
extern MacroRef ExpandLookupMacro(const Span& miSpan, const ASTCrate& crate, LList<const ASTModule*> modstack, const ASTAttributeName& path);
extern MacroRef ExpandLookupMacro(const Span& miSpan, const ASTCrate& crate, LList<const ASTModule*> modstack, const ASTPath& path);

extern ExpandProcMacro* ExpandFindProcMacro(const RcString& name);
extern ExpandDecorator* ExpandFindDecorator(const RcString& name);
