#include "ast_macro.h"

#include "output.h"

using namespace stl;

ASTMacroInvocation::ASTMacroInvocation() {
}

ASTMacroInvocation::ASTMacroInvocation(Span span, ASTPath macro, RcString ident, TokenTree input)
    : span_(mv$(span))
    , macroPath(mv$(macro))
    , ident(mv$(ident))
    , input(mv$(input))
{
}

void ASTMacroInvocation::clear() {
    macroPath = ASTPath();
    ident = "";
    input = TokenTree();
}

ASTMacroInvocation ASTMacroInvocation::clone() const {
    return ASTMacroInvocation(span_, ASTPath(macroPath), ident, input.clone());
}

namespace stl {
    template <>
    void output<ZeroCopyOutput, ASTMacroInvocation>(ZeroCopyOutput& os, const ASTMacroInvocation& x) {
        os << x.path() << StringView("! ") << x.inputIdent() << x.inputTt();
    }
}
