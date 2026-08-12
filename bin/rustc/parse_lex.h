#pragma once

#include <string>
#include <fstream>
#include "parse_tokenstream.h"

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
extern Token LexFindReservedWord(const ::std::string& s, AST::Edition edition);

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

    AST::Edition edition;
    Ident::Hygiene mHygiene;

public:
    Lexer(::std::istringstream& ss, AST::Edition edition, ParseState ps);
    Lexer(const ::std::string& filename, AST::Edition edition, ParseState ps);

    Position getPosition() const override;
    Ident::Hygiene realGetHygiene() const override;

    AST::Edition realGetEdition() const override {
        return edition;
    }

    Token realGetToken() override;

private:
    Token getTokenInt();

    signed int getSymbol();
    Token getTokenIntRawString(bool is_byte);
    Token getTokenIntIdentifier(Codepoint ch, Codepoint ch2 = '\0', bool parse_reserved_word = true);
    enum class NumMode {
        BIN,
        OCT,
        DEC,
        HEX,
    };
    U128 parseInt(NumMode* num_mode);
    FloatValue parseFloat(U128 whole);
    uint32_t parseEscape(char enclosing, bool* is_byte_escape = nullptr);

    void push_hygine() override;

    void pop_hygine() override;

    void ungetc();
    Codepoint getcNum();
    Codepoint getc();
    Codepoint getcCp();
    char getcByte();

    class EndOfFile {};
};
