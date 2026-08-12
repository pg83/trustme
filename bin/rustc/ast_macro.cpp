#include "ast_macro.h"

ASTMacroInvocation::ASTMacroInvocation() {
}

ASTMacroInvocation::ASTMacroInvocation(Span span, ASTPath macro, RcString ident, TokenTree input)
    : mSpan(mv$(span))
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

::std::ostream& operator<<(::std::ostream& os, const ASTMacroInvocation& x) {
    os << x.macroPath << "! " << x.ident << x.input;
    return os;
}
