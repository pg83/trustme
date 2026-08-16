#pragma once

/*
 * The `unused_must_use` lint
 */

#include "settings.h"

struct WireBoard;
class HIRCrate;

/// The level `unused_must_use` reports at: `warn` unless the crate or the
/// command line says otherwise.
extern CfgLintLevel LintUnusedMustUseLevel(const Settings& settings);

/// Report every discarded value whose producer is marked `#[must_use]`.
extern void LintUnusedMustUse(const WireBoard& wb, HIRCrate& crate);
