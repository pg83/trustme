#pragma once

#include "rc_string.h"

#include <stddef.h>

namespace stl {
    class ZeroCopyOutput;
}

class TokenTree;
class ASTExprNode;
class ASTPattern;
class ASTModule;
struct ASTType;

struct PprustMarkers {
    virtual RcString blockItem(const ASTModule& module, size_t index) = 0;
    virtual RcString dollarCrate(const RcString& crateName) = 0;
};

void pprustTtsToString(stl::ZeroCopyOutput& out, const TokenTree& tts);
void pprustExprToString(stl::ZeroCopyOutput& out, const ASTExprNode& expr, PprustMarkers* markers = nullptr);
void pprustPatToString(stl::ZeroCopyOutput& out, const ASTPattern& pat);
void pprustTypeToString(stl::ZeroCopyOutput& out, const ASTType& type);
bool pprustExprIsAtom(const ASTExprNode& expr);
