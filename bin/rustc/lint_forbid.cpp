#include "lint_forbid.h"

#include "span.h"
#include "ast_ast.h"
#include "ast_crate.h"
#include "wire_board.h"
#include "parse_ttstream.h"

#include <set>

namespace {
    /// The plain lint names listed in `(...)`. The list also carries entries
    /// this compiler has no lint for -- tool lints (`clippy::foo`) and keyed
    /// entries (`reason = "..."`) -- so the scan skips whatever it cannot read.
    template <typename F>
    void collectLintNames(const ASTAttribute& mi, const F& cb) {
        TTStream lex(mi.span(), ParseState(), mi.data());
        if (!lex.getTokenIf(TOK_PAREN_OPEN)) {
            return;
        }
        unsigned depth = 1;
        bool atName = true;
        while (depth > 0) {
            auto tok = lex.getToken();
            if (tok == TOK_EOF) {
                break;
            }
            if (tok == TOK_PAREN_OPEN) {
                depth += 1;
                atName = false;
                continue;
            }
            if (tok == TOK_PAREN_CLOSE) {
                depth -= 1;
                atName = true;
                continue;
            }
            if (tok == TOK_COMMA) {
                atName = (depth == 1);
                continue;
            }
            if (depth == 1 && atName && tok == TOK_IDENT) {
                const auto next = lex.lookahead(0);
                if (next == TOK_COMMA || next == TOK_PAREN_CLOSE) {
                    cb(tok.ident().name);
                }
            }
            atName = false;
        }
    }

    template <typename F>
    void readLintAttrs(const ASTAttributeList& attrs, ::std::set<RcString>& forbidden, const F& lowered) {
        for (const auto& a : attrs.items) {
            const auto& name = a.name();
            if (name == "forbid") {
                collectLintNames(a, [&](const RcString& lint) {
                    forbidden.insert(lint);
                });
            } else if (name == "allow" || name == "warn" || name == "deny" || name == "expect") {
                collectLintNames(a, [&](const RcString& lint) {
                    lowered(a.span(), lint);
                });
            }
        }
    }

    void checkModule(ASTModule& mod, ::std::set<RcString> forbidden);

    void checkNamedItem(ASTNamed<ASTItem>& item, const ::std::set<RcString>& outerForbidden) {
        auto forbidden = outerForbidden;
        readLintAttrs(item.attrs, forbidden, [&](const Span& sp, const RcString& lint) {
            if (outerForbidden.count(lint)) {
                ERROR(sp, E0000, "lint `" << lint << "` is forbidden by an enclosing item and cannot be lowered here");
            }
        });
        if (auto* sub = item.data.opt_Module()) {
            checkModule(*sub, forbidden);
        }
    }

    void checkModule(ASTModule& mod, ::std::set<RcString> forbidden) {
        for (auto& item : mod.items) {
            if (item) {
                checkNamedItem(*item, forbidden);
            }
        }
        for (auto& anon : mod.anonMods()) {
            if (anon) {
                checkModule(*anon, forbidden);
            }
        }
    }
}

void LintCheckForbid(const WireBoard& wb, ASTCrate& crate) {
    ::std::set<RcString> forbidden;
    readLintAttrs(crate.attrs, forbidden, [](const Span&, const RcString&) {});
    checkModule(crate.rootModule(), forbidden);
}
