#include "ast_expr_ptr.h"

namespace AST {

ExprNodeP::ExprNodeP()
    : ptr(nullptr) {
}
ExprNodeP::ExprNodeP(ExprNode* node)
    : ptr(node) {
}
ExprNodeP& ExprNodeP::operator=(ExprNodeP&& x) {
    this->~ExprNodeP();
    this->ptr = x.ptr;
    x.ptr = nullptr;
    return *this;
}
ExprNode* ExprNodeP::release() {
    auto rv = ptr;
    ptr = nullptr;
    return rv;
}
void ExprNodeP::reset(ExprNode* n) {
    this->~ExprNodeP();
    ptr = n;
}
const ExprNode& Expr::node() const {
    assert(mNode.get());
    return *mNode;
}
ExprNode& Expr::node() {
    assert(mNode.get());
    return *mNode;
}
::std::shared_ptr<ExprNode> Expr::takeNode() {
    assert(mNode.get());
    return ::std::move(mNode);
}
}
