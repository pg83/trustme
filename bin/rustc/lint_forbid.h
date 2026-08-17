#pragma once

/*
 * The check that a `forbid` is not lifted
 */

struct WireBoard;
class ASTCrate;

/// Report an `allow`, `warn` or `deny` that tries to lower a lint an enclosing
/// item put at `forbid`.
extern void LintCheckForbid(const WireBoard& wb, ASTCrate& crate);
