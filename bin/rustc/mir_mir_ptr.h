#pragma once


    class MIRFunction;

    class MIRFunctionPointer {
        MIRFunction* ptr;

    public:
        MIRFunctionPointer();

        MIRFunctionPointer(MIRFunction* p);

        MIRFunctionPointer(MIRFunctionPointer&& x);

        ~MIRFunctionPointer();

        MIRFunctionPointer& operator=(MIRFunctionPointer&& x);

        void reset();

        MIRFunction* operator->();

        const MIRFunction* operator->() const;

        MIRFunction& operator*();

        const MIRFunction& operator*() const;

        operator bool() const {
            return ptr != nullptr;
        }
    };

