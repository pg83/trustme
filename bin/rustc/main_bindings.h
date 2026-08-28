#pragma once

#include "ast_edition.h"

#include <memory>
#include <string>

class ASTCrate;
struct WireBoard;
class ASTExprNode;

namespace stl {
    class ObjPool;
}

class HIRTypeInterner;
class ExpandRegistry;

extern ASTCrate* ParseCrate(const WireBoard& wb, stl::ObjPool* pool, std::string mainfile, ASTEdition edition);

extern void ExpandInit(ExpandRegistry& registry);
extern void Expand(const WireBoard& wb, ASTCrate& crate);
extern void ExpandTestHarness(ASTCrate& crate);
extern void ExpandProcMacroHarness(const WireBoard& wb, ASTCrate& crate);
