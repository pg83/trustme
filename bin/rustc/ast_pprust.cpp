#include "ast_pprust.h"

#include "common.h"
#include "ast_expr.h"
#include "ast_path.h"
#include "ast_types.h"
#include "floats.h"
#include "coretypes.h"
#include "ast_pattern.h"
#include "ast_generics.h"
#include "parse_token.h"
#include "parse_tokentree.h"

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    constexpr i64 SIZE_INFINITY = 0xffff;
    constexpr i64 MARGIN = 78;
    constexpr i64 MIN_SPACE = 60;
    constexpr i64 INDENT_UNIT = 4;

    enum class Breaks : u8 {
        Consistent,
        Inconsistent,
    };

    enum class PpTokenKind : u8 {
        String,
        Break,
        Begin,
        End,
    };

    struct PpToken {
        PpTokenKind kind = PpTokenKind::End;
        StringView string;
        i64 offset = 0;
        i64 blankSpace = 0;
        char preBreak = 0;
        bool visual = false;
        Breaks breaks = Breaks::Inconsistent;

        bool isHardbreak() const;
    };

    struct BufEntry {
        PpToken token;
        i64 size;
    };

    struct PrintFrame {
        bool fits;
        i64 indent;
        Breaks breaks;
    };

    struct Printer {
        explicit Printer(ZeroCopyOutput& out);

        void rbox(i64 indent, Breaks breaks);
        void ibox(i64 indent);
        void cbox(i64 indent);
        void visualAlign();
        void breakOffset(i64 n, i64 off);
        void end();
        void eof();
        void word(StringView w);
        void spaces(i64 n);
        void zerobreak();
        void space();
        void hardbreak();
        bool isBeginningOfLine() const;
        void trailingComma();
        void trailingCommaOrSpace();
        void offset(i64 off);

        void wordSpace(StringView w);
        void wordNbsp(StringView w);
        void nbsp();
        void popen();
        void pclose();
        void hardbreakIfNotBol();
        void spaceIfNotBol();
        void breakOffsetIfNotBol(i64 n, i64 off);

    private:
        ObjPool::Ref pool_;
        ZeroCopyOutput& out_;
        i64 space_ = MARGIN;
        Vector<BufEntry> buf_;
        size_t bufFirst_ = 0;
        size_t bufBase_ = 0;
        i64 leftTotal_ = 0;
        i64 rightTotal_ = 0;
        Vector<size_t> scanStack_;
        size_t scanFirst_ = 0;
        Vector<PrintFrame> printStack_;
        i64 indent_ = 0;
        i64 pendingIndentation_ = 0;
        bool hasLastPrinted_ = false;
        PpToken lastPrinted_;

        bool bufEmpty() const;
        size_t bufPush(const BufEntry& entry);
        void bufClear();
        BufEntry& bufAt(size_t index);
        BufEntry popFirst();
        bool scanEmpty() const;
        void scanPushBack(size_t index);
        void scanPopBack();
        void scanPopFront();
        const PpToken* lastToken() const;
        const PpToken* lastTokenStillBuffered() const;

        void scanEof();
        void scanBegin(const PpToken& token);
        void scanEnd();
        void scanBreak(const PpToken& token);
        void scanString(StringView string);
        void checkStream();
        void advanceLeft();
        void checkStack(i64 depth);
        PrintFrame getTop() const;
        void printBegin(const PpToken& token, i64 size);
        void printEnd();
        void printBreak(const PpToken& token, i64 size);
        void printString(StringView string);
    };

    PpToken makeBreak(i64 offset, i64 blankSpace, char preBreak) {
        PpToken token;
        token.kind = PpTokenKind::Break;
        token.offset = offset;
        token.blankSpace = blankSpace;
        token.preBreak = preBreak;
        return token;
    }

    bool isDelimitedGroup(const TokenTree& tree) {
        if (tree.isToken() || tree.size() < 2 || !tree[0].isToken() || !tree[tree.size() - 1].isToken()) {
            return false;
        }
        const auto open = tree[0].tok().type();
        const auto close = tree[tree.size() - 1].tok().type();
        return (open == TOK_PAREN_OPEN && close == TOK_PAREN_CLOSE) || (open == TOK_SQUARE_OPEN && close == TOK_SQUARE_CLOSE) || (open == TOK_BRACE_OPEN && close == TOK_BRACE_CLOSE);
    }

    bool isReservedIdent(const Token& tok) {
        if (Token::typeIsRword(tok.type()) || tok.type() == TOK_UNDERSCORE) {
            return true;
        }
        return tok.type() == TOK_IDENT && !tok.ident().isRaw && tok.ident().name == "Self";
    }

    bool spaceBetween(const TokenTree& tt1, const TokenTree& tt2) {
        const bool tt1Delimited = !tt1.isToken();
        const bool tt2Delimited = !tt2.isToken();
        const Token* tok1 = tt1Delimited ? nullptr : &tt1.tok();
        const Token* tok2 = tt2Delimited ? nullptr : &tt2.tok();
        const bool tt1IsPunct = tok1 != nullptr && tok1->isPunct();
        const bool tt2IsPunct = tok2 != nullptr && tok2->isPunct();
        if (tok1 != nullptr && tok1->type() == TOK_DOT && !tt2IsPunct) {
            return false;
        }
        if (tok1 != nullptr && tok1->type() == TOK_DOLLAR && tok2 != nullptr && (tok2->type() == TOK_IDENT || Token::typeIsRword(tok2->type()))) {
            return false;
        }
        if (tok2 != nullptr && (tok2->type() == TOK_COMMA || tok2->type() == TOK_SEMICOLON || tok2->type() == TOK_DOT) && !tt1IsPunct) {
            return false;
        }
        const bool tt1IsIdent = tok1 != nullptr && (tok1->type() == TOK_IDENT || Token::typeIsRword(tok1->type()) || tok1->type() == TOK_UNDERSCORE);
        if (tt1IsIdent && tok2 != nullptr && tok2->type() == TOK_EXCLAM) {
            if (!isReservedIdent(*tok1) || (tok1->type() == TOK_IDENT && tok1->ident().isRaw)) {
                return false;
            }
        }
        if (tt1IsIdent && tt2Delimited && tt2[0].tok().type() == TOK_PAREN_OPEN) {
            const bool exempt = tok1->type() == TOK_RWORD_FN || tok1->type() == TOK_RWORD_PUB || (tok1->type() == TOK_IDENT && (tok1->ident().isRaw || tok1->ident().name == "Self"));
            if (!isReservedIdent(*tok1) || exempt) {
                return false;
            }
        }
        if (tok1 != nullptr && tok1->type() == TOK_HASH && tt2Delimited && tt2[0].tok().type() == TOK_SQUARE_OPEN) {
            return false;
        }
        return true;
    }

    enum class Prec : u8 {
        Jump,
        Assign,
        Range,
        LOr,
        LAnd,
        Compare,
        BitOr,
        BitXor,
        BitAnd,
        Shift,
        Sum,
        Product,
        Cast,
        Prefix,
        Unambiguous,
    };

    enum class Kind : u8 {
        Paren,
        Array,
        Repeat,
        ConstBlock,
        Struct,
        Tup,
        Call,
        MethodCall,
        Binary,
        Unary,
        AddrOf,
        Lit,
        Cast,
        Type,
        Let,
        If,
        While,
        ForLoop,
        Loop,
        Match,
        Closure,
        Block,
        Gen,
        Await,
        Use,
        TryBlock,
        Assign,
        AssignOp,
        Field,
        Index,
        Range,
        Underscore,
        Path,
        Break,
        Continue,
        Ret,
        Yeet,
        Become,
        Yield,
        InlineAsm,
        MacCall,
        Try,
        LetStmt,
        Other,
    };

    struct PExpr {
        const ASTExprNode* node = nullptr;
        unsigned parens = 0;
        const ASTIfLetCondition* conds = nullptr;
        size_t count = 0;

        bool isChain() const;
        bool isLet() const;
    };

    PExpr nodeExpr(const ASTExprNode* node);
    PExpr unparen(PExpr e);
    PExpr condExpr(const ASTIfLetCondition* conds, size_t count);
    Kind kindOf(PExpr e);
    Prec precedenceOf(PExpr e);
    bool exprIsComplete(PExpr e);
    bool exprRequiresSemiToBeStmt(PExpr e);
    bool leadingLabeledExpr(PExpr e);
    bool containsExteriorStructLit(PExpr e);
    bool letScrutineeNeedsPar(Prec order);
    bool isRawGuess(const RcString& name);

    struct Fixup {
        bool stmt = false;
        bool leftmostSubexpressionInStmt = false;
        bool matchArm = false;
        bool leftmostSubexpressionInMatchArm = false;
        bool parenthesizeExteriorStructLit = false;
        bool nextOperatorCanBeginExpr = false;
        bool nextOperatorCanContinueExpr = false;

        static Fixup newStmt();
        static Fixup newMatchArm();
        static Fixup newCond();
        Fixup leftmostSubexpression() const;
        Fixup leftmostSubexpressionWithDot() const;
        Fixup leftmostSubexpressionWithOperator(bool canBegin) const;
        Fixup rightmostSubexpression() const;
        bool wouldCauseStatementBoundary(PExpr e) const;
        bool needsParAsLetScrutinee(PExpr e) const;
        Prec precedence(PExpr e) const;
    };

    struct State: Printer {
        using Printer::Printer;

        void printTts(const TokenTree& tts);
        void printExpr(PExpr e, Fixup fixup);

    private:
        void collectTrees(const TokenTree& tree, Vector<const TokenTree*>& out);
        TokenSpacing printTt(const TokenTree& tt);
        void printTtsItems(const Vector<const TokenTree*>& items);
        void printDelimited(const TokenTree& group);

        void printIdent(const RcString& name);
        void printLifetimeName(const RcString& name);
        void printLabel(const Ident& label);
        void printMutability(bool isMut, bool printConst);
        void printPath(const ASTPath& path, bool colonsBeforeParams);
        void printPathSegment(const ASTPathNode& node, bool colonsBeforeParams);
        void printGenericArgs(const ASTPathParams& args, bool colonsBeforeParams);
        void printGenericArg(const ASTPathParamEnt& arg);
        void printFnRetTy(const ASTType* ty);
        void printType(const ASTType* ty);
        void printTypes(const ASTType* const* types, size_t count);
        void printTraitPath(const TypeTraitPath& bound);
        void printHigherRankedBounds(const ASTHigherRankedBounds& hrbs);
        void printPat(const ASTPattern& pat);
        void printPatData(const ASTPattern& pat);
        void printPats(const ASTPattern* pats, size_t count, Breaks breaks);
        void printBindingHead(const ASTPatternBinding& binding);
        void printPatValue(const ASTPatternValue& value);
        void printMac(const ASTPath& path, bool isBraced, bool isBracketed, const TokenTree& tokens);
        void printLiteral(const ASTExprNode& node);

        void printExprCondParen(PExpr e, bool needsPar, Fixup fixup);
        void printExprAsCond(PExpr e);
        bool condNeedsPar(PExpr e);
        void printCallPost(ASTExprNode* const* args, size_t count);
        void commasepExprs(Breaks breaks, ASTExprNode* const* exprs, size_t count);
        void printExprBinary(PExpr e, Fixup fixup);
        void printExprKind(PExpr e, Kind kind, Fixup fixup);
        void printLet(const ASTIfLetCondition& cond, Fixup fixup);
        void printPathExpr(const ASTPath& path);
        void printExprStruct(const ASTPath& path, const ASTExprNodeStructLiteral::tValues& values, const ASTExprNode* base, bool hasRest);
        void printClosure(const ASTExprNodeClosure& closure);
        void printBlockNode(const ASTExprNode* body, bool hasCb);
        void printBlock(const ASTExprNodeBlock& block, bool hasCb);
        void printStmt(const ASTExprNode* node, bool hasSemicolon);
        void printIf(const ASTExprNodeIf& node);
        void printArm(const ASTExprNodeMatchArm& arm);
        void printFallback(const ASTExprNode& node);
    };

    void binOpInfo(ASTExprNodeBinOp::Type type, StringView& text, Prec& prec, int& fixity, bool& canBeginExpr);
    StringView assignOpText(ASTExprNodeAssign::Operation op);
}

bool PpToken::isHardbreak() const {
    return kind == PpTokenKind::Break && offset == 0 && blankSpace == SIZE_INFINITY && preBreak == 0;
}


Printer::Printer(ZeroCopyOutput& out)
    : pool_(ObjPool::fromMemory())
    , out_(out)
{
}

bool Printer::bufEmpty() const {
    return bufFirst_ == buf_.length();
}

size_t Printer::bufPush(const BufEntry& entry) {
    const size_t index = bufBase_ + buf_.length();
    buf_.pushBack(entry);
    return index;
}

void Printer::bufClear() {
    bufBase_ += buf_.length();
    buf_.clear();
    bufFirst_ = 0;
}

BufEntry& Printer::bufAt(size_t index) {
    return buf_.mut(index - bufBase_);
}

BufEntry Printer::popFirst() {
    BufEntry entry = buf_[bufFirst_];
    bufFirst_++;
    if (bufFirst_ == buf_.length()) {
        this->bufClear();
    }
    return entry;
}

bool Printer::scanEmpty() const {
    return scanFirst_ == scanStack_.length();
}

void Printer::scanPushBack(size_t index) {
    scanStack_.pushBack(index);
}

void Printer::scanPopBack() {
    scanStack_.popBack();
    if (this->scanEmpty()) {
        scanStack_.clear();
        scanFirst_ = 0;
    }
}

void Printer::scanPopFront() {
    scanFirst_++;
    if (this->scanEmpty()) {
        scanStack_.clear();
        scanFirst_ = 0;
    }
}

const PpToken* Printer::lastTokenStillBuffered() const {
    if (this->bufEmpty()) {
        return nullptr;
    }
    return &buf_.back().token;
}

const PpToken* Printer::lastToken() const {
    if (const auto* token = this->lastTokenStillBuffered()) {
        return token;
    }
    return hasLastPrinted_ ? &lastPrinted_ : nullptr;
}

void Printer::scanEof() {
    if (!this->scanEmpty()) {
        this->checkStack(0);
        this->advanceLeft();
    }
}

void Printer::scanBegin(const PpToken& token) {
    if (this->scanEmpty()) {
        leftTotal_ = 1;
        rightTotal_ = 1;
        this->bufClear();
    }
    const size_t right = this->bufPush({token, -rightTotal_});
    this->scanPushBack(right);
}

void Printer::scanEnd() {
    if (this->scanEmpty()) {
        this->printEnd();
    } else {
        PpToken token;
        token.kind = PpTokenKind::End;
        const size_t right = this->bufPush({token, -1});
        this->scanPushBack(right);
    }
}

void Printer::scanBreak(const PpToken& token) {
    if (this->scanEmpty()) {
        leftTotal_ = 1;
        rightTotal_ = 1;
        this->bufClear();
    } else {
        this->checkStack(0);
    }
    const size_t right = this->bufPush({token, -rightTotal_});
    this->scanPushBack(right);
    rightTotal_ += token.blankSpace;
}

void Printer::scanString(StringView string) {
    if (this->scanEmpty()) {
        this->printString(string);
    } else {
        PpToken token;
        token.kind = PpTokenKind::String;
        token.string = string;
        const i64 len = static_cast<i64>(string.length());
        this->bufPush({token, len});
        rightTotal_ += len;
        this->checkStream();
    }
}

void Printer::offset(i64 off) {
    if (!this->bufEmpty() && buf_.back().token.kind == PpTokenKind::Break) {
        buf_.mutBack().token.offset += off;
    }
}

void Printer::checkStream() {
    while (rightTotal_ - leftTotal_ > space_) {
        if (!this->scanEmpty() && scanStack_[scanFirst_] == bufBase_ + bufFirst_) {
            this->scanPopFront();
            buf_.mut(bufFirst_).size = SIZE_INFINITY;
        }
        this->advanceLeft();
        if (this->bufEmpty()) {
            break;
        }
    }
}

void Printer::advanceLeft() {
    while (buf_[bufFirst_].size >= 0) {
        const BufEntry left = this->popFirst();
        switch (left.token.kind) {
            case PpTokenKind::String:
                leftTotal_ += static_cast<i64>(left.token.string.length());
                this->printString(left.token.string);
                break;
            case PpTokenKind::Break:
                leftTotal_ += left.token.blankSpace;
                this->printBreak(left.token, left.size);
                break;
            case PpTokenKind::Begin:
                this->printBegin(left.token, left.size);
                break;
            case PpTokenKind::End:
                this->printEnd();
                break;
        }
        lastPrinted_ = left.token;
        hasLastPrinted_ = true;
        if (this->bufEmpty()) {
            break;
        }
    }
}

void Printer::checkStack(i64 depth) {
    while (!this->scanEmpty()) {
        const size_t index = scanStack_.back();
        BufEntry& entry = this->bufAt(index);
        switch (entry.token.kind) {
            case PpTokenKind::Begin:
                if (depth == 0) {
                    return;
                }
                this->scanPopBack();
                entry.size += rightTotal_;
                depth -= 1;
                break;
            case PpTokenKind::End:
                this->scanPopBack();
                entry.size = 1;
                depth += 1;
                break;
            default:
                this->scanPopBack();
                entry.size += rightTotal_;
                if (depth == 0) {
                    return;
                }
                break;
        }
    }
}

PrintFrame Printer::getTop() const {
    if (printStack_.empty()) {
        return PrintFrame{false, 0, Breaks::Inconsistent};
    }
    return printStack_.back();
}

void Printer::printBegin(const PpToken& token, i64 size) {
    if (size > space_) {
        printStack_.pushBack(PrintFrame{false, indent_, token.breaks});
        if (token.visual) {
            indent_ = MARGIN - space_;
        } else {
            indent_ = indent_ + token.offset;
        }
    } else {
        printStack_.pushBack(PrintFrame{true, 0, Breaks::Inconsistent});
    }
}

void Printer::printEnd() {
    const PrintFrame frame = printStack_.popBack();
    if (!frame.fits) {
        indent_ = frame.indent;
    }
}

void Printer::printBreak(const PpToken& token, i64 size) {
    const PrintFrame top = this->getTop();
    bool fits;
    if (top.fits) {
        fits = true;
    } else if (top.breaks == Breaks::Consistent) {
        fits = false;
    } else {
        fits = size <= space_;
    }
    if (fits) {
        pendingIndentation_ += token.blankSpace;
        space_ -= token.blankSpace;
    } else {
        if (token.preBreak != 0) {
            out_ << StringView(reinterpret_cast<const u8*>(&token.preBreak), 1);
        }
        out_ << StringView("\n");
        const i64 indent = indent_ + token.offset;
        pendingIndentation_ = indent;
        space_ = MARGIN - indent > MIN_SPACE ? MARGIN - indent : MIN_SPACE;
    }
}

void Printer::printString(StringView string) {
    for (i64 i = 0; i < pendingIndentation_; i++) {
        out_ << StringView(" ");
    }
    pendingIndentation_ = 0;
    out_ << string;
    space_ -= static_cast<i64>(string.length());
}

void Printer::rbox(i64 indent, Breaks breaks) {
    PpToken token;
    token.kind = PpTokenKind::Begin;
    token.offset = indent;
    token.breaks = breaks;
    this->scanBegin(token);
}

void Printer::ibox(i64 indent) {
    this->rbox(indent, Breaks::Inconsistent);
}

void Printer::cbox(i64 indent) {
    this->rbox(indent, Breaks::Consistent);
}

void Printer::visualAlign() {
    PpToken token;
    token.kind = PpTokenKind::Begin;
    token.visual = true;
    token.breaks = Breaks::Consistent;
    this->scanBegin(token);
}

void Printer::breakOffset(i64 n, i64 off) {
    this->scanBreak(makeBreak(off, n, 0));
}

void Printer::end() {
    this->scanEnd();
}

void Printer::eof() {
    this->scanEof();
}

void Printer::word(StringView w) {
    this->scanString(pool_->intern(w));
}

void Printer::spaces(i64 n) {
    this->breakOffset(n, 0);
}

void Printer::zerobreak() {
    this->spaces(0);
}

void Printer::space() {
    this->spaces(1);
}

void Printer::hardbreak() {
    this->spaces(SIZE_INFINITY);
}

bool Printer::isBeginningOfLine() const {
    const auto* last = this->lastToken();
    return last == nullptr || last->isHardbreak();
}

void Printer::trailingComma() {
    this->scanBreak(makeBreak(0, 0, ','));
}

void Printer::trailingCommaOrSpace() {
    this->scanBreak(makeBreak(0, 1, ','));
}

void Printer::wordSpace(StringView w) {
    this->word(w);
    this->space();
}

void Printer::wordNbsp(StringView w) {
    this->word(w);
    this->nbsp();
}

void Printer::nbsp() {
    this->word(StringView(" "));
}

void Printer::popen() {
    this->word(StringView("("));
}

void Printer::pclose() {
    this->word(StringView(")"));
}

void Printer::hardbreakIfNotBol() {
    if (!this->isBeginningOfLine()) {
        this->hardbreak();
    }
}

void Printer::spaceIfNotBol() {
    if (!this->isBeginningOfLine()) {
        this->space();
    }
}

void Printer::breakOffsetIfNotBol(i64 n, i64 off) {
    if (!this->isBeginningOfLine()) {
        this->breakOffset(n, off);
    } else if (off != 0) {
        if (!this->bufEmpty() && buf_.back().token.isHardbreak()) {
            buf_.mutBack().token = makeBreak(off, SIZE_INFINITY, 0);
        }
    }
}




void State::collectTrees(const TokenTree& tree, Vector<const TokenTree*>& out) {
    if (tree.isToken() || isDelimitedGroup(tree)) {
        out.pushBack(&tree);
        return;
    }
    for (size_t i = 0; i < tree.size(); i++) {
        this->collectTrees(tree[i], out);
    }
}

TokenSpacing State::printTt(const TokenTree& tt) {
    if (tt.isToken()) {
        const auto text = tt.tok().toStr();
        this->word(StringView(reinterpret_cast<const u8*>(text.data()), text.size()));
        return tt.tok().spacing();
    }
    this->printDelimited(tt);
    return tt[tt.size() - 1].tok().spacing();
}

void State::printTtsItems(const Vector<const TokenTree*>& items) {
    for (size_t i = 0; i < items.length(); i++) {
        const TokenSpacing spacing = this->printTt(*items[i]);
        if (i + 1 < items.length()) {
            if (spacing == TokenSpacing::Alone && spaceBetween(*items[i], *items[i + 1])) {
                this->space();
            }
        }
    }
}

void State::printTts(const TokenTree& tts) {
    Vector<const TokenTree*> items;
    if (tts.isToken()) {
        items.pushBack(&tts);
    } else {
        for (size_t i = 0; i < tts.size(); i++) {
            this->collectTrees(tts[i], items);
        }
    }
    this->printTtsItems(items);
}

void State::printDelimited(const TokenTree& group) {
    Vector<const TokenTree*> inner;
    for (size_t i = 1; i + 1 < group.size(); i++) {
        this->collectTrees(group[i], inner);
    }
    const Token& open = group[0].tok();
    const Token& close = group[group.size() - 1].tok();
    if (open.type() == TOK_BRACE_OPEN) {
        this->cbox(INDENT_UNIT);
        this->word(StringView("{"));
        const bool openSpace = open.spacing() == TokenSpacing::Alone && !inner.empty();
        if (openSpace) {
            this->space();
        }
        this->ibox(0);
        this->printTtsItems(inner);
        this->end();
        if (openSpace) {
            this->breakOffsetIfNotBol(1, -INDENT_UNIT);
        }
        this->word(StringView("}"));
        this->end();
        return;
    }
    const auto openText = open.toStr();
    this->word(StringView(reinterpret_cast<const u8*>(openText.data()), openText.size()));
    this->ibox(0);
    this->printTtsItems(inner);
    this->end();
    const auto closeText = close.toStr();
    this->word(StringView(reinterpret_cast<const u8*>(closeText.data()), closeText.size()));
}

namespace {
    void appendCharLiteral(StringBuilder& out, u32 cp) {
        out << StringView("'");
        switch (cp) {
            case '\t':
                out << StringView("\\t");
                break;
            case '\r':
                out << StringView("\\r");
                break;
            case '\n':
                out << StringView("\\n");
                break;
            case '\\':
                out << StringView("\\\\");
                break;
            case '\'':
                out << StringView("\\'");
                break;
            case 0:
                out << StringView("\\0");
                break;
            default:
                if (cp < 0x20 || cp == 0x7F) {
                    out << StringView("\\u{") << formatHex(U128(cp)) << StringView("}");
                } else {
                    u8 bytes[4];
                    size_t len = 0;
                    if (cp < 0x80) {
                        bytes[len++] = static_cast<u8>(cp);
                    } else if (cp < 0x800) {
                        bytes[len++] = static_cast<u8>(0xC0 | (cp >> 6));
                        bytes[len++] = static_cast<u8>(0x80 | (cp & 0x3F));
                    } else if (cp < 0x10000) {
                        bytes[len++] = static_cast<u8>(0xE0 | (cp >> 12));
                        bytes[len++] = static_cast<u8>(0x80 | ((cp >> 6) & 0x3F));
                        bytes[len++] = static_cast<u8>(0x80 | (cp & 0x3F));
                    } else {
                        bytes[len++] = static_cast<u8>(0xF0 | (cp >> 18));
                        bytes[len++] = static_cast<u8>(0x80 | ((cp >> 12) & 0x3F));
                        bytes[len++] = static_cast<u8>(0x80 | ((cp >> 6) & 0x3F));
                        bytes[len++] = static_cast<u8>(0x80 | (cp & 0x3F));
                    }
                    out << StringView(bytes, len);
                }
                break;
        }
        out << StringView("'");
    }

    bool PExpr::isChain() const {
        return conds != nullptr && count > 1;
    }

    bool PExpr::isLet() const {
        return conds != nullptr && count == 1;
    }

    PExpr nodeExpr(const ASTExprNode* node) {
        PExpr e;
        e.node = node;
        e.parens = node->parens();
        return e;
    }

    PExpr unparen(PExpr e) {
        e.parens -= 1;
        return e;
    }

    PExpr condExpr(const ASTIfLetCondition* conds, size_t count) {
        if (count == 1 && !conds[0].optPat) {
            return nodeExpr(conds[0].value);
        }
        PExpr e;
        e.conds = conds;
        e.count = count;
        return e;
    }

    Kind kindOf(PExpr e) {
        if (e.isChain()) {
            return Kind::Binary;
        }
        if (e.isLet()) {
            return Kind::Let;
        }
        if (e.parens > 0) {
            return Kind::Paren;
        }
        const ASTExprNode* node = e.node;
        if (const auto* n = cast<const ASTExprNodeBlock>(node)) {
            return n->blockType == ASTExprNodeBlock::Type::Const ? Kind::ConstBlock : Kind::Block;
        }
        if (cast<const ASTExprNodeAsyncBlock>(node) || cast<const ASTExprNodeGeneratorBlock>(node)) {
            return Kind::Gen;
        }
        if (cast<const ASTExprNodeTry>(node)) {
            return Kind::TryBlock;
        }
        if (cast<const ASTExprNodeMacro>(node)) {
            return Kind::MacCall;
        }
        if (cast<const ASTExprNodeAsm>(node) || cast<const ASTExprNodeAsm2>(node)) {
            return Kind::InlineAsm;
        }
        if (const auto* n = cast<const ASTExprNodeFlow>(node)) {
            switch (n->type) {
                case ASTExprNodeFlow::RETURN:
                    return Kind::Ret;
                case ASTExprNodeFlow::TAILCALL:
                    return Kind::Become;
                case ASTExprNodeFlow::YIELD:
                    return Kind::Yield;
                case ASTExprNodeFlow::CONTINUE:
                    return Kind::Continue;
                case ASTExprNodeFlow::BREAK:
                    return Kind::Break;
                case ASTExprNodeFlow::YEET:
                    return Kind::Yeet;
            }
        }
        if (cast<const ASTExprNodeLetBinding>(node)) {
            return Kind::LetStmt;
        }
        if (const auto* n = cast<const ASTExprNodeAssign>(node)) {
            return n->op == ASTExprNodeAssign::NONE ? Kind::Assign : Kind::AssignOp;
        }
        if (cast<const ASTExprNodeCallPath>(node) || cast<const ASTExprNodeCallObject>(node)) {
            return Kind::Call;
        }
        if (cast<const ASTExprNodeCallMethod>(node)) {
            return Kind::MethodCall;
        }
        if (cast<const ASTExprNodeLoop>(node)) {
            return Kind::Loop;
        }
        if (cast<const ASTExprNodeFor>(node)) {
            return Kind::ForLoop;
        }
        if (cast<const ASTExprNodeWhile>(node)) {
            return Kind::While;
        }
        if (cast<const ASTExprNodeMatch>(node)) {
            return Kind::Match;
        }
        if (cast<const ASTExprNodeIf>(node)) {
            return Kind::If;
        }
        if (cast<const ASTExprNodeWildcardPattern>(node)) {
            return Kind::Underscore;
        }
        if (cast<const ASTExprNodeInteger>(node) || cast<const ASTExprNodeFloat>(node) || cast<const ASTExprNodeBool>(node) || cast<const ASTExprNodeString>(node) || cast<const ASTExprNodeByteString>(node) || cast<const ASTExprNodeCString>(node) || cast<const ASTExprNodeSuffixedLiteral>(node)) {
            return Kind::Lit;
        }
        if (cast<const ASTExprNodeClosure>(node)) {
            return Kind::Closure;
        }
        if (cast<const ASTExprNodeStructLiteral>(node) || cast<const ASTExprNodeStructLiteralPattern>(node)) {
            return Kind::Struct;
        }
        if (const auto* n = cast<const ASTExprNodeArray>(node)) {
            return n->size ? Kind::Repeat : Kind::Array;
        }
        if (cast<const ASTExprNodeTuple>(node)) {
            return Kind::Tup;
        }
        if (cast<const ASTExprNodeNamedValue>(node)) {
            return Kind::Path;
        }
        if (cast<const ASTExprNodeField>(node)) {
            return Kind::Field;
        }
        if (cast<const ASTExprNodeIndex>(node)) {
            return Kind::Index;
        }
        if (cast<const ASTExprNodeDeref>(node)) {
            return Kind::Unary;
        }
        if (const auto* n = cast<const ASTExprNodeUniOp>(node)) {
            switch (n->type) {
                case ASTExprNodeUniOp::REF:
                case ASTExprNodeUniOp::REFMUT:
                case ASTExprNodeUniOp::RawBorrow:
                case ASTExprNodeUniOp::RawBorrowMut:
                case ASTExprNodeUniOp::PinBorrow:
                case ASTExprNodeUniOp::PinBorrowMut:
                    return Kind::AddrOf;
                case ASTExprNodeUniOp::BOX:
                case ASTExprNodeUniOp::INVERT:
                case ASTExprNodeUniOp::NEGATE:
                    return Kind::Unary;
                case ASTExprNodeUniOp::QMARK:
                    return Kind::Try;
                case ASTExprNodeUniOp::AWait:
                case ASTExprNodeUniOp::AWaitNext:
                    return Kind::Await;
                case ASTExprNodeUniOp::USE:
                    return Kind::Use;
            }
        }
        if (cast<const ASTExprNodeCast>(node)) {
            return Kind::Cast;
        }
        if (cast<const ASTExprNodeTypeAnnotation>(node)) {
            return Kind::Type;
        }
        if (const auto* n = cast<const ASTExprNodeBinOp>(node)) {
            return n->type == ASTExprNodeBinOp::RANGE || n->type == ASTExprNodeBinOp::RANGE_INC ? Kind::Range : Kind::Binary;
        }
        return Kind::Other;
    }

    void binOpInfo(ASTExprNodeBinOp::Type type, StringView& text, Prec& prec, int& fixity, bool& canBeginExpr) {
        fixity = 0;
        canBeginExpr = false;
        switch (type) {
            case ASTExprNodeBinOp::CMPEQU:
                text = StringView("==");
                prec = Prec::Compare;
                fixity = 2;
                break;
            case ASTExprNodeBinOp::CMPNEQU:
                text = StringView("!=");
                prec = Prec::Compare;
                fixity = 2;
                break;
            case ASTExprNodeBinOp::CMPLT:
                text = StringView("<");
                prec = Prec::Compare;
                fixity = 2;
                canBeginExpr = true;
                break;
            case ASTExprNodeBinOp::CMPLTE:
                text = StringView("<=");
                prec = Prec::Compare;
                fixity = 2;
                break;
            case ASTExprNodeBinOp::CMPGT:
                text = StringView(">");
                prec = Prec::Compare;
                fixity = 2;
                break;
            case ASTExprNodeBinOp::CMPGTE:
                text = StringView(">=");
                prec = Prec::Compare;
                fixity = 2;
                break;
            case ASTExprNodeBinOp::BOOLAND:
                text = StringView("&&");
                prec = Prec::LAnd;
                canBeginExpr = true;
                break;
            case ASTExprNodeBinOp::BOOLOR:
                text = StringView("||");
                prec = Prec::LOr;
                canBeginExpr = true;
                break;
            case ASTExprNodeBinOp::BITAND:
                text = StringView("&");
                prec = Prec::BitAnd;
                canBeginExpr = true;
                break;
            case ASTExprNodeBinOp::BITOR:
                text = StringView("|");
                prec = Prec::BitOr;
                canBeginExpr = true;
                break;
            case ASTExprNodeBinOp::BITXOR:
                text = StringView("^");
                prec = Prec::BitXor;
                break;
            case ASTExprNodeBinOp::SHL:
                text = StringView("<<");
                prec = Prec::Shift;
                canBeginExpr = true;
                break;
            case ASTExprNodeBinOp::SHR:
                text = StringView(">>");
                prec = Prec::Shift;
                break;
            case ASTExprNodeBinOp::MULTIPLY:
                text = StringView("*");
                prec = Prec::Product;
                canBeginExpr = true;
                break;
            case ASTExprNodeBinOp::DIVIDE:
                text = StringView("/");
                prec = Prec::Product;
                break;
            case ASTExprNodeBinOp::MODULO:
                text = StringView("%");
                prec = Prec::Product;
                break;
            case ASTExprNodeBinOp::ADD:
                text = StringView("+");
                prec = Prec::Sum;
                break;
            case ASTExprNodeBinOp::SUB:
                text = StringView("-");
                prec = Prec::Sum;
                canBeginExpr = true;
                break;
            case ASTExprNodeBinOp::PLACE_IN:
                text = StringView("<-");
                prec = Prec::Assign;
                fixity = 1;
                break;
            case ASTExprNodeBinOp::RANGE:
                text = StringView("..");
                prec = Prec::Range;
                fixity = 2;
                break;
            case ASTExprNodeBinOp::RANGE_INC:
                text = StringView("..=");
                prec = Prec::Range;
                fixity = 2;
                break;
        }
    }

    StringView assignOpText(ASTExprNodeAssign::Operation op) {
        switch (op) {
            case ASTExprNodeAssign::NONE:
                return StringView("=");
            case ASTExprNodeAssign::ADD:
                return StringView("+=");
            case ASTExprNodeAssign::SUB:
                return StringView("-=");
            case ASTExprNodeAssign::MUL:
                return StringView("*=");
            case ASTExprNodeAssign::DIV:
                return StringView("/=");
            case ASTExprNodeAssign::MOD:
                return StringView("%=");
            case ASTExprNodeAssign::AND:
                return StringView("&=");
            case ASTExprNodeAssign::OR:
                return StringView("|=");
            case ASTExprNodeAssign::XOR:
                return StringView("^=");
            case ASTExprNodeAssign::SHR:
                return StringView(">>=");
            case ASTExprNodeAssign::SHL:
                return StringView("<<=");
        }
        return StringView("=");
    }

    Prec precedenceOf(PExpr e) {
        switch (kindOf(e)) {
            case Kind::Closure: {
                const auto* closure = cast<const ASTExprNodeClosure>(e.node);
                const bool hasReturnType = closure->returnType != nullptr && !closure->returnType->isWildcard();
                return hasReturnType ? Prec::Unambiguous : Prec::Jump;
            }
            case Kind::Break:
            case Kind::Ret:
            case Kind::Yield:
            case Kind::Yeet:
                return cast<const ASTExprNodeFlow>(e.node)->value ? Prec::Jump : Prec::Unambiguous;
            case Kind::Become:
                return Prec::Jump;
            case Kind::Range:
                return Prec::Range;
            case Kind::Binary: {
                if (e.isChain()) {
                    return Prec::LAnd;
                }
                StringView text;
                Prec prec;
                int fixity;
                bool canBegin;
                binOpInfo(cast<const ASTExprNodeBinOp>(e.node)->type, text, prec, fixity, canBegin);
                return prec;
            }
            case Kind::Cast:
                return Prec::Cast;
            case Kind::Assign:
            case Kind::AssignOp:
                return Prec::Assign;
            case Kind::AddrOf:
            case Kind::Let:
            case Kind::Unary:
                return Prec::Prefix;
            default:
                return Prec::Unambiguous;
        }
    }

    bool exprIsComplete(PExpr e) {
        switch (kindOf(e)) {
            case Kind::If:
            case Kind::Match:
            case Kind::Block:
            case Kind::While:
            case Kind::Loop:
            case Kind::ForLoop:
            case Kind::TryBlock:
            case Kind::ConstBlock:
                return true;
            default:
                return false;
        }
    }

    bool exprRequiresSemiToBeStmt(PExpr e) {
        if (kindOf(e) == Kind::MacCall) {
            return !cast<const ASTExprNodeMacro>(e.node)->isBraced;
        }
        return !exprIsComplete(e);
    }

    bool leadingLabeledExpr(PExpr e) {
        while (true) {
            switch (kindOf(e)) {
                case Kind::Block:
                    return cast<const ASTExprNodeBlock>(e.node)->label.name != "";
                case Kind::ForLoop:
                    return cast<const ASTExprNodeFor>(e.node)->label.name != "";
                case Kind::Loop:
                    return cast<const ASTExprNodeLoop>(e.node)->label.name != "";
                case Kind::While:
                    return cast<const ASTExprNodeWhile>(e.node)->label.name != "";
                case Kind::Assign:
                case Kind::AssignOp:
                    e = nodeExpr(cast<const ASTExprNodeAssign>(e.node)->slot);
                    break;
                case Kind::Await:
                case Kind::Use:
                case Kind::Try:
                    e = nodeExpr(cast<const ASTExprNodeUniOp>(e.node)->value);
                    break;
                case Kind::Binary:
                    if (e.isChain()) {
                        e = condExpr(e.conds, e.count - 1);
                    } else {
                        e = nodeExpr(cast<const ASTExprNodeBinOp>(e.node)->left);
                    }
                    break;
                case Kind::Call:
                    if (const auto* n = cast<const ASTExprNodeCallObject>(e.node)) {
                        e = nodeExpr(n->val);
                        break;
                    }
                    return false;
                case Kind::Cast:
                    e = nodeExpr(cast<const ASTExprNodeCast>(e.node)->value);
                    break;
                case Kind::Field:
                    e = nodeExpr(cast<const ASTExprNodeField>(e.node)->obj);
                    break;
                case Kind::Index:
                    e = nodeExpr(cast<const ASTExprNodeIndex>(e.node)->obj);
                    break;
                case Kind::Range: {
                    const auto* n = cast<const ASTExprNodeBinOp>(e.node);
                    if (!n->left) {
                        return false;
                    }
                    e = nodeExpr(n->left);
                    break;
                }
                case Kind::MethodCall:
                    e = nodeExpr(cast<const ASTExprNodeCallMethod>(e.node)->val);
                    break;
                default:
                    return false;
            }
        }
    }

    bool containsExteriorStructLit(PExpr e) {
        switch (kindOf(e)) {
            case Kind::Struct:
                return true;
            case Kind::Assign:
            case Kind::AssignOp: {
                const auto* n = cast<const ASTExprNodeAssign>(e.node);
                return containsExteriorStructLit(nodeExpr(n->slot)) || containsExteriorStructLit(nodeExpr(n->value));
            }
            case Kind::Binary: {
                if (e.isChain()) {
                    return containsExteriorStructLit(condExpr(e.conds, e.count - 1)) || containsExteriorStructLit(condExpr(e.conds + e.count - 1, 1));
                }
                const auto* n = cast<const ASTExprNodeBinOp>(e.node);
                return containsExteriorStructLit(nodeExpr(n->left)) || containsExteriorStructLit(nodeExpr(n->right));
            }
            case Kind::Await:
            case Kind::Try:
            case Kind::Use:
                return containsExteriorStructLit(nodeExpr(cast<const ASTExprNodeUniOp>(e.node)->value));
            case Kind::Unary:
                if (const auto* n = cast<const ASTExprNodeDeref>(e.node)) {
                    return containsExteriorStructLit(nodeExpr(n->value));
                }
                return containsExteriorStructLit(nodeExpr(cast<const ASTExprNodeUniOp>(e.node)->value));
            case Kind::Cast:
                return containsExteriorStructLit(nodeExpr(cast<const ASTExprNodeCast>(e.node)->value));
            case Kind::Type:
                return containsExteriorStructLit(nodeExpr(cast<const ASTExprNodeTypeAnnotation>(e.node)->value));
            case Kind::Field:
                return containsExteriorStructLit(nodeExpr(cast<const ASTExprNodeField>(e.node)->obj));
            case Kind::Index:
                return containsExteriorStructLit(nodeExpr(cast<const ASTExprNodeIndex>(e.node)->obj));
            case Kind::MethodCall:
                return containsExteriorStructLit(nodeExpr(cast<const ASTExprNodeCallMethod>(e.node)->val));
            default:
                return false;
        }
    }

    bool letScrutineeNeedsPar(Prec order) {
        return order <= Prec::LAnd;
    }

    bool isRawGuess(const RcString& name) {
        const StringView text(reinterpret_cast<const u8*>(name.c_str()), name.size());
        return text == StringView("as") ||
            text == StringView("break") ||
            text == StringView("const") ||
            text == StringView("continue") ||
            text == StringView("else") ||
            text == StringView("enum") ||
            text == StringView("extern") ||
            text == StringView("false") ||
            text == StringView("fn") ||
            text == StringView("for") ||
            text == StringView("if") ||
            text == StringView("impl") ||
            text == StringView("in") ||
            text == StringView("let") ||
            text == StringView("loop") ||
            text == StringView("match") ||
            text == StringView("mod") ||
            text == StringView("move") ||
            text == StringView("mut") ||
            text == StringView("pub") ||
            text == StringView("ref") ||
            text == StringView("return") ||
            text == StringView("static") ||
            text == StringView("struct") ||
            text == StringView("trait") ||
            text == StringView("true") ||
            text == StringView("type") ||
            text == StringView("unsafe") ||
            text == StringView("use") ||
            text == StringView("where") ||
            text == StringView("while") ||
            text == StringView("abstract") ||
            text == StringView("become") ||
            text == StringView("box") ||
            text == StringView("do") ||
            text == StringView("final") ||
            text == StringView("macro") ||
            text == StringView("override") ||
            text == StringView("priv") ||
            text == StringView("typeof") ||
            text == StringView("unsized") ||
            text == StringView("virtual") ||
            text == StringView("yield") ||
            text == StringView("async") ||
            text == StringView("await") ||
            text == StringView("dyn") ||
            text == StringView("try");
    }

    Fixup Fixup::newStmt() {
        Fixup f;
        f.stmt = true;
        return f;
    }

    Fixup Fixup::newMatchArm() {
        Fixup f;
        f.matchArm = true;
        return f;
    }

    Fixup Fixup::newCond() {
        Fixup f;
        f.parenthesizeExteriorStructLit = true;
        return f;
    }

    Fixup Fixup::leftmostSubexpression() const {
        Fixup f = *this;
        f.stmt = false;
        f.leftmostSubexpressionInStmt = stmt || leftmostSubexpressionInStmt;
        f.matchArm = false;
        f.leftmostSubexpressionInMatchArm = matchArm || leftmostSubexpressionInMatchArm;
        f.nextOperatorCanBeginExpr = false;
        f.nextOperatorCanContinueExpr = true;
        return f;
    }

    Fixup Fixup::leftmostSubexpressionWithDot() const {
        Fixup f = *this;
        f.stmt = stmt || leftmostSubexpressionInStmt;
        f.leftmostSubexpressionInStmt = false;
        f.matchArm = matchArm || leftmostSubexpressionInMatchArm;
        f.leftmostSubexpressionInMatchArm = false;
        f.nextOperatorCanBeginExpr = false;
        f.nextOperatorCanContinueExpr = true;
        return f;
    }

    Fixup Fixup::leftmostSubexpressionWithOperator(bool canBegin) const {
        Fixup f = this->leftmostSubexpression();
        f.nextOperatorCanBeginExpr = canBegin;
        return f;
    }

    Fixup Fixup::rightmostSubexpression() const {
        Fixup f = *this;
        f.stmt = false;
        f.leftmostSubexpressionInStmt = false;
        f.matchArm = false;
        f.leftmostSubexpressionInMatchArm = false;
        return f;
    }

    bool Fixup::wouldCauseStatementBoundary(PExpr e) const {
        return (leftmostSubexpressionInStmt && !exprRequiresSemiToBeStmt(e)) || (leftmostSubexpressionInMatchArm && exprIsComplete(e));
    }

    bool Fixup::needsParAsLetScrutinee(PExpr e) const {
        return (parenthesizeExteriorStructLit && containsExteriorStructLit(e)) || letScrutineeNeedsPar(this->precedence(e));
    }

    Prec Fixup::precedence(PExpr e) const {
        const Kind kind = kindOf(e);
        if (nextOperatorCanBeginExpr) {
            if (kind == Kind::Break || kind == Kind::Ret || kind == Kind::Yeet || kind == Kind::Yield) {
                return Prec::Jump;
            }
        }
        if (!nextOperatorCanContinueExpr) {
            if (kind == Kind::Break || kind == Kind::Closure || kind == Kind::Ret || kind == Kind::Yeet || kind == Kind::Yield) {
                return Prec::Prefix;
            }
            if (kind == Kind::Range && cast<const ASTExprNodeBinOp>(e.node)->left == nullptr) {
                return Prec::Prefix;
            }
        }
        return precedenceOf(e);
    }
}

void State::printIdent(const RcString& name) {
    if (isRawGuess(name)) {
        this->word(StringView("r#"));
    }
    this->word(StringView(reinterpret_cast<const u8*>(name.c_str()), name.size()));
}

void State::printLifetimeName(const RcString& name) {
    this->word(StringView("'"));
    this->word(StringView(reinterpret_cast<const u8*>(name.c_str()), name.size()));
}

void State::printLabel(const Ident& label) {
    this->printLifetimeName(label.name);
}

void State::printMutability(bool isMut, bool printConst) {
    if (isMut) {
        this->wordNbsp(StringView("mut"));
    } else if (printConst) {
        this->wordNbsp(StringView("const"));
    }
}

void State::printPathSegment(const ASTPathNode& node, bool colonsBeforeParams) {
    this->printIdent(node.name());
    if (!node.args().isEmpty()) {
        this->printGenericArgs(node.args(), colonsBeforeParams);
    }
}

void State::printPath(const ASTPath& path, bool colonsBeforeParams) {
    switch (path.cls.tag()) {
        case ASTPathClass::TAG_Invalid:
            break;
        case ASTPathClass::TAG_Local:
            this->printIdent(path.cls.as_Local().name);
            break;
        case ASTPathClass::TAG_Relative: {
            const auto& nodes = path.cls.as_Relative().nodes;
            for (size_t i = 0; i < nodes.size(); i++) {
                if (i > 0) {
                    this->word(StringView("::"));
                }
                this->printPathSegment(nodes[i], colonsBeforeParams);
            }
            break;
        }
        case ASTPathClass::TAG_Self:
            this->word(StringView("self"));
            for (const auto& node : path.cls.as_Self().nodes) {
                this->word(StringView("::"));
                this->printPathSegment(node, colonsBeforeParams);
            }
            break;
        case ASTPathClass::TAG_Super: {
            const auto& e = path.cls.as_Super();
            for (unsigned i = 0; i < e.count; i++) {
                if (i > 0) {
                    this->word(StringView("::"));
                }
                this->word(StringView("super"));
            }
            for (const auto& node : e.nodes) {
                this->word(StringView("::"));
                this->printPathSegment(node, colonsBeforeParams);
            }
            break;
        }
        case ASTPathClass::TAG_Absolute: {
            const auto& e = path.cls.as_Absolute();
            const char* crate = e.crate.c_str();
            if (crate[0] == 0) {
                this->word(StringView("crate"));
            } else if (crate[0] == '=') {
                this->word(StringView("::"));
                this->word(StringView(crate + 1));
            } else {
                this->word(StringView("$crate"));
            }
            for (const auto& node : e.nodes) {
                this->word(StringView("::"));
                this->printPathSegment(node, colonsBeforeParams);
            }
            break;
        }
        case ASTPathClass::TAG_UFCS: {
            const auto& e = path.cls.as_UFCS();
            this->word(StringView("<"));
            this->printType(e.type);
            if (e.trait) {
                this->space();
                this->wordSpace(StringView("as"));
                if (e.trait->cls.is_Invalid()) {
                    this->word(StringView("_"));
                } else {
                    this->printPath(*e.trait, false);
                }
            }
            this->word(StringView(">"));
            for (const auto& node : e.nodes) {
                this->word(StringView("::"));
                this->printPathSegment(node, colonsBeforeParams);
            }
            break;
        }
    }
}

void State::printGenericArg(const ASTPathParamEnt& arg) {
    switch (arg.tag()) {
        case ASTPathParamEnt::TAG_Null:
            break;
        case ASTPathParamEnt::TAG_Lifetime:
            this->printLifetimeName(arg.as_Lifetime().name().name);
            break;
        case ASTPathParamEnt::TAG_Type:
            this->printType(arg.as_Type());
            break;
        case ASTPathParamEnt::TAG_Value:
            this->printExpr(nodeExpr(arg.as_Value()), Fixup());
            break;
        case ASTPathParamEnt::TAG_AssociatedTyEqual: {
            const auto& e = arg.as_AssociatedTyEqual();
            this->printPathSegment(e.first, false);
            this->space();
            this->wordSpace(StringView("="));
            this->printType(e.second);
            break;
        }
        case ASTPathParamEnt::TAG_AssociatedTyBound: {
            const auto& e = arg.as_AssociatedTyBound();
            this->printPathSegment(e.first, false);
            this->space();
            if (!e.second.empty()) {
                this->wordNbsp(StringView(":"));
                for (size_t i = 0; i < e.second.size(); i++) {
                    if (i > 0) {
                        this->nbsp();
                        this->wordSpace(StringView("+"));
                    }
                    this->printTraitPath(e.second[i]);
                }
            }
            break;
        }
        case ASTPathParamEnt::TAG_AssociatedValueEqual: {
            const auto& e = arg.as_AssociatedValueEqual();
            this->printPathSegment(e.first, false);
            this->space();
            this->wordSpace(StringView("="));
            this->printExpr(nodeExpr(e.second), Fixup());
            break;
        }
    }
}

void State::printGenericArgs(const ASTPathParams& args, bool colonsBeforeParams) {
    if (colonsBeforeParams) {
        this->word(StringView("::"));
    }
    if (args.isRtn) {
        this->word(StringView("("));
        this->word(StringView(".."));
        this->word(StringView(")"));
        return;
    }
    if (args.isParen && args.entries.size() == 2 && args.entries[0].is_Type() && args.entries[0].as_Type()->isTuple()) {
        const auto& inputs = args.entries[0].as_Type()->data.as_Tuple().innerTypes;
        this->word(StringView("("));
        this->printTypes(inputs.data(), inputs.length());
        this->word(StringView(")"));
        if (args.entries[1].is_AssociatedTyEqual()) {
            this->printFnRetTy(args.entries[1].as_AssociatedTyEqual().second);
        }
        return;
    }
    this->word(StringView("<"));
    this->rbox(0, Breaks::Inconsistent);
    for (size_t i = 0; i < args.entries.size(); i++) {
        if (i > 0) {
            this->wordSpace(StringView(","));
        }
        this->printGenericArg(args.entries[i]);
    }
    this->end();
    this->word(StringView(">"));
}

void State::printFnRetTy(const ASTType* ty) {
    if (ty == nullptr || ty->isUnit() || ty->isWildcard()) {
        return;
    }
    this->spaceIfNotBol();
    this->ibox(INDENT_UNIT);
    this->wordSpace(StringView("->"));
    this->printType(ty);
    this->end();
}

void State::printTypes(const ASTType* const* types, size_t count) {
    this->rbox(0, Breaks::Inconsistent);
    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            this->wordSpace(StringView(","));
        }
        this->printType(types[i]);
    }
    this->end();
}

void State::printHigherRankedBounds(const ASTHigherRankedBounds& hrbs) {
    if (hrbs.empty()) {
        return;
    }
    this->word(StringView("for"));
    this->word(StringView("<"));
    this->rbox(0, Breaks::Inconsistent);
    bool first = true;
    for (const auto& lifetime : hrbs.lifetimes) {
        if (!first) {
            this->wordSpace(StringView(","));
        }
        first = false;
        this->printLifetimeName(lifetime.name().name);
    }
    for (const auto& type : hrbs.types) {
        if (!first) {
            this->wordSpace(StringView(","));
        }
        first = false;
        this->printIdent(type);
    }
    this->end();
    this->word(StringView(">"));
    this->nbsp();
}

void State::printTraitPath(const TypeTraitPath& bound) {
    this->printHigherRankedBounds(bound.hrbs);
    switch (bound.constness) {
        case ASTBoundConstness::Never:
            break;
        case ASTBoundConstness::Always:
            this->wordSpace(StringView("const"));
            break;
        case ASTBoundConstness::Maybe:
            this->wordSpace(StringView("[const]"));
            break;
    }
    if (bound.isAsync) {
        this->wordSpace(StringView("async"));
    }
    if (bound.path) {
        this->printPath(*bound.path, false);
    }
}

void State::printType(const ASTType* ty) {
    this->ibox(0);
    const auto& data = ty->data;
    switch (data.tag()) {
        case TypeData::TAG_None:
        case TypeData::TAG_Any:
            this->word(StringView("_"));
            break;
        case TypeData::TAG_Bang:
            this->word(StringView("!"));
            break;
        case TypeData::TAG_Unit:
            this->popen();
            this->pclose();
            break;
        case TypeData::TAG_Macro: {
            const auto* inv = data.as_Macro().inv;
            this->printMac(inv->path(), false, false, inv->inputTt());
            break;
        }
        case TypeData::TAG_Primitive:
            this->word(StringView(coretypeName(data.as_Primitive().coreType)));
            break;
        case TypeData::TAG_Function: {
            const auto& info = data.as_Function().info;
            this->ibox(INDENT_UNIT);
            this->printHigherRankedBounds(info.hrbs);
            if (info.isUnsafe) {
                this->wordNbsp(StringView("unsafe"));
            }
            if (!info.abi.empty() && info.abi != "Rust") {
                this->wordNbsp(StringView("extern"));
                this->word(StringView("\""));
                this->word(StringView(reinterpret_cast<const u8*>(info.abi.data()), info.abi.size()));
                this->word(StringView("\""));
                this->nbsp();
            }
            this->word(StringView("fn"));
            this->word(StringView("("));
            this->rbox(0, Breaks::Inconsistent);
            for (size_t i = 0; i < info.argTypes.length(); i++) {
                if (i > 0) {
                    this->wordSpace(StringView(","));
                }
                this->ibox(INDENT_UNIT);
                this->printType(info.argTypes[i]);
                this->end();
            }
            if (info.isVariadic) {
                if (info.argTypes.length() > 0) {
                    this->wordSpace(StringView(","));
                }
                this->word(StringView("..."));
            }
            this->end();
            this->word(StringView(")"));
            this->printFnRetTy(info.rettype);
            this->end();
            break;
        }
        case TypeData::TAG_Tuple: {
            const auto& inner = data.as_Tuple().innerTypes;
            this->popen();
            this->printTypes(inner.data(), inner.length());
            if (inner.length() == 1) {
                this->word(StringView(","));
            }
            this->pclose();
            break;
        }
        case TypeData::TAG_Borrow: {
            const auto& e = data.as_Borrow();
            this->word(StringView("&"));
            if (e.lifetime.name().name != "") {
                this->printLifetimeName(e.lifetime.name().name);
                this->nbsp();
            }
            if (e.isPin) {
                this->word(StringView("pin "));
                this->printMutability(e.isMut, true);
            } else {
                this->printMutability(e.isMut, false);
            }
            this->printType(e.inner);
            break;
        }
        case TypeData::TAG_Pointer: {
            const auto& e = data.as_Pointer();
            this->word(StringView("*"));
            this->printMutability(e.isMut, true);
            this->printType(e.inner);
            break;
        }
        case TypeData::TAG_Array: {
            const auto& e = data.as_Array();
            this->word(StringView("["));
            this->printType(e.inner);
            this->word(StringView("; "));
            if (e.size) {
                this->printExpr(nodeExpr(e.size), Fixup());
            } else {
                this->word(StringView("_"));
            }
            this->word(StringView("]"));
            break;
        }
        case TypeData::TAG_Slice:
            this->word(StringView("["));
            this->printType(data.as_Slice().inner);
            this->word(StringView("]"));
            break;
        case TypeData::TAG_Pattern:
            this->printType(data.as_Pattern().inner);
            this->word(StringView(" is "));
            this->printPat(*data.as_Pattern().pattern);
            break;
        case TypeData::TAG_Generic:
            this->printIdent(data.as_Generic().name);
            break;
        case TypeData::TAG_Path:
            this->printPath(*data.as_Path(), false);
            break;
        case TypeData::TAG_TraitObject: {
            const auto& e = data.as_TraitObject();
            this->wordNbsp(StringView("dyn"));
            bool first = true;
            for (const auto& bound : e.traits) {
                if (!first) {
                    this->nbsp();
                    this->wordSpace(StringView("+"));
                }
                first = false;
                this->printTraitPath(bound);
            }
            for (const auto& lifetime : e.lifetimes) {
                if (!first) {
                    this->nbsp();
                    this->wordSpace(StringView("+"));
                }
                first = false;
                this->printLifetimeName(lifetime.name().name);
            }
            break;
        }
        case TypeData::TAG_ErasedType: {
            const auto* e = data.as_ErasedType();
            this->wordNbsp(StringView("impl"));
            bool first = true;
            for (const auto& bound : e->traits) {
                if (!first) {
                    this->nbsp();
                    this->wordSpace(StringView("+"));
                }
                first = false;
                this->printTraitPath(bound);
            }
            for (const auto& bound : e->maybeTraits) {
                if (!first) {
                    this->nbsp();
                    this->wordSpace(StringView("+"));
                }
                first = false;
                this->word(StringView("?"));
                this->printTraitPath(bound);
            }
            for (const auto& lifetime : e->lifetimes) {
                if (!first) {
                    this->nbsp();
                    this->wordSpace(StringView("+"));
                }
                first = false;
                this->printLifetimeName(lifetime.name().name);
            }
            break;
        }
    }
    this->end();
}

void State::printBindingHead(const ASTPatternBinding& binding) {
    if (binding.isMutable) {
        this->wordNbsp(StringView("mut"));
    }
    if (binding.type == ASTPatternBinding::Type::REF) {
        this->wordNbsp(StringView("ref"));
    } else if (binding.type == ASTPatternBinding::Type::MUTREF) {
        this->wordNbsp(StringView("ref"));
        this->wordNbsp(StringView("mut"));
    }
    this->printIdent(binding.name.name);
}

void State::printPatValue(const ASTPatternValue& value) {
    switch (value.tag()) {
        case ASTPatternValue::TAG_Invalid:
            break;
        case ASTPatternValue::TAG_Integer: {
            const auto& e = value.as_Integer();
            StringBuilder text;
            if (e.type == CORETYPE_CHAR) {
                appendCharLiteral(text, static_cast<u32>(e.value.truncateU64()));
            } else {
                const bool isSigned = e.type == CORETYPE_I8 || e.type == CORETYPE_I16 || e.type == CORETYPE_I32 || e.type == CORETYPE_I64 || e.type == CORETYPE_I128 || e.type == CORETYPE_INT || e.type == CORETYPE_ANY;
                if (isSigned && (e.value.getHi() >> 63) != 0) {
                    text << StringView("-") << (U128(0) - e.value);
                } else {
                    text << e.value;
                }
                if (e.type != CORETYPE_ANY) {
                    text << StringView(coretypeName(e.type));
                }
            }
            this->word(StringView(text));
            break;
        }
        case ASTPatternValue::TAG_Float: {
            const auto& e = value.as_Float();
            auto text = formatFloatValueForToken(e.value);
            this->word(StringView(reinterpret_cast<const u8*>(text.data()), text.size()));
            if (e.type != CORETYPE_ANY) {
                this->word(StringView(coretypeName(e.type)));
            }
            break;
        }
        case ASTPatternValue::TAG_String: {
            StringBuilder text;
            printEscapedLiteral(text, TOK_STRING, reinterpret_cast<const u8*>(value.as_String().data()), value.as_String().size());
            this->word(StringView(text));
            break;
        }
        case ASTPatternValue::TAG_ByteString: {
            StringBuilder text;
            printEscapedLiteral(text, TOK_BYTESTRING, reinterpret_cast<const u8*>(value.as_ByteString().v.data()), value.as_ByteString().v.size());
            this->word(StringView(text));
            break;
        }
        case ASTPatternValue::TAG_Named:
            this->printPath(value.as_Named(), true);
            break;
    }
}

void State::printPats(const ASTPattern* pats, size_t count, Breaks breaks) {
    this->rbox(0, breaks);
    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            this->wordSpace(StringView(","));
        }
        this->printPat(pats[i]);
    }
    this->end();
}

void State::printPat(const ASTPattern& pat) {
    if (!pat.bindings().empty()) {
        this->printBindingHead(pat.bindings()[0]);
        if (!pat.data().is_Any()) {
            this->space();
            this->wordSpace(StringView("@"));
            this->printPatData(pat);
        }
        return;
    }
    this->printPatData(pat);
}

void State::printPatData(const ASTPattern& pat) {
    const auto& data = pat.data();
    switch (data.tag()) {
        case ASTPatternData::TAG_MaybeBind:
            this->printIdent(data.as_MaybeBind().name.name);
            break;
        case ASTPatternData::TAG_Macro: {
            const auto& inv = *data.as_Macro().inv;
            this->printMac(inv.path(), false, false, inv.inputTt());
            break;
        }
        case ASTPatternData::TAG_Any:
            this->word(StringView("_"));
            break;
        case ASTPatternData::TAG_Never:
            this->word(StringView("!"));
            break;
        case ASTPatternData::TAG_Box:
            this->word(StringView("box "));
            this->printPat(*data.as_Box().sub);
            break;
        case ASTPatternData::TAG_Deref:
            this->word(StringView("deref!"));
            this->popen();
            this->printPat(*data.as_Deref().sub);
            this->pclose();
            break;
        case ASTPatternData::TAG_Ref: {
            const auto& e = data.as_Ref();
            this->word(StringView("&"));
            if (e.mut) {
                this->word(StringView("mut "));
            }
            const auto& sub = *e.sub;
            const bool isMutBinding = !sub.bindings().empty() && sub.bindings()[0].isMutable && sub.bindings()[0].type == ASTPatternBinding::Type::MOVE && sub.data().is_Any();
            if (isMutBinding) {
                this->popen();
                this->printPat(sub);
                this->pclose();
            } else {
                this->printPat(sub);
            }
            break;
        }
        case ASTPatternData::TAG_Guard: {
            const auto& e = data.as_Guard();
            this->popen();
            this->printPat(*e.sub);
            this->space();
            this->wordSpace(StringView("if"));
            this->printExpr(nodeExpr(e.cond), Fixup());
            this->pclose();
            break;
        }
        case ASTPatternData::TAG_Value: {
            const auto& e = data.as_Value();
            if (e.end.is_Invalid()) {
                this->printPatValue(e.start);
            } else {
                this->printPatValue(e.start);
                this->word(StringView("..="));
                this->printPatValue(e.end);
            }
            break;
        }
        case ASTPatternData::TAG_ValueLeftInc: {
            const auto& e = data.as_ValueLeftInc();
            this->printPatValue(e.start);
            this->word(StringView(".."));
            this->printPatValue(e.end);
            break;
        }
        case ASTPatternData::TAG_Tuple:
        case ASTPatternData::TAG_StructTuple: {
            const auto& tup = data.is_Tuple() ? data.as_Tuple() : data.as_StructTuple().tupPat;
            if (data.is_StructTuple()) {
                this->printPath(data.as_StructTuple().path, true);
            }
            this->popen();
            this->rbox(0, Breaks::Inconsistent);
            size_t printed = 0;
            for (const auto& sub : tup.start) {
                if (printed++ > 0) {
                    this->wordSpace(StringView(","));
                }
                this->printPat(sub);
            }
            if (tup.hasWildcard) {
                if (printed++ > 0) {
                    this->wordSpace(StringView(","));
                }
                this->word(StringView(".."));
            }
            for (const auto& sub : tup.end) {
                if (printed++ > 0) {
                    this->wordSpace(StringView(","));
                }
                this->printPat(sub);
            }
            this->end();
            if (data.is_Tuple() && printed == 1) {
                this->word(StringView(","));
            }
            this->pclose();
            break;
        }
        case ASTPatternData::TAG_Struct: {
            const auto& e = data.as_Struct();
            this->printPath(e.path, true);
            this->nbsp();
            this->word(StringView("{"));
            const bool empty = e.subPatterns.empty() && e.isExhaustive;
            if (!empty) {
                this->space();
            }
            this->rbox(0, Breaks::Consistent);
            for (size_t i = 0; i < e.subPatterns.size(); i++) {
                const auto& field = e.subPatterns[i];
                this->cbox(INDENT_UNIT);
                const auto& sub = field.pat;
                bool isShorthand = false;
                if (!sub.bindings().empty() && sub.data().is_Any()) {
                    isShorthand = sub.bindings()[0].name.name == field.name;
                } else if (sub.bindings().empty() && sub.data().is_MaybeBind()) {
                    isShorthand = sub.data().as_MaybeBind().name.name == field.name;
                }
                if (!isShorthand) {
                    this->printIdent(field.name);
                    this->wordNbsp(StringView(":"));
                }
                this->printPat(sub);
                this->end();
                if (i + 1 < e.subPatterns.size()) {
                    this->word(StringView(","));
                    this->spaceIfNotBol();
                }
            }
            this->end();
            if (!e.isExhaustive) {
                if (!e.subPatterns.empty()) {
                    this->wordSpace(StringView(","));
                }
                this->word(StringView(".."));
            }
            if (!empty) {
                this->space();
            }
            this->word(StringView("}"));
            break;
        }
        case ASTPatternData::TAG_Slice: {
            const auto& e = data.as_Slice();
            this->word(StringView("["));
            this->printPats(e.subPats.data(), e.subPats.size(), Breaks::Inconsistent);
            this->word(StringView("]"));
            break;
        }
        case ASTPatternData::TAG_SplitSlice: {
            const auto& e = data.as_SplitSlice();
            this->word(StringView("["));
            this->rbox(0, Breaks::Inconsistent);
            size_t printed = 0;
            for (const auto& sub : e.leading) {
                if (printed++ > 0) {
                    this->wordSpace(StringView(","));
                }
                this->printPat(sub);
            }
            if (printed++ > 0) {
                this->wordSpace(StringView(","));
            }
            if (e.extraBind.isValid()) {
                this->printBindingHead(e.extraBind);
                this->space();
                this->wordSpace(StringView("@"));
            }
            this->word(StringView(".."));
            for (const auto& sub : e.trailing) {
                this->wordSpace(StringView(","));
                this->printPat(sub);
            }
            this->end();
            this->word(StringView("]"));
            break;
        }
        case ASTPatternData::TAG_Or: {
            const auto& pats = data.as_Or();
            this->rbox(0, Breaks::Inconsistent);
            for (size_t i = 0; i < pats.size(); i++) {
                if (i > 0) {
                    this->space();
                    this->wordSpace(StringView("|"));
                }
                this->printPat(pats[i]);
            }
            this->end();
            break;
        }
    }
}

void State::printMac(const ASTPath& path, bool isBraced, bool isBracketed, const TokenTree& tokens) {
    const bool empty = tokens.isToken() ? false : tokens.size() == 0;
    if (isBraced) {
        this->cbox(INDENT_UNIT);
    }
    this->printPath(path, false);
    this->word(StringView("!"));
    if (isBraced) {
        this->nbsp();
        this->word(StringView("{"));
        const bool openSpace = !empty;
        if (openSpace) {
            this->space();
        }
        this->ibox(0);
        this->printTts(tokens);
        this->end();
        if (openSpace) {
            this->breakOffsetIfNotBol(1, -INDENT_UNIT);
        }
        this->word(StringView("}"));
        this->end();
        return;
    }
    this->word(isBracketed ? StringView("[") : StringView("("));
    this->ibox(0);
    this->printTts(tokens);
    this->end();
    this->word(isBracketed ? StringView("]") : StringView(")"));
}

void State::printLiteral(const ASTExprNode& node) {
    StringBuilder text;
    if (const auto* n = cast<const ASTExprNodeInteger>(&node)) {
        const StringView spelling(reinterpret_cast<const u8*>(n->spelling.c_str()), n->spelling.size());
        if (spelling.length() > 0 && (spelling.startsWith(StringView("'")) || spelling.startsWith(StringView("b'")))) {
            text << spelling;
        } else if (n->datatype == CORETYPE_CHAR) {
            appendCharLiteral(text, static_cast<u32>(n->value.truncateU64()));
        } else {
            if (spelling.length() > 0) {
                text << spelling;
            } else {
                text << n->value;
            }
            if (n->datatype != CORETYPE_ANY) {
                text << StringView(coretypeName(n->datatype));
            }
        }
    } else if (const auto* n = cast<const ASTExprNodeFloat>(&node)) {
        if (n->spelling != RcString()) {
            text << StringView(reinterpret_cast<const u8*>(n->spelling.c_str()), n->spelling.size());
        } else {
            auto value = formatFloatValueForToken(n->value);
            text << StringView(reinterpret_cast<const u8*>(value.data()), value.size());
        }
        if (n->datatype != CORETYPE_ANY) {
            text << StringView(coretypeName(n->datatype));
        }
    } else if (const auto* n = cast<const ASTExprNodeBool>(&node)) {
        text << (n->value ? StringView("true") : StringView("false"));
    } else if (const auto* n = cast<const ASTExprNodeString>(&node)) {
        if (n->spelling != RcString()) {
            text << StringView(reinterpret_cast<const u8*>(n->spelling.c_str()), n->spelling.size());
        } else {
            printEscapedLiteral(text, TOK_STRING, reinterpret_cast<const u8*>(n->value.data()), n->value.size());
        }
    } else if (const auto* n = cast<const ASTExprNodeByteString>(&node)) {
        if (n->spelling != RcString()) {
            text << StringView(reinterpret_cast<const u8*>(n->spelling.c_str()), n->spelling.size());
        } else {
            printEscapedLiteral(text, TOK_BYTESTRING, reinterpret_cast<const u8*>(n->value.data()), n->value.size());
        }
    } else if (const auto* n = cast<const ASTExprNodeCString>(&node)) {
        if (n->spelling != RcString()) {
            text << StringView(reinterpret_cast<const u8*>(n->spelling.c_str()), n->spelling.size());
        } else {
            printEscapedLiteral(text, TOK_CSTRING, reinterpret_cast<const u8*>(n->value.data()), n->value.size());
        }
    } else if (const auto* n = cast<const ASTExprNodeSuffixedLiteral>(&node)) {
        text << StringView(reinterpret_cast<const u8*>(n->text.data()), n->text.size());
    }
    this->word(StringView(text));
}

void State::printFallback(const ASTExprNode& node) {
    StringBuilder text;
    node.print(text);
    this->word(StringView(text));
}

void State::printExprCondParen(PExpr e, bool needsPar, Fixup fixup) {
    if (needsPar) {
        this->popen();
        fixup = Fixup();
    }
    this->printExpr(e, fixup);
    if (needsPar) {
        this->pclose();
    }
}

bool State::condNeedsPar(PExpr e) {
    switch (kindOf(e)) {
        case Kind::Break:
        case Kind::Closure:
        case Kind::Ret:
        case Kind::Yeet:
            return true;
        default:
            return containsExteriorStructLit(e);
    }
}

void State::printExprAsCond(PExpr e) {
    this->printExprCondParen(e, this->condNeedsPar(e), Fixup::newCond());
}

void State::commasepExprs(Breaks breaks, ASTExprNode* const* exprs, size_t count) {
    this->rbox(0, breaks);
    for (size_t i = 0; i < count; i++) {
        this->printExpr(nodeExpr(exprs[i]), Fixup());
        if (i + 1 < count) {
            this->word(StringView(","));
            this->spaceIfNotBol();
        }
    }
    this->end();
}

void State::printCallPost(ASTExprNode* const* args, size_t count) {
    this->popen();
    this->commasepExprs(Breaks::Inconsistent, args, count);
    this->pclose();
}

void State::printPathExpr(const ASTPath& path) {
    this->ibox(INDENT_UNIT);
    this->printPath(path, true);
    this->end();
}

void State::printLet(const ASTIfLetCondition& cond, Fixup fixup) {
    this->word(StringView("let "));
    this->printPat(*cond.optPat);
    this->space();
    this->wordSpace(StringView("="));
    const PExpr scrutinee = nodeExpr(cond.value);
    this->printExprCondParen(scrutinee, fixup.needsParAsLetScrutinee(scrutinee), Fixup());
}

void State::printExprBinary(PExpr e, Fixup fixup) {
    PExpr lhs;
    PExpr rhs;
    StringView text;
    Prec binopPrec;
    int fixity;
    bool canBegin;
    if (e.isChain()) {
        lhs = condExpr(e.conds, e.count - 1);
        rhs = condExpr(e.conds + e.count - 1, 1);
        binOpInfo(ASTExprNodeBinOp::BOOLAND, text, binopPrec, fixity, canBegin);
    } else {
        const auto* n = cast<const ASTExprNodeBinOp>(e.node);
        lhs = nodeExpr(n->left);
        rhs = nodeExpr(n->right);
        binOpInfo(n->type, text, binopPrec, fixity, canBegin);
    }
    const Fixup leftFixup = fixup.leftmostSubexpressionWithOperator(canBegin);
    const Prec leftPrec = leftFixup.precedence(lhs);
    const Prec rightPrec = fixup.precedence(rhs);
    bool leftNeedsParen;
    bool rightNeedsParen;
    if (fixity == 0) {
        leftNeedsParen = leftPrec < binopPrec;
        rightNeedsParen = rightPrec <= binopPrec;
    } else if (fixity == 1) {
        leftNeedsParen = leftPrec <= binopPrec;
        rightNeedsParen = rightPrec < binopPrec;
    } else {
        leftNeedsParen = leftPrec <= binopPrec;
        rightNeedsParen = rightPrec <= binopPrec;
    }
    const Kind leftKind = kindOf(lhs);
    if (leftKind == Kind::Cast && (text == StringView("<") || text == StringView("<<"))) {
        leftNeedsParen = true;
    }
    if (leftKind == Kind::Let && !letScrutineeNeedsPar(binopPrec)) {
        leftNeedsParen = true;
    }
    this->printExprCondParen(lhs, leftNeedsParen, leftFixup);
    this->space();
    this->wordSpace(text);
    this->printExprCondParen(rhs, rightNeedsParen, fixup.rightmostSubexpression());
}

void State::printExprStruct(const ASTPath& path, const ASTExprNodeStructLiteral::tValues& values, const ASTExprNode* base, bool hasRest) {
    this->printPath(path, true);
    this->nbsp();
    this->word(StringView("{"));
    if (values.empty() && !hasRest) {
        this->word(StringView("}"));
        return;
    }
    this->cbox(0);
    for (size_t i = 0; i < values.size(); i++) {
        const auto& field = values[i];
        const bool isFirst = i == 0;
        const bool isLast = i + 1 == values.size();
        if (isFirst) {
            this->spaceIfNotBol();
        }
        bool isShorthand = false;
        if (const auto* value = cast<const ASTExprNodeNamedValue>(field.value)) {
            isShorthand = field.value->parens() == 0 && value->path.isTrivial() && value->path.asTrivial() == field.name;
        }
        if (!isShorthand) {
            this->printIdent(field.name);
            this->wordNbsp(StringView(":"));
        }
        this->printExpr(nodeExpr(field.value), Fixup());
        if (!isLast || hasRest) {
            this->wordSpace(StringView(","));
        } else {
            this->trailingCommaOrSpace();
        }
    }
    if (hasRest) {
        if (values.empty()) {
            this->space();
        }
        this->word(StringView(".."));
        if (base) {
            this->printExpr(nodeExpr(base), Fixup());
        }
        this->space();
    }
    this->offset(-INDENT_UNIT);
    this->end();
    this->word(StringView("}"));
}

void State::printClosure(const ASTExprNodeClosure& closure) {
    this->printHigherRankedBounds(closure.hrbs);
    if (closure.isMove) {
        this->wordSpace(StringView("move"));
    }
    if (closure.isUse) {
        this->wordSpace(StringView("use"));
    }
    this->word(StringView("|"));
    this->rbox(0, Breaks::Inconsistent);
    for (size_t i = 0; i < closure.args.size(); i++) {
        if (i > 0) {
            this->wordSpace(StringView(","));
        }
        const auto& arg = closure.args[i];
        this->ibox(INDENT_UNIT);
        if (arg.second == nullptr || arg.second->isWildcard()) {
            this->printPat(arg.first);
        } else {
            this->printPat(arg.first);
            this->word(StringView(":"));
            this->space();
            this->printType(arg.second);
        }
        this->end();
    }
    this->end();
    this->word(StringView("|"));
    if (closure.returnType != nullptr && !closure.returnType->isWildcard()) {
        this->spaceIfNotBol();
        this->ibox(INDENT_UNIT);
        this->wordSpace(StringView("->"));
        this->printType(closure.returnType);
        this->end();
    }
    this->space();
    this->printExpr(nodeExpr(closure.code), Fixup());
}

void State::printStmt(const ASTExprNode* node, bool hasSemicolon) {
    const PExpr e = nodeExpr(node);
    if (const auto* let = cast<const ASTExprNodeLetBinding>(node)) {
        this->spaceIfNotBol();
        this->ibox(INDENT_UNIT);
        if (let->isSuper) {
            this->wordNbsp(StringView("super"));
        }
        this->wordNbsp(StringView("let"));
        this->ibox(INDENT_UNIT);
        this->printPat(let->pat);
        if (let->type != nullptr && !let->type->isWildcard()) {
            this->wordSpace(StringView(":"));
            this->printType(let->type);
        }
        this->end();
        if (let->value) {
            this->nbsp();
            this->wordSpace(StringView("="));
            const PExpr init = nodeExpr(let->value);
            bool trailingBrace = false;
            if (let->elseNode) {
                switch (kindOf(init)) {
                    case Kind::Gen:
                    case Kind::Block:
                    case Kind::ForLoop:
                    case Kind::If:
                    case Kind::Loop:
                    case Kind::Match:
                    case Kind::Struct:
                    case Kind::TryBlock:
                    case Kind::While:
                    case Kind::ConstBlock:
                        trailingBrace = true;
                        break;
                    case Kind::MacCall:
                        trailingBrace = cast<const ASTExprNodeMacro>(let->value)->isBraced;
                        break;
                    default:
                        break;
                }
            }
            this->printExprCondParen(init, trailingBrace, Fixup());
            if (let->elseNode) {
                this->cbox(INDENT_UNIT);
                this->ibox(INDENT_UNIT);
                this->word(StringView(" else "));
                this->printBlockNode(let->elseNode, true);
            }
        }
        this->word(StringView(";"));
        this->end();
        return;
    }
    if (kindOf(e) == Kind::MacCall) {
        const auto* mac = cast<const ASTExprNodeMacro>(node);
        this->spaceIfNotBol();
        this->printMac(mac->path, mac->isBraced, mac->isBracketed, mac->tokens);
        if (hasSemicolon) {
            this->word(StringView(";"));
        }
        return;
    }
    this->spaceIfNotBol();
    this->printExpr(e, Fixup::newStmt());
    if (hasSemicolon || exprRequiresSemiToBeStmt(e)) {
        this->word(StringView(";"));
    }
}

void State::printBlock(const ASTExprNodeBlock& block, bool hasCb) {
    if (block.blockType == ASTExprNodeBlock::Type::Unsafe) {
        this->wordSpace(StringView("unsafe"));
    }
    this->word(StringView("{"));
    this->end();
    for (size_t i = 0; i < block.nodes.size(); i++) {
        const auto& line = block.nodes[i];
        const bool isTail = i + 1 == block.nodes.size() && !line.hasSemicolon && !cast<const ASTExprNodeLetBinding>(line.node) && !cast<const ASTExprNodeMacro>(line.node);
        if (isTail) {
            this->spaceIfNotBol();
            this->printExpr(nodeExpr(line.node), Fixup::newStmt());
        } else {
            this->printStmt(line.node, line.hasSemicolon);
        }
    }
    const bool noSpace = block.nodes.empty();
    if (!noSpace) {
        this->breakOffsetIfNotBol(1, -INDENT_UNIT);
    }
    this->word(StringView("}"));
    if (hasCb) {
        this->end();
    }
}

void State::printBlockNode(const ASTExprNode* body, bool hasCb) {
    if (const auto* block = cast<const ASTExprNodeBlock>(body)) {
        if (body->parens() == 0) {
            this->printBlock(*block, hasCb);
            return;
        }
    }
    this->word(StringView("{"));
    this->end();
    this->spaceIfNotBol();
    this->printExpr(nodeExpr(body), Fixup::newStmt());
    this->breakOffsetIfNotBol(1, -INDENT_UNIT);
    this->word(StringView("}"));
    if (hasCb) {
        this->end();
    }
}

void State::printIf(const ASTExprNodeIf& node) {
    for (size_t i = 0; i < node.arms.size(); i++) {
        const auto& arm = node.arms[i];
        this->cbox(0);
        this->ibox(0);
        if (i == 0) {
            this->wordNbsp(StringView("if"));
        } else {
            this->word(StringView(" else if "));
        }
        this->printExprAsCond(condExpr(arm.conditions.data(), arm.conditions.size()));
        this->space();
        this->printBlockNode(arm.body, true);
    }
    if (node.elseNode) {
        this->cbox(0);
        this->ibox(0);
        this->word(StringView(" else "));
        this->printBlockNode(node.elseNode, true);
    }
}

void State::printArm(const ASTExprNodeMatchArm& arm) {
    this->space();
    this->cbox(INDENT_UNIT);
    this->ibox(0);
    if (arm.patterns.size() == 1) {
        this->printPat(arm.patterns[0]);
    } else {
        this->rbox(0, Breaks::Inconsistent);
        for (size_t i = 0; i < arm.patterns.size(); i++) {
            if (i > 0) {
                this->space();
                this->wordSpace(StringView("|"));
            }
            this->printPat(arm.patterns[i]);
        }
        this->end();
    }
    this->space();
    if (!arm.guard.empty()) {
        this->wordSpace(StringView("if"));
        this->printExpr(condExpr(arm.guard.data(), arm.guard.size()), Fixup());
        this->space();
    }
    this->wordSpace(StringView("=>"));
    const auto* block = cast<const ASTExprNodeBlock>(arm.code);
    if (block && arm.code->parens() == 0 && block->blockType != ASTExprNodeBlock::Type::Const) {
        if (block->label.name != "") {
            this->printLabel(block->label);
            this->wordSpace(StringView(":"));
        }
        this->printBlock(*block, false);
        if (block->blockType == ASTExprNodeBlock::Type::Unsafe) {
            this->word(StringView(","));
        }
    } else {
        this->end();
        this->printExpr(nodeExpr(arm.code), Fixup::newMatchArm());
        this->word(StringView(","));
    }
    this->end();
}

void State::printExpr(PExpr e, Fixup fixup) {
    this->ibox(INDENT_UNIT);
    const bool needsPar = fixup.wouldCauseStatementBoundary(e);
    if (needsPar) {
        this->popen();
        fixup = Fixup();
    }
    this->printExprKind(e, kindOf(e), fixup);
    if (needsPar) {
        this->pclose();
    }
    this->end();
}

void State::printExprKind(PExpr e, Kind kind, Fixup fixup) {
    const ASTExprNode* node = e.node;
    switch (kind) {
        case Kind::Paren:
            this->popen();
            this->printExpr(unparen(e), Fixup());
            this->pclose();
            break;
        case Kind::Let:
            this->printLet(e.conds[0], fixup);
            break;
        case Kind::Binary:
            this->printExprBinary(e, fixup);
            break;
        case Kind::Array: {
            const auto* n = cast<const ASTExprNodeArray>(node);
            this->ibox(INDENT_UNIT);
            this->word(StringView("["));
            this->commasepExprs(Breaks::Inconsistent, n->values.data(), n->values.size());
            this->word(StringView("]"));
            this->end();
            break;
        }
        case Kind::Repeat: {
            const auto* n = cast<const ASTExprNodeArray>(node);
            this->ibox(INDENT_UNIT);
            this->word(StringView("["));
            this->printExpr(nodeExpr(n->values[0]), Fixup());
            this->wordSpace(StringView(";"));
            this->printExpr(nodeExpr(n->size), Fixup());
            this->word(StringView("]"));
            this->end();
            break;
        }
        case Kind::ConstBlock: {
            const auto* n = cast<const ASTExprNodeBlock>(node);
            this->ibox(INDENT_UNIT);
            this->word(StringView("const"));
            this->nbsp();
            this->cbox(0);
            this->ibox(0);
            this->printBlock(*n, true);
            this->end();
            break;
        }
        case Kind::Struct:
            if (const auto* n = cast<const ASTExprNodeStructLiteral>(node)) {
                this->printExprStruct(n->path, n->values, n->baseValue, n->baseValue != nullptr);
            } else {
                const auto* p = cast<const ASTExprNodeStructLiteralPattern>(node);
                this->printExprStruct(p->path, p->values, nullptr, false);
            }
            break;
        case Kind::Tup: {
            const auto* n = cast<const ASTExprNodeTuple>(node);
            this->popen();
            this->commasepExprs(Breaks::Inconsistent, n->values.data(), n->values.size());
            if (n->values.size() == 1) {
                this->word(StringView(","));
            }
            this->pclose();
            break;
        }
        case Kind::Call: {
            if (const auto* n = cast<const ASTExprNodeCallPath>(node)) {
                this->printPathExpr(n->path);
                this->printCallPost(n->args.data(), n->args.size());
            } else {
                const auto* o = cast<const ASTExprNodeCallObject>(node);
                const Fixup funcFixup = fixup.leftmostSubexpressionWithOperator(true);
                const PExpr func = nodeExpr(o->val);
                bool needsParen;
                if (kindOf(func) == Kind::Field) {
                    const auto& name = cast<const ASTExprNodeField>(o->val)->name;
                    needsParen = !(name.size() > 0 && name.c_str()[0] >= '0' && name.c_str()[0] <= '9');
                } else {
                    needsParen = funcFixup.precedence(func) < Prec::Unambiguous;
                }
                this->printExprCondParen(func, needsParen, funcFixup);
                this->printCallPost(o->args.data(), o->args.size());
            }
            break;
        }
        case Kind::MethodCall: {
            const auto* n = cast<const ASTExprNodeCallMethod>(node);
            const PExpr receiver = nodeExpr(n->val);
            this->printExprCondParen(receiver, precedenceOf(receiver) < Prec::Unambiguous, fixup.leftmostSubexpressionWithDot());
            this->word(StringView("."));
            this->printIdent(n->method.name());
            if (!n->method.args().isEmpty()) {
                this->printGenericArgs(n->method.args(), true);
            }
            this->printCallPost(n->args.data(), n->args.size());
            break;
        }
        case Kind::Unary: {
            const ASTExprNode* operand;
            if (const auto* d = cast<const ASTExprNodeDeref>(node)) {
                this->word(StringView("*"));
                operand = d->value;
            } else {
                const auto* u = cast<const ASTExprNodeUniOp>(node);
                switch (u->type) {
                    case ASTExprNodeUniOp::BOX:
                        this->word(StringView("box "));
                        break;
                    case ASTExprNodeUniOp::INVERT:
                        this->word(StringView("!"));
                        break;
                    default:
                        this->word(StringView("-"));
                        break;
                }
                operand = u->value;
            }
            const PExpr inner = nodeExpr(operand);
            this->printExprCondParen(inner, fixup.precedence(inner) < Prec::Prefix, fixup.rightmostSubexpression());
            break;
        }
        case Kind::AddrOf: {
            const auto* u = cast<const ASTExprNodeUniOp>(node);
            this->word(StringView("&"));
            switch (u->type) {
                case ASTExprNodeUniOp::REF:
                    break;
                case ASTExprNodeUniOp::REFMUT:
                    this->printMutability(true, false);
                    break;
                case ASTExprNodeUniOp::RawBorrow:
                    this->wordNbsp(StringView("raw"));
                    this->printMutability(false, true);
                    break;
                case ASTExprNodeUniOp::RawBorrowMut:
                    this->wordNbsp(StringView("raw"));
                    this->printMutability(true, true);
                    break;
                case ASTExprNodeUniOp::PinBorrow:
                    this->wordNbsp(StringView("pin"));
                    this->printMutability(false, true);
                    break;
                default:
                    this->wordNbsp(StringView("pin"));
                    this->printMutability(true, true);
                    break;
            }
            const PExpr inner = nodeExpr(u->value);
            this->printExprCondParen(inner, fixup.precedence(inner) < Prec::Prefix, fixup.rightmostSubexpression());
            break;
        }
        case Kind::Lit:
            this->printLiteral(*node);
            break;
        case Kind::Cast: {
            const auto* n = cast<const ASTExprNodeCast>(node);
            const PExpr inner = nodeExpr(n->value);
            this->printExprCondParen(inner, precedenceOf(inner) < Prec::Cast, fixup.leftmostSubexpression());
            this->space();
            this->wordSpace(StringView("as"));
            this->printType(n->type);
            break;
        }
        case Kind::Type: {
            const auto* n = cast<const ASTExprNodeTypeAnnotation>(node);
            this->word(StringView("builtin # type_ascribe"));
            this->popen();
            this->ibox(0);
            this->printExpr(nodeExpr(n->value), Fixup());
            this->word(StringView(","));
            this->spaceIfNotBol();
            this->printType(n->type);
            this->end();
            this->pclose();
            break;
        }
        case Kind::If:
            this->printIf(*cast<const ASTExprNodeIf>(node));
            break;
        case Kind::While: {
            const auto* n = cast<const ASTExprNodeWhile>(node);
            if (n->label.name != "") {
                this->printLabel(n->label);
                this->wordSpace(StringView(":"));
            }
            this->cbox(0);
            this->ibox(0);
            this->wordNbsp(StringView("while"));
            this->printExprAsCond(condExpr(n->conditions.data(), n->conditions.size()));
            this->space();
            this->printBlockNode(n->code, true);
            break;
        }
        case Kind::ForLoop: {
            const auto* n = cast<const ASTExprNodeFor>(node);
            if (n->label.name != "") {
                this->printLabel(n->label);
                this->wordSpace(StringView(":"));
            }
            this->cbox(0);
            this->ibox(0);
            this->wordNbsp(StringView("for"));
            if (n->isAwait) {
                this->wordNbsp(StringView("await"));
            }
            this->printPat(n->pattern);
            this->space();
            this->wordSpace(StringView("in"));
            this->printExprAsCond(nodeExpr(n->value));
            this->space();
            this->printBlockNode(n->code, true);
            break;
        }
        case Kind::Loop: {
            const auto* n = cast<const ASTExprNodeLoop>(node);
            this->cbox(0);
            this->ibox(0);
            if (n->label.name != "") {
                this->printLabel(n->label);
                this->wordSpace(StringView(":"));
            }
            this->wordNbsp(StringView("loop"));
            this->printBlockNode(n->code, true);
            break;
        }
        case Kind::Match: {
            const auto* n = cast<const ASTExprNodeMatch>(node);
            this->cbox(0);
            this->ibox(0);
            this->wordNbsp(StringView("match"));
            this->printExprAsCond(nodeExpr(n->val));
            this->space();
            this->word(StringView("{"));
            this->end();
            for (const auto& arm : n->arms) {
                this->printArm(arm);
            }
            if (!n->arms.empty()) {
                this->breakOffsetIfNotBol(1, -INDENT_UNIT);
            }
            this->word(StringView("}"));
            this->end();
            break;
        }
        case Kind::Closure:
            this->printClosure(*cast<const ASTExprNodeClosure>(node));
            break;
        case Kind::Block: {
            const auto* n = cast<const ASTExprNodeBlock>(node);
            if (n->label.name != "") {
                this->printLabel(n->label);
                this->wordSpace(StringView(":"));
            }
            this->cbox(0);
            this->ibox(0);
            this->printBlock(*n, true);
            break;
        }
        case Kind::Gen: {
            const ASTExprNode* inner;
            bool isMove;
            bool isUse = false;
            if (const auto* a = cast<const ASTExprNodeAsyncBlock>(node)) {
                this->wordNbsp(StringView("async"));
                inner = a->inner;
                isMove = a->isMove;
                isUse = a->isUse;
            } else {
                const auto* g = cast<const ASTExprNodeGeneratorBlock>(node);
                this->wordNbsp(g->isAsync ? StringView("async gen") : StringView("gen"));
                inner = g->inner;
                isMove = g->isMove;
            }
            if (isMove) {
                this->wordSpace(StringView("move"));
            }
            if (isUse) {
                this->wordSpace(StringView("use"));
            }
            this->cbox(0);
            this->ibox(0);
            this->printBlockNode(inner, true);
            break;
        }
        case Kind::Await:
        case Kind::Use:
        case Kind::Try: {
            const auto* u = cast<const ASTExprNodeUniOp>(node);
            const PExpr inner = nodeExpr(u->value);
            this->printExprCondParen(inner, precedenceOf(inner) < Prec::Unambiguous, kind == Kind::Use ? fixup : fixup.leftmostSubexpressionWithDot());
            this->word(kind == Kind::Await ? StringView(".await") : kind == Kind::Use ? StringView(".use") : StringView("?"));
            break;
        }
        case Kind::TryBlock: {
            const auto* n = cast<const ASTExprNodeTry>(node);
            this->cbox(0);
            this->ibox(0);
            this->wordNbsp(StringView("try"));
            this->printBlockNode(n->inner, true);
            break;
        }
        case Kind::Assign:
        case Kind::AssignOp: {
            const auto* n = cast<const ASTExprNodeAssign>(node);
            const PExpr lhs = nodeExpr(n->slot);
            const PExpr rhs = nodeExpr(n->value);
            this->printExprCondParen(lhs, precedenceOf(lhs) <= Prec::Range, fixup.leftmostSubexpression());
            this->space();
            this->wordSpace(assignOpText(n->op));
            this->printExprCondParen(rhs, fixup.precedence(rhs) < Prec::Assign, fixup.rightmostSubexpression());
            break;
        }
        case Kind::Field: {
            const auto* n = cast<const ASTExprNodeField>(node);
            const PExpr inner = nodeExpr(n->obj);
            this->printExprCondParen(inner, precedenceOf(inner) < Prec::Unambiguous, fixup.leftmostSubexpressionWithDot());
            this->word(StringView("."));
            this->printIdent(n->name);
            break;
        }
        case Kind::Index: {
            const auto* n = cast<const ASTExprNodeIndex>(node);
            const Fixup exprFixup = fixup.leftmostSubexpressionWithOperator(true);
            const PExpr inner = nodeExpr(n->obj);
            this->printExprCondParen(inner, exprFixup.precedence(inner) < Prec::Unambiguous, exprFixup);
            this->word(StringView("["));
            this->printExpr(nodeExpr(n->idx), Fixup());
            this->word(StringView("]"));
            break;
        }
        case Kind::Range: {
            const auto* n = cast<const ASTExprNodeBinOp>(node);
            if (n->left) {
                const Fixup startFixup = fixup.leftmostSubexpressionWithOperator(true);
                const PExpr start = nodeExpr(n->left);
                this->printExprCondParen(start, startFixup.precedence(start) < Prec::LOr, startFixup);
            }
            this->word(n->type == ASTExprNodeBinOp::RANGE_INC ? StringView("..=") : StringView(".."));
            if (n->right) {
                const PExpr finish = nodeExpr(n->right);
                this->printExprCondParen(finish, fixup.precedence(finish) < Prec::LOr, fixup.rightmostSubexpression());
            }
            break;
        }
        case Kind::Underscore:
            this->word(StringView("_"));
            break;
        case Kind::Path:
            this->printPath(cast<const ASTExprNodeNamedValue>(node)->path, true);
            break;
        case Kind::Break: {
            const auto* n = cast<const ASTExprNodeFlow>(node);
            this->word(StringView("break"));
            if (n->target.name != "") {
                this->space();
                this->printLabel(n->target);
            }
            if (n->value) {
                this->space();
                const PExpr value = nodeExpr(n->value);
                this->printExprCondParen(value, n->target.name == "" && leadingLabeledExpr(value), fixup.rightmostSubexpression());
            }
            break;
        }
        case Kind::Continue: {
            const auto* n = cast<const ASTExprNodeFlow>(node);
            this->word(StringView("continue"));
            if (n->target.name != "") {
                this->space();
                this->printLabel(n->target);
            }
            break;
        }
        case Kind::Ret:
        case Kind::Yeet:
        case Kind::Become: {
            const auto* n = cast<const ASTExprNodeFlow>(node);
            if (kind == Kind::Ret) {
                this->word(StringView("return"));
            } else if (kind == Kind::Yeet) {
                this->word(StringView("do"));
                this->word(StringView(" "));
                this->word(StringView("yeet"));
            } else {
                this->word(StringView("become"));
            }
            if (n->value) {
                this->word(StringView(" "));
                this->printExpr(nodeExpr(n->value), fixup.rightmostSubexpression());
            }
            break;
        }
        case Kind::Yield: {
            const auto* n = cast<const ASTExprNodeFlow>(node);
            this->word(StringView("yield"));
            if (n->value) {
                this->space();
                this->printExpr(nodeExpr(n->value), fixup.rightmostSubexpression());
            }
            break;
        }
        case Kind::MacCall: {
            const auto* n = cast<const ASTExprNodeMacro>(node);
            this->printMac(n->path, n->isBraced, n->isBracketed, n->tokens);
            break;
        }
        case Kind::LetStmt:
            this->printStmt(node, false);
            break;
        case Kind::InlineAsm:
        case Kind::Other:
            this->printFallback(*node);
            break;
    }
}

void pprustTtsToString(ZeroCopyOutput& out, const TokenTree& tts) {
    State state(out);
    state.printTts(tts);
    state.eof();
}

void pprustExprToString(ZeroCopyOutput& out, const ASTExprNode& expr) {
    State state(out);
    state.printExpr(nodeExpr(&expr), Fixup());
    state.eof();
}
