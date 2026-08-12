#pragma once

#include <vector>
#include <cassert>

#include "mir_mir_ptr.h"

struct Span;

namespace stl {
    class ObjPool;
}

namespace HIR {

    class TypeData;
    using TypeRef = const TypeData*;
    class ExprNode;
    class Crate;
    class ExprState;

    class ExprNodeP {
        ::HIR::ExprNode* ptr;

    public:
        ExprNodeP();

        ExprNodeP(::HIR::ExprNode* p);

        ExprNodeP(ExprNodeP&& x);

        ExprNodeP(const ExprNodeP&) = delete;
        ~ExprNodeP() = default;

        ExprNodeP& operator=(ExprNodeP&& x);

        ExprNodeP& operator=(const ExprNodeP&) = delete;

        operator bool() const {
            return ptr != nullptr;
        }

        ::HIR::ExprNode* get() const {
            return ptr;
        }

        void reset(::HIR::ExprNode* p = nullptr) {
            ptr = p;
        }

        ::HIR::ExprNode* release();

        void swap(ExprNodeP& x);

        ::HIR::ExprNode& operator*();

        const ::HIR::ExprNode& operator*() const;

        ::HIR::ExprNode* operator->();

        const ::HIR::ExprNode* operator->() const;
    };

    class ExprStatePtr {
        ::HIR::ExprState* ptr;

    public:
        ExprStatePtr();

        ExprStatePtr(stl::ObjPool* pool, ExprState);
        ExprStatePtr(const ExprStatePtr&) = delete;

        ExprStatePtr(ExprStatePtr&& x);

        ~ExprStatePtr();

        ExprStatePtr& operator=(const ExprStatePtr&) = delete;

        ExprStatePtr& operator=(ExprStatePtr&& x);

        operator bool() const {
            return ptr != nullptr;
        }

        ExprStatePtr clone(stl::ObjPool* pool) const;

        ::HIR::ExprState& operator*();

        const ::HIR::ExprState& operator*() const;

        ::HIR::ExprState* operator->();

        const ::HIR::ExprState* operator->() const;
    };

    class ExprPtr {
        //::HIR::Path m_path;
        ::HIR::ExprNodeP node;

    public:
        //::std::vector< ::HIR::TypeRef>  m_type_table;
        ::std::vector<::HIR::TypeRef> m_bindings;
        ::std::vector<::HIR::TypeRef> m_erased_types;

        // Public because too much relies on access to it
        ::MIR::FunctionPointer m_mir;

        ::HIR::ExprStatePtr m_state;

    public:
        ExprPtr();
        ExprPtr(::HIR::ExprNodeP node);
        ~ExprPtr();
        ExprPtr(const ExprPtr&) = delete;
        ExprPtr(ExprPtr&&);
        ExprPtr& operator=(ExprPtr&&);

        ::HIR::ExprNodeP take_node();

        operator bool() const {
            return node;
        }

        ::HIR::ExprNode* get() const {
            return node.get();
        }

        void reset(::HIR::ExprNode* p) {
            node.reset(p);
        }

        const Span& span() const;

        ::HIR::ExprNode& operator*() {
            return *node;
        }

        const ::HIR::ExprNode& operator*() const {
            return *node;
        }

        ::HIR::ExprNode* operator->() {
            return &*node;
        }

        const ::HIR::ExprNode* operator->() const {
            return &*node;
        }

        //void ensure_typechecked(const ::HIR::Crate& crate) const;
        /// Get MIR (checks if the MIR should be available)
        const ::MIR::Function* get_mir_opt() const;
        const ::MIR::Function& get_mir_or_error(const Span& sp) const;
        ::MIR::Function& get_mir_or_error_mut(const Span& sp);
        /// Get external MIR, returns nullptr if none
        const ::MIR::Function* get_ext_mir() const;
        ::MIR::Function* get_ext_mir_mut();

        void set_mir(::MIR::FunctionPointer mir);
    };

} // namespace HIR
