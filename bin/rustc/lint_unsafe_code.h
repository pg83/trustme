#pragma once

/*
 * The `unsafe_code` lint
 */

#include "settings.h"

struct WireBoard;
class HIRCrate;

/// Report every use of `unsafe` written in this crate.
///
/// The lint is `allow` by default, so this only does anything where the crate
/// or an item asks for it.
extern void LintUnsafeCode(const WireBoard& wb, HIRCrate& crate);
