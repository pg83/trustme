struct WireBoard;
#pragma once

/*
 */

#include "hir_hir.h"
#include "hir_typeck_static.h"
#include <std/mem/obj_pool.h>

class MIRLValue;
class MIRStatement;
class MIRTerminator;
class MIRTypeResolve;
void CtfeCreateContext(WireBoard& wb, stl::ObjPool& pool);
void ConvertHIRConstantEvaluate(const WireBoard& wb, HIRCrate& hirCrate);
void ConvertHIRConstantEvaluateExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exp);
void ConvertHIRConstantEvaluateEnum(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, const HIREnum& enm);
void ConvertHIRConstantEvaluateEnumVariant(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, const HIREnum& enm, size_t idx);
void ConvertHIRConstantEvaluateConstant(const StaticTraitResolve& callerResolve, const HIRGenericParams* implParams, const HIRItemPath& ip, HIRConstant& e);
void ConvertHIRConstantEvaluateMethodParams(const Span& sp, const WireBoard& wb, const HIRCrate& crate, const HIRGenericParams* paramsDef, HIRPathParams& params);
void ConvertHIRConstantEvaluateConstGeneric(const Span& sp, const WireBoard& wb, const HIRCrate& crate, const HIRTypeData* ty, HIRConstGeneric& cg);
void ConvertHIRConstantEvaluateConstGeneric(const Span& sp, const WireBoard& wb, const HIRCrate& crate, HIRConstGeneric& cg);
void ConvertHIRConstantEvaluateArraySize(const Span& sp, const WireBoard& wb, const HIRCrate& crate, const HIRSimplePath& path, HIRArraySize& size);

struct HIREvaluator {
    struct MIREvalAllocationPtr;
    struct MIREvalAllocation;
    struct MIREvalCallStackEntry;

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

    stl::ObjPool::Ref valuePool;
    StaticTraitResolve resolve;
    Newval& nvs;
    unsigned int numFrames;
    bool requireConstCalls;

    std::vector<CsePtr> callStack;

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
    void pushStackEntry(HIRItemPath printPath, const MIRFunction& fcn, MonomorphState ms, HIRTypeRef exp, HIRFunction::argsT argDefs, std::vector<MIREvalAllocationPtr> args, const HIRGenericParams* itemParamsDef, const HIRGenericParams* implParamsDef, SourceLocation callerLocation, bool tracksCaller);

    MIREvalAllocationPtr runUntilStackEmpty();
    void runStatement(MIREvalCallStackEntry& localState, const MIRStatement& stmt);

    unsigned runTerminator(MIREvalCallStackEntry& localState, const MIRTerminator& stmt);
    bool callFunction(MIREvalCallStackEntry& localState, const MIRLValue& rvSlot, HIRPath* path, std::vector<MIREvalAllocationPtr> callArgs, const SourceLocation& callsite, bool indirect);
    void callConstDestructor(MIREvalCallStackEntry& localState, HIRTypeRef ty, const MIRLValue& slot);
    void runConstDrop(MIREvalCallStackEntry& localState, HIRTypeRef ty, const MIRLValue& slot);

    EncodedLiteral allocationToEncoded(const HIRTypeData* ty, const MIREvalAllocation& a);
};
