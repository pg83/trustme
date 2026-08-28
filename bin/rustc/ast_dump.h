#pragma once

#include <iosfwd>

class ASTCrate;
class ASTExprNode;

void DumpRust(const char* filename, const ASTCrate& crate);
void DumpASTNode(::std::ostream& os, const ASTExprNode& node);
