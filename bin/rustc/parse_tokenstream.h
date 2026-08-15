#pragma once

#include "span.h"
#include "debug.h"
#include "ident.h"
#include "ast_edition.h"
#include "parse_token.h"

#include <vector>
#include <iostream>

class ASTModule;
class ASTCrate;
class ASTAttributeList;
struct WireBoard;
namespace stl {
    class ObjPool;
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

    const ASTCrate* crate = nullptr; // TODO: Remove this (needed for MetaItem)
    const WireBoard* wb = nullptr;   // cfg!() evaluation and expansion read components through the board
    ASTModule* module = nullptr;
    ASTAttributeList* parentAttrs = nullptr;

    ASTModule& getCurrentMod();

    friend ::std::ostream& operator<<(::std::ostream& os, const ParseState& ps);
};

class TokenStream {
    friend class TTLexer; // needs access to internals to know what was consumed

    bool cacheValid;
    Token cache;
    Ident::Hygiene mHygiene;
    ASTEdition edition;

    struct LookaheadEnt {
        Token tok;
        ASTEdition edition;
        Ident::Hygiene hygiene;
    };

    ::std::vector<LookaheadEnt> mLookahead;
    ParseState mParseState;
    bool mMacroExpansionPlaceholder = false;

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

    virtual void pushHygine();

    virtual void popHygine();

    ParseState& parseState() {
        return mParseState;
    }

    void markMacroExpansionPlaceholder();

    bool isMacroExpansionPlaceholder() const;

    // The pool that owns AST type nodes created while parsing this stream.
    stl::ObjPool& typePool() const;

    ASTEdition getEdition() const {
        return edition;
    }

    bool editionAfter(ASTEdition e) const {
        return edition >= e;
    }

    bool editionBefore(ASTEdition e) const {
        return edition < e;
    }

    ProtoSpan startSpan() const;
    Span endSpan(ProtoSpan ps) const;
    Span pointSpan() const;
    Span tokenStartSpan(const Token& tok) const;

    Span subSpan(const Position& p) const {
        return Span(outerSpan(), p);
    }

protected:
    virtual Position getPosition() const = 0;

    virtual Span outerSpan() const;

    virtual Token realGetToken() = 0;
    virtual ASTEdition realGetEdition() const = 0;
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

#define SET_MODULE(lex, mod)                     \
    SavedParseState _sps(lex, lex.parseState()); \
    lex.parseState().module = &(mod)
#define SET_ATTRS(lex, attrs)                    \
    SavedParseState _sps(lex, lex.parseState()); \
    lex.parseState().parentAttrs = &(attrs)
#define SET_PARSE_FLAG(lex, flag)                \
    SavedParseState _sps(lex, lex.parseState()); \
    lex.parseState().flag = true
#define CLEAR_PARSE_FLAG(lex, flag)              \
    SavedParseState _sps(lex, lex.parseState()); \
    lex.parseState().flag = false
#define CLEAR_PARSE_FLAGS_EXPR(lex)                 \
    SavedParseState _sps(lex, lex.parseState());    \
    lex.parseState().disallowStructLiteral = false; \
    lex.parseState().disallowCallOrIndex = false
#define CHECK_PARSE_FLAG(lex, flag) (lex.parseState().flag == true)
