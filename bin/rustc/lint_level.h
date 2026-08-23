#pragma once

/*
 * Resolving the level a lint reports at for one item
 */

#include "rc_string.h"
#include "settings.h"

class HIRCrate;
class HIRSimplePath;

/// Apply the levels written on one lexical scope to an inherited level.
extern CfgLintLevel ApplyLintLevelOverrides(
    const Settings& settings,
    const LintLevelOverrides& overrides,
    const char* name,
    CfgLintLevel inherited
);

/// Resolve a lint at a module path, applying every enclosing module in lexical
/// order. Impl blocks use this because HIR stores them outside the module tree.
extern CfgLintLevel LintLevelForModulePath(
    const Settings& settings,
    const HIRCrate& crate,
    const HIRSimplePath& path,
    const char* name,
    CfgLintLevel builtin
);
