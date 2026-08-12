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

namespace HIR {

    struct Evaluator {
        class Newval {
        public:
            virtual ::HIR::Path newStatic(::HIR::TypeRef type, EncodedLiteral value) = 0;
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
        Evaluator(const Span& sp, const ::HIR::Crate& crate, Newval& nvs);

        Evaluator(Evaluator&&) = default;
        Evaluator(const Evaluator&) = delete;

        EncodedLiteral evaluateConstant(const ::HIR::ItemPath& ip, const ::HIR::ExprPtr& expr, ::HIR::TypeRef exp);
        EncodedLiteral evaluateConstant(const ::HIR::ItemPath& ip, const ::HIR::ExprPtr& expr, ::HIR::TypeRef exp, MonomorphState ms);

        void setRequireConstCalls() {
            requireConstCalls = true;
        }

        StaticTraitResolve& getResolve() {
            return this->resolve;
        }

    private:
        void pushStackEntry(::FmtLambda printPath, const MIRFunction& fcn, MonomorphState ms, ::HIR::TypeRef exp, ::HIR::Function::argsT argDefs, ::std::vector<MIREvalAllocationPtr> args, const ::HIR::GenericParams* itemParamsDef, const ::HIR::GenericParams* implParamsDef);

        MIREvalAllocationPtr runUntilStackEmpty();
        void runStatement(MIREvalCallStackEntry& localState, const MIRStatement& stmt);
        // Returns UINT_MAX on return
        unsigned runTerminator(MIREvalCallStackEntry& localState, const MIRTerminator& stmt);
        bool callFunction(MIREvalCallStackEntry& localState, const MIRLValue& rvSlot, ::std::shared_ptr<::HIR::Path> path, ::std::vector<MIREvalAllocationPtr> callArgs);

        EncodedLiteral allocationToEncoded(const ::HIR::TypeData* ty, const MIREvalAllocation& a);
    };

} // namespace HIR
