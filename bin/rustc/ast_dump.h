#pragma once

#include <iosfwd>

namespace AST {

    class Crate;
    class ExprNode;

}

// Dump the crate AST (or one expression) as annotated Rust
void Dump_Rust(const char* filename, const AST::Crate& crate);
void DumpAST_Node(::std::ostream& os, const AST::ExprNode& node);
