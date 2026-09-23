#include "parse_tokenstream.h"

#include "common.h"
#include "output.h"
#include "ast_crate.h"
#include "wire_board.h"
#include "parse_parseerror.h"

using namespace stl;

TokenStream::TokenStream(ParseState ps)
    : cacheValid(false)
    , parseState_(ps)
{
}

TokenStream::~TokenStream() {
}

void TokenStream::markMacroExpansionPlaceholder() {
    macroExpansionPlaceholder_ = true;
}

bool TokenStream::isMacroExpansionPlaceholder() const {
    return macroExpansionPlaceholder_;
}

ObjPool& TokenStream::typePool() const {
    return *parseState_.wb->pool;
}

Token TokenStream::innerGetToken() {
    Token ret = this->realGetToken();
    if (ret != TOK_EOF && ret.getPos().filename == "" && !ret.getPos().span) {
        ret.setPos(this->getPosition());
    }
    if (sourceRecord_ && ret != TOK_EOF) {
        auto recorded = ret.fragmentTokens() ? Token::tokensOf(ret) : ret.clone();
        sourceRecord_->pushBack(typePool().make<RecordedToken>(mv$(recorded), this->realGetEdition(), this->realGetHygiene()));
    }
    return ret;
}

void TokenStream::startSourceRecording(Vector<RecordedToken*>* out) {
    BUG_ASSERT(sourceRecord_ == nullptr);
    if (cacheValid && cache != TOK_EOF) {
        auto recorded = cache.fragmentTokens() ? Token::tokensOf(cache) : cache.clone();
        out->pushBack(typePool().make<RecordedToken>(mv$(recorded), edition, hygiene_));
    }
    for (unsigned i = 0; i < lookaheadCount_; i++) {
        const auto& ent = lookaheadAt(i);
        if (ent.tok != TOK_EOF) {
            auto recorded = ent.tok.fragmentTokens() ? Token::tokensOf(ent.tok) : ent.tok.clone();
            out->pushBack(typePool().make<RecordedToken>(mv$(recorded), ent.edition, ent.hygiene));
        }
    }
    sourceRecord_ = out;
}

void TokenStream::stopSourceRecording() {
    unsigned pending = cacheValid && cache != TOK_EOF ? 1 : 0;
    for (unsigned i = 0; i < lookaheadCount_; i++) {
        if (lookaheadAt(i).tok != TOK_EOF) {
            pending++;
        }
    }
    while (pending > 0 && !sourceRecord_->empty()) {
        sourceRecord_->popBack();
        pending--;
    }
    sourceRecord_ = nullptr;
}

Token TokenStream::getToken() {
    Token ret = takeToken();
    if (record_) {
        record_->pushBack(typePool().make<RecordedToken>(ret.clone(), edition, hygiene_));
    }
    return ret;
}

Token TokenStream::takeToken() {
    if (cacheValid) {
        cacheValid = false;
        return mv$(cache);
    } else if (lookaheadCount_) {
        auto& front = lookaheadAt(0);
        Token ret = mv$(front.tok);
        edition = front.edition;
        hygiene_ = front.hygiene;
        lookaheadHead_ = (lookaheadHead_ + 1) % MAX_LOOKAHEAD;
        lookaheadCount_--;
        return ret;
    } else {
        Token ret = this->innerGetToken();
        edition = this->realGetEdition();
        hygiene_ = this->realGetHygiene();
        return ret;
    }
}

Token TokenStream::getTokenCheck(eTokenType exp) {
    auto tok = getToken();
    if (tok.type() != exp) {
        parseErrorUnexpected(*this, tok, Token(exp));
    }
    return tok;
}

void TokenStream::putback(Token tok) {
    if (cacheValid) {
        DEBUG(StringView("") << getPosition() << StringView(" - Double putback: ") << tok << StringView(" but ") << cache);
        compileErrorBugCheck("Double putback");
    } else {
        cacheValid = true;
        cache = mv$(tok);
        if (record_ && !record_->empty()) {
            record_->popBack();
        }
    }
}

eTokenType TokenStream::lookahead(unsigned int i) {
    if (cacheValid) {
        if (i == 0) {
            return cache.type();
        }
        i--;
    }

    if (i >= MAX_LOOKAHEAD) {
        compileErrorBugCheck("Excessive lookahead");
    }

    while (i >= lookaheadCount_) {
        DEBUG(StringView("lookahead - read #") << lookaheadCount_);
        auto tok = this->innerGetToken();
        auto hygiene = this->realGetHygiene();
        auto& slot = lookaheadAt(lookaheadCount_);
        slot.tok = mv$(tok);
        slot.edition = this->realGetEdition();
        slot.hygiene = mv$(hygiene);
        lookaheadCount_++;
    }

    DEBUG(StringView("lookahead(") << i << StringView(") = ") << lookaheadAt(i).tok);
    return lookaheadAt(i).tok.type();
}

bool TokenStream::lookaheadIdentIs(unsigned int i, const char* name) {
    if (this->lookahead(i) != TOK_IDENT) {
        return false;
    }
    if (cacheValid) {
        if (i == 0) {
            return cache.ident().name == name;
        }
        i--;
    }
    return lookaheadAt(i).tok.ident().name == name;
}

Ident::Hygiene TokenStream::getHygiene() const {
    return hygiene_;
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
        BUG_ASSERT(this->outerSpan());
        return this->outerSpan();
    }
    return Span(this->outerSpan(), std::move(ps.filename), ps.startLine, ps.startOfs, p.line, p.ofs);
}

Span TokenStream::pointSpan() const {
    auto p = this->getPosition();
    if (p.span) {
        return p.span;
    }
    if (p.filename == "") {
        BUG_ASSERT(this->outerSpan());
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
    BUG_ASSERT(this->module);
    return *this->module;
}

bool TokenStream::getTokenIf(eTokenType exp) {
    if (lookahead(0) == exp) {
        getToken();
        return true;
    } else {
        return false;
    }
}

bool TokenStream::getTokenIf(eTokenType exp, Token& dst) {
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
    DEBUG(StringView("Restoring ") << state);
    lex.parseState() = state;
}

void TokenStream::pushHygine() {
}

void TokenStream::popHygine() {
}

Span TokenStream::outerSpan() const {
    return Span();
}

template <>
void stl::output<ZeroCopyOutput, ParseState>(ZeroCopyOutput& os, const ParseState& ps) {
    os << StringView("ParseState {");
    if (ps.disallowStructLiteral) {
        os << StringView(" disallow_struct_literal");
    }
    if (ps.noExpandMacros) {
        os << StringView(" no_expand_macros");
    }
    os << StringView(" }");
    return;
}
