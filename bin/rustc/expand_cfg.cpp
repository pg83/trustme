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

::std::multimap<::std::string, ::std::string> g_cfg_values;
::std::map<::std::string, ::std::function<bool(const ::std::string&)>> g_cfg_value_fcns;
::std::set<::std::string> g_cfg_flags;

namespace {
    struct ExpectedCfgValues {
        bool any = false;
        bool none = false;
        ::std::set<::std::string> values;
    };

    struct LintSetting {
        bool is_set = false;
        CfgLintLevel level = CfgLintLevel::Warn;
    };

    struct CheckCfgState {
        bool active = false;
        bool exhaustive_names = true;
        ::std::map<::std::string, ExpectedCfgValues> expected;
        LintSetting warnings;
        LintSetting unexpected_cfgs;
        LintSetting cap;
    } g_check_cfg;

    class CfgSpecParser {
        const ::std::string& m_input;
        size_t m_pos = 0;

    public:
        struct CheckSpec {
            bool any_names = false;
            ::std::vector<::std::string> names;
            ExpectedCfgValues values;
        };

        explicit CfgSpecParser(const ::std::string& input)
            : m_input(input)
        {
        }

        [[noreturn]] void fail(const ::std::string& message) const {
            throw ::std::runtime_error(message);
        }

        void skip_ws() {
            while (m_pos < m_input.size()) {
                const auto c = static_cast<unsigned char>(m_input[m_pos]);
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                    break;
                }
                m_pos += 1;
            }
        }

        bool take(char c) {
            skip_ws();
            if (m_pos < m_input.size() && m_input[m_pos] == c) {
                m_pos += 1;
                return true;
            }
            return false;
        }

        void expect(char c, const char* description) {
            if (!take(c)) {
                fail(FMT("expected " << description));
            }
        }

        static bool is_ident_start(unsigned char c) {
            return c == '_' || c >= 0x80 || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');
        }

        static bool is_ident_continue(unsigned char c) {
            return is_ident_start(c) || ('0' <= c && c <= '9');
        }

        ::std::string ident() {
            skip_ws();
            const auto start = m_pos;
            if (m_pos >= m_input.size() || !is_ident_start(static_cast<unsigned char>(m_input[m_pos]))) {
                fail("expected an identifier");
            }
            m_pos += 1;
            while (m_pos < m_input.size() && is_ident_continue(static_cast<unsigned char>(m_input[m_pos]))) {
                m_pos += 1;
            }
            return m_input.substr(start, m_pos - start);
        }

        static unsigned hex_digit(char c) {
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

        ::std::string string_literal() {
            skip_ws();
            if (m_pos >= m_input.size() || m_input[m_pos] != '"') {
                fail("expected a string literal");
            }
            m_pos += 1;
            ::std::string rv;
            while (m_pos < m_input.size()) {
                auto c = m_input[m_pos++];
                if (c == '"') {
                    return rv;
                }
                if (c != '\\') {
                    rv += c;
                    continue;
                }
                if (m_pos >= m_input.size()) {
                    fail("unterminated string escape");
                }
                c = m_input[m_pos++];
                switch (c) {
                    case '\\': rv += '\\'; break;
                    case '"': rv += '"'; break;
                    case 'n': rv += '\n'; break;
                    case 'r': rv += '\r'; break;
                    case 't': rv += '\t'; break;
                    case '0': rv += '\0'; break;
                    case 'x': {
                        if (m_pos + 2 > m_input.size()) {
                            fail("incomplete hexadecimal string escape");
                        }
                        const auto hi = hex_digit(m_input[m_pos]);
                        const auto lo = hex_digit(m_input[m_pos + 1]);
                        if (hi >= 16 || lo >= 16) {
                            fail("invalid hexadecimal string escape");
                        }
                        rv += static_cast<char>((hi << 4) | lo);
                        m_pos += 2;
                        break;
                    }
                    default:
                        fail(FMT("unsupported string escape \\" << c));
                }
            }
            fail("unterminated string literal");
        }

        bool at_end() {
            skip_ws();
            return m_pos == m_input.size();
        }

        ::std::pair<::std::string, ::std::optional<::std::string>> parse_cfg_option() {
            auto name = ident();
            ::std::optional<::std::string> value;
            if (take('=')) {
                value = string_literal();
            }
            if (!at_end()) {
                fail("expected `key` or `key=\"value\"`");
            }
            return {::std::move(name), ::std::move(value)};
        }

        CheckSpec parse_check_spec() {
            if (ident() != "cfg") {
                fail("expected `cfg(...)`");
            }
            expect('(', "`(` after `cfg`");

            CheckSpec rv;
            bool saw_values = false;
            if (take(')')) {
                if (!at_end()) {
                    fail("unexpected input after `cfg(...)`");
                }
                return rv;
            }

            for (;;) {
                auto word = ident();
                if (take('(')) {
                    if (word == "any") {
                        if (!rv.names.empty() || saw_values || rv.any_names) {
                            fail("`cfg(any())` can only be provided in isolation");
                        }
                        expect(')', "`)` after `any(`");
                        rv.any_names = true;
                    } else if (word == "values") {
                        if (rv.names.empty()) {
                            fail("`values()` cannot be specified before the names");
                        }
                        if (saw_values) {
                            fail("`values()` cannot be specified multiple times");
                        }
                        saw_values = true;
                        bool saw_any = false;
                        if (!take(')')) {
                            for (;;) {
                                skip_ws();
                                if (m_pos < m_input.size() && m_input[m_pos] == '"') {
                                    if (saw_any) {
                                        fail("`values()` cannot combine string literals with `any()`");
                                    }
                                    rv.values.values.insert(string_literal());
                                } else {
                                    auto value_kind = ident();
                                    expect('(', "`(` in `values()` special value");
                                    expect(')', "`)` in `values()` special value");
                                    if (value_kind == "none") {
                                        if (saw_any) {
                                            fail("`values()` cannot combine `none()` with `any()`");
                                        }
                                        rv.values.none = true;
                                    } else if (value_kind == "any") {
                                        if (saw_any || rv.values.none || !rv.values.values.empty()) {
                                            fail("`values()` cannot combine `any()` with other values");
                                        }
                                        saw_any = true;
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
                    if (saw_values) {
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
            if (!at_end()) {
                fail("unexpected input after `cfg(...)`");
            }
            if (rv.any_names && (!rv.names.empty() || saw_values)) {
                fail("`cfg(any())` can only be provided in isolation");
            }
            if (!saw_values && !rv.any_names) {
                rv.values.none = true;
            }
            return rv;
        }
    };

    bool is_sticky_lint_level(CfgLintLevel level) {
        return level == CfgLintLevel::ForceWarn || level == CfgLintLevel::Forbid;
    }

    void update_lint_setting(LintSetting& setting, CfgLintLevel level) {
        if (setting.is_set && is_sticky_lint_level(setting.level)) {
            return;
        }
        setting.is_set = true;
        setting.level = level;
    }

    unsigned lint_level_rank(CfgLintLevel level) {
        switch (level) {
            case CfgLintLevel::Allow: return 0;
            case CfgLintLevel::Warn:
            case CfgLintLevel::ForceWarn: return 1;
            case CfgLintLevel::Deny: return 2;
            case CfgLintLevel::Forbid: return 3;
        }
        throw ::std::logic_error("invalid lint level");
    }

    CfgLintLevel unexpected_cfg_level() {
        auto level = g_check_cfg.unexpected_cfgs.is_set
            ? g_check_cfg.unexpected_cfgs.level
            : (g_check_cfg.warnings.is_set ? g_check_cfg.warnings.level : CfgLintLevel::Warn);
        if (level != CfgLintLevel::ForceWarn && g_check_cfg.cap.is_set
            && lint_level_rank(level) > lint_level_rank(g_check_cfg.cap.level)) {
            level = g_check_cfg.cap.level;
        }
        return level;
    }

    enum class BuiltinExpectation {
        UnknownName,
        Expected,
        UnexpectedValue,
    };

    BuiltinExpectation check_builtin_cfg(const ::std::string& name, const ::std::optional<::std::string>& value) {
        static const ::std::set<::std::string> no_value_names = {
            "debug_assertions", "clippy", "doc", "doctest", "miri", "rustfmt",
            "overflow_checks", "proc_macro", "sanitizer_cfi_generalize_pointers",
            "sanitizer_cfi_normalize_integers", "target_thread_local", "ub_checks",
            "contract_checks", "unix", "windows",
        };
        static const ::std::set<::std::string> any_value_names = {
            "fmt_debug", "panic", "relocation_model", "sanitize", "target_feature",
            "target_abi", "target_arch", "target_endian", "target_env", "target_family",
            "target_os", "target_pointer_width", "target_vendor",
        };
        static const ::std::set<::std::string> atomic_names = {
            "target_has_atomic", "target_has_atomic_equal_alignment", "target_has_atomic_load_store",
        };
        if (no_value_names.count(name) != 0) {
            return value ? BuiltinExpectation::UnexpectedValue : BuiltinExpectation::Expected;
        }
        if (any_value_names.count(name) != 0) {
            return value ? BuiltinExpectation::Expected : BuiltinExpectation::UnexpectedValue;
        }
        if (atomic_names.count(name) != 0) {
            if (!value || *value == "ptr" || *value == "8" || *value == "16"
                || *value == "32" || *value == "64" || *value == "128") {
                return BuiltinExpectation::Expected;
            }
            return BuiltinExpectation::UnexpectedValue;
        }
        return BuiltinExpectation::UnknownName;
    }

    void report_unexpected_cfg(const Span& span, const ::std::string& name, const ::std::optional<::std::string>& value, bool bad_value) {
        const auto level = unexpected_cfg_level();
        if (level == CfgLintLevel::Allow) {
            return;
        }
        const auto condition = value ? FMT("`" << name << " = \"" << *value << "\"`") : FMT("`" << name << "`");
        const auto message = bad_value
            ? FMT("unexpected cfg condition value " << condition)
            : FMT("unexpected cfg condition name " << condition);
        if (level == CfgLintLevel::Warn || level == CfgLintLevel::ForceWarn) {
            WARNING(span, W0000, message);
        } else {
            ERROR(span, E0000, message);
        }
    }

    void validate_cfg_use(const Span& span, const ::std::string& name, const ::std::optional<::std::string>& value) {
        if (!g_check_cfg.active) {
            return;
        }
        const auto it = g_check_cfg.expected.find(name);
        if (it != g_check_cfg.expected.end()) {
            const auto& expected = it->second;
            const auto valid = expected.any || (value ? expected.values.count(*value) != 0 : expected.none);
            if (!valid) {
                report_unexpected_cfg(span, name, value, true);
            }
            return;
        }
        switch (check_builtin_cfg(name, value)) {
            case BuiltinExpectation::Expected:
                return;
            case BuiltinExpectation::UnexpectedValue:
                report_unexpected_cfg(span, name, value, true);
                return;
            case BuiltinExpectation::UnknownName:
                if (g_check_cfg.exhaustive_names) {
                    report_unexpected_cfg(span, name, value, false);
                }
                return;
        }
    }
}

static const RcString rcstring_cfg = RcString::new_interned("cfg");

void CfgDump(::std::ostream& os) {
    for (const auto& v : g_cfg_values) {
        os << ">" << v.first << "=" << v.second << std::endl;
    }
    for (const auto& f : g_cfg_flags) {
        os << ">" << f << std::endl;
    }
    // NOTE: `g_cfg_value_fcns` is only used for feature flags, which minicargo doesn't need
}

void CfgSetFlag(::std::string name) {
    g_cfg_flags.insert(mv$(name));
}

void CfgSetValue(::std::string name, ::std::string val) {
    g_cfg_values.insert(::std::make_pair(mv$(name), mv$(val)));
}

void CfgSetValueCb(::std::string name, ::std::function<bool(const ::std::string&)> cb) {
    g_cfg_value_fcns.insert(::std::make_pair(mv$(name), mv$(cb)));
}

bool CfgParseOption(const ::std::string& spec, ::std::string& name, bool& has_value, ::std::string& value, ::std::string& error) {
    try {
        auto parsed = CfgSpecParser(spec).parse_cfg_option();
        name = ::std::move(parsed.first);
        has_value = parsed.second.has_value();
        value = parsed.second ? ::std::move(*parsed.second) : ::std::string();
        return true;
    } catch (const ::std::exception& e) {
        error = e.what();
        return false;
    }
}

bool CfgSetCheckSpec(const ::std::string& spec, ::std::string& error) {
    try {
        auto parsed = CfgSpecParser(spec).parse_check_spec();
        g_check_cfg.active = true;
        if (parsed.any_names) {
            g_check_cfg.exhaustive_names = false;
        }
        for (auto& name : parsed.names) {
            auto& expected = g_check_cfg.expected[name];
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
        update_lint_setting(g_check_cfg.warnings, level);
    } else if (name == "unexpected_cfgs") {
        update_lint_setting(g_check_cfg.unexpected_cfgs, level);
    }
}

void CfgSetLintCap(CfgLintLevel level) {
    g_check_cfg.cap.is_set = true;
    g_check_cfg.cap.level = level;
}

namespace {
    bool check_cfg_inner1(const RcString& name, TokenStream& lex);

    bool check_cfg_inner(TokenStream& lex) {
        TRACE_FUNCTION;
        if (lex.lookahead(0) == TOK_INTERPOLATED_META) {
            auto meta = std::move(lex.getTokenCheck(TOK_INTERPOLATED_META).frag_meta());
            auto ilex = TTStream(meta.span(), ParseState(), meta.data());
            return check_cfg_inner1(meta.name().as_trivial(), ilex);
        } else if (lex.lookahead(0) == TOK_RWORD_TRUE) {
            lex.getTokenCheck(TOK_RWORD_TRUE);
            return true;
        } else if (lex.lookahead(0) == TOK_RWORD_FALSE) {
            lex.getTokenCheck(TOK_RWORD_FALSE);
            return false;
        } else {
            auto name = lex.getTokenCheck(TOK_IDENT).ident().name;
            return check_cfg_inner1(name, lex);
        }
    }

    bool check_cfg_inner1(const RcString& name, TokenStream& lex) {
        // Some compiler-generated cfg streams have no source parent.  They do
        // not need a diagnostic span unless check-cfg is actually enabled.
        const auto condition_span = g_check_cfg.active ? lex.point_span() : Span();
        Token tok;
        switch (lex.lookahead(0)) {
            case TOK_EQUAL: {
                GET_CHECK_TOK(tok, lex, TOK_EQUAL);
                std::string val;
                if (lex.lookahead(0) == TOK_INTERPOLATED_EXPR) {
                    auto n = lex.getTokenCheck(TOK_INTERPOLATED_EXPR).take_frag_node();
                    const auto* np = cast<AST::ExprNodeString>(n.get());
                    ASSERT_BUG(n->span(), np, "");
                    val = np->m_value;
                } else {
                    GET_CHECK_TOK(tok, lex, TOK_STRING);
                    val = tok.str();
                }
                validate_cfg_use(condition_span, name.c_str(), val);
                // Equality
                auto its = g_cfg_values.equal_range(name.c_str());
                for (auto it = its.first; it != its.second; ++it) {
                    DEBUG(name << ": '" << it->second << "' == '" << val << "'");
                    if (it->second == val) {
                        return true;
                    }
                }
                if (its.first != its.second) {
                    return false;
                }

                auto it2 = g_cfg_value_fcns.find(name.c_str());
                if (it2 != g_cfg_value_fcns.end()) {
                    DEBUG(name << ": ('" << val << "')?");
                    return it2->second(val);
                }

                //WARNING(lex.point_span(), W0000, "Unknown cfg() param '" << name << "'");
                return false;
            }
            case TOK_PAREN_OPEN:
                GET_TOK(tok, lex);

                static const RcString rcstring_any = RcString::new_interned("any");
                static const RcString rcstring_not = RcString::new_interned("not");
                static const RcString rcstring_all = RcString::new_interned("all");
                static const RcString rcstring_target = RcString::new_interned("target");
                if (name == rcstring_any || name == rcstring_cfg) {
                    bool rv = false;
                    while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
                        rv |= check_cfg_inner(lex);
                        if (lex.lookahead(0) != TOK_COMMA) {
                            break;
                        }
                        GET_CHECK_TOK(tok, lex, TOK_COMMA);
                    }
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    return rv;
                } else if (name == rcstring_not) {
                    bool rv = check_cfg_inner(lex);
                    // Allow a trailing comma
                    lex.getTokenIf(TOK_COMMA);
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    return !rv;
                } else if (name == rcstring_all) {
                    bool rv = true;
                    while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
                        rv &= check_cfg_inner(lex);
                        if (lex.lookahead(0) != TOK_COMMA) {
                            break;
                        }
                        GET_CHECK_TOK(tok, lex, TOK_COMMA);
                    }
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    return rv;
                } else if (name == rcstring_target) {
                    // `target(os = "linux", arch = "x86_64")` is the compact
                    // spelling of `all(target_os = "linux", target_arch =
                    // "x86_64")`.  Keep evaluation in the ordinary cfg path
                    // so check-cfg sees the canonical target_* names too.
                    bool rv = true;
                    while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
                        const auto field = lex.getTokenCheck(TOK_IDENT).ident().name;
                        const auto canonical = RcString::new_interned(FMT("target_" << field));
                        rv &= check_cfg_inner1(canonical, lex);
                        if (lex.lookahead(0) != TOK_COMMA) {
                            break;
                        }
                        GET_CHECK_TOK(tok, lex, TOK_COMMA);
                    }
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    return rv;
                } else {
                    // oops
                    ERROR(lex.point_span(), E0000, "Unknown cfg() function - " << name);
                }

                break;
            default:
                validate_cfg_use(condition_span, name.c_str(), ::std::nullopt);
                auto its = g_cfg_values.equal_range(name.c_str());
                for (auto it = its.first; it != its.second; ++it) {
                    return true;
                }
                // Flag
                auto it = g_cfg_flags.find(name.c_str());
                return (it != g_cfg_flags.end());
        }
    }
}

bool check_cfg_stream(TokenStream& lex) {
    Token tok;
    bool rv = false;
    GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
    while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
        rv |= check_cfg_inner(lex);
        if (lex.lookahead(0) != TOK_COMMA) {
            break;
        }
        GET_CHECK_TOK(tok, lex, TOK_COMMA);
    }
    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
    return rv;
}

bool check_cfg(const Span& sp, const ::AST::Attribute& mi) {
    TTStream lex(sp, ParseState(), mi.data());
    return check_cfg_stream(lex);
}

bool check_cfg_attrs(const ::AST::AttributeList& attrs) {
    for (auto& a : attrs.m_items) {
        if (a.name() == rcstring_cfg) {
            if (!check_cfg(a.span(), a)) {
                return false;
            }
        }
    }
    return true;
}

std::vector<AST::Attribute> check_cfg_attr(const ::AST::Attribute& mi) {
    TTStream lex(mi.span(), ParseState(), mi.data());

    Token tok;
    std::vector<AST::Attribute> rv;
    lex.getTokenCheck(TOK_PAREN_OPEN);
    auto cfg_res = check_cfg_inner(lex);
    while (lex.lookahead(0) == TOK_COMMA) {
        lex.getTokenCheck(TOK_COMMA);
        rv.push_back(ParseMetaItem(lex));
    }
    lex.getTokenCheck(TOK_PAREN_CLOSE);
    lex.getTokenCheck(TOK_EOF);
    if (cfg_res) {
        return rv;
    } else {
        return std::vector<AST::Attribute>();
    }
}

class CCfgExpander: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const ::AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        DEBUG("cfg!() - " << tt);
        auto lex = TTStream(sp, ParseState(), tt);
        bool rv = check_cfg_inner(lex);
        lex.getTokenCheck(TOK_EOF);

        return box$(TTStreamO(sp, ParseState(), TokenTree(AST::Edition::Rust2015, {}, rv ? TOK_RWORD_TRUE : TOK_RWORD_FALSE)));
    }
};

class CCfgSelectExpander: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const ::AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        DEBUG("cfg_select!() - " << tt);
        auto lex = TTStream(sp, ParseState(), tt);
        for (;;) {
            bool rv = lex.getTokenIf(TOK_UNDERSCORE) || check_cfg_inner(lex);
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

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate) const override {
        DEBUG("#[cfg] crate - " << mi);
        // Ignore, as #[cfg] on a crate is handled in expand/mod.cpp
        if (check_cfg(sp, mi)) {
        } else {
            // Remove all items (can't remove the module)
            crate.m_root_module.m_items.clear();
        }
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        TRACE_FUNCTION_FR("#[cfg] item - " << mi, (i.is_None() ? "Deleted" : ""));
        if (check_cfg(sp, mi)) {
            // Leave
        } else {
            i = AST::Item::make_None({});
        }
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, AST::Impl& impl, const RcString& name, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        TRACE_FUNCTION_FR("#[cfg] item - " << mi, (i.is_None() ? "Deleted" : ""));
        if (check_cfg(sp, mi)) {
            // Leave
        } else {
            i = AST::Item::make_None({});
        }
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, const AST::AbsolutePath& path, AST::Trait& trait, slice<const AST::Attribute> attrs, AST::Item& i) const override {
        TRACE_FUNCTION_FR("#[cfg] item - " << mi, (i.is_None() ? "Deleted" : ""));
        if (check_cfg(sp, mi)) {
            // Leave
        } else {
            i = AST::Item::make_None({});
        }
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, ::AST::ExprNodeP& expr) const override {
        DEBUG("#[cfg] expr - " << mi);
        if (check_cfg(sp, mi)) {
            // Leave
        } else {
            expr.reset();
        }
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::StructItem& si) const override {
        DEBUG("#[cfg] struct item - " << mi);
        if (!check_cfg(sp, mi)) {
            si.m_name = RcString();
        }
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::TupleItem& i) const override {
        DEBUG("#[cfg] tuple item - " << mi);
        if (!check_cfg(sp, mi)) {
            i.m_type = ::TypeRef(sp);
        }
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::EnumVariant& i) const override {
        DEBUG("#[cfg] enum variant - " << mi);
        if (!check_cfg(sp, mi)) {
            i.m_name = RcString();
        }
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::ExprNodeMatchArm& i) const override {
        DEBUG("#[cfg] match arm - " << mi);
        if (!check_cfg(sp, mi)) {
            i.m_patterns.clear();
        }
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::ExprNodeStructLiteral::Ent& i) const override {
        DEBUG("#[cfg] struct lit - " << mi);
        if (!check_cfg(sp, mi)) {
            i.value.reset();
        }
    }
};

STATIC_MACRO("cfg", CCfgExpander);
STATIC_MACRO("cfg_select", CCfgSelectExpander);
STATIC_DECORATOR("cfg", CCfgHandler);
