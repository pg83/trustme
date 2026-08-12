#pragma once

namespace AST {
    class Crate;
}

extern void ResolveUse(::AST::Crate& crate);
extern void ResolveIndex(::AST::Crate& crate);
extern void ResolveAbsolutise(::AST::Crate& crate);
