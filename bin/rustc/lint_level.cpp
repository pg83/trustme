#include "lint_level.h"

#include "span.h"
#include "hir_hir.h"
#include "ast_attrs.h"
#include "parse_ttstream.h"

using namespace stl;

ThinVector<RcString> LintNamesOf(const ASTAttribute& attribute) {
    ThinVector<RcString> names;
    TTStream lex(attribute.span(), ParseState(), attribute.data());
    if (!lex.getTokenIf(TOK_PAREN_OPEN)) {
        return names;
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
                names.push_back(tok.ident().name);
            }
        }
        atName = false;
    }
    return names;
}

bool CollectLintLevelAttributes(const ASTAttributeList& attrs, LintLevelOverrides& overrides) {
    bool any = false;
    for (const auto& attribute : attrs.items) {
        CfgLintLevel level;
        if (attribute.name() == "allow" || attribute.name() == "expect") {
            level = CfgLintLevel::Allow;
        } else if (attribute.name() == "warn") {
            level = CfgLintLevel::Warn;
        } else if (attribute.name() == "deny") {
            level = CfgLintLevel::Deny;
        } else if (attribute.name() == "forbid") {
            level = CfgLintLevel::Forbid;
        } else {
            continue;
        }
        for (const auto& name : LintNamesOf(attribute)) {
            const bool isGroup = name == "warnings" || name == "unused";
            overrides.set(name, isGroup, level);
            any = true;
        }
    }
    return any;
}

CfgLintLevel ApplyLintLevelOverrides(const Settings& settings, const LintLevelOverrides& overrides, const char* name, CfgLintLevel inherited) {
    auto level = inherited;

    for (const auto& entry : overrides.entries) {
        if (!entry.isGroup || entry.name == "warnings") {
            continue;
        }
        if (Settings::lintGroupContains(entry.name.c_str(), name)) {
            level = entry.level;
        }
    }

    const auto lintName = RcString::newInterned(name);
    if (const auto* exact = overrides.find(lintName, false)) {
        level = exact->level;
    }

    if (const auto* warnings = overrides.find(RcString::newInterned("warnings"), true); warnings && (level == CfgLintLevel::Warn || level == CfgLintLevel::ForceWarn)) {
        level = warnings->level;
    }

    if (settings.lintCap && level > *settings.lintCap && level != CfgLintLevel::ForceWarn) {
        level = *settings.lintCap;
    }
    return level;
}

CfgLintLevel LintLevelForModulePath(const Settings& settings, const HIRCrate& crate, const HIRSimplePath& path, const char* name, CfgLintLevel builtin) {
    auto level = settings.lintLevel(name, builtin);
    const HIRModule* module = &crate.rootModule;
    level = ApplyLintLevelOverrides(settings, module->lintLevels, name, level);
    for (const auto& component : path.components()) {
        const auto item = module->modItems.find(component);
        ASSERT_BUG(Span(), item != module->modItems.end(), StringView("missing source module ") << path);
        module = item->second->ent.opt_Module();
        ASSERT_BUG(Span(), module, StringView("source module path names a non-module: ") << path);
        level = ApplyLintLevelOverrides(settings, module->lintLevels, name, level);
    }
    return level;
}
