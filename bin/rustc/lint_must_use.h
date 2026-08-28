#pragma once

/*
 * The `unused_must_use` lint
 */

#include "settings.h"

struct WireBoard;
class HIRCrate;

extern CfgLintLevel LintUnusedMustUseLevel(const Settings& settings);

extern void LintUnusedMustUse(const WireBoard& wb, HIRCrate& crate);
