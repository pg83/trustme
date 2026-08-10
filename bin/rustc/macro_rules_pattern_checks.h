#pragma once

#include "parse_token.h"

extern bool is_token_path(eTokenType tt);
extern bool is_token_pat(eTokenType tt);
extern bool is_token_type(eTokenType tt);
extern bool is_token_expr(eTokenType tt);
extern bool is_token_stmt(eTokenType tt);
extern bool is_token_item(eTokenType tt);
extern bool is_token_vis(eTokenType tt);
