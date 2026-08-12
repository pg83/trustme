#pragma once

#include <cassert>
#include <memory>

namespace AST {

    class ExprNode;
    class NodeVisitor;

    extern ::std::ostream& operator<<(::std::ostream& os, const ExprNode& node);

    class ExprNodeP {
        ExprNode* m_ptr;

    public:
        ~ExprNodeP();

        ExprNodeP();

        ExprNodeP(ExprNode* node);

        ExprNodeP(std::unique_ptr<ExprNode> node); //: m_ptr(node.release()) {}

        ExprNodeP(ExprNodeP&& x)
            : m_ptr(x.m_ptr)
        {
            x.m_ptr = nullptr;
        }

        ExprNodeP(const ExprNodeP& x) = delete;

        ExprNodeP& operator=(ExprNodeP&& x);

        ExprNodeP& operator=(const ExprNodeP& x) = delete;

        operator bool() const {
            return is_valid();
        }

        bool is_valid() const {
            return m_ptr != nullptr;
        }

        ExprNode& operator*() {
            return *m_ptr;
        }

        const ExprNode& operator*() const {
            return *m_ptr;
        }

        ExprNode* operator->() {
            return m_ptr;
        }

        const ExprNode* operator->() const {
            return m_ptr;
        }

        ExprNode* get() {
            return m_ptr;
        }

        const ExprNode* get() const {
            return m_ptr;
        }

        ExprNode* release();

        void reset(ExprNode* n = nullptr);

        const char* type_name() const;
    };

    class Expr {
        ::std::shared_ptr<ExprNode> m_node;

    public:
        Expr(ExprNodeP node);
        Expr(ExprNode* node);
        Expr();

        operator bool() const {
            return is_valid();
        }

        bool is_valid() const {
            return m_node.get() != nullptr;
        }

        const ExprNode& node() const;

        ExprNode& node();

        ::std::shared_ptr<ExprNode> take_node();

        void visit_nodes(NodeVisitor& v);
        void visit_nodes(NodeVisitor& v) const;

        Expr clone() const;

        friend ::std::ostream& operator<<(::std::ostream& os, const Expr& pat);
    };

}
