#pragma once

    class ASTCrate;

extern void ResolveUse(ASTCrate& crate);
extern void ResolveIndex(ASTCrate& crate);
extern void ResolveAbsolutise(ASTCrate& crate);
