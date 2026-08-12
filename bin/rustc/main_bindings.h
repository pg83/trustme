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
extern AST::Crate* Parse_Crate(stl::ObjPool* pool, HIR::TypeInterner& types, ::std::string mainfile, AST::Edition edition);

extern void Expand_Init();
extern void Expand(::AST::Crate& crate);
extern void Expand_TestHarness(::AST::Crate& crate);
extern void Expand_ProcMacro(::AST::Crate& crate);
