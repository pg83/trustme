#pragma once

/*
 * The check that a `forbid` is not lifted
 */

struct WireBoard;
class ASTCrate;

void LintCheckForbid(const WireBoard& wb, ASTCrate& crate);
