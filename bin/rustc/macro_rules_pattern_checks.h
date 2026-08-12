#pragma once

#include "parse_token.h"

extern bool isTokenPath(eTokenType tt);
extern bool isTokenPat(eTokenType tt);
extern bool isTokenType(eTokenType tt);
extern bool isTokenExpr(eTokenType tt);
extern bool isTokenStmt(eTokenType tt);
extern bool isTokenItem(eTokenType tt);
extern bool isTokenVis(eTokenType tt);
