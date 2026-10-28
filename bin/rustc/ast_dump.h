#pragma once

#include "output.h"

#include <iosfwd>

class ASTCrate;
class ASTExprNode;

void DumpRust(const char* filename, const ASTCrate& crate);
