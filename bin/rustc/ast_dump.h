#pragma once

#include <iosfwd>

class ASTCrate;
class ASTExprNode;

// Dump the crate AST (or one expression) as annotated Rust
void DumpRust(const char* filename, const ASTCrate& crate);
void DumpASTNode(::std::ostream& os, const ASTExprNode& node);
