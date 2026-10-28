#pragma once

#include "parse_token.h"

bool isTokenPath(eTokenType tt);
bool isTokenType(eTokenType tt);

struct MacroPatEnt;

void MacroRulesCheckFollowSets(const MacroPatEnt* ents, size_t count);
