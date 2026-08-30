#include "ast_expr_ptr.h"

#include "output.h"
#include "ast_expr.h"
#include "compile_error.h"

using namespace stl;

ASTExpr::ASTExpr(ASTExprNode* node)
    : node_(node)
{
}

ASTExpr::ASTExpr()
    : node_(nullptr)
{
}

void ASTExpr::visitNodes(ASTNodeVisitor& visitor) {
    node_ = visitor.visit(node_);
}

void ASTExpr::visitNodes(ASTNodeVisitor& visitor) const {
    if (node_) {
        BUG_ASSERT(visitor.isConst());
        node_->visit(visitor);
    }
}

ASTExpr ASTExpr::clone() const {
    return node_ ? ASTExpr(node_->clone()) : ASTExpr();
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

template <>
void stl::output<ZeroCopyOutput, ASTExpr>(ZeroCopyOutput& out, ASTExpr value) {
    if (value) {
        out << value.node();
    } else {
        out << StringView("/* null */");
    }
}
