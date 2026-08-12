#include "hir_expr_ptr.h"
#include "hir_expr.h"
#include "hir_expr_state.h"
#include <std/mem/obj_pool.h>

::HIR::ExprPtr::ExprPtr() = default;

::HIR::ExprPtr::ExprPtr(::HIR::ExprNodeP v)
    : node(mv$(v))
{
}

::HIR::ExprPtr::~ExprPtr() = default;
::HIR::ExprPtr::ExprPtr(ExprPtr&&) = default;
::HIR::ExprPtr& ::HIR::ExprPtr::operator=(ExprPtr&&) = default;

::HIR::ExprNodeP HIR::ExprPtr::take_node() {
    return ::HIR::ExprNodeP(node.release());
}

::HIR::ExprStatePtr::ExprStatePtr(stl::ObjPool* pool, ExprState x)
    : ptr(pool->make<ExprState>(::std::move(x)))
{
}

::HIR::ExprStatePtr::~ExprStatePtr() = default;

::HIR::ExprStatePtr HIR::ExprStatePtr::clone(stl::ObjPool* pool) const {
    auto rv = ::HIR::ExprStatePtr(pool, ::HIR::ExprState((*this)->m_types, (*this)->m_module, (*this)->m_mod_path));
    rv->m_traits = (*this)->m_traits;
    rv->m_impl_generics = (*this)->m_impl_generics;
    rv->m_item_generics = (*this)->m_item_generics;
    rv->m_current_trait_path = (*this)->m_current_trait_path;
    rv->m_current_trait_impl = (*this)->m_current_trait_impl;
    rv->stage = (*this)->stage;
    return rv;
}

const Span& HIR::ExprPtr::span() const {
    static Span static_sp;
    if (*this) {
        return (*this)->span();
    }
    return static_sp;
}

const ::MIR::Function* HIR::ExprPtr::get_mir_opt() const {
    if (!this->m_mir) {
        return nullptr;
    }
    return &*this->m_mir;
}

const ::MIR::Function& HIR::ExprPtr::get_mir_or_error(const Span& sp) const {
    if (!this->m_mir) {
        BUG(sp, "No MIR");
    }
    return *this->m_mir;
}

::MIR::Function& HIR::ExprPtr::get_mir_or_error_mut(const Span& sp) {
    if (!this->m_mir) {
        BUG(sp, "No MIR");
    }
    return *this->m_mir;
}

const ::MIR::Function* HIR::ExprPtr::get_ext_mir() const {
    if (this->node) {
        return nullptr;
    }
    if (!this->m_mir) {
        return nullptr;
    }
    return &*this->m_mir;
}

::MIR::Function* HIR::ExprPtr::get_ext_mir_mut() {
    if (this->node) {
        return nullptr;
    }
    if (!this->m_mir) {
        return nullptr;
    }
    return &*this->m_mir;
}

void HIR::ExprPtr::set_mir(::MIR::FunctionPointer mir) {
    assert(!this->m_mir);
    m_mir = ::std::move(mir);
}

namespace HIR {

ExprNodeP::ExprNodeP()
    : ptr(nullptr) {
}
ExprNodeP::ExprNodeP(::HIR::ExprNode* p)
    : ptr(p) {
}
ExprNodeP::ExprNodeP(ExprNodeP&& x)
    : ptr(x.ptr) {
    x.ptr = nullptr;
}
ExprNodeP& ExprNodeP::operator=(ExprNodeP&& x) {
    ptr = x.ptr;
    x.ptr = nullptr;
    return *this;
}
::HIR::ExprNode* ExprNodeP::release() {
    auto* rv = ptr;
    ptr = nullptr;
    return rv;
}
void ExprNodeP::swap(ExprNodeP& x) {
    auto* p = ptr;
    ptr = x.ptr;
    x.ptr = p;
}
::HIR::ExprNode& ExprNodeP::operator*() {
    assert(ptr);
    return *ptr;
}
const ::HIR::ExprNode& ExprNodeP::operator*() const {
    assert(ptr);
    return *ptr;
}
::HIR::ExprNode* ExprNodeP::operator->() {
    assert(ptr);
    return ptr;
}
const ::HIR::ExprNode* ExprNodeP::operator->() const {
    assert(ptr);
    return ptr;
}
ExprStatePtr::ExprStatePtr()
    : ptr(nullptr) {
}
ExprStatePtr::ExprStatePtr(ExprStatePtr&& x)
    : ptr(x.ptr) {
    x.ptr = nullptr;
}
ExprStatePtr& ExprStatePtr::operator=(ExprStatePtr&& x) {
    ptr = x.ptr;
    x.ptr = nullptr;
    return *this;
}
::HIR::ExprState& ExprStatePtr::operator*() {
    assert(ptr);
    return *ptr;
}
const ::HIR::ExprState& ExprStatePtr::operator*() const {
    assert(ptr);
    return *ptr;
}
::HIR::ExprState* ExprStatePtr::operator->() {
    assert(ptr);
    return ptr;
}
const ::HIR::ExprState* ExprStatePtr::operator->() const {
    assert(ptr);
    return ptr;
}
}
