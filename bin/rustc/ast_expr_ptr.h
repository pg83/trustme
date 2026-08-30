#pragma once

class ASTExprNode;
class ASTNodeVisitor;

class ASTExpr {
    ASTExprNode* node_ = nullptr;

public:
    ASTExpr(ASTExprNode* node);
    ASTExpr();

    operator bool() const {
        return isValid();
    }

    bool isValid() const {
        return node_ != nullptr;
    }

    const ASTExprNode& node() const;

    ASTExprNode& node();

    ASTExprNode* takeNode();

    void visitNodes(ASTNodeVisitor& v);
    void visitNodes(ASTNodeVisitor& v) const;

    ASTExpr clone() const;

};
