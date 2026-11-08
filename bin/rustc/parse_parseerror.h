#pragma once

#include "compile_error.h"
#include "parse_tokenstream.h"

#include <initializer_list>

[[noreturn]] void parseErrorBadChar(const TokenStream& lex, char character);
[[noreturn]] void parseErrorUnexpected(const TokenStream& lex, const Token& tok);
[[noreturn]] void parseErrorUnexpected(const TokenStream& lex, const Token& tok, Token exp);
[[noreturn]] void parseErrorUnexpected(const TokenStream& lex, const Token& tok, std::initializer_list<eTokenType> exp);
