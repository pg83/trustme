#include "hir_expr_ptr.h"

#include "hir_expr.h"
#include "hir_expr_state.h"

#include <std/mem/obj_pool.h>

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

HIRExprStatePtr::HIRExprStatePtr(stl::ObjPool* pool, HIRExprState x)
    : ptr(pool->make<HIRExprState>(::std::move(x)))
{
}

HIRExprStatePtr::~HIRExprStatePtr() = default;

HIRExprStatePtr HIRExprStatePtr::clone(stl::ObjPool* pool) const {
    auto rv = HIRExprStatePtr(pool, HIRExprState((*this)->types, (*this)->mModule, (*this)->modPath));
    rv->traits = (*this)->traits;
    rv->mImplGenerics = (*this)->mImplGenerics;
    rv->mItemGenerics = (*this)->mItemGenerics;
    rv->mCurrentTraitPath = (*this)->mCurrentTraitPath;
    rv->currentTraitImpl = (*this)->currentTraitImpl;
    rv->stage = (*this)->stage;
    return rv;
}

const Span& HIRExprPtr::span() const {
    static Span staticSp;
    if (*this) {
        return (*this)->span();
    }
    return staticSp;
}

const MIRFunction* HIRExprPtr::getMirOpt() const {
    if (!this->mir) {
        return nullptr;
    }
    return &*this->mir;
}

const MIRFunction& HIRExprPtr::getMirOrError(const Span& sp) const {
    if (!this->mir) {
        BUG(sp, "No MIR");
    }
    return *this->mir;
}

MIRFunction& HIRExprPtr::getMirOrErrorMut(const Span& sp) {
    if (!this->mir) {
        BUG(sp, "No MIR");
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
    assert(!this->mir);
    this->mir = ::std::move(mir);
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
        assert(ptr);
        return *ptr;
    }

    const HIRExprNode& HIRExprNodeP::operator*() const {
        assert(ptr);
        return *ptr;
    }

    HIRExprNode* HIRExprNodeP::operator->() {
        assert(ptr);
        return ptr;
    }

    const HIRExprNode* HIRExprNodeP::operator->() const {
        assert(ptr);
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
        assert(ptr);
        return *ptr;
    }

    const HIRExprState& HIRExprStatePtr::operator*() const {
        assert(ptr);
        return *ptr;
    }

    HIRExprState* HIRExprStatePtr::operator->() {
        assert(ptr);
        return ptr;
    }

    const HIRExprState* HIRExprStatePtr::operator->() const {
        assert(ptr);
        return ptr;
    }
