#pragma once

#include "span.h"

#include <memory>
#include <string>

struct ASTType;

class ASTCrate;
class ASTModule;
class TokenTree;
class TokenStream;
struct WireBoard;
class ExpandRegistry;

class ExpandProcMacro {
public:
    virtual ~ExpandProcMacro() = default;
    virtual ::std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) = 0;

    virtual ::std::unique_ptr<TokenStream> expandIdent(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const RcString& ident, const TokenTree& tt, ASTModule& mod);
};

void RegisterBuiltinMacros(ExpandRegistry& registry);
