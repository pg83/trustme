#include "parse_parseerror.h"
#include <iostream>

CompileError::Base::~Base() throw() {
}

CompileError::Generic::Generic(::std::string message)
    : message(message)
{
    ::std::cout << "Generic(" << message << ")" << ::std::endl;
}

CompileError::Generic::Generic(const TokenStream& lex, ::std::string message) {
    ::std::cout << lex.pointSpan() << ": Generic(" << message << ")" << ::std::endl;
}

CompileError::BugCheck::BugCheck(const TokenStream& lex, ::std::string message)
    : message(message)
{
    ::std::cout << lex.pointSpan() << "BugCheck(" << message << ")" << ::std::endl;
}

CompileError::BugCheck::BugCheck(::std::string message)
    : message(message)
{
    ::std::cout << "BugCheck(" << message << ")" << ::std::endl;
}

CompileError::Todo::Todo(::std::string message)
    : message(message)
{
    ::std::cout << "Todo(" << message << ")" << ::std::endl;
}

CompileError::Todo::Todo(const TokenStream& lex, ::std::string message)
    : message(message)
{
    ::std::cout << lex.pointSpan() << ": Todo(" << message << ")" << ::std::endl;
}

CompileError::Todo::~Todo() throw() {
}

ParseError::BadChar::BadChar(const TokenStream& lex, char character) {
    ::std::cout << lex.pointSpan() << ": BadChar(" << character << ")" << ::std::endl;
}

ParseError::BadChar::~BadChar() throw() {
}

ParseError::Unexpected::Unexpected(const TokenStream& lex, const Token& tok) //:
//    m_tok( mv$(tok) )
{
    Span pos = tok.getPos().filename != "" ? lex.subSpan(tok.getPos()) : lex.pointSpan();
    ERROR(pos, E0000, "Unexpected token " << tok);
}

ParseError::Unexpected::Unexpected(const TokenStream& lex, const Token& tok, Token exp) //:
//    m_tok( mv$(tok) )
{
    Span pos = tok.getPos().filename != "" ? lex.subSpan(tok.getPos()) : lex.pointSpan();
    ERROR(pos, E0000, "Unexpected token " << tok << ", expected " << exp);
}

ParseError::Unexpected::Unexpected(const TokenStream& lex, const Token& tok, ::std::vector<eTokenType> exp) {
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

ParseError::Unexpected::~Unexpected() throw() {
}
