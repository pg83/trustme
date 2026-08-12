#pragma once

#include <iostream>
#include <vector>
#include "span.h"
#include "debug.h"
#include "ident.h"
#include "parse_token.h"
#include "ast_edition.h"

namespace AST {
    class Module;
    class Crate;
    class AttributeList;
}

/// State the parser needs to pass down via a second channel.
struct ParseState {
public:
    ParseState();

    // Used for "for/if/while" to handle ambiguity
    bool disallowStructLiteral = false;
    // Used for match arms to disallow `foo => if false {} (bar) => ...`
    bool disallowCallOrIndex = false;
    // A debugging hook that disables expansion of macros
    bool noExpandMacros = false;

    const ::AST::Crate* crate = nullptr; // TODO: Remove this (needed for MetaItem)
    ::AST::Module* module = nullptr;
    ::AST::AttributeList* parentAttrs = nullptr;

    ::AST::Module& getCurrentMod();

    friend ::std::ostream& operator<<(::std::ostream& os, const ParseState& ps);
};

class TokenStream {
    friend class TTLexer; // needs access to internals to know what was consumed

    bool cacheValid;
    Token cache;
    Ident::Hygiene mHygiene;
    AST::Edition edition;

    struct LookaheadEnt {
        Token tok;
        AST::Edition edition;
        Ident::Hygiene hygiene;
    };

    ::std::vector<LookaheadEnt> mLookahead;
    ParseState parseState;

public:
    TokenStream(ParseState ps);
    virtual ~TokenStream();
    Token getToken();

    /// <summary>Consumes a token if it is of the specified type</summary>
    bool getTokenIf(eTokenType exp);

    /// <summary>Consumes a token if it is of the specified type</summary>
    bool getTokenIf(eTokenType exp, Token& dst);

    /// <summary>Obtains a token, asserting that it's of the specified type</summary>
    Token getTokenCheck(eTokenType exp);
    void putback(Token tok);
    eTokenType lookahead(unsigned int count);

    Ident::Hygiene getHygiene() const;

    virtual void pushHygine() {
    }

    virtual void popHygine() {
    }

    ParseState& parse_state() {
        return parseState;
    }

    AST::Edition getEdition() const {
        return edition;
    }

    bool editionAfter(AST::Edition e) const {
        return edition >= e;
    }

    bool editionBefore(AST::Edition e) const {
        return edition < e;
    }

    ProtoSpan start_span() const;
    Span endSpan(ProtoSpan ps) const;
    Span pointSpan() const;

    Span sub_span(const Position& p) const {
        return Span(outerSpan(), p);
    }

protected:
    virtual Position getPosition() const = 0;

    virtual Span outerSpan() const {
        return Span();
    }

    virtual Token realGetToken() = 0;
    virtual AST::Edition realGetEdition() const = 0;
    virtual Ident::Hygiene realGetHygiene() const = 0;

private:
    Token innerGetToken();
};

class SavedParseState {
    TokenStream& lex;
    ParseState state;

public:
    SavedParseState(TokenStream& lex, ParseState state);

    ~SavedParseState();
};

#define SET_MODULE(lex, mod)                      \
    SavedParseState _sps(lex, lex.parse_state()); \
    lex.parse_state().module = &(mod)
#define SET_ATTRS(lex, attrs)                     \
    SavedParseState _sps(lex, lex.parse_state()); \
    lex.parse_state().parentAttrs = &(attrs)
#define SET_PARSE_FLAG(lex, flag)                 \
    SavedParseState _sps(lex, lex.parse_state()); \
    lex.parse_state().flag = true
#define CLEAR_PARSE_FLAG(lex, flag)               \
    SavedParseState _sps(lex, lex.parse_state()); \
    lex.parse_state().flag = false
#define CLEAR_PARSE_FLAGS_EXPR(lex)                    \
    SavedParseState _sps(lex, lex.parse_state());      \
    lex.parse_state().disallowStructLiteral = false; \
    lex.parse_state().disallowCallOrIndex = false
#define CHECK_PARSE_FLAG(lex, flag) (lex.parse_state().flag == true)
