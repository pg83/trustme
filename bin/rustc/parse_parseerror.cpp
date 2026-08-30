#include "parse_parseerror.h"

#include <initializer_list>

using namespace stl;

void parseErrorBadChar(const TokenStream& lex, char character) {
    ERROR(lex.pointSpan(), E0000, StringView("Bad character `") << character << StringView("`"));
}

void parseErrorUnexpected(const TokenStream& lex, const Token& tok) {
    Span pos = tok.getPos().filename != "" ? lex.subSpan(tok.getPos()) : lex.pointSpan();
    ERROR(pos, E0000, StringView("Unexpected token ") << tok);
}

void parseErrorUnexpected(const TokenStream& lex, const Token& tok, Token exp) {
    Span pos = tok.getPos().filename != "" ? lex.subSpan(tok.getPos()) : lex.pointSpan();
    ERROR(pos, E0000, StringView("Unexpected token ") << tok << StringView(", expected ") << exp);
}

void parseErrorUnexpected(const TokenStream& lex, const Token& tok, std::initializer_list<eTokenType> exp) {
    Span pos = tok.getPos().filename != "" ? lex.subSpan(tok.getPos()) : lex.pointSpan();
    ERROR(pos, E0000, StringView("Unexpected token ") << tok << StringView(", expected one of ") << FMT_CB(os, {
                          bool f = true;
                          for (auto v : exp) {
                              if (!f) {
                                  os << StringView(" or ");
                              }
                              f = false;
                              os << Token::typestr(v);
                          }
                      }));
}
