#include "ast_expr_ptr.h"

#include "compile_error.h"

ASTExprNodeP::ASTExprNodeP()
    : ptr(nullptr)
{
}

ASTExprNodeP::ASTExprNodeP(ASTExprNode* node)
    : ptr(node)
{
}

ASTExprNodeP& ASTExprNodeP::operator=(ASTExprNodeP&& x) {
    this->ptr = x.ptr;
    x.ptr = nullptr;
    return *this;
}

ASTExprNode* ASTExprNodeP::release() {
    auto rv = ptr;
    ptr = nullptr;
    return rv;
}

void ASTExprNodeP::reset(ASTExprNode* n) {
    ptr = n;
}

const ASTExprNode& ASTExpr::node() const {
    BUG_ASSERT(node_);
    return *node_;
}

ASTExprNode& ASTExpr::node() {
    BUG_ASSERT(node_);
    return *node_;
}

ASTExprNode* ASTExpr::takeNode() {
    BUG_ASSERT(node_);
    auto* node = node_;
    node_ = nullptr;
    return node;
}
