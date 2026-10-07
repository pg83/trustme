#include "mir_mir_ptr.h"

#include "mir_mir.h"

void MIRFunctionPointer::reset() {
    if (this->ptr) {
        delete this->ptr;
        this->ptr = nullptr;
    }
    this->lazy = nullptr;
}

MIRFunctionPointer::MIRFunctionPointer()
    : ptr(nullptr)
    , lazy(nullptr)
{
}

MIRFunctionPointer::MIRFunctionPointer(MIRFunction* p)
    : ptr(p)
    , lazy(nullptr)
{
}

MIRFunctionPointer::MIRFunctionPointer(MIRLazyBody* body)
    : ptr(nullptr)
    , lazy(body)
{
}

MIRFunctionPointer::MIRFunctionPointer(MIRFunctionPointer&& x)
    : ptr(x.ptr)
    , lazy(x.lazy)
{
    x.ptr = nullptr;
    x.lazy = nullptr;
}

MIRFunctionPointer::~MIRFunctionPointer() {
    reset();
}

MIRFunctionPointer& MIRFunctionPointer::operator=(MIRFunctionPointer&& x) {
    reset();
    ptr = x.ptr;
    lazy = x.lazy;
    x.ptr = nullptr;
    x.lazy = nullptr;
    return *this;
}

MIRFunction* MIRFunctionPointer::materialise() const {
    if (!ptr && lazy) {
        ptr = lazy->source->decodeBody(*lazy);
        if (lazy->bind) {
            lazy->bind(*lazy, *ptr);
        }
        lazy = nullptr;
    }
    if (!ptr) {
        UNREACHABLE();
    }
    return ptr;
}

MIRFunction* MIRFunctionPointer::operator->() {
    return materialise();
}

const MIRFunction* MIRFunctionPointer::operator->() const {
    return materialise();
}

MIRFunction& MIRFunctionPointer::operator*() {
    return *materialise();
}

const MIRFunction& MIRFunctionPointer::operator*() const {
    return *materialise();
}
