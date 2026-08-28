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

ASTCrate* ParseCrate(const WireBoard& wb, stl::ObjPool* pool, std::string mainfile, ASTEdition edition);

void ExpandInit(ExpandRegistry& registry);
void Expand(const WireBoard& wb, ASTCrate& crate);
void ExpandTestHarness(ASTCrate& crate);
void ExpandProcMacroHarness(const WireBoard& wb, ASTCrate& crate);
