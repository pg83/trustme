#pragma once

#include "span.h"
#include "ident.h"
#include "floats.h"
#include "int128.h"
#include "coretypes.h"
#include "rc_string.h"

#include <memory>

enum eTokenType {
#define _(t) t,
#include "parse_eTokenType.enum.inc"
#undef _
};

class Position {
public:
    Span span;
    RcString filename;
    unsigned int line;
    unsigned int ofs;

    Position();

    Position(Span sp);

    Position(RcString filename, unsigned int line, unsigned int ofs);
};

extern ::std::ostream& operator<<(::std::ostream& os, const Position& p);

struct ASTType;
class TokenTree;

class ASTVisibility;
class ASTPattern;
class ASTPath;
class ASTExprNode;
class ASTAttribute;
class ASTItem;

template <typename T>
struct ASTNamed;

class InterpolatedFragment;

#include "parse_token_tu.h"

class Token {
    friend class HirSerialiser;
    friend class HirDeserialiser;

    using Data = TokenData;

    enum eTokenType type_;
    Data data_;
    Position pos;
    Ident::Hygiene hygiene_;
    bool isDocComment_ = false;

    Token(enum eTokenType t, Data d, Position p);

public:
    virtual ~Token();
    Token();

    Token& operator=(Token&& t);

    Token(Token&& t);

    Token& operator=(const Token& t);

    Token(const Token& t);
    Token clone() const;

    Token(enum eTokenType type);
    Token(enum eTokenType type, ::std::string str, Ident::Hygiene h);
    Token(enum eTokenType type, Ident i);
    Token(U128 val, enum eCoreType datatype);
    static Token makeFloat(FloatValue val, enum eCoreType datatype);
    Token(const InterpolatedFragment&);

    struct TagTakeIP {};

    Token(TagTakeIP, InterpolatedFragment);

    enum eTokenType type() const {
        return type_;
    }

    bool hasData() const {
        return !data_.is_None();
    }

    const Ident& ident() const {
        return data_.as_Ident();
    }

    ::std::string& str() {
        return data_.as_String();
    }

    const ::std::string& str() const {
        return data_.as_String();
    }

    const Ident::Hygiene& strHygiene() const {
        return hygiene_;
    }

    enum eCoreType datatype() const {
        switch (data_.tag()) {
            case Data::TAG_Integer: {
                auto& e = data_.as_Integer();
                return e.datatype;
            }
            case Data::TAG_Float: {
                auto& e = data_.as_Float();
                return e.datatype;
            }
            default: {
                assert(!"Getting datatype of invalid token type");
                break;
            }
        }
        UNREACHABLE();
    }

    U128 intval() const {
        return data_.as_Integer().intval;
    }

    FloatValue floatval() const {
        return data_.as_Float().floatval;
    }

    // TODO: Replace these with a way of getting a InterpolatedFragment&
    ASTType*& fragType();

    ASTPath& fragPath();

    ASTPattern& fragPattern();

    ASTAttribute& fragMeta();

    ASTExprNode& fragNode();

    ::std::unique_ptr<ASTExprNode> takeFragNode();
    ASTNamed<ASTItem> takeFragItem();
    ASTNamed<ASTItem> takeFragStmtItem();
    ASTVisibility takeFragVis();

    bool operator==(eTokenType tty) const {
        return type() == tty;
    }

    bool operator!=(eTokenType tty) const {
        return !(*this == tty);
    }

    bool operator==(const Token& r) const;

    bool operator!=(const Token& r) const {
        return !(*this == r);
    }

    ::std::string toStr() const;

    void setPos(Position pos) {
        this->pos = pos;
    }

    const Position& getPos() const {
        return pos;
    }

    void markAsDocComment() {
        isDocComment_ = true;
    }

    bool isDocComment() const {
        return isDocComment_;
    }

    static bool typeIsRword(enum eTokenType type) {
        return type >= TOK_RWORD_PUB && type <= TOK_RWORD_TRY;
    }

    static const char* typestr(enum eTokenType type);
    static eTokenType typefromstr(const ::std::string& s);

    friend ::std::ostream& operator<<(::std::ostream& os, const Token& tok);
};

extern ::std::ostream& operator<<(::std::ostream& os, const Token& tok);

extern void printEscapedLiteral(::std::ostream& os, eTokenType type, const u8* value, size_t size);

extern bool tokensNeedSpace(eTokenType prev, eTokenType cur);
