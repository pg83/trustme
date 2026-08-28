#pragma once

#include "compile_error.h"
#include "parse_tokenstream.h"

[[noreturn]] void parseErrorBadChar(const TokenStream& lex, char character);
[[noreturn]] void parseErrorUnexpected(const TokenStream& lex, const Token& tok);
[[noreturn]] void parseErrorUnexpected(const TokenStream& lex, const Token& tok, Token exp);
[[noreturn]] void parseErrorUnexpected(const TokenStream& lex, const Token& tok, std::vector<eTokenType> exp);

#define ASSERT(lex, cnd)                                                         \
    do {                                                                         \
        if (!(cnd))                                                              \
            compileErrorBugCheck(lex, "Assertion failed: " __FILE__ " - " #cnd); \
    } while (0)
