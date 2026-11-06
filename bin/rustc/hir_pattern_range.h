#pragma once

#include "hir_type_ref.h"

struct HIRTypePattern;

void checkPatternExclusiveEnds(const Span& sp, const HIRType* type, const HIRTypePattern& pattern);
