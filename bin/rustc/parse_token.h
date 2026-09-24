#pragma once

#include "output.h"

#include "span.h"
#include "ident.h"
#include "floats.h"
#include "int128.h"
#include "coretypes.h"
#include "rc_string.h"

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
    unsigned int endLine;
    unsigned int endOfs;

    Position();

    Position(Span sp);

    Position(RcString filename, unsigned int line, unsigned int ofs);
};

struct ASTType;
class TokenTree;

class ASTVisibility;
class ASTPattern;
class ASTPath;
class ASTExprNode;
class ASTExprNode;
class ASTAttribute;
class ASTItem;

template <typename T>
struct ASTNamed;

class InterpolatedFragment;
struct RecordedToken;

struct RecordedTokens {
    RecordedToken* const* items = nullptr;
    size_t count = 0;

    RecordedToken* const* begin() const {
        return items;
    }

    RecordedToken* const* end() const {
        return items + count;
    }
};

#include "parse_token_tu.h"

enum class TokenSpacing : u8 {
    Alone,
    Joint,
    JointHidden,
};

class Token {
    using Data = TokenData;

    enum eTokenType type_;
    Data data_;
    Position pos;
    Ident::Hygiene hygiene_;
    bool isDocComment_ = false;
    TokenSpacing spacing_ = TokenSpacing::Alone;

    Token(enum eTokenType t, Data d, Position p);

public:
    ~Token();
    Token();

    Token& operator=(Token&& t);

    Token(Token&& t);

    Token& operator=(const Token& t);

    Token(const Token& t);
    Token clone() const;

    Token(enum eTokenType type);
    Token(enum eTokenType type, std::string str, Ident::Hygiene h);
    Token(enum eTokenType type, std::string str, RcString spelling, Ident::Hygiene h);
    Token(enum eTokenType type, Ident i);
    Token(U128 val, enum eCoreType datatype, RcString spelling = RcString());
    static Token makeFloat(FloatValue val, enum eCoreType datatype, RcString spelling = RcString());
    Token(const InterpolatedFragment&);

    struct TagTakeIP {};

    Token(TagTakeIP, InterpolatedFragment);

    enum eTokenType type() const {
        return type_;
    }

    static Token fromSerialised(enum eTokenType type, TokenData data);
    static Token tokensOf(const Token& fragment);

    const TokenData& rawData() const;

    bool hasData() const {
        return !data_.is_None();
    }

    const Ident& ident() const {
        return data_.as_Ident();
    }

    std::string& str() {
        return data_.as_String().value;
    }

    const std::string& str() const {
        return data_.as_String().value;
    }

    RcString spelling() const {
        switch (data_.tag()) {
            case Data::TAG_String:
                return data_.as_String().spelling;
            case Data::TAG_Integer:
                return data_.as_Integer().spelling;
            case Data::TAG_Float:
                return data_.as_Float().spelling;
            default:
                return RcString();
        }
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
                BUG_ASSERT(!"Getting datatype of invalid token type");
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
    const ASTType* fragType() const;

    ASTPath& fragPath();

    ASTPattern& fragPattern();
    const ASTPattern& fragPattern() const;

    ASTAttribute& fragMeta();

    ASTExprNode& fragNode();
    const ASTExprNode& fragNode() const;

    ASTExprNode* takeFragNode();
    const ASTNamed<ASTItem>& fragItem() const;
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

    std::string toStr() const;

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

    TokenSpacing spacing() const {
        return spacing_;
    }

    void setSpacing(TokenSpacing spacing) {
        spacing_ = spacing;
    }

    bool isPunct() const;

    const RecordedTokens* fragmentTokens() const {
        return data_.is_Fragment() ? data_.as_Fragment().tokens : nullptr;
    }

    static bool typeIsRword(enum eTokenType type) {
        return type >= TOK_RWORD_PUB && type <= TOK_RWORD_TRY;
    }

    static const char* typestr(enum eTokenType type);
    static eTokenType typefromstr(const std::string& s);

};

void printEscapedLiteral(stl::ZeroCopyOutput& os, eTokenType type, const u8* value, size_t size);

