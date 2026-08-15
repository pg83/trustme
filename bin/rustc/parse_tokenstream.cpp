#include "parse_tokenstream.h"

#include "common.h"
#include "ast_crate.h" // Edition lookup
#include "wire_board.h" // typePool() reads the pool off the board
#include "parse_parseerror.h"

const bool DEBUG_PRINT_TOKENS = false;
//#define DEBUG_PRINT_TOKENS  debug_enabled("Lexer Tokens")
#define FULL_TRACE

TokenStream::TokenStream(ParseState ps)
    : cacheValid(false)
    , mParseState(ps)
{
}

TokenStream::~TokenStream() {
}

void TokenStream::markMacroExpansionPlaceholder() {
    mMacroExpansionPlaceholder = true;
}

bool TokenStream::isMacroExpansionPlaceholder() const {
    return mMacroExpansionPlaceholder;
}

stl::ObjPool& TokenStream::typePool() const {
    return *mParseState.wb->pool;
}

Token TokenStream::innerGetToken() {
    Token ret = this->realGetToken();
    if (ret != TOK_EOF && ret.getPos().filename == "" && !ret.getPos().span) {
        ret.setPos(this->getPosition());
    }
    return ret;
}

Token TokenStream::getToken() {
    if (cacheValid) {
#ifdef FULL_TRACE
        DEBUG("<= " << cache << " (cache)");
#endif
        cacheValid = false;
        return mv$(cache);
    } else if (mLookahead.size()) {
        Token ret = mv$(mLookahead.front().tok);
        edition = mLookahead.front().edition;
        mHygiene = mLookahead.front().hygiene;
        mLookahead.erase(mLookahead.begin());
#ifdef FULL_TRACE
        DEBUG("<= " << ret << " (lookahead)");
#endif
        if (DEBUG_PRINT_TOKENS) {
            ::std::cout << "getToken[" << typeid(*this).name() << "] - " << ret.getPos() << "-" << ret << ::std::endl;
        }
        return ret;
    } else {
        Token ret = this->innerGetToken();
        edition = this->realGetEdition();
        mHygiene = this->realGetHygiene();
#ifdef FULL_TRACE
        DEBUG("<= " << ret << " (new)");
#endif
        if (DEBUG_PRINT_TOKENS) {
            ::std::cout << "getToken[" << typeid(*this).name() << "] - " << ret.getPos() << "-" << ret << ::std::endl;
        }
        return ret;
    }
}

Token TokenStream::getTokenCheck(eTokenType exp) {
    auto tok = getToken();
    if (tok.type() != exp) {
        throw ParseErrorUnexpected(*this, tok, Token(exp));
    }
    return tok;
}

void TokenStream::putback(Token tok) {
    if (cacheValid) {
        DEBUG("" << getPosition() << " - Double putback: " << tok << " but " << cache);
        throw CompileErrorBugCheck("Double putback");
    } else {
#ifdef FULL_TRACE
        DEBUG(">>> " << tok);
#endif
        cacheValid = true;
        cache = mv$(tok);
    }
}

eTokenType TokenStream::lookahead(unsigned int i) {
    const unsigned int MAX_LOOKAHEAD = 4;

    if (cacheValid) {
        if (i == 0) {
            return cache.type();
        }
        i--;
    }

    if (i >= MAX_LOOKAHEAD) {
        throw CompileErrorBugCheck("Excessive lookahead");
    }

    while (i >= mLookahead.size()) {
        DEBUG("lookahead - read #" << mLookahead.size());
        auto tok = this->innerGetToken();
        auto hygiene = this->realGetHygiene();
        mLookahead.push_back({mv$(tok), this->realGetEdition(), mv$(hygiene)});
    }

    DEBUG("lookahead(" << i << ") = " << mLookahead[i].tok);
    return mLookahead[i].tok.type();
}

Ident::Hygiene TokenStream::getHygiene() const {
    return mHygiene;
}

ProtoSpan TokenStream::startSpan() const {
    auto p = this->getPosition();
    return ProtoSpan{p.span, p.filename, p.line, p.ofs};
}

Span TokenStream::endSpan(ProtoSpan ps) const {
    auto p = this->getPosition();
    if (ps.span && p.span) {
        if (ps.span == p.span) {
            return ps.span;
        }
    }
    if (ps.filename == "") {
        assert(this->outerSpan());
        return this->outerSpan();
    }
    return Span(this->outerSpan(), ::std::move(ps.filename), ps.startLine, ps.startOfs, p.line, p.ofs);
}

Span TokenStream::pointSpan() const {
    auto p = this->getPosition();
    if (p.span) {
        return p.span;
    }
    if (p.filename == "") {
        assert(this->outerSpan());
        return this->outerSpan();
    }
    return Span(this->outerSpan(), p);
}

Span TokenStream::tokenStartSpan(const Token& tok) const {
    const auto& pos = tok.getPos();
    if (pos.span) {
        return pos.span;
    }
    if (pos.filename == "") {
        return this->pointSpan();
    }

    return this->subSpan(pos);
}

ParseState::ParseState() {
}

ASTModule& ParseState::getCurrentMod() {
    assert(this->module);
    return *this->module;
}

/// <summary>Consumes a token if it is of the specified type</summary>
bool TokenStream::getTokenIf(eTokenType exp) { // I'd like std::optional, but not available
    if (lookahead(0) == exp) {
        getToken();
        return true;
    } else {
        return false;
    }
}

/// <summary>Consumes a token if it is of the specified type</summary>
bool TokenStream::getTokenIf(eTokenType exp, Token& dst) { // I'd like std::optional, but not available
    if (lookahead(0) == exp) {
        dst = getToken();
        return true;
    } else {
        return false;
    }
}

SavedParseState::SavedParseState(TokenStream& lex, ParseState state)
    : lex(lex)
    , state(state)
{
}

SavedParseState::~SavedParseState() {
    DEBUG("Restoring " << state);
    lex.parseState() = state;
}

::std::ostream& operator<<(::std::ostream& os, const ParseState& ps) {
    os << "ParseState {";
    if (ps.disallowStructLiteral) {
        os << " disallow_struct_literal";
    }
    if (ps.noExpandMacros) {
        os << " no_expand_macros";
    }
    os << " }";
    return os;
}

void TokenStream::pushHygine() {
}

void TokenStream::popHygine() {
}

Span TokenStream::outerSpan() const {
    return Span();
}
