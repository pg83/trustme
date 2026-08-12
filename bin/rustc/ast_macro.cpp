#include "ast_macro.h"

namespace AST {

MacroInvocation::MacroInvocation() {
}
MacroInvocation::MacroInvocation(Span span, AST::Path macro, RcString ident, TokenTree input)
    : mSpan(mv$(span))
    , macroPath(mv$(macro))
    , ident(mv$(ident))
    , input(mv$(input)) {
}
void MacroInvocation::clear() {
    macroPath = AST::Path();
    ident = "";
    input = TokenTree();
}
}

namespace AST {

::std::ostream& operator<<(::std::ostream& os, const MacroInvocation& x) {
    os << x.macroPath << "! " << x.ident << x.input;
    return os;
}
}
