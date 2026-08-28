#pragma once

#include "parse_token.h"

bool isTokenPath(eTokenType tt);
bool isTokenPat(eTokenType tt);
bool isTokenType(eTokenType tt);
bool isTokenExpr(eTokenType tt);
bool isTokenStmt(eTokenType tt);
bool isTokenItem(eTokenType tt);
bool isTokenVis(eTokenType tt);

struct MacroPatEnt;

void MacroRulesCheckFollowSets(const MacroPatEnt* ents, size_t count);
