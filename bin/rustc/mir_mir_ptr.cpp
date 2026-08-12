#include "mir_mir_ptr.h"
#include "mir_mir.h"

void MIRFunctionPointer::reset() {
    if (this->ptr) {
        delete this->ptr;
        this->ptr = nullptr;
    }
}


MIRFunctionPointer::MIRFunctionPointer()
    : ptr(nullptr) {
}
MIRFunctionPointer::MIRFunctionPointer(MIRFunction* p)
    : ptr(p) {
}
MIRFunctionPointer::MIRFunctionPointer(MIRFunctionPointer&& x)
    : ptr(x.ptr) {
    x.ptr = nullptr;
}
MIRFunctionPointer::~MIRFunctionPointer() {
    reset();
}
MIRFunctionPointer& MIRFunctionPointer::operator=(MIRFunctionPointer&& x) {
    reset();
    ptr = x.ptr;
    x.ptr = nullptr;
    return *this;
}
MIRFunction* MIRFunctionPointer::operator->() {
    if (!ptr) {
        throw "";
    }
    return ptr;
}
const MIRFunction* MIRFunctionPointer::operator->() const {
    if (!ptr) {
        throw "";
    }
    return ptr;
}
MIRFunction& MIRFunctionPointer::operator*() {
    if (!ptr) {
        throw "";
    }
    return *ptr;
}
const MIRFunction& MIRFunctionPointer::operator*() const {
    if (!ptr) {
        throw "";
    }
    return *ptr;
}
