#pragma once

#include <stdexcept>
#include "parse_tokenstream.h"
#include "compile_error.h"



    class ParseErrorBadChar: public CompileErrorBase {
        //char    m_char;
    public:
        ParseErrorBadChar(const TokenStream& lex, char character);
        virtual ~ParseErrorBadChar() throw();
    };

    class ParseErrorUnexpected: public CompileErrorBase {
        Token mTok;

    public:
        ParseErrorUnexpected(const TokenStream& lex, const Token& tok);
        ParseErrorUnexpected(const TokenStream& lex, const Token& tok, Token exp);
        ParseErrorUnexpected(const TokenStream& lex, const Token& tok, ::std::vector<eTokenType> exp);
        virtual ~ParseErrorUnexpected() throw();
    };

#define ASSERT(lex, cnd)                                                                 \
    do {                                                                                 \
        if (!(cnd))                                                                      \
            throw CompileErrorBugCheck(lex, "Assertion failed: " __FILE__ " - " #cnd); \
    } while (0)

