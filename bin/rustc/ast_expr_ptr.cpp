#include "ast_expr_ptr.h"


ASTExprNodeP::ASTExprNodeP()
    : ptr(nullptr) {
}
ASTExprNodeP::ASTExprNodeP(ASTExprNode* node)
    : ptr(node) {
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
    assert(mNode.get());
    return *mNode;
}
ASTExprNode& ASTExpr::node() {
    assert(mNode.get());
    return *mNode;
}
::std::shared_ptr<ASTExprNode> ASTExpr::takeNode() {
    assert(mNode.get());
    return ::std::move(mNode);
}
