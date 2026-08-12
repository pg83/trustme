#pragma once

namespace MIR {

    class Function;

    class FunctionPointer {
        ::MIR::Function* ptr;

    public:
        FunctionPointer();

        FunctionPointer(::MIR::Function* p);

        FunctionPointer(FunctionPointer&& x);

        ~FunctionPointer();

        FunctionPointer& operator=(FunctionPointer&& x);

        void reset();

        ::MIR::Function* operator->();

        const ::MIR::Function* operator->() const;

        ::MIR::Function& operator*();

        const ::MIR::Function& operator*() const;

        operator bool() const {
            return ptr != nullptr;
        }
    };

}
