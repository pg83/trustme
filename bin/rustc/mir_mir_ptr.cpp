#include "mir_mir_ptr.h"
#include "mir_mir.h"

void ::MIR::FunctionPointer::reset() {
    if (this->ptr) {
        delete this->ptr;
        this->ptr = nullptr;
    }
}

namespace MIR {

FunctionPointer::FunctionPointer()
    : ptr(nullptr) {
}
FunctionPointer::FunctionPointer(::MIR::Function* p)
    : ptr(p) {
}
FunctionPointer::FunctionPointer(FunctionPointer&& x)
    : ptr(x.ptr) {
    x.ptr = nullptr;
}
FunctionPointer::~FunctionPointer() {
    reset();
}
FunctionPointer& FunctionPointer::operator=(FunctionPointer&& x) {
    reset();
    ptr = x.ptr;
    x.ptr = nullptr;
    return *this;
}
::MIR::Function* FunctionPointer::operator->() {
    if (!ptr) {
        throw "";
    }
    return ptr;
}
const ::MIR::Function* FunctionPointer::operator->() const {
    if (!ptr) {
        throw "";
    }
    return ptr;
}
::MIR::Function& FunctionPointer::operator*() {
    if (!ptr) {
        throw "";
    }
    return *ptr;
}
const ::MIR::Function& FunctionPointer::operator*() const {
    if (!ptr) {
        throw "";
    }
    return *ptr;
}
}
