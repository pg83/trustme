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
        bool require_const_calls;
        // Note: Pointer is needed to maintain internal reference stability
        ::std::vector<CsePtr> callStack;

        static unsigned s_next_eval_index;

    public:
        Evaluator(const Span& sp, const ::HIR::Crate& crate, Newval& nvs);

        Evaluator(Evaluator&&) = default;
        Evaluator(const Evaluator&) = delete;

        EncodedLiteral evaluateConstant(const ::HIR::ItemPath& ip, const ::HIR::ExprPtr& expr, ::HIR::TypeRef exp);
        EncodedLiteral evaluateConstant(const ::HIR::ItemPath& ip, const ::HIR::ExprPtr& expr, ::HIR::TypeRef exp, MonomorphState ms);

        void set_require_const_calls() {
            require_const_calls = true;
        }

        StaticTraitResolve& getResolve() {
            return this->resolve;
        }

    private:
        void push_stack_entry(::FmtLambda print_path, const ::MIR::Function& fcn, MonomorphState ms, ::HIR::TypeRef exp, ::HIR::Function::argsT argDefs, ::std::vector<::MIR::eval::AllocationPtr> args, const ::HIR::GenericParams* itemParamsDef, const ::HIR::GenericParams* implParamsDef);

        ::MIR::eval::AllocationPtr run_until_stack_empty();
        void run_statement(::MIR::eval::CallStackEntry& localState, const ::MIR::Statement& stmt);
        // Returns UINT_MAX on return
        unsigned run_terminator(::MIR::eval::CallStackEntry& localState, const ::MIR::Terminator& stmt);
        bool callFunction(::MIR::eval::CallStackEntry& localState, const MIR::LValue& rv_slot, ::std::shared_ptr<::HIR::Path> path, ::std::vector<::MIR::eval::AllocationPtr> callArgs);

        EncodedLiteral allocationToEncoded(const ::HIR::TypeData* ty, const ::MIR::eval::Allocation& a);
    };

} // namespace HIR
