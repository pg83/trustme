/*
 * MRustC - Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * mir/mir_ptr.cpp
 * - Destructor for MIR function pointers (cold path code)
 */
#include "mir_mir_ptr.h"
#include "mir_mir.h"

void ::MIR::FunctionPointer::reset() {
    if (this->ptr) {
        delete this->ptr;
        this->ptr = nullptr;
    }
}
