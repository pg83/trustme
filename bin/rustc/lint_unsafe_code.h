#pragma once

/*
 * The `unsafe_code` lint
 */

#include "settings.h"

struct WireBoard;
class HIRCrate;

extern void LintUnsafeCode(const WireBoard& wb, HIRCrate& crate);
