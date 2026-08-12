#pragma once

class ASTCrate;
struct WireBoard;

extern void ResolveUse(const WireBoard& wb, ASTCrate& crate);
extern void ResolveIndex(ASTCrate& crate);
extern void ResolveAbsolutise(const WireBoard& wb, ASTCrate& crate);
