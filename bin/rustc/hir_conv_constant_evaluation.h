#pragma once

/*
 */

#include "hir_hir.h"
#include "hir_typeck_static.h"
#include <std/mem/obj_pool.h>

namespace MIR {
    namespace eval {
        class AllocationPtr;
        class Allocation;
        class CallStackEntry;
    }
    class LValue;
    class Statement;
    class Terminator;
    class TypeResolve;
}

namespace HIR {

    struct Evaluator {
        class Newval {
        public:
            virtual ::HIR::Path new_static(::HIR::TypeRef type, EncodedLiteral value) = 0;
        };

        class CsePtr {
            ::MIR::eval::CallStackEntry* inner;

        public:
            ~CsePtr();

            CsePtr(::MIR::eval::CallStackEntry* ptr);

            CsePtr(const CsePtr&) = delete;
            CsePtr& operator=(const CsePtr&) = delete;

            CsePtr(CsePtr&& x);

            CsePtr& operator=(CsePtr&& x);

            ::MIR::eval::CallStackEntry* operator->() {
                return inner;
            }

            ::MIR::eval::CallStackEntry& operator*() {
                return *inner;
            }
        };

        Span root_span;
        // All values and relocations created by one constant evaluation form a
        // single graph. Nothing in that graph escapes `evaluate_constant`:
        // `allocation_to_encoded` deep-copies the result into an EncodedLiteral.
        stl::ObjPool::Ref value_pool;
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
        void pushStackEntry(::FmtLambda printPath, const ::MIR::Function& fcn, MonomorphState ms, ::HIR::TypeRef exp, ::HIR::Function::argsT argDefs, ::std::vector<::MIR::eval::AllocationPtr> args, const ::HIR::GenericParams* itemParamsDef, const ::HIR::GenericParams* implParamsDef);

        ::MIR::eval::AllocationPtr runUntilStackEmpty();
        void runStatement(::MIR::eval::CallStackEntry& localState, const ::MIR::Statement& stmt);
        // Returns UINT_MAX on return
        unsigned runTerminator(::MIR::eval::CallStackEntry& localState, const ::MIR::Terminator& stmt);
        bool callFunction(::MIR::eval::CallStackEntry& localState, const MIR::LValue& rvSlot, ::std::shared_ptr<::HIR::Path> path, ::std::vector<::MIR::eval::AllocationPtr> callArgs);

        EncodedLiteral allocationToEncoded(const ::HIR::TypeData* ty, const ::MIR::eval::Allocation& a);
    };

} // namespace HIR
