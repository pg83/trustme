#pragma once

#include <iosfwd>

namespace AST {

    class Crate;
    class ExprNode;

}

// Dump the crate AST (or one expression) as annotated Rust
void DumpRust(const char* filename, const AST::Crate& crate);
void DumpASTNode(::std::ostream& os, const AST::ExprNode& node);
