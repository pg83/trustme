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

void ExpandTestHarness(ASTCrate& crate);
