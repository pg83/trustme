#pragma once

#include "settings.h"

struct WireBoard;
class HIRCrate;

void LintDeadCode(const WireBoard& wb, HIRCrate& crate);
