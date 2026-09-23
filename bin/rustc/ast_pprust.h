#pragma once

namespace stl {
    class ZeroCopyOutput;
}

class TokenTree;
class ASTExprNode;

void pprustTtsToString(stl::ZeroCopyOutput& out, const TokenTree& tts);
void pprustExprToString(stl::ZeroCopyOutput& out, const ASTExprNode& expr);
