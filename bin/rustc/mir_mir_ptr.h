#pragma once

#include <stddef.h>

class MIRFunction;
class HIRType;
struct WireBoard;
class HIRGenericParams;
struct MIRLazyBody;

struct MIRBodySource {
    virtual MIRFunction* decodeBody(const MIRLazyBody& body) = 0;
};

struct MIRLazyBody {
    MIRBodySource* source;
    size_t offset;
    size_t typeBase;
    size_t pathBase;

    void (*bind)(const MIRLazyBody& body, MIRFunction& mir) = nullptr;
    const WireBoard* wb = nullptr;
    const HIRGenericParams* implGenerics = nullptr;
    const HIRGenericParams* itemGenerics = nullptr;
    const HIRType* selfType = nullptr;
};

class MIRFunctionPointer {
    mutable MIRFunction* ptr;
    mutable MIRLazyBody* lazy;

    MIRFunction* materialise() const;

public:
    MIRFunctionPointer();

    MIRFunctionPointer(MIRFunction* p);

    explicit MIRFunctionPointer(MIRLazyBody* body);

    MIRFunctionPointer(MIRFunctionPointer&& x);

    ~MIRFunctionPointer();

    MIRFunctionPointer& operator=(MIRFunctionPointer&& x);

    void reset();

    MIRLazyBody* pendingBody() const {
        return ptr ? nullptr : lazy;
    }

    MIRFunction* operator->();

    const MIRFunction* operator->() const;

    MIRFunction& operator*();

    const MIRFunction& operator*() const;

    operator bool() const {
        return ptr != nullptr || lazy != nullptr;
    }
};
