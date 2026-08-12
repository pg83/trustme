#pragma once

#include <string>
#include <memory>
#include "ast_edition.h"

namespace AST {
    class Crate;
    class ExprNode;
}

namespace stl {
    class ObjPool;
}

namespace HIR {
    class TypeInterner;
}

/// Parse a crate from the given file
extern AST::Crate* ParseCrate(stl::ObjPool* pool, HIR::TypeInterner& types, ::std::string mainfile, AST::Edition edition);

extern void ExpandInit();
extern void Expand(::AST::Crate& crate);
extern void ExpandTestHarness(::AST::Crate& crate);
extern void ExpandProcMacroHarness(::AST::Crate& crate);
