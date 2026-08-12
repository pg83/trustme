#pragma once

#include "rc_string.h"
#include "tagged_union.h"
#include "coretypes.h"
#include "ident.h"
#include <memory>
#include "int128.h"
#include "floats.h"
#include "span.h"

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

class TypeRef;
class TokenTree;

namespace AST {
    class Visibility;
    class Pattern;
    class Path;
    class ExprNode;
    class Attribute;
    class Item;

    template <typename T>
    struct Named;
};

class InterpolatedFragment;

class Token {
    friend class HirSerialiser;
    friend class HirDeserialiser;

    TAGGED_UNION(
        Data,
        None,
        (None, struct {}),
        (Ident, Ident),
        (String, ::std::string),
        (Integer,
         struct {
             enum eCoreType m_datatype;
             U128 m_intval;
         }),
        (Float,
         struct {
             enum eCoreType m_datatype;
             FloatValue m_floatval;
         }),
        (Fragment, void*)
    );

    enum eTokenType m_type;
    Data m_data;
    Position m_pos;
    Ident::Hygiene m_hygiene; // Only for strings, for formatting

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
    static Token make_float(FloatValue val, enum eCoreType datatype);
    Token(const InterpolatedFragment&);

    struct TagTakeIP {};

    Token(TagTakeIP, InterpolatedFragment);

    enum eTokenType type() const {
        return m_type;
    }

    bool has_data() const {
        return !m_data.is_None();
    }

    const Ident& ident() const {
        return m_data.as_Ident();
    }

    ::std::string& str() {
        return m_data.as_String();
    }

    const ::std::string& str() const {
        return m_data.as_String();
    }

    const Ident::Hygiene& str_hygiene() const {
        return m_hygiene;
    }

    enum eCoreType datatype() const {
        TU_MATCH_DEF(Data, (m_data), (e), (assert(!"Getting datatype of invalid token type");), (Integer, return e.m_datatype;), (Float, return e.m_datatype;)) throw "";
    }

    U128 intval() const {
        return m_data.as_Integer().m_intval;
    }

    FloatValue floatval() const {
        return m_data.as_Float().m_floatval;
    }

    // TODO: Replace these with a way of getting a InterpolatedFragment&
    TypeRef& frag_type();

    AST::Path& frag_path();

    AST::Pattern& frag_pattern();

    AST::Attribute& frag_meta();

    AST::ExprNode& frag_node();

    ::std::unique_ptr<AST::ExprNode> take_frag_node();
    ::AST::Named<AST::Item> take_frag_item();
    ::AST::Named<AST::Item> take_frag_stmt_item();
    ::AST::Visibility take_frag_vis();

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

    /// Return a re-parseable version of the token
    ::std::string to_str() const;

    void set_pos(Position pos) {
        m_pos = pos;
    }

    const Position& get_pos() const {
        return m_pos;
    }

    static bool type_is_rword(enum eTokenType type) {
        return type >= TOK_RWORD_PUB && type <= TOK_RWORD_TRY;
    }

    static const char* typestr(enum eTokenType type);
    static eTokenType typefromstr(const ::std::string& s);

    friend ::std::ostream& operator<<(::std::ostream& os, const Token& tok);
};

extern ::std::ostream& operator<<(::std::ostream& os, const Token& tok);
