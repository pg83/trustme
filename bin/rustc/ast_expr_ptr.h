#pragma once

#include <memory>

class ASTExprNode;
class ASTNodeVisitor;

std::ostream& operator<<(std::ostream& os, const ASTExprNode& node);

class ASTExprNodeP {
    ASTExprNode* ptr;

public:
    ~ASTExprNodeP();

    ASTExprNodeP();

    ASTExprNodeP(ASTExprNode* node);

    ASTExprNodeP(std::unique_ptr<ASTExprNode> node);

    ASTExprNodeP(ASTExprNodeP&& x)
        : ptr(x.ptr)
    {
        x.ptr = nullptr;
    }

    ASTExprNodeP(const ASTExprNodeP& x) = delete;

    ASTExprNodeP& operator=(ASTExprNodeP&& x);

    ASTExprNodeP& operator=(const ASTExprNodeP& x) = delete;

    operator bool() const {
        return isValid();
    }

    bool isValid() const {
        return ptr != nullptr;
    }

    ASTExprNode& operator*() {
        return *ptr;
    }

    const ASTExprNode& operator*() const {
        return *ptr;
    }

    ASTExprNode* operator->() {
        return ptr;
    }

    const ASTExprNode* operator->() const {
        return ptr;
    }

    ASTExprNode* get() {
        return ptr;
    }

    const ASTExprNode* get() const {
        return ptr;
    }

    ASTExprNode* release();

    void reset(ASTExprNode* n = nullptr);

    const char* typeName() const;
};

class ASTExpr {
    std::shared_ptr<ASTExprNode> node_;

public:
    ASTExpr(ASTExprNodeP node);
    ASTExpr(ASTExprNode* node);
    ASTExpr();

    operator bool() const {
        return isValid();
    }

    bool isValid() const {
        return node_.get() != nullptr;
    }

    const ASTExprNode& node() const;

    ASTExprNode& node();

    std::shared_ptr<ASTExprNode> takeNode();

    void visitNodes(ASTNodeVisitor& v);
    void visitNodes(ASTNodeVisitor& v) const;

    ASTExpr clone() const;

    friend std::ostream& operator<<(std::ostream& os, const ASTExpr& pat);
};
