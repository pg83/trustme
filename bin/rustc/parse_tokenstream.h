#pragma once

#include "span.h"
#include "ident.h"
#include "ast_edition.h"
#include "parse_token.h"

#include <vector>
#include <iostream>
#include <algorithm>

class ASTModule;
class ASTCrate;
class ASTAttributeList;
struct WireBoard;

namespace stl {
    class ObjPool;
}

struct ParseState {
public:
    ParseState();

    bool disallowStructLiteral = false;

    bool disallowCallOrIndex = false;

    bool noExpandMacros = false;

    ::std::vector<RcString> erasedLifetimes;

    bool lifetimeIsErased(const RcString& name) const {
        return ::std::find(erasedLifetimes.begin(), erasedLifetimes.end(), name) != erasedLifetimes.end();
    }

    const ASTCrate* crate = nullptr; // TODO: Remove this (needed for MetaItem)
    const WireBoard* wb = nullptr;
    ASTModule* module = nullptr;
    ASTAttributeList* parentAttrs = nullptr;

    ASTModule& getCurrentMod();

    friend ::std::ostream& operator<<(::std::ostream& os, const ParseState& ps);
};

class TokenStream {
    friend class TTLexer;

    bool cacheValid;
    Token cache;
    Ident::Hygiene hygiene_;
    ASTEdition edition;

    struct LookaheadEnt {
        Token tok;
        ASTEdition edition;
        Ident::Hygiene hygiene;
    };

    ::std::vector<LookaheadEnt> lookahead_;
    ParseState parseState_;
    bool macroExpansionPlaceholder_ = false;

public:
    TokenStream(ParseState ps);
    virtual ~TokenStream();
    Token getToken();

    bool getTokenIf(eTokenType exp);

    bool getTokenIf(eTokenType exp, Token& dst);

    Token getTokenCheck(eTokenType exp);
    void putback(Token tok);
    eTokenType lookahead(unsigned int count);

    bool lookaheadIdentIs(unsigned int count, const char* name);

    Ident::Hygiene getHygiene() const;

    virtual void pushHygine();

    virtual void popHygine();

    ParseState& parseState() {
        return parseState_;
    }

    void markMacroExpansionPlaceholder();

    bool isMacroExpansionPlaceholder() const;

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
