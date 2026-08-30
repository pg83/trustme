#pragma once

namespace stl {
    class ObjPool;
    class StringView;
}

#include "parse_tokenstream.h"

#include <string>
#include <fstream>

struct Codepoint {
    u32 v;

    Codepoint();

    Codepoint(u32 v);

    bool isspace() const;
    bool isdigit() const;
    bool isxdigit() const;

    bool operator==(char x) const {
        return v == static_cast<u32>(x);
    }

    bool operator!=(char x) const {
        return v != static_cast<u32>(x);
    }

    bool operator==(Codepoint x) const {
        return v == x.v;
    }

    bool operator!=(Codepoint x) const {
        return v != x.v;
    }
};

std::string& operator+=(std::string& s, const Codepoint& cp);
Token LexFindOperator(stl::StringView s);
Token LexFindOperator(const std::string& s);
Token LexFindReservedWord(const std::string& s, ASTEdition edition);

typedef Codepoint uchar;

class Lexer: public TokenStream {
    u32& id;
    RcString path_;
    unsigned int line;
    unsigned int lineOfs;

    std::unique_ptr<std::ifstream> istreamFp;
    std::istream& istream;
    bool lastCharValid;
    Codepoint lastChar;
    bool initialShebangChecked;
    bool initialFrontmatterAllowed;
    bool initialFrontmatterPrecededByWhitespace;
    stl::Vector<Codepoint> replayChars;
    size_t replayCharOffset;
    std::vector<Token> nextTokens;

    ASTEdition edition;
    Ident::Hygiene hygiene_;

public:
    Lexer(u32& id, stl::ObjPool& pool, std::istringstream& ss, ASTEdition edition, ParseState ps);
    Lexer(u32& id, stl::ObjPool& pool, const std::string& filename, ASTEdition edition, ParseState ps);

    Position getPosition() const override;
    Ident::Hygiene realGetHygiene() const override;

    ASTEdition realGetEdition() const override;

    Token realGetToken() override;

private:
    void checkInitialShebang();
    bool trySkipInitialFrontmatter();
    Token getTokenInt();

    signed int getSymbol();

    Token getTokenIntRawString(eTokenType kind);
    Token getTokenIntIdentifier(Codepoint ch, Codepoint ch2 = '\0', bool parseReservedWord = true);
    Token withLiteralSuffix(Token tok);
    enum class NumMode {
        BIN,
        OCT,
        DEC,
        HEX,
    };
    U128 parseInt(NumMode* numMode);
    FloatValue parseFloat(U128 whole);
    u32 parseEscape(char enclosing, bool* isByteEscape = nullptr);

    void pushHygine() override;

    void popHygine() override;

    void ungetc();
    Codepoint getcNum();
    Codepoint getc();
    Codepoint getcCp();
    char getcByte();

    class EndOfFile {};
};
