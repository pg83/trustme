#include "expand_cfg.h"

#include "synext.h"
#include "ast_expr.h"
#include "settings.h"
#include "ast_attrs.h"
#include "ast_crate.h"
#include "wire_board.h"
#include "parse_common.h"
#include "parse_ttstream.h"
#include "target_version.h"
#include "parse_tokentree.h"
#include "parse_parseerror.h"

#include <std/mem/obj_pool.h>

#include <map>
#include <set>
#include <array>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <stdexcept>

using namespace stl;

namespace {}

struct CfgState {
    ObjPool* pool;
    std::multimap<std::string, std::string> values;
    std::map<std::string, CfgValueCallback*> valueFcns;
    std::set<std::string> flags;

    explicit CfgState(ObjPool& pool);
};

struct CCfgExpander: public ExpandProcMacro {
    std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
};

struct CCfgSelectExpander: public ExpandProcMacro {
    std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
};

struct CCfgHandler: public ExpandDecorator {
    AttrStage stage() const override;

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override;

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override;

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeP& expr) const override;

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTStructItem& si) const override;

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTTupleItem& i) const override;

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTEnumVariant& i) const override;

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeMatchArm& i) const override;

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeStructLiteral::Ent& i) const override;
};

namespace {
    struct CfgSpecParser {
        const std::string& input;
        size_t pos = 0;

        explicit CfgSpecParser(const std::string& input);

        [[noreturn]] void fail(const std::string& message) const;

        void skipWs();

        bool take(char c);

        void expect(char c, const char* description);

        static bool isIdentStart(unsigned char c);

        static bool isIdentContinue(unsigned char c);

        std::string ident();

        static unsigned hexDigit(char c);

        std::string stringLiteral();

        bool atEnd();

        std::pair<std::string, std::optional<std::string>> parseCfgOption();
    };
}

CfgState* CfgCreateState(ObjPool& pool) {
    return pool.make<CfgState>(pool);
}

void CfgDump(const Settings& settings, std::ostream& os) {
    const auto& cfg = *settings.cfg;
    for (const auto& v : cfg.values) {
        os << ">" << v.first << "=" << v.second << std::endl;
    }
    for (const auto& f : cfg.flags) {
        os << ">" << f << std::endl;
    }
}

void CfgSetFlag(Settings& settings, std::string name) {
    auto& cfg = *settings.cfg;
    cfg.flags.insert(mv$(name));
}

void CfgSetValue(Settings& settings, std::string name, std::string val) {
    auto& cfg = *settings.cfg;
    cfg.values.insert(std::make_pair(mv$(name), mv$(val)));
}

void CfgSetValueCallback(Settings& settings, CfgString name, const CfgValueCallback& cb) {
    auto& cfg = *settings.cfg;
    cfg.valueFcns.insert(std::make_pair(mv$(name), cb.cloneIn(*cfg.pool)));
}

void CfgParseOption(const std::string& spec, std::string& name, bool& hasValue, std::string& value) {
    auto parsed = CfgSpecParser(spec).parseCfgOption();
    name = std::move(parsed.first);
    hasValue = parsed.second.has_value();
    value = parsed.second ? std::move(*parsed.second) : std::string();
}

bool CfgSetCheckSpec(Settings& settings, const std::string& spec, std::string& error) {
    return true;
}

void CfgSetLintLevel(Settings& settings, std::string name, CfgLintLevel level) {
    auto it = settings.lintLevels.find(name);
    if (it != settings.lintLevels.end() && it->second == CfgLintLevel::Forbid) {
        return;
    }
    settings.lintLevels[std::move(name)] = level;
}

void CfgSetLintCap(Settings& settings, CfgLintLevel level) {
    settings.lintCap = level;
}

namespace {
    bool checkCfgInner1(const CfgState& cfg, const RcString& name, TokenStream& lex);

    bool checkCfgInner(const CfgState& cfg, TokenStream& lex) {
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
        Token tok;
        switch (lex.lookahead(0)) {
            case TOK_EQUAL: {
                GET_CHECK_TOK(tok, lex, TOK_EQUAL);
                std::string val;
                if (lex.lookahead(0) == TOK_INTERPOLATED_EXPR) {
                    auto n = lex.getTokenCheck(TOK_INTERPOLATED_EXPR).takeFragNode();
                    const auto* np = cast<ASTExprNodeString>(n.get());
                    ASSERT_BUG(n->span(), np, "");
                    val = np->value;
                } else {
                    GET_CHECK_TOK(tok, lex, TOK_STRING);
                    val = tok.str();
                }
                auto its = cfg.values.equal_range(name.c_str());
                for (auto it = its.first; it != its.second; ++it) {
                    if (it->second == val) {
                        return true;
                    }
                }
                if (its.first != its.second) {
                    return false;
                }

                auto it2 = cfg.valueFcns.find(name.c_str());
                if (it2 != cfg.valueFcns.end()) {
                    return it2->second->matches(val);
                }

                return false;
            }
            case TOK_PAREN_OPEN:
                GET_TOK(tok, lex);

                if (name == "any" || name == "cfg") {
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
                } else if (name == "not") {
                    bool rv = checkCfgInner(cfg, lex);
                    lex.getTokenIf(TOK_COMMA);
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    return !rv;
                } else if (name == "all") {
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
                } else if (name == "target") {
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
                } else if (name == "version") {
                    auto wanted = lex.getTokenCheck(TOK_STRING).str();
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);

                    struct H {
                        static std::array<unsigned, 3> parse(const std::string& v) {
                            std::array<unsigned, 3> rv = {0, 0, 0};
                            size_t pos = 0;
                            for (unsigned i = 0; i < 3 && pos < v.size(); i++) {
                                unsigned val = 0;
                                while (pos < v.size() && std::isdigit(static_cast<unsigned char>(v[pos]))) {
                                    val = val * 10 + static_cast<unsigned>(v[pos] - '0');
                                    pos++;
                                }
                                rv[i] = val;
                                if (pos < v.size() && v[pos] == '.') {
                                    pos++;
                                } else {
                                    break;
                                }
                            }
                            return rv;
                        }
                    };

                    const char* override = std::getenv("RUSTC_OVERRIDE_VERSION_STRING");
                    auto have = H::parse(override ? std::string(override) : std::string(RUSTC_TARGET_VERSION) + ".100");
                    auto want = H::parse(wanted);
                    return have >= want;
                } else {
                    ERROR(lex.pointSpan(), E0000, "Unknown cfg() function - " << name);
                }

                break;
            default:
                auto its = cfg.values.equal_range(name.c_str());
                for (auto it = its.first; it != its.second; ++it) {
                    return true;
                }
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
    for (auto& a : attrs.items) {
        if (a.name() == "cfg") {
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

void RegisterCfgBuiltins(ExpandRegistry& registry) {
    registry.addMacro<CCfgExpander>("cfg");
    registry.addMacro<CCfgSelectExpander>("cfg_select");
    registry.addDecorator<CCfgHandler>("cfg");
}

CfgState::CfgState(ObjPool& pool)
    : pool(&pool)
{
}

CfgSpecParser::CfgSpecParser(const std::string& input)
    : input(input)
{
}

[[noreturn]] auto CfgSpecParser::fail(const std::string& message) const -> void {
    throw std::runtime_error("invalid `--cfg` argument `" + input + "`: " + message);
}

auto CfgSpecParser::skipWs() -> void {
    while (pos < input.size()) {
        const auto c = static_cast<unsigned char>(input[pos]);
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        pos += 1;
    }
}

auto CfgSpecParser::take(char c) -> bool {
    skipWs();
    if (pos < input.size() && input[pos] == c) {
        pos += 1;
        return true;
    }
    return false;
}

auto CfgSpecParser::expect(char c, const char* description) -> void {
    if (!take(c)) {
        fail(FMT("expected " << description));
    }
}

auto CfgSpecParser::isIdentStart(unsigned char c) -> bool {
    return c == '_' || c >= 0x80 || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');
}

auto CfgSpecParser::isIdentContinue(unsigned char c) -> bool {
    return isIdentStart(c) || ('0' <= c && c <= '9');
}

auto CfgSpecParser::ident() -> std::string {
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

auto CfgSpecParser::hexDigit(char c) -> unsigned {
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

auto CfgSpecParser::stringLiteral() -> std::string {
    skipWs();
    if (pos >= input.size() || input[pos] != '"') {
        fail("expected a string literal");
    }
    pos += 1;
    std::string rv;
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

auto CfgSpecParser::atEnd() -> bool {
    skipWs();
    return pos == input.size();
}

auto CfgSpecParser::parseCfgOption() -> std::pair<std::string, std::optional<std::string>> {
    auto name = ident();
    std::optional<std::string> value;
    if (take('=')) {
        value = stringLiteral();
    }
    if (!atEnd()) {
        fail("expected `key` or `key=\"value\"`");
    }
    return {std::move(name), std::move(value)};
}

auto CCfgExpander::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    auto lex = TTStream(sp, ParseState(), tt);
    const auto& cfg = *wb.settings->cfg;
    bool rv = checkCfgInner(cfg, lex);
    lex.getTokenCheck(TOK_EOF);

    return box$(TTStreamO(sp, ParseState(), TokenTree(ASTEdition::Rust2015, {}, rv ? TOK_RWORD_TRUE : TOK_RWORD_FALSE)));
}

auto CCfgSelectExpander::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
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

auto CCfgHandler::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CCfgHandler::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const -> void {
    if (!checkCfg(*wb.settings, sp, mi)) {
        crate.rootModule_.items.clear();
    }
}

auto CCfgHandler::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (!checkCfg(*wb.settings, sp, mi)) {
        i = ASTItem::make_None({});
    }
}

auto CCfgHandler::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (!checkCfg(*wb.settings, sp, mi)) {
        i = ASTItem::make_None({});
    }
}

auto CCfgHandler::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const -> void {
    if (!checkCfg(*wb.settings, sp, mi)) {
        i = ASTItem::make_None({});
    }
}

auto CCfgHandler::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeP& expr) const -> void {
    if (!checkCfg(*wb.settings, sp, mi)) {
        expr.reset();
    }
}

auto CCfgHandler::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTStructItem& si) const -> void {
    if (!checkCfg(*wb.settings, sp, mi)) {
        si.name = RcString();
    }
}

auto CCfgHandler::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTTupleItem& i) const -> void {
    if (!checkCfg(*wb.settings, sp, mi)) {
        i.type = ::mkType(*crate.pool, sp);
    }
}

auto CCfgHandler::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTEnumVariant& i) const -> void {
    if (!checkCfg(*wb.settings, sp, mi)) {
        i.name = RcString();
    }
}

auto CCfgHandler::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeMatchArm& i) const -> void {
    if (!checkCfg(*wb.settings, sp, mi)) {
        i.patterns.clear();
    }
}

auto CCfgHandler::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeStructLiteral::Ent& i) const -> void {
    if (!checkCfg(*wb.settings, sp, mi)) {
        i.value.reset();
    }
}
