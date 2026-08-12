#include "ast_expr_ptr.h"

namespace AST {

ExprNodeP::ExprNodeP()
    : m_ptr(nullptr) {
}
ExprNodeP::ExprNodeP(ExprNode* node)
    : m_ptr(node) {
}
ExprNodeP& ExprNodeP::operator=(ExprNodeP&& x) {
    this->~ExprNodeP();
    this->m_ptr = x.m_ptr;
    x.m_ptr = nullptr;
    return *this;
}
ExprNode* ExprNodeP::release() {
    auto rv = m_ptr;
    m_ptr = nullptr;
    return rv;
}
void ExprNodeP::reset(ExprNode* n) {
    this->~ExprNodeP();
    m_ptr = n;
}
const ExprNode& Expr::node() const {
    assert(m_node.get());
    return *m_node;
}
ExprNode& Expr::node() {
    assert(m_node.get());
    return *m_node;
}
::std::shared_ptr<ExprNode> Expr::take_node() {
    assert(m_node.get());
    return ::std::move(m_node);
}
}
