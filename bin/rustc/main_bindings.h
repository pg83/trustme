#pragma once

#include "ast_edition.h"

#include <memory>
#include <string>

class ASTCrate;
class ASTExprNode;

namespace stl {
    class ObjPool;
}

class HIRTypeInterner;

/// Parse a crate from the given file
extern ASTCrate* ParseCrate(stl::ObjPool* pool, HIRTypeInterner& types, ::std::string mainfile, ASTEdition edition);

extern void ExpandInit();
extern void Expand(ASTCrate& crate);
extern void ExpandTestHarness(ASTCrate& crate);
extern void ExpandProcMacroHarness(ASTCrate& crate);
