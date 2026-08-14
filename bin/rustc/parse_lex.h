#pragma once

namespace stl {
    class ObjPool;
}

#include "parse_tokenstream.h"

#include <string>
#include <fstream>

struct Codepoint {
    uint32_t v;

    Codepoint();

    Codepoint(uint32_t v);

    bool isspace() const;
    bool isdigit() const;
    bool isxdigit() const;

    bool operator==(char x) {
        return v == static_cast<uint32_t>(x);
    }

    bool operator!=(char x) {
        return v != static_cast<uint32_t>(x);
    }

    bool operator==(Codepoint x) {
        return v == x.v;
    }

    bool operator!=(Codepoint x) {
        return v != x.v;
    }
};

extern ::std::string& operator+=(::std::string& s, const Codepoint& cp);
extern ::std::ostream& operator<<(::std::ostream& s, const Codepoint& cp);

extern Token LexFindOperator(const ::std::string& s);
extern Token LexFindReservedWord(const ::std::string& s, ASTEdition edition);

typedef Codepoint uchar;

class Lexer: public TokenStream {
    RcString mPath;
    unsigned int line;
    unsigned int lineOfs;

    ::std::unique_ptr<::std::ifstream> istreamFp;
    ::std::istream& istream;
    bool lastCharValid;
    Codepoint lastChar;
    ::std::vector<Token> nextTokens;

    ASTEdition edition;
    Ident::Hygiene mHygiene;

public:
    Lexer(stl::ObjPool& pool, ::std::istringstream& ss, ASTEdition edition, ParseState ps);
    Lexer(stl::ObjPool& pool, const ::std::string& filename, ASTEdition edition, ParseState ps);

    Position getPosition() const override;
    Ident::Hygiene realGetHygiene() const override;

    ASTEdition realGetEdition() const override;

    Token realGetToken() override;

private:
    Token getTokenInt();

    signed int getSymbol();
    Token getTokenIntRawString(bool isByte);
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
    uint32_t parseEscape(char enclosing, bool* isByteEscape = nullptr);

    void pushHygine() override;

    void popHygine() override;

    void ungetc();
    Codepoint getcNum();
    Codepoint getc();
    Codepoint getcCp();
    char getcByte();

    class EndOfFile {};
};
