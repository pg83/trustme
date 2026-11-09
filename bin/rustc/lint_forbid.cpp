#include "lint_forbid.h"

#include "span.h"
#include "ast_ast.h"
#include "ast_crate.h"
#include "wire_board.h"
#include "lint_level.h"

#include <set>

using namespace stl;

namespace {
    template <typename F>
    void readLintAttrs(const ASTAttributeList& attrs, std::set<RcString>& forbidden, const F& lowered) {
        for (const auto& a : attrs.items) {
            const auto& name = a.name();
            if (name == "forbid") {
                for (const auto& lint : LintNamesOf(a)) {
                    forbidden.insert(lint);
                }
            } else if (name == "allow" || name == "warn" || name == "deny" || name == "expect") {
                for (const auto& lint : LintNamesOf(a)) {
                    lowered(a.span(), lint);
                }
            }
        }
    }

    void checkModule(ASTModule& mod, std::set<RcString> forbidden);

    void checkNamedItem(ASTNamed<ASTItem>& item, const std::set<RcString>& outerForbidden) {
        auto forbidden = outerForbidden;
        readLintAttrs(item.attrs, forbidden, [&](const Span& sp, const RcString& lint) {
            if (outerForbidden.count(lint)) {
                ERROR(sp, E0000, StringView("lint `") << lint << StringView("` is forbidden by an enclosing item and cannot be lowered here"));
            }
        });
        if (auto* sub = item.data.opt_Module()) {
            checkModule(*sub, forbidden);
        }
    }

    void checkModule(ASTModule& mod, std::set<RcString> forbidden) {
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
    std::set<RcString> forbidden;
    readLintAttrs(crate.attrs, forbidden, [](const Span&, const RcString&) {});
    checkModule(crate.rootModule(), forbidden);
}
