struct WireBoard;
#pragma once

/*
 */

#include "hir_hir.h"
#include "hir_typeck_static.h"
#include <std/mem/obj_pool.h>

class MIREvalAllocationPtr;
class MIREvalAllocation;
class MIREvalCallStackEntry;
class MIRLValue;
class MIRStatement;
class MIRTerminator;
class MIRTypeResolve;

// Thrown by constant evaluation when the requested value cannot be computed
// in the current environment. Reaching a catch of this with a fully concrete
// environment is a compiler bug, never a retry-later. See dev/DEFER.md.
struct Defer {
    enum class Reason {
        Layout,         // size/align/repr of a type is not computable yet
        GenericValue,   // the value or its path still names a generic parameter
        Infer,          // inference variables in the value or environment
        NotYetKnown,    // value resolution could not commit to an impl
        UnresolvedCall, // method call unresolved before main typecheck
    };
    static constexpr unsigned NUM_REASONS = 5;

    Reason reason;
    Span span;
};
extern ::std::ostream& operator<<(::std::ostream& os, Defer::Reason r);

struct HIREvaluator {
    class Newval {
    public:
        virtual HIRPath newStatic(HIRTypeRef type, EncodedLiteral value, size_t alignment) = 0;
    };

    class CsePtr {
        MIREvalCallStackEntry* inner;

    public:
        ~CsePtr();

        CsePtr(MIREvalCallStackEntry* ptr);

        CsePtr(const CsePtr&) = delete;
        CsePtr& operator=(const CsePtr&) = delete;

        CsePtr(CsePtr&& x);

        CsePtr& operator=(CsePtr&& x);

        MIREvalCallStackEntry* operator->() {
            return inner;
        }

        MIREvalCallStackEntry& operator*() {
            return *inner;
        }
    };

    Span rootSpan;
    // All values and relocations created by one constant evaluation form a
    // single graph. Nothing in that graph escapes `evaluate_constant`:
    // `allocation_to_encoded` deep-copies the result into an EncodedLiteral.
    stl::ObjPool::Ref valuePool;
    StaticTraitResolve resolve;
    Newval& nvs;
    unsigned int evalIndex;
    unsigned int numFrames;
    bool requireConstCalls;
    // Note: Pointer is needed to maintain internal reference stability
    ::std::vector<CsePtr> callStack;

    static unsigned sNextEvalIndex;

public:
    HIREvaluator(const Span& sp, const WireBoard& wb, Newval& nvs);

    HIREvaluator(HIREvaluator&&) = default;
    HIREvaluator(const HIREvaluator&) = delete;

    EncodedLiteral evaluateConstant(const HIRItemPath& ip, const HIRExprPtr& expr, HIRTypeRef exp);
    EncodedLiteral evaluateConstant(const HIRItemPath& ip, const HIRExprPtr& expr, HIRTypeRef exp, MonomorphState ms);

    void setRequireConstCalls() {
        requireConstCalls = true;
    }

    StaticTraitResolve& getResolve() {
        return this->resolve;
    }

private:
    void pushStackEntry(HIRItemPath printPath, const MIRFunction& fcn, MonomorphState ms, HIRTypeRef exp, HIRFunction::argsT argDefs, ::std::vector<MIREvalAllocationPtr> args, const HIRGenericParams* itemParamsDef, const HIRGenericParams* implParamsDef, SourceLocation callerLocation, bool tracksCaller);

    MIREvalAllocationPtr runUntilStackEmpty();
    void runStatement(MIREvalCallStackEntry& localState, const MIRStatement& stmt);
    // Returns UINT_MAX on return
    unsigned runTerminator(MIREvalCallStackEntry& localState, const MIRTerminator& stmt);
    bool callFunction(MIREvalCallStackEntry& localState, const MIRLValue& rvSlot, HIRPath* path, ::std::vector<MIREvalAllocationPtr> callArgs, const SourceLocation& callsite, bool indirect);
    void callConstDestructor(MIREvalCallStackEntry& localState, HIRTypeRef ty, const MIRLValue& slot);
    void runConstDrop(MIREvalCallStackEntry& localState, HIRTypeRef ty, const MIRLValue& slot);

    EncodedLiteral allocationToEncoded(const HIRTypeData* ty, const MIREvalAllocation& a);
};
