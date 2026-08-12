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

::HIR::ExprNodeP HIR::ExprPtr::takeNode() {
    return ::HIR::ExprNodeP(node.release());
}

::HIR::ExprStatePtr::ExprStatePtr(stl::ObjPool* pool, ExprState x)
    : ptr(pool->make<ExprState>(::std::move(x)))
{
}

::HIR::ExprStatePtr::~ExprStatePtr() = default;

::HIR::ExprStatePtr HIR::ExprStatePtr::clone(stl::ObjPool* pool) const {
    auto rv = ::HIR::ExprStatePtr(pool, ::HIR::ExprState((*this)->types, (*this)->mModule, (*this)->modPath));
    rv->traits = (*this)->traits;
    rv->mImplGenerics = (*this)->mImplGenerics;
    rv->mItemGenerics = (*this)->mItemGenerics;
    rv->mCurrentTraitPath = (*this)->mCurrentTraitPath;
    rv->currentTraitImpl = (*this)->currentTraitImpl;
    rv->stage = (*this)->stage;
    return rv;
}

const Span& HIR::ExprPtr::span() const {
    static Span staticSp;
    if (*this) {
        return (*this)->span();
    }
    return staticSp;
}

const MIRFunction* HIR::ExprPtr::getMirOpt() const {
    if (!this->mir) {
        return nullptr;
    }
    return &*this->mir;
}

const MIRFunction& HIR::ExprPtr::getMirOrError(const Span& sp) const {
    if (!this->mir) {
        BUG(sp, "No MIR");
    }
    return *this->mir;
}

MIRFunction& HIR::ExprPtr::getMirOrErrorMut(const Span& sp) {
    if (!this->mir) {
        BUG(sp, "No MIR");
    }
    return *this->mir;
}

const MIRFunction* HIR::ExprPtr::getExtMir() const {
    if (this->node) {
        return nullptr;
    }
    if (!this->mir) {
        return nullptr;
    }
    return &*this->mir;
}

MIRFunction* HIR::ExprPtr::getExtMirMut() {
    if (this->node) {
        return nullptr;
    }
    if (!this->mir) {
        return nullptr;
    }
    return &*this->mir;
}

void HIR::ExprPtr::setMir(MIRFunctionPointer mir) {
    assert(!this->mir);
    this->mir = ::std::move(mir);
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
