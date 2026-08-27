#include "trans_main_bindings.h"
#include "trans_main_bindings.h"

#include <std/alg/defer.h>

#include "hir_hir.h"
#include "mir_mir.h"
#include "wire_board.h"
#include "hir_visitor.h"
#include "mir_helpers.h"
#include "trans_target.h"
#include "hir_item_path.h"
#include "mir_operations.h"
#include "trans_mangling.h"
#include "trans_allocator.h"
#include "trans_trans_list.h"
#include "hir_typeck_common.h" // monomorph
#include "hir_typeck_static.h" // StaticTraitResolve
#include "hir_conv_main_bindings.h"

#include <deque>
#include <algorithm> // find_if
#include <unordered_set>

namespace {
    struct State {
        HIRCrate& crate;
        StaticTraitResolve resolve;
        const TransList& transList;
        ::std::deque<HIRTypeRef> todoList;
        HIRTypeRefSet doneList;

        HIRSimplePath langClone;

        State(const WireBoard& wb, HIRCrate& crate, const TransList& transList)
            : crate(crate)
            , resolve(wb, OpaqueReveal::All)
            , transList(transList)
        {
            langClone = crate.getLangItemPathOpt("clone");
        }

        void enqueueType(const HIRTypeData* ty) {
            if (this->transList.autoCloneImpls.count(ty) == 0 && this->doneList.count(ty) == 0) {
                this->doneList.insert(ty);
                this->todoList.push_back(ty);
            }
        }
    };

}

namespace {
    /// The one place a generated body is handed to a function: the pointer
    /// takes ownership of the `MIRFunction`, which outlives this scope.
    MIRFunctionPointer generatedBody(MIRFunction mir = MIRFunction()) {
        return MIRFunctionPointer(new MIRFunction(mv$(mir)));
    }

    struct CloneCleanupState {
        ::std::vector<MIRBasicBlockId> calls;
        ::std::vector<::std::pair<MIRLValue, unsigned>> values;
    };

    MIRBasicBlock& cloneOpenBlock(MIRFunction& mirFcn) {
        if (mirFcn.blocks.empty() || !mirFcn.blocks.back().terminator.is_Incomplete()) {
            mirFcn.blocks.push_back(MIRBasicBlock());
        }
        return mirFcn.blocks.back();
    }

    MIRParam cloneField(const State& state, const Span& sp, MIRFunction& mirFcn, CloneCleanupState& cleanup, const HIRTypeData* subty, MIRLValue fldLvalue) {
        if (state.resolve.typeIsCopy(sp, subty)) {
            return ::std::move(fldLvalue);
        } else {
            const auto& langClone = state.resolve.hirCrate().getLangItemPath(sp, "clone");
            // Allocate to locals (one for the `&T`, the other for the cloned `T`)
            auto borrowLv = MIRLValue::newLocal(mirFcn.locals.size());
            mirFcn.locals.push_back(state.crate.types.borrow(HIRBorrowType::Shared, subty));
            auto resLv = MIRLValue::newLocal(mirFcn.locals.size());
            mirFcn.locals.push_back(subty);
            const auto dropFlag = static_cast<unsigned>(mirFcn.dropFlags.size());
            mirFcn.dropFlags.push_back(false);

            // Call `<T as Clone>::clone`, passing a borrow of the field
            auto& bb = cloneOpenBlock(mirFcn);
            bb.statements.push_back(MIRStatement::make_Assign({borrowLv.clone(), MIRRValue::make_Borrow({HIRBorrowType::Shared, false, mv$(fldLvalue)})}));
            HIRPathParams pp;
            const auto callBlock = static_cast<MIRBasicBlockId>(mirFcn.blocks.size() - 1);
            const auto retBlock = static_cast<MIRBasicBlockId>(mirFcn.blocks.size());
            bb.terminator = MIRTerminator::make_Call({retBlock, MIRUnwindAction::make_Continue({}), resLv.clone(), MIRCallTarget(HIRPath(subty, langClone, "clone", std::move(pp))), ::makeVec1<MIRParam>(::std::move(borrowLv))});
            cleanup.calls.push_back(callBlock);
            cleanup.values.push_back(::std::make_pair(resLv.clone(), dropFlag));

            mirFcn.blocks.push_back(MIRBasicBlock());
            mirFcn.blocks.back().statements.push_back(MIRStatement::make_SetDropFlag({dropFlag, true, ~0u}));

            // Save the output of the `clone` call
            return ::std::move(resLv);
        }
    }

    void appendCloneCleanup(MIRFunction& mirFcn, const CloneCleanupState& cleanup) {
        if (cleanup.calls.empty()) {
            return;
        }

        const auto cleanupStart = static_cast<MIRBasicBlockId>(mirFcn.blocks.size());
        const auto resume = static_cast<MIRBasicBlockId>(cleanupStart + cleanup.values.size());
        for (auto it = cleanup.values.rbegin(); it != cleanup.values.rend(); ++it) {
            MIRBasicBlock block;
            block.isCleanup = true;
            block.terminator = MIRTerminator::make_Drop({
                MIRDropKind::DEEP,
                it->first.clone(),
                it->second,
                static_cast<MIRBasicBlockId>(mirFcn.blocks.size() + 1),
                MIRUnwindAction::make_Terminate({}),
            });
            mirFcn.blocks.push_back(mv$(block));
        }
        assert(mirFcn.blocks.size() == resume);
        MIRBasicBlock resumeBlock;
        resumeBlock.isCleanup = true;
        resumeBlock.terminator = MIRTerminator::make_UnwindResume({});
        mirFcn.blocks.push_back(mv$(resumeBlock));

        for (const auto call : cleanup.calls) {
            assert(mirFcn.blocks.at(call).terminator.is_Call());
            mirFcn.blocks[call].terminator.as_Call().unwind = MIRUnwindAction::make_Cleanup(cleanupStart);
        }
    }
}

void TransAutoImplClone(State& state, HIRTypeRef ty) {
    Span sp;
    TRACE_FUNCTION_F(ty);

    // Create MIR
    MIRFunction mirFcn;
    if (state.resolve.typeIsCopy(sp, ty)) {
        MIRBasicBlock bb;
        bb.statements.push_back(MIRStatement::make_Assign({MIRLValue::newReturn(), MIRRValue::make_Use(MIRLValue::newDeref(MIRLValue::newArgument(0)))}));
        bb.terminator = MIRTerminator::make_Return({});
        mirFcn.blocks.push_back(::std::move(bb));
    } else {
        switch ((*ty).tag()) {
default:
            TODO(sp, "auto Clone for " << ty << " - Unknown and not Copy");
            case HIRTypeData::TAG_Path: {
                auto& te = (*ty).as_Path();
                if (te.isClosure()) {
                    const auto& gp = te.path.data.as_Generic();
                    const auto& str = state.resolve.hirCrate().getStructByPath(sp, gp.path);
                    auto p = TransParams::newImpl(state.crate.types, sp, ty, gp.params.clone());
                    CloneCleanupState cleanup;
                    ::std::vector<MIRParam> values;
                    values.reserve(str.data.as_Tuple().size());
                    for (const auto& fld : str.data.as_Tuple()) {
                        HIRTypeRef tmp;
                        const auto& tyM = monomorphiseTypeNeeded(fld.ent) ? (tmp = p.monomorph(state.resolve, fld.ent)) : fld.ent;
                        auto fldLvalue = MIRLValue::newField(MIRLValue::newDeref(MIRLValue::newArgument(0)), static_cast<unsigned>(values.size()));
                        values.push_back(cloneField(state, sp, mirFcn, cleanup, tyM, mv$(fldLvalue)));
                    }
                    // Construct the result value
                    auto& bb = cloneOpenBlock(mirFcn);
                    bb.statements.push_back(MIRStatement::make_Assign({MIRLValue::newReturn(), MIRRValue::make_Struct({gp.clone(), mv$(values)})}));
                    bb.terminator = MIRTerminator::make_Return({});
                    appendCloneCleanup(mirFcn, cleanup);
                } else {
                    TODO(sp, "auto Clone for " << ty << " - Unknown and not Copy");
                }
                break;
            }
            case HIRTypeData::TAG_Array: {
                auto& te = (*ty).as_Array();
                ASSERT_BUG(sp, te.size.as_Known() < 256, "TODO: Is more than 256 elements sane for auto-generated non-Copy Clone impl? " << ty);
                CloneCleanupState cleanup;
                ::std::vector<MIRParam> values;
                values.reserve(te.size.as_Known());
                for (size_t i = 0; i < te.size.as_Known(); i++) {
                    auto fldLvalue = MIRLValue::newField(MIRLValue::newDeref(MIRLValue::newArgument(0)), static_cast<unsigned>(values.size()));
                    values.push_back(cloneField(state, sp, mirFcn, cleanup, te.inner, mv$(fldLvalue)));
                }
                // Construct the result
                auto& bb = cloneOpenBlock(mirFcn);
                bb.statements.push_back(MIRStatement::make_Assign({MIRLValue::newReturn(), MIRRValue::make_Array({mv$(values)})}));
                bb.terminator = MIRTerminator::make_Return({});
                appendCloneCleanup(mirFcn, cleanup);
                break;
            }
            case HIRTypeData::TAG_Tuple: {
                auto& te = (*ty).as_Tuple();
                assert(te.size() > 0);

                CloneCleanupState cleanup;
                ::std::vector<MIRParam> values;
                values.reserve(te.size());
                // For each field of the tuple, create a clone (either using Copy if posible, or calling Clone::clone)
                for (const auto& subty : te) {
                    auto fldLvalue = MIRLValue::newField(MIRLValue::newDeref(MIRLValue::newArgument(0)), static_cast<unsigned>(values.size()));
                    values.push_back(cloneField(state, sp, mirFcn, cleanup, subty, mv$(fldLvalue)));
                }

                // Construct the result tuple
                auto& bb = cloneOpenBlock(mirFcn);
                bb.statements.push_back(MIRStatement::make_Assign({MIRLValue::newReturn(), MIRRValue::make_Tuple({mv$(values)})}));
                bb.terminator = MIRTerminator::make_Return({});
                appendCloneCleanup(mirFcn, cleanup);
                break;
            }
        }
    }

    // Function
    HIRFunction fcn{
        HIRFunction::Receiver::BorrowShared,
        HIRGenericParams{},
        /*m_args=*/::makeVec1(::std::make_pair(HIRPattern(HIRPatternBinding(false, HIRPatternBinding::Type::Move, "self", 0), HIRPattern::Data::make_Any({})), state.crate.types.borrow(HIRBorrowType::Shared, ty))),
        /*m_return=*/ty,
        HIRExprPtr{}
    };
    fcn.code.mir = generatedBody(mv$(mirFcn));

    // Impl
    HIRTraitImpl impl;
    impl.type = ty;
    impl.methods.insert(::std::make_pair(RcString("clone"), HIRTraitImpl::ImplEnt<HIRFunction>{false, ::std::move(fcn)}));

    // `clone_from` is the trait's own default body -- clone the source, drop
    // what the destination held, and move the clone in. A generated impl is
    // named by the caller like any other, so it carries its own copy.
    if (state.transList.autoCloneFromImpls.count(ty)) {
        MIRFunction fromMir;
        auto dst = MIRLValue::newDeref(MIRLValue::newArgument(0));
        if (state.resolve.typeIsCopy(sp, ty)) {
            MIRBasicBlock bb;
            bb.statements.push_back(MIRStatement::make_Assign({dst.clone(), MIRRValue::make_Use(MIRLValue::newDeref(MIRLValue::newArgument(1)))}));
            bb.terminator = MIRTerminator::make_Return({});
            fromMir.blocks.push_back(mv$(bb));
        } else {
            const auto& langClone = state.resolve.hirCrate().getLangItemPath(sp, "clone");
            auto cloned = MIRLValue::newLocal(fromMir.locals.size());
            fromMir.locals.push_back(ty);

            MIRBasicBlock call;
            call.terminator = MIRTerminator::make_Call({
                1,
                MIRUnwindAction::make_Continue({}),
                cloned.clone(),
                MIRCallTarget(HIRPath(ty, langClone, "clone", HIRPathParams())),
                ::makeVec1<MIRParam>(MIRLValue::newArgument(1)),
            });
            fromMir.blocks.push_back(mv$(call));

            MIRBasicBlock drop;
            drop.terminator = MIRTerminator::make_Drop({MIRDropKind::DEEP, dst.clone(), ~0u, 2, MIRUnwindAction::make_Continue({})});
            fromMir.blocks.push_back(mv$(drop));

            MIRBasicBlock store;
            store.statements.push_back(MIRStatement::make_Assign({dst.clone(), MIRRValue::make_Use(mv$(cloned))}));
            store.terminator = MIRTerminator::make_Return({});
            fromMir.blocks.push_back(mv$(store));
        }

        auto fromArgs = ::makeVec1(::std::make_pair(
            HIRPattern(HIRPatternBinding(false, HIRPatternBinding::Type::Move, "self", 0), HIRPattern::Data::make_Any({})),
            state.crate.types.borrow(HIRBorrowType::Unique, ty)
        ));
        fromArgs.push_back(::std::make_pair(
            HIRPattern(HIRPatternBinding(false, HIRPatternBinding::Type::Move, "source", 1), HIRPattern::Data::make_Any({})),
            state.crate.types.borrow(HIRBorrowType::Shared, ty)
        ));
        HIRFunction fromFcn{
            HIRFunction::Receiver::BorrowUnique,
            HIRGenericParams{},
            mv$(fromArgs),
            /*m_return=*/state.crate.types.unit(),
            HIRExprPtr{}
        };
        fromFcn.code.mir = generatedBody(mv$(fromMir));
        impl.methods.insert(::std::make_pair(RcString("clone_from"), HIRTraitImpl::ImplEnt<HIRFunction>{false, ::std::move(fromFcn)}));
    }

    // Add impl to the crate
    auto& list = state.crate.traitImpls[state.langClone].getListForTypeMut(impl.type);
    list.push_back(box$(impl));
    state.crate.allTraitImpls[state.langClone].getListForTypeMut(list.back()->type).push_back(list.back().get());
}

namespace {

    struct Builder {
        const State& state;
        MIRFunction& mir;
        const MIRLValue self;

        Builder(const State& state, MIRFunction& mir)
            : state(state)
            , mir(mir)
            , self(MIRLValue::newArgument(0))
        {
            mir.blocks.push_back(MIRBasicBlock());
        }

        MIRLValue addLocal(HIRTypeRef ty) {
            auto rv = mir.locals.size();
            mir.locals.push_back(mv$(ty));
            return MIRLValue::newLocal(rv);
        }

        MIRLValue inTemporary(HIRTypeRef ty, MIRRValue val) {
            auto rv = addLocal(mv$(ty));
            pushStmtAssign(rv.clone(), mv$(val));
            return rv;
        }

        void ensureOpen() {
            if (!mir.blocks.back().terminator.is_Incomplete()) {
                mir.blocks.push_back(MIRBasicBlock());
            }
        }

        void pushStmt(MIRStatement s) {
            ensureOpen();
            mir.blocks.back().statements.push_back(mv$(s));
        }

        void pushStmtAssign(MIRLValue lv, MIRRValue rv) {
            this->pushStmt(MIRStatement::make_Assign({mv$(lv), mv$(rv)}));
        }

        MIRBasicBlockId pushStmtDrop(MIRLValue lv) {
            ensureOpen();
            const auto dropBlock = static_cast<MIRBasicBlockId>(mir.blocks.size() - 1);
            const auto next = static_cast<MIRBasicBlockId>(mir.blocks.size());
            terminateBlock(MIRTerminator::make_Drop({MIRDropKind::DEEP, mv$(lv), ~0u, next, MIRUnwindAction::make_Continue({})}));
            mir.blocks.push_back(MIRBasicBlock());
            return dropBlock;
        }

        void pushDropSequence(::std::vector<MIRLValue> values, MIRBasicBlockId customDropCall = ~0u) {
            if (values.empty()) {
                return;
            }

            // Lay the cleanup suffixes out before the normal chain.  A panic
            // from field N starts at field N+1; a panic from the user's Drop
            // implementation starts at field zero.  Cleanup drops terminate
            // on a second panic.
            ensureOpen();
            const auto entry = static_cast<MIRBasicBlockId>(mir.blocks.size() - 1);
            terminateBlock(MIRTerminator::make_Goto(~0u));

            const size_t cleanupFirst = customDropCall == ~0u ? 1 : 0;
            const auto cleanupStart = static_cast<MIRBasicBlockId>(mir.blocks.size());
            const auto cleanupBlock = [&](size_t field) {
                assert(field >= cleanupFirst && field < values.size());
                return static_cast<MIRBasicBlockId>(cleanupStart + field - cleanupFirst);
            };

            if (cleanupFirst < values.size()) {
                const auto resume = static_cast<MIRBasicBlockId>(cleanupStart + values.size() - cleanupFirst);
                for (size_t i = cleanupFirst; i < values.size(); i++) {
                    MIRBasicBlock block;
                    block.isCleanup = true;
                    const auto target = i + 1 < values.size() ? cleanupBlock(i + 1) : resume;
                    block.terminator = MIRTerminator::make_Drop({
                        MIRDropKind::DEEP,
                        values[i].clone(),
                        ~0u,
                        target,
                        MIRUnwindAction::make_Terminate({}),
                    });
                    mir.blocks.push_back(mv$(block));
                }
                MIRBasicBlock resumeBlock;
                resumeBlock.isCleanup = true;
                resumeBlock.terminator = MIRTerminator::make_UnwindResume({});
                mir.blocks.push_back(mv$(resumeBlock));
            }

            const auto normalStart = static_cast<MIRBasicBlockId>(mir.blocks.size());
            mir.blocks[entry].terminator.as_Goto() = normalStart;
            for (size_t i = 0; i < values.size(); i++) {
                MIRBasicBlock block;
                const auto target = static_cast<MIRBasicBlockId>(normalStart + i + 1);
                auto unwind = i + 1 < values.size() ? MIRUnwindAction::make_Cleanup(cleanupBlock(i + 1)) : MIRUnwindAction::make_Continue({});
                block.terminator = MIRTerminator::make_Drop({
                    MIRDropKind::DEEP,
                    values[i].clone(),
                    ~0u,
                    target,
                    mv$(unwind),
                });
                mir.blocks.push_back(mv$(block));
            }
            mir.blocks.push_back(MIRBasicBlock());

            if (customDropCall != ~0u) {
                assert(mir.blocks.at(customDropCall).terminator.is_Call());
                mir.blocks[customDropCall].terminator.as_Call().unwind = MIRUnwindAction::make_Cleanup(cleanupBlock(0));
            }
        }

        void terminateBlock(MIRTerminator term) {
            assert(mir.blocks.back().terminator.is_Incomplete());
            mir.blocks.back().terminator = mv$(term);
        }

        void terminateCall(MIRLValue rv, MIRCallTarget tgt, std::vector<MIRParam> args, MIRBasicBlockId bbRet, MIRBasicBlockId bbPanic, bool tracksCaller = false) {
            this->terminateBlock(MIRTerminator::make_Call({bbRet, MIRUnwindAction::make_Cleanup(bbPanic), mv$(rv), mv$(tgt), mv$(args), {}, tracksCaller}));
        }

        MIRBasicBlockId pushCallDrop(const HIRTypeData* ty) {
            // Get a `&mut *self`
            auto borrowLv = this->addLocal(state.crate.types.borrow(HIRBorrowType::Unique, ty));
            this->pushStmtAssign(borrowLv.clone(), MIRRValue::make_Borrow({HIRBorrowType::Unique, false, MIRLValue::newDeref(this->self.clone())}));

            ensureOpen();
            const auto callBlock = static_cast<MIRBasicBlockId>(mir.blocks.size() - 1);
            const auto retBlock = static_cast<MIRBasicBlockId>(mir.blocks.size());
            this->terminateBlock(
                MIRTerminator::make_Call({
                    retBlock,
                    MIRUnwindAction::make_Continue({}),
                    MIRLValue::newReturn(),
                    HIRPath(ty, state.resolve.langDrop(), "drop"),
                    makeVec1<MIRParam>(mv$(borrowLv)),
                })
            );
            mir.blocks.push_back(MIRBasicBlock());
            return callBlock;
        }
    };

    MIRLValue derefBox(MIRLValue box) {
        auto innerPtr = MIRLValue::newField(MIRLValue::newField(mv$(box), 0), 0);
        innerPtr = MIRLValue::newField(std::move(innerPtr), 0);
        return MIRLValue::newDeref(std::move(innerPtr));
    }

    MIRLValue getUnitPtr(const Span& sp, Builder& mutator, HIRTypeRef ty, MIRLValue lv, MIRLValue& outInnerPtr) {
        if (ty->is_Path()) {
            const auto& te = ty->as_Path();
            ASSERT_BUG(sp, te.binding.is_Struct(), "");
            const auto& tyPath = te.path.data.as_Generic();
            const auto& str = *te.binding.as_Struct();
            HIRTypeRef tmp;
            auto monomorph = [&](const auto& t) {
                return MonomorphStatePtr(mutator.state.crate.types, ty, &tyPath.params, nullptr).monomorphType(sp, t);
            };
            ::std::vector<MIRParam> vals;
            switch (str.data.tag()) {
                case HIRStructData::TAG_Unit: {
                    break;
                }
                case HIRStructData::TAG_Tuple: {
                    auto& se = str.data.as_Tuple();
                    for (unsigned int i = 0; i < se.size(); i++) {
                        auto val = MIRLValue::newField((i == se.size() - 1 ? mv$(lv) : lv.clone()), i);
                        if (i == str.structMarkings.coerceUnsizedIndex) {
                            vals.push_back(getUnitPtr(sp, mutator, monomorph(se[i].ent), mv$(val), outInnerPtr));
                        } else {
                            vals.push_back(mv$(val));
                        }
                    }
                    break;
                }
                case HIRStructData::TAG_Named: {
                    auto& se = str.data.as_Named();
                    for (unsigned int i = 0; i < se.size(); i++) {
                        auto val = MIRLValue::newField((i == se.size() - 1 ? mv$(lv) : lv.clone()), i);
                        if (i == str.structMarkings.coerceUnsizedIndex) {
                            vals.push_back(getUnitPtr(sp, mutator, monomorph(se[i].ty), mv$(val), outInnerPtr));
                        } else {
                            vals.push_back(mv$(val));
                        }
                    }
                    break;
                }
            }

            auto newPath = tyPath.clone();
            return mutator.inTemporary( mv$(ty), MIRRValue::make_Struct({ mv$(newPath), mv$(vals) }) );
        } else if (ty->is_Borrow() || ty->is_Pointer()) {
            outInnerPtr = lv.clone();
            return mutator.inTemporary(mv$(ty), MIRRValue::make_DstPtr({mv$(lv)}));
        } else {
            BUG(sp, "Unexpected type coerce_unsize in receiver - " << ty);
        }
    }
}

void TransAutoImpls(const WireBoard& wb, HIRCrate& crate, TransList& transList) {
    State state{wb, crate, transList};

    {
        // Generate for all
        for (const auto& ty : transList.autoCloneImpls) {
            state.doneList.insert(ty);
            TransAutoImplClone(state, ty);
        }

        while (!state.todoList.empty()) {
            auto ty = ::std::move(state.todoList.front());
            state.todoList.pop_back();

            TransAutoImplClone(state, mv$(ty));
        }

        auto implListIt = crate.traitImpls.find(state.langClone);
        for (const auto& ty : state.doneList) {
            assert(implListIt != crate.traitImpls.end());
            // TODO: Find a way of turning a set into a vector so items can be erased.

            const auto* implList = implListIt->second.getListForType(ty);
            ASSERT_BUG(Span(), implList, "No impl list of Clone for " << ty);
            auto& impl = **::std::find_if(implList->begin(), implList->end(), [&](const auto& i) {
                return i->type == ty;
            });

            auto bind = [&](const RcString& method) {
                auto p = HIRPath(ty, HIRGenericPath(state.langClone), method);
                auto* e = transList.addFunction(crate.types, p.clone());
                if (!e) {
                    // The list already holds it under its symbol, put there
                    // with a body; there is nothing to fill in.
                    DEBUG(p << " was already enumerated");
                    return;
                }
                auto m = impl.methods.find(method);
                ASSERT_BUG(Span(), m != impl.methods.end(), "Generated Clone for " << ty << " has no `" << method << "`");
                e->ptr = &m->second.data;
            };
            bind("clone");
            if (transList.autoCloneFromImpls.count(ty)) {
                bind("clone_from");
            }
        }
        transList.autoCloneImpls.clear();
        transList.autoCloneFromImpls.clear();
    }

    if (!transList.autoFnptrImpls.empty()) {
        const auto& langFnPtr = crate.getLangItemPath(Span(), "fn_ptr_trait");
        for (const auto& ty : transList.autoFnptrImpls) {
            auto outTy = state.crate.types.pointer(HIRBorrowType::Shared, state.crate.types.unit());
            MIRFunction mirFcn;

            MIRBasicBlock bb;
            bb.statements.push_back(MIRStatement::make_Assign({MIRLValue::newReturn(), MIRRValue::make_Cast({MIRLValue::newArgument(0), outTy})}));
            bb.terminator = MIRTerminator::make_Return({});
            mirFcn.blocks.push_back(::std::move(bb));

            // Function
            // `fn addr(self) -> usize;`
            HIRFunction fcn{
                HIRFunction::Receiver::Value,
                HIRGenericParams{},
                /*m_args=*/::makeVec1(::std::make_pair(HIRPattern(HIRPatternBinding(false, HIRPatternBinding::Type::Move, "self", 0), HIRPattern::Data::make_Any({})), ty)),
                /*m_return=*/std::move(outTy),
                HIRExprPtr{}
            };
            fcn.code.mir = generatedBody(mv$(mirFcn));

            // Impl
            HIRTraitImpl impl;
            impl.type = ty;
            impl.methods.insert(::std::make_pair(RcString::newInterned("addr"), HIRTraitImpl::ImplEnt<HIRFunction>{false, ::std::move(fcn)}));

            // Add impl to the crate
            auto& list = state.crate.traitImpls[langFnPtr].getListForTypeMut(impl.type);
            list.push_back(box$(impl));
            state.crate.allTraitImpls[langFnPtr].getListForTypeMut(list.back()->type).push_back(list.back().get());

            // - Add this function to the TransList

            {
                auto p = HIRPath(ty, HIRGenericPath(langFnPtr), "addr");
                auto e = transList.addFunction(crate.types, ::std::move(p));

                auto& impl = *list.back();
                assert(impl.methods.size() == 1);
                e->ptr = &impl.methods.begin()->second.data;
            }
        }
        transList.autoFnptrImpls.clear();
    }

    // Trait object methods
    {
        TRACE_FUNCTION_F("Trait object methods");
        transList.autoFunctions.reserve(transList.autoFunctions.size() + transList.traitObjectMethods.size());
        for (const auto& path : transList.traitObjectMethods) {
            DEBUG(path);
            Span sp;
            const auto& pe = path.data.as_UfcsKnown();
            const auto& traitPath = pe.trait;
            const auto& name = pe.item;
            const auto& tyDyn = pe.type->as_TraitObject();

            const auto& trait = crate.getTraitByPath(sp, traitPath.path);
            const auto& fcnDef = trait.values.at(name).as_Function();

            // Get the vtable index for this function
            unsigned vtableIdx = tyDyn.trait.traitPtr->getVtableValueIndex(traitPath, name);
            ASSERT_BUG(sp, vtableIdx > 0, "Calling method '" << name << "' from " << traitPath << " through " << pe.type << " which isn't in the vtable");

            auto pp = fcnDef.params.makeNopParams(crate.types, 1);
            MonomorphStatePtr ms(crate.types, pe.type, &traitPath.params, &pp);

            HIRFunction newFcn;
            newFcn.markings.trackCaller = fcnDef.markings.trackCaller;
            newFcn.markings.alignment = fcnDef.markings.alignment;
            newFcn.returnType = ms.monomorphType(sp, fcnDef.returnType);
            state.resolve.expandAssociatedTypes(sp, newFcn.returnType);
            for (const auto& arg : fcnDef.args) {
                newFcn.args.push_back(std::make_pair(HIRPattern(), ms.monomorphType(sp, arg.second)));
                state.resolve.expandAssociatedTypes(sp, newFcn.args.back().second);
            }
            ASSERT_BUG(sp, !newFcn.args.empty(), "Trait object method with no arguments?!");

            newFcn.code.mir = generatedBody();
            Builder builder(state, *newFcn.code.mir);

            MIRLValue lvSelf = MIRLValue::newArgument(0);
            MIRLValue lvPtr;
            // ---
            // bb0:
            //   _1 = DstPtr a1
            switch (fcnDef.receiver) {
                case HIRFunction::Receiver::Value: {
                    // By-value trait object dispatch
                    // - Receiver should be a `&move` (BUT, does the caller know this?)
                    // - MIR Cleanup should fix that (after monomoprh)
                    auto& selfTy = newFcn.args.front().second;
                    selfTy = crate.types.borrow(HIRBorrowType::Owned, selfTy);
                    lvPtr = builder.addLocal(crate.types.borrow(HIRBorrowType::Owned, crate.types.unit()));
                    builder.pushStmtAssign(lvPtr.clone(), MIRRValue::make_DstPtr({lvSelf.clone()}));
                    DEBUG("<dyn " << traitPath << ">::" << name << " - By-Value");
                } break;
                case HIRFunction::Receiver::BorrowOwned:
                case HIRFunction::Receiver::BorrowUnique:
                case HIRFunction::Receiver::BorrowShared: {
                    ASSERT_BUG(sp, newFcn.args.front().second->is_Borrow(), newFcn.args.front().second);
                    auto bt = newFcn.args.front().second->as_Borrow().type;
                    DEBUG("<dyn " << traitPath << ">::" << name << " - By-borrow");
                    lvPtr = builder.addLocal(crate.types.borrow(bt, crate.types.unit()));
                    builder.pushStmtAssign(lvPtr.clone(), MIRRValue::make_DstPtr({lvSelf.clone()}));
                } break;
                case HIRFunction::Receiver::Box: {
                    // TODO: What is the real reciver here? (for the MIR)
                    // - the `self` type is `Box<dyn ThisTrait>`, so need to deref through that to the right type
                    DEBUG("<dyn " << traitPath << ">::" << name << " - Boxed");
                    // - Need to make a new receiver (convert `Box<dyn ThisTrait>` into `Box<()>`)
                    auto gpath = newFcn.args.front().second->as_Path().path.data.as_Generic().clone();
                    gpath.params.types.at(0) = crate.types.unit();
                    auto ty = crate.types.path(mv$(gpath), newFcn.args.front().second->as_Path().binding.clone());
                    lvPtr = getUnitPtr(sp, builder, mv$(ty), MIRLValue::newArgument(0), lvSelf);
                } break;
                case HIRFunction::Receiver::Custom: {
                    ASSERT_BUG(sp, fcnDef.receiverType, "Custom receiver without a receiver type");
                    auto thinReceiver = cloneTyWith(crate.types, sp, newFcn.args.front().second, [&](const HIRTypeData* ty, HIRTypeRef& out) {
                        if (ty == pe.type) {
                            out = crate.types.unit();
                            return true;
                        }
                        return false;
                    });
                    lvPtr = getUnitPtr(sp, builder, mv$(thinReceiver), MIRLValue::newArgument(0), lvSelf);
                } break;
                default:
                    TODO(sp, "Handle different receiver types: <dyn " << traitPath << ">::" << name << " - self: " << newFcn.args.front().second);
            }

            //   _2 = DstMeta a1
            auto lvVtable = builder.addLocal(crate.types.borrow(HIRBorrowType::Shared, tyDyn.trait.traitPtr->getVtableType(sp, crate, tyDyn)));
            builder.pushStmtAssign(lvVtable.clone(), MIRRValue::make_DstMeta({mv$(lvSelf)}));
            //   rv = _2*.{idx}(a2, ...) goto bb2 else bb3
            std::vector<MIRParam> callArgs;
            callArgs.push_back(mv$(lvPtr));
            for (size_t i = 1; i < fcnDef.args.size(); i++) {
                callArgs.push_back(MIRLValue::newArgument(i));
            }
            builder.terminateCall(MIRLValue::newReturn(), MIRLValue::newField(MIRLValue::newDeref(mv$(lvVtable)), vtableIdx), mv$(callArgs), 1, 2, fcnDef.markings.trackCaller);
            // bb1:
            //   RETURN
            builder.ensureOpen();
            builder.terminateBlock(MIRTerminator::make_Return({}));
            // bb2:
            //   UNWIND
            builder.ensureOpen();
            builder.mir.blocks.back().isCleanup = true;
            builder.terminateBlock(MIRTerminator::make_UnwindResume({}));
            // ---

            transList.autoFunctions.push_back(box$(newFcn));
            auto* e = transList.addFunction(crate.types, path.clone());
            if (e) {
                e->ptr = transList.autoFunctions.back().get();
            } else {
                transList.autoFunctions.pop_back();
            }
        }
        transList.traitObjectMethods.clear();
    }

    // Create VTable instances
    {
        TRACE_FUNCTION_F("VTables");
        transList.autoStatics.reserve(transList.vtables.size());
        for (const auto& ent : transList.vtables) {
            Span sp;
            const auto& path = ent.first;
            const auto& traitPath = path.data.as_UfcsKnown().trait;
            const auto& type = path.data.as_UfcsKnown().type;

            struct {
                const char* fcnName;
                const HIRSimplePath* traitPath;
                HIRBorrowType bt;
            } const entries[3] = {{"call", &state.resolve.langFn(), HIRBorrowType::Shared}, {"call_mut", &state.resolve.langFnMut(), HIRBorrowType::Unique}, {"call_once", &state.resolve.langFnOnce(), HIRBorrowType::Owned}};

            size_t offset;
            if (traitPath.path == state.resolve.langFn()) {
                offset = 0;
            } else if (traitPath.path == state.resolve.langFnMut()) {
                offset = 1;
            } else if (traitPath.path == state.resolve.langFnOnce()) {
                offset = 2;
            } else {
                offset = 3; // Wait, is this reachable?
            }

            if (const auto* te = type->opt_NamedFunction()) {
                for (; offset < sizeof(entries) / sizeof(entries[0]); offset++) {
                    bool isByValue = (offset == 2);
                    const auto& ent = entries[offset];

                    auto fcnP = path.clone();
                    fcnP.data.as_UfcsKnown().item = ent.fcnName;
                    fcnP.data.as_UfcsKnown().trait.path = ent.traitPath->clone();

                    auto* e = transList.addFunction(crate.types, mv$(fcnP));
                    if (e) {
                        auto ft = te->decay(crate.types, sp);

                        ::std::vector<HIRTypeRef> argTys;
                        for (auto& ty : ft.argTypes) {
                            argTys.push_back(ty);
                        }
                        auto argTy = crate.types.tuple(mv$(argTys));
                        state.resolve.expandAssociatedTypes(sp, argTy);

                        HIRFunction fcn;
                        fcn.returnType = ft.rettype;
                        state.resolve.expandAssociatedTypes(sp, argTy);
                        fcn.args.push_back(std::make_pair(HIRPattern(), !isByValue ? crate.types.borrow(ent.bt, type) : type));
                        fcn.args.push_back(std::make_pair(HIRPattern(), mv$(argTy)));

                        fcn.code.mir = generatedBody();
                        Builder builder(state, *fcn.code.mir);

                        std::vector<MIRParam> argParams;
                        for (size_t i = 0; i < ft.argTypes.size(); i++) {
                            argParams.push_back(MIRLValue::newField(MIRLValue::newArgument(1), i));
                        }
                        builder.terminateCall(MIRLValue::newReturn(), te->path.clone(), mv$(argParams), 1, 2);
                        // BB1: Return
                        builder.ensureOpen();
                        builder.terminateBlock(MIRTerminator::make_Return({}));
                        // BB1: Diverge
                        builder.ensureOpen();
                        builder.mir.blocks.back().isCleanup = true;
                        builder.terminateBlock(MIRTerminator::make_UnwindResume({}));

                        transList.autoFunctions.push_back(box$(fcn));
                        e->ptr = transList.autoFunctions.back().get();
                    }
                }
            } else if (const auto* te = type->opt_Function()) {
                for (; offset < sizeof(entries) / sizeof(entries[0]); offset++) {
                    bool isByValue = (offset == 2);
                    const auto& ent = entries[offset];

                    auto fcnP = path.clone();
                    fcnP.data.as_UfcsKnown().item = ent.fcnName;
                    fcnP.data.as_UfcsKnown().trait.path = ent.traitPath->clone();

                    auto* e = transList.addFunction(crate.types, mv$(fcnP));
                    if (e) {
                        ::std::vector<HIRTypeRef> argTys;
                        for (const auto& ty : te->argTypes) {
                            argTys.push_back(ty);
                        }
                        auto argTy = crate.types.tuple(mv$(argTys));

                        HIRFunction fcn;
                        fcn.returnType = te->rettype;
                        fcn.args.push_back(std::make_pair(HIRPattern(), !isByValue ? crate.types.borrow(ent.bt, type) : type));
                        fcn.args.push_back(std::make_pair(HIRPattern(), mv$(argTy)));

                        fcn.code.mir = generatedBody();
                        Builder builder(state, *fcn.code.mir);

                        std::vector<MIRParam> argParams;
                        for (size_t i = 0; i < te->argTypes.size(); i++) {
                            argParams.push_back(MIRLValue::newField(MIRLValue::newArgument(1), i));
                        }
                        builder.terminateCall(MIRLValue::newReturn(), !isByValue ? MIRLValue::newDeref(MIRLValue::newArgument(0)) : MIRLValue::newArgument(0), mv$(argParams), 1, 2);
                        // BB1: Return
                        builder.ensureOpen();
                        builder.terminateBlock(MIRTerminator::make_Return({}));
                        // BB1: Diverge
                        builder.ensureOpen();
                        builder.mir.blocks.back().isCleanup = true;
                        builder.terminateBlock(MIRTerminator::make_UnwindResume({}));

                        transList.autoFunctions.push_back(box$(fcn));
                        e->ptr = transList.autoFunctions.back().get();
                    }
                }
            }
        }
        for (const auto& ent : transList.vtables) {
            Span sp;
            const auto& traitPath = ent.first.data.as_UfcsKnown().trait;
            const auto& type = ent.first.data.as_UfcsKnown().type;
            if (traitPath.path != HIRSimplePath()) {
                continue;
            }
            DEBUG("VTABLE <empty> for " << type);

            ::std::vector<HIRTypeRef> tupleTys;
            tupleTys.push_back(crate.types.primitive(HIRCoreType::Usize));
            tupleTys.push_back(crate.types.primitive(HIRCoreType::Usize));
            tupleTys.push_back(crate.types.primitive(HIRCoreType::Usize)); // fn
            auto vtableTy = crate.types.tuple(std::move(tupleTys));

            // Look up the size of the VTable, so we can allocate the right buffer size
            const auto* repr = TargetGetTypeRepr(sp, state.resolve, vtableTy);
            assert(repr);

            HIRLinkage linkage;
            linkage.type = HIRLinkage::Type::Weak;
            HIRStatic vtableStatic(::std::move(linkage), /*is_mut*/ false, mv$(vtableTy), {});
            auto& vtableData = vtableStatic.valueRes;
            const auto ptrBytes = TargetGetPointerBits() / 8;
            vtableData.bytes.resize(repr->size);
            size_t ofs = 0;
            auto pushPtr = [&vtableData, &ofs, ptrBytes](HIRPath p, bool preserveTrackCaller = false) {
                DEBUG("@" << ofs << " = " << p);
                assert(ofs + ptrBytes <= vtableData.bytes.size());
                vtableData.relocations.push_back(Reloc::newNamed(ofs, ptrBytes, mv$(p), preserveTrackCaller));
                vtableData.writeUint(ofs, ptrBytes, EncodedLiteral::PTR_BASE);
                ofs += ptrBytes;
                assert(ofs <= vtableData.bytes.size());
            };
            // Drop glue
            transList.dropGlue.insert(type);
            pushPtr(HIRPath(type, "#drop_glue"));
            // Size & align
            {
                size_t size, align;
                // NOTE: Uses the Size+Align version because that doesn't panic on unsized
                ASSERT_BUG(sp, TargetGetSizeAndAlignOf(sp, state.resolve, type, size, align), "Unexpected generic? " << type);
                vtableData.writeUint(ofs, ptrBytes, size);
                ofs += ptrBytes;
                vtableData.writeUint(ofs, ptrBytes, align);
                ofs += ptrBytes;
            }
            assert(ofs == vtableData.bytes.size());
            vtableStatic.valueGenerated = true;

            // Add to list
            transList.autoStatics.push_back(box$(vtableStatic));
            auto* e = transList.addStatic(crate.types, ent.first.clone());
            if (e) {
                e->ptr = transList.autoStatics.back().get();
            } else {
                transList.autoStatics.pop_back();
            }
        }
        for (const auto& ent : transList.vtables) {
            Span sp;
            const auto& traitPath = ent.first.data.as_UfcsKnown().trait;
            const auto& type = ent.first.data.as_UfcsKnown().type;
            if (traitPath.path == HIRSimplePath()) {
                continue;
            }
            DEBUG("VTABLE " << traitPath << " for " << type);
            // TODO: What's the use of `ent.second` here? (it's a `Trans_Params`)

            // Get the vtable type
            const auto& trait = crate.getTraitByPath(sp, traitPath.path);
            const auto& vtableSp = trait.vtablePath;
            ASSERT_BUG(sp, vtableSp != HIRSimplePath(), "Trait " << traitPath.path << " doesn't have a vtable");
            auto vtableParams = traitPath.params.clone();
            for (const auto& ty : trait.typeIndexes) {
                auto aty = crate.types.path(HIRPath(type, traitPath.clone(), ty.first), {});
                state.resolve.expandAssociatedTypes(sp, aty);
                vtableParams.types.push_back(mv$(aty));
            }
            const auto& vtableRef = crate.getStructByPath(sp, vtableSp);
            auto vtableTy = crate.types.path(HIRGenericPath(mv$(vtableSp), mv$(vtableParams)), &vtableRef);

            // Ensure that the type is defined/populated
            transList.addType(vtableTy, false);

            // Look up the size of the VTable, so we can allocate the right buffer size
            const auto* repr = TargetGetTypeRepr(sp, state.resolve, vtableTy);
            assert(repr);

            // Create vtable contents
            auto monomorphCbTrait = MonomorphStatePtr(crate.types, type, &traitPath.params, nullptr);

            HIRLinkage linkage;
            linkage.type = HIRLinkage::Type::Weak;
            HIRStatic vtableStatic(::std::move(linkage), /*is_mut*/ false, mv$(vtableTy), {});
            auto& vtableData = vtableStatic.valueRes;
            const auto ptrBytes = TargetGetPointerBits() / 8;
            vtableData.bytes.resize(repr->size);
            size_t ofs = 0;
            auto pushPtr = [&vtableData, &ofs, ptrBytes](HIRPath p, bool preserveTrackCaller = false) {
                DEBUG("@" << ofs << " = " << p);
                assert(ofs + ptrBytes <= vtableData.bytes.size());
                vtableData.relocations.push_back(Reloc::newNamed(ofs, ptrBytes, mv$(p), preserveTrackCaller));
                vtableData.writeUint(ofs, ptrBytes, EncodedLiteral::PTR_BASE);
                ofs += ptrBytes;
                assert(ofs <= vtableData.bytes.size());
            };
            // Drop glue
            transList.dropGlue.insert(type);
            pushPtr(HIRPath(type, "#drop_glue"));
            // Size & align
            {
                size_t size, align;
                // NOTE: Uses the Size+Align version because that doesn't panic on unsized
                ASSERT_BUG(sp, TargetGetSizeAndAlignOf(sp, state.resolve, type, size, align), "Unexpected generic? " << type);
                vtableData.writeUint(ofs, ptrBytes, size);
                ofs += ptrBytes;
                vtableData.writeUint(ofs, ptrBytes, align);
                ofs += ptrBytes;
            }

            // Methods
            // - The `m_value_indexes` list isn't sorted (well, it's sorted differently) so we need an `O(n^2)` search

            for (unsigned int i = 0; i < trait.valueIndexes.size(); i++) {
                // Find the corresponding vtable entry
                for (const auto& m : trait.valueIndexes) {
                    // NOTE: The "3" is the number of non-method vtable entries
                    if (m.second.first != 3 + i) {
                        continue;
                    }

                    DEBUG("- " << m.second.first << " = " << m.second.second << " :: " << m.first);

                    auto traitGpath = monomorphCbTrait.monomorphGenericpath(sp, m.second.second, false);
                    auto itemPath = HIRPath(type, mv$(traitGpath), m.first);
                    state.resolve.expandAssociatedTypesPath(sp, itemPath);

                    auto srcTraitMs = MonomorphStatePtr(crate.types, type, &itemPath.data.as_UfcsKnown().trait.params, nullptr);
                    const auto& srcTrait = state.resolve.hirCrate().getTraitByPath(sp, m.second.second.path);
                    const auto& item = srcTrait.values.at(m.first);
                    bool preserveTrackCaller = false;
                    // If the entry is a by-value function, then emit a reference to a shim
                    if (item.is_Function()) {
                        const auto& tplFcn = item.as_Function();
                        preserveTrackCaller = tplFcn.markings.trackCaller;
                        if (tplFcn.receiver == HIRFunction::Receiver::Value) {
                            auto callPath = itemPath.clone();
                            itemPath.data.as_UfcsKnown().item = RcString::newInterned(FMT(m.first << "#ptr"));
                            auto* e = transList.addFunction(crate.types, itemPath.clone());
                            if (e) {
                                // Create the shim (forward to the true call, dereferencing the first argument)
                                HIRFunction newFcn;
                                newFcn.markings.trackCaller = preserveTrackCaller;
                                newFcn.returnType = srcTraitMs.monomorphType(sp, tplFcn.returnType);
                                state.resolve.expandAssociatedTypes(sp, newFcn.returnType);
                                newFcn.args.push_back(std::make_pair(HIRPattern(), crate.types.borrow(HIRBorrowType::Owned, type)));
                                for (size_t i = 1; i < tplFcn.args.size(); i++) {
                                    newFcn.args.push_back(std::make_pair(HIRPattern(), srcTraitMs.monomorphType(sp, tplFcn.args[i].second)));
                                }
                                for (size_t i = 0; i < newFcn.args.size(); i++) {
                                    state.resolve.expandAssociatedTypes(sp, newFcn.args[i].second);
                                }

                                DEBUG("> Generate shim: " << itemPath);

                                newFcn.code.mir = generatedBody();
                                auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) { os << itemPath; });
                                MIRTypeResolve localMirRes{sp, state.resolve, pathCallback, newFcn.returnType, newFcn.args, *newFcn.code.mir};
                                Builder builder(state, *newFcn.code.mir);
                                // bb0:
                                //   rv = CALL ...
                                ::std::vector<MIRParam> callArgs;
                                callArgs.push_back(MIRLValue::newDeref(MIRLValue::newArgument(0)));
                                for (size_t i = 1; i < tplFcn.args.size(); i++) {
                                    callArgs.push_back(MIRLValue::newArgument(i));
                                }
                                builder.terminateCall(MIRLValue::newReturn(), mv$(callPath), std::move(callArgs), 1, 2, preserveTrackCaller);
                                // bb1:
                                //   RETURN
                                builder.ensureOpen();
                                builder.terminateBlock(MIRTerminator::make_Return({}));
                                // bb2:
                                //   UNWIND
                                builder.ensureOpen();
                                builder.mir.blocks.back().isCleanup = true;
                                builder.terminateBlock(MIRTerminator::make_UnwindResume({}));
                                // ---

                                transList.autoFunctions.push_back(box$(newFcn));
                                e->ptr = transList.autoFunctions.back().get();
                            }
                        }
                    }
                    //MIR_ASSERT(*m_mir_res, tr.m_values.at(m.first).is_Function(), "TODO: Handle generating vtables with non-function items");
                    pushPtr(mv$(itemPath), preserveTrackCaller);
                }
            }
            // Parent trait vtables
            for (size_t i = 0; i < trait.allParentTraits.size(); i++) {
                const auto& pt = trait.allParentTraits[i];
                const auto& fld = repr->fields.at(trait.vtableParentTraitsStart + i);
                ASSERT_BUG(sp, fld.offset == ofs, "");
                if (!fld.ty->is_Tuple()) {
                    auto ptMono = MonomorphStatePtr(crate.types, type, &traitPath.params, nullptr).monomorphGenericpath(sp, pt.path);
                    auto ptVtablePath = HIRPath(type, mv$(ptMono), ent.first.data.as_UfcsKnown().item);
                    state.resolve.expandAssociatedTypesPath(sp, ptVtablePath);
                    pushPtr(mv$(ptVtablePath));
                }
            }
            assert(ofs == vtableData.bytes.size());
            vtableStatic.valueGenerated = true;

            // Add to list
            transList.autoStatics.push_back(box$(vtableStatic));
            auto* e = transList.addStatic(crate.types, ent.first.clone());
            if (e) {
                e->ptr = transList.autoStatics.back().get();
            } else {
                transList.autoStatics.pop_back();
            }
        }
        transList.vtables.clear();
    }

    // Create drop glue implementations
    {
        TRACE_FUNCTION_F("Drop Glue");
        for (const auto& ty : transList.types) {
            Span sp;
            if (ty.second) {
                continue;
            }
            if (!state.resolve.typeNeedsDropGlue(sp, ty.first)) {
                continue;
            }

            if (ty.first->is_TraitObject()) {
                continue;
            }
            if (ty.first->is_Slice()) {
                continue;
            }
            transList.dropGlue.insert(ty.first);
        }

        for (const auto& ty : transList.dropGlue) {
            Span sp;
            auto path = HIRPath(ty, "#drop_glue");

            HIRFunction fcn;
            fcn.returnType = crate.types.unit();
            fcn.args.push_back(std::make_pair(HIRPattern(), crate.types.borrow(HIRBorrowType::Owned, ty)));

            fcn.code.mir = generatedBody();
            auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) { os << path; });
            MIRTypeResolve localMirRes{sp, state.resolve, pathCallback, fcn.returnType, fcn.args, *fcn.code.mir};
            Builder builder(state, *fcn.code.mir);
            builder.pushStmtAssign(MIRLValue::newReturn(), MIRRValue::make_Tuple({}));
            auto ownedBoxPointeeDrop = static_cast<MIRBasicBlockId>(~0u);
            auto ownedBoxDropCall = static_cast<MIRBasicBlockId>(~0u);
            if (const auto* ity = state.resolve.isTypeOwnedBox(ty)) {
                // Call inner destructors
                auto innerVal = derefBox(MIRLValue::newDeref(builder.self.clone()));
                HIRTypeRef tmp;
                ASSERT_BUG(sp, localMirRes.getLvalueType(tmp, innerVal) == ity, "Hard-coded box pointer path didn't result in the inner type");
                ownedBoxPointeeDrop = builder.pushStmtDrop(std::move(innerVal));
            }

            if (state.resolve.typeNeedsDropGlue(sp, ty)) {
                switch ((*ty).tag()) {
                    case HIRTypeData::TAG_Infer: {
                        UNREACHABLE();
                    }
                    case HIRTypeData::TAG_Generic: {
                        UNREACHABLE();
                    }
                    case HIRTypeData::TAG_ErasedType: {
                        UNREACHABLE();
                    }
                    case HIRTypeData::TAG_TraitObject: {
                        TODO(sp, "Drop glue for TraitObject? " << ty);
                        break;
                    }
                    case HIRTypeData::TAG_Slice: {
                        TODO(sp, "Drop glue for Slice? " << ty);
                        break;
                    }
                    case HIRTypeData::TAG_NodeType: {
                        TODO(sp, "Drop glue for NodeType? " << ty);
                        break;
                    }
                    case HIRTypeData::TAG_Diverge: {
                        // Exists for reasons...
                        builder.terminateBlock(MIRTerminator::make_Unreachable({}));
                        break;
                    }
                    case HIRTypeData::TAG_Primitive: {
                        // Nothing to do
                        break;
                    }
                    case HIRTypeData::TAG_Pattern: {
                        // Pattern types are restricted scalars and have no
                        // independent drop glue beyond their base type.
                        break;
                    }
                    case HIRTypeData::TAG_NamedFunction: {
                        // Nothing to do
                        break;
                    }
                    case HIRTypeData::TAG_Function: {
                        // Nothing to do
                        break;
                    }
                    case HIRTypeData::TAG_Pointer: {
                        // Nothing to do
                        break;
                    }
                    case HIRTypeData::TAG_Borrow: {
                        auto& te = (*ty).as_Borrow();
                        if (te.type == HIRBorrowType::Owned) {
                            // `drop a0**`
                            builder.pushStmtDrop(MIRLValue::newDeref(MIRLValue::newDeref(builder.self.clone())));
                        }
                        break;
                    }
                    case HIRTypeData::TAG_Tuple: {
                        auto& te = (*ty).as_Tuple();
                        auto self = MIRLValue::newDeref(builder.self.clone());
                        auto fldLv = MIRLValue::newField(mv$(self), 0);
                        ::std::vector<MIRLValue> fields;
                        for (size_t i = 0; i < te.size(); i++) {
                            if (state.resolve.typeNeedsDropGlue(sp, te[i])) {
                                fields.push_back(fldLv.clone());
                            }
                            fldLv.incField();
                        }
                        builder.pushDropSequence(mv$(fields));
                        break;
                    }
                    case HIRTypeData::TAG_Array: {
                        auto& te = (*ty).as_Array();
                        auto size = te.size.as_Known();
                        auto self = MIRLValue::newDeref(builder.self.clone());
                        if (size > 0 && state.resolve.typeNeedsDropGlue(sp, te.inner)) {
                            // The C++ backend expands structural array drops
                            // itself, including the unwind cleanup of the tail.
                            // Keeping a second hand-built MIR loop here would
                            // duplicate that logic and lose the tail on panic.
                            builder.pushStmtDrop(mv$(self));
                        }
                        break;
                    }
                    case HIRTypeData::TAG_Path: {
                        auto& te = (*ty).as_Path();
                        bool hasDrop = false;
                        switch (te.binding.tag()) {
                            case HIRTypePathBinding::TAG_Unbound: {
                                UNREACHABLE();
                            }
                            case HIRTypePathBinding::TAG_Opaque: {
                                UNREACHABLE();
                            }
                            case HIRTypePathBinding::TAG_ExternType: {
                                // Why is this trying to be dropped?
                                break;
                            }
                            case HIRTypePathBinding::TAG_Struct: {
                                auto& pbe = te.binding.as_Struct();
                                auto customDropCall = static_cast<MIRBasicBlockId>(~0u);
                                if (pbe->markings.hasDropImpl) {
                                    customDropCall = builder.pushCallDrop(ty);
                                    if (ownedBoxPointeeDrop != ~0u) {
                                        ownedBoxDropCall = customDropCall;
                                    }
                                    hasDrop = true;
                                }

                                if (ty->is_Path() && (ty->as_Path().isGenerator() || ty->as_Path().isFuture())) {
                                    ASSERT_BUG(sp, hasDrop, "");
                                    // Coroutines use a custom Drop impl that handles dropping values
                                } else {
                                    // NOTE: Lazy option of monomorphising and handling the two classes
                                    const auto* repr = TargetGetTypeRepr(sp, state.resolve, ty);
                                    ASSERT_BUG(sp, repr, "No repr for struct " << ty);

                                    auto self = MIRLValue::newDeref(builder.self.clone());
                                    auto fldLv = MIRLValue::newField(mv$(self), 0);
                                    ::std::vector<MIRLValue> fields;
                                    for (size_t i = 0; i < repr->fields.size(); i++) {
                                        if (state.resolve.typeNeedsDropGlue(sp, repr->fields[i].ty)) {
                                            fields.push_back(fldLv.clone());
                                        }
                                        fldLv.incField();
                                    }
                                    builder.pushDropSequence(mv$(fields), customDropCall);
                                }
                                break;
                            }
                            case HIRTypePathBinding::TAG_Union: {
                                auto& pbe = te.binding.as_Union();
                                if (pbe->markings.hasDropImpl) {
                                    builder.pushCallDrop(ty);
                                    hasDrop = true;
                                }
                                // Union requires no internal drop glue
                                break;
                            }
                            case HIRTypePathBinding::TAG_Enum: {
                                auto& pbe = te.binding.as_Enum();
                                auto customDropCall = static_cast<MIRBasicBlockId>(~0u);
                                        if (pbe->markings.hasDropImpl) {
                                            customDropCall = builder.pushCallDrop(ty);
                                            hasDrop = true;
                                        }
                                        const HIREnum& enm = *pbe;
                                switch (enm.data.tag()) {
                                    case HIREnumClass::TAG_Value: {
                                        builder.terminateBlock(MIRTerminator::make_Return({}));
                                        break;
                                    }
                                    case HIREnumClass::TAG_Data: {
                                        auto& variants = enm.data.as_Data();
                                        auto self = MIRLValue::newDeref(builder.self.clone());
                                        MIRTerminator::Data_Switch sw;
                                        sw.val = self.clone();
                                        const auto switchBlock = builder.mir.blocks.size() - 1;
                                        builder.terminateBlock(MIRTerminator::make_Switch(mv$(sw)));

                                        ::std::vector<MIRBasicBlockId> targets;
                                        targets.reserve(variants.size());
                                        auto fldLv = MIRLValue::newDowncast(mv$(self), 0);
                                        for (size_t idx = 0; idx < variants.size(); idx++) {
                                            builder.ensureOpen();
                                            targets.push_back(builder.mir.blocks.size() - 1);
                                            // TODO: Monomorphise and check
                                            //if( state.resolve.type_needs_drop_glue(sp, repr->fields[i].ty) )
                                            {
                                                builder.pushStmtDrop(fldLv.clone());
                                            }
                                            fldLv.incDowncast();
                                            builder.ensureOpen();
                                            builder.terminateBlock(MIRTerminator::make_Return({}));
                                        }
                                        builder.mir.blocks[switchBlock].terminator.as_Switch().targets = mv$(targets);

                                        if (customDropCall != ~0u) {
                                            // If the user's Drop implementation
                                            // panics, select the active variant
                                            // again and destroy its payload on a
                                            // cleanup edge.
                                            const auto cleanupSwitch = static_cast<MIRBasicBlockId>(builder.mir.blocks.size());
                                            MIRBasicBlock switchCleanupBlock;
                                            switchCleanupBlock.isCleanup = true;
                                            MIRTerminator::Data_Switch cleanupSwitchData;
                                            cleanupSwitchData.val = MIRLValue::newDeref(builder.self.clone());
                                            switchCleanupBlock.terminator = MIRTerminator::make_Switch(mv$(cleanupSwitchData));
                                            builder.mir.blocks.push_back(mv$(switchCleanupBlock));

                                            const auto resume = static_cast<MIRBasicBlockId>(builder.mir.blocks.size() + variants.size());
                                            ::std::vector<MIRBasicBlockId> cleanupTargets;
                                            cleanupTargets.reserve(variants.size());
                                            auto cleanupField = MIRLValue::newDowncast(MIRLValue::newDeref(builder.self.clone()), 0);
                                            for (size_t idx = 0; idx < variants.size(); idx++) {
                                                cleanupTargets.push_back(builder.mir.blocks.size());
                                                MIRBasicBlock block;
                                                block.isCleanup = true;
                                                block.terminator = MIRTerminator::make_Drop({
                                                    MIRDropKind::DEEP,
                                                    cleanupField.clone(),
                                                    ~0u,
                                                    resume,
                                                    MIRUnwindAction::make_Terminate({}),
                                                });
                                                builder.mir.blocks.push_back(mv$(block));
                                                cleanupField.incDowncast();
                                            }
                                            MIRBasicBlock resumeBlock;
                                            resumeBlock.isCleanup = true;
                                            resumeBlock.terminator = MIRTerminator::make_UnwindResume({});
                                            builder.mir.blocks.push_back(mv$(resumeBlock));

                                            builder.mir.blocks[cleanupSwitch].terminator.as_Switch().targets = mv$(cleanupTargets);
                                            builder.mir.blocks[customDropCall].terminator.as_Call().unwind = MIRUnwindAction::make_Cleanup(cleanupSwitch);
                                        }
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                        if( hasDrop ) {
                                if (auto* e = transList.addFunction(crate.types, HIRPath(ty, state.resolve.langDrop(), "drop"))) {
                                    MonomorphState params(crate.types);
                                    auto p = HIRPath(ty, state.resolve.langDrop(), "drop");
                                    const HIRGenericParams* implParamsDef = nullptr;
                                    auto fcnE = state.resolve.getValue(sp, p, /*out*/ params, /*signature_only=*/false, &implParamsDef);
                                    ASSERT_BUG(sp, fcnE.is_Function(), "Drop didn't point to a function! " << fcnE.tagStr() << " " << p);
                                    e->ptr = fcnE.as_Function();
                                    e->pp.selfType = params.getSelfType();
                                    e->pp.gdefImpl = implParamsDef;
                                    if (const auto* implParams = params.getImplParams()) {
                                        e->pp.ppImpl = implParams->clone();
                                    }
                                    if (const auto* methodParams = params.getMethodParams()) {
                                        e->pp.ppMethod = methodParams->clone();
                                    }
                                }
                        }
                        break;
                    }
                }
            }

            if (ownedBoxPointeeDrop != ~0u) {
                ASSERT_BUG(sp, ownedBoxDropCall != ~0u, "Owned Box did not have a Drop implementation: " << ty);

                // The pointee is destroyed before Box::drop deallocates its
                // storage.  If the pointee panics, run Box::drop and the
                // remaining fields on the cleanup path before resuming the
                // original exception.
                if (builder.mir.blocks.back().terminator.is_Incomplete()) {
                    builder.terminateBlock(MIRTerminator::make_Return({}));
                }

                MIRBasicBlockId afterCleanupCall;
                if (const auto* fieldCleanup = builder.mir.blocks[ownedBoxDropCall].terminator.as_Call().unwind.opt_Cleanup()) {
                    afterCleanupCall = *fieldCleanup;
                } else {
                    afterCleanupCall = static_cast<MIRBasicBlockId>(builder.mir.blocks.size() + 1);
                }

                auto cleanupBorrow = builder.addLocal(state.crate.types.borrow(HIRBorrowType::Unique, ty));
                const auto cleanupCall = static_cast<MIRBasicBlockId>(builder.mir.blocks.size());
                MIRBasicBlock cleanupCallBlock;
                cleanupCallBlock.isCleanup = true;
                cleanupCallBlock.statements.push_back(
                    MIRStatement::make_Assign({
                        cleanupBorrow.clone(),
                        MIRRValue::make_Borrow({HIRBorrowType::Unique, false, MIRLValue::newDeref(builder.self.clone())}),
                    })
                );
                cleanupCallBlock.terminator = MIRTerminator::make_Call({
                    afterCleanupCall,
                    MIRUnwindAction::make_Terminate({}),
                    MIRLValue::newReturn(),
                    HIRPath(ty, state.resolve.langDrop(), "drop"),
                    makeVec1<MIRParam>(mv$(cleanupBorrow)),
                });
                builder.mir.blocks.push_back(mv$(cleanupCallBlock));

                if (afterCleanupCall == builder.mir.blocks.size()) {
                    MIRBasicBlock resumeBlock;
                    resumeBlock.isCleanup = true;
                    resumeBlock.terminator = MIRTerminator::make_UnwindResume({});
                    builder.mir.blocks.push_back(mv$(resumeBlock));
                }
                builder.mir.blocks[ownedBoxPointeeDrop].terminator.as_Drop().unwind = MIRUnwindAction::make_Cleanup(cleanupCall);
            }
            if (builder.mir.blocks.back().terminator.is_Incomplete()) {
                builder.terminateBlock(MIRTerminator::make_Return({}));
            }

            transList.autoFunctions.push_back(box$(fcn));
            auto* e = transList.addFunction(crate.types, mv$(path));
            if (e) {
                e->ptr = transList.autoFunctions.back().get();
            } else {
                transList.autoFunctions.pop_back();
            }
        }
        transList.dropGlue.clear();
    }
}

namespace {
    // Translation paths are assembled after inference and monomorphisation.
    // A nominal type can therefore arrive through an older, structurally
    // equivalent ASTType* whose path binding is still Unbound.  rustc's Ty
    // carries the ADT DefId as part of the nominal type and cannot represent
    // that state.  Restore the equivalent invariant at the translation
    // boundary instead of teaching codegen to accept missing metadata.
    class BindTranslationNominals final: public HIRVisitor {
        const HIRCrate& crate;

    public:
        explicit BindTranslationNominals(const HIRCrate& crate)
            : HIRVisitor(nullptr, crate.types)
            , crate(crate)
        {
        }

        [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override {
            auto data = ty->cloneData();
            visitTypeDataChildren(data);

            if (auto* pathTy = data.opt_Path()) {
                if (pathTy->binding.is_Unbound() && pathTy->path.data.is_Generic()) {
                    const auto& path = pathTy->path.data.as_Generic().path;
                    const auto& item = crate.getTypeitemByPath(Span(), path);
                    switch (item.tag()) {
default:
                        BUG(Span(), "Nominal translation type points to " << item.tagStr() << " - " << ty);
                        case HIRTypeItem::TAG_ExternType: {
                            auto& e = item.as_ExternType();
                            pathTy->binding = HIRTypePathBinding::make_ExternType(&e);
                            break;
                        }
                        case HIRTypeItem::TAG_Struct: {
                            auto& e = item.as_Struct();
                            pathTy->binding = HIRTypePathBinding::make_Struct(&e);
                            break;
                        }
                        case HIRTypeItem::TAG_Union: {
                            auto& e = item.as_Union();
                            pathTy->binding = HIRTypePathBinding::make_Union(&e);
                            break;
                        }
                        case HIRTypeItem::TAG_Enum: {
                            auto& e = item.as_Enum();
                            pathTy->binding = HIRTypePathBinding::make_Enum(&e);
                            break;
                        }
                    }
                }
            }

            return typeInterner().intern(mv$(data));
        }
    };

    void bindTranslationNominals(const HIRCrate& crate, HIRPath& path) {
        BindTranslationNominals visitor(crate);
        visitor.visitPath(path, HIRVisitor::PathContext::VALUE);
    }

    struct EnumState {
        const HIRCrate& crate;
        StaticTraitResolve resolve;
        TransList rv;
        const TransList* origList;

        // Queue of items to enumerate
        ::std::deque<TransListFunction*> fcnQueue;
        ::std::vector<TransListFunction*> fcnsToTypeVisit;

        ::std::set<std::string> emittedFunctions;
        ::std::set<HIRPath> activePaths;

        // Map of locally-defined exported `link_name` functions
        ::std::unordered_map<std::string, std::pair<HIRSimplePath, const HIRFunction*>> linkFunctions;

        EnumState(const WireBoard& wb)
            : crate(*wb.crate)
            , resolve(wb, OpaqueReveal::All)
            , rv(wb)
            , origList(nullptr)
        {
            enumerateLinkFunctions();
        }

        void enumFcn(HIRPath p, const HIRFunction& fcn, TransParams pp) {
            if (auto* e = rv.addFunction(crate.types, mv$(p))) {
                auto name = FMT(TransMangleValue(resolve.board(), *e->path));
                auto inserted = emittedFunctions.insert(name).second;
                ASSERT_BUG(Span(), inserted, "Duplicated mangled name - " << *e->path);
                fcnsToTypeVisit.push_back(e);
                e->ptr = &fcn;
                e->pp = mv$(pp);
                DEBUG(*e->path << " w/ " << e->pp.ppImpl << " and " << e->pp.ppMethod);
                fcnQueue.push_back(e);
            }
        }

    private:
        void enumerateLinkFunctions() {
            enumerateLinkFunctionsIn(crate.rootModule, HIRItemPath(crate.crateName));
            for (const auto& eCrate : crate.extCrates) {
                enumerateLinkFunctionsIn(eCrate.second.data->rootModule, HIRItemPath(eCrate.first));
            }
        }

        void enumerateLinkFunctionsIn(const HIRModule& mod, HIRItemPath modPath) {
            for (const auto& vi : mod.valueItems) {
                if (const auto* ip = vi.second->ent.opt_Function()) {
                    const auto& i = **ip;
                    if (i.code.mir && i.linkage.name != "") {
                        linkFunctions[i.linkage.name] = std::make_pair((modPath + vi.first).getSimplePath(), &i);
                    }
                }
            }

            for (const auto& ti : mod.modItems) {
                if (const auto* ip = ti.second->ent.opt_Module()) {
                    enumerateLinkFunctionsIn(*ip, modPath + ti.first);
                }
            }
        }
    };

}

TransList TransEnumerateCommonPost(EnumState& state);

namespace {
    void TransEnumerateExplicitLinkage(EnumState& state, const HIRModule& mod, HIRSimplePath modPath);
}

void TransEnumerateTypes(EnumState& state);
void TransEnumerateFillFromPath(EnumState& state, const HIRPath& path, const TransParams& pp);
void TransEnumerateFillFromPathMono(EnumState& state, HIRPath path);
void TransEnumerateFillFromFunction(EnumState& state, const HIRPath& path, const HIRFunction& function, const TransParams& pp);
void TransEnumerateFillFromStatic(EnumState& state, const HIRStatic& stat, TransListStatic& statOut, TransParams pp);
void TransEnumerateFillFromVTable(EnumState& state, HIRPath vtablePath, const TransParams& pp);
void TransEnumerateFillFromLiteral(EnumState& state, const EncodedLiteral& lit, const TransParams& pp);
void TransEnumerateFillFromMIR(MIREnumCache& state, const MIRFunction& code);

namespace {
    class GlobalAsmOperandEvaluator: public HIRVisitor {
        const WireBoard& wb;
        const HIRCrate& crate;
        const Span* span = nullptr;

    public:
        explicit GlobalAsmOperandEvaluator(const WireBoard& wb)
            : HIRVisitor(nullptr, wb.crate->types)
            , wb(wb)
            , crate(*wb.crate)
        {
        }

        void evaluate(HIRGlobalAssembly& item) {
            span = &item.span;
            visitGlobalAssembly(item);
            span = nullptr;
        }

        void visitConstgeneric(HIRConstGeneric& value) override {
            ConvertHIRConstantEvaluateConstGeneric(*span, wb, crate, value);
            ASSERT_BUG(*span, value.is_Evaluated(), "global_asm operand remained unevaluated at translation");
        }
    };

    void TransEnumerateGlobalAsm(EnumState& state, HIRModule& mod) {
        GlobalAsmOperandEvaluator evaluator{state.resolve.board()};
        for (auto& item : mod.globalAsm) {
            evaluator.evaluate(item);
            for (const auto& operand : item.operands) {
                if (const auto* path = operand.opt_Sym()) {
                    state.rv.roots.push_back(path->clone());
                    TransEnumerateFillFromPathMono(state, path->clone());
                }
            }
        }
        for (auto& named : mod.modItems) {
            if (auto* child = named.second->ent.opt_Module()) {
                TransEnumerateGlobalAsm(state, *child);
            }
        }
    }
}

void TransEnumerateGlobalAllocator(EnumState& state) {
    const auto allocatorIt = state.crate.langItems.find(GLOBAL_ALLOCATOR_LANG_ITEM);
    if (allocatorIt == state.crate.langItems.end()) {
        return;
    }

    const auto& allocatorPath = allocatorIt->second;
    const auto& allocator = state.crate.getStaticByPath(Span(), allocatorPath);

    HIRPath staticPath = HIRGenericPath(allocatorPath);
    state.rv.roots.push_back(staticPath.clone());
    TransEnumerateFillFromPathMono(state, std::move(staticPath));

    auto layoutCtor = TransAllocatorLayoutCtorPath(state.crate);
    state.rv.roots.push_back(layoutCtor.clone());
    TransEnumerateFillFromPathMono(state, std::move(layoutCtor));

    for (size_t i = 0; i < NUM_ALLOCATOR_METHODS; i++) {
        auto methodPath = TransAllocatorMethodPath(state.crate, allocator.type, ALLOCATOR_METHODS[i]);
        state.rv.roots.push_back(methodPath.clone());
        TransEnumerateFillFromPathMono(state, std::move(methodPath));
    }
}

namespace {
    void enumerateDestructorType(EnumState& state, HIRTypeRef type) {
        if (!state.resolve.typeNeedsDropGlue(Span(), type)) {
            return;
        }
        switch (type->tag()) {
            case HIRTypeData::TAG_Path:
                state.rv.dropGlue.insert(type);
                break;
            case HIRTypeData::TAG_Borrow: {
                const auto& borrow = type->as_Borrow();
                if (borrow.type == HIRBorrowType::Owned) {
                    enumerateDestructorType(state, borrow.inner);
                }
                break;
            }
            case HIRTypeData::TAG_Array:
                enumerateDestructorType(state, type->as_Array().inner);
                break;
            case HIRTypeData::TAG_Slice:
                enumerateDestructorType(state, type->as_Slice().inner);
                break;
            case HIRTypeData::TAG_Tuple:
                for (const auto* field : type->as_Tuple()) {
                    enumerateDestructorType(state, field);
                }
                break;
            case HIRTypeData::TAG_Pattern:
                enumerateDestructorType(state, type->as_Pattern().inner);
                break;
            default:
                break;
        }
    }
}

struct MIREnumCache {
    stl::Vector<const HIRPath*> paths;
    stl::Vector<const HIRTypeData*> typeids;
    stl::Vector<const HIRTypeData*> destructorTypes;

    MIREnumCache() {
    }

    void insertPath(const HIRPath& newPath) {
        for (const auto* p : this->paths) {
            if (*p == newPath) {
                return;
            }
        }
        this->paths.pushBack(&newPath);
    }

    void insertTypeid(const HIRTypeData* newTy) {
        for (const auto* p : this->typeids) {
            if (p == newTy) {
                return;
            }
        }
        this->typeids.pushBack(newTy);
    }

    void insertDestructorType(const HIRTypeData* newTy) {
        for (const auto* p : this->destructorTypes) {
            if (p == newTy) {
                return;
            }
        }
        this->destructorTypes.pushBack(newTy);
    }

    void apply(EnumState& state, const TransParams& pp) const {
        TRACE_FUNCTION_F(" w/ impl=" << pp.ppImpl << " method=" << pp.ppMethod);
        for (const auto* tyP : this->typeids) {
            DEBUG("TypeID " << tyP);
            state.rv.typeids.insert(pp.monomorph(state.resolve, tyP));
        }
        for (const auto* tyP : this->destructorTypes) {
            enumerateDestructorType(state, pp.monomorph(state.resolve, tyP));
        }
        for (const auto& path : this->paths) {
            DEBUG("Path " << *path);
            TransEnumerateFillFromPath(state, *path, pp);
        }
    }
};

MIREnumCachePtr::~MIREnumCachePtr() {
    delete this->p;
    this->p = nullptr;
}

/// Enumerate trans items starting from `::main` (binary crate)
TransList TransEnumerateMain(const WireBoard& wb, HIRCrate& crate) {
    Span sp;

    EnumState state{wb};

    if (!crate.noMain) {
        auto cStartPath = crate.getLangItemPathOpt("trustme-start");
        if (cStartPath == HIRSimplePath()) {
            // user entrypoint
            auto mainPath = crate.getLangItemPath(Span(), "trustme-main");
            const auto& mainFcn = crate.getFunctionByPath(sp, mainPath);

            state.rv.roots.push_back(mainPath);
            state.enumFcn(mainPath, mainFcn, TransParams(crate.types));

            // "start" language item
            // - Takes main, and argc/argv as arguments
            const auto& startPath = crate.getLangItemPathOpt("start");
            if (startPath != HIRSimplePath()) {
                const auto& fcn = crate.getFunctionByPath(sp, startPath);

                TransParams langStartPp(crate.types);
                langStartPp.ppMethod.types.push_back(mainFcn.returnType);
                HIRPath p = HIRGenericPath(startPath, langStartPp.ppMethod.clone());
                state.rv.roots.push_back(p.clone());
                state.enumFcn(std::move(p), fcn, mv$(langStartPp));
            } else if (!crate.isNoCore) {
                // Preserve the usual diagnostic for crates that rely on the
                // standard entrypoint protocol.
                crate.getLangItemPath(sp, "start");
            }
        } else {
            const auto& fcn = crate.getFunctionByPath(sp, cStartPath);

            state.rv.roots.push_back(cStartPath);
            state.enumFcn(cStartPath, fcn, TransParams(crate.types));
        }
    }

    TransEnumerateExplicitLinkage(state, crate.rootModule, HIRSimplePath(crate.crateName, {}));
    TransEnumerateGlobalAllocator(state);
    TransEnumerateGlobalAsm(state, crate.rootModule);

    return TransEnumerateCommonPost(state);
}

namespace {
    void TransEnumerateGenericFunctionItems(EnumState& state, const Span& sp, const HIRFunction& e, MonomorphStatePtr ms, bool hasConditionalBounds) {
        if (e.code.mir) {
            const auto& mirFcn = *e.code.mir;
            auto params = HIRPathParams();
            ms.ppMethod = &params;
            if (!mirFcn.transEnumState) {
                auto* esp = new MIREnumCache();
                TransEnumerateFillFromMIR(*esp, *e.code.mir);
                mirFcn.transEnumState = MIREnumCachePtr(esp);
            }

            for (const auto& path : mirFcn.transEnumState->paths) {
                if (!monomorphisePathNeeded(*path)) {
                    DEBUG("Path " << *path);
                    MonomorphState unusedMs(state.crate.types);
                    auto v = state.resolve.getValue(sp, *path, unusedMs, true);
                    bool deferBoundPath = hasConditionalBounds && v.is_NotYetKnown();
                    if (hasConditionalBounds && !deferBoundPath && v.is_Function() && path->data.is_UfcsKnown() && !path->data.as_UfcsKnown().type->is_TraitObject()) {
                        // Signature-only lookup can return the trait method even
                        // when no concrete impl is available.  Resolve just
                        // these UFCS paths fully before treating them as roots.
                        MonomorphState implMs(state.crate.types);
                        deferBoundPath = state.resolve.getValue(sp, *path, implMs, false).is_NotYetKnown();
                    }
                    if (v.is_StructConstructor() || v.is_EnumConstructor()) {
                    } else if (deferBoundPath) {
                        // A path can be concrete while its availability still
                        // depends on this generic function's bounds.  It must
                        // be resolved when the saved MIR is instantiated, not
                        // emitted as an unconditional item of this crate.
                        DEBUG("Defer conditionally available path " << *path);
                    } else {
                        auto p = ms.monomorphPath(sp, *path);
                        state.rv.roots.push_back(p.clone());
                        TransEnumerateFillFromPathMono(state, std::move(p));
                    }
                } else {
                    DEBUG("Path " << *path << " - Generic");
                }
            }
        }
    }

    struct TransPathCallback {
        virtual HIRSimplePath get() = 0;
    };

    template <typename F>
    struct TransPathCb final: TransPathCallback {
        F f;

        explicit TransPathCb(F f)
            : f(f)
        {
        }

        HIRSimplePath get() override {
            return f();
        }
    };

    void TransEnumerateValItem(EnumState& state, const HIRValueItem& vi, bool isVisible, TransPathCallback& getPath) {
        TRACE_FUNCTION_F(getPath.get() << " : " << vi.tagStr() << " is_visible=" << isVisible);
        const Span sp;
        switch (vi.tag()) {
                break;
                case HIRValueItem::TAG_Import: {
                    auto& e = vi.as_Import();
                    // TODO: If visible, ensure that target is visited.
                    if (isVisible) {
                        if (!e.isVariant && e.path.crateName() == state.crate.crateName) {
                            const auto& vi2 = state.crate.getValitemByPath(sp, e.path, false);
                            auto callback = makeCallable<TransPathCb>([&]() {
                                return e.path;
                            });
                            TransEnumerateValItem(state, vi2, isVisible, callback);
                        }
                    }

                }
                break;
                break;
                case HIRValueItem::TAG_StructConstant: {

                }
                break;
                break;
                case HIRValueItem::TAG_StructConstructor: {

                }
                break;
                break;
                case HIRValueItem::TAG_Constant: {
                    const auto& e = *vi.as_Constant();
                    if (isVisible) {
                        // Visible constants need their relocations added as roots
                        // - Can't add this logic to `Trans_Enumerate_FillFrom_Literal` as it's used by non-public enumeration
                        for (const auto& r : e.valueRes.relocations) {
                            if (r.p) {
                                state.rv.roots.push_back(r.p->clone());
                            }
                        }
                        TransEnumerateFillFromLiteral(state, e.valueRes, TransParams(state.crate.types));
                    }

                }
                break;
                break;
                case HIRValueItem::TAG_Static: {
                    const auto& e = *vi.as_Static();
                    if (e.linkage.name != "" || e.linkage.section != "") {
                        // If a link name is set, force emit
                        isVisible = true;
                    }
                    if (e.isPromoted && !e.params.isGeneric()) {
                        // Storage this compiler made for a promoted borrow is
                        // named after the crate that made it. A generic body
                        // another crate monomorphises reads it by that name, so
                        // this crate defines it whether or not anything here
                        // does.
                        isVisible = true;
                    }
                    if (isVisible && !e.params.isGeneric()) {
                        // HACK: Refuse to emit unused generated statics
                        // - Needed because all items are visited (regardless of
                        // visibility)
                        if (e.type->is_Infer()) {
                            break;
                        }
                        auto* ptr = state.rv.addStatic(state.crate.types, getPath.get());
                        if (ptr) {
                            TransEnumerateFillFromStatic(state, e, *ptr, TransParams(state.crate.types));
                        }

                        state.rv.roots.push_back(getPath.get());
                    }

                }
                break;
                break;
                case HIRValueItem::TAG_Function: {
                    const auto& e = *vi.as_Function();
                    bool isInline = false;
                    if (isVisible) {
                        switch (e.markings.inlineType) {
                            case HIRFunction::Markings::Inline::Always:
                            case HIRFunction::Markings::Inline::Normal:
                                // Don't emit, it's going to be emitted by callers
                                DEBUG("Don't emit inlined function");
                                isInline = true;
                                break;
                            case HIRFunction::Markings::Inline::Auto:
                            case HIRFunction::Markings::Inline::Never:
                                // Should still be emitted, as it won't be emitted downstream
                                break;
                        }
                    }
                    if (e.linkage.name != "" || e.linkage.section != "") {
                        // If a link name is set, force emit
                        isVisible = true;
                    }

                    if (e.params.isGeneric() || (isInline && isVisible)) {
                        const_cast<HIRFunction&>(e).saveCode = true;
                    } else {
                        if (isVisible) {
                            TransParams pp(state.crate.types);
                            pp.ppMethod = HIRPathParams();
                            state.enumFcn(getPath.get(), e, mv$(pp));

                            state.rv.roots.push_back(getPath.get());
                        }
                    }
                    // Enumerate concrete items used
                    // - These are functions that have to be emitted, even if they're not public themselves
                    if (e.saveCode) {
                        TransEnumerateGenericFunctionItems(state, sp, e, MonomorphStatePtr(state.crate.types), !e.params.bounds.empty());
                    }

                }
                break;
        }
    }

    void TransEnumerateExplicitLinkage(EnumState& state, const HIRModule& mod, HIRSimplePath modPath) {
        for (const auto& vi : mod.valueItems) {
            bool hasExplicitLinkage = false;
            if (const auto* function = vi.second->ent.opt_Function()) {
                hasExplicitLinkage = (*function)->linkage.name != "" || (*function)->linkage.section != "";
            } else if (const auto* stat = vi.second->ent.opt_Static()) {
                hasExplicitLinkage = (*stat)->linkage.name != "" || (*stat)->linkage.section != "";
            }
            if (hasExplicitLinkage) {
                auto path = modPath + vi.first;
                auto callback = makeCallable<TransPathCb>([path]() {
                    return path;
                });
                TransEnumerateValItem(state, vi.second->ent, false, callback);
            }
        }

        for (const auto& ti : mod.modItems) {
            if (const auto* child = ti.second->ent.opt_Module()) {
                TransEnumerateExplicitLinkage(state, *child, modPath + ti.first);
            }
        }
    }

    void TransEnumeratePublicMod(EnumState& state, HIRModule& mod, HIRSimplePath modPath, bool isVisible) {
        TRACE_FUNCTION_F(modPath);
        for (auto& vi : mod.valueItems) {
            bool emit = isVisible && vi.second->publicity.isGlobal();
            auto p = modPath + vi.first;
            if (::std::any_of(state.crate.langItems.begin(), state.crate.langItems.end(), [&](const auto& e) {
                return e.second == p;
            })) {
                emit = true;
            }
            auto callback = makeCallable<TransPathCb>([&]() {
                return p;
            });
            TransEnumerateValItem(state, vi.second->ent, emit, callback);
        }

        for (auto& ti : mod.modItems) {
            if (auto* e = ti.second->ent.opt_Module()) {
                TransEnumeratePublicMod(state, *e, modPath + ti.first, ti.second->publicity.isGlobal());
            } else if (const HIRTrait* e = ti.second->ent.opt_Trait()) {
                auto params = HIRPathParams();
                MonomorphStatePtr ms(state.crate.types);
                ms.ppImpl = &params;
                for (const auto& vi : e->values) {
                    if (const auto* fcn = vi.second.opt_Function()) {
                        TransEnumerateGenericFunctionItems(state, Span(), *fcn, ms, !e->params.bounds.empty() || !fcn->params.bounds.empty());
                    }
                }
            }
        }
    }

    void TransEnumeratePublicTraitImpl(EnumState& state, StaticTraitResolve& resolve, const HIRSimplePath& traitPath, /*const*/ HIRTraitImpl& impl) {
        Span sp;
        const auto& implTy = impl.type;
        TRACE_FUNCTION_F("Impl" << impl.params.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << implTy);

        auto paramsImpl = HIRPathParams();
        MonomorphStatePtr ms(state.crate.types);
        ms.ppImpl = &paramsImpl;
        if (!impl.params.isGeneric()) {
            // Erased lifetimes and concrete `where` clauses leave an impl with
            // no translation parameters, but do not make the impl
            // unconditionally available.  Ask the normal trait resolver to
            // prove this exact impl before eagerly emitting its public items.
            bool implAvailable = true;
            if (!impl.params.bounds.empty()) {
                implAvailable = resolve.findImpl(sp, traitPath, impl.traitArgs, implTy, [&](const ImplRef& implRef, bool isFuzzy) {
                    const auto* candidate = implRef.data.opt_TraitImpl();
                    return !isFuzzy && candidate && candidate->impl == &impl;
                });
            }
            if (!implAvailable) {
                DEBUG("Skip conditionally unavailable concrete impl");
                return;
            }

            auto implParams = HIRPathParams();
            auto cbMonomorph = MonomorphStatePtr(state.crate.types, implTy, &impl.traitArgs, nullptr);
            auto cbMonomorph2 = MonomorphStatePtr(state.crate.types, nullptr, &implParams, nullptr);

            // TODO: Only emit impls if the type is going to be visible to downstream crates
            // - But how to tell that? What if the type is exposed via `-> impl Foo`?
            // - Lazy (wrong) version would be to not emit if the type is private - but private types can be leaked
            //   - Could flag leaked private types in a previous pass?

            // Emit each method/static (in the trait itself)
            const auto& trait = resolve.hirCrate().getTraitByPath(sp, traitPath);
            for (const auto& vi : trait.values) {
                TRACE_FUNCTION_F("Item " << vi.first << " : " << vi.second.tagStr());
                // Constant, no codegen
                if (vi.second.is_Constant())
                    ;
                // Generic method, no codegen
                else if (vi.second.is_Function() && vi.second.as_Function().params.isGeneric())
                    ;
                // VTable, magic
                else if (vi.first == "vtable#")
                    ;
                else {
                    // Check bounds before queueing for codegen
                    HIRPathParams pp;
                    if (vi.second.is_Function()) {
                        const auto& fcn = vi.second.as_Function();
                        bool rv = true;
                        DEBUG("Bounds = " << fcn.params.fmtBounds());
                        for (const auto& b : fcn.params.bounds) {
                            if (!b.is_TraitBound()) {
                                continue;
                            }
                            const auto& be = b.as_TraitBound();

                            auto bTyMono = resolve.monomorphExpand(sp, be.type, cbMonomorph);
                            auto bTpMono = cbMonomorph.monomorphTraitpath(sp, be.trait, false);
                            resolve.expandAssociatedTypesTp(sp, bTpMono);

                            DEBUG("Check " << bTyMono << ": " << bTpMono);
                            rv = resolve.findImpl(sp, bTpMono.path.path, bTpMono.path.params, bTyMono, [&](const ImplRef& impl, bool) {
                                for (const auto& tyB : bTpMono.typeBounds) {
                                    const auto& ty = impl.getType(state.crate.types, tyB.first.c_str(), tyB.second.atyParams);
                                    DEBUG("ATY " << tyB.first << " " << ty << " ?= exp " << tyB.second.type);
                                    if (ty != tyB.second.type) {
                                        return false;
                                    }
                                }
                                return true;
                            });
                            if (!rv) {
                                break;
                            }
                        }
                        if (!rv) {
                            continue;
                        }

                        DEBUG("Params = " << fcn.params.fmtArgs());
                    }
                    auto path = HIRPath(cbMonomorph2.monomorphType(sp, implTy), HIRGenericPath(traitPath, cbMonomorph2.monomorphPathParams(sp, impl.traitArgs, false)), vi.first, mv$(pp));
                    state.rv.roots.push_back(path.clone());
                    TransEnumerateFillFromPathMono(state, mv$(path));
                }
            }
            for (auto& m : impl.methods) {
                if (m.second.data.params.isGeneric()) {
                    m.second.data.saveCode = true;
                    TransEnumerateGenericFunctionItems(state, Span(), m.second.data, ms, !m.second.data.params.bounds.empty());
                }
            }
        } else {
            for (auto& m : impl.methods) {
                m.second.data.saveCode = true;
                TransEnumerateGenericFunctionItems(state, Span(), m.second.data, ms, !impl.params.bounds.empty() || !m.second.data.params.bounds.empty());
            }
        }
    }
}

/// Enumerate trans items for all public non-generic items (library crate)
TransList TransEnumeratePublic(const WireBoard& wb, HIRCrate& crate) {
    Span sp;
    EnumState state{wb};

    TransEnumeratePublicMod(state, crate.rootModule, HIRSimplePath(crate.crateName, {}), true);

    // Impl blocks
    StaticTraitResolve resolve{wb, OpaqueReveal::All};
    for (auto& implGroup : crate.traitImpls) {
        const auto& traitPath = implGroup.first;
        for (auto& implList : implGroup.second.named) {
            for (auto& impl : implList.second) {
                TransEnumeratePublicTraitImpl(state, resolve, traitPath, *impl);
            }
        }
        for (auto& impl : implGroup.second.nonNamed) {
            TransEnumeratePublicTraitImpl(state, resolve, traitPath, *impl);
        }
        for (auto& impl : implGroup.second.generic) {
            TransEnumeratePublicTraitImpl(state, resolve, traitPath, *impl);
        }
    }

    struct H1 {
        static void enumerateTypeImpl(EnumState& state, HIRTypeImpl& impl) {
            TRACE_FUNCTION_F("impl" << impl.params.fmtArgs() << " " << impl.type);
            HIRPathParams implParams = HIRPathParams();
            MonomorphStatePtr ms(state.crate.types);
            ms.ppImpl = &implParams;
            if (!impl.params.isGeneric()) {
                for (auto& fcn : impl.methods) {
                    DEBUG("fn " << fcn.first << fcn.second.data.params.fmtArgs());
                    if (!fcn.second.data.params.isGeneric()) {
                        TransParams pp(state.crate.types);
                        pp.ppImpl = implParams.clone();
                        pp.ppMethod = HIRPathParams();
                        auto path = HIRPath(MonomorphStatePtr(state.crate.types, nullptr, &implParams, nullptr).monomorphType(Span(), impl.type), fcn.first);
                        path.data.as_UfcsInherent().implParams = pp.ppImpl.clone();
                        path.data.as_UfcsInherent().params = pp.ppMethod.clone();
                        if (fcn.second.publicity.isGlobal()) {
                            state.rv.roots.push_back(path.clone());
                        }
                        state.enumFcn(mv$(path), fcn.second.data, mv$(pp));
                    } else {
                        fcn.second.data.saveCode = true;
                    }
                    if (fcn.second.data.saveCode) {
                        TransEnumerateGenericFunctionItems(state, Span(), fcn.second.data, ms, !impl.params.bounds.empty() || !fcn.second.data.params.bounds.empty());
                    }
                }
            } else {
                for (auto& m : impl.methods) {
                    m.second.data.saveCode = true;
                    TransEnumerateGenericFunctionItems(state, Span(), m.second.data, ms, !impl.params.bounds.empty() || !m.second.data.params.bounds.empty());
                }
            }
            for (auto& e : impl.constants) {
                TransParams tp(state.crate.types);
                tp.ppImpl = HIRPathParams();
                TransEnumerateFillFromLiteral(state, e.second.data.valueRes, std::move(tp));

                if (e.second.publicity.isGlobal() && !impl.params.isGeneric() && !e.second.data.params.isGeneric()) {
                    auto ppMethod = HIRPathParams();
                    for (const auto& r : e.second.data.valueRes.relocations) {
                        if (r.p) {
                            // Still need to monomorph, as lifetimes aren't counted in `is_generic`
                            state.rv.roots.push_back(MonomorphStatePtr(state.crate.types, nullptr, &implParams, &ppMethod).monomorphPath(Span(), *r.p));
                        }
                    }
                }
            }
        }
    };

    for (auto& implGrp : crate.typeImpls.named) {
        for (auto& impl : implGrp.second) {
            H1::enumerateTypeImpl(state, *impl);
        }
    }
    for (auto& impl : crate.typeImpls.nonNamed) {
        H1::enumerateTypeImpl(state, *impl);
    }
    for (auto& impl : crate.typeImpls.generic) {
        H1::enumerateTypeImpl(state, *impl);
    }

    // Ensure that the panic handler is emitted
    {
        auto it = crate.langItems.find("trustme-panic_implementation");
        if (it != crate.langItems.end()) {
            HIRGenericPath p = it->second;
            const auto& f = crate.getFunctionByPath(Span(), p.path);
            p.params = HIRPathParams();
            TransEnumerateFillFromPathMono(state, std::move(p));
        }
    }

    TransEnumerateGlobalAsm(state, crate.rootModule);

    auto rv = TransEnumerateCommonPost(state);

    // Strip out any functions/types/statics that are still generic?
    for (auto it = rv.functions.begin(); it != rv.functions.end();) {
        if (monomorphisePathNeeded(it->first)) {
            rv.functions.erase(it++);
        } else {
            ++it;
        }
    }
    for (auto it = rv.statics.begin(); it != rv.statics.end();) {
        if (monomorphisePathNeeded(it->first)) {
            rv.statics.erase(it++);
        } else {
            ++it;
        }
    }

    return rv;
}

namespace {
    template <typename T>
    void removeMissing(const WireBoard& wb, std::map<HIRPath, T>& target, const std::map<HIRPath, T>& tpl) {
        ::std::unordered_map<::std::string, const HIRPath*> requiredSymbols;
        for (const auto& entry : tpl) {
            auto symbol = FMT(TransMangleValue(wb, entry.first));
            auto inserted = requiredSymbols.emplace(mv$(symbol), &entry.first);
            ASSERT_BUG(Span(), inserted.second || inserted.first->second->equalsIgnoringRegions(entry.first), "Distinct paths have the same mangled name: " << *inserted.first->second << " and " << entry.first);
        }

        for (auto itIn = target.begin(); itIn != target.end();) {
            const auto symbol = FMT(TransMangleValue(wb, itIn->first));
            const auto required = requiredSymbols.find(symbol);
            if (required == requiredSymbols.end()) {
                DEBUG("Remove " << itIn->first);
                itIn = target.erase(itIn);
            } else {
                ASSERT_BUG(Span(), required->second->equalsIgnoringRegions(itIn->first), "Distinct paths have the same mangled name: " << *required->second << " and " << itIn->first);
                DEBUG("Keep " << itIn->first);
                ++itIn;
            }
        }
    }

    HIRTypeRef implicitDropType(const HIRPath& path, const HIRSimplePath& dropTrait) {
        if (const auto* inherent = path.data.opt_UfcsInherent()) {
            return inherent->item == "#drop_glue" ? inherent->type : nullptr;
        }
        if (const auto* known = path.data.opt_UfcsKnown()) {
            return known->item == "drop" && known->trait.path == dropTrait ? known->type : nullptr;
        }
        return nullptr;
    }
}

void TransEnumerateCleanup(const WireBoard& wb, const HIRCrate& crate, TransList& list) {
    // Clear the function enum cache and re-generate
    // - This is called after optimisation, so the cache may point to functions that have been optimised out
    for (const auto& fcnE : list.functions) {
        auto& function = *fcnE.second->ptr;
        if (function.code.mir) {
            function.code.mir->transEnumState = MIREnumCachePtr();
        }
    }
    for (const auto& fcnE : list.functions) {
        auto& function = *fcnE.second->ptr;
        if (function.code.mir && !function.code.mir->transEnumState) {
            DEBUG(fcnE.first);
            auto* esp = new MIREnumCache();
            TransEnumerateFillFromMIR(*esp, *function.code.mir);
            function.code.mir->transEnumState = MIREnumCachePtr(esp);
        }
    }

    // Completely re-run enumeration, but this time include the TransList so MIR recursion uses the optimised versions
    EnumState state{wb};
    state.origList = &list;
    for (const auto& p : list.roots) {
        HIRPath path = p.clone();
        MonomorphState unusedParams(state.crate.types);
        const auto& vi = state.resolve.getValue(Span(), path, unusedParams, /*signature_only=*/true);
        if (const auto* f = vi.opt_Function()) {
            switch (path.data.tag()) {
default:
                break;
                case HIRPathData::TAG_Generic: {
                    break;
                }
            }
        } else {
            // Statics don't have lifetime params
        }
        TransEnumerateFillFromPathMono(state, std::move(path));
    }
    auto newList = TransEnumerateCommonPost(state);

    // Region-bearing drop variants are retained below when their value type or
    // an explicit MIR drop is reachable. Their optimised bodies can name
    // region-bearing value paths which the canonical ABI variant does not, so
    // enumerate those bodies too. A borrowed ABI type alone needs only a C
    // forward declaration and must not retain a body which accesses its fields.
    stl::Vector<const TransListFunction*> enumeratedImplicitDrops;
    for (;;) {
        stl::Vector<const TransListFunction*> generatedFunctions;
        for (const auto& entry : list.functions) {
            const auto* type = implicitDropType(entry.first, state.resolve.langDrop());
            if (!type || (!newList.hasType(type, false) && newList.dropGlue.count(type) == 0)
                || newList.findFunction(entry.first)
                || entry.second->forcePrototype) {
                continue;
            }
            if (!entry.second->monomorphised.code && !entry.second->ptr->code.mir) {
                continue;
            }

            bool alreadyEnumerated = false;
            for (const auto* function : enumeratedImplicitDrops) {
                alreadyEnumerated |= function == entry.second.get();
            }
            if (alreadyEnumerated) {
                continue;
            }
            enumeratedImplicitDrops.pushBack(entry.second.get());
            generatedFunctions.pushBack(entry.second.get());
        }
        if (generatedFunctions.empty()) {
            break;
        }
        TransEnumerateGeneratedMIR(wb, newList, generatedFunctions);
    }

    // Add stub entries to `new_list` for vtables and destructors, items that would be created by stages after enumerate
    // - VTables
    for (const auto* type : newList.dropGlue) {
        newList.functions.insert(std::make_pair(HIRPath(type, "#drop_glue"), nullptr));
    }
    for (const auto& vtp : newList.vtables) {
        Span sp;
        const auto& traitPath = vtp.first.data.as_UfcsKnown().trait;
        const auto& type = vtp.first.data.as_UfcsKnown().type;

        HIRPath dropGlueFn(type, "#drop_glue");
        DEBUG("++ " << dropGlueFn);
        newList.functions.insert(std::make_pair(std::move(dropGlueFn), nullptr));

        DEBUG("++ " << vtp.first);
        newList.statics.insert(std::make_pair(vtp.first.clone(), nullptr));

        if (traitPath.path == HIRSimplePath()) {
            // Non-data traits
            continue;
        }

        const auto& trait = crate.getTraitByPath(sp, traitPath.path);

        auto monomorphCbTrait = MonomorphStatePtr(state.crate.types, type, &traitPath.params, nullptr);
        for (unsigned int i = 0; i < trait.valueIndexes.size(); i++) {
            // Find the corresponding vtable entry
            for (const auto& m : trait.valueIndexes) {
                // NOTE: The "3" is the number of non-method vtable entries
                if (m.second.first != 3 + i) {
                    continue;
                }

                auto traitGpath = monomorphCbTrait.monomorphGenericpath(sp, m.second.second, false);
                auto itemPath = HIRPath(type, mv$(traitGpath), m.first);
                state.resolve.expandAssociatedTypesPath(sp, itemPath);

                DEBUG("++ " << itemPath);
                newList.functions.insert(std::make_pair(std::move(itemPath), nullptr));

                // If the entry is a by-value function, then emit a reference to a shim
                const auto& srcTrait = state.resolve.hirCrate().getTraitByPath(sp, m.second.second.path);
                const auto& item = srcTrait.values.at(m.first);
                if (item.is_Function() && item.as_Function().receiver == HIRFunction::Receiver::Value) {
                    traitGpath = monomorphCbTrait.monomorphGenericpath(sp, m.second.second, false);
                    auto itemPath = HIRPath(type, mv$(traitGpath), RcString::newInterned(FMT(m.first << "#ptr")));
                    state.resolve.expandAssociatedTypesPath(sp, itemPath);
                    DEBUG("++ " << itemPath);
                    newList.functions.insert(std::make_pair(std::move(itemPath), nullptr));
                }
            }
        }
    }
    // - Drop Glue
    for (const auto& ty : newList.types) {
        Span sp;
        // Ignore shallow types
        if (ty.second) {
            continue;
        }
        // TraitObject and Slice flag as needing drop glue... but don't actually get it generated
        if (ty.first->is_TraitObject() || ty.first->is_Slice()) {
            continue;
        }
        if (!state.resolve.typeNeedsDropGlue(sp, ty.first)) {
            continue;
        }

        HIRPath dropGlueFn(ty.first, "#drop_glue");
        DEBUG("++ " << dropGlueFn);
        newList.functions.insert(std::make_pair(std::move(dropGlueFn), nullptr));

        if (ty.first->is_Path() && ty.first->as_Path().binding.getTraitMarkings()->hasDropImpl) {
            auto fcnPath = HIRPath(ty.first, state.resolve.langDrop(), "drop");
            DEBUG("++ " << fcnPath);
            newList.functions.insert(std::make_pair(std::move(fcnPath), nullptr));
        }
    }
    for (const auto& ty : newList.autoCloneImpls) {
        HIRPath fnPath(ty, crate.getLangItemPath(Span(), "clone"), "clone");
        DEBUG("++ " << fnPath);
        newList.functions.insert(std::make_pair(std::move(fnPath), nullptr));
    }
    for (const auto& ty : newList.autoCloneFromImpls) {
        HIRPath fnPath(ty, crate.getLangItemPath(Span(), "clone"), "clone_from");
        DEBUG("++ " << fnPath);
        newList.functions.insert(std::make_pair(std::move(fnPath), nullptr));
    }
    for (const auto& fnPath : newList.traitObjectMethods) {
        DEBUG("++ " << fnPath);
        newList.functions.insert(std::make_pair(fnPath.clone(), nullptr));
    }
    for (const auto& ty : newList.autoFnptrImpls) {
        // - <fn(...) as FnPtr>::addr
        HIRPath fnPath(ty, crate.getLangItemPath(Span(), "fn_ptr_trait"), "addr");
        DEBUG("++ " << fnPath);
        newList.functions.insert(std::make_pair(std::move(fnPath), nullptr));
    }

    // Drop terminators enumerate their type, not a function path. Keep every
    // exact region-bearing glue/method variant already generated for a value
    // type or explicit MIR drop; its body may observe that exact type through
    // TypeId. A type seen only behind a borrow does not need a drop body.
    for (const auto& entry : list.functions) {
        const auto* type = implicitDropType(entry.first, state.resolve.langDrop());
        if (type && (newList.hasType(type, false) || newList.dropGlue.count(type) != 0)) {
            newList.functions.insert(std::make_pair(entry.first.clone(), nullptr));
        }
    }

    list.clearTypes();
    for (const auto& ty : newList.types) {
        ASSERT_BUG(Span(), list.addType(ty.first, ty.second), "Duplicate type in cleaned translation list: " << ty.first);
    }
    // Constructors are re-derived along with the types, so a constructor that
    // optimisation made unreachable must go too: its wrapper would name a type
    // this list no longer carries.
    list.constructors = mv$(newList.constructors);
    removeMissing(wb, list.functions, newList.functions);
    removeMissing(wb, list.statics, newList.statics);
}

/// Common post-processing
void TransEnumerateCommonPostRun(EnumState& state) {
    // Run the enumerate queue (keeps the recursion depth down)
    while (!state.fcnQueue.empty()) {
        auto& fcnOut = *state.fcnQueue.front();
        state.fcnQueue.pop_front();

        TRACE_FUNCTION_F("Function " << ::std::find_if(state.rv.functions.begin(), state.rv.functions.end(), [&](const auto& x) {
            return x.second.get() == &fcnOut;
        })->first);

        TransEnumerateFillFromFunction(state, *fcnOut.path, *fcnOut.ptr, fcnOut.pp);
    }

}

TransList TransEnumerateCommonPost(EnumState& state) {
    TransEnumerateCommonPostRun(state);
    TransEnumerateTypes(state);

    return mv$(state.rv);
}

namespace {
    bool mergeEnumeratedItems(HIRTypeInterner& types, TransList& out, TransList additions) {
        ASSERT_BUG(Span(), additions.roots.empty(), "Incremental translation enumeration unexpectedly added roots");
        ASSERT_BUG(Span(), additions.autoStatics.empty() && additions.autoFunctions.empty(), "Enumeration generated translation items before TransAutoImpls");

        bool changed = false;
        for (auto& ent : additions.functions) {
            if (auto* dst = out.addFunction(types, ent.first.clone())) {
                changed = true;
                dst->ptr = ent.second->ptr;
                dst->pp = mv$(ent.second->pp);
                dst->monomorphised = mv$(ent.second->monomorphised);
                dst->forcePrototype = ent.second->forcePrototype;
            }
        }
        for (auto& ent : additions.statics) {
            if (auto* dst = out.addStatic(types, ent.first.clone())) {
                changed = true;
                dst->ptr = ent.second->ptr;
                dst->pp = mv$(ent.second->pp);
            }
        }
        for (auto& ent : additions.constants) {
            if (auto* dst = out.addConst(types, ent.first.clone())) {
                changed = true;
                dst->ptr = ent.second->ptr;
                dst->pp = mv$(ent.second->pp);
            }
        }
        for (auto& ent : additions.vtables) {
            changed |= out.addVtable(ent.first.clone(), mv$(ent.second));
        }
        for (const auto& ty : additions.typeids) {
            changed |= out.typeids.insert(ty).second;
        }
        for (const auto& ty : additions.dropGlue) {
            changed |= out.dropGlue.insert(ty).second;
        }
        for (const auto& path : additions.constructors) {
            changed |= out.constructors.insert(path.clone()).second;
        }
        for (const auto& ty : additions.autoCloneImpls) {
            changed |= out.autoCloneImpls.insert(ty).second;
        }
        for (const auto& ty : additions.autoCloneFromImpls) {
            changed |= out.autoCloneFromImpls.insert(ty).second;
        }
        for (const auto& ty : additions.autoFnptrImpls) {
            changed |= out.autoFnptrImpls.insert(ty).second;
        }
        for (const auto& path : additions.traitObjectMethods) {
            changed |= out.traitObjectMethods.insert(path.clone()).second;
        }
        for (const auto& ent : additions.types) {
            changed |= out.addType(ent.first, ent.second);
        }
        return changed;
    }

    bool transListContainsPath(const TransList& list, const HIRPath& path) {
        return list.findFunction(path)
            || list.statics.count(path)
            || list.constants.count(path)
            || list.vtables.count(path);
    }
}

void TransEnumerateGeneratedStatics(const WireBoard& wb, TransList& list, const ::std::vector<HIRPath>& paths) {
    if (paths.empty()) {
        return;
    }

    EnumState state{wb};
    for (const auto& path : paths) {
        TransEnumerateFillFromPathMono(state, path.clone());
    }
    mergeEnumeratedItems(state.crate.types, list, TransEnumerateCommonPost(state));
}

bool TransEnumerateGeneratedLiteral(const WireBoard& wb, TransList& list, const EncodedLiteral& literal) {
    EnumState state{wb};
    for (const auto& relocation : literal.relocations) {
        if (relocation.p && !transListContainsPath(list, *relocation.p)) {
            ASSERT_BUG(Span(), !monomorphisePathNeeded(*relocation.p),
                "Generated literal contains a generic translation path: " << *relocation.p);
            TransEnumerateFillFromPathMono(state, relocation.p->clone());
        }
    }
    return mergeEnumeratedItems(state.crate.types, list, TransEnumerateCommonPost(state));
}

bool TransEnumerateGeneratedMIR(const WireBoard& wb, TransList& list, const stl::Vector<const TransListFunction*>& functions) {
    EnumState state{wb};
    for (const auto* function : functions) {
        const MIRFunction* mir;
        HIRTypeRef returnType;
        const HIRFunction::argsT* args;
        if (function->monomorphised.code) {
            mir = &*function->monomorphised.code;
            returnType = function->monomorphised.retTy;
            args = &function->monomorphised.argTys;
        } else {
            ASSERT_BUG(Span(), function->ptr->code.mir, "Generated function has no MIR: " << *function->path);
            mir = &*function->ptr->code.mir;
            returnType = function->ptr->returnType;
            args = &function->ptr->args;
        }

        MIREnumCache cache;
        TransEnumerateFillFromMIR(cache, *mir);
        for (const auto* ty : cache.typeids) {
            if (list.typeids.count(ty) == 0) {
                state.rv.typeids.insert(ty);
            }
        }
        for (const auto* type : cache.destructorTypes) {
            enumerateDestructorType(state, type);
        }
        for (const auto* path : cache.paths) {
            ASSERT_BUG(Span(), !monomorphisePathNeeded(*path), "Generated MIR contains a generic translation path: " << *path);
            if (!transListContainsPath(list, *path)) {
                TransEnumerateFillFromPathMono(state, path->clone());
            }
        }

        Span sp;
        auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) { os << *function->path; });
        MIRTypeResolve mirResolve{sp, state.resolve, pathCallback, returnType, *args, *mir};
        for (const auto& block : mir->blocks) {
            if (const auto* drop = block.terminator.opt_Drop()) {
                HIRTypeRef tmp;
                enumerateDestructorType(state, mirResolve.getLvalueType(tmp, drop->slot));
            }
        }
    }
    return mergeEnumeratedItems(state.crate.types, list, TransEnumerateCommonPost(state));
}

namespace {
    struct PtrComp {
        template <typename T>
        bool operator()(const T* lhs, const T* rhs) const {
            return *lhs < *rhs;
        }
    };

    struct TypeVisitor {
        const HIRCrate& crate;
        ::StaticTraitResolve resolve;
        TransList& out;
        const TransList* prevList;

        HIRTypeRefSet activeSet;

        TypeVisitor(const WireBoard& wb, TransList& out, const TransList* prevList)
            : crate(*wb.crate)
            , resolve(wb, OpaqueReveal::All)
            , out(out)
            , prevList(prevList)
        {
        }

        ~TypeVisitor() {
            DEBUG("Emitted a total of " << out.types.size() << " type entries");
        }

        void visitStruct(const HIRTypeData* selfType, const HIRGenericPath& path, const HIRStruct& item) {
            Span sp;
            HIRTypeRef tmp;
            size_t fieldCount = 0;
            MonomorphStatePtr ms(crate.types, selfType, &path.params, nullptr);
            auto monomorph = [&](const auto& x) {
                DEBUG(x);
                return resolve.monomorphExpandOpt(sp, tmp, x, ms);
            };
            switch (item.data.tag()) {
                case HIRStructData::TAG_Unit: {
                    break;
                }
                case HIRStructData::TAG_Tuple: {
                    auto& e = item.data.as_Tuple();
                    fieldCount = e.size();
                    for (const auto& fld : e) { visitType(monomorph(fld.ent)); }
                    break;
                }
                case HIRStructData::TAG_Named: {
                    auto& e = item.data.as_Named();
                    fieldCount = e.size();
                    for (const auto& fld : e) visitType(monomorph(fld.ty));
                    break;
                }
            }
            if (item.structMarkings.isAsyncDropGlue) {
                const auto* repr = TargetGetTypeRepr(sp, resolve, selfType);
                ASSERT_BUG(sp, repr && repr->fields.size() >= fieldCount, "invalid async-drop glue representation for " << selfType);
                for (size_t i = fieldCount; i < repr->fields.size(); i++) {
                    visitType(repr->fields[i].ty);
                }
            }
        }

        void visitUnion(const HIRTypeData* selfType, const HIRGenericPath& path, const HIRUnion& item) {
            Span sp;
            HIRTypeRef tmp;
            MonomorphStatePtr ms(crate.types, selfType, &path.params, nullptr);
            auto monomorph = [&](const auto& x) {
                return resolve.monomorphExpandOpt(sp, tmp, x, ms);
            };
            for (const auto& variant : item.variants) {
                visitType(monomorph(variant.ty));
            }
        }

        void visitEnum(const HIRTypeData* selfType, const HIRGenericPath& path, const HIREnum& item) {
            Span sp;
            HIRTypeRef tmp;
            MonomorphStatePtr ms(crate.types, selfType, &path.params, nullptr);
            auto monomorph = [&](const auto& x) {
                return resolve.monomorphExpandOpt(sp, tmp, x, ms);
            };
            if (const auto* e = item.data.opt_Data()) {
                for (const auto& variant : *e) {
                    visitType(monomorph(variant.type));
                }
            }
        }

        enum class Mode {
            Shallow,
            Normal,
            Deep,
        };

        void visitType(const HIRTypeData* ty, Mode mode = Mode::Normal) {
            Span sp;
            // Trans is reveal-all: a projection over someone's
            // return-position opaque (a struct field instantiated with a
            // fn-def parameter) may only normalise once the opaque is
            // revealed (mir/validate/needs-reveal-all).
            if (visitTyWith(ty, [](const HIRTypeData* inner) { return inner->is_ErasedType(); })) {
                HIRTypeRef revealed = ty;
                resolve.revealOpaqueTypes(sp, revealed);
                resolve.expandAssociatedTypes(sp, revealed);
                ty = revealed;
            }
            // If the type has already been visited, AND either this is a shallow visit, or the previous wasn't
            if (out.hasType(ty, mode == Mode::Shallow)) {
                return;
            }
            TRACE_FUNCTION_F(ty << " - " << (mode == Mode::Shallow ? "Shallow" : (mode == Mode::Normal ? "Normal" : "Deep")));

            if (mode == Mode::Shallow) {
                switch ((*ty).tag()) {
default:
                    break;
                    case HIRTypeData::TAG_Infer: {
                        BUG(sp, "`_` type hit in enumeration");
                        break;
                    }
                    case HIRTypeData::TAG_Path: {
                        auto& te = (*ty).as_Path();
                        switch (te.binding.tag()) {
                            case HIRTypePathBinding::TAG_Unbound: {
                                BUG(sp, "Unbound type hit in enumeration - " << ty);
                                break;
                            }
                            case HIRTypePathBinding::TAG_Opaque: {
                                BUG(sp, "Opaque type hit in enumeration - " << ty);
                                break;
                            }
                            case HIRTypePathBinding::TAG_ExternType: {
                                break;
                            }
                            case HIRTypePathBinding::TAG_Struct: {
                                break;
                            }
                            case HIRTypePathBinding::TAG_Union: {
                                break;
                            }
                            case HIRTypePathBinding::TAG_Enum: {
                                break;
                            }
                        }
                        break;
                    }
                    case HIRTypeData::TAG_Array: {
                        auto& te = (*ty).as_Array();
                        ASSERT_BUG(sp, te.size.is_Known(), "Encountered unknown array size - " << ty);
                        break;
                    }
                    case HIRTypeData::TAG_Function: {
                        auto& te = (*ty).as_Function();
                        visitType(te.rettype, Mode::Shallow);
                        for (const auto& sty : te.argTypes) {
                            visitType(sty, Mode::Shallow);
                        }
                        break;
                    }
                    case HIRTypeData::TAG_Pointer: {
                        auto& te = (*ty).as_Pointer();
                        visitType(te.inner, Mode::Shallow);
                        break;
                    }
                    case HIRTypeData::TAG_Borrow: {
                        auto& te = (*ty).as_Borrow();
                        visitType(te.inner, Mode::Shallow);
                        break;
                    }
                    case HIRTypeData::TAG_Pattern: {
                        auto& te = (*ty).as_Pattern();
                        visitType(te.inner, Mode::Shallow);
                        break;
                    }
                }
            } else {
                if (activeSet.find(ty) != activeSet.end()) {
                    // TODO: Handle recursion
                    BUG(sp, "- Type recursion on " << ty);
                }
                activeSet.insert(ty);

                switch ((*ty).tag()) {
                    case HIRTypeData::TAG_Infer: {
                        BUG(sp, "`_` type hit in enumeration");
                        break;
                    }
                    case HIRTypeData::TAG_Generic: {
                        BUG(sp, "Generic type hit in enumeration - " << ty);
                        break;
                    }
                    case HIRTypeData::TAG_ErasedType: {
                        break;
                    }
                    case HIRTypeData::TAG_NodeType: {
                        BUG(sp, "NodeType type hit in enumeration - " << ty);
                        break;
                    }
                    case HIRTypeData::TAG_Diverge: {
                        break;
                    }
                    case HIRTypeData::TAG_Primitive: {
                        break;
                    }
                    case HIRTypeData::TAG_Path: {
                        auto& te = (*ty).as_Path();
                        switch (te.binding.tag()) {
                            case HIRTypePathBinding::TAG_Unbound: {
                                BUG(sp, "Unbound type hit in enumeration - " << ty);
                                break;
                            }
                            case HIRTypePathBinding::TAG_Opaque: {
                                BUG(sp, "Opaque type hit in enumeration - " << ty);
                                break;
                            }
                            case HIRTypePathBinding::TAG_ExternType: {
                                // No innards to visit
                                break;
                            }
                            case HIRTypePathBinding::TAG_Struct: {
                                auto& tpb = te.binding.as_Struct();
                                visitStruct(ty, te.path.data.as_Generic(), *tpb);
                                break;
                            }
                            case HIRTypePathBinding::TAG_Union: {
                                auto& tpb = te.binding.as_Union();
                                visitUnion(ty, te.path.data.as_Generic(), *tpb);
                                break;
                            }
                            case HIRTypePathBinding::TAG_Enum: {
                                auto& tpb = te.binding.as_Enum();
                                // NOTE: Force repr generation before recursing into enums (allows layout optimisation to be calculated)
                                TargetGetTypeRepr(sp, resolve, ty);
                                visitEnum(ty, te.path.data.as_Generic(), *tpb);
                                break;
                            }
                        }
                        break;
                    }
                    case HIRTypeData::TAG_TraitObject: {
                        auto& te = (*ty).as_TraitObject();
                        Span sp;

                        // If the data trait is empty, then no vtable to visit
                        if (!te.trait.path.path.components().empty()) {
                            // Ensure that the data trait's vtable is present
                            const auto& trait = *te.trait.traitPtr;
                            auto vtableTy = trait.getVtableType(sp, crate, te);

                            visitType(vtableTy);
                        } else {
                            // Wait, what vtable should be used then?
                        }
                        break;
                    }
                    case HIRTypeData::TAG_Array: {
                        auto& te = (*ty).as_Array();
                        ASSERT_BUG(sp, te.size.is_Known(), "Encountered unknown array size - " << ty);
                        visitType(te.inner, mode);
                        break;
                    }
                    case HIRTypeData::TAG_Slice: {
                        auto& te = (*ty).as_Slice();
                        visitType(te.inner, mode);
                        break;
                    }
                    case HIRTypeData::TAG_Pattern: {
                        auto& te = (*ty).as_Pattern();
                        visitType(te.inner, mode);
                        break;
                    }
                    case HIRTypeData::TAG_Borrow: {
                        auto& te = (*ty).as_Borrow();
                        visitType(te.inner, mode != Mode::Deep ? Mode::Shallow : Mode::Deep);
                        break;
                    }
                    case HIRTypeData::TAG_Pointer: {
                        auto& te = (*ty).as_Pointer();
                        visitType(te.inner, mode != Mode::Deep ? Mode::Shallow : Mode::Deep);
                        break;
                    }
                    case HIRTypeData::TAG_Tuple: {
                        auto& te = (*ty).as_Tuple();
                        for (const auto& sty : te) {
                            visitType(sty, mode);
                        }
                        break;
                    }
                    case HIRTypeData::TAG_NamedFunction: {
                        break;
                    }
                    case HIRTypeData::TAG_Function: {
                        auto& te = (*ty).as_Function();
                        visitType(te.rettype, mode != Mode::Deep ? Mode::Shallow : Mode::Deep);
                        for (const auto& sty : te.argTypes) {
                            visitType(sty, mode != Mode::Deep ? Mode::Shallow : Mode::Deep);
                        }
                        break;
                    }
                }
                activeSet.erase(ty);
            }

            bool shallow = (mode == Mode::Shallow);
            auto i = out.types.size();
            ASSERT_BUG(sp, out.addType(ty, shallow), "Type was emitted while it was being visited: " << ty);
            DEBUG("Add type " << ty << (shallow ? " (Shallow)" : "") << " " << i);
        }

        void __attribute__((noinline)) visitFunction(const HIRPath& path, const HIRFunction& fcn, const TransParams& pp) {
            Span sp;
            auto& tv = *this;

            HIRTypeRef tmp;
            bool useMonomorph = true;
            auto monomorph = [&](const HIRTypeData* ty) -> const HIRTypeData* {
                return useMonomorph ? pp.maybeMonomorph(resolve, tmp, ty) : ty;
            };
            DEBUG(fcn.returnType);
            bool hasErased = visitTyWith(fcn.returnType, [&](const auto& x) {
                return x->is_ErasedType();
            });
            // Handle erased types in the return type.
            if (hasErased || monomorphiseTypeNeeded(fcn.returnType)) {
                // If there's an erased type, make a copy with the erased type expanded
                HIRTypeRef retTy;
                if (hasErased) {
                    retTy = cloneTyWith(crate.types, sp, fcn.returnType, [&](const auto& x, auto& out) {
                        if (const auto* te = x->opt_ErasedType()) {
                            if (const auto* e = te->inner.opt_Fcn()) {
                                out = fcn.code.erasedTypes.at(e->index);
                                return true;
                            }
                        }
                        return false;
                    });
                    DEBUG(retTy);
                    retTy = pp.monomorph(tv.resolve, retTy);
                } else {
                    retTy = pp.monomorph(tv.resolve, fcn.returnType);
                }
                tv.visitType(retTy);
            } else {
                tv.visitType(fcn.returnType);
            }
            for (const auto& arg : fcn.args) {
                DEBUG(arg.second);
                tv.visitType(monomorph(arg.second));
            }

            const MIRFunction* mirP = nullptr;
            if (fcn.code.mir) {
                mirP = &*fcn.code.mir;
            }
            // If the previous list is populated, then this should be in it.
            if (prevList) {
                const auto* transFcn = prevList->findFunction(path);
                ASSERT_BUG(sp, transFcn, "Unable to find " << path << " in first-pass enumerate result");
                if (transFcn && transFcn->monomorphised.code) {
                    mirP = &*transFcn->monomorphised.code;
                    useMonomorph = false;
                }
            }
            if (mirP) {
                const MIRFunction& mir = *mirP;
                for (const auto& ty : mir.locals) {
                    tv.visitType(monomorph(ty));
                }

                // Find all LValue::Deref instances and get the result type
                MIRTypeResolve::argsT emptyArgs;
                HIRTypeRef emptyTy;
                auto pathCallback = makeCallable<MIRPathCb>([](auto&) {});
                MIRTypeResolve localMirRes(sp, tv.resolve, pathCallback, /*ret_ty=*/emptyTy, emptyArgs, mir);
                for (const auto& block : mir.blocks) {
                    struct MirVisitor: public MIRVisitor {
                        const Span& sp;
                        TypeVisitor& tv;
                        const TransParams& pp;
                        const HIRFunction& fcn;
                        const MIRTypeResolve& localMirRes;

                        MirVisitor(const Span& sp, TypeVisitor& tv, const TransParams& pp, const HIRFunction& fcn, const MIRTypeResolve& localMirRes)
                            : sp(sp)
                            , tv(tv)
                            , pp(pp)
                            , fcn(fcn)
                            , localMirRes(localMirRes)
                        {
                        }

                        bool visitLvalue(const MIRLValue& lv, MIRValUsage /*vu*/) override {
                            TRACE_FUNCTION_F(lv);
                            if (::std::none_of(lv.wrappers.begin(), lv.wrappers.end(), [](const auto& w) {
                                return w.is_Deref();
                            })) {
                                return false;
                            }
                            HIRTypeRef tmp;
                            auto monomorphOuter = [&](const auto& tpl) {
                                return pp.maybeMonomorph(tv.resolve, tmp, tpl);
                            };
                            const HIRTypeData* ty = nullptr;
                            ;
                            // Recurse, if Deref get the type and add it to the visitor
                            switch (lv.root.tag()) {
                                case MIRLValue::Storage::TAG_Return: {
                                    MIR_TODO(localMirRes, "Get return type for MIR type enumeration");
                                    break;
                                }
                                case MIRLValue::Storage::TAG_Argument: {
                                    decltype(lv.root.as_Argument()) e = lv.root.as_Argument();
                                    ty = monomorphOuter(fcn.args[e].second);
                                    break;
                                }
                                case MIRLValue::Storage::TAG_Local: {
                                    decltype(lv.root.as_Local()) e = lv.root.as_Local();
                                    if (&localMirRes.fcn == &*fcn.code.mir) {
                                        ty = monomorphOuter(fcn.code.mir->locals[e]);
                                    } else {
                                        ty = localMirRes.fcn.locals[e];
                                    }
                                    break;
                                }
                                case MIRLValue::Storage::TAG_Static: {
                                    decltype(lv.root.as_Static()) e = lv.root.as_Static();
                                    // TODO: Monomorphise the path then hand to MIR::TypeResolve?
                                    const auto& path = e;
                                    switch (path.data.tag()) {
                                        case HIRPathData::TAG_Generic: {
                                            auto& pe = path.data.as_Generic();
                                            MIR_ASSERT(localMirRes, pe.params.types.empty(), "Path params on static - " << path);
                                            const auto& s = tv.resolve.hirCrate().getStaticByPath(localMirRes.sp, pe.path);
                                            ty = s.type;
                                            break;
                                        }
                                        case HIRPathData::TAG_UfcsKnown: {
                                            MIR_TODO(localMirRes, "LValue::Static - UfcsKnown - " << path);
                                        }
                                        case HIRPathData::TAG_UfcsUnknown: {
                                            MIR_BUG(localMirRes, "Encountered UfcsUnknown in LValue::Static - " << path);
                                        }
                                        case HIRPathData::TAG_UfcsInherent: {
                                            MIR_TODO(localMirRes, "LValue::Static - UfcsInherent - " << path);
                                        }
                                    }
                                    break;
                                }
                            }
                            assert(ty);

                                for (const auto& w : lv.wrappers) {
                                    ty = localMirRes.getUnwrappedType(tmp, w, ty);
                                    if (w.is_Deref()) {
                                        tv.visitType(ty);
                                    }
                                }
                                return false;
                        }

                        bool visitConst(const MIRConstant& c) override {
                            // A byte string can introduce a type that appears
                            // in neither a local nor the callee signature.  A
                            // byte string passed through C varargs is emitted
                            // as a pointer to its inferred `[u8; N]` type, so
                            // make that type reachable before codegen asks
                            // emitCtype for it.  Other constant kinds can name
                            // generic parameters that are not available in
                            // this signature-only traversal.
                            if (c.is_Bytes()) {
                                HIRTypeRef tmp;
                                auto ty = localMirRes.getConstType(c);
                                tv.visitType(pp.maybeMonomorph(tv.resolve, tmp, ty));
                            }
                            return MIRVisitor::visitConst(c);
                        }

                void visitPath(const HIRPath& /*p*/) override {
                    // Paths don't need visiting?
                }
                void visitType(const HIRTypeData* ty) override {
                    HIRTypeRef tmp;
                    tv.visitType(pp.maybeMonomorph(tv.resolve, tmp, ty));
                }
            };
            MirVisitor mirVisit(sp, tv, pp, fcn, localMirRes);
            for (const auto& stmt : block.statements) {
                DEBUG(stmt);
                mirVisit.visitStmt(stmt);
            }
            DEBUG(block.terminator);
            mirVisit.visitTerminator(block.terminator);

            // HACK: Currently calling `caller_location` creates an empty location (so needs the type)
            if (block.terminator.is_Call() && block.terminator.as_Call().fcn.is_Intrinsic()) {
                const auto& e2 = block.terminator.as_Call().fcn.as_Intrinsic();
                if (e2.name == "caller_location") {
                    const auto& p = localMirRes.resolve.hirCrate().getLangItemPath(sp, "panic_location");
                    const auto& s = localMirRes.resolve.hirCrate().getStructByPath(sp, p);
                    tv.visitType(tv.crate.types.path(HIRPath(p), &s));
                }
                // In 1.74+ the `offset` intrinsic takes a pointer as its generic
                else if (e2.name == "offset") {
                    HIRTypeRef tmp;
                    const auto& ty = pp.maybeMonomorph(tv.resolve, tmp, e2.params.types.at(0));
                    tv.visitType(ty->as_Pointer().inner);
                }
            }
            if (block.terminator.is_Call() && block.terminator.as_Call().fcn.is_Path()) {
                const auto& p = block.terminator.as_Call().fcn.as_Path();
                if (p.data.is_UfcsKnown()) {
                    HIRTypeRef tmp;
                    const auto& ty = pp.maybeMonomorph(tv.resolve, tmp, p.data.as_UfcsKnown().type);
                    if (ty->is_TraitObject()) {
                        // Must have the vtable for the trait object available!
                        tv.visitType(ty);
                    }
                }
            }
        }
    }
}
}
; // struct TypeVisitor
} // namespace <empty>

// Enumerate types required for the enumerated items
void TransEnumerateTypes(EnumState& state) {
    TRACE_FUNCTION;
    Span sp;
    TypeVisitor tv{state.resolve.board(), state.rv, state.origList};

    // Taking `<dyn Trait>::method` as a function item creates a dispatch
    // wrapper without creating any concrete vtable instance.  The wrapper
    // dereferences the vtable, so visit its complete type and all field
    // dependencies explicitly.  This runs in both initial enumeration and
    // cleanup's re-derivation of the reachable list.
    for (const auto& path : state.rv.traitObjectMethods) {
        const auto& pe = path.data.as_UfcsKnown();
        const auto& tyDyn = pe.type->as_TraitObject();
        tv.visitType(tyDyn.trait.traitPtr->getVtableType(sp, state.crate, tyDyn));
    }

    unsigned int typesCount = 0;
    size_t constructorsVisited = 0;
    bool constructorsAdded;
    do {
        // Visit all functions that haven't been type-visited yet
        for (unsigned int i = 0; i < state.fcnsToTypeVisit.size(); i++) {
            auto* p = state.fcnsToTypeVisit[i];
            assert(p->path);
            assert(p->ptr);
            auto& fcnPath = *p->path;
            const auto& fcn = *p->ptr;
            const auto& pp = p->pp;

            TRACE_FUNCTION_F("Function " << fcnPath);
            tv.visitFunction(fcnPath, fcn, pp);
        }
        state.fcnsToTypeVisit.clear();
        // TODO: Similarly restrict revisiting of statics.
        // - Challenging, as they're stored as a std::map
        for (const auto& ent : state.rv.statics) {
            TRACE_FUNCTION_F("Enumerate static " << ent.first);
            assert(ent.second->ptr);
            const auto& stat = *ent.second->ptr;
            const auto& pp = ent.second->pp;

            tv.visitType(pp.monomorph(tv.resolve, stat.type));
        }
        // - Constants need visiting, as they will be expanded
        for (const auto& ent : state.rv.constants) {
            TRACE_FUNCTION_F("Enumerate constant " << ent.first);
            assert(ent.second->ptr);
            const auto& stat = *ent.second->ptr;
            const auto& pp = ent.second->pp;

            tv.visitType(pp.monomorph(tv.resolve, stat.type));
        }
        for (const auto& ent : state.rv.vtables) {
            TRACE_FUNCTION_F("vtable " << ent.first);
            const auto& ty = ent.first.data.as_UfcsKnown().type;
            const auto& gpath = ent.first.data.as_UfcsKnown().trait;
            if (gpath.path == HIRSimplePath()) {
                ::std::vector<HIRTypeRef> tupleTys;
                tupleTys.push_back(state.crate.types.primitive(HIRCoreType::Usize));
                tupleTys.push_back(state.crate.types.primitive(HIRCoreType::Usize));
                tupleTys.push_back(state.crate.types.primitive(HIRCoreType::Usize)); // fn
                auto vtableTy = state.crate.types.tuple(std::move(tupleTys));
                tv.visitType(ty);
                tv.visitType(vtableTy);
                continue;
            }
            const auto& trait = state.crate.getTraitByPath(sp, gpath.path);

            const auto& vtableTySpath = trait.vtablePath;
            const auto& vtableRef = state.crate.getStructByPath(sp, vtableTySpath);
            // Copy the param set from the trait in the trait object
            HIRPathParams vtableParams = gpath.params.clone();
            // - Include associated types on bound
            for (const auto& tyIdx : trait.typeIndexes) {
                auto idx = tyIdx.second;
                if (vtableParams.types.size() <= idx) {
                    vtableParams.types.resize(idx + 1);
                }
                auto p = ent.first.clone();
                p.data.as_UfcsKnown().item = tyIdx.first;
                vtableParams.types[idx] = state.crate.types.path(mv$(p), {});
                tv.resolve.expandAssociatedTypes(sp, vtableParams.types[idx]);
            }
            DEBUG("VTable: " << vtableTySpath << vtableParams);

            tv.visitType(ty);
            tv.visitType(state.crate.types.path(HIRPath(HIRGenericPath(vtableTySpath, mv$(vtableParams))), &vtableRef));

            // If this is for a function pointer, visit all arguments
            // - `auto_impls.cpp` will generate a vtable shim for it (which requires argument types to be fully known)
            // NOTE: Assumes that the trait is one of the Fn* traits (doesn't matter if it isn't here)
            if (const auto* te = ty->opt_Function()) {
                for (const auto& t : te->argTypes) {
                    tv.visitType(t);
                }
                tv.visitType(te->rettype);

                if (gpath.params.types.size() >= 1) {
                    tv.visitType(gpath.params.types[0]);
                }
            }

            if (gpath.path == state.resolve.langFn() || gpath.path == state.resolve.langFnMut() || gpath.path == state.resolve.langFnOnce()) {
                tv.visitType(gpath.params.types[0]);
            }
        }
        for (const auto& ty : state.rv.autoCloneImpls) {
            tv.visitType(ty);
        }
        // A tuple struct or tuple variant used as a function value has its
        // wrapper generated by codegen, so the type it builds is reachable only
        // through this list. Without it the wrapper names a type that was never
        // emitted.
        constructorsVisited = state.rv.constructors.size();
        for (const auto& path : state.rv.constructors) {
            TRACE_FUNCTION_F("constructor " << path);
            if (path.path.components().size() > 1) {
                const auto& item = state.crate.getTypeitemByPath(sp, path.path, false, true);
                if (const auto* e = item.opt_Enum()) {
                    tv.visitType(state.crate.types.path(HIRPath(HIRGenericPath(path.path.parent(), path.params.clone())), e));
                    continue;
                }
            }
            const auto& str = state.crate.getStructByPath(sp, path.path);
            tv.visitType(state.crate.types.path(HIRPath(HIRGenericPath(path.path.clone(), path.params.clone())), &str));
        }

        constructorsAdded = false;
        for (unsigned int i = typesCount; i < state.rv.types.size(); i++) {
            const auto& ent = state.rv.types[i];
            // Shallow? Skip.
            if (ent.second) {
                continue;
            }
            const auto& ty = ent.first;
            TRACE_FUNCTION_F(ty);
            if (ty->is_Path()) {
                const auto& te = ty->as_Path();
                ASSERT_BUG(sp, te.path.data.is_Generic(), "Non-Generic type path after enumeration - " << ty);
                const auto& gp = te.path.data.as_Generic();
                const HIRTraitMarkings* markingsPtr = te.binding.getTraitMarkings();
                ASSERT_BUG(sp, markingsPtr, "Path binding not set correctly - " << ty);

                // If the type has a drop impl, and it's either defined in this crate or has params (and thus was monomorphised)
                if (markingsPtr->hasDropImpl && (gp.path.crateName() == state.crate.crateName || gp.params.hasParams())) {
                    // Add the Drop impl to the codegen list
                    TransEnumerateFillFromPathMono(state, HIRPath(ty, state.crate.getLangItemPath(sp, "drop"), "drop", HIRPathParams()));
                    constructorsAdded = true;
                }
            }

            if (const auto* ity = tv.resolve.isTypeOwnedBox(ty)) {
                // NOTE: Save the params before visiting, as the ASTType* might move as types are added, but the inner data won't move
                const auto& p = ty->as_Path().path.data.as_Generic().params;
                tv.visitType(ity);
            }
        }
        typesCount = state.rv.types.size();

        // Run queue
        TransEnumerateCommonPostRun(state);
        // The queue can name further constructors, whose types are then still
        // unvisited.
        if (state.rv.constructors.size() != constructorsVisited) {
            constructorsAdded = true;
        }
    } while (constructorsAdded);
}

namespace {
// Definitions generated from trans_ent_ptr.tu.
#include "trans_ent_ptr_tu.h"

    bool pathAlreadyEnumerated(const EnumState& state, const HIRPath& path) {
        return state.rv.functions.count(path) || state.rv.statics.count(path) || state.rv.constants.count(path) || state.rv.vtables.count(path);
    }

    void evaluateTranslationParams(const Span& sp, const WireBoard& wb, const HIRCrate& crate, const HIRGenericParams* defs, HIRPathParams& params) {
        if (params.values.empty()) {
            return;
        }

        ASSERT_BUG(sp, defs, "Missing const parameter definitions for " << params);
        ASSERT_BUG(sp, params.values.size() <= defs->values.size(), "Too many const parameters in " << params << " for " << defs->fmtArgs());
        for (size_t i = 0; i < params.values.size(); i++) {
            auto& value = params.values[i];
            if (value.is_Unevaluated()) {
                const HIRTypeData* type = defs->values[i].type;
                HIRTypeRef tmp;
                if (monomorphiseTypeNeeded(type)) {
                    // A const parameter's type may name the parameters before it
                    // (`const M: [T; N]`), and those are concrete by now. The
                    // list fills both slots because a definition indexes its own
                    // parameters as `I:n` or as `M:n` depending on the item.
                    MonomorphStatePtr ms(crate.types, nullptr, &params, &params);
                    type = tmp = ms.monomorphType(sp, type);
                    ASSERT_BUG(sp, !monomorphiseTypeNeeded(type), "Generic const parameter type " << type << " in " << defs->fmtArgs());
                }
                ConvertHIRConstantEvaluateConstGeneric(sp, wb, crate, type, value);
            }
            ASSERT_BUG(sp, value.is_Evaluated(), "Const parameter was not concrete at translation: " << value);
        }
    }

    void evaluateTranslationImplAndTraitParams(const Span& sp, const WireBoard& wb, const HIRCrate& crate, HIRPath& path, TransParams& pp) {
        evaluateTranslationParams(sp, wb, crate, pp.gdefImpl, pp.ppImpl);

        switch (path.data.tag()) {
            case HIRPathData::TAG_Generic: {
                break;
            }
            case HIRPathData::TAG_UfcsKnown: {
                auto& pe = path.data.as_UfcsKnown();
                // An empty trait path is the marker-only vtable sentinel. It
                // has no trait parameters to evaluate; the vtable enumerator
                // handles this representation directly.
                if (pe.trait.path != HIRSimplePath()) {
                    const auto& trait = crate.getTraitByPath(sp, pe.trait.path);
                    evaluateTranslationParams(sp, wb, crate, &trait.params, pe.trait.params);
                }
                break;
            }
            case HIRPathData::TAG_UfcsInherent: {
                auto& pe = path.data.as_UfcsInherent();
                evaluateTranslationParams(sp, wb, crate, pp.gdefImpl, pe.implParams);
                break;
            }
            case HIRPathData::TAG_UfcsUnknown: {
                BUG(sp, "UfcsUnknown at translation: " << path);
                break;
            }
        }
    }

    void evaluateTranslationItemParams(const Span& sp, const WireBoard& wb, const HIRCrate& crate, const HIRGenericParams& defs, HIRPath& path, TransParams& pp) {
        evaluateTranslationParams(sp, wb, crate, &defs, pp.ppMethod);

        switch (path.data.tag()) {
            case HIRPathData::TAG_Generic: {
                auto& pe = path.data.as_Generic();
                evaluateTranslationParams(sp, wb, crate, &defs, pe.params);
                break;
            }
            case HIRPathData::TAG_UfcsKnown: {
                auto& pe = path.data.as_UfcsKnown();
                evaluateTranslationParams(sp, wb, crate, &defs, pe.params);
                break;
            }
            case HIRPathData::TAG_UfcsInherent: {
                auto& pe = path.data.as_UfcsInherent();
                evaluateTranslationParams(sp, wb, crate, &defs, pe.params);
                break;
            }
            case HIRPathData::TAG_UfcsUnknown: {
                BUG(sp, "UfcsUnknown at translation: " << path);
                break;
            }
        }
    }

    void enumerateConstRelocations(EnumState& state, const HIRPathParams& params) {
        for (const auto& value : params.values) {
            if (const auto* evaluated = value.opt_Evaluated()) {
                TransEnumerateFillFromLiteral(state, **evaluated, TransParams(state.crate.types));
            }
        }
    }

    void enumerateConstRelocations(EnumState& state, const HIRPath& path, const TransParams& params) {
        enumerateConstRelocations(state, params.ppImpl);
        enumerateConstRelocations(state, params.ppMethod);
        switch (path.data.tag()) {
            case HIRPathData::TAG_Generic: {
                auto& pe = path.data.as_Generic();
                enumerateConstRelocations(state, pe.params);
                break;
            }
            case HIRPathData::TAG_UfcsKnown: {
                auto& pe = path.data.as_UfcsKnown();
                enumerateConstRelocations(state, pe.trait.params);
                enumerateConstRelocations(state, pe.params);
                break;
            }
            case HIRPathData::TAG_UfcsInherent: {
                auto& pe = path.data.as_UfcsInherent();
                enumerateConstRelocations(state, pe.params);
                enumerateConstRelocations(state, pe.implParams);
                break;
            }
            case HIRPathData::TAG_UfcsUnknown: {
                break;
            }
        }
    }

    EntPtr getEntFullpath(const Span& sp, const WireBoard& wb, const HIRCrate& crate, HIRPath& path, TransParams& params) {
        TRACE_FUNCTION_F(path);
        StaticTraitResolve resolve{wb, OpaqueReveal::All};

        if (path.data.is_UfcsInherent() && path.data.as_UfcsInherent().item == "#type_id") {
            return EntPtr::make_AutoGenerate({});
        }

        MonomorphState ms(crate.types);
        params.gdefImpl = nullptr;
        StaticTraitResolve::ResolvedTraitImplPath traitImplPath;
        auto ent = resolve.getValue(sp, path, ms, /*signature_only=*/false, &params.gdefImpl, &traitImplPath);
        if (traitImplPath.type) {
            auto& pe = path.data.as_UfcsKnown();
            pe.type = traitImplPath.type;
            pe.trait.params = mv$(traitImplPath.traitParams);
            params.selfType = pe.type;
        }
        if (ms.getImplParams()) {
            params.ppImpl = ms.getImplParams()->clone();
            if (params.ppImpl.hasParams()) {
                assert(params.gdefImpl);
            }
        }
        DEBUG(path << " = " << ent.tagStr() << " w/ impl" << params.ppImpl);
        switch (ent.tag()) {
default:
            TODO(sp, path << " was " << ent.tagStr());
            case TypeckValuePtr::TAG_NotYetKnown: {
                const auto* pe = &path.data.as_UfcsKnown();
                // Options:
                // - VTable
                if (pe->item == "vtable#") {
                    DEBUG("VTable, quick return");
                    return EntPtr::make_AutoGenerate({});
                }
                // - Auto-generated impl (the only trait impl was a bound)
                //  > Need to check if the trait is impled bounded
                bool foundBound = false;
                bool foundImpl = false;
                resolve.findImpl(sp, pe->trait.path, pe->trait.params, pe->type, [&](auto implRef, auto isFuzz) -> bool {
                    DEBUG("[get_ent_fullpath] Found " << implRef);
                    if (implRef.data.is_TraitImpl()) {
                        foundImpl = true;
                    } else {
                        foundBound = true;
                    }
                    return false;
                });
                if (foundBound) {
                    return EntPtr::make_AutoGenerate({});
                }
                DEBUG("NotYetKnown -> NotFound");
                return EntPtr();
            }
            case TypeckValuePtr::TAG_Function: {
                auto& f = ent.as_Function();
                // Check for trait provided bodies
                // - They need a little hack to ensure that monomorph is run
                if (const auto* pe = path.data.opt_UfcsKnown()) {
                    const auto& traitRef = crate.getTraitByPath(sp, pe->trait.path);
                    const auto& traitVi = traitRef.values.at(pe->item);

                    if (f == &traitVi.as_Function()) {
                        DEBUG("Default trait body");
                        params.forceMonomorphisation = true;
                    }
                }
                return EntPtr{f};
            }
            case TypeckValuePtr::TAG_Static: {
                auto& f = ent.as_Static();
                return EntPtr{f};
            }
            case TypeckValuePtr::TAG_Constant: {
                auto& f = ent.as_Constant();
                return EntPtr{f};
            }
            case TypeckValuePtr::TAG_StructConstructor: {
                auto& _ = ent.as_StructConstructor();
                return EntPtr::make_AutoGenerate({});
            }
            case TypeckValuePtr::TAG_EnumConstructor: {
                auto& _ = ent.as_EnumConstructor();
                return EntPtr::make_AutoGenerate({});
            }
        }
        UNREACHABLE();
    }
}

void TransEnumerateFillFromPath(EnumState& state, const HIRPath& path, const TransParams& pp) {
    auto pathMono = pp.monomorph(state.resolve, path);
    TransEnumerateFillFromPathMono(state, mv$(pathMono));
}

void TransEnumerateFillFromPathMono(EnumState& state, HIRPath pathMono) {
    Span sp;
    bindTranslationNominals(state.crate, pathMono);
    TRACE_FUNCTION_F(pathMono);
    ASSERT_BUG(sp, !monomorphisePathNeeded(pathMono), "Path " << pathMono << " is generic");
    // TODO: If already in the list, return early
    if (pathAlreadyEnumerated(state, pathMono)) {
        DEBUG("> Already enumerated");
        return;
    }

    TransParams subPp(state.crate.types, sp);
    switch (pathMono.data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& pe = pathMono.data.as_Generic();
            subPp.ppMethod = pe.params.clone();
            break;
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& pe = pathMono.data.as_UfcsKnown();
            subPp.ppMethod = pe.params.clone();
            subPp.selfType = pe.type;
            break;
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& pe = pathMono.data.as_UfcsInherent();
            subPp.ppMethod = pe.params.clone();
            subPp.ppImpl = pe.implParams.clone();
            subPp.selfType = pe.type;
            break;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            BUG(sp, "UfcsUnknown - " << pathMono);
            break;
        }
    }

    // A trait method whose Self is the corresponding trait object is always
    // dispatched through that object's vtable.  This also applies when the
    // method is used as a function item: resolving the path to a default trait
    // body here would turn `Trait::method` into a static call once Self is
    // inferred as `dyn Trait`.
    if (const auto* pe = pathMono.data.opt_UfcsKnown()) {
        if (const auto* tyDyn = pe->type->opt_TraitObject()) {
            if (pe->item != "vtable#" && tyDyn->trait.traitPtr->getVtableValueIndex(pe->trait, pe->item) > 0) {
                state.rv.traitObjectMethods.insert(mv$(pathMono));
                return;
            }
        }
    }

    // Get the item type
    // - Valid types are Function and Static
    auto itemRef = getEntFullpath(sp, state.resolve.board(), state.crate, pathMono, subPp);
    DEBUG("item_ref.tag_str() = " << itemRef.tagStr());
    DEBUG("sub_pp.pp_method = " << subPp.ppMethod);
    DEBUG("sub_pp.pp_impl = " << subPp.ppImpl);
    evaluateTranslationImplAndTraitParams(sp, state.resolve.board(), state.crate, pathMono, subPp);
    if (pathAlreadyEnumerated(state, pathMono)) {
        DEBUG("> Already enumerated after const evaluation");
        return;
    }

    auto activePath = state.activePaths.insert(pathMono.clone());
    if (!activePath.second) {
        DEBUG("> Already being enumerated");
        return;
    }
    STD_DEFER {
        state.activePaths.erase(activePath.first);
    };

    enumerateConstRelocations(state, pathMono, subPp);
    switch (itemRef.tag()) {
        case EntPtr::TAG_NotFound: {
            BUG(sp, "Item not found for " << pathMono);
            break;
        }
        case EntPtr::TAG_AutoGenerate: {
            if (pathAlreadyEnumerated(state, pathMono)) {
                DEBUG("> Already enumerated after const evaluation");
                return;
            }
            if (pathMono.data.is_Generic()) {
                // Leave generation of struct/enum constructors to codgen
                // TODO: Add to a list of required constructors
                state.rv.constructors.insert(mv$(pathMono.data.as_Generic()));
            }
            // - <T>::#type_id
            else if (pathMono.data.is_UfcsInherent() && pathMono.data.as_UfcsInherent().item == "#type_id") {
                state.rv.typeids.insert(pathMono.data.as_UfcsInherent().type);
            }
            // - <T as U>::#vtable
            else if (pathMono.data.is_UfcsKnown() && pathMono.data.as_UfcsKnown().item == "vtable#") {
                if (state.rv.addVtable(pathMono.clone(), TransParams(state.crate.types))) {
                    // Fill from the vtable
                    TransEnumerateFillFromVTable(state, mv$(pathMono), subPp);
                }
            }
            // - <(Trait) as Trait>::method
            else if (pathMono.data.is_UfcsKnown() && pathMono.data.as_UfcsKnown().type->is_TraitObject()) {
                state.rv.traitObjectMethods.insert(mv$(pathMono));
            }
            // - <fn(...) as Fn*>::call*
            else if (pathMono.data.is_UfcsKnown() && pathMono.data.as_UfcsKnown().type->is_Function() && (pathMono.data.as_UfcsKnown().trait.path == state.crate.getLangItemPathOpt("fn") || pathMono.data.as_UfcsKnown().trait.path == state.crate.getLangItemPathOpt("fn_mut") || pathMono.data.as_UfcsKnown().trait.path == state.crate.getLangItemPathOpt("fn_once"))) {
                // Must have been a dynamic dispatch request, just leave as-is
                // - However, ensure that all arguments are visited?
                //    state.rv.vi
            }
            // - <fn{...} as Fn*>::call*
            else if (pathMono.data.is_UfcsKnown() && pathMono.data.as_UfcsKnown().type->is_NamedFunction() && (pathMono.data.as_UfcsKnown().trait.path == state.crate.getLangItemPathOpt("fn") || pathMono.data.as_UfcsKnown().trait.path == state.crate.getLangItemPathOpt("fn_mut") || pathMono.data.as_UfcsKnown().trait.path == state.crate.getLangItemPathOpt("fn_once"))) {
                // Calling a non-dynamic function, need to visit that function
                TransEnumerateFillFromPath(state, pathMono.data.as_UfcsKnown().type->as_NamedFunction().path, subPp);
            }
            // - <fn(...) as FnPtr>::addr
            else if (pathMono.data.is_UfcsKnown() && pathMono.data.as_UfcsKnown().type->is_Function() && pathMono.data.as_UfcsKnown().trait.path == state.crate.getLangItemPathOpt("fn_ptr_trait")) {
                state.rv.autoFnptrImpls.insert(pathMono.data.as_UfcsKnown().type);
            }
            // <* as Clone>::clone
            else if (pathMono.data.is_UfcsKnown() && pathMono.data.as_UfcsKnown().trait == state.crate.getLangItemPathOpt("clone")) {
                const auto& pe = pathMono.data.as_UfcsKnown();
                ASSERT_BUG(sp, pe.item == "clone" || pe.item == "clone_from", "Unexpected Clone method called, " << pathMono);
                const auto& innerTy = pe.type;
                // If this is !Copy, then we need to ensure that the inner type's clone impls are also available
                ::StaticTraitResolve resolve{state.resolve.board(), OpaqueReveal::All};
                if (!resolve.typeIsCopy(sp, innerTy)) {
                    auto enumImpl = [&](const HIRTypeData* ity) {
                        if (!resolve.typeIsCopy(sp, ity)) {
                            auto innerPp = HIRPathParams();
                            TransEnumerateFillFromPathMono(state, HIRPath(ity, pe.trait.clone(), pe.item, mv$(innerPp)));
                        }
                    };
                    if (const auto* te = innerTy->opt_Tuple()) {
                        for (const auto& ity : *te) {
                            enumImpl(ity);
                        }
                    } else if (const auto* te = innerTy->opt_Array()) {
                        enumImpl(te->inner);
                    } else if (((*innerTy).is_Path() && ((*innerTy).as_Path().isClosure()))) {
                        const auto& gp = innerTy->as_Path().path.data.as_Generic();
                        const auto& str = state.crate.getStructByPath(sp, gp.path);
                        auto p = TransParams::newImpl(state.crate.types, sp, {}, gp.params.clone());
                        for (const auto& fld : str.data.as_Tuple()) {
                            HIRTypeRef tmp;
                            const auto& tyM = monomorphiseTypeNeeded(fld.ent) ? (tmp = p.monomorph(resolve, fld.ent)) : fld.ent;
                            enumImpl(tyM);
                        }
                    } else {
                        BUG(sp, "Unhandled magic clone in enumerate - " << innerTy);
                    }
                }
                // Add this type to a list of types that will have the impl auto-generated
                state.rv.autoCloneImpls.insert(innerTy);
                if (pe.item == "clone_from") {
                    state.rv.autoCloneFromImpls.insert(innerTy);
                }
            } else {
                BUG(sp, "AutoGenerate returned for unknown path type - " << pathMono);
            }
            break;
        }
        case EntPtr::TAG_Function: {
            auto& e = itemRef.as_Function();
            evaluateTranslationItemParams(sp, state.resolve.board(), state.crate, e->params, pathMono, subPp);
            if (pathAlreadyEnumerated(state, pathMono)) {
                DEBUG("> Already enumerated after const evaluation");
                return;
            }
            // Add this path (monomorphised) to the queue
            state.enumFcn(mv$(pathMono), *e, mv$(subPp));
            break;
        }
        case EntPtr::TAG_Static: {
            auto& e = itemRef.as_Static();
            evaluateTranslationItemParams(sp, state.resolve.board(), state.crate, e->params, pathMono, subPp);
            if (pathAlreadyEnumerated(state, pathMono)) {
                DEBUG("> Already enumerated after const evaluation");
                return;
            }
            if (auto* ptr = state.rv.addStatic(state.crate.types, mv$(pathMono))) {
                TransEnumerateFillFromStatic(state, *e, *ptr, mv$(subPp));
            }
            break;
        }
        case EntPtr::TAG_Constant: {
            auto& e = itemRef.as_Constant();
            evaluateTranslationItemParams(sp, state.resolve.board(), state.crate, e->params, pathMono, subPp);
            if (pathAlreadyEnumerated(state, pathMono)) {
                DEBUG("> Already enumerated after const evaluation");
                return;
            }
            switch (e->valueState) {
                case HIRConstant::ValueState::InProgress:
                    BUG(sp, "Constant still marked in-progress at translation: " << pathMono);
                case HIRConstant::ValueState::Unknown:
                    BUG(sp, "Unevaluated constant: " << pathMono);
                case HIRConstant::ValueState::Generic:
                    if (auto* slot = state.rv.addConst(state.crate.types, mv$(pathMono))) {
                        slot->ptr = e;
                        slot->pp = ::std::move(subPp);
                    }
                    break;
                case HIRConstant::ValueState::Known:
                    TransEnumerateFillFromLiteral(state, e->valueRes, subPp);
                    break;
            }
            break;
        }
    }
}

void TransEnumerateFillFromMIRLValue(MIREnumCache& state, const MIRLValue& lv) {
    if (lv.root.is_Static()) {
        state.insertPath(lv.root.as_Static());
    }
}

void TransEnumerateFillFromMIRConstant(MIREnumCache& state, const MIRConstant& c) {
    switch (c.tag()) {
        case MIRConstant::TAG_Int: {
            break;
        }
        case MIRConstant::TAG_Uint: {
            break;
        }
        case MIRConstant::TAG_Float: {
            break;
        }
        case MIRConstant::TAG_Bool: {
            break;
        }
        case MIRConstant::TAG_Bytes: {
            break;
        }
        case MIRConstant::TAG_StaticString: {
            break;
        }
        // String
        case MIRConstant::TAG_Encoded: {
            auto& ce = c.as_Encoded();
            for (const auto& reloc : ce.value.relocations) if (reloc.p) state.insertPath(*reloc.p);
            break;
        }
        case MIRConstant::TAG_Const: {
            auto& ce = c.as_Const();
            // - Check if this constant has a value of Defer
            state.insertPath(*ce.p);
            break;
        }
        case MIRConstant::TAG_Generic: {
            break;
        }
        case MIRConstant::TAG_Function: {
            auto& ce = c.as_Function();
            state.insertPath(*ce.p);
            break;
        }
        case MIRConstant::TAG_ItemAddr: {
            auto& ce = c.as_ItemAddr();
            if (ce) state.insertPath(*ce);
            break;
        }
    }
}

void TransEnumerateFillFromMIRParam(MIREnumCache& state, const MIRParam& p) {
    switch (p.tag()) {
        case MIRParam::TAG_LValue: {
            auto& e = p.as_LValue();
            TransEnumerateFillFromMIRLValue(state, e);
            break;
        }
        case MIRParam::TAG_Borrow: {
            auto& e = p.as_Borrow();
            TransEnumerateFillFromMIRLValue(state, e.val);
            break;
        }
        case MIRParam::TAG_Constant: {
            auto& e = p.as_Constant();
            TransEnumerateFillFromMIRConstant(state, e);
            break;
        }
    }
}

void TransEnumerateFillFromMIR(MIREnumCache& state, const MIRFunction& code) {
    TRACE_FUNCTION_F("");
    for (const auto& ty : code.locals) {
        visitTyWith(ty, [&state](const HIRTypeData* t) -> bool {
            if (const auto* te = t->opt_NamedFunction()) {
                state.insertPath(te->path);
            }
            return false;
        });
    }
    for (const auto& bb : code.blocks) {
        for (const auto& stmt : bb.statements) {
            switch (stmt.tag()) {
                case MIRStatement::TAG_Assign: {
                    auto& se = stmt.as_Assign();
                    DEBUG("- " << se.dst << " = " << se.src);
                    TransEnumerateFillFromMIRLValue(state, se.dst);
                    switch (se.src.tag()) {
                        case MIRRValue::TAG_Use: {
                            auto& e = se.src.as_Use();
                            TransEnumerateFillFromMIRLValue(state, e);
                            break;
                        }
                        case MIRRValue::TAG_Constant: {
                            auto& e = se.src.as_Constant();
                            TransEnumerateFillFromMIRConstant(state, e);
                            break;
                        }
                        case MIRRValue::TAG_SizedArray: {
                            auto& e = se.src.as_SizedArray();
                            TransEnumerateFillFromMIRParam(state, e.val);
                            break;
                        }
                        case MIRRValue::TAG_Borrow: {
                            auto& e = se.src.as_Borrow();
                            TransEnumerateFillFromMIRLValue(state, e.val);
                            break;
                        }
                        case MIRRValue::TAG_Cast: {
                            auto& e = se.src.as_Cast();
                            TransEnumerateFillFromMIRLValue(state, e.val);
                            break;
                        }
                        case MIRRValue::TAG_BinOp: {
                            auto& e = se.src.as_BinOp();
                            TransEnumerateFillFromMIRParam(state, e.valL); TransEnumerateFillFromMIRParam(state, e.valR);
                            break;
                        }
                        case MIRRValue::TAG_UniOp: {
                            auto& e = se.src.as_UniOp();
                            TransEnumerateFillFromMIRLValue(state, e.val);
                            break;
                        }
                        case MIRRValue::TAG_DstMeta: {
                            auto& e = se.src.as_DstMeta();
                            TransEnumerateFillFromMIRLValue(state, e.val);
                            break;
                        }
                        case MIRRValue::TAG_DstPtr: {
                            auto& e = se.src.as_DstPtr();
                            TransEnumerateFillFromMIRLValue(state, e.val);
                            break;
                        }
                        case MIRRValue::TAG_MakeDst: {
                            auto& e = se.src.as_MakeDst();
                            TransEnumerateFillFromMIRParam(state, e.ptrVal); TransEnumerateFillFromMIRParam(state, e.metaVal);
                            break;
                        }
                        case MIRRValue::TAG_Tuple: {
                            auto& e = se.src.as_Tuple();
                            for (const auto& val : e.vals) TransEnumerateFillFromMIRParam(state, val);
                            break;
                        }
                        case MIRRValue::TAG_Array: {
                            auto& e = se.src.as_Array();
                            for (const auto& val : e.vals) TransEnumerateFillFromMIRParam(state, val);
                            break;
                        }
                        case MIRRValue::TAG_UnionVariant: {
                            auto& e = se.src.as_UnionVariant();
                            TransEnumerateFillFromMIRParam(state, e.val);
                            break;
                        }
                        case MIRRValue::TAG_EnumVariant: {
                            auto& e = se.src.as_EnumVariant();
                            for (const auto& val : e.vals) TransEnumerateFillFromMIRParam(state, val);
                            break;
                        }
                        case MIRRValue::TAG_Struct: {
                            auto& e = se.src.as_Struct();
                            for (const auto& val : e.vals) TransEnumerateFillFromMIRParam(state, val);
                            break;
                        }
                    }
                    break;
                }
                case MIRStatement::TAG_Asm2: {
                    auto& e = stmt.as_Asm2();
                    for (auto& p : e.params) {
                    switch (p.tag()) {
                        case MIRAsmParam::TAG_Const: {
                            auto& v = p.as_Const();
                            TransEnumerateFillFromMIRConstant(state, v);
                            break;
                        }
                        case MIRAsmParam::TAG_Sym: {
                            auto& v = p.as_Sym();
                            state.insertPath(v);
                            break;
                        }
                        case MIRAsmParam::TAG_Reg: {
                            auto& v = p.as_Reg();
                            if (v.input) {
                                TransEnumerateFillFromMIRParam(state, *v.input);
                            }
                            if (v.output) {
                                TransEnumerateFillFromMIRLValue(state, *v.output);
                            }
                            break;
                        }
                        case MIRAsmParam::TAG_Label: {
                            break;
                        }
                    }
                    }
                    break;
                }
                case MIRStatement::TAG_Asm: {
                    auto& se = stmt.as_Asm();
                    DEBUG("- llvm_asm! ...");
                    for (const auto& v : se.inputs) {
                        TransEnumerateFillFromMIRLValue(state, v.second);
                    }
                    for (const auto& v : se.outputs) {
                        TransEnumerateFillFromMIRLValue(state, v.second);
                    }
                    break;
                }
                case MIRStatement::TAG_SetDropFlag: {
                    break;
                }
                case MIRStatement::TAG_SaveDropFlag: {
                    auto& se = stmt.as_SaveDropFlag();
                    TransEnumerateFillFromMIRLValue(state, se.slot);
                    break;
                }
                case MIRStatement::TAG_LoadDropFlag: {
                    auto& se = stmt.as_LoadDropFlag();
                    TransEnumerateFillFromMIRLValue(state, se.slot);
                    break;
                }
                case MIRStatement::TAG_ScopeEnd: {
                    break;
                }
            }
        }
        DEBUG("> " << bb.terminator);
        switch (bb.terminator.tag()) {
            case MIRTerminator::TAG_Incomplete: {
                break;
            }
            case MIRTerminator::TAG_Return: {
                break;
            }
            case MIRTerminator::TAG_UnwindResume: {
                break;
            }
            case MIRTerminator::TAG_UnwindTerminate: {
                break;
            }
            case MIRTerminator::TAG_Unreachable: {
                break;
            }
            case MIRTerminator::TAG_Goto: {
                break;
            }
            case MIRTerminator::TAG_If: {
                auto& e = bb.terminator.as_If();
                TransEnumerateFillFromMIRLValue(state, e.cond);
                break;
            }
            case MIRTerminator::TAG_Switch: {
                auto& e = bb.terminator.as_Switch();
                TransEnumerateFillFromMIRLValue(state, e.val);
                break;
            }
            case MIRTerminator::TAG_SwitchValue: {
                auto& e = bb.terminator.as_SwitchValue();
                TransEnumerateFillFromMIRLValue(state, e.val);
                break;
            }
            case MIRTerminator::TAG_Drop: {
                auto& e = bb.terminator.as_Drop();
                TransEnumerateFillFromMIRLValue(state, e.slot);
                break;
            }
            case MIRTerminator::TAG_Call: {
                auto& e = bb.terminator.as_Call();
                TransEnumerateFillFromMIRLValue(state, e.retVal); switch (e.fcn.tag()) {
                    case MIRCallTarget::TAG_Value: {
                        auto& e2 = e.fcn.as_Value();
                        TransEnumerateFillFromMIRLValue(state, e2);
                        break;
                    }
                    case MIRCallTarget::TAG_Path: {
                        auto& e2 = e.fcn.as_Path();
                        state.insertPath(e2);
                        break;
                    }
                    case MIRCallTarget::TAG_Intrinsic: {
                        auto& e2 = e.fcn.as_Intrinsic();
                        if (e2.name == "type_id") {
                            // Add <T>::#type_id to the enumerate list
                            state.insertTypeid(e2.params.types.at(0));
                        } else if (e2.name == "drop_in_place") {
                            // C lowering turns this intrinsic into static
                            // #drop_glue calls, so expose those dependencies
                            // to translation enumeration before codegen.
                            state.insertDestructorType(e2.params.types.at(0));
                        }
                        break;
                    }
                } for (const auto& arg : e.args) TransEnumerateFillFromMIRParam(state, arg);
                break;
            }
            case MIRTerminator::TAG_TailCall: {
                auto& e = bb.terminator.as_TailCall();
                switch (e.fcn.tag()) {
                    case MIRCallTarget::TAG_Value: {
                        auto& e2 = e.fcn.as_Value();
                        TransEnumerateFillFromMIRLValue(state, e2);
                        break;
                    }
                    case MIRCallTarget::TAG_Path: {
                        auto& e2 = e.fcn.as_Path();
                        state.insertPath(e2);
                        break;
                    }
                    case MIRCallTarget::TAG_Intrinsic: {
                        auto& e2 = e.fcn.as_Intrinsic();
                        if (e2.name == "type_id") state.insertTypeid(e2.params.types.at(0));
                        else if (e2.name == "drop_in_place") state.insertDestructorType(e2.params.types.at(0));
                        break;
                    }
                } for (const auto& arg : e.args) TransEnumerateFillFromMIRParam(state, arg);
                break;
            }
            case MIRTerminator::TAG_Asm2: {
                auto& e = bb.terminator.as_Asm2();
                for (const auto& p : e.params) { if (const auto* c = p.opt_Const()) TransEnumerateFillFromMIRConstant(state, *c); else if (const auto* s = p.opt_Sym()) state.insertPath(*s); else if (const auto* r = p.opt_Reg()) { if (r->input) TransEnumerateFillFromMIRParam(state, *r->input); if (r->output) TransEnumerateFillFromMIRLValue(state, *r->output); } }
                break;
            }
        }
    }
}

void TransEnumerateFillFromVTable(EnumState& state, HIRPath vtablePath, const TransParams& pp) {
    Span sp;
    const auto& type = vtablePath.data.as_UfcsKnown().type;
    const auto& traitPath = vtablePath.data.as_UfcsKnown().trait;
    if (traitPath == HIRSimplePath()) {
        // TODO: Ensure that the drop glue is available
        return;
    }
    const auto& tr = state.crate.getTraitByPath(Span(), traitPath.path);

    ASSERT_BUG(sp, !type->is_Slice(), "Getting vtable for unsized type - " << vtablePath);
    ASSERT_BUG(sp, !type->is_TraitObject(), "Getting vtable for unsized type - " << vtablePath);

    auto monomorphCbTrait = MonomorphStatePtr(state.crate.types, type, &traitPath.params, nullptr);
    for (const auto& m : tr.valueIndexes) {
        DEBUG("- " << m.second.first << " = " << m.second.second << " :: " << m.first);
        auto gpath = monomorphCbTrait.monomorphGenericpath(sp, m.second.second, false);
        const auto& fcn = state.crate.getTraitByPath(sp, gpath.path).values.at(m.first).as_Function();
        auto methodPath = HIRPath(type, gpath.clone(), m.first, HIRPathParams());
        state.resolve.expandAssociatedTypesPath(sp, methodPath);
        TransEnumerateFillFromPathMono(state, methodPath.clone());
    }
    for (const auto& ptPath : tr.allParentTraits) {
        ASSERT_BUG(sp, ptPath.traitPtr, "Unset trait pointer - " << ptPath);
        const auto& pt = *ptPath.traitPtr;
        if (pt.vtablePath != HIRSimplePath()) {
            auto ptMono = MonomorphStatePtr(state.crate.types, type, &traitPath.params, nullptr).monomorphGenericpath(sp, ptPath.path);
            auto ptVtablePath = HIRPath(type, mv$(ptMono), vtablePath.data.as_UfcsKnown().item);
            state.rv.addVtable(mv$(ptVtablePath), TransParams(state.crate.types));
            // No need to recurse.
        }
    }
}

void TransEnumerateFillFromLiteral(EnumState& state, const EncodedLiteral& lit, const TransParams& pp) {
    for (const auto& r : lit.relocations) {
        if (r.p) {
            // TODO: Replace lifetimes
            TransEnumerateFillFromPath(state, *r.p, pp);
        }
    }
}

void TransEnumerateFillFromFunction(EnumState& state, const HIRPath& p, const HIRFunction& function, const TransParams& pp) {
    TRACE_FUNCTION_F("Function " << p << " pp=" << pp.ppImpl << " + " << pp.ppMethod);
    if (!function.code.mir) {
        // External.
        if (function.linkage.name != "") {
            // Search for a function with the same linkage name anywhere in the loaded crates
            auto it = state.linkFunctions.find(function.linkage.name);
            if (it != state.linkFunctions.end()) {
                state.enumFcn(HIRPath(it->second.first), *it->second.second, TransParams(state.crate.types, pp.sp));
            }
        }
    } else if (state.origList) {
        const auto* transFcn = state.origList->findFunction(p);
        if (transFcn) {
            if (transFcn->monomorphised.code) {
                DEBUG("Monomorphised");
                MIREnumCache ec;
                TransEnumerateFillFromMIR(ec, *transFcn->monomorphised.code);
                ec.apply(state, pp);
            } else if (transFcn->ptr->code.mir) {
                DEBUG("Concrete");
                MIREnumCache ec;
                TransEnumerateFillFromMIR(ec, *transFcn->ptr->code.mir);
                ec.apply(state, pp);
            } else {
                DEBUG("No code");
            }
        } else {
            ASSERT_BUG(Span(), transFcn, "Missing " << p << " in input TransList?");
        }
    } else {
        const auto& mirFcn = *function.code.mir;
        if (!mirFcn.transEnumState) {
            auto* esp = new MIREnumCache();
            TransEnumerateFillFromMIR(*esp, *function.code.mir);
            mirFcn.transEnumState = MIREnumCachePtr(esp);
        }
        // TODO: Ensure that all types have drop glue generated too? (Iirc this is unconditional currently)
        mirFcn.transEnumState->apply(state, pp);
    }
}

void TransEnumerateFillFromStatic(EnumState& state, const HIRStatic& item, TransListStatic& outStat, TransParams pp) {
    if (item.params.isGeneric()) {
        MIREnumCache es;
        TransEnumerateFillFromMIR(es, *item.value.mir);
        es.apply(state, pp);
    } else if (item.type->is_Infer()) {
        BUG(Span(), "Enumerating static with no assigned type (unused elevated literal)");
    } else if (item.valueGenerated) {
        TransEnumerateFillFromLiteral(state, item.valueRes, pp);
    }
    outStat.ptr = &item;
    outStat.pp = mv$(pp);
}

// Bodies of the generated local unions (see trans_ent_ptr.tu).
#include "trans_ent_ptr_tu.cpp"
