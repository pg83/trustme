#include "expand_cfg.h"

#include "synext.h"
#include "ast_expr.h" // Needed to clear a ExprNodeP
#include "settings.h"
#include "ast_attrs.h"
#include "ast_crate.h"
#include "wire_board.h"
#include "parse_common.h"
#include "parse_ttstream.h"
#include "parse_tokentree.h"
#include "parse_parseerror.h"

#include <std/mem/obj_pool.h>

#include <map>
#include <set>
#include <optional>
#include <stdexcept>

namespace {
}

// The cfg!() evaluation state: --cfg values/flags, value callbacks and the
// --check-cfg expectations with their lint settings. Opaque outside this
// file; Settings holds a pointer.
struct CfgState {
    ::std::multimap<::std::string, ::std::string> values;
    ::std::map<::std::string, ::std::function<bool(const ::std::string&)>> valueFcns;
    ::std::set<::std::string> flags;
};

CfgState* CfgCreateState(stl::ObjPool& pool) {
    return pool.make<CfgState>();
}

namespace {

    class CfgSpecParser {
        const ::std::string& input;
        size_t pos = 0;

    public:

        explicit CfgSpecParser(const ::std::string& input)
            : input(input)
        {
        }

        [[noreturn]] void fail(const ::std::string& message) const {
            throw ::std::runtime_error(message);
        }

        void skipWs() {
            while (pos < input.size()) {
                const auto c = static_cast<unsigned char>(input[pos]);
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                    break;
                }
                pos += 1;
            }
        }

        bool take(char c) {
            skipWs();
            if (pos < input.size() && input[pos] == c) {
                pos += 1;
                return true;
            }
            return false;
        }

        void expect(char c, const char* description) {
            if (!take(c)) {
                fail(FMT("expected " << description));
            }
        }

        static bool isIdentStart(unsigned char c) {
            return c == '_' || c >= 0x80 || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');
        }

        static bool isIdentContinue(unsigned char c) {
            return isIdentStart(c) || ('0' <= c && c <= '9');
        }

        ::std::string ident() {
            skipWs();
            const auto start = pos;
            if (pos >= input.size() || !isIdentStart(static_cast<unsigned char>(input[pos]))) {
                fail("expected an identifier");
            }
            pos += 1;
            while (pos < input.size() && isIdentContinue(static_cast<unsigned char>(input[pos]))) {
                pos += 1;
            }
            return input.substr(start, pos - start);
        }

        static unsigned hexDigit(char c) {
            if ('0' <= c && c <= '9') {
                return c - '0';
            }
            if ('a' <= c && c <= 'f') {
                return c - 'a' + 10;
            }
            if ('A' <= c && c <= 'F') {
                return c - 'A' + 10;
            }
            return 16;
        }

        ::std::string stringLiteral() {
            skipWs();
            if (pos >= input.size() || input[pos] != '"') {
                fail("expected a string literal");
            }
            pos += 1;
            ::std::string rv;
            while (pos < input.size()) {
                auto c = input[pos++];
                if (c == '"') {
                    return rv;
                }
                if (c != '\\') {
                    rv += c;
                    continue;
                }
                if (pos >= input.size()) {
                    fail("unterminated string escape");
                }
                c = input[pos++];
                switch (c) {
                    case '\\':
                        rv += '\\';
                        break;
                    case '"':
                        rv += '"';
                        break;
                    case 'n':
                        rv += '\n';
                        break;
                    case 'r':
                        rv += '\r';
                        break;
                    case 't':
                        rv += '\t';
                        break;
                    case '0':
                        rv += '\0';
                        break;
                    case 'x': {
                        if (pos + 2 > input.size()) {
                            fail("incomplete hexadecimal string escape");
                        }
                        const auto hi = hexDigit(input[pos]);
                        const auto lo = hexDigit(input[pos + 1]);
                        if (hi >= 16 || lo >= 16) {
                            fail("invalid hexadecimal string escape");
                        }
                        rv += static_cast<char>((hi << 4) | lo);
                        pos += 2;
                        break;
                    }
                    default:
                        fail(FMT("unsupported string escape \\" << c));
                }
            }
            fail("unterminated string literal");
        }

        bool atEnd() {
            skipWs();
            return pos == input.size();
        }

        ::std::pair<::std::string, ::std::optional<::std::string>> parseCfgOption() {
            auto name = ident();
            ::std::optional<::std::string> value;
            if (take('=')) {
                value = stringLiteral();
            }
            if (!atEnd()) {
                fail("expected `key` or `key=\"value\"`");
            }
            return {::std::move(name), ::std::move(value)};
        }

    };

}

static const RcString rcstringCfg = RcString::newInterned("cfg");

void CfgDump(const Settings& settings, ::std::ostream& os) {
    const auto& cfg = *settings.cfg;
    for (const auto& v : cfg.values) {
        os << ">" << v.first << "=" << v.second << std::endl;
    }
    for (const auto& f : cfg.flags) {
        os << ">" << f << std::endl;
    }
    // NOTE: `g_cfg_value_fcns` is only used for feature flags, which minicargo doesn't need
}

void CfgSetFlag(Settings& settings, ::std::string name) {
    auto& cfg = *settings.cfg;
    cfg.flags.insert(mv$(name));
}

void CfgSetValue(Settings& settings, ::std::string name, ::std::string val) {
    auto& cfg = *settings.cfg;
    cfg.values.insert(::std::make_pair(mv$(name), mv$(val)));
}

void CfgSetValueCb(Settings& settings, ::std::string name, ::std::function<bool(const ::std::string&)> cb) {
    auto& cfg = *settings.cfg;
    cfg.valueFcns.insert(::std::make_pair(mv$(name), mv$(cb)));
}

bool CfgParseOption(const ::std::string& spec, ::std::string& name, bool& hasValue, ::std::string& value, ::std::string& error) {
    try {
        auto parsed = CfgSpecParser(spec).parseCfgOption();
        name = ::std::move(parsed.first);
        hasValue = parsed.second.has_value();
        value = parsed.second ? ::std::move(*parsed.second) : ::std::string();
        return true;
    } catch (const ::std::exception& e) {
        error = e.what();
        return false;
    }
}

bool CfgSetCheckSpec(Settings& settings, const ::std::string& spec, ::std::string& error) {
    // Accepted for cargo compatibility; expectations are not checked.
    return true;
}

void CfgSetLintLevel(Settings& settings, ::std::string name, CfgLintLevel level) {
    // Accepted for cargo compatibility; cfg lints are not checked.
}

void CfgSetLintCap(Settings& settings, CfgLintLevel level) {
    // Accepted for cargo compatibility; cfg lints are not checked.
}

namespace {
    bool checkCfgInner1(const CfgState& cfg, const RcString& name, TokenStream& lex);

    bool checkCfgInner(const CfgState& cfg, TokenStream& lex) {
        TRACE_FUNCTION;
        if (lex.lookahead(0) == TOK_INTERPOLATED_META) {
            auto meta = std::move(lex.getTokenCheck(TOK_INTERPOLATED_META).fragMeta());
            auto ilex = TTStream(meta.span(), ParseState(), meta.data());
            return checkCfgInner1(cfg, meta.name().asTrivial(), ilex);
        } else if (lex.lookahead(0) == TOK_RWORD_TRUE) {
            lex.getTokenCheck(TOK_RWORD_TRUE);
            return true;
        } else if (lex.lookahead(0) == TOK_RWORD_FALSE) {
            lex.getTokenCheck(TOK_RWORD_FALSE);
            return false;
        } else {
            auto name = lex.getTokenCheck(TOK_IDENT).ident().name;
            return checkCfgInner1(cfg, name, lex);
        }
    }

    bool checkCfgInner1(const CfgState& cfg, const RcString& name, TokenStream& lex) {
        // Some compiler-generated cfg streams have no source parent.  They do
        // not need a diagnostic span unless check-cfg is actually enabled.
        Token tok;
        switch (lex.lookahead(0)) {
            case TOK_EQUAL: {
                GET_CHECK_TOK(tok, lex, TOK_EQUAL);
                std::string val;
                if (lex.lookahead(0) == TOK_INTERPOLATED_EXPR) {
                    auto n = lex.getTokenCheck(TOK_INTERPOLATED_EXPR).takeFragNode();
                    const auto* np = cast<ASTExprNodeString>(n.get());
                    ASSERT_BUG(n->span(), np, "");
                    val = np->mValue;
                } else {
                    GET_CHECK_TOK(tok, lex, TOK_STRING);
                    val = tok.str();
                }
                // Equality
                auto its = cfg.values.equal_range(name.c_str());
                for (auto it = its.first; it != its.second; ++it) {
                    DEBUG(name << ": '" << it->second << "' == '" << val << "'");
                    if (it->second == val) {
                        return true;
                    }
                }
                if (its.first != its.second) {
                    return false;
                }

                auto it2 = cfg.valueFcns.find(name.c_str());
                if (it2 != cfg.valueFcns.end()) {
                    DEBUG(name << ": ('" << val << "')?");
                    return it2->second(val);
                }

                return false;
            }
            case TOK_PAREN_OPEN:
                GET_TOK(tok, lex);

                static const RcString rcstringAny = RcString::newInterned("any");
                static const RcString rcstringNot = RcString::newInterned("not");
                static const RcString rcstringAll = RcString::newInterned("all");
                static const RcString rcstringTarget = RcString::newInterned("target");
                if (name == rcstringAny || name == rcstringCfg) {
                    bool rv = false;
                    while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
                        rv |= checkCfgInner(cfg, lex);
                        if (lex.lookahead(0) != TOK_COMMA) {
                            break;
                        }
                        GET_CHECK_TOK(tok, lex, TOK_COMMA);
                    }
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    return rv;
                } else if (name == rcstringNot) {
                    bool rv = checkCfgInner(cfg, lex);
                    // Allow a trailing comma
                    lex.getTokenIf(TOK_COMMA);
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    return !rv;
                } else if (name == rcstringAll) {
                    bool rv = true;
                    while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
                        rv &= checkCfgInner(cfg, lex);
                        if (lex.lookahead(0) != TOK_COMMA) {
                            break;
                        }
                        GET_CHECK_TOK(tok, lex, TOK_COMMA);
                    }
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    return rv;
                } else if (name == rcstringTarget) {
                    // `target(os = "linux", arch = "x86_64")` is the compact
                    // spelling of `all(target_os = "linux", target_arch =
                    // "x86_64")`.  Keep evaluation in the ordinary cfg path
                    // so check-cfg sees the canonical target_* names too.
                    bool rv = true;
                    while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
                        const auto field = lex.getTokenCheck(TOK_IDENT).ident().name;
                        const auto canonical = RcString::newInterned(FMT("target_" << field));
                        rv &= checkCfgInner1(cfg, canonical, lex);
                        if (lex.lookahead(0) != TOK_COMMA) {
                            break;
                        }
                        GET_CHECK_TOK(tok, lex, TOK_COMMA);
                    }
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    return rv;
                } else {
                    // oops
                    ERROR(lex.pointSpan(), E0000, "Unknown cfg() function - " << name);
                }

                break;
            default:
                auto its = cfg.values.equal_range(name.c_str());
                for (auto it = its.first; it != its.second; ++it) {
                    return true;
                }
                // Flag
                auto it = cfg.flags.find(name.c_str());
                return (it != cfg.flags.end());
        }
    }
}

bool checkCfgStream(const Settings& settings, TokenStream& lex) {
    const auto& cfg = *settings.cfg;
    Token tok;
    bool rv = false;
    GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
    while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
        rv |= checkCfgInner(cfg, lex);
        if (lex.lookahead(0) != TOK_COMMA) {
            break;
        }
        GET_CHECK_TOK(tok, lex, TOK_COMMA);
    }
    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
    return rv;
}

bool checkCfg(const Settings& settings, const Span& sp, const ASTAttribute& mi) {
    TTStream lex(sp, ParseState(), mi.data());
    return checkCfgStream(settings, lex);
}

bool checkCfgAttrs(const Settings& settings, const ASTAttributeList& attrs) {
    for (auto& a : attrs.mItems) {
        if (a.name() == rcstringCfg) {
            if (!checkCfg(settings, a.span(), a)) {
                return false;
            }
        }
    }
    return true;
}

std::vector<ASTAttribute> checkCfgAttr(const Settings& settings, const ASTAttribute& mi) {
    const auto& cfg = *settings.cfg;
    TTStream lex(mi.span(), ParseState(), mi.data());

    Token tok;
    std::vector<ASTAttribute> rv;
    lex.getTokenCheck(TOK_PAREN_OPEN);
    auto cfgRes = checkCfgInner(cfg, lex);
    while (lex.lookahead(0) == TOK_COMMA) {
        lex.getTokenCheck(TOK_COMMA);
        rv.push_back(ParseMetaItem(lex));
    }
    lex.getTokenCheck(TOK_PAREN_CLOSE);
    lex.getTokenCheck(TOK_EOF);
    if (cfgRes) {
        return rv;
    } else {
        return std::vector<ASTAttribute>();
    }
}

class CCfgExpander: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override {
        DEBUG("cfg!() - " << tt);
        auto lex = TTStream(sp, ParseState(), tt);
        const auto& cfg = *wb.settings->cfg;
        bool rv = checkCfgInner(cfg, lex);
        lex.getTokenCheck(TOK_EOF);

        return box$(TTStreamO(sp, ParseState(), TokenTree(ASTEdition::Rust2015, {}, rv ? TOK_RWORD_TRUE : TOK_RWORD_FALSE)));
    }
};

class CCfgSelectExpander: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override {
        DEBUG("cfg_select!() - " << tt);
        auto lex = TTStream(sp, ParseState(), tt);
        for (;;) {
            const auto& cfg = *wb.settings->cfg;
            bool rv = lex.getTokenIf(TOK_UNDERSCORE) || checkCfgInner(cfg, lex);
            lex.getTokenCheck(TOK_FATARROW);
            auto t = ParseTT(lex, true);
            if (rv) {
                return box$(TTStreamO(sp, ParseState(), std::move(t)));
            }
        }
        lex.getTokenCheck(TOK_EOF);

        ERROR(sp, E0000, "cfg_select - Nothing matched");
    }
};

class CCfgHandler: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override {
        DEBUG("#[cfg] crate - " << mi);
        // Ignore, as #[cfg] on a crate is handled in expand/mod.cpp
        if (checkCfg(*wb.settings, sp, mi)) {
        } else {
            // Remove all items (can't remove the module)
            crate.mRootModule.mItems.clear();
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        TRACE_FUNCTION_FR("#[cfg] item - " << mi, (i.is_None() ? "Deleted" : ""));
        if (checkCfg(*wb.settings, sp, mi)) {
            // Leave
        } else {
            i = ASTItem::make_None({});
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        TRACE_FUNCTION_FR("#[cfg] item - " << mi, (i.is_None() ? "Deleted" : ""));
        if (checkCfg(*wb.settings, sp, mi)) {
            // Leave
        } else {
            i = ASTItem::make_None({});
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override {
        TRACE_FUNCTION_FR("#[cfg] item - " << mi, (i.is_None() ? "Deleted" : ""));
        if (checkCfg(*wb.settings, sp, mi)) {
            // Leave
        } else {
            i = ASTItem::make_None({});
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeP& expr) const override {
        DEBUG("#[cfg] expr - " << mi);
        if (checkCfg(*wb.settings, sp, mi)) {
            // Leave
        } else {
            expr.reset();
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTStructItem& si) const override {
        DEBUG("#[cfg] struct item - " << mi);
        if (!checkCfg(*wb.settings, sp, mi)) {
            si.mName = RcString();
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTTupleItem& i) const override {
        DEBUG("#[cfg] tuple item - " << mi);
        if (!checkCfg(*wb.settings, sp, mi)) {
            i.mType = ::TypeRef(sp);
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTEnumVariant& i) const override {
        DEBUG("#[cfg] enum variant - " << mi);
        if (!checkCfg(*wb.settings, sp, mi)) {
            i.mName = RcString();
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeMatchArm& i) const override {
        DEBUG("#[cfg] match arm - " << mi);
        if (!checkCfg(*wb.settings, sp, mi)) {
            i.patterns.clear();
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeStructLiteral::Ent& i) const override {
        DEBUG("#[cfg] struct lit - " << mi);
        if (!checkCfg(*wb.settings, sp, mi)) {
            i.value.reset();
        }
    }
};

STATIC_MACRO("cfg", CCfgExpander);
STATIC_MACRO("cfg_select", CCfgSelectExpander);
STATIC_DECORATOR("cfg", CCfgHandler);
