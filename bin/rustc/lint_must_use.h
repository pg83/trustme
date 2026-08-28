#pragma once

/*
 * The `unused_must_use` lint
 */

#include "settings.h"

struct WireBoard;
class HIRCrate;

CfgLintLevel LintUnusedMustUseLevel(const Settings& settings);

void LintUnusedMustUse(const WireBoard& wb, HIRCrate& crate);
