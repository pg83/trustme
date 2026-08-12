#pragma once

#include <string>
#include <memory>
#include "ast_edition.h"

    class ASTCrate;
    class ASTExprNode;

namespace stl {
    class ObjPool;
}

namespace HIR {
    class TypeInterner;
}

/// Parse a crate from the given file
extern ASTCrate* ParseCrate(stl::ObjPool* pool, HIR::TypeInterner& types, ::std::string mainfile, ASTEdition edition);

extern void ExpandInit();
extern void Expand(ASTCrate& crate);
extern void ExpandTestHarness(ASTCrate& crate);
extern void ExpandProcMacroHarness(ASTCrate& crate);
