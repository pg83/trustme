#include "lint_level.h"

CfgLintLevel LintLevelForItem(
    const Settings& settings,
    const ::std::map<RcString, CfgLintLevel>& byName,
    const ::std::map<RcString, CfgLintLevel>& byGroup,
    const char* name,
    CfgLintLevel builtin
) {
    auto level = settings.lintLevel(name, builtin);

    // A group the lint belongs to, but not `warnings` -- that one is applied
    // last, so that it can lift whatever is still at warn.
    for (const auto& group : byGroup) {
        if (group.first == "warnings") {
            continue;
        }
        if (Settings::lintGroupContains(group.first.c_str(), name)) {
            level = group.second;
        }
    }

    const auto exact = byName.find(RcString::newInterned(name));
    if (exact != byName.end()) {
        level = exact->second;
    }

    const auto warnings = byGroup.find(RcString::newInterned("warnings"));
    if (warnings != byGroup.end() && (level == CfgLintLevel::Warn || level == CfgLintLevel::ForceWarn)) {
        level = warnings->second;
    }

    if (settings.lintCap && level > *settings.lintCap && level != CfgLintLevel::ForceWarn) {
        level = *settings.lintCap;
    }
    return level;
}
