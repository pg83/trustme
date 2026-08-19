#pragma once

#include "parse_token.h"

extern bool isTokenPath(eTokenType tt);
extern bool isTokenPat(eTokenType tt);
extern bool isTokenType(eTokenType tt);
extern bool isTokenExpr(eTokenType tt);
extern bool isTokenStmt(eTokenType tt);
extern bool isTokenItem(eTokenType tt);
extern bool isTokenVis(eTokenType tt);

struct MacroPatEnt;

/// Rust's follow-set restriction: a fragment is matched by a parser that stops
/// where it stops, so only a token that could never continue it may stand
/// next. Raises an error on the first entry in the arm that breaks the rule.
extern void MacroRulesCheckFollowSets(const MacroPatEnt* ents, size_t count);
