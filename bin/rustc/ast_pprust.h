#pragma once

namespace stl {
    class ZeroCopyOutput;
}

class TokenTree;
class ASTExprNode;
class ASTPattern;
struct ASTType;

void pprustTtsToString(stl::ZeroCopyOutput& out, const TokenTree& tts);
void pprustExprToString(stl::ZeroCopyOutput& out, const ASTExprNode& expr);
void pprustPatToString(stl::ZeroCopyOutput& out, const ASTPattern& pat);
void pprustTypeToString(stl::ZeroCopyOutput& out, const ASTType& type);
bool pprustExprIsAtom(const ASTExprNode& expr);
