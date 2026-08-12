#include "ast_macro.h"

namespace AST {

MacroInvocation::MacroInvocation() {
}
MacroInvocation::MacroInvocation(Span span, AST::Path macro, RcString ident, TokenTree input)
    : m_span(mv$(span))
    , m_macro_path(mv$(macro))
    , m_ident(mv$(ident))
    , m_input(mv$(input)) {
}
void MacroInvocation::clear() {
    m_macro_path = AST::Path();
    m_ident = "";
    m_input = TokenTree();
}
}
