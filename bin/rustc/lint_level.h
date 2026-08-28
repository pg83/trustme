#pragma once

/*
 * Resolving the level a lint reports at for one item
 */

#include "rc_string.h"
#include "settings.h"

class HIRCrate;
class HIRSimplePath;

extern CfgLintLevel ApplyLintLevelOverrides(const Settings& settings, const LintLevelOverrides& overrides, const char* name, CfgLintLevel inherited);

extern CfgLintLevel LintLevelForModulePath(const Settings& settings, const HIRCrate& crate, const HIRSimplePath& path, const char* name, CfgLintLevel builtin);
