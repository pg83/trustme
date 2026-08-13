#include "parse_token.h"

#include "common.h"
#include "ast_ast.h"
#include "ast_expr.h" // for reasons
#include "ast_types.h"
#include "parse_parseerror.h"
#include "parse_interpolated_fragment.h"

Token::~Token() {
    switch (mType) {
        case TOK_INTERPOLATED_TYPE:
            delete reinterpret_cast<ASTType**>(mData.as_Fragment());
            break;
        case TOK_INTERPOLATED_PATTERN:
            delete reinterpret_cast<ASTPattern*>(mData.as_Fragment());
            break;
        case TOK_INTERPOLATED_PATH:
            delete reinterpret_cast<ASTPath*>(mData.as_Fragment());
            break;
        case TOK_INTERPOLATED_EXPR:
            delete reinterpret_cast<ASTExprNode*>(mData.as_Fragment());
            break;
        case TOK_INTERPOLATED_STMT:
            delete reinterpret_cast<ASTExprNode*>(mData.as_Fragment());
            break;
        case TOK_INTERPOLATED_STMT_ITEM:
        case TOK_INTERPOLATED_ITEM:
            delete reinterpret_cast<ASTNamed<ASTItem>*>(mData.as_Fragment());
            break;
        case TOK_INTERPOLATED_BLOCK:
            delete reinterpret_cast<ASTExprNode*>(mData.as_Fragment());
            break;
        case TOK_INTERPOLATED_META:
            delete reinterpret_cast<ASTAttribute*>(mData.as_Fragment());
            break;
        case TOK_INTERPOLATED_VIS:
            delete reinterpret_cast<ASTVisibility*>(mData.as_Fragment());
            break;
        default:
            break;
    }
}

Token::Token()
    : mType(TOK_NULL)
{
}

Token::Token(enum eTokenType type)
    : mType(type)
{
}

Token::Token(enum eTokenType type, Ident i)
    : mType(type)
    , mData(mv$(i))
{
}

Token::Token(enum eTokenType type, ::std::string str, Ident::Hygiene h)
    : mType(type)
    , mData(Data::make_String(mv$(str)))
    , mHygiene(std::move(h))
{
}

Token::Token(U128 val, enum eCoreType datatype)
    : mType(TOK_INTEGER)
    , mData(Data::make_Integer({datatype, val}))
{
}

Token Token::makeFloat(FloatValue val, enum eCoreType datatype) {
    auto rv = Token(TOK_FLOAT);
    rv.mData = Data::make_Float({datatype, val});
    switch (datatype) {
        case CORETYPE_F16:
        case CORETYPE_F32:
        case CORETYPE_F64:
        case CORETYPE_F128:
        case CORETYPE_ANY:
            break;
        default:
            throw std::runtime_error("Bad type for float");
    }
    return rv;
}

Token::Token(const InterpolatedFragment& frag) {
    switch (frag.mType) {
        case InterpolatedFragment::TT:
            throw "";
        case InterpolatedFragment::VIS:
            mType = TOK_INTERPOLATED_VIS;
            mData = new ASTVisibility(*reinterpret_cast<const ASTVisibility*>(frag.ptr));
            break;
        case InterpolatedFragment::TYPE:
            mType = TOK_INTERPOLATED_TYPE;
            mData = new ASTType*((*reinterpret_cast<ASTType* const*>(frag.ptr))->clone());
            break;
        case InterpolatedFragment::PAT:
            mType = TOK_INTERPOLATED_PATTERN;
            mData = new ASTPattern(reinterpret_cast<const ASTPattern*>(frag.ptr)->clone());
            break;
        case InterpolatedFragment::PATH:
            mType = TOK_INTERPOLATED_PATH;
            mData = new ASTPath(*reinterpret_cast<const ASTPath*>(frag.ptr));
            break;
        case InterpolatedFragment::EXPR:
            mType = TOK_INTERPOLATED_EXPR;
            if (0) {
                case InterpolatedFragment::STMT:
                    mType = TOK_INTERPOLATED_STMT;
            }
            if (0) {
                case InterpolatedFragment::BLOCK:
                    mType = TOK_INTERPOLATED_BLOCK;
            }

            mData = reinterpret_cast<const ASTExprNode*>(frag.ptr)->clone().release();
            break;
        case InterpolatedFragment::META:
            mType = TOK_INTERPOLATED_META;
            mData = new ASTAttribute(reinterpret_cast<const ASTAttribute*>(frag.ptr)->clone());
            break;
        case InterpolatedFragment::STMT_ITEM:
        case InterpolatedFragment::ITEM: {
            mType = frag.mType == InterpolatedFragment::STMT_ITEM ? TOK_INTERPOLATED_STMT_ITEM : TOK_INTERPOLATED_ITEM;
            const auto& named = *reinterpret_cast<const ASTNamed<ASTItem>*>(frag.ptr);
            auto item = named.data.clone();
            mData = new ASTNamed<ASTItem>(named.span, named.attrs.clone(), named.vis, named.name, mv$(item));
            break;
        }
    }
}

Token::Token(TagTakeIP, InterpolatedFragment frag) {
    switch (frag.mType) {
        case InterpolatedFragment::TT:
            throw "";
        case InterpolatedFragment::VIS:
            mType = TOK_INTERPOLATED_VIS;
            mData = frag.ptr;
            frag.ptr = nullptr;
            break;
        case InterpolatedFragment::TYPE:
            mType = TOK_INTERPOLATED_TYPE;
            mData = frag.ptr;
            frag.ptr = nullptr;
            break;
        case InterpolatedFragment::PAT:
            mType = TOK_INTERPOLATED_PATTERN;
            mData = frag.ptr;
            frag.ptr = nullptr;
            break;
        case InterpolatedFragment::PATH:
            mType = TOK_INTERPOLATED_PATH;
            mData = frag.ptr;
            frag.ptr = nullptr;
            break;
        case InterpolatedFragment::EXPR:
            mType = TOK_INTERPOLATED_EXPR;
            if (0) {
                case InterpolatedFragment::STMT:
                    mType = TOK_INTERPOLATED_STMT;
            }
            if (0) {
                case InterpolatedFragment::BLOCK:
                    mType = TOK_INTERPOLATED_BLOCK;
            }

            mData = reinterpret_cast<ASTExprNode*>(frag.ptr);
            frag.ptr = nullptr;
            break;
        case InterpolatedFragment::STMT_ITEM:
        case InterpolatedFragment::ITEM:
            mType = frag.mType == InterpolatedFragment::STMT_ITEM ? TOK_INTERPOLATED_STMT_ITEM : TOK_INTERPOLATED_ITEM;
            mData = frag.ptr;
            frag.ptr = nullptr;
            break;
        case InterpolatedFragment::META:
            mType = TOK_INTERPOLATED_META;
            mData = frag.ptr;
            frag.ptr = nullptr;
            break;
    }
}

Token::Token(const Token& t)
    : mType(t.mType)
    , mData(Data::make_None({}))
    , pos(t.pos)
    , mHygiene(t.mHygiene)
    , mIsDocComment(t.mIsDocComment)
{
    assert(t.mData.tag() != Data::TAGDEAD);
    TU_MATCH_HDRA( (t.mData), {)
    TU_ARMA(None, e) {
        }
        TU_ARMA(Ident, e) {
            mData = Data::make_Ident(e);
        }
        TU_ARMA(String, e) {
            mData = Data::make_String(e);
        }
        TU_ARMA(Integer, e) {
            mData = Data::make_Integer(e);
        }
        TU_ARMA(Float, e) {
            mData = Data::make_Float(e);
        }
        TU_ARMA(Fragment, e) {
            BUG(Span(Span(), t.pos), "Attempted to copy a fragment - " << t);
        }
    }
}

Token Token::clone() const {
    Token rv(mType);
    rv.pos = pos;
    rv.mHygiene = mHygiene;
    rv.mIsDocComment = mIsDocComment;

    assert(mData.tag() != Data::TAGDEAD);
    TU_MATCH(Data, (mData), (e), (None, ), (Ident, rv.mData = Data::make_Ident(e);), (String, rv.mData = Data::make_String(e);), (Integer, rv.mData = Data::make_Integer(e);), (Float, rv.mData = Data::make_Float(e);), (Fragment, assert(e); switch (mType) {
                 case TOK_INTERPOLATED_TYPE:
                     rv.mData = new ASTType*((*reinterpret_cast<ASTType**>(e))->clone());
                     break;
                 case TOK_INTERPOLATED_PATTERN:
                     rv.mData = new ASTPattern(reinterpret_cast<ASTPattern*>(e)->clone());
                     break;
                 case TOK_INTERPOLATED_PATH:
                     rv.mData = new ASTPath(*reinterpret_cast<ASTPath*>(e));
                     break;
                 case TOK_INTERPOLATED_EXPR:
                     rv.mData = reinterpret_cast<ASTExprNode*>(e)->clone().release();
                     break;
                 case TOK_INTERPOLATED_STMT:
                     rv.mData = reinterpret_cast<ASTExprNode*>(e)->clone().release();
                     break;
                 case TOK_INTERPOLATED_BLOCK:
                     rv.mData = reinterpret_cast<ASTExprNode*>(e)->clone().release();
                     break;
                 case TOK_INTERPOLATED_META:
                     rv.mData = new ASTAttribute(reinterpret_cast<ASTAttribute*>(e)->clone());
                     break;
                 case TOK_INTERPOLATED_STMT_ITEM:
                 case TOK_INTERPOLATED_ITEM: {
                     const auto& named = *reinterpret_cast<ASTNamed<ASTItem>*>(e);
                     auto item = named.data.clone();
                     rv.mData = new ASTNamed<ASTItem>(named.span, named.attrs.clone(), named.vis, named.name, mv$(item));
                     break;
                 }
                 default:
                     BUG(Span(Span(), pos), "Fragment with invalid token type (" << *this << ")");
                     break;
             } assert(rv.mData.is_Fragment());))
    return rv;
}

ASTExprNode& Token::fragNode() {
    assert(mType == TOK_INTERPOLATED_EXPR || mType == TOK_INTERPOLATED_STMT || mType == TOK_INTERPOLATED_BLOCK);
    auto ptr = mData.as_Fragment();
    return *reinterpret_cast<ASTExprNode*>(ptr);
}

::std::unique_ptr<ASTExprNode> Token::takeFragNode() {
    assert(mType == TOK_INTERPOLATED_EXPR || mType == TOK_INTERPOLATED_STMT || mType == TOK_INTERPOLATED_BLOCK);
    auto ptr = mData.as_Fragment();
    mData.as_Fragment() = nullptr;
    return ::std::unique_ptr<ASTExprNode>(reinterpret_cast<ASTExprNode*>(ptr));
}

ASTNamed<ASTItem> Token::takeFragItem() {
    assert(mType == TOK_INTERPOLATED_ITEM);
    auto ptr = reinterpret_cast<ASTNamed<ASTItem>*>(mData.as_Fragment());
    mData.as_Fragment() = nullptr;
    auto rv = mv$(*ptr);
    delete ptr;
    return mv$(rv);
}

ASTNamed<ASTItem> Token::takeFragStmtItem() {
    assert(mType == TOK_INTERPOLATED_STMT_ITEM);
    auto ptr = reinterpret_cast<ASTNamed<ASTItem>*>(mData.as_Fragment());
    mData.as_Fragment() = nullptr;
    auto rv = mv$(*ptr);
    delete ptr;
    return mv$(rv);
}

ASTVisibility Token::takeFragVis() {
    assert(mType == TOK_INTERPOLATED_VIS);
    auto ptr = reinterpret_cast<ASTVisibility*>(mData.as_Fragment());
    mData.as_Fragment() = nullptr;
    auto rv = mv$(*ptr);
    delete ptr;
    return mv$(rv);
}

const char* Token::typestr(enum eTokenType type) {
    switch (type) {
#define _(t) \
    case t:  \
        return #t;
#include "parse_eTokenType.enum.inc"
#undef _
    }
    return ">>BUGCHECK: BADTOK<<";
}

enum eTokenType Token::typefromstr(const ::std::string& s) {
    if (s == "") {
        return TOK_NULL;
    }
#define _(t)     \
    if (s == #t) \
        return t;
#include "parse_eTokenType.enum.inc"
#undef _
    return TOK_NULL;
}

struct EscapedString {
    const ::std::string& s;

    EscapedString(const ::std::string& s)
        : s(s)
    {
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const EscapedString& x) {
        for (auto b : x.s) {
            switch (b) {
                case '"':
                    os << "\\\"";
                    break;
                case '\\':
                    os << "\\\\";
                    break;
                case '\n':
                    os << "\\n";
                    break;
                default:
                    if (' ' <= b && b < 0x7F) {
                        os << b;
                    } else {
                        os << "\\u{" << ::std::hex << (unsigned int)b << "}";
                    }
                    break;
            }
        }
        return os;
    }
};

::std::string Token::toStr() const {
    ::std::stringstream ss;
    switch (mType) {
        case TOK_NULL:
            return "/*null*/";
        case TOK_EOF:
            return "/*eof*/";

        case TOK_NEWLINE:
            return "\n";
        case TOK_WHITESPACE:
            return " ";
        case TOK_COMMENT:
            return "/*" + mData.as_String() + "*/";
        case TOK_INTERPOLATED_TYPE:
            (*reinterpret_cast<const ::ASTType**>(mData.as_Fragment()))->print(ss, false);
            return ss.str();
        case TOK_INTERPOLATED_PATH:
            reinterpret_cast<const ASTPath*>(mData.as_Fragment())->printPretty(ss, true);
            return ss.str();
        case TOK_INTERPOLATED_PATTERN:
            // TODO: Use a pretty printer too?
            return FMT(*reinterpret_cast<const ASTPattern*>(mData.as_Fragment()));
        case TOK_INTERPOLATED_STMT:
        case TOK_INTERPOLATED_BLOCK:
        case TOK_INTERPOLATED_EXPR: {
            ::std::stringstream ss;
            reinterpret_cast<const ASTExprNode*>(mData.as_Fragment())->print(ss);
            return ss.str();
        }
        case TOK_INTERPOLATED_META:
            return "/*:meta*/";
        case TOK_INTERPOLATED_STMT_ITEM:
            return "/*:stmt-item*/";
        case TOK_INTERPOLATED_ITEM:
            return "/*:item*/";
        case TOK_INTERPOLATED_VIS: {
            ::std::stringstream ss;
            ss << *reinterpret_cast<const ASTVisibility*>(mData.as_Fragment());
            return ss.str();
        }
        // Value tokens
        case TOK_IDENT:
            return mData.as_Ident().name.c_str();
        case TOK_LIFETIME:
            return FMT("'" << mData.as_Ident().name.c_str());
        case TOK_INTEGER: {
            auto v = mData.as_Integer().intval;
            switch (mData.as_Integer().datatype) {
                case CORETYPE_CHAR:
                    if (v >= 0x20 && v < 128) {
                        switch (v.truncateU64()) {
                            case '\'':
                                return "'\\''";
                            case '\\':
                                return "'\\\\'";
                            default:
                                return FMT("'" << (char)v.truncateU64() << "'");
                        }
                    }
                    return FMT("'\\u{" << ::std::hex << v << ::std::dec << "}'");
                case CORETYPE_ANY:
                    return FMT(mData.as_Integer().intval);
                default:
                    return FMT(mData.as_Integer().intval << "_" << coretypeName(mData.as_Integer().datatype));
            }
            break;
        }
        case TOK_CHAR:
            return FMT("'\\u{" << ::std::hex << mData.as_Integer().intval << "}");
        case TOK_FLOAT:
            if (mData.as_Float().datatype == CORETYPE_ANY) {
                return FMT(mData.as_Float().floatval);
            } else {
                return FMT(mData.as_Float().floatval << "_" << mData.as_Float().datatype);
            }
        case TOK_STRING:
            return FMT("\"" << EscapedString(mData.as_String()) << "\"");
        case TOK_CSTRING:
            return FMT("c\"" << EscapedString(mData.as_String()) << "\"");
        case TOK_BYTESTRING:
            return FMT("b\"" << mData.as_String() << "\"");
        case TOK_HASH:
            return "#";
        case TOK_UNDERSCORE:
            return "_";
        // Symbols
        case TOK_PAREN_OPEN:
            return "(";
        case TOK_PAREN_CLOSE:
            return ")";
        case TOK_BRACE_OPEN:
            return "{";
        case TOK_BRACE_CLOSE:
            return "}";
        case TOK_LT:
            return "<";
        case TOK_GT:
            return ">";
        case TOK_SQUARE_OPEN:
            return "[";
        case TOK_SQUARE_CLOSE:
            return "]";
        case TOK_COMMA:
            return ",";
        case TOK_SEMICOLON:
            return ";";
        case TOK_COLON:
            return ":";
        case TOK_DOUBLE_COLON:
            return "::";
        case TOK_STAR:
            return "*";
        case TOK_AMP:
            return "&";
        case TOK_PIPE:
            return "|";

        case TOK_FATARROW:
            return "=>";
        case TOK_THINARROW:
            return "->";
        case TOK_THINARROW_LEFT:
            return "<-";

        case TOK_PLUS:
            return "+";
        case TOK_DASH:
            return "-";
        case TOK_EXCLAM:
            return "!";
        case TOK_PERCENT:
            return "%";
        case TOK_SLASH:
            return "/";

        case TOK_DOT:
            return ".";
        case TOK_DOUBLE_DOT:
            return "..";
        case TOK_DOUBLE_DOT_EQUAL:
            return "..=";
        case TOK_TRIPLE_DOT:
            return "...";

        case TOK_EQUAL:
            return "=";
        case TOK_PLUS_EQUAL:
            return "+=";
        case TOK_DASH_EQUAL:
            return "-";
        case TOK_PERCENT_EQUAL:
            return "%=";
        case TOK_SLASH_EQUAL:
            return "/=";
        case TOK_STAR_EQUAL:
            return "*=";
        case TOK_AMP_EQUAL:
            return "&=";
        case TOK_PIPE_EQUAL:
            return "|=";

        case TOK_DOUBLE_EQUAL:
            return "==";
        case TOK_EXCLAM_EQUAL:
            return "!=";
        case TOK_GTE:
            return ">=";
        case TOK_LTE:
            return "<=";

        case TOK_DOUBLE_AMP:
            return "&&";
        case TOK_DOUBLE_PIPE:
            return "||";
        case TOK_DOUBLE_LT:
            return "<<";
        case TOK_DOUBLE_GT:
            return ">>";
        case TOK_DOUBLE_LT_EQUAL:
            return "<=";
        case TOK_DOUBLE_GT_EQUAL:
            return ">=";

        case TOK_DOLLAR:
            return "$";

        case TOK_QMARK:
            return "?";
        case TOK_AT:
            return "@";
        case TOK_TILDE:
            return "~";
        case TOK_BACKSLASH:
            return "\\";
        case TOK_CARET:
            return "^";
        case TOK_CARET_EQUAL:
            return "^=";
        case TOK_BACKTICK:
            return "`";

        // Reserved Words
        case TOK_RWORD_PUB:
            return "pub";
        case TOK_RWORD_PRIV:
            return "priv";
        case TOK_RWORD_MUT:
            return "mut";
        case TOK_RWORD_CONST:
            return "const";
        case TOK_RWORD_STATIC:
            return "static";
        case TOK_RWORD_UNSAFE:
            return "unsafe";
        case TOK_RWORD_EXTERN:
            return "extern";

        case TOK_RWORD_CRATE:
            return "crate";
        case TOK_RWORD_MOD:
            return "mod";
        case TOK_RWORD_STRUCT:
            return "struct";
        case TOK_RWORD_ENUM:
            return "enum";
        case TOK_RWORD_TRAIT:
            return "trait";
        case TOK_RWORD_FN:
            return "fn";
        case TOK_RWORD_USE:
            return "use";
        case TOK_RWORD_IMPL:
            return "impl";
        case TOK_RWORD_TYPE:
            return "type";

        case TOK_RWORD_WHERE:
            return "where";
        case TOK_RWORD_AS:
            return "as";

        case TOK_RWORD_LET:
            return "let";
        case TOK_RWORD_MATCH:
            return "match";
        case TOK_RWORD_IF:
            return "if";
        case TOK_RWORD_ELSE:
            return "else";
        case TOK_RWORD_LOOP:
            return "loop";
        case TOK_RWORD_WHILE:
            return "while";
        case TOK_RWORD_FOR:
            return "for";
        case TOK_RWORD_IN:
            return "in";
        case TOK_RWORD_DO:
            return "do";

        case TOK_RWORD_CONTINUE:
            return "continue";
        case TOK_RWORD_BREAK:
            return "break";
        case TOK_RWORD_RETURN:
            return "return";
        case TOK_RWORD_YIELD:
            return "yeild";
        case TOK_RWORD_BOX:
            return "box";
        case TOK_RWORD_REF:
            return "ref";

        case TOK_RWORD_FALSE:
            return "false";
        case TOK_RWORD_TRUE:
            return "true";
        case TOK_RWORD_SELF:
            return "self";
        case TOK_RWORD_SUPER:
            return "super";

        case TOK_RWORD_MOVE:
            return "move";

        case TOK_RWORD_ABSTRACT:
            return "abstract";
        case TOK_RWORD_FINAL:
            return "final";
        case TOK_RWORD_OVERRIDE:
            return "override";
        case TOK_RWORD_VIRTUAL:
            return "virtual";

        case TOK_RWORD_TYPEOF:
            return "typeof";

        case TOK_RWORD_BECOME:
            return "become";
        case TOK_RWORD_UNSIZED:
            return "unsized";
        case TOK_RWORD_MACRO:
            return "macro";

        // 2018
        case TOK_RWORD_ASYNC:
            return "async";
        case TOK_RWORD_AWAIT:
            return "await";
        case TOK_RWORD_DYN:
            return "dyn";
        case TOK_RWORD_TRY:
            return "try";
    }
    throw CompileErrorBugCheck("Reached end of Token::to_str");
}

::std::ostream& operator<<(::std::ostream& os, const Token& tok) {
    os << Token::typestr(tok.type());
    switch (tok.type()) {
        case TOK_STRING:
        case TOK_BYTESTRING:
            if (tok.mData.is_String()) {
                os << "\"" << EscapedString(tok.str()) << "\"";
            } else if (tok.mData.is_None())
                ;
            else {
                os << "?inner?";
            }
            os << tok.mHygiene;
            break;
        case TOK_IDENT:
        case TOK_LIFETIME:
            if (const auto* td = tok.mData.opt_Ident()) {
                os << "\"" << td->name << "\"" << td->hygiene;
            } else if (tok.mData.is_None())
                ;
            else {
                os << "?inner?";
            }
            break;
        case TOK_INTEGER:
            if (tok.mData.is_Integer()) {
                os << ":" << tok.intval();
            }
            break;
        case TOK_INTERPOLATED_TYPE:
            os << ":" << *reinterpret_cast<ASTType**>(tok.mData.as_Fragment());
            break;
        case TOK_INTERPOLATED_PATTERN:
            os << ":" << *reinterpret_cast<ASTPattern*>(tok.mData.as_Fragment());
            break;
        case TOK_INTERPOLATED_PATH:
            os << ":" << *reinterpret_cast<ASTPath*>(tok.mData.as_Fragment());
            break;
        case TOK_INTERPOLATED_EXPR:
            os << ":" << *reinterpret_cast<const ASTExprNode*>(tok.mData.as_Fragment());
            break;
        case TOK_INTERPOLATED_STMT:
            os << ":" << *reinterpret_cast<const ASTExprNode*>(tok.mData.as_Fragment());
            break;
        case TOK_INTERPOLATED_STMT_ITEM: {
            const auto& namedItem = *reinterpret_cast<const ASTNamed<ASTItem>*>(tok.mData.as_Fragment());
            os << ":" << namedItem.data.tagStr() << "(" << namedItem.name << ")";
        } break;
        case TOK_INTERPOLATED_BLOCK:
            os << ":" << *reinterpret_cast<const ASTExprNode*>(tok.mData.as_Fragment());
            break;
        case TOK_INTERPOLATED_META:
            os << ":" << *reinterpret_cast<ASTAttribute*>(tok.mData.as_Fragment());
            break;
        case TOK_INTERPOLATED_ITEM: {
            const auto& namedItem = *reinterpret_cast<const ASTNamed<ASTItem>*>(tok.mData.as_Fragment());
            os << ":" << namedItem.data.tagStr() << "(" << namedItem.name << ")";
        } break;
        default:
            break;
    }
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const Position& p) {
    return os << ::std::dec << p.filename << ":" << p.line;
}

Position::Position()
    : filename("")
    , line(0)
    , ofs(0)
{
}

Position::Position(Span sp)
    : span(std::move(sp))
    , filename("")
    , line(0)
    , ofs(0)
{
}

Position::Position(RcString filename, unsigned int line, unsigned int ofs)
    : filename(filename)
    , line(line)
    , ofs(ofs)
{
}

// Only for strings, for formatting

Token::Token(enum eTokenType t, Data d, Position p)
    : mType(t)
    , mData(::std::move(d))
    , pos(::std::move(p))
{
}

Token& Token::operator=(Token&& t) {
    if (this == &t) {
        return *this;
    }
    this->~Token();
    new (this) Token(::std::move(t));
    return *this;
}

Token::Token(Token&& t)
    : mType(t.mType)
    , mData(::std::move(t.mData))
    , pos(::std::move(t.pos))
    , mHygiene(std::move(t.mHygiene))
    , mIsDocComment(t.mIsDocComment)
{
    t.mType = TOK_NULL;
}

Token& Token::operator=(const Token& t) {
    this->~Token();
    new (this) Token(t);
    return *this;
}

// TODO: Replace these with a way of getting a InterpolatedFragment&
ASTType*& Token::fragType() {
    assert(mType == TOK_INTERPOLATED_TYPE);
    return *reinterpret_cast<ASTType**>(mData.as_Fragment());
}

ASTPath& Token::fragPath() {
    assert(mType == TOK_INTERPOLATED_PATH);
    return *reinterpret_cast<ASTPath*>(mData.as_Fragment());
}

ASTPattern& Token::fragPattern() {
    assert(mType == TOK_INTERPOLATED_PATTERN);
    return *reinterpret_cast<ASTPattern*>(mData.as_Fragment());
}

ASTAttribute& Token::fragMeta() {
    assert(mType == TOK_INTERPOLATED_META);
    return *reinterpret_cast<ASTAttribute*>(mData.as_Fragment());
}

bool Token::operator==(const Token& r) const {
    if (type() != r.type()) {
        return false;
    }
    TU_MATCH(Data, (mData, r.mData), (e, re), (None, return true;), (Ident, return e.sameName(re);), (String, return e == re;), (Integer, return e.datatype == re.datatype && e.intval == re.intval;), (Float, return e.datatype == re.datatype && e.floatval == re.floatval;), (Fragment, assert(!"Token equality on Fragment");))
    throw "";
}
