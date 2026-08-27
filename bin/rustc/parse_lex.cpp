#include "parse_lex.h"

#include <std/str/view.h>
#include "wire_board.h"

#include "common.h"
#include "parse_tokentree.h"
#include "parse_parseerror.h"
#include "unicode_nfc.h"

#include <cctype>
#include <limits> // std::numeric_limits
#include <cassert>
#include <cstdlib> // strtol
#include <iostream>
#include <typeinfo>
#include <algorithm> // std::count

//#define TRACE_CHARS
//#define TRACE_RAW_TOKENS

Lexer::Lexer(HygieneContext& hygieneContext, stl::ObjPool& pool, const ::std::string& filename, ASTEdition edition, ParseState ps)
    : TokenStream(ps)
    , hygieneContext(hygieneContext)
    , path_(filename.c_str())
    , line(1)
    , lineOfs(0)
    , istreamFp(filename != "-" ? new std::ifstream(filename.c_str()) : nullptr)
    , istream(filename != "-" ? *istreamFp : std::cin)
    , lastCharValid(false)
    , initialShebangChecked(false)
    , initialFrontmatterAllowed(true)
    , initialFrontmatterPrecededByWhitespace(false)
    , replayCharOffset(0)
    , edition(edition)
    , hygiene_(Ident::Hygiene::newScope(hygieneContext, pool))
{
    if (istreamFp) {
        if (!istreamFp->is_open()) {
            throw ::std::runtime_error("Unable to open file '" + filename + "'");
        }
        // Consume the BOM
        if (this->getcByte() == '\xef') {
            if (this->getcByte() != '\xbb') {
                throw ::std::runtime_error("Incomplete BOM - missing \\xBB in second position");
            }
            if (this->getcByte() != '\xbf') {
                throw ::std::runtime_error("Incomplete BOM - missing \\xBF in third position");
            }
            lineOfs = 0;
        } else {
            istream.unget();
        }
    }
}

Lexer::Lexer(HygieneContext& hygieneContext, stl::ObjPool& pool, ::std::istringstream& ss, ASTEdition edition, ParseState ps)
    : TokenStream(ps)
    , hygieneContext(hygieneContext)
    , path_("-")
    , line(1)
    , lineOfs(0)
    , istreamFp(nullptr)
    , istream(ss)
    , lastCharValid(false)
    , initialShebangChecked(false)
    , initialFrontmatterAllowed(true)
    , initialFrontmatterPrecededByWhitespace(false)
    , replayCharOffset(0)
    , edition(edition)
    , hygiene_(Ident::Hygiene::newScope(hygieneContext, pool))
{
}

#define LINECOMMENT -1
#define BLOCKCOMMENT -2
#define SINGLEQUOTE -3
#define DOUBLEQUOTE -4
#define SHEBANG -5

// NOTE: This array must be kept sorted, or symbols are will be skipped
#define TOKENT(str, sym) {sizeof(str) - 1, str, sym}

static const struct {
    unsigned char len;
    const char* chars;
    signed int type;
} TOKENMAP[] = {
    TOKENT("!", TOK_EXCLAM),
    TOKENT("!=", TOK_EXCLAM_EQUAL),
    TOKENT("\"", DOUBLEQUOTE),
    TOKENT("#", TOK_HASH),
    TOKENT("$", TOK_DOLLAR),
    TOKENT("%", TOK_PERCENT),
    TOKENT("%=", TOK_PERCENT_EQUAL),
    TOKENT("&", TOK_AMP),
    TOKENT("&&", TOK_DOUBLE_AMP),
    TOKENT("&=", TOK_AMP_EQUAL),
    TOKENT("'", SINGLEQUOTE),
    TOKENT("(", TOK_PAREN_OPEN),
    TOKENT(")", TOK_PAREN_CLOSE),
    TOKENT("*", TOK_STAR),
    TOKENT("*=", TOK_STAR_EQUAL),
    TOKENT("+", TOK_PLUS),
    TOKENT("+=", TOK_PLUS_EQUAL),
    TOKENT(",", TOK_COMMA),
    TOKENT("-", TOK_DASH),
    TOKENT("-=", TOK_DASH_EQUAL),
    TOKENT("->", TOK_THINARROW),
    TOKENT(".", TOK_DOT),
    // NOTE: These have special handling when following numbers
    TOKENT("..", TOK_DOUBLE_DOT),
    TOKENT("...", TOK_TRIPLE_DOT),
    TOKENT("..=", TOK_DOUBLE_DOT_EQUAL),
    TOKENT("/", TOK_SLASH),
    TOKENT("/*", BLOCKCOMMENT),
    TOKENT("//", LINECOMMENT),
    TOKENT("/=", TOK_SLASH_EQUAL),
    // 0-9 :: Elsewhere
    TOKENT(":", TOK_COLON),
    TOKENT("::", TOK_DOUBLE_COLON),
    TOKENT(";", TOK_SEMICOLON),
    TOKENT("<", TOK_LT),
    TOKENT("<-", TOK_THINARROW_LEFT),
    TOKENT("<<", TOK_DOUBLE_LT),
    TOKENT("<<=", TOK_DOUBLE_LT_EQUAL),
    TOKENT("<=", TOK_LTE),
    TOKENT("=", TOK_EQUAL),
    TOKENT("==", TOK_DOUBLE_EQUAL),
    TOKENT("=>", TOK_FATARROW),
    TOKENT(">", TOK_GT),
    TOKENT(">=", TOK_GTE),
    TOKENT(">>", TOK_DOUBLE_GT),
    TOKENT(">>=", TOK_DOUBLE_GT_EQUAL),
    TOKENT("?", TOK_QMARK),
    TOKENT("@", TOK_AT),
    // A-Z :: Elsewhere
    TOKENT("[", TOK_SQUARE_OPEN),
    TOKENT("\\", TOK_BACKSLASH),
    TOKENT("]", TOK_SQUARE_CLOSE),
    TOKENT("^", TOK_CARET),
    TOKENT("^=", TOK_CARET_EQUAL),
    TOKENT("`", TOK_BACKTICK),
    // a-z :: Elsewhere
    //TOKENT("b\"", DOUBLEQUOTE),

    TOKENT("{", TOK_BRACE_OPEN),
    TOKENT("|", TOK_PIPE),
    TOKENT("|=", TOK_PIPE_EQUAL),
    TOKENT("||", TOK_DOUBLE_PIPE),
    TOKENT("}", TOK_BRACE_CLOSE),
    TOKENT("~", TOK_TILDE),
};

#define LEN(arr) (sizeof(arr) / sizeof(arr[0]))

struct sRWORD {
    unsigned char len;
    const char* chars;
    signed int type;
};

static const sRWORD RWORDS_2015[] = {
    TOKENT("_", TOK_UNDERSCORE),
    TOKENT("abstract", TOK_RWORD_ABSTRACT), // Reserved 2015+
    TOKENT("as", TOK_RWORD_AS),
    TOKENT("become", TOK_RWORD_BECOME), //  // Reserved 2015+
    TOKENT("box", TOK_RWORD_BOX),
    TOKENT("break", TOK_RWORD_BREAK),
    TOKENT("const", TOK_RWORD_CONST),
    TOKENT("continue", TOK_RWORD_CONTINUE),
    TOKENT("crate", TOK_RWORD_CRATE),
    TOKENT("do", TOK_RWORD_DO), // Reserved 2015+
    TOKENT("else", TOK_RWORD_ELSE),
    TOKENT("enum", TOK_RWORD_ENUM),
    TOKENT("extern", TOK_RWORD_EXTERN),
    TOKENT("false", TOK_RWORD_FALSE),
    TOKENT("final", TOK_RWORD_FINAL), // Reserved 2015+
    TOKENT("fn", TOK_RWORD_FN),
    TOKENT("for", TOK_RWORD_FOR),
    TOKENT("if", TOK_RWORD_IF),
    TOKENT("impl", TOK_RWORD_IMPL),
    TOKENT("in", TOK_RWORD_IN),
    TOKENT("let", TOK_RWORD_LET),
    TOKENT("loop", TOK_RWORD_LOOP),
    TOKENT("macro", TOK_RWORD_MACRO),
    TOKENT("match", TOK_RWORD_MATCH),
    TOKENT("mod", TOK_RWORD_MOD),
    TOKENT("move", TOK_RWORD_MOVE),
    TOKENT("mut", TOK_RWORD_MUT),
    TOKENT("override", TOK_RWORD_OVERRIDE), // Reserved 2015+
    TOKENT("priv", TOK_RWORD_PRIV),
    TOKENT("pub", TOK_RWORD_PUB),
    TOKENT("ref", TOK_RWORD_REF),
    TOKENT("return", TOK_RWORD_RETURN),
    TOKENT("self", TOK_RWORD_SELF),
    TOKENT("static", TOK_RWORD_STATIC),
    TOKENT("struct", TOK_RWORD_STRUCT),
    TOKENT("super", TOK_RWORD_SUPER),
    TOKENT("trait", TOK_RWORD_TRAIT),
    TOKENT("true", TOK_RWORD_TRUE),
    TOKENT("type", TOK_RWORD_TYPE),
    TOKENT("typeof", TOK_RWORD_TYPEOF), // Reserved 2015+
    TOKENT("unsafe", TOK_RWORD_UNSAFE),
    TOKENT("unsized", TOK_RWORD_UNSIZED), // Reserved 2015+
    TOKENT("use", TOK_RWORD_USE),
    TOKENT("virtual", TOK_RWORD_VIRTUAL), // Reserved 2015+
    TOKENT("where", TOK_RWORD_WHERE),
    TOKENT("while", TOK_RWORD_WHILE),
    TOKENT("yield", TOK_RWORD_YIELD), // Reserved 2015+
};

static const sRWORD RWORDS_2018[] = {
    TOKENT("_", TOK_UNDERSCORE),
    TOKENT("abstract", TOK_RWORD_ABSTRACT), // Reserved 2015+
    TOKENT("as", TOK_RWORD_AS),
    TOKENT("async", TOK_RWORD_ASYNC),   // Added 2018
    TOKENT("await", TOK_RWORD_AWAIT),   // Added 2018
    TOKENT("become", TOK_RWORD_BECOME), //  // Reserved 2015+
    TOKENT("box", TOK_RWORD_BOX),
    TOKENT("break", TOK_RWORD_BREAK),
    TOKENT("const", TOK_RWORD_CONST),
    TOKENT("continue", TOK_RWORD_CONTINUE),
    TOKENT("crate", TOK_RWORD_CRATE),
    TOKENT("do", TOK_RWORD_DO),   // Reserved 2015+
    TOKENT("dyn", TOK_RWORD_DYN), // Added 2018
    TOKENT("else", TOK_RWORD_ELSE),
    TOKENT("enum", TOK_RWORD_ENUM),
    TOKENT("extern", TOK_RWORD_EXTERN),
    TOKENT("false", TOK_RWORD_FALSE),
    TOKENT("final", TOK_RWORD_FINAL), // Reserved 2015+
    TOKENT("fn", TOK_RWORD_FN),
    TOKENT("for", TOK_RWORD_FOR),
    TOKENT("if", TOK_RWORD_IF),
    TOKENT("impl", TOK_RWORD_IMPL),
    TOKENT("in", TOK_RWORD_IN),
    TOKENT("let", TOK_RWORD_LET),
    TOKENT("loop", TOK_RWORD_LOOP),
    TOKENT("macro", TOK_RWORD_MACRO),
    TOKENT("match", TOK_RWORD_MATCH),
    TOKENT("mod", TOK_RWORD_MOD),
    TOKENT("move", TOK_RWORD_MOVE),
    TOKENT("mut", TOK_RWORD_MUT),
    TOKENT("override", TOK_RWORD_OVERRIDE), // Reserved 2015+
    TOKENT("priv", TOK_RWORD_PRIV),
    TOKENT("pub", TOK_RWORD_PUB),
    TOKENT("ref", TOK_RWORD_REF),
    TOKENT("return", TOK_RWORD_RETURN),
    TOKENT("self", TOK_RWORD_SELF),
    TOKENT("static", TOK_RWORD_STATIC),
    TOKENT("struct", TOK_RWORD_STRUCT),
    TOKENT("super", TOK_RWORD_SUPER),
    TOKENT("trait", TOK_RWORD_TRAIT),
    TOKENT("true", TOK_RWORD_TRUE),
    TOKENT("try", TOK_RWORD_TRY), // Reserved 2018+
    TOKENT("type", TOK_RWORD_TYPE),
    TOKENT("typeof", TOK_RWORD_TYPEOF), // Reserved 2015+
    TOKENT("unsafe", TOK_RWORD_UNSAFE),
    TOKENT("unsized", TOK_RWORD_UNSIZED), // Reserved 2015+
    TOKENT("use", TOK_RWORD_USE),
    TOKENT("virtual", TOK_RWORD_VIRTUAL), // Reserved 2015+
    TOKENT("where", TOK_RWORD_WHERE),
    TOKENT("while", TOK_RWORD_WHILE),
    TOKENT("yield", TOK_RWORD_YIELD), // Reserved 2015+
};

signed int Lexer::getSymbol() {
    Codepoint ch = this->getc();
    // 1. lsearch for character
    // 2. Consume as many characters as currently match
    // 3. IF: a smaller character or, EOS is hit - Return current best
    unsigned ofs = 0;
    signed int best = 0;
    bool hitEof = false;
    for (unsigned i = 0; i < LEN(TOKENMAP); i++) {
        const char* const chars = TOKENMAP[i].chars;
        const size_t len = TOKENMAP[i].len;

        if (ofs >= len || static_cast<u32>(chars[ofs]) > ch.v) {
            break;
        }

        while (chars[ofs] && ch == chars[ofs]) {
            try {
                ch = this->getc();
            } catch (Lexer::EndOfFile) {
                ch = 0;
                // Prevent `ungetc` if EOF was hit
                hitEof = true;
            }
            ofs++;
        }
        if (chars[ofs] == 0) {
            best = TOKENMAP[i].type;
        }
    }

    if (!hitEof) {
        this->ungetc();
    }
    return best;
}

bool issym(Codepoint ch) {
    if ('0' <= ch.v && ch.v <= '9') {
        return true;
    }
    if (::std::isalpha(ch.v)) {
        return true;
    }
    if (ch == '_') {
        return true;
    }
    if (ch.v >= 128) {
        return !ch.isspace();
    }
    return false;
}

Token Lexer::withLiteralSuffix(Token tok) {
    Codepoint ch;
    try {
        ch = this->getc();
    } catch (const Lexer::EndOfFile&) {
        return tok;
    }
    if (ch.isdigit() || !issym(ch)) {
        this->ungetc();
        return tok;
    }

    ::std::string suffix;
    do {
        suffix += ch;
        try {
            ch = this->getc();
        } catch (const Lexer::EndOfFile&) {
            return Token(TOK_LITERAL_SUFFIXED, tok.toStr() + suffix, this->realGetHygiene());
        }
    } while (issym(ch));
    this->ungetc();
    return Token(TOK_LITERAL_SUFFIXED, tok.toStr() + suffix, this->realGetHygiene());
}

Position Lexer::getPosition() const {
    return Position(path_, line, lineOfs);
}

Ident::Hygiene Lexer::realGetHygiene() const {
    return hygiene_;
}

Token Lexer::realGetToken() {
    while (true) {
        auto tokenPosition = this->getPosition();
        // Whitespace scanning leaves the first non-whitespace codepoint in
        // the one-character cache after advancing the source offset.
        if (lastCharValid && tokenPosition.ofs > 0) {
            tokenPosition.ofs--;
        }
        Token tok = getTokenInt();
#ifdef TRACE_RAW_TOKENS
        ::std::cout << "getTokenInt: tok = " << tok << ::std::endl;
#endif
        switch (tok.type()) {
            case TOK_NEWLINE:
                continue;
            case TOK_WHITESPACE:
                continue;
            case TOK_COMMENT: {
                continue;
            }
            default:
                if (tok.getPos().filename == "" && !tok.getPos().span) {
                    tok.setPos(std::move(tokenPosition));
                }
                return tok;
        }
    }
}

void Lexer::checkInitialShebang() {
    initialShebangChecked = true;

    const auto initialLine = line;
    const auto initialLineOfs = lineOfs;
    ::std::vector<Codepoint> consumed;
    bool eof = false;
    auto read = [&]() {
        try {
            auto ch = this->getc();
            consumed.push_back(ch);
            return ch;
        } catch (const Lexer::EndOfFile&) {
            eof = true;
            return Codepoint();
        }
    };
    auto restore = [&](size_t start) {
        line = initialLine;
        lineOfs = initialLineOfs;
        lastCharValid = false;
        replayChars = ::std::move(consumed);
        replayCharOffset = start;
    };

    Codepoint first;
    try {
        first = this->getc();
    } catch (const Lexer::EndOfFile&) {
        return;
    }
    if (first != '#') {
        this->ungetc();
        return;
    }
    consumed.push_back(first);
    if (read() != '!' || eof) {
        restore(0);
        return;
    }

    enum class Classification {
        InnerAttribute,
        Shebang,
    };
    auto classification = Classification::Shebang;
    while (!eof) {
        auto ch = read();
        if (eof) {
            break;
        }
        if (ch.isspace()) {
            continue;
        }
        if (ch == '[') {
            classification = Classification::InnerAttribute;
            break;
        }
        if (ch != '/') {
            break;
        }

        const auto commentStart = consumed.size() - 1;
        ch = read();
        if (eof || (ch != '/' && ch != '*')) {
            break;
        }
        const bool lineComment = ch == '/';

        auto third = read();
        bool docComment = false;
        if (!eof) {
            if (lineComment) {
                if (third == '!') {
                    docComment = true;
                } else if (third == '/') {
                    auto fourth = read();
                    docComment = eof || fourth != '/';
                }
            } else if (third == '!') {
                docComment = true;
            } else if (third == '*') {
                auto fourth = read();
                docComment = eof || (fourth != '*' && fourth != '/');
            }
        }
        if (docComment) {
            break;
        }

        if (lineComment) {
            while (!eof && consumed.back() != '\n') {
                read();
            }
            continue;
        }

        unsigned depth = 1;
        Codepoint previous;
        size_t index = commentStart + 2;
        while (depth > 0 && !eof) {
            Codepoint current;
            if (index < consumed.size()) {
                current = consumed[index++];
            } else {
                current = read();
                index = consumed.size();
            }
            if (eof) {
                break;
            }
            if (previous == '/' && current == '*') {
                depth += 1;
                previous = Codepoint();
            } else if (previous == '*' && current == '/') {
                depth -= 1;
                previous = Codepoint();
            } else {
                previous = current;
            }
        }
    }

    if (classification == Classification::InnerAttribute) {
        restore(0);
        return;
    }

    auto replayStart = consumed.size();
    bool foundNewline = false;
    for (size_t i = 0; i < consumed.size(); i += 1) {
        if (consumed[i] == '\n' || consumed[i] == '\r') {
            replayStart = i;
            foundNewline = true;
            break;
        }
    }
    while (!foundNewline && !eof) {
        auto ch = read();
        if (!eof && (ch == '\n' || ch == '\r')) {
            replayStart = consumed.size() - 1;
            foundNewline = true;
        }
    }
    if (!foundNewline) {
        replayStart = consumed.size();
    }
    restore(replayStart);
}

bool Lexer::trySkipInitialFrontmatter() {
    if (!initialFrontmatterAllowed) {
        return false;
    }

    const auto initialLine = line;
    const auto initialLineOfs = lineOfs;
    ::std::vector<Codepoint> consumed;
    bool eof = false;
    auto read = [&]() {
        try {
            auto ch = this->getc();
            consumed.push_back(ch);
            return ch;
        } catch (const Lexer::EndOfFile&) {
            eof = true;
            return Codepoint();
        }
    };
    auto restore = [&]() {
        line = initialLine;
        lineOfs = initialLineOfs;
        lastCharValid = false;
        replayChars = ::std::move(consumed);
        replayCharOffset = 0;
    };

    auto ch = read();
    if (eof) {
        initialFrontmatterAllowed = false;
        return false;
    }
    if (ch.isspace()) {
        if (ch == '\n') {
            initialFrontmatterPrecededByWhitespace = false;
        } else {
            initialFrontmatterPrecededByWhitespace = true;
        }
        this->ungetc();
        return false;
    }
    if (ch != '-' || initialFrontmatterPrecededByWhitespace) {
        initialFrontmatterAllowed = false;
        this->ungetc();
        return false;
    }

    size_t openingLength = 1;
    while (!eof) {
        ch = read();
        if (!eof && ch == '-') {
            openingLength += 1;
        } else {
            break;
        }
    }
    if (openingLength < 3) {
        initialFrontmatterAllowed = false;
        restore();
        return false;
    }

    while (!eof && ch != '\n' && ch.isspace()) {
        ch = read();
    }
    if (!eof && ch != '\n') {
        const bool validIdentifierStart = ch == '_' || (ch.v < 128 && ::std::isalpha(ch.v)) || ch.v >= 128;
        if (!validIdentifierStart) {
            initialFrontmatterAllowed = false;
            restore();
            return false;
        }
        do {
            ch = read();
        } while (!eof && (issym(ch) || ch == '.'));
        while (!eof && ch != '\n' && ch.isspace()) {
            ch = read();
        }
        if (!eof && ch != '\n') {
            initialFrontmatterAllowed = false;
            restore();
            return false;
        }
    }

    bool linePrefixIsWhitespace = true;
    bool linePrefixHasWhitespace = false;
    while (!eof) {
        ch = read();
        if (eof) {
            break;
        }
        if (ch == '\n') {
            linePrefixIsWhitespace = true;
            linePrefixHasWhitespace = false;
            continue;
        }
        if (ch.isspace()) {
            linePrefixHasWhitespace = true;
            continue;
        }
        if (ch != '-' || !linePrefixIsWhitespace) {
            linePrefixIsWhitespace = false;
            continue;
        }

        size_t closingLength = 1;
        while (!eof) {
            ch = read();
            if (!eof && ch == '-') {
                closingLength += 1;
            } else {
                break;
            }
        }
        if (closingLength >= openingLength) {
            bool validClosing = closingLength == openingLength && !linePrefixHasWhitespace;
            while (!eof && ch != '\n') {
                validClosing &= ch.isspace();
                ch = read();
            }
            if (!validClosing) {
                initialFrontmatterAllowed = false;
                restore();
                return false;
            }
            initialFrontmatterAllowed = false;
            return true;
        }
        linePrefixIsWhitespace = false;
        if (!eof && ch == '\n') {
            linePrefixIsWhitespace = true;
            linePrefixHasWhitespace = false;
        }
    }

    initialFrontmatterAllowed = false;
    restore();
    return false;
}

Token Lexer::getTokenInt() {
    if (!this->nextTokens.empty()) {
        auto rv = ::std::move(this->nextTokens.back());
        nextTokens.pop_back();
        return rv;
    }
    if (!initialShebangChecked) {
        this->checkInitialShebang();
    }
    if (this->trySkipInitialFrontmatter()) {
        return this->getTokenInt();
    }
    try {
        Codepoint ch = this->getc();

        if (ch == '\n') {
            return Token(TOK_NEWLINE);
        }
        if (ch.isspace()) {
            while ((ch = this->getc()).isspace() && ch != '\n')
                ;
            this->ungetc();
            return Token(TOK_WHITESPACE);
        }
        this->ungetc();

        const signed int sym = this->getSymbol();
        if (sym == 0) {
            // No match at all, check for symbol
            auto ch = this->getc();
            if (ch.isdigit()) {
                enum eCoreType numType = CORETYPE_ANY;
                NumMode numMode = NumMode::DEC;

                // Handle integers/floats
                this->ungetc();
                auto val = this->parseInt(&numMode);
                ch = this->getc();

                if (ch == 'e' || ch == 'E' || ch == '.') {
                    // Special handling for `.` - for `..` and method access
                    if (ch == '.') {
                        ch = this->getc();

                        // Double/Triple Dot
                        if (ch == '.') {
                            ch = this->getc();
                            if (ch == '.') {
                                this->nextTokens.push_back(TOK_TRIPLE_DOT);
                            } else if (ch == '=') {
                                this->nextTokens.push_back(TOK_DOUBLE_DOT_EQUAL);
                            } else {
                                this->ungetc();
                                this->nextTokens.push_back(TOK_DOUBLE_DOT);
                            }
                            return Token(val, CORETYPE_ANY);
                        }

                        // Single dot followed by a non-digit, could be a float or an integer with a method/field access
                        // NOTE: `1.e1` is not a float
                        if (!ch.isdigit()) {
                            bool sawSpace = false;
                            while (ch.isspace()) {
                                sawSpace = true;
                                ch = this->getc();
                            }
                            this->ungetc();
                            // A digit after the dot is a tuple index however it
                            // is spaced, but a name is a field or a method only
                            // when it is written right after the dot: `1.min(2)`
                            // is a call, while `1. as u16` is a float and a cast.
                            if (ch.isdigit() || (issym(ch) && !sawSpace)) {
                                this->nextTokens.push_back(TOK_DOT);
                                return Token(val, CORETYPE_ANY);
                            } else {
                                FloatValue fval = val.toDouble();
                                return Token::makeFloat(fval, CORETYPE_ANY);
                            }
                        } else {
                            // Digit, continue
                            // NOTE: parseFloat assumes that the '.' has been consumed, and reads digits until it hits a non-digit and then parses exponents
                            // - Thus, continuing here and letting the below 'ungetc' push a digit back is correct.
                        }
                    }
                    if (numMode != NumMode::DEC) {
                        TODO(this->pointSpan(), "Non-decimal floats");
                    }

                    this->ungetc();
                    FloatValue fval = this->parseFloat(val);
                    if (fval != fval) {
                        assert(!this->nextTokens.empty());
                        auto t = std::move(this->nextTokens.back());
                        this->nextTokens.pop_back();
                        return t;
                    }
                    if (issym(ch = this->getc())) {
                        ::std::string suffix;
                        while (issym(ch)) {
                            suffix += ch;
                            ch = this->getc();
                        }
                        this->ungetc();

                        if (0)
                            ;
                        else if (suffix == "f16") {
                            numType = CORETYPE_F16;
                        } else if (suffix == "f32") {
                            numType = CORETYPE_F32;
                        } else if (suffix == "f64") {
                            numType = CORETYPE_F64;
                        } else if (suffix == "f128") {
                            numType = CORETYPE_F128;
                        } else {
                            auto tok = Token::makeFloat(fval, CORETYPE_ANY);
                            return Token(TOK_LITERAL_SUFFIXED, tok.toStr() + suffix, this->realGetHygiene());
                        }
                    } else {
                        this->ungetc();
                    }
                    return Token::makeFloat(fval, numType);

                } else if (issym(ch)) {
                    // Unsigned
                    ::std::string suffix;
                    while (issym(ch)) {
                        suffix += ch;
                        ch = this->getc();
                    }
                    this->ungetc();

                    if (0)
                        ;
                    else if (suffix == "i8") {
                        numType = CORETYPE_I8;
                    } else if (suffix == "i16") {
                        numType = CORETYPE_I16;
                    } else if (suffix == "i32") {
                        numType = CORETYPE_I32;
                    } else if (suffix == "i64") {
                        numType = CORETYPE_I64;
                    } else if (suffix == "i128") {
                        numType = CORETYPE_I128;
                    } else if (suffix == "isize") {
                        numType = CORETYPE_INT;
                    } else if (suffix == "u8") {
                        numType = CORETYPE_U8;
                    } else if (suffix == "u16") {
                        numType = CORETYPE_U16;
                    } else if (suffix == "u32") {
                        numType = CORETYPE_U32;
                    } else if (suffix == "u64") {
                        numType = CORETYPE_U64;
                    } else if (suffix == "u128") {
                        numType = CORETYPE_U128;
                    } else if (suffix == "usize") {
                        numType = CORETYPE_UINT;
                    } else if (suffix == "f16") {
                        numType = CORETYPE_F16;
                    } else if (suffix == "f32") {
                        numType = CORETYPE_F32;
                    } else if (suffix == "f64") {
                        numType = CORETYPE_F64;
                    } else if (suffix == "f128") {
                        numType = CORETYPE_F128;
                    } else {
                        // An arbitrary suffix is part of the literal token. It
                        // is valid in token trees, but the Rust parser rejects
                        // this distinct token kind if a macro emits it as an
                        // expression.
                        auto tok = Token(val, CORETYPE_ANY);
                        return Token(TOK_LITERAL_SUFFIXED, tok.toStr() + suffix, this->realGetHygiene());
                    }
                    return Token(val, numType);
                } else {
                    this->ungetc();
                    return Token(val, numType);
                }
            }
            // Byte/Raw strings
            else if (ch == 'b' || ch == 'r') {
                bool isByte = false;
                if (ch == 'b') {
                    isByte = true;
                    ch = this->getc();
                }

                if (ch == 'r') {
                    return this->getTokenIntRawString(isByte ? TOK_BYTESTRING : TOK_STRING);
                } else {
                    assert(isByte);

                    // Byte string
                    if (ch == '"') {
                        ::std::string str;
                        while ((ch = this->getc()) != '"') {
                            if (ch == '\\') {
                                auto v = this->parseEscape('"');
                                if (v != ~0u) {
                                    if (v > 256) {
                                        compileErrorGeneric(*this, "Value out of range for byte literal");
                                    }
                                    str += (char)v;
                                }
                            } else {
                                str += ch;
                            }
                        }
                        return this->withLiteralSuffix(Token(TOK_BYTESTRING, mv$(str), realGetHygiene()));
                    }
                    // Byte constant
                    else if (ch == '\'') {
                        // Byte constant
                        ch = this->getc();
                        if (ch == '\\') {
                            u32 val = this->parseEscape('\'');
                            if (this->getc() != '\'') {
                                compileErrorGeneric(*this, "Multi-byte character literal");
                            }
                            return this->withLiteralSuffix(Token(U128(val), CORETYPE_U8));
                        } else {
                            if (this->getc() != '\'') {
                                compileErrorGeneric(*this, "Multi-byte character literal");
                            }
                            return this->withLiteralSuffix(Token(U128(ch.v), CORETYPE_U8));
                        }
                    } else {
                        assert(isByte);
                        this->ungetc();
                        return this->getTokenIntIdentifier('b');
                    }
                }
            }
            // Symbols
            else if (issym(ch)) {
                return this->getTokenIntIdentifier(ch);
            } else {
                parseErrorBadChar(*this, ch.v);
            }
        } else if (sym > 0) {
            // If the symbol is `TOK_DOT`, check if the next character is a digit and consume an integer
            if (sym == TOK_DOT) {
                auto ch = this->getc();
                this->ungetc();
                if (ch.isdigit()) {
                    auto val = this->parseInt(nullptr);
                    ch = this->getc();
                    if (ch == '.') {
                        ch = this->getc();
                        if (ch == '.') {
                            // `.0..`, `.0..=`, and `.0...`: the first dot
                            // belongs to the tuple index and the remaining
                            // run is a range operator.
                            ch = this->getc();
                            if (ch == '.') {
                                nextTokens.push_back(TOK_TRIPLE_DOT);
                            } else if (ch == '=') {
                                nextTokens.push_back(TOK_DOUBLE_DOT_EQUAL);
                            } else {
                                this->ungetc();
                                nextTokens.push_back(TOK_DOUBLE_DOT);
                            }
                            nextTokens.push_back(Token(val, CORETYPE_ANY));
                        } else if (ch.isdigit()) {
                            this->ungetc();
                            auto fval = this->parseFloat(val);
                            if (fval == fval) {
                                nextTokens.push_back(Token::makeFloat(fval, CORETYPE_ANY));
                            }
                        } else {
                            this->ungetc();
                            nextTokens.push_back(TOK_DOT);
                            nextTokens.push_back(Token(val, CORETYPE_ANY));
                        }
                    } else {
                        this->ungetc();
                        nextTokens.push_back(Token(val, CORETYPE_ANY));
                    }
                } else {
                }
            }
            return Token((enum eTokenType)sym);
        } else {
            switch (sym) {
                case LINECOMMENT: {
                    // Line comment
                    ::std::string str;
                    auto ch = this->getc();
                    bool isDoc = false;
                    bool isPdoc = false;
                    if (ch == '/') {
                        ch = this->getc();
                        if (ch == '/') {
                            str += "/";
                        } else {
                            isDoc = true;
                        }
                    } else if (ch == '!') {
                        isPdoc = true;
                        ch = this->getc();
                    }
                    // A line comment ends at the newline: a bare CR inside one
                    // is content, not a terminator.
                    while (ch != '\n') {
                        str += ch;
                        ch = this->getc();
                    }
                    this->ungetc();
                    // ... but a CRLF's CR is not part of the comment.
                    if (!str.empty() && str.back() == '\r') {
                        str.pop_back();
                    }
                    if (isDoc || isPdoc) {
                        //# [ doc = "commment data" ]
                        nextTokens.push_back(TOK_SQUARE_CLOSE);
                        auto docString = Token(TOK_STRING, mv$(str), realGetHygiene());
                        docString.markAsDocComment();
                        nextTokens.push_back(mv$(docString));
                        nextTokens.push_back(TOK_EQUAL);
                        nextTokens.push_back(Token(TOK_IDENT, RcString::newInterned("doc")));
                        nextTokens.push_back(TOK_SQUARE_OPEN);
                        if (isPdoc) {
                            nextTokens.push_back(TOK_EXCLAM);
                        }
                        auto rv = Token(TOK_HASH);
                        rv.markAsDocComment();
                        return rv;
                    }
                    return Token(TOK_COMMENT, str, realGetHygiene());
                }
                case BLOCKCOMMENT: {
                    ::std::string str;
                    bool isDoc = false;
                    bool isPdoc = false;
                    ch = this->getc();
                    if (ch == '*') {
                        ch = this->getc();
                        if (ch == '*') {
                            // `/***....` - more than two `*`s is a normal block comment with a bunch of stuff
                            str += "*";
                        } else if (ch == '/') {
                            // `/**/` - an empty block comment
                            return Token(TOK_COMMENT, str, realGetHygiene());
                        } else {
                            // `/**` - A doc comment
                            isDoc = true;
                        }
                    } else if (ch == '!') {
                        isPdoc = true;
                        ch = this->getc();
                    }
                    unsigned int level = 0;
                    while (true) {
                        // A `*` or a `/` that does not open or close a comment
                        // is an ordinary character, and the one after it has to
                        // be looked at again: a run of `*`s ends a comment on
                        // whichever of them the `/` follows.
                        if (ch == '*') {
                            auto next = this->getc();
                            if (next == '/') {
                                if (level == 0) {
                                    break;
                                }
                                level--;
                                str.push_back('*');
                                str.push_back('/');
                                ch = this->getc();
                            } else {
                                str.push_back('*');
                                ch = next;
                            }
                        } else if (ch == '/') {
                            auto next = this->getc();
                            if (next == '*') {
                                level++;
                                str.push_back('/');
                                str.push_back('*');
                                ch = this->getc();
                            } else {
                                str.push_back('/');
                                ch = next;
                            }
                        } else {
                            str += ch;
                            ch = this->getc();
                        }
                    }
                    if (isDoc || isPdoc) {
                        //# [ doc = "commment data" ]
                        nextTokens.push_back(TOK_SQUARE_CLOSE);
                        auto docString = Token(TOK_STRING, mv$(str), realGetHygiene());
                        docString.markAsDocComment();
                        nextTokens.push_back(mv$(docString));
                        nextTokens.push_back(TOK_EQUAL);
                        nextTokens.push_back(Token(TOK_IDENT, RcString::newInterned("doc")));
                        nextTokens.push_back(TOK_SQUARE_OPEN);
                        if (isPdoc) {
                            nextTokens.push_back(TOK_EXCLAM);
                        }
                        auto rv = Token(TOK_HASH);
                        rv.markAsDocComment();
                        return rv;
                    }
                    return Token(TOK_COMMENT, str, realGetHygiene());
                }
                case SINGLEQUOTE: {
                    auto firstchar = this->getc();
                    if (firstchar.v == '\\') {
                        // Character constant with an escape code
                        u32 val = this->parseEscape('\'');
                        if (this->getc() != '\'') {
                            TODO(this->pointSpan(), "Proper error for lex failures - multi-char const?");
                        }
                        return this->withLiteralSuffix(Token(U128(val), CORETYPE_CHAR));
                    } else if (firstchar.v == '\'') {
                        TODO(this->pointSpan(), "Proper error for empty char literals");
                    } else {
                        ch = this->getc();
                        if (ch == '\'') {
                            // Character constant
                            return this->withLiteralSuffix(Token(U128(firstchar.v), CORETYPE_CHAR));
                        }
                        // A raw lifetime needs raw identifiers, which arrived in
                        // Rust 2021: before that `'r#lt` is three tokens.
                        else if (firstchar == 'r' && ch == '#' && this->editionAfter(ASTEdition::Rust2018)) {
                            ::std::string str;
                            ch = this->getc();
                            if (!issym(ch)) {
                                compileErrorGeneric(*this, "Invalid raw lifetime");
                            }
                            while (issym(ch)) {
                                str += ch;
                                ch = this->getc();
                            }
                            this->ungetc();
                            return Token(TOK_LIFETIME, Ident(this->realGetHygiene(), RcString::newInterned(str)));
                        } else if (issym(firstchar.v)) {
                            // Lifetime name
                            ::std::string str;
                            str += firstchar;
                            while (issym(ch)) {
                                str += ch;
                                ch = this->getc();
                            }
                            this->ungetc();
                            return Token(TOK_LIFETIME, Ident(this->realGetHygiene(), RcString::newInterned(str)));
                        } else {
                            TODO(this->pointSpan(), "Lex Fail - Expected ' after character constant");
                        }
                    }
                    break;
                }
                case DOUBLEQUOTE: {
                    ::std::string str;
                    while ((ch = this->getc()) != '"') {
                        if (ch == '\\') {
                            auto v = this->parseEscape('"');
                            if (v != ~0u) {
                                str += Codepoint(v);
                            }
                        } else {
                            str += ch;
                        }
                    }
                    return this->withLiteralSuffix(Token(TOK_STRING, mv$(str), realGetHygiene()));
                }
                default:
                    assert(!"bugcheck");
            }
        }
    } catch (const Lexer::EndOfFile& /*e*/) {
        return Token(TOK_EOF);
    }
    UNREACHABLE();
}

Token Lexer::getTokenIntRawString(eTokenType kind) {
    // Raw string (possibly a byte or C string)
    const bool isByte = (kind == TOK_BYTESTRING);
    Codepoint ch = this->getc();
    unsigned int hashes = 0;
    while (ch == '#') {
        hashes++;
        ch = this->getc();
    }
    if (ch != '"') {
        // 'b' or 'br' identifier
        if (hashes == 0) {
            this->ungetc(); // Unget the not '"'
            if (isByte) {
                return this->getTokenIntIdentifier('b', 'r');
            } else if (kind == TOK_CSTRING) {
                return this->getTokenIntIdentifier('c', 'r');
            } else {
                return this->getTokenIntIdentifier('r');
            }
        }
        // Raw identifier
        else if (hashes == 1) {
            return this->getTokenIntIdentifier(ch, Codepoint(), /*parse_reserved_word*/ false);
        } else {
            compileErrorGeneric(*this, "Expected '\"' after hashes following `r`");
        }
    }
    auto terminator = ch;
    ::std::string val;

    unsigned terminatingHashes = 0;
    for (;;) {
        try {
            ch = this->getc();
        } catch (const Lexer::EndOfFile& /*e*/) {
            compileErrorGeneric(*this, "EOF reached in raw string");
        }

        if (terminatingHashes > 0) {
            assert(terminatingHashes > 0);
            if (ch != '#') {
                val += terminator;
                while (terminatingHashes < hashes) {
                    val += '#';
                    terminatingHashes += 1;
                }
                terminatingHashes = 0;

                this->ungetc();
            } else {
                terminatingHashes -= 1;
                if (terminatingHashes == 0) {
                    break;
                }
            }
        } else {
            if (ch == terminator) {
                if (hashes == 0) {
                    break;
                }
                terminatingHashes = hashes;
            } else {
                val += ch;
            }
        }
    }
    return this->withLiteralSuffix(Token(kind, mv$(val), realGetHygiene()));
}

Token Lexer::getTokenIntIdentifier(Codepoint leader, Codepoint leader2, bool parseReservedWord) {
    ::std::string str;
    if (leader2 != '\0') {
        str += leader;
    }
    auto ch = leader2 == '\0' ? leader : leader2;
    while (issym(ch)) {
        str += ch;
        ch = this->getc();
    }
    // Raw C string literal: `cr"..."` or `cr#"..."#`
    if (str == "cr" && (ch == '\"' || ch == '#')) {
        this->ungetc();
        return this->getTokenIntRawString(TOK_CSTRING);
    }
    if (ch == '\"') {
        // C String literal
        if (str == "c") {
            str = "";
            while ((ch = this->getc()) != '"') {
                if (ch == '\\') {
                    bool isByteEscape;
                    auto v = this->parseEscape('"', &isByteEscape);
                    if (v != ~0u) {
                        if (isByteEscape) {
                            str += static_cast<char>(v);
                        } else {
                            str += Codepoint(v);
                        }
                    }
                } else {
                    str += ch;
                }
            }
            return this->withLiteralSuffix(Token(TOK_CSTRING, mv$(str), realGetHygiene()));
        }
    }

    this->ungetc();
    if (parseReservedWord) {
        auto v = LexFindReservedWord(str, this->edition);
        if (v != TOK_NULL) {
            return Token(v);
        }
    }
    // Rust normalises identifiers, so `Résumé` written with a precomposed `é`
    // and with `e` plus a combining acute are the same name.
    auto ident = Ident(this->realGetHygiene(), RcString::newInterned(unicodeNormaliseNfc(str)));
    // The only caller that asks for no reserved words is the `r#` prefix.
    ident.isRaw = !parseReservedWord;
    return Token(TOK_IDENT, mv$(ident));
}

/// Parse an integer from the input stream
U128 Lexer::parseInt(NumMode* numModeOut) {
    auto numMode = NumMode::DEC;

    U128 val(0);
    auto ch = this->getc();
    // Leading zero could be part of a base suffix
    // - `0x` Hex
    // - `0o` Octal
    // - `0b` Binary
    // - Any other character: decimal
    if (ch == '0') {
        ch = this->getcNum();
        if (ch == 'x') {
            numMode = NumMode::HEX;
            while ((ch = this->getcNum()).isxdigit()) {
                val *= 16;
                if (ch.v <= '9') {
                    val += U128(ch.v - '0');
                } else if (ch.v <= 'F') {
                    val += U128(ch.v - 'A' + 10);
                } else if (ch.v <= 'f') {
                    val += U128(ch.v - 'a' + 10);
                }
            }
        } else if (ch == 'b') {
            numMode = NumMode::BIN;
            while ((ch = this->getcNum()).isdigit()) {
                val *= 2;
                if (ch.v == '0') {
                    val += 0;
                } else if (ch.v == '1') {
                    val += 1;
                } else {
                    compileErrorGeneric("Invalid digit in binary literal");
                }
            }
        } else if (ch == 'o') {
            numMode = NumMode::OCT;
            while ((ch = this->getcNum()).isdigit()) {
                val *= 8;
                if ('0' <= ch.v && ch.v <= '7') {
                    val += U128(ch.v - '0');
                } else {
                    compileErrorGeneric("Invalid digit in octal literal");
                }
            }
        } else {
            numMode = NumMode::DEC;
            while (ch.isdigit()) {
                val *= 10;
                val += U128(ch.v - '0');
                ch = this->getcNum();
            }
        }
    } else {
        numMode = NumMode::DEC;
        while (ch.isdigit()) {
            val *= 10;
            val += U128(ch.v - '0');
            ch = this->getcNum();
        }
    }

    this->ungetc();
    if (numModeOut) {
        *numModeOut = numMode;
    }
    return val;
}

// Takes the VERY lazy way of reading the float into a string then passing to strtod
FloatValue Lexer::parseFloat(U128 whole) {
    std::string sbuf = FMT(whole << ".");
    //char buf[MAX_LEN+1];

    auto ch = this->getcNum();
#define PUTC(ch)                                                                                                                          \
    do {                                                                                                                                  \
        assert(ch.v < 127);                                                                                                               \
        sbuf += char(ch.v); /* if( ofs < MAX_SIG ) { buf[ofs] = ch.v; ofs ++; } else { throw ParseError::Generic("Oversized float"); } */ \
    } while (0)
    while (ch.isdigit()) {
        PUTC(ch);
        ch = this->getcNum();
    }
    auto queueFloat = [&]() {
        nextTokens.push_back(Token::makeFloat(parseFloatValue(sbuf.c_str()), CORETYPE_ANY));
        return std::numeric_limits<double>::quiet_NaN();
    };
    // If the current char is a `.`
    if (ch == '.') {
        // TODO: `0.0..` should be a range, so if the next character is `.`, then unget and continue
        ch = this->getc();
        if (ch == '.') {
            // Double-dot, need to unget twice
            // OR: Explicitly handle the symbols
            switch (this->getc().v) {
                case '.':
                    nextTokens.push_back(TOK_TRIPLE_DOT);
                    break;
                case '=':
                    nextTokens.push_back(TOK_DOUBLE_DOT_EQUAL);
                    break;
                default:
                    this->ungetc();
                    nextTokens.push_back(TOK_DOUBLE_DOT);
                    break;
            }
            nextTokens.push_back(Token::makeFloat(parseFloatValue(sbuf.c_str()), CORETYPE_ANY));

            return std::numeric_limits<double>::quiet_NaN();
        } else {
            while (ch.isspace()) {
                ch = this->getc();
            }
            this->ungetc();
            nextTokens.push_back(TOK_DOT);
            queueFloat();
            return std::numeric_limits<double>::quiet_NaN();
        }
    } else if (ch.isspace()) {
        while (ch.isspace()) {
            ch = this->getc();
        }
        if (ch != '.') {
            this->ungetc();
            return queueFloat();
        }

        ch = this->getc();
        if (ch == '.') {
            // `0.0 ..= 1.0` is a range, not the start of a tuple-index chain.
            switch (this->getc().v) {
                case '.':
                    nextTokens.push_back(TOK_TRIPLE_DOT);
                    break;
                case '=':
                    nextTokens.push_back(TOK_DOUBLE_DOT_EQUAL);
                    break;
                default:
                    this->ungetc();
                    nextTokens.push_back(TOK_DOUBLE_DOT);
                    break;
            }
            nextTokens.push_back(Token::makeFloat(parseFloatValue(sbuf.c_str()), CORETYPE_ANY));

            return std::numeric_limits<double>::quiet_NaN();
        }
        while (ch.isspace()) {
            ch = this->getc();
        }
        this->ungetc();
        nextTokens.push_back(TOK_DOT);
        queueFloat();
        return std::numeric_limits<double>::quiet_NaN();
    } else {
        if (ch == 'e' || ch == 'E') {
            PUTC(ch);
            ch = this->getcNum();
            if (ch == '-' || ch == '+') {
                PUTC(ch);
                ch = this->getcNum();
            }
            if (!ch.isdigit()) {
                compileErrorGeneric(FMT("Non-numeric '" << ch << "' in float exponent").c_str());
            }
            do {
                PUTC(ch);
                ch = this->getcNum();
            } while (ch.isdigit());
        }
        this->ungetc();

        return parseFloatValue(sbuf.c_str());
    }
}

u32 Lexer::parseEscape(char enclosing, bool* isByteEscape) {
    if (isByteEscape) {
        *isByteEscape = false;
    }
    auto ch = this->getc();
    switch (ch.v) {
        case 'x': {
            if (isByteEscape) {
                *isByteEscape = true;
            }
            ch = this->getc();
            if (!ch.isxdigit()) {
                compileErrorGeneric(*this, FMT("Found invalid character '\\x" << ::std::hex << ch.v << "' in \\u sequence").c_str());
            }
            char tmp[3] = {static_cast<char>(ch.v), 0, 0};
            ch = this->getc();
            if (!ch.isxdigit()) {
                compileErrorGeneric(*this, FMT("Found invalid character '\\x" << ::std::hex << ch.v << "' in \\u sequence").c_str());
            }
            tmp[1] = static_cast<char>(ch.v);
            return ::std::strtol(tmp, NULL, 16);
        } break;
        case 'u': {
            // Unicode (up to six hex digits)
            u32 val = 0;
            ch = this->getc();
            bool reqCloseBrace = false;
            if (ch == '{') {
                reqCloseBrace = true;
                ch = this->getc();
            }
            if (!ch.isxdigit()) {
                compileErrorGeneric(*this, FMT("Found invalid character '\\x" << ::std::hex << ch.v << "' in \\u sequence").c_str());
            }
            // `\u{10__FFFF}` -- an underscore may separate the digits, as it
            // may in any other numeric literal.
            while (ch.isxdigit() || ch == '_') {
                if (ch != '_') {
                    char tmp[2] = {static_cast<char>(ch.v), 0};
                    val *= 16;
                    val += ::std::strtol(tmp, NULL, 16);
                }
                ch = this->getc();
            }
            if (!reqCloseBrace) {
                this->ungetc();
            } else if (ch != '}') {
                compileErrorGeneric(*this, "Expected terminating } in \\u sequence");
            } else {
            }
            return val;
        }
        case '0':
            return '\0';
        case '\\':
            return '\\';
        case '\'':
            return '\'';
        case '"':
            return '"';
        case 'r':
            return '\r';
        case 'n':
            return '\n';
        case 't':
            return '\t';
        case '\r':
        case '\n':
            // A line continuation skips the four characters Rust calls
            // whitespace here, not everything a locale would: a form feed or a
            // no-break space after it stays in the string.
            while (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
                ch = this->getc();
            }
            if (ch == '\\') {
                return parseEscape(enclosing, isByteEscape);
            } else if (ch == enclosing) {
                this->ungetc();
                return ~0;
            } else {
                return ch.v;
            }
        default:
            compileErrorTodo(*this, FMT("Unknown escape sequence \\" << ch).c_str());
    }
}

char Lexer::getcByte() {
    int rv = istream.get();
    if (rv == EOF) {
        throw Lexer::EndOfFile();
    }

    if (rv == '\r') {
        if (istream.get() != '\n') {
            istream.unget();
        } else {
            rv = '\n';
        }
    }
    if (rv == '\n') {
        line++;
        lineOfs = 0;
    }

    return rv;
}

Codepoint Lexer::getc() {
    if (lastCharValid) {
        lastCharValid = false;
#ifdef TRACE_CHARS
        ::std::cout << "getc(): U+" << ::std::hex << lastChar.v << " (cached)" << ::std::endl;
#endif
    } else if (replayCharOffset < replayChars.size()) {
        lastChar = replayChars[replayCharOffset++];
        if (lastChar == '\n') {
            line += 1;
            lineOfs = 0;
        } else {
            lineOfs += 1;
        }
#ifdef TRACE_CHARS
        ::std::cout << "getc(): U+" << ::std::hex << lastChar.v << " (replayed)" << ::std::endl;
#endif
    } else {
        replayChars.clear();
        replayCharOffset = 0;
        lastChar = this->getcCp();
        if (lastChar != '\n') {
            lineOfs += 1;
        }
#ifdef TRACE_CHARS
        ::std::cout << "getc(): U+" << ::std::hex << lastChar.v << ::std::endl;
#endif
    }
    return lastChar;
}

Codepoint Lexer::getcNum() {
    Codepoint ch;
    do {
        ch = this->getc();
    } while (ch == '_');
    return ch;
}

Codepoint Lexer::getcCp() {
    u8 v1 = this->getcByte();
    if (v1 < 128) {
        return {v1};
    } else if ((v1 & 0xC0) == 0x80) {
        // Invalid (continuation)
        return {0xFFFE};
    } else if ((v1 & 0xE0) == 0xC0) {
        // Two bytes
        u8 e1 = this->getcByte();
        if ((e1 & 0xC0) != 0x80) {
            return {0xFFFE};
        }

        u32 outval = ((v1 & 0x1F) << 6) | ((e1 & 0x3F) << 0);
        return {outval};
    } else if ((v1 & 0xF0) == 0xE0) {
        // Three bytes
        u8 e1 = this->getcByte();
        if ((e1 & 0xC0) != 0x80) {
            return {0xFFFE};
        }
        u8 e2 = this->getcByte();
        if ((e2 & 0xC0) != 0x80) {
            return {0xFFFE};
        }

        u32 outval = ((v1 & 0x0F) << 12) | ((e1 & 0x3F) << 6) | ((e2 & 0x3F) << 0);
        return {outval};
    } else if ((v1 & 0xF8) == 0xF0) {
        // Four bytes
        u8 e1 = this->getcByte();
        if ((e1 & 0xC0) != 0x80) {
            return {0xFFFE};
        }
        u8 e2 = this->getcByte();
        if ((e2 & 0xC0) != 0x80) {
            return {0xFFFE};
        }
        u8 e3 = this->getcByte();
        if ((e3 & 0xC0) != 0x80) {
            return {0xFFFE};
        }

        u32 outval = ((v1 & 0x07) << 18) | ((e1 & 0x3F) << 12) | ((e2 & 0x3F) << 6) | ((e3 & 0x3F) << 0);
        return {outval};
    } else {
        compileErrorGeneric("Invalid UTF-8 (too long)");
    }
}

void Lexer::ungetc() {
#ifdef TRACE_CHARS
    ::std::cout << "ungetc(): cache U+" << ::std::hex << lastChar.v << ::std::endl;
#endif
    assert(!lastCharValid);
    lastCharValid = true;
}

// --------------------------------------------------------------------
// Codepoint - Unicode codepoint.
// --------------------------------------------------------------------

bool Codepoint::isspace() const {
    switch (this->v) {
        case '\t':
        case '\r':
        case '\n':
        case ' ':
        case 0xB: // vertical tab
        case 0xC: // ^L
        case 0x85:
        case 0x200E:
        case 0x200F: // LTR / RTL markers
        case 0x2028: // Line Separator
        case 0x2029: // Paragrah Separator
            return true;
        default:
            return false;
    }
}

bool Codepoint::isdigit() const {
    return this->v < 128 && std::isdigit(static_cast<int>(this->v));
}

bool Codepoint::isxdigit() const {
    return this->v < 128 && std::isxdigit(static_cast<int>(this->v));
}

::std::string& operator+=(::std::string& s, const Codepoint& cp) {
    if (cp.v < 0x80) {
        s += (char)cp.v;
    } else if (cp.v < (0x1F + 1) << (1 * 6)) {
        s += (char)(0xC0 | ((cp.v >> 6) & 0x1F));
        s += (char)(0x80 | ((cp.v >> 0) & 0x3F));
    } else if (cp.v < (0x0F + 1) << (2 * 6)) {
        s += (char)(0xE0 | ((cp.v >> 12) & 0x0F));
        s += (char)(0x80 | ((cp.v >> 6) & 0x3F));
        s += (char)(0x80 | ((cp.v >> 0) & 0x3F));
    } else if (cp.v < (0x07 + 1) << (3 * 6)) {
        s += (char)(0xF0 | ((cp.v >> 18) & 0x07));
        s += (char)(0x80 | ((cp.v >> 12) & 0x3F));
        s += (char)(0x80 | ((cp.v >> 6) & 0x3F));
        s += (char)(0x80 | ((cp.v >> 0) & 0x3F));
    } else {
        throw ::std::runtime_error(FMT("BUGCHECK: Bad unicode codepoint encountered - " << ::std::hex << cp.v));
    }
    return s;
}

::std::ostream& operator<<(::std::ostream& os, const Codepoint& cp) {
    if (cp.v < 0x80) {
        os << (char)cp.v;
    } else if (cp.v < (0x1F + 1) << (1 * 6)) {
        os << (char)(0xC0 | ((cp.v >> 6) & 0x1F));
        os << (char)(0x80 | ((cp.v >> 0) & 0x3F));
    } else if (cp.v < (0x0F + 1) << (2 * 6)) {
        os << (char)(0xE0 | ((cp.v >> 12) & 0x0F));
        os << (char)(0x80 | ((cp.v >> 6) & 0x3F));
        os << (char)(0x80 | ((cp.v >> 0) & 0x3F));
    } else if (cp.v < (0x07 + 1) << (2 * 6)) {
        os << (char)(0xF0 | ((cp.v >> 18) & 0x07));
        os << (char)(0x80 | ((cp.v >> 12) & 0x3F));
        os << (char)(0x80 | ((cp.v >> 6) & 0x3F));
        os << (char)(0x80 | ((cp.v >> 0) & 0x3F));
    } else {
        throw ::std::runtime_error("BUGCHECK: Bad unicode codepoint encountered");
    }
    return os;
}

Token LexFindOperator(stl::StringView s) {
    const stl::StringView underscore(reinterpret_cast<const u8*>("_"), 1);
    if (s == underscore) {
        return TOK_UNDERSCORE;
    }
    for (size_t i = 0; i < LEN(TOKENMAP); i++) {
        const auto& e = TOKENMAP[i];
        const stl::StringView chars(reinterpret_cast<const u8*>(e.chars), e.len);
        if (s < chars) {
            break;
        }
        if (s == chars) {
            if (e.type < 0) {
                break;
            }
            return static_cast<eTokenType>(e.type);
        }
    }
    return TOK_NULL;
}

Token LexFindOperator(const ::std::string& s) {
    return LexFindOperator(stl::StringView(reinterpret_cast<const u8*>(s.data()), s.size()));
}

Token LexFindReservedWord(const ::std::string& s, ASTEdition edition) {
    size_t len = 0;
    const sRWORD* RWORDS = nullptr;
    switch (edition) {
        case ASTEdition::Rust2015:
            len = LEN(RWORDS_2015);
            RWORDS = RWORDS_2015;
            break;
        case ASTEdition::Rust2018:
        case ASTEdition::Rust2021:
        case ASTEdition::Rust2024:
            len = LEN(RWORDS_2018);
            RWORDS = RWORDS_2018;
            break;
    }
    assert(len > 0);
    for (size_t i = 0; i < len; i++) {
        const auto& e = RWORDS[i];
        if (s < e.chars) {
            break;
        }
        if (s == e.chars) {
            assert(e.type > 0);
            return static_cast<eTokenType>(e.type);
        }
    }
    return TOK_NULL;
}

Codepoint::Codepoint()
    : v(0)
{
}

Codepoint::Codepoint(u32 v)
    : v(v)
{
}

void Lexer::pushHygine() {
    hygiene_ = Ident::Hygiene::newScopeChained(hygieneContext, typePool(), hygiene_);
}

void Lexer::popHygine() {
    hygiene_ = hygiene_.getParent(typePool());
}

ASTEdition Lexer::realGetEdition() const {
    return edition;
}
