#include "mir_mir_ptr.h"
#include "mir_mir.h"

void ::MIR::FunctionPointer::reset() {
    if (this->ptr) {
        delete this->ptr;
        this->ptr = nullptr;
    }
}
