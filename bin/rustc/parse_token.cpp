#include "parse_token.h"

#include "common.h"
#include "ast_ast.h"
#include "ast_expr.h" // for reasons
#include "ast_types.h"
#include "parse_parseerror.h"
#include "parse_interpolated_fragment.h"

#include <iomanip>

#include <std/str/view.h>
#include <std/str/builder.h>

Token::~Token() {
    switch (type_) {
        case TOK_INTERPOLATED_TYPE:
            delete reinterpret_cast<ASTType**>(data_.as_Fragment());
            break;
        case TOK_INTERPOLATED_PATTERN:
            delete reinterpret_cast<ASTPattern*>(data_.as_Fragment());
            break;
        case TOK_INTERPOLATED_PATH:
            delete reinterpret_cast<ASTPath*>(data_.as_Fragment());
            break;
        case TOK_INTERPOLATED_EXPR:
            delete reinterpret_cast<ASTExprNode*>(data_.as_Fragment());
            break;
        case TOK_INTERPOLATED_STMT:
            delete reinterpret_cast<ASTExprNode*>(data_.as_Fragment());
            break;
        case TOK_INTERPOLATED_STMT_ITEM:
        case TOK_INTERPOLATED_ITEM:
            delete reinterpret_cast<ASTNamed<ASTItem>*>(data_.as_Fragment());
            break;
        case TOK_INTERPOLATED_BLOCK:
            delete reinterpret_cast<ASTExprNode*>(data_.as_Fragment());
            break;
        case TOK_INTERPOLATED_META:
            delete reinterpret_cast<ASTAttribute*>(data_.as_Fragment());
            break;
        case TOK_INTERPOLATED_VIS:
            delete reinterpret_cast<ASTVisibility*>(data_.as_Fragment());
            break;
        default:
            break;
    }
}

Token::Token()
    : type_(TOK_NULL)
{
}

Token::Token(enum eTokenType type)
    : type_(type)
{
}

Token::Token(enum eTokenType type, Ident i)
    : type_(type)
    , data_(mv$(i))
{
}

Token::Token(enum eTokenType type, ::std::string str, Ident::Hygiene h)
    : type_(type)
    , data_(Data::make_String(mv$(str)))
    , hygiene_(std::move(h))
{
}

Token::Token(U128 val, enum eCoreType datatype)
    : type_(TOK_INTEGER)
    , data_(Data::make_Integer({datatype, val}))
{
}

Token Token::makeFloat(FloatValue val, enum eCoreType datatype) {
    auto rv = Token(TOK_FLOAT);
    rv.data_ = Data::make_Float({datatype, val});
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
    switch (frag.type) {
        case InterpolatedFragment::TT:
            UNREACHABLE();
        case InterpolatedFragment::VIS:
            type_ = TOK_INTERPOLATED_VIS;
            data_ = new ASTVisibility(*reinterpret_cast<const ASTVisibility*>(frag.ptr));
            break;
        case InterpolatedFragment::TYPE:
            type_ = TOK_INTERPOLATED_TYPE;
            data_ = new ASTType*((*reinterpret_cast<ASTType* const*>(frag.ptr))->clone());
            break;
        case InterpolatedFragment::PAT:
            type_ = TOK_INTERPOLATED_PATTERN;
            data_ = new ASTPattern(reinterpret_cast<const ASTPattern*>(frag.ptr)->clone());
            break;
        case InterpolatedFragment::PATH:
            type_ = TOK_INTERPOLATED_PATH;
            data_ = new ASTPath(*reinterpret_cast<const ASTPath*>(frag.ptr));
            break;
        case InterpolatedFragment::EXPR:
            type_ = TOK_INTERPOLATED_EXPR;
            if (0) {
                case InterpolatedFragment::STMT:
                    type_ = TOK_INTERPOLATED_STMT;
            }
            if (0) {
                case InterpolatedFragment::BLOCK:
                    type_ = TOK_INTERPOLATED_BLOCK;
            }

            data_ = reinterpret_cast<const ASTExprNode*>(frag.ptr)->clone().release();
            break;
        case InterpolatedFragment::META:
            type_ = TOK_INTERPOLATED_META;
            data_ = new ASTAttribute(reinterpret_cast<const ASTAttribute*>(frag.ptr)->clone());
            break;
        case InterpolatedFragment::STMT_ITEM:
        case InterpolatedFragment::ITEM: {
            type_ = frag.type == InterpolatedFragment::STMT_ITEM ? TOK_INTERPOLATED_STMT_ITEM : TOK_INTERPOLATED_ITEM;
            const auto& named = *reinterpret_cast<const ASTNamed<ASTItem>*>(frag.ptr);
            auto item = named.data.clone();
            data_ = new ASTNamed<ASTItem>(named.span, named.attrs.clone(), named.vis, named.name, mv$(item));
            break;
        }
    }
    if (frag.span) {
        pos = Position(frag.span);
    }
}

Token::Token(TagTakeIP, InterpolatedFragment frag) {
    switch (frag.type) {
        case InterpolatedFragment::TT:
            UNREACHABLE();
        case InterpolatedFragment::VIS:
            type_ = TOK_INTERPOLATED_VIS;
            data_ = frag.ptr;
            frag.ptr = nullptr;
            break;
        case InterpolatedFragment::TYPE:
            type_ = TOK_INTERPOLATED_TYPE;
            data_ = frag.ptr;
            frag.ptr = nullptr;
            break;
        case InterpolatedFragment::PAT:
            type_ = TOK_INTERPOLATED_PATTERN;
            data_ = frag.ptr;
            frag.ptr = nullptr;
            break;
        case InterpolatedFragment::PATH:
            type_ = TOK_INTERPOLATED_PATH;
            data_ = frag.ptr;
            frag.ptr = nullptr;
            break;
        case InterpolatedFragment::EXPR:
            type_ = TOK_INTERPOLATED_EXPR;
            if (0) {
                case InterpolatedFragment::STMT:
                    type_ = TOK_INTERPOLATED_STMT;
            }
            if (0) {
                case InterpolatedFragment::BLOCK:
                    type_ = TOK_INTERPOLATED_BLOCK;
            }

            data_ = reinterpret_cast<ASTExprNode*>(frag.ptr);
            frag.ptr = nullptr;
            break;
        case InterpolatedFragment::STMT_ITEM:
        case InterpolatedFragment::ITEM:
            type_ = frag.type == InterpolatedFragment::STMT_ITEM ? TOK_INTERPOLATED_STMT_ITEM : TOK_INTERPOLATED_ITEM;
            data_ = frag.ptr;
            frag.ptr = nullptr;
            break;
        case InterpolatedFragment::META:
            type_ = TOK_INTERPOLATED_META;
            data_ = frag.ptr;
            frag.ptr = nullptr;
            break;
    }
    if (frag.span) {
        pos = Position(std::move(frag.span));
    }
}

Token::Token(const Token& t)
    : type_(t.type_)
    , data_(Data::make_None({}))
    , pos(t.pos)
    , hygiene_(t.hygiene_)
    , isDocComment_(t.isDocComment_)
{
    assert(!t.data_.isDead());
    switch (t.data_.tag()) {
        case TokenData::TAG_None: {
            break;
        }
        case TokenData::TAG_Ident: {
            auto& e = t.data_.as_Ident();
            data_ = Data::make_Ident(e);
            break;
        }
        case TokenData::TAG_String: {
            auto& e = t.data_.as_String();
            data_ = Data::make_String(e);
            break;
        }
        case TokenData::TAG_Integer: {
            auto& e = t.data_.as_Integer();
            data_ = Data::make_Integer(e);
            break;
        }
        case TokenData::TAG_Float: {
            auto& e = t.data_.as_Float();
            data_ = Data::make_Float(e);
            break;
        }
        case TokenData::TAG_Fragment: {
            BUG(Span(Span(), t.pos), "Attempted to copy a fragment - " << t);
            break;
        }
    }
}

Token Token::clone() const {
    Token rv(type_);
    rv.pos = pos;
    rv.hygiene_ = hygiene_;
    rv.isDocComment_ = isDocComment_;

    assert(!data_.isDead());
    switch (data_.tag()) {
        case Data::TAG_None: {
            break;
        }
        case Data::TAG_Ident: {
            auto& e = data_.as_Ident();
            rv.data_ = Data::make_Ident(e);
            break;
        }
        case Data::TAG_String: {
            auto& e = data_.as_String();
            rv.data_ = Data::make_String(e);
            break;
        }
        case Data::TAG_Integer: {
            auto& e = data_.as_Integer();
            rv.data_ = Data::make_Integer(e);
            break;
        }
        case Data::TAG_Float: {
            auto& e = data_.as_Float();
            rv.data_ = Data::make_Float(e);
            break;
        }
        case Data::TAG_Fragment: {
            auto& e = data_.as_Fragment();
            assert(e); switch (type_) {
                case TOK_INTERPOLATED_TYPE:
                    rv.data_ = new ASTType*((*reinterpret_cast<ASTType**>(e))->clone());
                    break;
                case TOK_INTERPOLATED_PATTERN:
                    rv.data_ = new ASTPattern(reinterpret_cast<ASTPattern*>(e)->clone());
                    break;
                case TOK_INTERPOLATED_PATH:
                    rv.data_ = new ASTPath(*reinterpret_cast<ASTPath*>(e));
                    break;
                case TOK_INTERPOLATED_EXPR:
                    rv.data_ = reinterpret_cast<ASTExprNode*>(e)->clone().release();
                    break;
                case TOK_INTERPOLATED_STMT:
                    rv.data_ = reinterpret_cast<ASTExprNode*>(e)->clone().release();
                    break;
                case TOK_INTERPOLATED_BLOCK:
                    rv.data_ = reinterpret_cast<ASTExprNode*>(e)->clone().release();
                    break;
                case TOK_INTERPOLATED_META:
                    rv.data_ = new ASTAttribute(reinterpret_cast<ASTAttribute*>(e)->clone());
                    break;
                case TOK_INTERPOLATED_STMT_ITEM:
                case TOK_INTERPOLATED_ITEM: {
                    const auto& named = *reinterpret_cast<ASTNamed<ASTItem>*>(e);
                    auto item = named.data.clone();
                    rv.data_ = new ASTNamed<ASTItem>(named.span, named.attrs.clone(), named.vis, named.name, mv$(item));
                    break;
                }
                default:
                    BUG(Span(Span(), pos), "Fragment with invalid token type (" << *this << ")");
                    break;
            } assert(rv.data_.is_Fragment());
            break;
        }
    }
    return rv;
}

ASTExprNode& Token::fragNode() {
    assert(type_ == TOK_INTERPOLATED_EXPR || type_ == TOK_INTERPOLATED_STMT || type_ == TOK_INTERPOLATED_BLOCK);
    auto ptr = data_.as_Fragment();
    return *reinterpret_cast<ASTExprNode*>(ptr);
}

::std::unique_ptr<ASTExprNode> Token::takeFragNode() {
    assert(type_ == TOK_INTERPOLATED_EXPR || type_ == TOK_INTERPOLATED_STMT || type_ == TOK_INTERPOLATED_BLOCK);
    auto ptr = data_.as_Fragment();
    data_.as_Fragment() = nullptr;
    return ::std::unique_ptr<ASTExprNode>(reinterpret_cast<ASTExprNode*>(ptr));
}

ASTNamed<ASTItem> Token::takeFragItem() {
    assert(type_ == TOK_INTERPOLATED_ITEM);
    auto ptr = reinterpret_cast<ASTNamed<ASTItem>*>(data_.as_Fragment());
    data_.as_Fragment() = nullptr;
    auto rv = mv$(*ptr);
    delete ptr;
    return mv$(rv);
}

ASTNamed<ASTItem> Token::takeFragStmtItem() {
    assert(type_ == TOK_INTERPOLATED_STMT_ITEM);
    auto ptr = reinterpret_cast<ASTNamed<ASTItem>*>(data_.as_Fragment());
    data_.as_Fragment() = nullptr;
    auto rv = mv$(*ptr);
    delete ptr;
    return mv$(rv);
}

ASTVisibility Token::takeFragVis() {
    assert(type_ == TOK_INTERPOLATED_VIS);
    auto ptr = reinterpret_cast<ASTVisibility*>(data_.as_Fragment());
    data_.as_Fragment() = nullptr;
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

/// The text of a string literal, viewed as the bytes it holds.
static stl::StringView literalBytes(const ::std::string& s) {
    return stl::StringView(reinterpret_cast<const u8*>(s.data()), s.size());
}

struct EscapedString {
    stl::StringView s;

    EscapedString(stl::StringView s)
        : s(s)
    {
    }

    /// How many bytes the UTF-8 sequence starting at `i` runs for, or zero
    /// when the bytes there are not one.
    static size_t utf8Run(stl::StringView s, size_t i) {
        const u8 lead = s[i];
        const size_t len = (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 0;
        if (len == 0 || i + len > s.length()) {
            return 0;
        }
        for (size_t k = 1; k < len; k++) {
            if ((s[i + k] & 0xC0) != 0x80) {
                return 0;
            }
        }
        return len;
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const EscapedString& x) {
        for (size_t i = 0; i < x.s.length(); i++) {
            // A byte, not a char: `char` is signed here, and a high byte read
            // through it would print as its sign-extended self.
            const u8 b = x.s[i];
            switch (b) {
                case '"':
                    os << "\\\"";
                    continue;
                case '\\':
                    os << "\\\\";
                    continue;
                case '\n':
                    os << "\\n";
                    continue;
                case '\r':
                    os << "\\r";
                    continue;
                case '\t':
                    os << "\\t";
                    continue;
                default:
                    break;
            }
            if (' ' <= b && b < 0x7F) {
                os << static_cast<char>(b);
                continue;
            }
            if (b < 0x80) {
                // A control character is written by its value.
                os << "\\u{" << ::std::hex << static_cast<unsigned int>(b) << ::std::dec << "}";
                continue;
            }
            // Text passes through as itself; a byte that starts no sequence is
            // written as the byte it is.
            if (const auto run = utf8Run(x.s, i)) {
                os.write(reinterpret_cast<const char*>(x.s.data() + i), run);
                i += run - 1;
                continue;
            }
            os << "\\x" << ::std::hex << ::std::uppercase << static_cast<unsigned int>(b) << ::std::nouppercase << ::std::dec;
        }
        return os;
    }
};

/// A byte string holds bytes, not text: rustc writes every byte that is not
/// printable ASCII as a hex escape, since `\u{..}` has no meaning there.
struct EscapedByteString {
    stl::StringView s;

    EscapedByteString(stl::StringView s)
        : s(s)
    {
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const EscapedByteString& x) {
        for (size_t i = 0; i < x.s.length(); i++) {
            const u8 b = x.s[i];
            switch (b) {
                case '"':
                    os << "\\\"";
                    continue;
                case '\\':
                    os << "\\\\";
                    continue;
                case '\n':
                    os << "\\n";
                    continue;
                case '\r':
                    os << "\\r";
                    continue;
                case '\t':
                    os << "\\t";
                    continue;
                default:
                    break;
            }
            if (' ' <= b && b < 0x7F) {
                os << static_cast<char>(b);
                continue;
            }
            os << "\\x" << ::std::hex << ::std::uppercase << ::std::setw(2) << ::std::setfill('0') << static_cast<unsigned int>(b)
               << ::std::nouppercase << ::std::dec << ::std::setfill(' ');
        }
        return os;
    }
};

void printEscapedLiteral(::std::ostream& os, eTokenType type, const u8* value, size_t size) {
    const auto bytes = stl::StringView(value, size);
    switch (type) {
        case TOK_STRING:
            os << "\"" << EscapedString(bytes) << "\"";
            return;
        case TOK_BYTESTRING:
            os << "b\"" << EscapedByteString(bytes) << "\"";
            return;
        case TOK_CSTRING:
            os << "c\"" << EscapedString(bytes) << "\"";
            return;
        default:
            throw CompileErrorBugCheck("printEscapedLiteral called for a non-string token");
    }
}

namespace {
    /// The fewest hashes that let a raw literal hold `text`: one more than the
    /// longest run of `#` that follows a quote in it, and none without a quote.
    static size_t rawStringHashes(stl::StringView text) {
        size_t needed = 0;
        for (size_t i = 0; i < text.length(); i++) {
            if (text[i] != '"') {
                continue;
            }
            size_t run = 0;
            while (i + 1 + run < text.length() && text[i + 1 + run] == '#') {
                run++;
            }
            if (run + 1 > needed) {
                needed = run + 1;
            }
        }
        return needed;
    }

    /// Append `tt` to `out` as source, spacing the tokens the way whoever wrote
    /// them had to.  `prev` carries the token before the tree.
    static void appendTokenTreeSource(stl::StringBuilder& out, const TokenTree& tt, eTokenType& prev) {
        if (tt.isToken()) {
            if (!out.empty() && tokensNeedSpace(prev, tt.tok().type())) {
                out.append(" ", 1);
            }
            auto text = tt.tok().toStr();
            out.append(text.data(), text.size());
            prev = tt.tok().type();
        }
        for (size_t i = 0; i < tt.size(); i++) {
            appendTokenTreeSource(out, tt[i], prev);
        }
    }

    /// An attribute's meta item as source: `doc = "..."`, `cfg(unix)`, `C`.
    static void attributeToSource(stl::StringBuilder& out, const ASTAttribute& attr) {
        auto name = FMT(attr.name());
        out.append(name.data(), name.size());
        auto prev = TOK_IDENT;
        appendTokenTreeSource(out, attr.data(), prev);
    }
}

bool tokensNeedSpace(eTokenType prev, eTokenType cur) {
    // These bind to what is on their left: `x,` `x;` `x.y` `f()` `a[0]`.
    switch (cur) {
        case TOK_COMMA:
        case TOK_SEMICOLON:
        case TOK_DOT:
        case TOK_PAREN_CLOSE:
        case TOK_SQUARE_CLOSE:
        case TOK_QMARK:
        case TOK_DOUBLE_COLON:
            return false;
        default:
            break;
    }
    // And these to what is on their right: `.y` `#[a]` `$x` `(a` `[a`.
    switch (prev) {
        case TOK_DOT:
        case TOK_HASH:
        case TOK_DOLLAR:
        case TOK_PAREN_OPEN:
        case TOK_SQUARE_OPEN:
        case TOK_DOUBLE_COLON:
            return false;
        default:
            break;
    }
    // A macro call keeps its name, its `!` and its delimiter together.
    if (cur == TOK_EXCLAM && (prev == TOK_IDENT || Token::typeIsRword(prev))) {
        return false;
    }
    if (prev == TOK_EXCLAM && (cur == TOK_PAREN_OPEN || cur == TOK_SQUARE_OPEN || cur == TOK_BRACE_OPEN)) {
        return false;
    }
    // As does a call or an index.
    if ((cur == TOK_PAREN_OPEN || cur == TOK_SQUARE_OPEN)
        && (prev == TOK_IDENT || prev == TOK_PAREN_CLOSE || prev == TOK_SQUARE_CLOSE)) {
        return false;
    }
    return true;
}

::std::string Token::toStr() const {
    ::std::stringstream ss;
    switch (type_) {
        case TOK_NULL:
            return "/*null*/";
        case TOK_EOF:
            return "/*eof*/";

        case TOK_NEWLINE:
            return "\n";
        case TOK_WHITESPACE:
            return " ";
        case TOK_COMMENT:
            return "/*" + data_.as_String() + "*/";
        case TOK_INTERPOLATED_TYPE:
            (*reinterpret_cast<const ::ASTType**>(data_.as_Fragment()))->print(ss, false);
            return ss.str();
        case TOK_INTERPOLATED_PATH:
            reinterpret_cast<const ASTPath*>(data_.as_Fragment())->printPretty(ss, true);
            return ss.str();
        case TOK_INTERPOLATED_PATTERN:
            // TODO: Use a pretty printer too?
            return FMT(*reinterpret_cast<const ASTPattern*>(data_.as_Fragment()));
        case TOK_INTERPOLATED_STMT:
        case TOK_INTERPOLATED_BLOCK:
        case TOK_INTERPOLATED_EXPR: {
            ::std::stringstream ss;
            reinterpret_cast<const ASTExprNode*>(data_.as_Fragment())->print(ss);
            return ss.str();
        }
        case TOK_INTERPOLATED_META: {
            stl::StringBuilder out;
            attributeToSource(out, *reinterpret_cast<const ASTAttribute*>(data_.as_Fragment()));
            return {static_cast<const char*>(out.data()), out.used()};
        }
        case TOK_INTERPOLATED_STMT_ITEM:
            return "/*:stmt-item*/";
        case TOK_INTERPOLATED_ITEM:
            return "/*:item*/";
        case TOK_INTERPOLATED_VIS: {
            ::std::stringstream ss;
            ss << *reinterpret_cast<const ASTVisibility*>(data_.as_Fragment());
            return ss.str();
        }
        // Value tokens
        case TOK_IDENT:
            return data_.as_Ident().isRaw ? "r#" + ::std::string(data_.as_Ident().name.c_str()) : ::std::string(data_.as_Ident().name.c_str());
        case TOK_LIFETIME:
            return FMT("'" << data_.as_Ident().name.c_str());
        case TOK_INTEGER: {
            auto v = data_.as_Integer().intval;
            switch (data_.as_Integer().datatype) {
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
                    return FMT(data_.as_Integer().intval);
                default:
                    return FMT(data_.as_Integer().intval << coretypeName(data_.as_Integer().datatype));
            }
            break;
        }
        case TOK_CHAR:
            return FMT("'\\u{" << ::std::hex << data_.as_Integer().intval << "}");
        case TOK_FLOAT:
            if (data_.as_Float().datatype == CORETYPE_ANY) {
                return formatFloatValueForToken(data_.as_Float().floatval);
            } else {
                return FMT(formatFloatValueForToken(data_.as_Float().floatval) << coretypeName(data_.as_Float().datatype));
            }
        case TOK_STRING: {
            // A doc comment is a `#[doc = ...]` whose string rustc writes as a
            // raw literal, with just enough hashes to close it unambiguously.
            const auto& text = data_.as_String();
            if (!isDocComment_) {
                return FMT("\"" << EscapedString(literalBytes(text)) << "\"");
            }
            auto hashes = rawStringHashes(literalBytes(text));
            ss << "r";
            for (size_t i = 0; i < hashes; i++) {
                ss << "#";
            }
            ss << "\"" << text << "\"";
            for (size_t i = 0; i < hashes; i++) {
                ss << "#";
            }
            return ss.str();
        }
        case TOK_CSTRING:
            return FMT("c\"" << EscapedString(literalBytes(data_.as_String())) << "\"");
        case TOK_LITERAL_SUFFIXED:
            return data_.as_String();
        case TOK_BYTESTRING:
            return FMT("b\"" << EscapedByteString(literalBytes(data_.as_String())) << "\"");
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
        case TOK_LITERAL_SUFFIXED:
            if (tok.data_.is_String()) {
                os << "\"" << EscapedString(literalBytes(tok.str())) << "\"";
            } else if (tok.data_.is_None())
                ;
            else {
                os << "?inner?";
            }
            os << tok.hygiene_;
            break;
        case TOK_IDENT:
        case TOK_LIFETIME:
            if (const auto* td = tok.data_.opt_Ident()) {
                os << "\"" << td->name << "\"" << td->hygiene;
            } else if (tok.data_.is_None())
                ;
            else {
                os << "?inner?";
            }
            break;
        case TOK_INTEGER:
            if (tok.data_.is_Integer()) {
                os << ":" << tok.intval();
            }
            break;
        case TOK_INTERPOLATED_TYPE:
            os << ":" << *reinterpret_cast<ASTType**>(tok.data_.as_Fragment());
            break;
        case TOK_INTERPOLATED_PATTERN:
            os << ":" << *reinterpret_cast<ASTPattern*>(tok.data_.as_Fragment());
            break;
        case TOK_INTERPOLATED_PATH:
            os << ":" << *reinterpret_cast<ASTPath*>(tok.data_.as_Fragment());
            break;
        case TOK_INTERPOLATED_EXPR:
            os << ":" << *reinterpret_cast<const ASTExprNode*>(tok.data_.as_Fragment());
            break;
        case TOK_INTERPOLATED_STMT:
            os << ":" << *reinterpret_cast<const ASTExprNode*>(tok.data_.as_Fragment());
            break;
        case TOK_INTERPOLATED_STMT_ITEM: {
            const auto& namedItem = *reinterpret_cast<const ASTNamed<ASTItem>*>(tok.data_.as_Fragment());
            os << ":" << namedItem.data.tagStr() << "(" << namedItem.name << ")";
        } break;
        case TOK_INTERPOLATED_BLOCK:
            os << ":" << *reinterpret_cast<const ASTExprNode*>(tok.data_.as_Fragment());
            break;
        case TOK_INTERPOLATED_META:
            os << ":" << *reinterpret_cast<ASTAttribute*>(tok.data_.as_Fragment());
            break;
        case TOK_INTERPOLATED_ITEM: {
            const auto& namedItem = *reinterpret_cast<const ASTNamed<ASTItem>*>(tok.data_.as_Fragment());
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
    : type_(t)
    , data_(::std::move(d))
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
    : type_(t.type_)
    , data_(::std::move(t.data_))
    , pos(::std::move(t.pos))
    , hygiene_(std::move(t.hygiene_))
    , isDocComment_(t.isDocComment_)
{
    t.type_ = TOK_NULL;
}

Token& Token::operator=(const Token& t) {
    this->~Token();
    new (this) Token(t);
    return *this;
}

// TODO: Replace these with a way of getting a InterpolatedFragment&
ASTType*& Token::fragType() {
    assert(type_ == TOK_INTERPOLATED_TYPE);
    return *reinterpret_cast<ASTType**>(data_.as_Fragment());
}

ASTPath& Token::fragPath() {
    assert(type_ == TOK_INTERPOLATED_PATH);
    return *reinterpret_cast<ASTPath*>(data_.as_Fragment());
}

ASTPattern& Token::fragPattern() {
    assert(type_ == TOK_INTERPOLATED_PATTERN);
    return *reinterpret_cast<ASTPattern*>(data_.as_Fragment());
}

ASTAttribute& Token::fragMeta() {
    assert(type_ == TOK_INTERPOLATED_META);
    return *reinterpret_cast<ASTAttribute*>(data_.as_Fragment());
}

bool Token::operator==(const Token& r) const {
    if (type() != r.type()) {
        return false;
    }
    switch (data_.tag()) {
        case Data::TAG_None: {
            return true;
        }
        case Data::TAG_Ident: {
            auto& e = data_.as_Ident();
            auto& re = r.data_.as_Ident();
            return e.sameToken(re);
        }
        case Data::TAG_String: {
            auto& e = data_.as_String();
            auto& re = r.data_.as_String();
            return e == re;
        }
        case Data::TAG_Integer: {
            auto& e = data_.as_Integer();
            auto& re = r.data_.as_Integer();
            return e.datatype == re.datatype && e.intval == re.intval;
        }
        case Data::TAG_Float: {
            auto& e = data_.as_Float();
            auto& re = r.data_.as_Float();
            return e.datatype == re.datatype && e.floatval == re.floatval;
        }
        case Data::TAG_Fragment: {
            assert(!"Token equality on Fragment");
            break;
        }
    }
    UNREACHABLE();
}
