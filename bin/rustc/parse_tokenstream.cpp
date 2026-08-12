#include "parse_tokenstream.h"
#include "common.h"
#include "parse_parseerror.h"
#include "ast_crate.h" // Edition lookup

const bool DEBUG_PRINT_TOKENS = false;
//const bool DEBUG_PRINT_TOKENS = true;
//#define DEBUG_PRINT_TOKENS  debug_enabled("Lexer Tokens")
#define FULL_TRACE

TokenStream::TokenStream(ParseState ps)
    : cacheValid(false)
    , parseState(ps)
{
}

TokenStream::~TokenStream() {
}

Token TokenStream::innerGetToken() {
    Token ret = this->realGetToken();
    if (ret != TOK_EOF && ret.getPos().filename == "") {
        ret.set_pos(this->getPosition());
    }
    //DEBUG("ret.get_pos() = " << ret.get_pos());
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
        throw ParseError::Unexpected(*this, tok, Token(exp));
    }
    return tok;
}

void TokenStream::putback(Token tok) {
    if (cacheValid) {
        DEBUG("" << getPosition() << " - Double putback: " << tok << " but " << cache);
        throw ParseError::BugCheck("Double putback");
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
        throw ParseError::BugCheck("Excessive lookahead");
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

ProtoSpan TokenStream::start_span() const {
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
    return Span(this->outerSpan(), ::std::move(ps.filename), ps.start_line, ps.start_ofs, p.line, p.ofs);
}

Span TokenStream::point_span() const {
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

ParseState::ParseState() {
}
::AST::Module& ParseState::getCurrentMod() {
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
    , state(state) {
}
SavedParseState::~SavedParseState() {
    DEBUG("Restoring " << state);
    lex.parse_state() = state;
}

::std::ostream& operator<<(::std::ostream& os, const ParseState& ps) {
    os << "ParseState {";
    if (ps.disallowStructLiteral) {
        os << " disallow_struct_literal";
    }
    if (ps.no_expand_macros) {
        os << " no_expand_macros";
    }
    os << " }";
    return os;
}
