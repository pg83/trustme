#pragma once

class ASTCrate;
struct WireBoard;

void ResolveUse(const WireBoard& wb, ASTCrate& crate);
void ResolveIndex(ASTCrate& crate);
void ResolveAbsolutise(const WireBoard& wb, ASTCrate& crate);
