#include "lint_level.h"

#include "hir_hir.h"
#include "span.h"

CfgLintLevel ApplyLintLevelOverrides(
    const Settings& settings,
    const LintLevelOverrides& overrides,
    const char* name,
    CfgLintLevel inherited
) {
    auto level = inherited;

    // A group the lint belongs to, but not `warnings` -- that one is applied
    // last, so that it can lift whatever is still at warn.
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

    if (const auto* warnings = overrides.find(RcString::newInterned("warnings"), true);
        warnings && (level == CfgLintLevel::Warn || level == CfgLintLevel::ForceWarn)) {
        level = warnings->level;
    }

    if (settings.lintCap && level > *settings.lintCap && level != CfgLintLevel::ForceWarn) {
        level = *settings.lintCap;
    }
    return level;
}

CfgLintLevel LintLevelForModulePath(
    const Settings& settings,
    const HIRCrate& crate,
    const HIRSimplePath& path,
    const char* name,
    CfgLintLevel builtin
) {
    auto level = settings.lintLevel(name, builtin);
    const HIRModule* module = &crate.rootModule;
    level = ApplyLintLevelOverrides(settings, module->lintLevels, name, level);
    for (const auto& component : path.components()) {
        const auto item = module->modItems.find(component);
        ASSERT_BUG(Span(), item != module->modItems.end(), "missing source module " << path);
        module = item->second->ent.opt_Module();
        ASSERT_BUG(Span(), module, "source module path names a non-module: " << path);
        level = ApplyLintLevelOverrides(settings, module->lintLevels, name, level);
    }
    return level;
}
