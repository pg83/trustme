#include "hir_expr_ptr.h"

#include "hir_expr.h"
#include "hir_expr_state.h"

#include <std/mem/obj_pool.h>

using namespace stl;

HIRExprPtr::HIRExprPtr() = default;

HIRExprPtr::HIRExprPtr(HIRExprNodeP v)
    : node(mv$(v))
{
}

HIRExprPtr::~HIRExprPtr() = default;
HIRExprPtr::HIRExprPtr(HIRExprPtr&&) = default;
HIRExprPtr& HIRExprPtr::operator=(HIRExprPtr&&) = default;

HIRExprNodeP HIRExprPtr::takeNode() {
    return HIRExprNodeP(node.release());
}

HIRExprStatePtr::HIRExprStatePtr(ObjPool* pool, HIRExprState x)
    : ptr(pool->make<HIRExprState>(std::move(x)))
{
}

HIRExprStatePtr::~HIRExprStatePtr() = default;

HIRExprStatePtr HIRExprStatePtr::clone(ObjPool* pool) const {
    auto rv = HIRExprStatePtr(pool, HIRExprState((*this)->types, (*this)->module, (*this)->modPath));
    rv->traits = (*this)->traits;
    rv->implGenerics = (*this)->implGenerics;
    rv->itemGenerics = (*this)->itemGenerics;
    rv->currentTraitPath = (*this)->currentTraitPath;
    rv->currentTraitImpl = (*this)->currentTraitImpl;
    rv->currentSelfType = (*this)->currentSelfType;
    rv->defineOpaque = (*this)->defineOpaque;
    rv->stage = (*this)->stage;
    rv->anonymousConst = (*this)->anonymousConst;
    return rv;
}

Span HIRExprPtr::span() const {
    if (*this) {
        return (*this)->span();
    }
    return {};
}

const MIRFunction* HIRExprPtr::getMirOpt() const {
    if (!this->mir) {
        return nullptr;
    }
    return &*this->mir;
}

const MIRFunction& HIRExprPtr::getMirOrError(const Span& sp) const {
    if (!this->mir) {
        BUG(sp, StringView("No MIR"));
    }
    return *this->mir;
}

MIRFunction& HIRExprPtr::getMirOrErrorMut(const Span& sp) {
    if (!this->mir) {
        BUG(sp, StringView("No MIR"));
    }
    return *this->mir;
}

const MIRFunction* HIRExprPtr::getExtMir() const {
    if (this->node) {
        return nullptr;
    }
    if (!this->mir) {
        return nullptr;
    }
    return &*this->mir;
}

MIRFunction* HIRExprPtr::getExtMirMut() {
    if (this->node) {
        return nullptr;
    }
    if (!this->mir) {
        return nullptr;
    }
    return &*this->mir;
}

void HIRExprPtr::setMir(MIRFunctionPointer mir) {
    BUG_ASSERT(!this->mir);
    this->mir = std::move(mir);
}

HIRExprNodeP::HIRExprNodeP()
    : ptr(nullptr)
{
}

HIRExprNodeP::HIRExprNodeP(HIRExprNode* p)
    : ptr(p)
{
}

HIRExprNodeP::HIRExprNodeP(HIRExprNodeP&& x)
    : ptr(x.ptr)
{
    x.ptr = nullptr;
}

HIRExprNodeP& HIRExprNodeP::operator=(HIRExprNodeP&& x) {
    ptr = x.ptr;
    x.ptr = nullptr;
    return *this;
}

HIRExprNode* HIRExprNodeP::release() {
    auto* rv = ptr;
    ptr = nullptr;
    return rv;
}

void HIRExprNodeP::swap(HIRExprNodeP& x) {
    auto* p = ptr;
    ptr = x.ptr;
    x.ptr = p;
}

HIRExprNode& HIRExprNodeP::operator*() {
    BUG_ASSERT(ptr);
    return *ptr;
}

const HIRExprNode& HIRExprNodeP::operator*() const {
    BUG_ASSERT(ptr);
    return *ptr;
}

HIRExprNode* HIRExprNodeP::operator->() {
    BUG_ASSERT(ptr);
    return ptr;
}

const HIRExprNode* HIRExprNodeP::operator->() const {
    BUG_ASSERT(ptr);
    return ptr;
}

HIRExprStatePtr::HIRExprStatePtr()
    : ptr(nullptr)
{
}

HIRExprStatePtr::HIRExprStatePtr(HIRExprStatePtr&& x)
    : ptr(x.ptr)
{
    x.ptr = nullptr;
}

HIRExprStatePtr& HIRExprStatePtr::operator=(HIRExprStatePtr&& x) {
    ptr = x.ptr;
    x.ptr = nullptr;
    return *this;
}

HIRExprState& HIRExprStatePtr::operator*() {
    BUG_ASSERT(ptr);
    return *ptr;
}

const HIRExprState& HIRExprStatePtr::operator*() const {
    BUG_ASSERT(ptr);
    return *ptr;
}

HIRExprState* HIRExprStatePtr::operator->() {
    BUG_ASSERT(ptr);
    return ptr;
}

const HIRExprState* HIRExprStatePtr::operator->() const {
    BUG_ASSERT(ptr);
    return ptr;
}
