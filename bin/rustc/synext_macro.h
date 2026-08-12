#pragma once

//#include "../common.h"   // for mv$ and other things
#include <string>
#include <memory>
#include "span.h"

class TypeRef;

class ASTCrate;
class ASTModule;
class TokenTree;
class TokenStream;
struct WireBoard;

class ExpandProcMacro {
public:
    virtual ~ExpandProcMacro() = default;
    virtual ::std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) = 0;

    virtual ::std::unique_ptr<TokenStream> expandIdent(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const RcString& ident, const TokenTree& tt, ASTModule& mod);
};

struct MacroDef;
extern void RegisterSynextMacro(::std::string name, ::std::unique_ptr<ExpandProcMacro> handler);
extern void RegisterSynextMacroStatic(MacroDef* def);

struct MacroDef {
    MacroDef* prev;
    ::std::string name;
    ::std::unique_ptr<ExpandProcMacro> def;

    MacroDef(::std::string name, ::std::unique_ptr<ExpandProcMacro> def);
};

#define STATIC_MACRO(ident, _handler_class) static MacroDef s_register_##_handler_class(ident, ::std::unique_ptr<ExpandProcMacro>(new _handler_class()));
