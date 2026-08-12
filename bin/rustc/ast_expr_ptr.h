#pragma once

#include <cassert>
#include <memory>

namespace AST {

    class ExprNode;
    class NodeVisitor;

    extern ::std::ostream& operator<<(::std::ostream& os, const ExprNode& node);

    class ExprNodeP {
        ExprNode* ptr;

    public:
        ~ExprNodeP();

        ExprNodeP();

        ExprNodeP(ExprNode* node);

        ExprNodeP(std::unique_ptr<ExprNode> node); //: m_ptr(node.release()) {}

        ExprNodeP(ExprNodeP&& x)
            : ptr(x.ptr)
        {
            x.ptr = nullptr;
        }

        ExprNodeP(const ExprNodeP& x) = delete;

        ExprNodeP& operator=(ExprNodeP&& x);

        ExprNodeP& operator=(const ExprNodeP& x) = delete;

        operator bool() const {
            return isValid();
        }

        bool isValid() const {
            return ptr != nullptr;
        }

        ExprNode& operator*() {
            return *ptr;
        }

        const ExprNode& operator*() const {
            return *ptr;
        }

        ExprNode* operator->() {
            return ptr;
        }

        const ExprNode* operator->() const {
            return ptr;
        }

        ExprNode* get() {
            return ptr;
        }

        const ExprNode* get() const {
            return ptr;
        }

        ExprNode* release();

        void reset(ExprNode* n = nullptr);

        const char* typeName() const;
    };

    class Expr {
        ::std::shared_ptr<ExprNode> mNode;

    public:
        Expr(ExprNodeP node);
        Expr(ExprNode* node);
        Expr();

        operator bool() const {
            return isValid();
        }

        bool isValid() const {
            return mNode.get() != nullptr;
        }

        const ExprNode& node() const;

        ExprNode& node();

        ::std::shared_ptr<ExprNode> takeNode();

        void visitNodes(NodeVisitor& v);
        void visitNodes(NodeVisitor& v) const;

        Expr clone() const;

        friend ::std::ostream& operator<<(::std::ostream& os, const Expr& pat);
    };

}
