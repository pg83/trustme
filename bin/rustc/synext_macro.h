#pragma once

    //#include "../common.h"   // for mv$ and other things
#include <string>
#include <memory>
#include "span.h"

class TypeRef;

namespace AST {
    class Crate;
    class Module;
}
class TokenTree;
class TokenStream;

class ExpandProcMacro {
public:
    virtual ~ExpandProcMacro() = default;
    virtual ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) = 0;

    virtual ::std::unique_ptr<TokenStream> expand_ident(const Span& sp, const AST::Crate& crate, const RcString& ident, const TokenTree& tt, AST::Module& mod) {
        ERROR(sp, E0000, "macro doesn't take an identifier");
    }
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
