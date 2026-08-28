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
    this->~ASTExprNodeP();
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
    this->~ASTExprNodeP();
    ptr = n;
}

const ASTExprNode& ASTExpr::node() const {
    BUG_ASSERT(node_.get());
    return *node_;
}

ASTExprNode& ASTExpr::node() {
    BUG_ASSERT(node_.get());
    return *node_;
}

std::shared_ptr<ASTExprNode> ASTExpr::takeNode() {
    BUG_ASSERT(node_.get());
    return std::move(node_);
}
