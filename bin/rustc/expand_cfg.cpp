#include "expand_cfg.h"
#include "synext.h"
#include "parse_common.h"
#include "parse_tokentree.h"
#include "parse_ttstream.h"
#include "ast_expr.h" // Needed to clear a ExprNodeP
#include "ast_crate.h"
#include "ast_attrs.h"
#include "parse_parseerror.h"

#include <map>
#include <optional>
#include <set>
#include <stdexcept>

::std::multimap<::std::string, ::std::string> gCfgValues;
::std::map<::std::string, ::std::function<bool(const ::std::string&)>> gCfgValueFcns;
::std::set<::std::string> gCfgFlags;

namespace {
    struct ExpectedCfgValues {
        bool any = false;
        bool none = false;
        ::std::set<::std::string> values;
    };

    struct LintSetting {
        bool isSet = false;
        CfgLintLevel level = CfgLintLevel::Warn;
    };

    struct CheckCfgState {
        bool active = false;
        bool exhaustiveNames = true;
        ::std::map<::std::string, ExpectedCfgValues> expected;
        LintSetting warnings;
        LintSetting unexpectedCfgs;
        LintSetting cap;
    } gCheckCfg;

    class CfgSpecParser {
        const ::std::string& input;
        size_t pos = 0;

    public:
        struct CheckSpec {
            bool anyNames = false;
            ::std::vector<::std::string> names;
            ExpectedCfgValues values;
        };

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
                    case '\\': rv += '\\'; break;
                    case '"': rv += '"'; break;
                    case 'n': rv += '\n'; break;
                    case 'r': rv += '\r'; break;
                    case 't': rv += '\t'; break;
                    case '0': rv += '\0'; break;
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

        CheckSpec parseCheckSpec() {
            if (ident() != "cfg") {
                fail("expected `cfg(...)`");
            }
            expect('(', "`(` after `cfg`");

            CheckSpec rv;
            bool sawValues = false;
            if (take(')')) {
                if (!atEnd()) {
                    fail("unexpected input after `cfg(...)`");
                }
                return rv;
            }

            for (;;) {
                auto word = ident();
                if (take('(')) {
                    if (word == "any") {
                        if (!rv.names.empty() || sawValues || rv.anyNames) {
                            fail("`cfg(any())` can only be provided in isolation");
                        }
                        expect(')', "`)` after `any(`");
                        rv.anyNames = true;
                    } else if (word == "values") {
                        if (rv.names.empty()) {
                            fail("`values()` cannot be specified before the names");
                        }
                        if (sawValues) {
                            fail("`values()` cannot be specified multiple times");
                        }
                        sawValues = true;
                        bool sawAny = false;
                        if (!take(')')) {
                            for (;;) {
                                skipWs();
                                if (pos < input.size() && input[pos] == '"') {
                                    if (sawAny) {
                                        fail("`values()` cannot combine string literals with `any()`");
                                    }
                                    rv.values.values.insert(stringLiteral());
                                } else {
                                    auto valueKind = ident();
                                    expect('(', "`(` in `values()` special value");
                                    expect(')', "`)` in `values()` special value");
                                    if (valueKind == "none") {
                                        if (sawAny) {
                                            fail("`values()` cannot combine `none()` with `any()`");
                                        }
                                        rv.values.none = true;
                                    } else if (valueKind == "any") {
                                        if (sawAny || rv.values.none || !rv.values.values.empty()) {
                                            fail("`values()` cannot combine `any()` with other values");
                                        }
                                        sawAny = true;
                                        rv.values.any = true;
                                    } else {
                                        fail("`values()` arguments must be string literals, `none()` or `any()`");
                                    }
                                }
                                if (take(')')) {
                                    break;
                                }
                                expect(',', "`,` between `values()` arguments");
                                if (take(')')) {
                                    break;
                                }
                            }
                        }
                    } else {
                        fail("`cfg()` arguments must be identifiers, `any()` or `values(...)`");
                    }
                } else {
                    if (sawValues) {
                        fail("`cfg()` names cannot be after values");
                    }
                    rv.names.push_back(::std::move(word));
                }

                if (take(')')) {
                    break;
                }
                expect(',', "`,` between `cfg()` arguments");
                if (take(')')) {
                    break;
                }
            }
            if (!atEnd()) {
                fail("unexpected input after `cfg(...)`");
            }
            if (rv.anyNames && (!rv.names.empty() || sawValues)) {
                fail("`cfg(any())` can only be provided in isolation");
            }
            if (!sawValues && !rv.anyNames) {
                rv.values.none = true;
            }
            return rv;
        }
    };

    bool isStickyLintLevel(CfgLintLevel level) {
        return level == CfgLintLevel::ForceWarn || level == CfgLintLevel::Forbid;
    }

    void updateLintSetting(LintSetting& setting, CfgLintLevel level) {
        if (setting.isSet && isStickyLintLevel(setting.level)) {
            return;
        }
        setting.isSet = true;
        setting.level = level;
    }

    unsigned lintLevelRank(CfgLintLevel level) {
        switch (level) {
            case CfgLintLevel::Allow: return 0;
            case CfgLintLevel::Warn:
            case CfgLintLevel::ForceWarn: return 1;
            case CfgLintLevel::Deny: return 2;
            case CfgLintLevel::Forbid: return 3;
        }
        throw ::std::logic_error("invalid lint level");
    }

    CfgLintLevel unexpectedCfgLevel() {
        auto level = gCheckCfg.unexpectedCfgs.isSet
            ? gCheckCfg.unexpectedCfgs.level
            : (gCheckCfg.warnings.isSet ? gCheckCfg.warnings.level : CfgLintLevel::Warn);
        if (level != CfgLintLevel::ForceWarn && gCheckCfg.cap.isSet
            && lintLevelRank(level) > lintLevelRank(gCheckCfg.cap.level)) {
            level = gCheckCfg.cap.level;
        }
        return level;
    }

    enum class BuiltinExpectation {
        UnknownName,
        Expected,
        UnexpectedValue,
    };

    BuiltinExpectation checkBuiltinCfg(const ::std::string& name, const ::std::optional<::std::string>& value) {
        static const ::std::set<::std::string> noValueNames = {
            "debug_assertions", "clippy", "doc", "doctest", "miri", "rustfmt",
            "overflow_checks", "proc_macro", "sanitizer_cfi_generalize_pointers",
            "sanitizer_cfi_normalize_integers", "target_thread_local", "ub_checks",
            "contract_checks", "unix", "windows",
        };
        static const ::std::set<::std::string> anyValueNames = {
            "fmt_debug", "panic", "relocation_model", "sanitize", "target_feature",
            "target_abi", "target_arch", "target_endian", "target_env", "target_family",
            "target_os", "target_pointer_width", "target_vendor",
        };
        static const ::std::set<::std::string> atomicNames = {
            "target_has_atomic", "target_has_atomic_equal_alignment", "target_has_atomic_load_store",
        };
        if (noValueNames.count(name) != 0) {
            return value ? BuiltinExpectation::UnexpectedValue : BuiltinExpectation::Expected;
        }
        if (anyValueNames.count(name) != 0) {
            return value ? BuiltinExpectation::Expected : BuiltinExpectation::UnexpectedValue;
        }
        if (atomicNames.count(name) != 0) {
            if (!value || *value == "ptr" || *value == "8" || *value == "16"
                || *value == "32" || *value == "64" || *value == "128") {
                return BuiltinExpectation::Expected;
            }
            return BuiltinExpectation::UnexpectedValue;
        }
        return BuiltinExpectation::UnknownName;
    }

    void reportUnexpectedCfg(const Span& span, const ::std::string& name, const ::std::optional<::std::string>& value, bool badValue) {
        const auto level = unexpectedCfgLevel();
        if (level == CfgLintLevel::Allow) {
            return;
        }
        const auto condition = value ? FMT("`" << name << " = \"" << *value << "\"`") : FMT("`" << name << "`");
        const auto message = badValue
            ? FMT("unexpected cfg condition value " << condition)
            : FMT("unexpected cfg condition name " << condition);
        if (level == CfgLintLevel::Warn || level == CfgLintLevel::ForceWarn) {
            WARNING(span, W0000, message);
        } else {
            ERROR(span, E0000, message);
        }
    }

    void validateCfgUse(const Span& span, const ::std::string& name, const ::std::optional<::std::string>& value) {
        if (!gCheckCfg.active) {
            return;
        }
        const auto it = gCheckCfg.expected.find(name);
        if (it != gCheckCfg.expected.end()) {
            const auto& expected = it->second;
            const auto valid = expected.any || (value ? expected.values.count(*value) != 0 : expected.none);
            if (!valid) {
                reportUnexpectedCfg(span, name, value, true);
            }
            return;
        }
        switch (checkBuiltinCfg(name, value)) {
            case BuiltinExpectation::Expected:
                return;
            case BuiltinExpectation::UnexpectedValue:
                reportUnexpectedCfg(span, name, value, true);
                return;
            case BuiltinExpectation::UnknownName:
                if (gCheckCfg.exhaustiveNames) {
                    reportUnexpectedCfg(span, name, value, false);
                }
                return;
        }
    }
}

static const RcString rcstringCfg = RcString::newInterned("cfg");

void CfgDump(::std::ostream& os) {
    for (const auto& v : gCfgValues) {
        os << ">" << v.first << "=" << v.second << std::endl;
    }
    for (const auto& f : gCfgFlags) {
        os << ">" << f << std::endl;
    }
    // NOTE: `g_cfg_value_fcns` is only used for feature flags, which minicargo doesn't need
}

void CfgSetFlag(::std::string name) {
    gCfgFlags.insert(mv$(name));
}

void CfgSetValue(::std::string name, ::std::string val) {
    gCfgValues.insert(::std::make_pair(mv$(name), mv$(val)));
}

void CfgSetValueCb(::std::string name, ::std::function<bool(const ::std::string&)> cb) {
    gCfgValueFcns.insert(::std::make_pair(mv$(name), mv$(cb)));
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

bool CfgSetCheckSpec(const ::std::string& spec, ::std::string& error) {
    try {
        auto parsed = CfgSpecParser(spec).parseCheckSpec();
        gCheckCfg.active = true;
        if (parsed.anyNames) {
            gCheckCfg.exhaustiveNames = false;
        }
        for (auto& name : parsed.names) {
            auto& expected = gCheckCfg.expected[name];
            if (parsed.values.any) {
                expected.any = true;
                expected.none = false;
                expected.values.clear();
            } else if (!expected.any) {
                expected.none |= parsed.values.none;
                expected.values.insert(parsed.values.values.begin(), parsed.values.values.end());
            }
        }
        return true;
    } catch (const ::std::exception& e) {
        error = e.what();
        return false;
    }
}

void CfgSetLintLevel(::std::string name, CfgLintLevel level) {
    for (auto& c : name) {
        if (c == '-') {
            c = '_';
        }
    }
    if (name == "warnings") {
        updateLintSetting(gCheckCfg.warnings, level);
    } else if (name == "unexpected_cfgs") {
        updateLintSetting(gCheckCfg.unexpectedCfgs, level);
    }
}

void CfgSetLintCap(CfgLintLevel level) {
    gCheckCfg.cap.isSet = true;
    gCheckCfg.cap.level = level;
}

namespace {
    bool checkCfgInner1(const RcString& name, TokenStream& lex);

    bool checkCfgInner(TokenStream& lex) {
        TRACE_FUNCTION;
        if (lex.lookahead(0) == TOK_INTERPOLATED_META) {
            auto meta = std::move(lex.getTokenCheck(TOK_INTERPOLATED_META).fragMeta());
            auto ilex = TTStream(meta.span(), ParseState(), meta.data());
            return checkCfgInner1(meta.name().asTrivial(), ilex);
        } else if (lex.lookahead(0) == TOK_RWORD_TRUE) {
            lex.getTokenCheck(TOK_RWORD_TRUE);
            return true;
        } else if (lex.lookahead(0) == TOK_RWORD_FALSE) {
            lex.getTokenCheck(TOK_RWORD_FALSE);
            return false;
        } else {
            auto name = lex.getTokenCheck(TOK_IDENT).ident().name;
            return checkCfgInner1(name, lex);
        }
    }

    bool checkCfgInner1(const RcString& name, TokenStream& lex) {
        // Some compiler-generated cfg streams have no source parent.  They do
        // not need a diagnostic span unless check-cfg is actually enabled.
        const auto conditionSpan = gCheckCfg.active ? lex.pointSpan() : Span();
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
                validateCfgUse(conditionSpan, name.c_str(), val);
                // Equality
                auto its = gCfgValues.equal_range(name.c_str());
                for (auto it = its.first; it != its.second; ++it) {
                    DEBUG(name << ": '" << it->second << "' == '" << val << "'");
                    if (it->second == val) {
                        return true;
                    }
                }
                if (its.first != its.second) {
                    return false;
                }

                auto it2 = gCfgValueFcns.find(name.c_str());
                if (it2 != gCfgValueFcns.end()) {
                    DEBUG(name << ": ('" << val << "')?");
                    return it2->second(val);
                }

                //WARNING(lex.point_span(), W0000, "Unknown cfg() param '" << name << "'");
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
                        rv |= checkCfgInner(lex);
                        if (lex.lookahead(0) != TOK_COMMA) {
                            break;
                        }
                        GET_CHECK_TOK(tok, lex, TOK_COMMA);
                    }
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    return rv;
                } else if (name == rcstringNot) {
                    bool rv = checkCfgInner(lex);
                    // Allow a trailing comma
                    lex.getTokenIf(TOK_COMMA);
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    return !rv;
                } else if (name == rcstringAll) {
                    bool rv = true;
                    while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
                        rv &= checkCfgInner(lex);
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
                        rv &= checkCfgInner1(canonical, lex);
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
                validateCfgUse(conditionSpan, name.c_str(), ::std::nullopt);
                auto its = gCfgValues.equal_range(name.c_str());
                for (auto it = its.first; it != its.second; ++it) {
                    return true;
                }
                // Flag
                auto it = gCfgFlags.find(name.c_str());
                return (it != gCfgFlags.end());
        }
    }
}

bool checkCfgStream(TokenStream& lex) {
    Token tok;
    bool rv = false;
    GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
    while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
        rv |= checkCfgInner(lex);
        if (lex.lookahead(0) != TOK_COMMA) {
            break;
        }
        GET_CHECK_TOK(tok, lex, TOK_COMMA);
    }
    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
    return rv;
}

bool checkCfg(const Span& sp, const ASTAttribute& mi) {
    TTStream lex(sp, ParseState(), mi.data());
    return checkCfgStream(lex);
}

bool checkCfgAttrs(const ASTAttributeList& attrs) {
    for (auto& a : attrs.mItems) {
        if (a.name() == rcstringCfg) {
            if (!checkCfg(a.span(), a)) {
                return false;
            }
        }
    }
    return true;
}

std::vector<ASTAttribute> checkCfgAttr(const ASTAttribute& mi) {
    TTStream lex(mi.span(), ParseState(), mi.data());

    Token tok;
    std::vector<ASTAttribute> rv;
    lex.getTokenCheck(TOK_PAREN_OPEN);
    auto cfgRes = checkCfgInner(lex);
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
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override {
        DEBUG("cfg!() - " << tt);
        auto lex = TTStream(sp, ParseState(), tt);
        bool rv = checkCfgInner(lex);
        lex.getTokenCheck(TOK_EOF);

        return box$(TTStreamO(sp, ParseState(), TokenTree(ASTEdition::Rust2015, {}, rv ? TOK_RWORD_TRUE : TOK_RWORD_FALSE)));
    }
};

class CCfgSelectExpander: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override {
        DEBUG("cfg_select!() - " << tt);
        auto lex = TTStream(sp, ParseState(), tt);
        for (;;) {
            bool rv = lex.getTokenIf(TOK_UNDERSCORE) || checkCfgInner(lex);
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

    void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate) const override {
        DEBUG("#[cfg] crate - " << mi);
        // Ignore, as #[cfg] on a crate is handled in expand/mod.cpp
        if (checkCfg(sp, mi)) {
        } else {
            // Remove all items (can't remove the module)
            crate.mRootModule.mItems.clear();
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        TRACE_FUNCTION_FR("#[cfg] item - " << mi, (i.is_None() ? "Deleted" : ""));
        if (checkCfg(sp, mi)) {
            // Leave
        } else {
            i = ASTItem::make_None({});
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        TRACE_FUNCTION_FR("#[cfg] item - " << mi, (i.is_None() ? "Deleted" : ""));
        if (checkCfg(sp, mi)) {
            // Leave
        } else {
            i = ASTItem::make_None({});
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override {
        TRACE_FUNCTION_FR("#[cfg] item - " << mi, (i.is_None() ? "Deleted" : ""));
        if (checkCfg(sp, mi)) {
            // Leave
        } else {
            i = ASTItem::make_None({});
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate, ASTExprNodeP& expr) const override {
        DEBUG("#[cfg] expr - " << mi);
        if (checkCfg(sp, mi)) {
            // Leave
        } else {
            expr.reset();
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate, ASTStructItem& si) const override {
        DEBUG("#[cfg] struct item - " << mi);
        if (!checkCfg(sp, mi)) {
            si.mName = RcString();
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate, ASTTupleItem& i) const override {
        DEBUG("#[cfg] tuple item - " << mi);
        if (!checkCfg(sp, mi)) {
            i.mType = ::TypeRef(sp);
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate, ASTEnumVariant& i) const override {
        DEBUG("#[cfg] enum variant - " << mi);
        if (!checkCfg(sp, mi)) {
            i.mName = RcString();
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate, ASTExprNodeMatchArm& i) const override {
        DEBUG("#[cfg] match arm - " << mi);
        if (!checkCfg(sp, mi)) {
            i.patterns.clear();
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate, ASTExprNodeStructLiteral::Ent& i) const override {
        DEBUG("#[cfg] struct lit - " << mi);
        if (!checkCfg(sp, mi)) {
            i.value.reset();
        }
    }
};

STATIC_MACRO("cfg", CCfgExpander);
STATIC_MACRO("cfg_select", CCfgSelectExpander);
STATIC_DECORATOR("cfg", CCfgHandler);
