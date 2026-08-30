#pragma once

#include "mir_mir_ptr.h"

#include <std/lib/vector.h>

#include <vector>

struct Span;

namespace stl {
    class ObjPool;
}

class HIRTypeData;
using HIRTypeRef = const HIRTypeData*;
class HIRSimplePath;
class HIRExprNode;
class HIRCrate;
class HIRExprState;

class HIRExprNodeP {
    HIRExprNode* ptr;

public:
    HIRExprNodeP();

    HIRExprNodeP(HIRExprNode* p);

    HIRExprNodeP(HIRExprNodeP&& x);

    HIRExprNodeP(const HIRExprNodeP&) = delete;
    ~HIRExprNodeP() = default;

    HIRExprNodeP& operator=(HIRExprNodeP&& x);

    HIRExprNodeP& operator=(const HIRExprNodeP&) = delete;

    operator bool() const {
        return ptr != nullptr;
    }

    HIRExprNode* get() const {
        return ptr;
    }

    void reset(HIRExprNode* p = nullptr) {
        ptr = p;
    }

    HIRExprNode* release();

    void swap(HIRExprNodeP& x);

    HIRExprNode& operator*();

    const HIRExprNode& operator*() const;

    HIRExprNode* operator->();

    const HIRExprNode* operator->() const;
};

class HIRExprStatePtr {
    HIRExprState* ptr;

public:
    HIRExprStatePtr();

    HIRExprStatePtr(stl::ObjPool* pool, HIRExprState);
    HIRExprStatePtr(const HIRExprStatePtr&) = delete;

    HIRExprStatePtr(HIRExprStatePtr&& x);

    ~HIRExprStatePtr();

    HIRExprStatePtr& operator=(const HIRExprStatePtr&) = delete;

    HIRExprStatePtr& operator=(HIRExprStatePtr&& x);

    operator bool() const {
        return ptr != nullptr;
    }

    HIRExprStatePtr clone(stl::ObjPool* pool) const;

    HIRExprState& operator*();

    const HIRExprState& operator*() const;

    HIRExprState* operator->();

    const HIRExprState* operator->() const;
};

class HIRExprPtr {
    HIRExprNodeP node;

public:
    stl::Vector<HIRTypeRef> bindings;
    stl::Vector<HIRTypeRef> erasedTypes;
    std::vector<HIRSimplePath> defineOpaque;

    MIRFunctionPointer mir;

    HIRExprStatePtr state;

public:
    HIRExprPtr();
    HIRExprPtr(HIRExprNodeP node);
    ~HIRExprPtr();
    HIRExprPtr(const HIRExprPtr&) = delete;
    HIRExprPtr(HIRExprPtr&&);
    HIRExprPtr& operator=(HIRExprPtr&&);

    HIRExprNodeP takeNode();

    operator bool() const {
        return node;
    }

    HIRExprNode* get() const {
        return node.get();
    }

    void reset(HIRExprNode* p) {
        node.reset(p);
    }

    Span span() const;

    HIRExprNode& operator*() {
        return *node;
    }

    const HIRExprNode& operator*() const {
        return *node;
    }

    HIRExprNode* operator->() {
        return &*node;
    }

    const HIRExprNode* operator->() const {
        return &*node;
    }

    const MIRFunction* getMirOpt() const;
    const MIRFunction& getMirOrError(const Span& sp) const;
    MIRFunction& getMirOrErrorMut(const Span& sp);

    const MIRFunction* getExtMir() const;
    MIRFunction* getExtMirMut();

    void setMir(MIRFunctionPointer mir);
};
