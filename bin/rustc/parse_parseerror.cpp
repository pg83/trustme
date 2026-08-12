#include "parse_parseerror.h"
#include <iostream>

CompileErrorBase::~CompileErrorBase() throw() {
}

CompileErrorGeneric::CompileErrorGeneric(::std::string message)
    : message(message)
{
    ::std::cout << "Generic(" << message << ")" << ::std::endl;
}

CompileErrorGeneric::CompileErrorGeneric(const TokenStream& lex, ::std::string message) {
    ::std::cout << lex.pointSpan() << ": Generic(" << message << ")" << ::std::endl;
}

CompileErrorBugCheck::CompileErrorBugCheck(const TokenStream& lex, ::std::string message)
    : message(message)
{
    ::std::cout << lex.pointSpan() << "BugCheck(" << message << ")" << ::std::endl;
}

CompileErrorBugCheck::CompileErrorBugCheck(::std::string message)
    : message(message)
{
    ::std::cout << "BugCheck(" << message << ")" << ::std::endl;
}

CompileErrorTodo::CompileErrorTodo(::std::string message)
    : message(message)
{
    ::std::cout << "Todo(" << message << ")" << ::std::endl;
}

CompileErrorTodo::CompileErrorTodo(const TokenStream& lex, ::std::string message)
    : message(message)
{
    ::std::cout << lex.pointSpan() << ": Todo(" << message << ")" << ::std::endl;
}

CompileErrorTodo::~CompileErrorTodo() throw() {
}

ParseErrorBadChar::ParseErrorBadChar(const TokenStream& lex, char character) {
    ::std::cout << lex.pointSpan() << ": BadChar(" << character << ")" << ::std::endl;
}

ParseErrorBadChar::~ParseErrorBadChar() throw() {
}

ParseErrorUnexpected::ParseErrorUnexpected(const TokenStream& lex, const Token& tok) //:
//    m_tok( mv$(tok) )
{
    Span pos = tok.getPos().filename != "" ? lex.subSpan(tok.getPos()) : lex.pointSpan();
    ERROR(pos, E0000, "Unexpected token " << tok);
}

ParseErrorUnexpected::ParseErrorUnexpected(const TokenStream& lex, const Token& tok, Token exp) //:
//    m_tok( mv$(tok) )
{
    Span pos = tok.getPos().filename != "" ? lex.subSpan(tok.getPos()) : lex.pointSpan();
    ERROR(pos, E0000, "Unexpected token " << tok << ", expected " << exp);
}

ParseErrorUnexpected::ParseErrorUnexpected(const TokenStream& lex, const Token& tok, ::std::vector<eTokenType> exp) {
    Span pos = tok.getPos().filename != "" ? lex.subSpan(tok.getPos()) : lex.pointSpan();
    ERROR(pos, E0000, "Unexpected token " << tok << ", expected one of " << FMT_CB(os, {
                          bool f = true;
                          for (auto v : exp) {
                              if (!f) {
                                  os << " or ";
                              }
                              f = false;
                              os << Token::typestr(v);
                          }
                      }));
}

ParseErrorUnexpected::~ParseErrorUnexpected() throw() {
}
