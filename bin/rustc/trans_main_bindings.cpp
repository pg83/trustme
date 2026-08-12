#include "trans_main_bindings.h"
#include "trans_main_bindings.h"

#include "hir_hir.h"
#include "mir_mir.h"
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
        ::HIR::Crate& crate;
        StaticTraitResolve resolve;
        const TransList& transList;
        ::std::deque<::HIR::TypeRef> todoList;
        ::std::set<::HIR::TypeRef> doneList;

        ::HIR::SimplePath langClone;

        State(::HIR::Crate& crate, const TransList& transList)
            : crate(crate)
            , resolve(crate)
            , transList(transList)
        {
            langClone = crate.getLangItemPathOpt("clone");
        }

        void enqueueType(const ::HIR::TypeData* ty) {
            if (this->transList.autoCloneImpls.count(ty) == 0 && this->doneList.count(ty) == 0) {
                this->doneList.insert(ty);
                this->todoList.push_back(ty);
            }
        }
    };

    const RcString rcstringCloneLower = RcString::newInterned("clone");
    const RcString rcstringDrop = RcString::newInterned("drop");
    const RcString rcstringSelfLower = RcString::newInterned("self");
    const RcString rcstringDropGlue = RcString::newInterned("#drop_glue");
}

namespace {
    struct CloneCleanupState {
        ::std::vector<::MIR::BasicBlockId> calls;
        ::std::vector<::std::pair<::MIR::LValue, unsigned>> values;
    };

    ::MIR::BasicBlock& cloneOpenBlock(::MIR::Function& mirFcn) {
        if (mirFcn.blocks.empty() || !mirFcn.blocks.back().terminator.is_Incomplete()) {
            mirFcn.blocks.push_back(::MIR::BasicBlock());
        }
        return mirFcn.blocks.back();
    }

    ::MIR::Param cloneField(const State& state, const Span& sp, ::MIR::Function& mirFcn, CloneCleanupState& cleanup, const ::HIR::TypeData* subty, ::MIR::LValue fldLvalue) {
        if (state.resolve.typeIsCopy(sp, subty)) {
            return ::std::move(fldLvalue);
        } else {
            const auto& langClone = state.resolve.crate.getLangItemPath(sp, "clone");
            // Allocate to locals (one for the `&T`, the other for the cloned `T`)
            auto borrowLv = ::MIR::LValue::newLocal(mirFcn.locals.size());
            mirFcn.locals.push_back(state.crate.types.borrow(::HIR::BorrowType::Shared, subty));
            auto resLv = ::MIR::LValue::newLocal(mirFcn.locals.size());
            mirFcn.locals.push_back(subty);
            const auto dropFlag = static_cast<unsigned>(mirFcn.dropFlags.size());
            mirFcn.dropFlags.push_back(false);

            // Call `<T as Clone>::clone`, passing a borrow of the field
            auto& bb = cloneOpenBlock(mirFcn);
            bb.statements.push_back(::MIR::Statement::make_Assign({borrowLv.clone(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, mv$(fldLvalue)})}));
            ::HIR::PathParams pp;
            pp.mLifetimes.push_back(HIR::LifetimeRef(1 * 256 + 0)); // 'M:0
            const auto callBlock = static_cast<::MIR::BasicBlockId>(mirFcn.blocks.size() - 1);
            const auto retBlock = static_cast<::MIR::BasicBlockId>(mirFcn.blocks.size());
            bb.terminator = ::MIR::Terminator::make_Call({retBlock, ::MIR::UnwindAction::make_Continue({}), resLv.clone(), ::MIR::CallTarget(::HIR::Path(subty, langClone, rcstringCloneLower, std::move(pp))), ::makeVec1<::MIR::Param>(::std::move(borrowLv))});
            cleanup.calls.push_back(callBlock);
            cleanup.values.push_back(::std::make_pair(resLv.clone(), dropFlag));

            mirFcn.blocks.push_back(::MIR::BasicBlock());
            mirFcn.blocks.back().statements.push_back(::MIR::Statement::make_SetDropFlag({dropFlag, true, ~0u}));

            // Save the output of the `clone` call
            return ::std::move(resLv);
        }
    }

    void appendCloneCleanup(::MIR::Function& mirFcn, const CloneCleanupState& cleanup) {
        if (cleanup.calls.empty()) {
            return;
        }

        const auto cleanupStart = static_cast<::MIR::BasicBlockId>(mirFcn.blocks.size());
        const auto resume = static_cast<::MIR::BasicBlockId>(cleanupStart + cleanup.values.size());
        for (auto it = cleanup.values.rbegin(); it != cleanup.values.rend(); ++it) {
            ::MIR::BasicBlock block;
            block.isCleanup = true;
            block.terminator = ::MIR::Terminator::make_Drop({
                ::MIR::eDropKind::DEEP,
                it->first.clone(),
                it->second,
                static_cast<::MIR::BasicBlockId>(mirFcn.blocks.size() + 1),
                ::MIR::UnwindAction::make_Terminate({}),
            });
            mirFcn.blocks.push_back(mv$(block));
        }
        assert(mirFcn.blocks.size() == resume);
        ::MIR::BasicBlock resumeBlock;
        resumeBlock.isCleanup = true;
        resumeBlock.terminator = ::MIR::Terminator::make_UnwindResume({});
        mirFcn.blocks.push_back(mv$(resumeBlock));

        for (const auto call : cleanup.calls) {
            assert(mirFcn.blocks.at(call).terminator.is_Call());
            mirFcn.blocks[call].terminator.as_Call().unwind = ::MIR::UnwindAction::make_Cleanup(cleanupStart);
        }
    }
}

void TransAutoImplClone(State& state, ::HIR::TypeRef ty) {
    Span sp;
    TRACE_FUNCTION_F(ty);

    // Create MIR
    ::MIR::Function mirFcn;
    if (state.resolve.typeIsCopy(sp, ty)) {
        ::MIR::BasicBlock bb;
        bb.statements.push_back(::MIR::Statement::make_Assign({::MIR::LValue::newReturn(), ::MIR::RValue::make_Use(::MIR::LValue::newDeref(::MIR::LValue::newArgument(0)))}));
        bb.terminator = ::MIR::Terminator::make_Return({});
        mirFcn.blocks.push_back(::std::move(bb));
    } else {
        TU_MATCH_HDRA( ((*ty)), {)
        default:
            TODO(sp, "auto Clone for " << ty << " - Unknown and not Copy");
            TU_ARMA(Path, te) {
                if (te.isClosure()) {
                    const auto& gp = te.path.mData.as_Generic();
                    const auto& str = state.resolve.crate.getStructByPath(sp, gp.mPath);
                    auto p = TransParams::newImpl(state.crate.types, sp, ty, gp.mParams.clone());
                    CloneCleanupState cleanup;
                    ::std::vector<::MIR::Param> values;
                    values.reserve(str.mData.as_Tuple().size());
                    for (const auto& fld : str.mData.as_Tuple()) {
                        ::HIR::TypeRef tmp;
                        const auto& tyM = monomorphiseTypeNeeded(fld.ent) ? (tmp = p.monomorph(state.resolve, fld.ent)) : fld.ent;
                        auto fldLvalue = ::MIR::LValue::newField(::MIR::LValue::newDeref(::MIR::LValue::newArgument(0)), static_cast<unsigned>(values.size()));
                        values.push_back(cloneField(state, sp, mirFcn, cleanup, tyM, mv$(fldLvalue)));
                    }
                    // Construct the result value
                    auto& bb = cloneOpenBlock(mirFcn);
                    bb.statements.push_back(::MIR::Statement::make_Assign({::MIR::LValue::newReturn(), ::MIR::RValue::make_Struct({gp.clone(), mv$(values)})}));
                    bb.terminator = ::MIR::Terminator::make_Return({});
                    appendCloneCleanup(mirFcn, cleanup);
                } else {
                    TODO(sp, "auto Clone for " << ty << " - Unknown and not Copy");
                }
            }
            TU_ARMA(Array, te) {
                ASSERT_BUG(sp, te.size.as_Known() < 256, "TODO: Is more than 256 elements sane for auto-generated non-Copy Clone impl? " << ty);
                CloneCleanupState cleanup;
                ::std::vector<::MIR::Param> values;
                values.reserve(te.size.as_Known());
                for (size_t i = 0; i < te.size.as_Known(); i++) {
                    auto fldLvalue = ::MIR::LValue::newField(::MIR::LValue::newDeref(::MIR::LValue::newArgument(0)), static_cast<unsigned>(values.size()));
                    values.push_back(cloneField(state, sp, mirFcn, cleanup, te.inner, mv$(fldLvalue)));
                }
                // Construct the result
                auto& bb = cloneOpenBlock(mirFcn);
                bb.statements.push_back(::MIR::Statement::make_Assign({::MIR::LValue::newReturn(), ::MIR::RValue::make_Array({mv$(values)})}));
                bb.terminator = ::MIR::Terminator::make_Return({});
                appendCloneCleanup(mirFcn, cleanup);
            }
            TU_ARMA(Tuple, te) {
                assert(te.size() > 0);

                CloneCleanupState cleanup;
                ::std::vector<::MIR::Param> values;
                values.reserve(te.size());
                // For each field of the tuple, create a clone (either using Copy if posible, or calling Clone::clone)
                for (const auto& subty : te) {
                    auto fldLvalue = ::MIR::LValue::newField(::MIR::LValue::newDeref(::MIR::LValue::newArgument(0)), static_cast<unsigned>(values.size()));
                    values.push_back(cloneField(state, sp, mirFcn, cleanup, subty, mv$(fldLvalue)));
                }

                // Construct the result tuple
                auto& bb = cloneOpenBlock(mirFcn);
                bb.statements.push_back(::MIR::Statement::make_Assign({::MIR::LValue::newReturn(), ::MIR::RValue::make_Tuple({mv$(values)})}));
                bb.terminator = ::MIR::Terminator::make_Return({});
                appendCloneCleanup(mirFcn, cleanup);
            }
        }
    }

    // Function
    ::HIR::Function fcn{
        ::HIR::Function::Receiver::BorrowShared,
        ::HIR::GenericParams{},
        /*m_args=*/::makeVec1(::std::make_pair(::HIR::Pattern(::HIR::PatternBinding(false, ::HIR::PatternBinding::Type::Move, rcstringSelfLower, 0), ::HIR::Pattern::Data::make_Any({})), state.crate.types.borrow(::HIR::BorrowType::Shared, ty))),
        /*m_return=*/ty,
        ::HIR::ExprPtr{}
    };
    fcn.mParams.mLifetimes.push_back(HIR::LifetimeDef()); // 'M:0 - for the `&self` argument
    fcn.mCode.mir = ::MIR::FunctionPointer(new ::MIR::Function(mv$(mirFcn)));

    // Impl
    ::HIR::TraitImpl impl;
    impl.mType = mv$(ty);
    impl.methods.insert(::std::make_pair(rcstringCloneLower, ::HIR::TraitImpl::ImplEnt<::HIR::Function>{false, ::std::move(fcn)}));

    // Add impl to the crate
    auto& list = state.crate.traitImpls[state.langClone].getListForTypeMut(impl.mType);
    list.push_back(box$(impl));
    state.crate.allTraitImpls[state.langClone].getListForTypeMut(list.back()->mType).push_back(list.back().get());
}

namespace {

    struct Builder {
        const State& state;
        MIR::Function& mir;
        const MIR::LValue self;

        Builder(const State& state, MIR::Function& mir)
            : state(state)
            , mir(mir)
            , self(MIR::LValue::newArgument(0))
        {
            mir.blocks.push_back(MIR::BasicBlock());
        }

        MIR::LValue addLocal(HIR::TypeRef ty) {
            auto rv = mir.locals.size();
            mir.locals.push_back(mv$(ty));
            return MIR::LValue::newLocal(rv);
        }

        MIR::LValue inTemporary(HIR::TypeRef ty, MIR::RValue val) {
            auto rv = addLocal(mv$(ty));
            pushStmtAssign(rv.clone(), mv$(val));
            return rv;
        }

        void ensureOpen() {
            if (!mir.blocks.back().terminator.is_Incomplete()) {
                mir.blocks.push_back(MIR::BasicBlock());
            }
        }

        void pushStmt(MIR::Statement s) {
            ensureOpen();
            mir.blocks.back().statements.push_back(mv$(s));
        }

        void pushStmtAssign(MIR::LValue lv, MIR::RValue rv) {
            this->pushStmt(MIR::Statement::make_Assign({mv$(lv), mv$(rv)}));
        }

        MIR::BasicBlockId pushStmtDrop(MIR::LValue lv) {
            ensureOpen();
            const auto dropBlock = static_cast<MIR::BasicBlockId>(mir.blocks.size() - 1);
            const auto next = static_cast<MIR::BasicBlockId>(mir.blocks.size());
            terminateBlock(MIR::Terminator::make_Drop({MIR::eDropKind::DEEP, mv$(lv), ~0u, next, MIR::UnwindAction::make_Continue({})}));
            mir.blocks.push_back(MIR::BasicBlock());
            return dropBlock;
        }

        void pushDropSequence(::std::vector<MIR::LValue> values, MIR::BasicBlockId customDropCall = ~0u) {
            if (values.empty()) {
                return;
            }

            // Lay the cleanup suffixes out before the normal chain.  A panic
            // from field N starts at field N+1; a panic from the user's Drop
            // implementation starts at field zero.  Cleanup drops terminate
            // on a second panic.
            ensureOpen();
            const auto entry = static_cast<MIR::BasicBlockId>(mir.blocks.size() - 1);
            terminateBlock(MIR::Terminator::make_Goto(~0u));

            const size_t cleanupFirst = customDropCall == ~0u ? 1 : 0;
            const auto cleanupStart = static_cast<MIR::BasicBlockId>(mir.blocks.size());
            const auto cleanupBlock = [&](size_t field) {
                assert(field >= cleanupFirst && field < values.size());
                return static_cast<MIR::BasicBlockId>(cleanupStart + field - cleanupFirst);
            };

            if (cleanupFirst < values.size()) {
                const auto resume = static_cast<MIR::BasicBlockId>(cleanupStart + values.size() - cleanupFirst);
                for (size_t i = cleanupFirst; i < values.size(); i++) {
                    MIR::BasicBlock block;
                    block.isCleanup = true;
                    const auto target = i + 1 < values.size() ? cleanupBlock(i + 1) : resume;
                    block.terminator = MIR::Terminator::make_Drop({
                        MIR::eDropKind::DEEP,
                        values[i].clone(),
                        ~0u,
                        target,
                        MIR::UnwindAction::make_Terminate({}),
                    });
                    mir.blocks.push_back(mv$(block));
                }
                MIR::BasicBlock resumeBlock;
                resumeBlock.isCleanup = true;
                resumeBlock.terminator = MIR::Terminator::make_UnwindResume({});
                mir.blocks.push_back(mv$(resumeBlock));
            }

            const auto normalStart = static_cast<MIR::BasicBlockId>(mir.blocks.size());
            mir.blocks[entry].terminator.as_Goto() = normalStart;
            for (size_t i = 0; i < values.size(); i++) {
                MIR::BasicBlock block;
                const auto target = static_cast<MIR::BasicBlockId>(normalStart + i + 1);
                auto unwind = i + 1 < values.size() ? MIR::UnwindAction::make_Cleanup(cleanupBlock(i + 1)) : MIR::UnwindAction::make_Continue({});
                block.terminator = MIR::Terminator::make_Drop({
                    MIR::eDropKind::DEEP,
                    values[i].clone(),
                    ~0u,
                    target,
                    mv$(unwind),
                });
                mir.blocks.push_back(mv$(block));
            }
            mir.blocks.push_back(MIR::BasicBlock());

            if (customDropCall != ~0u) {
                assert(mir.blocks.at(customDropCall).terminator.is_Call());
                mir.blocks[customDropCall].terminator.as_Call().unwind = MIR::UnwindAction::make_Cleanup(cleanupBlock(0));
            }
        }

        void terminateBlock(MIR::Terminator term) {
            assert(mir.blocks.back().terminator.is_Incomplete());
            mir.blocks.back().terminator = mv$(term);
        }

        void terminateCall(MIR::LValue rv, MIR::CallTarget tgt, std::vector<MIR::Param> args, MIR::BasicBlockId bbRet, MIR::BasicBlockId bbPanic) {
            this->terminateBlock(MIR::Terminator::make_Call({bbRet, MIR::UnwindAction::make_Cleanup(bbPanic), mv$(rv), mv$(tgt), mv$(args)}));
        }

        MIR::BasicBlockId pushCallDrop(const HIR::TypeData* ty) {
            // Get a `&mut *self`
            auto borrowLv = this->addLocal(state.crate.types.borrow(HIR::BorrowType::Unique, ty));
            this->pushStmtAssign(borrowLv.clone(), MIR::RValue::make_Borrow({HIR::BorrowType::Unique, false, ::MIR::LValue::newDeref(this->self.clone())}));

            ensureOpen();
            const auto callBlock = static_cast<MIR::BasicBlockId>(mir.blocks.size() - 1);
            const auto retBlock = static_cast<MIR::BasicBlockId>(mir.blocks.size());
            this->terminateBlock(
                MIR::Terminator::make_Call({
                    retBlock,
                    MIR::UnwindAction::make_Continue({}),
                    MIR::LValue::newReturn(),
                    ::HIR::Path(ty, state.resolve.mLangDrop, rcstringDrop),
                    makeVec1<MIR::Param>(mv$(borrowLv)),
                })
            );
            mir.blocks.push_back(MIR::BasicBlock());
            return callBlock;
        }
    };

    MIR::LValue derefBox(MIR::LValue box) {
        auto innerPtr = ::MIR::LValue::newField(::MIR::LValue::newField(mv$(box), 0), 0);
        innerPtr = ::MIR::LValue::newField(std::move(innerPtr), 0);
        return ::MIR::LValue::newDeref(std::move(innerPtr));
    }

    ::MIR::LValue getUnitPtr(const Span& sp, Builder& mutator, ::HIR::TypeRef ty, ::MIR::LValue lv, ::MIR::LValue& outInnerPtr) {
        if (ty->is_Path()) {
            const auto& te = ty->as_Path();
            ASSERT_BUG(sp, te.binding.is_Struct(), "");
            const auto& tyPath = te.path.mData.as_Generic();
            const auto& str = *te.binding.as_Struct();
            ::HIR::TypeRef tmp;
            auto monomorph = [&](const auto& t) {
                return MonomorphStatePtr(mutator.state.crate.types, nullptr, &tyPath.mParams, nullptr).monomorphType(sp, t);
            };
            ::std::vector<::MIR::Param> vals;
            TU_MATCH_HDRA( (str.mData), {)
            TU_ARMA(Unit, se) {
                }
                TU_ARMA(Tuple, se) {
                    for (unsigned int i = 0; i < se.size(); i++) {
                        auto val = ::MIR::LValue::newField((i == se.size() - 1 ? mv$(lv) : lv.clone()), i);
                        if (i == str.structMarkings.coerceUnsizedIndex) {
                            vals.push_back(getUnitPtr(sp, mutator, monomorph(se[i].ent), mv$(val), outInnerPtr));
                        } else {
                            vals.push_back(mv$(val));
                        }
                    }
                }
                TU_ARMA(Named, se) {
                    for (unsigned int i = 0; i < se.size(); i++) {
                        auto val = ::MIR::LValue::newField((i == se.size() - 1 ? mv$(lv) : lv.clone()), i);
                        if (i == str.structMarkings.coerceUnsizedIndex) {
                            vals.push_back(getUnitPtr(sp, mutator, monomorph(se[i].ty), mv$(val), outInnerPtr));
                        } else {
                            vals.push_back(mv$(val));
                        }
                    }
                }
            }

            auto newPath = tyPath.clone();
            return mutator.inTemporary( mv$(ty), ::MIR::RValue::make_Struct({ mv$(newPath), mv$(vals) }) );
        } else if (ty->is_Borrow() || ty->is_Pointer()) {
            outInnerPtr = lv.clone();
            return mutator.inTemporary(mutator.state.crate.types.pointer(::HIR::BorrowType::Shared, mutator.state.crate.types.unit()), ::MIR::RValue::make_DstPtr({mv$(lv)}));
        } else {
            BUG(sp, "Unexpected type coerce_unsize in receiver - " << ty);
        }
    }
}

void TransAutoImpls(::HIR::Crate& crate, TransList& transList) {
    State state{crate, transList};

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

            auto p = ::HIR::Path(ty, ::HIR::GenericPath(state.langClone), "clone");
            //DEBUG("add_function(" << p << ")");
            auto e = transList.addFunction(crate.types, ::std::move(p));

            const auto* implList = implListIt->second.getListForType(ty);
            ASSERT_BUG(Span(), implList, "No impl list of Clone for " << ty);
            auto& impl = **::std::find_if(implList->begin(), implList->end(), [&](const auto& i) {
                return i->mType == ty;
            });
            assert(impl.methods.size() == 1);
            e->ptr = &impl.methods.begin()->second.data;
        }
    }

    if (!transList.autoFnptrImpls.empty()) {
        const auto& langFnPtr = crate.getLangItemPath(Span(), "fn_ptr_trait");
        for (const auto& ty : transList.autoFnptrImpls) {
            auto outTy = state.crate.types.pointer(HIR::BorrowType::Shared, state.crate.types.unit());
            ::MIR::Function mirFcn;

            ::MIR::BasicBlock bb;
            bb.statements.push_back(::MIR::Statement::make_Assign({::MIR::LValue::newReturn(), ::MIR::RValue::make_Cast({::MIR::LValue::newArgument(0), outTy})}));
            bb.terminator = ::MIR::Terminator::make_Return({});
            mirFcn.blocks.push_back(::std::move(bb));

            // Function
            // `fn addr(self) -> usize;`
            ::HIR::Function fcn{
                ::HIR::Function::Receiver::Value,
                ::HIR::GenericParams{},
                /*m_args=*/::makeVec1(::std::make_pair(::HIR::Pattern(::HIR::PatternBinding(false, ::HIR::PatternBinding::Type::Move, rcstringSelfLower, 0), ::HIR::Pattern::Data::make_Any({})), ty)),
                /*m_return=*/std::move(outTy),
                ::HIR::ExprPtr{}
            };
            fcn.mCode.mir = ::MIR::FunctionPointer(new ::MIR::Function(mv$(mirFcn)));

            // Impl
            ::HIR::TraitImpl impl;
            impl.mType = ty;
            impl.methods.insert(::std::make_pair(RcString::newInterned("addr"), ::HIR::TraitImpl::ImplEnt<::HIR::Function>{false, ::std::move(fcn)}));

            // Add impl to the crate
            auto& list = state.crate.traitImpls[langFnPtr].getListForTypeMut(impl.mType);
            list.push_back(box$(impl));
            state.crate.allTraitImpls[langFnPtr].getListForTypeMut(list.back()->mType).push_back(list.back().get());

            // - Add this function to the TransList

            {
                auto p = ::HIR::Path(ty, ::HIR::GenericPath(langFnPtr), "addr");
                auto e = transList.addFunction(crate.types, ::std::move(p));

                auto& impl = *list.back();
                assert(impl.methods.size() == 1);
                e->ptr = &impl.methods.begin()->second.data;
            }
        }
    }

    // Trait object methods
    {
        TRACE_FUNCTION_F("Trait object methods");
        transList.autoFunctions.reserve(transList.autoFunctions.size() + transList.traitObjectMethods.size());
        for (const auto& path : transList.traitObjectMethods) {
            DEBUG(path);
            static Span sp;
            const auto& pe = path.mData.as_UfcsKnown();
            const auto& traitPath = pe.trait;
            const auto& name = pe.item;
            const auto& tyDyn = pe.type->as_TraitObject();

            const auto& trait = crate.getTraitByPath(sp, traitPath.mPath);
            const auto& fcnDef = trait.values.at(name).as_Function();

            // Get the vtable index for this function
            unsigned vtableIdx = tyDyn.mTrait.traitPtr->getVtableValueIndex(traitPath, name);
            ASSERT_BUG(sp, vtableIdx > 0, "Calling method '" << name << "' from " << traitPath << " through " << pe.type << " which isn't in the vtable");

            auto pp = fcnDef.mParams.makeNopParams(crate.types, 1, true);
            MonomorphStatePtr ms(crate.types, pe.type, &traitPath.mParams, &pp);

            HIR::Function newFcn;
            newFcn.returnType = ms.monomorphType(sp, fcnDef.returnType);
            state.resolve.expandAssociatedTypes(sp, newFcn.returnType);
            for (const auto& arg : fcnDef.mArgs) {
                newFcn.mArgs.push_back(std::make_pair(HIR::Pattern(), ms.monomorphType(sp, arg.second)));
                state.resolve.expandAssociatedTypes(sp, newFcn.mArgs.back().second);
            }
            ASSERT_BUG(sp, !newFcn.mArgs.empty(), "Trait object method with no arguments?!");

            newFcn.mCode.mir = MIR::FunctionPointer(new MIR::Function());
            Builder builder(state, *newFcn.mCode.mir);

            MIR::LValue lvSelf = MIR::LValue::newArgument(0);
            MIR::LValue lvPtr;
            // ---
            // bb0:
            //   _1 = DstPtr a1
            switch (fcnDef.receiver) {
                case HIR::Function::Receiver::Value: {
                    // By-value trait object dispatch
                    // - Receiver should be a `&move` (BUT, does the caller know this?)
                    // - MIR Cleanup should fix that (after monomoprh)
                    auto& selfTy = newFcn.mArgs.front().second;
                    selfTy = crate.types.borrow(HIR::BorrowType::Owned, selfTy);
                    lvPtr = builder.addLocal(crate.types.borrow(HIR::BorrowType::Owned, crate.types.unit()));
                    builder.pushStmtAssign(lvPtr.clone(), MIR::RValue::make_DstPtr({lvSelf.clone()}));
                    DEBUG("<dyn " << traitPath << ">::" << name << " - By-Value");
                } break;
                case HIR::Function::Receiver::BorrowOwned:
                case HIR::Function::Receiver::BorrowUnique:
                case HIR::Function::Receiver::BorrowShared: {
                    ASSERT_BUG(sp, newFcn.mArgs.front().second->is_Borrow(), newFcn.mArgs.front().second);
                    auto bt = newFcn.mArgs.front().second->as_Borrow().type;
                    DEBUG("<dyn " << traitPath << ">::" << name << " - By-borrow");
                    lvPtr = builder.addLocal(crate.types.borrow(bt, crate.types.unit()));
                    builder.pushStmtAssign(lvPtr.clone(), MIR::RValue::make_DstPtr({lvSelf.clone()}));
                } break;
                case HIR::Function::Receiver::Box: {
                    // TODO: What is the real reciver here? (for the MIR)
                    // - the `self` type is `Box<dyn ThisTrait>`, so need to deref through that to the right type
                    DEBUG("<dyn " << traitPath << ">::" << name << " - Boxed");
                    // - Need to make a new receiver (convert `Box<dyn ThisTrait>` into `Box<()>`)
                    auto gpath = newFcn.mArgs.front().second->as_Path().path.mData.as_Generic().clone();
                    gpath.mParams.types.at(0) = crate.types.unit();
                    auto ty = crate.types.path(mv$(gpath), newFcn.mArgs.front().second->as_Path().binding.clone());
                    lvPtr = getUnitPtr(sp, builder, mv$(ty), MIR::LValue::newArgument(0), lvSelf);
                } break;
                default:
                    TODO(sp, "Handle different receiver types: <dyn " << traitPath << ">::" << name << " - self: " << newFcn.mArgs.front().second);
            }

            //   _2 = DstMeta a1
            auto lvVtable = builder.addLocal(crate.types.borrow(HIR::BorrowType::Shared, tyDyn.mTrait.traitPtr->getVtableType(sp, crate, tyDyn)));
            builder.pushStmtAssign(lvVtable.clone(), MIR::RValue::make_DstMeta({mv$(lvSelf)}));
            //   rv = _2*.{idx}(a2, ...) goto bb2 else bb3
            std::vector<MIR::Param> callArgs;
            callArgs.push_back(mv$(lvPtr));
            for (size_t i = 1; i < fcnDef.mArgs.size(); i++) {
                callArgs.push_back(MIR::LValue::newArgument(i));
            }
            builder.terminateCall(MIR::LValue::newReturn(), MIR::LValue::newField(MIR::LValue::newDeref(mv$(lvVtable)), vtableIdx), mv$(callArgs), 1, 2);
            // bb1:
            //   RETURN
            builder.ensureOpen();
            builder.terminateBlock(MIR::Terminator::make_Return({}));
            // bb2:
            //   UNWIND
            builder.ensureOpen();
            builder.mir.blocks.back().isCleanup = true;
            builder.terminateBlock(MIR::Terminator::make_UnwindResume({}));
            // ---

            MIRValidate(state.resolve, HIR::ItemPath(path), *newFcn.mCode.mir, newFcn.mArgs, newFcn.returnType);
            transList.autoFunctions.push_back(box$(newFcn));
            auto* e = transList.addFunction(crate.types, path.clone());
            if (e) {
                e->ptr = transList.autoFunctions.back().get();
            } else {
                transList.autoFunctions.pop_back();
            }
        }
    }

    // Create VTable instances
    {
        TRACE_FUNCTION_F("VTables");
        transList.autoStatics.reserve(transList.vtables.size());
        for (const auto& ent : transList.vtables) {
            Span sp;
            const auto& path = ent.first;
            const auto& traitPath = path.mData.as_UfcsKnown().trait;
            const auto& type = path.mData.as_UfcsKnown().type;

            struct {
                const char* fcnName;
                const HIR::SimplePath* traitPath;
                HIR::BorrowType bt;
            } const entries[3] = {{"call", &state.resolve.mLangFn, HIR::BorrowType::Shared}, {"call_mut", &state.resolve.mLangFnMut, HIR::BorrowType::Unique}, {"call_once", &state.resolve.mLangFnOnce, HIR::BorrowType::Owned}};

            size_t offset;
            if (traitPath.mPath == state.resolve.mLangFn) {
                offset = 0;
            } else if (traitPath.mPath == state.resolve.mLangFnMut) {
                offset = 1;
            } else if (traitPath.mPath == state.resolve.mLangFnOnce) {
                offset = 2;
            } else {
                offset = 3; // Wait, is this reachable?
            }

            if (const auto* te = type->opt_NamedFunction()) {
                for (; offset < sizeof(entries) / sizeof(entries[0]); offset++) {
                    bool isByValue = (offset == 2);
                    const auto& ent = entries[offset];

                    auto fcnP = path.clone();
                    fcnP.mData.as_UfcsKnown().item = ent.fcnName;
                    fcnP.mData.as_UfcsKnown().trait.mPath = ent.traitPath->clone();

                    auto* e = transList.addFunction(crate.types, mv$(fcnP));
                    if (e) {
                        auto ft = te->decay(crate.types, sp);

                        ::std::vector<HIR::TypeRef> argTys;
                        for (auto& ty : ft.argTypes) {
                            argTys.push_back(ty);
                        }
                        auto argTy = crate.types.tuple(mv$(argTys));
                        state.resolve.expandAssociatedTypes(sp, argTy);

                        HIR::Function fcn;
                        fcn.returnType = ft.mRettype;
                        state.resolve.expandAssociatedTypes(sp, argTy);
                        fcn.mArgs.push_back(std::make_pair(HIR::Pattern(), !isByValue ? crate.types.borrow(ent.bt, type) : type));
                        fcn.mArgs.push_back(std::make_pair(HIR::Pattern(), mv$(argTy)));

                        fcn.mCode.mir = MIR::FunctionPointer(new MIR::Function());
                        Builder builder(state, *fcn.mCode.mir);

                        std::vector<MIR::Param> argParams;
                        for (size_t i = 0; i < ft.argTypes.size(); i++) {
                            argParams.push_back(MIR::LValue::newField(MIR::LValue::newArgument(1), i));
                        }
                        builder.terminateCall(MIR::LValue::newReturn(), te->path.clone(), mv$(argParams), 1, 2);
                        // BB1: Return
                        builder.ensureOpen();
                        builder.terminateBlock(MIR::Terminator::make_Return({}));
                        // BB1: Diverge
                        builder.ensureOpen();
                        builder.mir.blocks.back().isCleanup = true;
                        builder.terminateBlock(MIR::Terminator::make_UnwindResume({}));

                        MIRValidate(state.resolve, HIR::ItemPath(path), *fcn.mCode.mir, fcn.mArgs, fcn.returnType);
                        transList.autoFunctions.push_back(box$(fcn));
                        e->ptr = transList.autoFunctions.back().get();
                    }
                }
            } else if (const auto* te = type->opt_Function()) {
                for (; offset < sizeof(entries) / sizeof(entries[0]); offset++) {
                    bool isByValue = (offset == 2);
                    const auto& ent = entries[offset];

                    auto fcnP = path.clone();
                    fcnP.mData.as_UfcsKnown().item = ent.fcnName;
                    fcnP.mData.as_UfcsKnown().trait.mPath = ent.traitPath->clone();

                    auto* e = transList.addFunction(crate.types, mv$(fcnP));
                    if (e) {
                        ::std::vector<HIR::TypeRef> argTys;
                        for (const auto& ty : te->argTypes) {
                            argTys.push_back(ty);
                        }
                        auto argTy = crate.types.tuple(mv$(argTys));

                        HIR::Function fcn;
                        fcn.returnType = te->mRettype;
                        fcn.mArgs.push_back(std::make_pair(HIR::Pattern(), !isByValue ? crate.types.borrow(ent.bt, type) : type));
                        fcn.mArgs.push_back(std::make_pair(HIR::Pattern(), mv$(argTy)));

                        fcn.mCode.mir = MIR::FunctionPointer(new MIR::Function());
                        Builder builder(state, *fcn.mCode.mir);

                        std::vector<MIR::Param> argParams;
                        for (size_t i = 0; i < te->argTypes.size(); i++) {
                            argParams.push_back(MIR::LValue::newField(MIR::LValue::newArgument(1), i));
                        }
                        builder.terminateCall(MIR::LValue::newReturn(), !isByValue ? MIR::LValue::newDeref(MIR::LValue::newArgument(0)) : MIR::LValue::newArgument(0), mv$(argParams), 1, 2);
                        // BB1: Return
                        builder.ensureOpen();
                        builder.terminateBlock(MIR::Terminator::make_Return({}));
                        // BB1: Diverge
                        builder.ensureOpen();
                        builder.mir.blocks.back().isCleanup = true;
                        builder.terminateBlock(MIR::Terminator::make_UnwindResume({}));

                        MIRValidate(state.resolve, HIR::ItemPath(path), *fcn.mCode.mir, fcn.mArgs, fcn.returnType);
                        transList.autoFunctions.push_back(box$(fcn));
                        e->ptr = transList.autoFunctions.back().get();
                    }
                }
            }
        }
        for (const auto& ent : transList.vtables) {
            Span sp;
            const auto& traitPath = ent.first.mData.as_UfcsKnown().trait;
            const auto& type = ent.first.mData.as_UfcsKnown().type;
            if (traitPath.mPath != HIR::SimplePath()) {
                continue;
            }
            DEBUG("VTABLE <empty> for " << type);

            ::std::vector<HIR::TypeRef> tupleTys;
            tupleTys.push_back(crate.types.primitive(::HIR::CoreType::Usize));
            tupleTys.push_back(crate.types.primitive(::HIR::CoreType::Usize));
            tupleTys.push_back(crate.types.primitive(::HIR::CoreType::Usize)); // fn
            auto vtableTy = crate.types.tuple(std::move(tupleTys));

            // Look up the size of the VTable, so we can allocate the right buffer size
            const auto* repr = TargetGetTypeRepr(sp, state.resolve, vtableTy);
            assert(repr);

            HIR::Linkage linkage;
            linkage.type = HIR::Linkage::Type::Weak;
            HIR::Static vtableStatic(::std::move(linkage), /*is_mut*/ false, mv$(vtableTy), {});
            auto& vtableData = vtableStatic.valueRes;
            const auto ptrBytes = TargetGetPointerBits() / 8;
            vtableData.bytes.resize(repr->size);
            size_t ofs = 0;
            auto pushPtr = [&vtableData, &ofs, ptrBytes](HIR::Path p) {
                DEBUG("@" << ofs << " = " << p);
                assert(ofs + ptrBytes <= vtableData.bytes.size());
                vtableData.relocations.push_back(Reloc::newNamed(ofs, ptrBytes, mv$(p)));
                vtableData.writeUint(ofs, ptrBytes, EncodedLiteral::PTR_BASE);
                ofs += ptrBytes;
                assert(ofs <= vtableData.bytes.size());
            };
            // Drop glue
            transList.dropGlue.insert(type);
            pushPtr(::HIR::Path(type, rcstringDropGlue));
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
            const auto& traitPath = ent.first.mData.as_UfcsKnown().trait;
            const auto& type = ent.first.mData.as_UfcsKnown().type;
            if (traitPath.mPath == HIR::SimplePath()) {
                continue;
            }
            DEBUG("VTABLE " << traitPath << " for " << type);
            // TODO: What's the use of `ent.second` here? (it's a `Trans_Params`)

            // Get the vtable type
            const auto& trait = crate.getTraitByPath(sp, traitPath.mPath);
            const auto& vtableSp = trait.vtablePath;
            ASSERT_BUG(sp, vtableSp != HIR::SimplePath(), "Trait " << traitPath.mPath << " doesn't have a vtable");
            auto vtableParams = traitPath.mParams.clone();
            for (const auto& ty : trait.typeIndexes) {
                auto aty = crate.types.path(::HIR::Path(type, traitPath.clone(), ty.first), {});
                state.resolve.expandAssociatedTypes(sp, aty);
                vtableParams.types.push_back(mv$(aty));
            }
            const auto& vtableRef = crate.getStructByPath(sp, vtableSp);
            auto vtableTy = crate.types.path(::HIR::GenericPath(mv$(vtableSp), mv$(vtableParams)), &vtableRef);

            // Ensure that the type is defined/populated
            transList.addType(vtableTy, false);

            // Look up the size of the VTable, so we can allocate the right buffer size
            const auto* repr = TargetGetTypeRepr(sp, state.resolve, vtableTy);
            assert(repr);

            // Create vtable contents
            auto monomorphCbTrait = MonomorphStatePtr(crate.types, type, &traitPath.mParams, nullptr);

            HIR::Linkage linkage;
            linkage.type = HIR::Linkage::Type::Weak;
            HIR::Static vtableStatic(::std::move(linkage), /*is_mut*/ false, mv$(vtableTy), {});
            auto& vtableData = vtableStatic.valueRes;
            const auto ptrBytes = TargetGetPointerBits() / 8;
            vtableData.bytes.resize(repr->size);
            size_t ofs = 0;
            auto pushPtr = [&vtableData, &ofs, ptrBytes](HIR::Path p) {
                DEBUG("@" << ofs << " = " << p);
                assert(ofs + ptrBytes <= vtableData.bytes.size());
                vtableData.relocations.push_back(Reloc::newNamed(ofs, ptrBytes, mv$(p)));
                vtableData.writeUint(ofs, ptrBytes, EncodedLiteral::PTR_BASE);
                ofs += ptrBytes;
                assert(ofs <= vtableData.bytes.size());
            };
            // Drop glue
            transList.dropGlue.insert(type);
            pushPtr(::HIR::Path(type, rcstringDropGlue));
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
                    auto itemPath = ::HIR::Path(type, mv$(traitGpath), m.first);

                    auto srcTraitMs = MonomorphStatePtr(crate.types, type, &itemPath.mData.as_UfcsKnown().trait.mParams, nullptr);
                    const auto& srcTrait = state.resolve.crate.getTraitByPath(sp, m.second.second.mPath);
                    const auto& item = srcTrait.values.at(m.first);
                    // If the entry is a by-value function, then emit a reference to a shim
                    if (item.is_Function()) {
                        const auto& tplFcn = item.as_Function();
                        if (tplFcn.receiver == HIR::Function::Receiver::Value) {
                            auto callPath = itemPath.clone();
                            itemPath.mData.as_UfcsKnown().item = RcString::newInterned(FMT(m.first << "#ptr"));
                            auto* e = transList.addFunction(crate.types, itemPath.clone());
                            if (e) {
                                // Create the shim (forward to the true call, dereferencing the first argument)
                                HIR::Function newFcn;
                                newFcn.returnType = srcTraitMs.monomorphType(sp, tplFcn.returnType);
                                state.resolve.expandAssociatedTypes(sp, newFcn.returnType);
                                newFcn.mArgs.push_back(std::make_pair(HIR::Pattern(), crate.types.borrow(HIR::BorrowType::Owned, type)));
                                for (size_t i = 1; i < tplFcn.mArgs.size(); i++) {
                                    newFcn.mArgs.push_back(std::make_pair(HIR::Pattern(), srcTraitMs.monomorphType(sp, tplFcn.mArgs[i].second)));
                                }
                                for (size_t i = 0; i < newFcn.mArgs.size(); i++) {
                                    state.resolve.expandAssociatedTypes(sp, newFcn.mArgs[i].second);
                                }

                                DEBUG("> Generate shim: " << itemPath);

                                newFcn.mCode.mir = MIR::FunctionPointer(new MIR::Function());
                                ::MIR::TypeResolve localMirRes{sp, state.resolve, FMT_CB(ss, ss << itemPath), newFcn.returnType, newFcn.mArgs, *newFcn.mCode.mir};
                                Builder builder(state, *newFcn.mCode.mir);
                                // bb0:
                                //   rv = CALL ...
                                ::std::vector<::MIR::Param> callArgs;
                                callArgs.push_back(::MIR::LValue::newDeref(::MIR::LValue::newArgument(0)));
                                for (size_t i = 1; i < tplFcn.mArgs.size(); i++) {
                                    callArgs.push_back(::MIR::LValue::newArgument(i));
                                }
                                builder.terminateCall(::MIR::LValue::newReturn(), mv$(callPath), std::move(callArgs), 1, 2);
                                // bb1:
                                //   RETURN
                                builder.ensureOpen();
                                builder.terminateBlock(MIR::Terminator::make_Return({}));
                                // bb2:
                                //   UNWIND
                                builder.ensureOpen();
                                builder.mir.blocks.back().isCleanup = true;
                                builder.terminateBlock(MIR::Terminator::make_UnwindResume({}));
                                // ---

                                MIRValidate(state.resolve, HIR::ItemPath(itemPath), *newFcn.mCode.mir, newFcn.mArgs, newFcn.returnType);
                                transList.autoFunctions.push_back(box$(newFcn));
                                e->ptr = transList.autoFunctions.back().get();
                            }
                        }
                    }
                    //MIR_ASSERT(*m_mir_res, tr.m_values.at(m.first).is_Function(), "TODO: Handle generating vtables with non-function items");
                    pushPtr(mv$(itemPath));
                }
            }
            // Parent trait vtables
            for (size_t i = 0; i < trait.allParentTraits.size(); i++) {
                const auto& pt = trait.allParentTraits[i];
                const auto& fld = repr->fields.at(trait.vtableParentTraitsStart + i);
                ASSERT_BUG(sp, fld.offset == ofs, "");
                if (!fld.ty->is_Tuple()) {
                    auto ptMono = MonomorphStatePtr(crate.types, nullptr, &traitPath.mParams, nullptr).monomorphGenericpath(sp, pt.mPath);
                    auto ptVtablePath = ::HIR::Path(type, mv$(ptMono), ent.first.mData.as_UfcsKnown().item);
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
            auto path = ::HIR::Path(ty, rcstringDropGlue);

            HIR::Function fcn;
            fcn.returnType = crate.types.unit();
            fcn.mArgs.push_back(std::make_pair(HIR::Pattern(), crate.types.borrow(HIR::BorrowType::Owned, ty)));

            fcn.mCode.mir = MIR::FunctionPointer(new MIR::Function());
            ::MIR::TypeResolve localMirRes{sp, state.resolve, FMT_CB(ss, ss << path), fcn.returnType, fcn.mArgs, *fcn.mCode.mir};
            Builder builder(state, *fcn.mCode.mir);
            builder.pushStmtAssign(MIR::LValue::newReturn(), MIR::RValue::make_Tuple({}));
            auto ownedBoxPointeeDrop = static_cast<MIR::BasicBlockId>(~0u);
            auto ownedBoxDropCall = static_cast<MIR::BasicBlockId>(~0u);
            if (const auto* ity = state.resolve.isTypeOwnedBox(ty)) {
                // Call inner destructors
                auto innerVal = derefBox(::MIR::LValue::newDeref(builder.self.clone()));
                HIR::TypeRef tmp;
                ASSERT_BUG(sp, localMirRes.getLvalueType(tmp, innerVal) == ity, "Hard-coded box pointer path didn't result in the inner type");
                ownedBoxPointeeDrop = builder.pushStmtDrop(std::move(innerVal));
            }

            if (state.resolve.typeNeedsDropGlue(sp, ty)) {
                TU_MATCH_HDRA( ((*ty)), {)
                TU_ARMA(Infer, _te)
                    throw "";
                    TU_ARMA(Generic, _te)
                    throw "";
                    TU_ARMA(ErasedType, _te)
                    throw "";
                    TU_ARMA(TraitObject, _te)
                    TODO(sp, "Drop glue for TraitObject? " << ty);
                    TU_ARMA(Slice, _te)
                    TODO(sp, "Drop glue for Slice? " << ty);
                    TU_ARMA(NodeType, _te)
                    TODO(sp, "Drop glue for NodeType? " << ty); // Should have been converted to a named structure by this point
                    TU_ARMA(Diverge, te) {
                        // Exists for reasons...
                        builder.terminateBlock(MIR::Terminator::make_Unreachable({}));
                    }
                    TU_ARMA(Primitive, te) {
                        // Nothing to do
                    }
                    TU_ARMA(NamedFunction, te) {
                        // Nothing to do
                    }
                    TU_ARMA(Function, te) {
                        // Nothing to do
                    }
                    TU_ARMA(Pointer, te) {
                        // Nothing to do
                    }
                    TU_ARMA(Borrow, te) {
                        if (te.type == HIR::BorrowType::Owned) {
                            // `drop a0**`
                            builder.pushStmtDrop(::MIR::LValue::newDeref(::MIR::LValue::newDeref(builder.self.clone())));
                        }
                    }
                    TU_ARMA(Tuple, te) {
                        auto self = ::MIR::LValue::newDeref(builder.self.clone());
                        auto fldLv = ::MIR::LValue::newField(mv$(self), 0);
                        ::std::vector<MIR::LValue> fields;
                        for (size_t i = 0; i < te.size(); i++) {
                            if (state.resolve.typeNeedsDropGlue(sp, te[i])) {
                                fields.push_back(fldLv.clone());
                            }
                            fldLv.incField();
                        }
                        builder.pushDropSequence(mv$(fields));
                    }
                    TU_ARMA(Array, te) {
                        auto size = te.size.as_Known();
                        auto self = ::MIR::LValue::newDeref(builder.self.clone());
                        if (size > 0 && state.resolve.typeNeedsDropGlue(sp, te.inner)) {
                            // The C++ backend expands structural array drops
                            // itself, including the unwind cleanup of the tail.
                            // Keeping a second hand-built MIR loop here would
                            // duplicate that logic and lose the tail on panic.
                            builder.pushStmtDrop(mv$(self));
                        }
                    }
                    TU_ARMA(Path, te) {
                        bool hasDrop = false;
                    TU_MATCH_HDRA( (te.binding), {)
                    TU_ARMA(Unbound, pbe) throw "";
                            TU_ARMA(Opaque, pbe) throw "";
                            TU_ARMA(ExternType, pbe) {
                                // Why is this trying to be dropped?
                            }

                            TU_ARMA(Struct, pbe) {
                                auto customDropCall = static_cast<MIR::BasicBlockId>(~0u);
                                if (pbe->markings.hasDropImpl) {
                                    customDropCall = builder.pushCallDrop(ty);
                                    if (ownedBoxPointeeDrop != ~0u) {
                                        ownedBoxDropCall = customDropCall;
                                    }
                                    hasDrop = true;
                                }

                                if (ty->is_Path() && ty->as_Path().isGenerator()) {
                                    ASSERT_BUG(sp, hasDrop, "");
                                    // Generators use a custom Drop impl that handles dropping values
                                } else {
                                    // NOTE: Lazy option of monomorphising and handling the two classes
                                    const auto* repr = TargetGetTypeRepr(sp, state.resolve, ty);
                                    ASSERT_BUG(sp, repr, "No repr for struct " << ty);

                                    auto self = ::MIR::LValue::newDeref(builder.self.clone());
                                    auto fldLv = ::MIR::LValue::newField(mv$(self), 0);
                                    ::std::vector<MIR::LValue> fields;
                                    for (size_t i = 0; i < repr->fields.size(); i++) {
                                        if (state.resolve.typeNeedsDropGlue(sp, repr->fields[i].ty)) {
                                            fields.push_back(fldLv.clone());
                                        }
                                        fldLv.incField();
                                    }
                                    builder.pushDropSequence(mv$(fields), customDropCall);
                                }
                            }
                            TU_ARMA(Union, pbe) {
                                if (pbe->markings.hasDropImpl) {
                                    builder.pushCallDrop(ty);
                                    hasDrop = true;
                                }
                                // Union requires no internal drop glue
                            }
                            TU_ARMA(Enum, pbe) {
                                auto customDropCall = static_cast<MIR::BasicBlockId>(~0u);
                                if (pbe->markings.hasDropImpl) {
                                    customDropCall = builder.pushCallDrop(ty);
                                    hasDrop = true;
                                }
                                const HIR::Enum& enm = *pbe;
                        TU_MATCH_HDRA( (enm.mData), {)
                        TU_ARMA(Value, ee) {
                                        builder.terminateBlock(MIR::Terminator::make_Return({}));
                                    }
                                    TU_ARMA(Data, variants) {
                                        auto self = ::MIR::LValue::newDeref(builder.self.clone());
                                        MIR::Terminator::Data_Switch sw;
                                        sw.val = self.clone();
                                        const auto switchBlock = builder.mir.blocks.size() - 1;
                                        builder.terminateBlock(MIR::Terminator::make_Switch(mv$(sw)));

                                        ::std::vector<MIR::BasicBlockId> targets;
                                        targets.reserve(variants.size());
                                        auto fldLv = ::MIR::LValue::newDowncast(mv$(self), 0);
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
                                            builder.terminateBlock(MIR::Terminator::make_Return({}));
                                        }
                                        builder.mir.blocks[switchBlock].terminator.as_Switch().targets = mv$(targets);

                                        if (customDropCall != ~0u) {
                                            // If the user's Drop implementation
                                            // panics, select the active variant
                                            // again and destroy its payload on a
                                            // cleanup edge.
                                            const auto cleanupSwitch = static_cast<MIR::BasicBlockId>(builder.mir.blocks.size());
                                            MIR::BasicBlock switchCleanupBlock;
                                            switchCleanupBlock.isCleanup = true;
                                            MIR::Terminator::Data_Switch cleanupSwitchData;
                                            cleanupSwitchData.val = ::MIR::LValue::newDeref(builder.self.clone());
                                            switchCleanupBlock.terminator = MIR::Terminator::make_Switch(mv$(cleanupSwitchData));
                                            builder.mir.blocks.push_back(mv$(switchCleanupBlock));

                                            const auto resume = static_cast<MIR::BasicBlockId>(builder.mir.blocks.size() + variants.size());
                                            ::std::vector<MIR::BasicBlockId> cleanupTargets;
                                            cleanupTargets.reserve(variants.size());
                                            auto cleanupField = ::MIR::LValue::newDowncast(::MIR::LValue::newDeref(builder.self.clone()), 0);
                                            for (size_t idx = 0; idx < variants.size(); idx++) {
                                                cleanupTargets.push_back(builder.mir.blocks.size());
                                                MIR::BasicBlock block;
                                                block.isCleanup = true;
                                                block.terminator = MIR::Terminator::make_Drop({
                                                    MIR::eDropKind::DEEP,
                                                    cleanupField.clone(),
                                                    ~0u,
                                                    resume,
                                                    MIR::UnwindAction::make_Terminate({}),
                                                });
                                                builder.mir.blocks.push_back(mv$(block));
                                                cleanupField.incDowncast();
                                            }
                                            MIR::BasicBlock resumeBlock;
                                            resumeBlock.isCleanup = true;
                                            resumeBlock.terminator = MIR::Terminator::make_UnwindResume({});
                                            builder.mir.blocks.push_back(mv$(resumeBlock));

                                            builder.mir.blocks[cleanupSwitch].terminator.as_Switch().targets = mv$(cleanupTargets);
                                            builder.mir.blocks[customDropCall].terminator.as_Call().unwind = MIR::UnwindAction::make_Cleanup(cleanupSwitch);
                                        }
                                    }
                        }
                            }
                    }
                    if( hasDrop ) {
                            if (auto* e = transList.addFunction(crate.types, ::HIR::Path(ty, state.resolve.mLangDrop, rcstringDrop))) {
                                MonomorphState params(crate.types);
                                auto p = ::HIR::Path(ty, state.resolve.mLangDrop, rcstringDrop);
                                auto fcnE = state.resolve.getValue(sp, p, /*out*/ params, /*signature_only=*/false);
                                ASSERT_BUG(sp, fcnE.is_Function(), "Drop didn't point to a function! " << fcnE.tagStr() << " " << p);
                                ASSERT_BUG(sp, !params.hasTypes(), "Generic drop impl encountered during auto_impls (should have been populated during enum)");
                                e->forcePrototype = true;
                                e->ptr = fcnE.as_Function();
                                //e->pp = mv$(params);
                            }
                    }
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
                    builder.terminateBlock(MIR::Terminator::make_Return({}));
                }

                MIR::BasicBlockId afterCleanupCall;
                if (const auto* fieldCleanup = builder.mir.blocks[ownedBoxDropCall].terminator.as_Call().unwind.opt_Cleanup()) {
                    afterCleanupCall = *fieldCleanup;
                } else {
                    afterCleanupCall = static_cast<MIR::BasicBlockId>(builder.mir.blocks.size() + 1);
                }

                auto cleanupBorrow = builder.addLocal(state.crate.types.borrow(HIR::BorrowType::Unique, ty));
                const auto cleanupCall = static_cast<MIR::BasicBlockId>(builder.mir.blocks.size());
                MIR::BasicBlock cleanupCallBlock;
                cleanupCallBlock.isCleanup = true;
                cleanupCallBlock.statements.push_back(
                    MIR::Statement::make_Assign({
                        cleanupBorrow.clone(),
                        MIR::RValue::make_Borrow({HIR::BorrowType::Unique, false, ::MIR::LValue::newDeref(builder.self.clone())}),
                    })
                );
                cleanupCallBlock.terminator = MIR::Terminator::make_Call({
                    afterCleanupCall,
                    MIR::UnwindAction::make_Terminate({}),
                    MIR::LValue::newReturn(),
                    ::HIR::Path(ty, state.resolve.mLangDrop, rcstringDrop),
                    makeVec1<MIR::Param>(mv$(cleanupBorrow)),
                });
                builder.mir.blocks.push_back(mv$(cleanupCallBlock));

                if (afterCleanupCall == builder.mir.blocks.size()) {
                    MIR::BasicBlock resumeBlock;
                    resumeBlock.isCleanup = true;
                    resumeBlock.terminator = MIR::Terminator::make_UnwindResume({});
                    builder.mir.blocks.push_back(mv$(resumeBlock));
                }
                builder.mir.blocks[ownedBoxPointeeDrop].terminator.as_Drop().unwind = MIR::UnwindAction::make_Cleanup(cleanupCall);
            }
            if (builder.mir.blocks.back().terminator.is_Incomplete()) {
                builder.terminateBlock(MIR::Terminator::make_Return({}));
            }

            MIRValidate(state.resolve, HIR::ItemPath(path), *fcn.mCode.mir, fcn.mArgs, fcn.returnType);
            transList.autoFunctions.push_back(box$(fcn));
            auto* e = transList.addFunction(crate.types, mv$(path));
            if (e) {
                e->ptr = transList.autoFunctions.back().get();
            } else {
                transList.autoFunctions.pop_back();
            }
        }
    }
}

namespace {
    // Translation paths are assembled after inference and monomorphisation.
    // A nominal type can therefore arrive through an older, structurally
    // equivalent TypeRef whose path binding is still Unbound.  rustc's Ty
    // carries the ADT DefId as part of the nominal type and cannot represent
    // that state.  Restore the equivalent invariant at the translation
    // boundary instead of teaching codegen to accept missing metadata.
    class BindTranslationNominals final: public ::HIR::Visitor {
        const ::HIR::Crate& crate;

    public:
        explicit BindTranslationNominals(const ::HIR::Crate& crate)
            : ::HIR::Visitor(nullptr, crate.types)
            , crate(crate)
        {
        }

        void visitType(::HIR::TypeRef& ty) override {
            auto data = ty->cloneData();
            visitTypeData(data);

            if (auto* pathTy = data.opt_Path()) {
                if (pathTy->binding.is_Unbound() && pathTy->path.mData.is_Generic()) {
                    const auto& path = pathTy->path.mData.as_Generic().mPath;
                    const auto& item = crate.getTypeitemByPath(Span(), path);
                    TU_MATCH_HDRA((item), {)
                    default:
                        BUG(Span(), "Nominal translation type points to " << item.tagStr() << " - " << ty);
                        TU_ARMA(ExternType, e) {
                            pathTy->binding = ::HIR::TypePathBinding::make_ExternType(&e);
                        }
                        TU_ARMA(Struct, e) {
                            pathTy->binding = ::HIR::TypePathBinding::make_Struct(&e);
                        }
                        TU_ARMA(Union, e) {
                            pathTy->binding = ::HIR::TypePathBinding::make_Union(&e);
                        }
                        TU_ARMA(Enum, e) {
                            pathTy->binding = ::HIR::TypePathBinding::make_Enum(&e);
                        }
                    }
                }
            }

            ty = typeInterner().intern(mv$(data));
        }
    };

    void bindTranslationNominals(const ::HIR::Crate& crate, ::HIR::Path& path) {
        BindTranslationNominals visitor(crate);
        visitor.visitPath(path, ::HIR::Visitor::PathContext::VALUE);
    }

    struct EnumState {
        const ::HIR::Crate& crate;
        StaticTraitResolve resolve;
        TransList rv;
        const TransList* origList;

        // Queue of items to enumerate
        ::std::deque<TransListFunction*> fcnQueue;
        ::std::vector<TransListFunction*> fcnsToTypeVisit;

        ::std::set<std::string> emittedFunctions;

        // Map of locally-defined exported `link_name` functions
        ::std::unordered_map<std::string, std::pair<HIR::SimplePath, const HIR::Function*>> linkFunctions;

        EnumState(const ::HIR::Crate& crate)
            : crate(crate)
            , resolve(crate)
            , rv()
            , origList(nullptr)
        {
            enumerateLinkFunctions();
        }

        void enumFcn(::HIR::Path p, const ::HIR::Function& fcn, TransParams pp) {
            if (auto* e = rv.addFunction(crate.types, mv$(p))) {
#if 1
                auto name = FMT(TransMangle(*e->path));
                auto inserted = emittedFunctions.insert(name).second;
                ASSERT_BUG(Span(), inserted, "Duplicated mangled name - " << *e->path);
#endif
                fcnsToTypeVisit.push_back(e);
                e->ptr = &fcn;
                e->pp = mv$(pp);
                DEBUG(*e->path << " w/ " << e->pp.ppImpl << " and " << e->pp.ppMethod);
                fcnQueue.push_back(e);
            }
        }

    private:
        void enumerateLinkFunctions() {
            enumerateLinkFunctionsIn(crate.mRootModule, HIR::ItemPath(crate.crateName));
            for (const auto& eCrate : crate.extCrates) {
                enumerateLinkFunctionsIn(eCrate.second.mData->mRootModule, HIR::ItemPath(eCrate.first));
            }
        }

        void enumerateLinkFunctionsIn(const HIR::Module& mod, HIR::ItemPath modPath) {
            for (const auto& vi : mod.valueItems) {
                if (const auto* ip = vi.second->ent.opt_Function()) {
                    const auto& i = *ip;
                    if (i.mCode.mir && i.linkage.name != "") {
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

    const RcString enumerateRcstringDrop = RcString::newInterned("drop");
}

TransList TransEnumerateCommonPost(EnumState& state);

namespace {
    void TransEnumerateExplicitLinkage(EnumState& state, const ::HIR::Module& mod, ::HIR::SimplePath modPath);
}

void TransEnumerateTypes(EnumState& state);
void TransEnumerateFillFromPath(EnumState& state, const ::HIR::Path& path, const TransParams& pp);
void TransEnumerateFillFromPathMono(EnumState& state, ::HIR::Path path);
void TransEnumerateFillFromFunction(EnumState& state, const ::HIR::Path& path, const ::HIR::Function& function, const TransParams& pp);
void TransEnumerateFillFromStatic(EnumState& state, const ::HIR::Static& stat, TransListStatic& statOut, TransParams pp);
void TransEnumerateFillFromVTable(EnumState& state, ::HIR::Path vtablePath, const TransParams& pp);
void TransEnumerateFillFromLiteral(EnumState& state, const EncodedLiteral& lit, const TransParams& pp);
void TransEnumerateFillFromMIR(MIR::EnumCache& state, const ::MIR::Function& code);

void TransEnumerateGlobalAllocator(EnumState& state) {
    const auto allocatorIt = state.crate.mLangItems.find(GLOBAL_ALLOCATOR_LANG_ITEM);
    if (allocatorIt == state.crate.mLangItems.end()) {
        return;
    }

    const auto& allocatorPath = allocatorIt->second;
    const auto& allocator = state.crate.getStaticByPath(Span(), allocatorPath);

    HIR::Path staticPath = HIR::GenericPath(allocatorPath);
    state.rv.roots.push_back(staticPath.clone());
    TransEnumerateFillFromPathMono(state, std::move(staticPath));

    auto layoutCtor = TransAllocatorLayoutCtorPath(state.crate);
    state.rv.roots.push_back(layoutCtor.clone());
    TransEnumerateFillFromPathMono(state, std::move(layoutCtor));

    for (size_t i = 0; i < NUM_ALLOCATOR_METHODS; i++) {
        auto methodPath = TransAllocatorMethodPath(state.crate, allocator.mType, ALLOCATOR_METHODS[i]);
        state.rv.roots.push_back(methodPath.clone());
        TransEnumerateFillFromPathMono(state, std::move(methodPath));
    }
}

namespace MIR {
    struct EnumCache {
        ::std::vector<const ::HIR::Path*> paths;
        ::std::vector<const ::HIR::TypeData*> typeids;

        EnumCache() {
        }

        void insertPath(const ::HIR::Path& newPath) {
            for (const auto* p : this->paths) {
                if (*p == newPath) {
                    return;
                }
            }
            this->paths.push_back(&newPath);
        }

        void insertTypeid(const ::HIR::TypeData* newTy) {
            for (const auto* p : this->typeids) {
                if (p == newTy) {
                    return;
                }
            }
            this->typeids.push_back(newTy);
        }

        void apply(EnumState& state, const TransParams& pp) const {
            TRACE_FUNCTION_F(" w/ impl=" << pp.ppImpl << " method=" << pp.ppMethod);
            for (const auto* tyP : this->typeids) {
                DEBUG("TypeID " << tyP);
                state.rv.typeids.insert(pp.monomorph(state.resolve, tyP));
            }
            for (const auto& path : this->paths) {
                DEBUG("Path " << *path);
                TransEnumerateFillFromPath(state, *path, pp);
            }
        }
    };

    EnumCachePtr::~EnumCachePtr() {
        delete this->p;
        this->p = nullptr;
    }
}

/// Enumerate trans items starting from `::main` (binary crate)
TransList TransEnumerateMain(const ::HIR::Crate& crate) {
    static Span sp;

    EnumState state{crate};

    if (!crate.noMain) {
        auto cStartPath = crate.getLangItemPathOpt("mrustc-start");
        if (cStartPath == ::HIR::SimplePath()) {
            // user entrypoint
            auto mainPath = crate.getLangItemPath(Span(), "mrustc-main");
            const auto& mainFcn = crate.getFunctionByPath(sp, mainPath);

            state.rv.roots.push_back(mainPath);
            state.enumFcn(mainPath, mainFcn, TransParams(crate.types));

            // "start" language item
            // - Takes main, and argc/argv as arguments
            const auto& startPath = crate.getLangItemPathOpt("start");
            if (startPath != ::HIR::SimplePath()) {
                const auto& fcn = crate.getFunctionByPath(sp, startPath);

                TransParams langStartPp(crate.types);
                langStartPp.ppMethod.types.push_back(mainFcn.returnType);
                HIR::Path p = HIR::GenericPath(startPath, langStartPp.ppMethod.clone());
                state.rv.roots.push_back(p.clone());
                //state.enum_fcn( start_path, fcn, mv$(lang_start_pp) );
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

    TransEnumerateExplicitLinkage(state, crate.mRootModule, ::HIR::SimplePath(crate.crateName, {}));
    TransEnumerateGlobalAllocator(state);

    return TransEnumerateCommonPost(state);
}

namespace {
    void TransEnumerateGenericFunctionItems(EnumState& state, const Span& sp, const ::HIR::Function& e, MonomorphStatePtr ms) {
        if (e.mCode.mir) {
            const auto& mirFcn = *e.mCode.mir;
            auto params = e.mParams.makeEmptyParams(true);
            ms.ppMethod = &params;
            if (!mirFcn.transEnumState) {
                auto* esp = new MIR::EnumCache();
                TransEnumerateFillFromMIR(*esp, *e.mCode.mir);
                mirFcn.transEnumState = ::MIR::EnumCachePtr(esp);
            }

            for (const auto& path : mirFcn.transEnumState->paths) {
                if (!monomorphisePathNeeded(*path, true)) {
                    DEBUG("Path " << *path);
                    MonomorphState unusedMs(state.crate.types);
                    auto v = state.resolve.getValue(sp, *path, unusedMs, true);
                    if (v.is_StructConstructor() || v.is_EnumConstructor()) {
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

    void TransEnumerateValItem(EnumState& state, const ::HIR::ValueItem& vi, bool isVisible, ::std::function<::HIR::SimplePath()> getPath) {
        TRACE_FUNCTION_F(getPath() << " : " << vi.tagStr() << " is_visible=" << isVisible);
        const Span sp;
        switch (vi.tag()) {
            case ::HIR::ValueItem::TAGDEAD:
                throw "";
                TU_ARM(vi, Import, e) {
                    // TODO: If visible, ensure that target is visited.
                    if (isVisible) {
                        if (!e.isVariant && e.path.crateName() == state.crate.crateName) {
                            const auto& vi2 = state.crate.getValitemByPath(sp, e.path, false);
                            TransEnumerateValItem(state, vi2, isVisible, [&]() {
                                return e.path;
                            });
                        }
                    }
                }
                break;
                TU_ARM(vi, StructConstant, e) {
                }
                break;
                TU_ARM(vi, StructConstructor, e) {
                }
                break;
                TU_ARM(vi, Constant, e) {
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
                TU_ARM(vi, Static, e) {
                    if (e.linkage.name != "" || e.linkage.section != "") {
                        // If a link name is set, force emit
                        isVisible = true;
                    }
                    if (isVisible && !e.mParams.isGeneric()) {
                        // HACK: Refuse to emit unused generated statics
                        // - Needed because all items are visited (regardless of
                        // visibility)
                        if (e.mType->is_Infer()) {
                            continue;
                        }
                        //state.enum_static(mod_path + vi.first, *e);
                        auto* ptr = state.rv.addStatic(state.crate.types, getPath());
                        if (ptr) {
                            TransEnumerateFillFromStatic(state, e, *ptr, TransParams(state.crate.types));
                        }

                        state.rv.roots.push_back(getPath());
                    }
                }
                break;
                TU_ARM(vi, Function, e) {
                    bool isInline = false;
                    if (isVisible) {
                        switch (e.markings.inlineType) {
                            case ::HIR::Function::Markings::Inline::Always:
                            case ::HIR::Function::Markings::Inline::Normal:
                                // Don't emit, it's going to be emitted by callers
                                DEBUG("Don't emit inlined function");
                                isInline = true;
                                break;
                            case ::HIR::Function::Markings::Inline::Auto:
                            case ::HIR::Function::Markings::Inline::Never:
                                // Should still be emitted, as it won't be emitted downstream
                                break;
                        }
                    }
                    if (e.linkage.name != "" || e.linkage.section != "") {
                        // If a link name is set, force emit
                        isVisible = true;
                    }

                    if (e.mParams.isGeneric() || (isInline && isVisible)) {
                        const_cast<::HIR::Function&>(e).saveCode = true;
                    } else {
                        if (isVisible) {
                            TransParams pp(state.crate.types);
                            pp.ppMethod = e.mParams.makeEmptyParams(/*lifetimes_only=*/true);
                            state.enumFcn(getPath(), e, mv$(pp));

                            state.rv.roots.push_back(getPath());
                        }
                    }
                    // Enumerate concrete items used
                    // - These are functions that have to be emitted, even if they're not public themselves
                    if (e.saveCode) {
                        TransEnumerateGenericFunctionItems(state, sp, e, MonomorphStatePtr(state.crate.types));
                    }
                }
                break;
        }
    }

    void TransEnumerateExplicitLinkage(EnumState& state, const ::HIR::Module& mod, ::HIR::SimplePath modPath) {
        for (const auto& vi : mod.valueItems) {
            bool hasExplicitLinkage = false;
            if (const auto* function = vi.second->ent.opt_Function()) {
                hasExplicitLinkage = function->linkage.name != "" || function->linkage.section != "";
            } else if (const auto* stat = vi.second->ent.opt_Static()) {
                hasExplicitLinkage = stat->linkage.name != "" || stat->linkage.section != "";
            }
            if (hasExplicitLinkage) {
                auto path = modPath + vi.first;
                TransEnumerateValItem(state, vi.second->ent, false, [path]() {
                    return path;
                });
            }
        }

        for (const auto& ti : mod.modItems) {
            if (const auto* child = ti.second->ent.opt_Module()) {
                TransEnumerateExplicitLinkage(state, *child, modPath + ti.first);
            }
        }
    }

    void TransEnumeratePublicMod(EnumState& state, ::HIR::Module& mod, ::HIR::SimplePath modPath, bool isVisible) {
        TRACE_FUNCTION_F(modPath);
        for (auto& vi : mod.valueItems) {
            bool emit = isVisible && vi.second->publicity.isGlobal();
            auto p = modPath + vi.first;
            if (::std::any_of(state.crate.mLangItems.begin(), state.crate.mLangItems.end(), [&](const auto& e) {
                return e.second == p;
            })) {
                emit = true;
            }
            TransEnumerateValItem(state, vi.second->ent, emit, [&]() {
                return p;
            });
        }

        for (auto& ti : mod.modItems) {
            if (auto* e = ti.second->ent.opt_Module()) {
                TransEnumeratePublicMod(state, *e, modPath + ti.first, ti.second->publicity.isGlobal());
            } else if (const HIR::Trait* e = ti.second->ent.opt_Trait()) {
                auto params = e->mParams.makeEmptyParams(true);
                MonomorphStatePtr ms(state.crate.types);
                ms.ppImpl = &params;
                for (const auto& vi : e->values) {
                    if (const auto* fcn = vi.second.opt_Function()) {
                        TransEnumerateGenericFunctionItems(state, Span(), *fcn, ms);
                    }
                }
            }
        }
    }

    void TransEnumeratePublicTraitImpl(EnumState& state, StaticTraitResolve& resolve, const ::HIR::SimplePath& traitPath, /*const*/ ::HIR::TraitImpl& impl) {
        static Span sp;
        const auto& implTy = impl.mType;
        TRACE_FUNCTION_F("Impl" << impl.mParams.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << implTy);

        auto paramsImpl = impl.mParams.makeEmptyParams(true);
        MonomorphStatePtr ms(state.crate.types);
        ms.ppImpl = &paramsImpl;
        if (!impl.mParams.isGeneric()) {
            auto implParams = impl.mParams.makeEmptyParams(true);
            auto cbMonomorph = MonomorphStatePtr(state.crate.types, implTy, &impl.traitArgs, nullptr);
            auto cbMonomorph2 = MonomorphStatePtr(state.crate.types, nullptr, &implParams, nullptr);

            // TODO: Only emit impls if the type is going to be visible to downstream crates
            // - But how to tell that? What if the type is exposed via `-> impl Foo`?
            // - Lazy (wrong) version would be to not emit if the type is private - but private types can be leaked
            //   - Could flag leaked private types in a previous pass?

            // Emit each method/static (in the trait itself)
            const auto& trait = resolve.crate.getTraitByPath(sp, traitPath);
            for (const auto& vi : trait.values) {
                TRACE_FUNCTION_F("Item " << vi.first << " : " << vi.second.tagStr());
                // Constant, no codegen
                if (vi.second.is_Constant())
                    ;
                // Generic method, no codegen
                else if (vi.second.is_Function() && vi.second.as_Function().mParams.isGeneric())
                    ;
                // VTable, magic
                else if (vi.first == "vtable#")
                    ;
                else {
                    // Check bounds before queueing for codegen
                    HIR::PathParams pp;
                    if (vi.second.is_Function()) {
                        const auto& fcn = vi.second.as_Function();
                        bool rv = true;
                        DEBUG("Bounds = " << fcn.mParams.fmtBounds());
                        for (const auto& b : fcn.mParams.bounds) {
                            if (!b.is_TraitBound()) {
                                continue;
                            }
                            const auto& be = b.as_TraitBound();

                            auto bTyMono = resolve.monomorphExpand(sp, be.type, cbMonomorph);
                            auto bTpMono = cbMonomorph.monomorphTraitpath(sp, be.trait, false);
                            resolve.expandAssociatedTypesTp(sp, bTpMono);

                            DEBUG("Check " << bTyMono << ": " << bTpMono);
                            rv = resolve.findImpl(sp, bTpMono.mPath.mPath, bTpMono.mPath.mParams, bTyMono, [&](const ImplRef& impl, bool) {
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

                        DEBUG("Params = " << fcn.mParams.fmtArgs());
                        for (const auto& lft : fcn.mParams.mLifetimes) {
                            (void)lft;
                            pp.mLifetimes.push_back(HIR::LifetimeRef());
                        }
                    }
                    auto path = ::HIR::Path(cbMonomorph2.monomorphType(sp, implTy), ::HIR::GenericPath(traitPath, cbMonomorph2.monomorphPathParams(sp, impl.traitArgs, false)), vi.first, mv$(pp));
                    state.rv.roots.push_back(path.clone());
                    TransEnumerateFillFromPathMono(state, mv$(path));
                    //state.enum_fcn(mv$(path), fcn.second.data, {});
                }
            }
            for (auto& m : impl.methods) {
                if (m.second.data.mParams.isGeneric()) {
                    m.second.data.saveCode = true;
                    TransEnumerateGenericFunctionItems(state, Span(), m.second.data, ms);
                }
            }
        } else {
            for (auto& m : impl.methods) {
                m.second.data.saveCode = true;
                TransEnumerateGenericFunctionItems(state, Span(), m.second.data, ms);
            }
        }
    }
}

/// Enumerate trans items for all public non-generic items (library crate)
TransList TransEnumeratePublic(::HIR::Crate& crate) {
    static Span sp;
    EnumState state{crate};

    TransEnumeratePublicMod(state, crate.mRootModule, ::HIR::SimplePath(crate.crateName, {}), true);

    // Impl blocks
    StaticTraitResolve resolve{crate};
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
        static void enumerateTypeImpl(EnumState& state, ::HIR::TypeImpl& impl) {
            TRACE_FUNCTION_F("impl" << impl.mParams.fmtArgs() << " " << impl.mType);
            HIR::PathParams implParams = impl.mParams.makeEmptyParams(/*allow_lifetimes_only=*/true);
            MonomorphStatePtr ms(state.crate.types);
            ms.ppImpl = &implParams;
            if (!impl.mParams.isGeneric()) {
                for (auto& fcn : impl.methods) {
                    DEBUG("fn " << fcn.first << fcn.second.data.mParams.fmtArgs());
                    if (!fcn.second.data.mParams.isGeneric()) {
                        TransParams pp(state.crate.types);
                        pp.ppImpl = implParams.clone();
                        pp.ppMethod = fcn.second.data.mParams.makeEmptyParams(/*allow_lifetimes_only=*/true);
                        auto path = ::HIR::Path(MonomorphStatePtr(state.crate.types, nullptr, &implParams, nullptr).monomorphType(Span(), impl.mType), fcn.first);
                        path.mData.as_UfcsInherent().implParams = pp.ppImpl.clone();
                        path.mData.as_UfcsInherent().params = pp.ppMethod.clone();
                        if (fcn.second.publicity.isGlobal()) {
                            state.rv.roots.push_back(path.clone());
                        }
                        state.enumFcn(mv$(path), fcn.second.data, mv$(pp));
                    } else {
                        fcn.second.data.saveCode = true;
                    }
                    if (fcn.second.data.saveCode) {
                        TransEnumerateGenericFunctionItems(state, Span(), fcn.second.data, ms);
                    }
                }
            } else {
                for (auto& m : impl.methods) {
                    m.second.data.saveCode = true;
                    TransEnumerateGenericFunctionItems(state, Span(), m.second.data, ms);
                }
            }
            for (auto& e : impl.constants) {
                TransParams tp(state.crate.types);
                tp.ppImpl = impl.mParams.makeEmptyParams(/*allow_lifetimes_only=*/true);
                TransEnumerateFillFromLiteral(state, e.second.data.valueRes, std::move(tp));

                if (e.second.publicity.isGlobal() && !impl.mParams.isGeneric() && !e.second.data.mParams.isGeneric()) {
                    auto ppMethod = e.second.data.mParams.makeEmptyParams(/*allow_lifetimes_only=*/true);
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
        auto it = crate.mLangItems.find("mrustc-panic_implementation");
        if (it != crate.mLangItems.end()) {
            HIR::GenericPath p = it->second;
            const auto& f = crate.getFunctionByPath(Span(), p.mPath);
            p.mParams = f.mParams.makeEmptyParams(true);
            TransEnumerateFillFromPathMono(state, std::move(p));
        }
    }

    auto rv = TransEnumerateCommonPost(state);

    // Strip out any functions/types/statics that are still generic?
    for (auto it = rv.functions.begin(); it != rv.functions.end();) {
        if (monomorphisePathNeeded(it->first, /*ignore_lifetimes*/ true)) {
            rv.functions.erase(it++);
        } else {
            ++it;
        }
    }
    for (auto it = rv.statics.begin(); it != rv.statics.end();) {
        if (monomorphisePathNeeded(it->first, /*ignore_lifetimes*/ true)) {
            rv.statics.erase(it++);
        } else {
            ++it;
        }
    }

    return rv;
}

namespace {
    template <typename T>
    void removeMissing(std::map<HIR::Path, T>& target, const std::map<HIR::Path, T>& tpl) {
        ::std::unordered_map<::std::string, const HIR::Path*> requiredSymbols;
        for (const auto& entry : tpl) {
            auto symbol = FMT(TransMangle(entry.first));
            auto inserted = requiredSymbols.emplace(mv$(symbol), &entry.first);
            ASSERT_BUG(Span(), inserted.second || inserted.first->second->equalsIgnoringRegions(entry.first), "Distinct paths have the same mangled name: " << *inserted.first->second << " and " << entry.first);
        }

        for (auto itIn = target.begin(); itIn != target.end();) {
            const auto symbol = FMT(TransMangle(itIn->first));
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
}

void TransEnumerateCleanup(const ::HIR::Crate& crate, TransList& list) {
#if 1
    // Clear the function enum cache and re-generate
    // - This is called after optimisation, so the cache may point to functions that have been optimised out
    for (const auto& fcnE : list.functions) {
        auto& function = *fcnE.second->ptr;
        if (function.mCode.mir) {
            function.mCode.mir->transEnumState = MIR::EnumCachePtr();
        }
    }
    for (const auto& fcnE : list.functions) {
        auto& function = *fcnE.second->ptr;
        if (function.mCode.mir && !function.mCode.mir->transEnumState) {
            DEBUG(fcnE.first);
            auto* esp = new MIR::EnumCache();
            TransEnumerateFillFromMIR(*esp, *function.mCode.mir);
            function.mCode.mir->transEnumState = ::MIR::EnumCachePtr(esp);
        }
    }

    // Completely re-run enumeration, but this time include the TransList so MIR recursion uses the optimised versions
    EnumState state{crate};
    state.origList = &list;
    for (const auto& p : list.roots) {
        HIR::Path path = p.clone();
        MonomorphState unusedParams(state.crate.types);
        const auto& vi = state.resolve.getValue(Span(), path, unusedParams, /*signature_only=*/true);
        if (const auto* f = vi.opt_Function()) {
            TU_MATCH_HDRA( (path.mData), {)
            default:
                break;
                TU_ARMA(Generic, e) {
                    e.mParams.mLifetimes.resize((*f)->mParams.mLifetimes.size());
                }
                //TU_ARMA(Generic, e) {
                //    e.m_params.m_lifetimes.resize( (*f)->m_params.m_lifetimes.size() );
                //    }
            }
        } else {
            // Statics don't have lifetime params
        }
        TransEnumerateFillFromPathMono(state, std::move(path));
    }
    auto newList = TransEnumerateCommonPost(state);

    // Add stub entries to `new_list` for vtables and destructors, items that would be created by stages after enumerate
    // - VTables
    static RcString rcstringDropGlue = RcString::newInterned("#drop_glue");
    for (const auto& vtp : newList.vtables) {
        static Span sp;
        const auto& traitPath = vtp.first.mData.as_UfcsKnown().trait;
        const auto& type = vtp.first.mData.as_UfcsKnown().type;

        HIR::Path dropGlueFn(type, rcstringDropGlue);
        DEBUG("++ " << dropGlueFn);
        newList.functions.insert(std::make_pair(std::move(dropGlueFn), nullptr));

        DEBUG("++ " << vtp.first);
        newList.statics.insert(std::make_pair(vtp.first.clone(), nullptr));

        if (traitPath.mPath == HIR::SimplePath()) {
            // Non-data traits
            continue;
        }

        const auto& trait = crate.getTraitByPath(sp, traitPath.mPath);

        auto monomorphCbTrait = MonomorphStatePtr(state.crate.types, type, &traitPath.mParams, nullptr);
        for (unsigned int i = 0; i < trait.valueIndexes.size(); i++) {
            // Find the corresponding vtable entry
            for (const auto& m : trait.valueIndexes) {
                // NOTE: The "3" is the number of non-method vtable entries
                if (m.second.first != 3 + i) {
                    continue;
                }

                auto traitGpath = monomorphCbTrait.monomorphGenericpath(sp, m.second.second, false);
                auto itemPath = ::HIR::Path(type, mv$(traitGpath), m.first);

                DEBUG("++ " << itemPath);
                newList.functions.insert(std::make_pair(std::move(itemPath), nullptr));

                // If the entry is a by-value function, then emit a reference to a shim
                const auto& srcTrait = state.resolve.crate.getTraitByPath(sp, m.second.second.mPath);
                const auto& item = srcTrait.values.at(m.first);
                if (item.is_Function() && item.as_Function().receiver == HIR::Function::Receiver::Value) {
                    traitGpath = monomorphCbTrait.monomorphGenericpath(sp, m.second.second, false);
                    auto itemPath = ::HIR::Path(type, mv$(traitGpath), RcString::newInterned(FMT(m.first << "#ptr")));
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

        HIR::Path dropGlueFn(ty.first, rcstringDropGlue);
        DEBUG("++ " << dropGlueFn);
        newList.functions.insert(std::make_pair(std::move(dropGlueFn), nullptr));

        if (ty.first->is_Path() && ty.first->as_Path().binding.getTraitMarkings()->hasDropImpl) {
            auto fcnPath = ::HIR::Path(ty.first, state.resolve.mLangDrop, enumerateRcstringDrop);
            DEBUG("++ " << fcnPath);
            newList.functions.insert(std::make_pair(std::move(fcnPath), nullptr));
        }
    }
    for (const auto& ty : newList.autoCloneImpls) {
        static RcString rcstringCloneLower = RcString::newInterned("clone");
        HIR::Path fnPath(ty, crate.getLangItemPath(Span(), "clone"), rcstringCloneLower);
        DEBUG("++ " << fnPath);
        newList.functions.insert(std::make_pair(std::move(fnPath), nullptr));
    }
    for (const auto& fnPath : newList.traitObjectMethods) {
        DEBUG("++ " << fnPath);
        newList.functions.insert(std::make_pair(fnPath.clone(), nullptr));
    }
    for (const auto& ty : newList.autoFnptrImpls) {
        // - <fn(...) as FnPtr>::addr
        static RcString rcstringItem = RcString::newInterned("addr");
        HIR::Path fnPath(ty, crate.getLangItemPath(Span(), "fn_ptr_trait"), rcstringItem);
        DEBUG("++ " << fnPath);
        newList.functions.insert(std::make_pair(std::move(fnPath), nullptr));
    }

    removeMissing(list.functions, newList.functions);
    removeMissing(list.statics, newList.statics);
#endif
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
    struct PtrComp {
        template <typename T>
        bool operator()(const T* lhs, const T* rhs) const {
            return *lhs < *rhs;
        }
    };

    struct TypeVisitor {
        const ::HIR::Crate& crate;
        ::StaticTraitResolve mResolve;
        TransList& out;
        const TransList* prevList;

        ::std::set<::HIR::TypeRef> activeSet;

        TypeVisitor(const ::HIR::Crate& crate, TransList& out, const TransList* prevList)
            : crate(crate)
            , mResolve(crate)
            , out(out)
            , prevList(prevList)
        {
        }

        ~TypeVisitor() {
            DEBUG("Emitted a total of " << out.types.size() << " type entries");
        }

        void visitStruct(const ::HIR::GenericPath& path, const ::HIR::Struct& item) {
            static Span sp;
            ::HIR::TypeRef tmp;
            MonomorphStatePtr ms(crate.types, nullptr, &path.mParams, nullptr);
            auto monomorph = [&](const auto& x) {
                DEBUG(x);
                return mResolve.monomorphExpandOpt(sp, tmp, x, ms);
            };
            TU_MATCHA((item.mData), (e), (Unit, ), (Tuple, for (const auto& fld : e) { visitType(monomorph(fld.ent)); }), (Named, for (const auto& fld : e) visitType(monomorph(fld.ty));))
        }

        void visitUnion(const ::HIR::GenericPath& path, const ::HIR::Union& item) {
            static Span sp;
            ::HIR::TypeRef tmp;
            MonomorphStatePtr ms(crate.types, nullptr, &path.mParams, nullptr);
            auto monomorph = [&](const auto& x) {
                return mResolve.monomorphExpandOpt(sp, tmp, x, ms);
            };
            for (const auto& variant : item.mVariants) {
                visitType(monomorph(variant.ty));
            }
        }

        void visitEnum(const ::HIR::GenericPath& path, const ::HIR::Enum& item) {
            static Span sp;
            ::HIR::TypeRef tmp;
            MonomorphStatePtr ms(crate.types, nullptr, &path.mParams, nullptr);
            auto monomorph = [&](const auto& x) {
                return mResolve.monomorphExpandOpt(sp, tmp, x, ms);
            };
            if (const auto* e = item.mData.opt_Data()) {
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

        void visitType(const ::HIR::TypeData* ty, Mode mode = Mode::Normal) {
            Span sp;
            // If the type has already been visited, AND either this is a shallow visit, or the previous wasn't
            if (out.hasType(ty, mode == Mode::Shallow)) {
                return;
            }
            TRACE_FUNCTION_F(ty << " - " << (mode == Mode::Shallow ? "Shallow" : (mode == Mode::Normal ? "Normal" : "Deep")));

            if (mode == Mode::Shallow) {
                TU_MATCH_HDRA( (*ty), {)
                default:
                    break;
                    TU_ARMA(Infer, te) {
                        BUG(sp, "`_` type hit in enumeration");
                    }
                    TU_ARMA(Path, te) {
                        TU_MATCHA((te.binding), (tpb), (Unbound, BUG(sp, "Unbound type hit in enumeration - " << ty);), (Opaque, BUG(sp, "Opaque type hit in enumeration - " << ty);), (ExternType, ), (Struct, ), (Union, ), (Enum, ))
                    }
                    TU_ARMA(Array, te) {
                        ASSERT_BUG(sp, te.size.is_Known(), "Encountered unknown array size - " << ty);
                    }
                    TU_ARMA(Function, te) {
                        visitType(te.mRettype, Mode::Shallow);
                        for (const auto& sty : te.argTypes) {
                            visitType(sty, Mode::Shallow);
                        }
                    }
                    TU_ARMA(Pointer, te) {
                        visitType(te.inner, Mode::Shallow);
                    }
                    TU_ARMA(Borrow, te) {
                        visitType(te.inner, Mode::Shallow);
                    }
                }
            } else {
                if (activeSet.find(ty) != activeSet.end()) {
                    // TODO: Handle recursion
                    BUG(sp, "- Type recursion on " << ty);
                }
                activeSet.insert(ty);

                TU_MATCH_HDRA( (*ty), {)
                // Impossible
                TU_ARMA(Infer, te) {
                        BUG(sp, "`_` type hit in enumeration");
                    }
                    TU_ARMA(Generic, te) {
                        BUG(sp, "Generic type hit in enumeration - " << ty);
                    }
                    TU_ARMA(ErasedType, te) {
                        //BUG(sp, "ErasedType hit in enumeration - " << ty);
                    }
                    TU_ARMA(NodeType, te) {
                        BUG(sp, "NodeType type hit in enumeration - " << ty);
                    }
                    // Nothing to do
                    TU_ARMA(Diverge, te) {
                    }
                    TU_ARMA(Primitive, te) {
                    }
                    // Recursion!
                    TU_ARMA(Path, te) {
                        TU_MATCHA(
                            (te.binding),
                            (tpb),
                            (Unbound, BUG(sp, "Unbound type hit in enumeration - " << ty);),
                            (Opaque, BUG(sp, "Opaque type hit in enumeration - " << ty);),
                            (
                                ExternType,
                                // No innards to visit
                            ),
                            (Struct, visitStruct(te.path.mData.as_Generic(), *tpb);),
                            (Union, visitUnion(te.path.mData.as_Generic(), *tpb);),
                            (Enum,
                             // NOTE: Force repr generation before recursing into enums (allows layout optimisation to be calculated)
                             TargetGetTypeRepr(sp, mResolve, ty);
                             visitEnum(te.path.mData.as_Generic(), *tpb);)
                        )
                    }
                    TU_ARMA(TraitObject, te) {
                        static Span sp;

                        // If the data trait is empty, then no vtable to visit
                        if (!te.mTrait.mPath.mPath.components().empty()) {
                            // Ensure that the data trait's vtable is present
                            const auto& trait = *te.mTrait.traitPtr;
                            auto vtableTy = trait.getVtableType(sp, crate, te);

                            visitType(vtableTy);
                        } else {
                            // Wait, what vtable should be used then?
                        }
                    }
                    TU_ARMA(Array, te) {
                        ASSERT_BUG(sp, te.size.is_Known(), "Encountered unknown array size - " << ty);
                        visitType(te.inner, mode);
                    }
                    TU_ARMA(Slice, te) {
                        visitType(te.inner, mode);
                    }
                    TU_ARMA(Borrow, te) {
                        visitType(te.inner, mode != Mode::Deep ? Mode::Shallow : Mode::Deep);
                    }
                    TU_ARMA(Pointer, te) {
                        visitType(te.inner, mode != Mode::Deep ? Mode::Shallow : Mode::Deep);
                    }
                    TU_ARMA(Tuple, te) {
                        for (const auto& sty : te) {
                            visitType(sty, mode);
                        }
                    }
                    TU_ARMA(NamedFunction, te) {
                    }
                    TU_ARMA(Function, te) {
                        visitType(te.mRettype, mode != Mode::Deep ? Mode::Shallow : Mode::Deep);
                        for (const auto& sty : te.argTypes) {
                            visitType(sty, mode != Mode::Deep ? Mode::Shallow : Mode::Deep);
                        }
                    }
                }
                activeSet.erase(ty);
            }

            bool shallow = (mode == Mode::Shallow);
            auto i = out.types.size();
            ASSERT_BUG(sp, out.addType(ty, shallow), "Type was emitted while it was being visited: " << ty);
            DEBUG("Add type " << ty << (shallow ? " (Shallow)" : "") << " " << i);
        }

        void __attribute__((noinline)) visitFunction(const ::HIR::Path& path, const ::HIR::Function& fcn, const TransParams& pp) {
            Span sp;
            auto& tv = *this;

            ::HIR::TypeRef tmp;
            std::function<const HIR::TypeData*(const HIR::TypeData*)> monomorph = [&](const HIR::TypeData* ty) -> const HIR::TypeData* {
                return pp.maybeMonomorph(mResolve, tmp, ty);
            };
            DEBUG(fcn.returnType);
            bool hasErased = visitTyWith(fcn.returnType, [&](const auto& x) {
                return x->is_ErasedType();
            });
            // Handle erased types in the return type.
            if (hasErased || monomorphiseTypeNeeded(fcn.returnType)) {
                // If there's an erased type, make a copy with the erased type expanded
                ::HIR::TypeRef retTy;
                if (hasErased) {
                    retTy = cloneTyWith(crate.types, sp, fcn.returnType, [&](const auto& x, auto& out) {
                        if (const auto* te = x->opt_ErasedType()) {
                            if (const auto* e = te->inner.opt_Fcn()) {
                                out = fcn.mCode.erasedTypes.at(e->index);
                                return true;
                            }
                        }
                        return false;
                    });
                    DEBUG(retTy);
                    retTy = pp.monomorph(tv.mResolve, retTy);
                } else {
                    retTy = pp.monomorph(tv.mResolve, fcn.returnType);
                }
                tv.visitType(retTy);
            } else {
                tv.visitType(fcn.returnType);
            }
            for (const auto& arg : fcn.mArgs) {
                DEBUG(arg.second);
                tv.visitType(monomorph(arg.second));
            }

            const MIR::Function* mirP = nullptr;
            if (fcn.mCode.mir) {
                mirP = &*fcn.mCode.mir;
            }
            // If the previous list is populated, then this should be in it.
            if (prevList) {
                const auto* transFcn = prevList->findFunction(path);
                ASSERT_BUG(sp, transFcn, "Unable to find " << path << " in first-pass enumerate result");
                if (transFcn && transFcn->monomorphised.code) {
                    mirP = &*transFcn->monomorphised.code;
                    monomorph = [](const HIR::TypeData* ty) {
                        return ty;
                    };
                }
            }
            if (mirP) {
                const MIR::Function& mir = *mirP;
                for (const auto& ty : mir.locals) {
                    tv.visitType(monomorph(ty));
                }

                // Find all LValue::Deref instances and get the result type
                ::MIR::TypeResolve::argsT emptyArgs;
                ::HIR::TypeRef emptyTy;
                ::MIR::TypeResolve localMirRes(sp, tv.mResolve, FMT_CB(fcnPath), /*ret_ty=*/emptyTy, emptyArgs, mir);
                for (const auto& block : mir.blocks) {
                    struct MirVisitor: public ::MIR::MIRVisitor {
                        const Span& sp;
                        TypeVisitor& tv;
                        const TransParams& pp;
                        const ::HIR::Function& fcn;
                        const ::MIR::TypeResolve& localMirRes;

                        MirVisitor(const Span& sp, TypeVisitor& tv, const TransParams& pp, const ::HIR::Function& fcn, const ::MIR::TypeResolve& localMirRes)
                            : sp(sp)
                            , tv(tv)
                            , pp(pp)
                            , fcn(fcn)
                            , localMirRes(localMirRes)
                        {
                        }

                        bool visitLvalue(const ::MIR::LValue& lv, MIR::MIRValUsage /*vu*/) override {
                            TRACE_FUNCTION_F(lv);
                            if (::std::none_of(lv.wrappers.begin(), lv.wrappers.end(), [](const auto& w) {
                                return w.is_Deref();
                            })) {
                                return false;
                            }
                            ::HIR::TypeRef tmp;
                            auto monomorphOuter = [&](const auto& tpl) {
                                return pp.maybeMonomorph(tv.mResolve, tmp, tpl);
                            };
                            const ::HIR::TypeData* ty = nullptr;
                            ;
                            // Recurse, if Deref get the type and add it to the visitor
                            TU_MATCH_HDRA( (lv.root), {)
                            TU_ARMA(Return, e) {
                                MIR_TODO(localMirRes, "Get return type for MIR type enumeration");
                        }

                        TU_ARMA(Argument, e) {
                            ty = monomorphOuter(fcn.mArgs[e].second);
                        }

                        TU_ARMA(Local, e) {
                            if (&localMirRes.fcn == &*fcn.mCode.mir) {
                                ty = monomorphOuter(fcn.mCode.mir->locals[e]);
                            } else {
                                ty = localMirRes.fcn.locals[e];
                            }
                        }

                        TU_ARMA(Static, e) {
                            // TODO: Monomorphise the path then hand to MIR::TypeResolve?
                            const auto& path = e;
                                TU_MATCHA( (path.mData), (pe),
                                (Generic,
                                    MIR_ASSERT(localMirRes, pe.mParams.types.empty(), "Path params on static - " << path);
                                    const auto& s = tv.mResolve.crate.getStaticByPath(localMirRes.sp, pe.mPath);
                                    ty = s.mType;
                                    ),
                                (UfcsKnown,
                                    MIR_TODO(localMirRes, "LValue::Static - UfcsKnown - " << path);
                                    ),
                                (UfcsUnknown,
                                    MIR_BUG(localMirRes, "Encountered UfcsUnknown in LValue::Static - " << path);
                                    ),
                                (UfcsInherent,
                                    MIR_TODO(localMirRes, "LValue::Static - UfcsInherent - " << path);
                                    )
                        }
                                }
                            )
                            assert(ty);

                                for (const auto& w : lv.wrappers) {
                                    ty = localMirRes.getUnwrappedType(tmp, w, ty);
                                    if (w.is_Deref()) {
                                        tv.visitType(ty);
                                    }
                                }
                                return false;
                }

                void visitPath(const HIR::Path& /*p*/) override {
                    // Paths don't need visiting?
                }
                void visitType(const HIR::TypeData* ty) override {
                    HIR::TypeRef tmp;
                    tv.visitType(pp.maybeMonomorph(tv.mResolve, tmp, ty));
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
                    const auto& p = localMirRes.mResolve.crate.getLangItemPath(sp, "panic_location");
                    const auto& s = localMirRes.mResolve.crate.getStructByPath(sp, p);
                    tv.visitType(tv.crate.types.path(HIR::Path(p), &s));
                }
                // In 1.74+ the `offset` intrinsic takes a pointer as its generic
                else if (e2.name == "offset") {
                    HIR::TypeRef tmp;
                    const auto& ty = pp.maybeMonomorph(tv.mResolve, tmp, e2.params.types.at(0));
                    tv.visitType(ty->as_Pointer().inner);
                }
            }
            if (block.terminator.is_Call() && block.terminator.as_Call().fcn.is_Path()) {
                const auto& p = block.terminator.as_Call().fcn.as_Path();
                if (p.mData.is_UfcsKnown()) {
                    HIR::TypeRef tmp;
                    const auto& ty = pp.maybeMonomorph(tv.mResolve, tmp, p.mData.as_UfcsKnown().type);
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
    static Span sp;
    TypeVisitor tv{state.crate, state.rv, state.origList};

    unsigned int typesCount = 0;
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

            tv.visitType(pp.monomorph(tv.mResolve, stat.mType));
        }
        // - Constants need visiting, as they will be expanded
        for (const auto& ent : state.rv.constants) {
            TRACE_FUNCTION_F("Enumerate constant " << ent.first);
            assert(ent.second->ptr);
            const auto& stat = *ent.second->ptr;
            const auto& pp = ent.second->pp;

            tv.visitType(pp.monomorph(tv.mResolve, stat.mType));
        }
        for (const auto& ent : state.rv.vtables) {
            TRACE_FUNCTION_F("vtable " << ent.first);
            const auto& ty = ent.first.mData.as_UfcsKnown().type;
            const auto& gpath = ent.first.mData.as_UfcsKnown().trait;
            if (gpath.mPath == HIR::SimplePath()) {
                ::std::vector<HIR::TypeRef> tupleTys;
                tupleTys.push_back(state.crate.types.primitive(::HIR::CoreType::Usize));
                tupleTys.push_back(state.crate.types.primitive(::HIR::CoreType::Usize));
                tupleTys.push_back(state.crate.types.primitive(::HIR::CoreType::Usize)); // fn
                auto vtableTy = state.crate.types.tuple(std::move(tupleTys));
                tv.visitType(ty);
                tv.visitType(vtableTy);
                continue;
            }
            const auto& trait = state.crate.getTraitByPath(sp, gpath.mPath);

            const auto& vtableTySpath = trait.vtablePath;
            const auto& vtableRef = state.crate.getStructByPath(sp, vtableTySpath);
            // Copy the param set from the trait in the trait object
            ::HIR::PathParams vtableParams = gpath.mParams.clone();
            // - Include associated types on bound
            for (const auto& tyIdx : trait.typeIndexes) {
                auto idx = tyIdx.second;
                if (vtableParams.types.size() <= idx) {
                    vtableParams.types.resize(idx + 1);
                }
                auto p = ent.first.clone();
                p.mData.as_UfcsKnown().item = tyIdx.first;
                vtableParams.types[idx] = state.crate.types.path(mv$(p), {});
                tv.mResolve.expandAssociatedTypes(sp, vtableParams.types[idx]);
            }
            DEBUG("VTable: " << vtableTySpath << vtableParams);

            tv.visitType(ty);
            tv.visitType(state.crate.types.path(::HIR::Path(::HIR::GenericPath(vtableTySpath, mv$(vtableParams))), &vtableRef));

            // If this is for a function pointer, visit all arguments
            // - `auto_impls.cpp` will generate a vtable shim for it (which requires argument types to be fully known)
            // NOTE: Assumes that the trait is one of the Fn* traits (doesn't matter if it isn't here)
            if (const auto* te = ty->opt_Function()) {
                for (const auto& t : te->argTypes) {
                    tv.visitType(t);
                }
                tv.visitType(te->mRettype);

                if (gpath.mParams.types.size() >= 1) {
                    tv.visitType(gpath.mParams.types[0]);
                }
            }

            if (gpath.mPath == state.resolve.mLangFn || gpath.mPath == state.resolve.mLangFnMut || gpath.mPath == state.resolve.mLangFnOnce) {
                tv.visitType(gpath.mParams.types[0]);
            }
        }
        for (const auto& ty : state.rv.autoCloneImpls) {
            tv.visitType(ty);
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
                ASSERT_BUG(sp, te.path.mData.is_Generic(), "Non-Generic type path after enumeration - " << ty);
                const auto& gp = te.path.mData.as_Generic();
                const ::HIR::TraitMarkings* markingsPtr = te.binding.getTraitMarkings();
                ASSERT_BUG(sp, markingsPtr, "Path binding not set correctly - " << ty);

                // If the type has a drop impl, and it's either defined in this crate or has params (and thus was monomorphised)
                if (markingsPtr->hasDropImpl && (gp.mPath.crateName() == state.crate.crateName || gp.mParams.hasParams())) {
                    // Add the Drop impl to the codegen list
                    TransEnumerateFillFromPathMono(state, ::HIR::Path(ty, state.crate.getLangItemPath(sp, "drop"), enumerateRcstringDrop, HIR::PathParams(HIR::LifetimeRef())));
                    constructorsAdded = true;
                }
            }

            if (const auto* ity = tv.mResolve.isTypeOwnedBox(ty)) {
                // NOTE: Save the params before visiting, as the TypeRef might move as types are added, but the inner data won't move
                const auto& p = ty->as_Path().path.mData.as_Generic().mParams;
                tv.visitType(ity);
            }
        }
        typesCount = state.rv.types.size();

        // Run queue
        TransEnumerateCommonPostRun(state);
    } while (constructorsAdded);
}

namespace {
    TAGGED_UNION(EntPtr, NotFound, (NotFound, struct {}), (AutoGenerate, struct {}), (Function, const ::HIR::Function*), (Static, const ::HIR::Static*), (Constant, const ::HIR::Constant*));

    bool pathAlreadyEnumerated(const EnumState& state, const ::HIR::Path& path) {
        return state.rv.functions.count(path) || state.rv.statics.count(path) || state.rv.constants.count(path) || state.rv.vtables.count(path);
    }

    void evaluateTranslationParams(const Span& sp, const ::HIR::Crate& crate, const ::HIR::GenericParams* defs, ::HIR::PathParams& params) {
        if (params.values.empty()) {
            return;
        }

        ASSERT_BUG(sp, defs, "Missing const parameter definitions for " << params);
        ASSERT_BUG(sp, params.values.size() <= defs->values.size(), "Too many const parameters in " << params << " for " << defs->fmtArgs());
        for (size_t i = 0; i < params.values.size(); i++) {
            auto& value = params.values[i];
            if (value.is_Unevaluated()) {
                const auto& type = defs->values[i].mType;
                ASSERT_BUG(sp, !monomorphiseTypeNeeded(type), "Generic const parameter type " << type << " in " << defs->fmtArgs());
                ConvertHIRConstantEvaluateConstGeneric(sp, crate, type, value);
            }
            ASSERT_BUG(sp, value.is_Evaluated(), "Const parameter was not concrete at translation: " << value);
        }
    }

    void evaluateTranslationImplAndTraitParams(const Span& sp, const ::HIR::Crate& crate, ::HIR::Path& path, TransParams& pp) {
        evaluateTranslationParams(sp, crate, pp.gdefImpl, pp.ppImpl);

        TU_MATCH_HDRA((path.mData), {)
        TU_ARMA(Generic, _pe) {
            }
            TU_ARMA(UfcsKnown, pe) {
                // An empty trait path is the marker-only vtable sentinel. It
                // has no trait parameters to evaluate; the vtable enumerator
                // handles this representation directly.
                if (pe.trait.mPath != HIR::SimplePath()) {
                    const auto& trait = crate.getTraitByPath(sp, pe.trait.mPath);
                    evaluateTranslationParams(sp, crate, &trait.mParams, pe.trait.mParams);
                }
            }
            TU_ARMA(UfcsInherent, pe) {
                evaluateTranslationParams(sp, crate, pp.gdefImpl, pe.implParams);
            }
            TU_ARMA(UfcsUnknown, _pe) {
                BUG(sp, "UfcsUnknown at translation: " << path);
            }
        }
    }

    void evaluateTranslationItemParams(const Span& sp, const ::HIR::Crate& crate, const ::HIR::GenericParams& defs, ::HIR::Path& path, TransParams& pp) {
        evaluateTranslationParams(sp, crate, &defs, pp.ppMethod);

        TU_MATCH_HDRA((path.mData), {)
        TU_ARMA(Generic, pe) {
                evaluateTranslationParams(sp, crate, &defs, pe.mParams);
            }
            TU_ARMA(UfcsKnown, pe) {
                evaluateTranslationParams(sp, crate, &defs, pe.params);
            }
            TU_ARMA(UfcsInherent, pe) {
                evaluateTranslationParams(sp, crate, &defs, pe.params);
            }
            TU_ARMA(UfcsUnknown, _pe) {
                BUG(sp, "UfcsUnknown at translation: " << path);
            }
        }
    }

    EntPtr getEntFullpath(const Span& sp, const ::HIR::Crate& crate, const ::HIR::Path& path, TransParams& params) {
        TRACE_FUNCTION_F(path);
        StaticTraitResolve resolve{crate};

        if (path.mData.is_UfcsInherent() && path.mData.as_UfcsInherent().item == "#type_id") {
            return EntPtr::make_AutoGenerate({});
        }

        MonomorphState ms(crate.types);
        params.gdefImpl = nullptr;
        auto ent = resolve.getValue(sp, path, ms, /*signature_only=*/false, &params.gdefImpl);
        if (ms.getImplParams()) {
            params.ppImpl = ms.getImplParams()->clone();
            if (params.ppImpl.hasParams()) {
                assert(params.gdefImpl);
            }
        }
        DEBUG(path << " = " << ent.tagStr() << " w/ impl" << params.ppImpl);
        TU_MATCH_HDRA( (ent), {)
        default:
            TODO(sp, path << " was " << ent.tagStr());
            TU_ARMA(NotYetKnown, _e) {
                const auto* pe = &path.mData.as_UfcsKnown();
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
                resolve.findImpl(sp, pe->trait.mPath, pe->trait.mParams, pe->type, [&](auto implRef, auto isFuzz) -> bool {
                    DEBUG("[get_ent_fullpath] Found " << implRef);
                    if (implRef.mData.is_TraitImpl()) {
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
            TU_ARMA(Function, f) {
                // Check for trait provided bodies
                // - They need a little hack to ensure that monomorph is run
                if (const auto* pe = path.mData.opt_UfcsKnown()) {
                    const auto& traitRef = crate.getTraitByPath(sp, pe->trait.mPath);
                    const auto& traitVi = traitRef.values.at(pe->item);

                    if (f == &traitVi.as_Function()) {
                        DEBUG("Default trait body");
                        params.forceMonomorphisation = true;
                    }
                }
                return EntPtr{f};
            }
            TU_ARMA(Static, f) {
                return EntPtr{f};
            }
            TU_ARMA(Constant, f) {
                return EntPtr{f};
            }
            TU_ARMA(StructConstructor, _) {
                return EntPtr::make_AutoGenerate({});
            }
            TU_ARMA(EnumConstructor, _) {
                return EntPtr::make_AutoGenerate({});
            }
        }
        throw "";
    }
}

void TransEnumerateFillFromPath(EnumState& state, const ::HIR::Path& path, const TransParams& pp) {
    auto pathMono = pp.monomorph(state.resolve, path);
    TransEnumerateFillFromPathMono(state, mv$(pathMono));
}

void TransEnumerateFillFromPathMono(EnumState& state, ::HIR::Path pathMono) {
    Span sp;
    bindTranslationNominals(state.crate, pathMono);
    TRACE_FUNCTION_F(pathMono);
    // Don't want duplicates of lifetime-generic items
    ASSERT_BUG(sp, !monomorphisePathNeeded(pathMono, /*ignore_lifetimes=*/false), "Path " << pathMono << " is generic");
    // TODO: If already in the list, return early
    if (pathAlreadyEnumerated(state, pathMono)) {
        DEBUG("> Already enumerated");
        return;
    }

    TransParams subPp(state.crate.types, sp);
    TU_MATCH_HDRA( (pathMono.mData), { )
    TU_ARMA(Generic, pe) {
            subPp.ppMethod = pe.mParams.clone();
        }
        TU_ARMA(UfcsKnown, pe) {
            subPp.ppMethod = pe.params.clone();
            subPp.selfType = pe.type;
        }
        TU_ARMA(UfcsInherent, pe) {
            subPp.ppMethod = pe.params.clone();
            subPp.ppImpl = pe.implParams.clone();
            subPp.selfType = pe.type;
        }
        TU_ARMA(UfcsUnknown, pe) {
            BUG(sp, "UfcsUnknown - " << pathMono);
        }
    }
    // Get the item type
    // - Valid types are Function and Static
    auto itemRef = getEntFullpath(sp, state.crate, pathMono, subPp);
    DEBUG("item_ref.tag_str() = " << itemRef.tagStr());
    DEBUG("sub_pp.pp_method = " << subPp.ppMethod);
    DEBUG("sub_pp.pp_impl = " << subPp.ppImpl);
    evaluateTranslationImplAndTraitParams(sp, state.crate, pathMono, subPp);
    TU_MATCH_HDRA( (itemRef), {)
    TU_ARMA(NotFound, e) {
            BUG(sp, "Item not found for " << pathMono);
        }
        TU_ARMA(AutoGenerate, e) {
            if (pathAlreadyEnumerated(state, pathMono)) {
                DEBUG("> Already enumerated after const evaluation");
                return;
            }
            if (pathMono.mData.is_Generic()) {
                // Leave generation of struct/enum constructors to codgen
                // TODO: Add to a list of required constructors
                state.rv.constructors.insert(mv$(pathMono.mData.as_Generic()));
            }
            // - <T>::#type_id
            else if (pathMono.mData.is_UfcsInherent() && pathMono.mData.as_UfcsInherent().item == "#type_id") {
                state.rv.typeids.insert(pathMono.mData.as_UfcsInherent().type);
            }
            // - <T as U>::#vtable
            else if (pathMono.mData.is_UfcsKnown() && pathMono.mData.as_UfcsKnown().item == "vtable#") {
                if (state.rv.addVtable(pathMono.clone(), TransParams(state.crate.types))) {
                    // Fill from the vtable
                    TransEnumerateFillFromVTable(state, mv$(pathMono), subPp);
                }
            }
            // - <(Trait) as Trait>::method
            else if (pathMono.mData.is_UfcsKnown() && pathMono.mData.as_UfcsKnown().type->is_TraitObject()) {
                state.rv.traitObjectMethods.insert(mv$(pathMono));
            }
            // - <fn(...) as Fn*>::call*
            else if (pathMono.mData.is_UfcsKnown() && pathMono.mData.as_UfcsKnown().type->is_Function() && (pathMono.mData.as_UfcsKnown().trait.mPath == state.crate.getLangItemPathOpt("fn") || pathMono.mData.as_UfcsKnown().trait.mPath == state.crate.getLangItemPathOpt("fn_mut") || pathMono.mData.as_UfcsKnown().trait.mPath == state.crate.getLangItemPathOpt("fn_once"))) {
                // Must have been a dynamic dispatch request, just leave as-is
                // - However, ensure that all arguments are visited?
                //const auto& fcn_ty = path_mono.m_data.as_UfcsKnown().type->as_Function();
                //for(const auto& ty : fcn_ty.m_arg_types)
                //    state.rv.vi
            }
            // - <fn{...} as Fn*>::call*
            else if (pathMono.mData.is_UfcsKnown() && pathMono.mData.as_UfcsKnown().type->is_NamedFunction() && (pathMono.mData.as_UfcsKnown().trait.mPath == state.crate.getLangItemPathOpt("fn") || pathMono.mData.as_UfcsKnown().trait.mPath == state.crate.getLangItemPathOpt("fn_mut") || pathMono.mData.as_UfcsKnown().trait.mPath == state.crate.getLangItemPathOpt("fn_once"))) {
                // Calling a non-dynamic function, need to visit that function
                TransEnumerateFillFromPath(state, pathMono.mData.as_UfcsKnown().type->as_NamedFunction().path, subPp);
            }
            // - <fn(...) as FnPtr>::addr
            else if (pathMono.mData.is_UfcsKnown() && pathMono.mData.as_UfcsKnown().type->is_Function() && pathMono.mData.as_UfcsKnown().trait.mPath == state.crate.getLangItemPathOpt("fn_ptr_trait")) {
                state.rv.autoFnptrImpls.insert(pathMono.mData.as_UfcsKnown().type);
            }
            // <* as Clone>::clone
            else if (pathMono.mData.is_UfcsKnown() && pathMono.mData.as_UfcsKnown().trait == state.crate.getLangItemPathOpt("clone")) {
                const auto& pe = pathMono.mData.as_UfcsKnown();
                ASSERT_BUG(sp, pe.item == "clone" || pe.item == "clone_from", "Unexpected Clone method called, " << pathMono);
                const auto& innerTy = pe.type;
                // If this is !Copy, then we need to ensure that the inner type's clone impls are also available
                ::StaticTraitResolve resolve{state.crate};
                if (!resolve.typeIsCopy(sp, innerTy)) {
                    auto enumImpl = [&](const ::HIR::TypeData* ity) {
                        if (!resolve.typeIsCopy(sp, ity)) {
                            auto innerPp = HIR::PathParams(HIR::LifetimeRef());
                            if (pe.item == "clone_from") {
                                innerPp.mLifetimes.push_back(HIR::LifetimeRef());
                            }
                            TransEnumerateFillFromPathMono(state, ::HIR::Path(ity, pe.trait.clone(), pe.item, mv$(innerPp)));
                        }
                    };
                    if (const auto* te = innerTy->opt_Tuple()) {
                        for (const auto& ity : *te) {
                            enumImpl(ity);
                        }
                    } else if (const auto* te = innerTy->opt_Array()) {
                        enumImpl(te->inner);
                    } else if (TU_TEST1(*innerTy, Path, .isClosure())) {
                        const auto& gp = innerTy->as_Path().path.mData.as_Generic();
                        const auto& str = state.crate.getStructByPath(sp, gp.mPath);
                        auto p = TransParams::newImpl(state.crate.types, sp, {}, gp.mParams.clone());
                        for (const auto& fld : str.mData.as_Tuple()) {
                            ::HIR::TypeRef tmp;
                            const auto& tyM = monomorphiseTypeNeeded(fld.ent) ? (tmp = p.monomorph(resolve, fld.ent)) : fld.ent;
                            enumImpl(tyM);
                        }
                    } else {
                        BUG(sp, "Unhandled magic clone in enumerate - " << innerTy);
                    }
                }
                // Add this type to a list of types that will have the impl auto-generated
                state.rv.autoCloneImpls.insert(innerTy);
            } else {
                BUG(sp, "AutoGenerate returned for unknown path type - " << pathMono);
            }
        }
        TU_ARMA(Function, e) {
            evaluateTranslationItemParams(sp, state.crate, e->mParams, pathMono, subPp);
            if (pathAlreadyEnumerated(state, pathMono)) {
                DEBUG("> Already enumerated after const evaluation");
                return;
            }
            // Add this path (monomorphised) to the queue
            state.enumFcn(mv$(pathMono), *e, mv$(subPp));
        }
        TU_ARMA(Static, e) {
            evaluateTranslationItemParams(sp, state.crate, e->mParams, pathMono, subPp);
            if (pathAlreadyEnumerated(state, pathMono)) {
                DEBUG("> Already enumerated after const evaluation");
                return;
            }
            if (auto* ptr = state.rv.addStatic(state.crate.types, mv$(pathMono))) {
                TransEnumerateFillFromStatic(state, *e, *ptr, mv$(subPp));
            }
        }
        TU_ARMA(Constant, e) {
            evaluateTranslationItemParams(sp, state.crate, e->mParams, pathMono, subPp);
            if (pathAlreadyEnumerated(state, pathMono)) {
                DEBUG("> Already enumerated after const evaluation");
                return;
            }
            switch (e->valueState) {
                case HIR::Constant::ValueState::Unknown:
                    BUG(sp, "Unevaluated constant: " << pathMono);
                case HIR::Constant::ValueState::Generic:
                    if (auto* slot = state.rv.addConst(state.crate.types, mv$(pathMono))) {
                        MIR::EnumCache es;
                        TransEnumerateFillFromMIR(es, *e->mValue.mir);
                        es.apply(state, subPp);
                        slot->ptr = e;
                        slot->pp = ::std::move(subPp);
                    }
                    break;
                case HIR::Constant::ValueState::Known:
                    TransEnumerateFillFromLiteral(state, e->valueRes, subPp);
                    break;
            }
        }
    }
}

void TransEnumerateFillFromMIRLValue(MIR::EnumCache& state, const ::MIR::LValue& lv) {
    if (lv.root.is_Static()) {
        state.insertPath(lv.root.as_Static());
    }
}

void TransEnumerateFillFromMIRConstant(MIR::EnumCache& state, const ::MIR::Constant& c) {
    TU_MATCHA(
        (c),
        (ce),
        (Int, ),
        (Uint, ),
        (Float, ),
        (Bool, ),
        (Bytes, ),
        (StaticString, ), // String
        (Const,
         // - Check if this constant has a value of Defer
         state.insertPath(*ce.p);),
        (Generic, ),
        (Function, state.insertPath(*ce.p);),
        (ItemAddr, if (ce) state.insertPath(*ce);)
    )
}

void TransEnumerateFillFromMIRParam(MIR::EnumCache& state, const ::MIR::Param& p) {
    TU_MATCHA((p), (e), (LValue, TransEnumerateFillFromMIRLValue(state, e);), (Borrow, TransEnumerateFillFromMIRLValue(state, e.val);), (Constant, TransEnumerateFillFromMIRConstant(state, e);))
}

void TransEnumerateFillFromMIR(MIR::EnumCache& state, const ::MIR::Function& code) {
    TRACE_FUNCTION_F("");
    for (const auto& ty : code.locals) {
        visitTyWith(ty, [&state](const HIR::TypeData* t) -> bool {
            if (const auto* te = t->opt_NamedFunction()) {
                state.insertPath(te->path);
            }
            return false;
        });
    }
    for (const auto& bb : code.blocks) {
        for (const auto& stmt : bb.statements) {
            TU_MATCH_HDRA((stmt), {)
            TU_ARMA(Assign, se) {
                    DEBUG("- " << se.dst << " = " << se.src);
                    TransEnumerateFillFromMIRLValue(state, se.dst);
                    TU_MATCHA((se.src), (e), (Use, TransEnumerateFillFromMIRLValue(state, e);), (Constant, TransEnumerateFillFromMIRConstant(state, e);), (SizedArray, TransEnumerateFillFromMIRParam(state, e.val);), (Borrow, TransEnumerateFillFromMIRLValue(state, e.val);), (Cast, TransEnumerateFillFromMIRLValue(state, e.val);), (BinOp, TransEnumerateFillFromMIRParam(state, e.valL); TransEnumerateFillFromMIRParam(state, e.valR);), (UniOp, TransEnumerateFillFromMIRLValue(state, e.val);), (DstMeta, TransEnumerateFillFromMIRLValue(state, e.val);), (DstPtr, TransEnumerateFillFromMIRLValue(state, e.val);), (MakeDst, TransEnumerateFillFromMIRParam(state, e.ptrVal); TransEnumerateFillFromMIRParam(state, e.metaVal);), (Tuple, for (const auto& val : e.vals) TransEnumerateFillFromMIRParam(state, val);), (Array, for (const auto& val : e.vals) TransEnumerateFillFromMIRParam(state, val);), (UnionVariant, TransEnumerateFillFromMIRParam(state, e.val);), (EnumVariant, for (const auto& val : e.vals) TransEnumerateFillFromMIRParam(state, val);), (Struct, for (const auto& val : e.vals) TransEnumerateFillFromMIRParam(state, val);))
                }
                TU_ARMA(Asm2, e) {
                    for (auto& p : e.params) {
                    TU_MATCH_HDRA( (p), { )
                    TU_ARMA(Const, v) TransEnumerateFillFromMIRConstant(state, v);
                            TU_ARMA(Sym, v) state.insertPath(v);
                            TU_ARMA(Reg, v) {
                                if (v.input) {
                                    TransEnumerateFillFromMIRParam(state, *v.input);
                                }
                                if (v.output) {
                                    TransEnumerateFillFromMIRLValue(state, *v.output);
                                }
                            }
                    }
                    }
                }
                TU_ARMA(Asm, se) {
                    DEBUG("- llvm_asm! ...");
                    for (const auto& v : se.inputs) {
                        TransEnumerateFillFromMIRLValue(state, v.second);
                    }
                    for (const auto& v : se.outputs) {
                        TransEnumerateFillFromMIRLValue(state, v.second);
                    }
                }
                TU_ARMA(SetDropFlag, se) {
                }
                TU_ARMA(SaveDropFlag, se) {
                    TransEnumerateFillFromMIRLValue(state, se.slot);
                }
                TU_ARMA(LoadDropFlag, se) {
                    TransEnumerateFillFromMIRLValue(state, se.slot);
                }
                TU_ARMA(ScopeEnd, se) {
                }
            }
        }
        DEBUG("> " << bb.terminator);
        TU_MATCHA((bb.terminator), (e), (Incomplete, ), (Return, ), (UnwindResume, ), (UnwindTerminate, ), (Unreachable, ), (Goto, ), (If, TransEnumerateFillFromMIRLValue(state, e.cond);), (Switch, TransEnumerateFillFromMIRLValue(state, e.val);), (SwitchValue, TransEnumerateFillFromMIRLValue(state, e.val);), (Drop, TransEnumerateFillFromMIRLValue(state, e.slot);), (Call, TransEnumerateFillFromMIRLValue(state, e.retVal); TU_MATCHA((e.fcn), (e2), (Value, TransEnumerateFillFromMIRLValue(state, e2);), (Path, state.insertPath(e2);), (Intrinsic, if (e2.name == "type_id") {
                                                                                                                                                                                                                                                                                                                                                                                                                                                      // Add <T>::#type_id to the enumerate list
                                                                                                                                                                                                                                                                                                                                                                                                                                                      state.insertTypeid(e2.params.types.at(0));
                                                                                                                                                                                                                                                                                                                                                                                                                                                  })) for (const auto& arg : e.args) TransEnumerateFillFromMIRParam(state, arg);))
    }
}

void TransEnumerateFillFromVTable(EnumState& state, ::HIR::Path vtablePath, const TransParams& pp) {
    static Span sp;
    const auto& type = vtablePath.mData.as_UfcsKnown().type;
    const auto& traitPath = vtablePath.mData.as_UfcsKnown().trait;
    if (traitPath == HIR::SimplePath()) {
        // TODO: Ensure that the drop glue is available
        return;
    }
    const auto& tr = state.crate.getTraitByPath(Span(), traitPath.mPath);

    ASSERT_BUG(sp, !type->is_Slice(), "Getting vtable for unsized type - " << vtablePath);
    ASSERT_BUG(sp, !type->is_TraitObject(), "Getting vtable for unsized type - " << vtablePath);

    auto monomorphCbTrait = MonomorphStatePtr(state.crate.types, type, &traitPath.mParams, nullptr);
    for (const auto& m : tr.valueIndexes) {
        DEBUG("- " << m.second.first << " = " << m.second.second << " :: " << m.first);
        auto gpath = monomorphCbTrait.monomorphGenericpath(sp, m.second.second, false);
        const auto& fcn = state.crate.getTraitByPath(sp, gpath.mPath).values.at(m.first).as_Function();
        TransEnumerateFillFromPathMono(state, ::HIR::Path(type, mv$(gpath), m.first, fcn.mParams.makeEmptyParams(true)));
    }
    for (const auto& ptPath : tr.allParentTraits) {
        ASSERT_BUG(sp, ptPath.traitPtr, "Unset trait pointer - " << ptPath);
        const auto& pt = *ptPath.traitPtr;
        if (pt.vtablePath != HIR::SimplePath()) {
            auto ptMono = MonomorphStatePtr(state.crate.types, nullptr, &traitPath.mParams, nullptr).monomorphGenericpath(sp, ptPath.mPath);
            auto ptVtablePath = ::HIR::Path(type, mv$(ptMono), vtablePath.mData.as_UfcsKnown().item);
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

void TransEnumerateFillFromFunction(EnumState& state, const HIR::Path& p, const ::HIR::Function& function, const TransParams& pp) {
    TRACE_FUNCTION_F("Function " << p << " pp=" << pp.ppImpl << " + " << pp.ppMethod);
    if (!function.mCode.mir) {
        // External.
        if (function.linkage.name != "") {
            // Search for a function with the same linkage name anywhere in the loaded crates
            auto it = state.linkFunctions.find(function.linkage.name);
            if (it != state.linkFunctions.end()) {
                state.enumFcn(::HIR::Path(it->second.first), *it->second.second, TransParams(state.crate.types, pp.sp));
            }
        }
    } else if (state.origList) {
        const auto* transFcn = state.origList->findFunction(p);
        if (transFcn) {
            if (transFcn->monomorphised.code) {
                DEBUG("Monomorphised");
                MIR::EnumCache ec;
                TransEnumerateFillFromMIR(ec, *transFcn->monomorphised.code);
                ec.apply(state, pp);
            } else if (transFcn->ptr->mCode.mir) {
                DEBUG("Concrete");
                MIR::EnumCache ec;
                TransEnumerateFillFromMIR(ec, *transFcn->ptr->mCode.mir);
                ec.apply(state, pp);
            } else {
                DEBUG("No code");
            }
        } else {
            ASSERT_BUG(Span(), transFcn, "Missing " << p << " in input TransList?");
        }
    } else {
        const auto& mirFcn = *function.mCode.mir;
        if (!mirFcn.transEnumState) {
            auto* esp = new MIR::EnumCache();
            TransEnumerateFillFromMIR(*esp, *function.mCode.mir);
            mirFcn.transEnumState = ::MIR::EnumCachePtr(esp);
        }
        // TODO: Ensure that all types have drop glue generated too? (Iirc this is unconditional currently)
        mirFcn.transEnumState->apply(state, pp);
    }
}

void TransEnumerateFillFromStatic(EnumState& state, const ::HIR::Static& item, TransListStatic& outStat, TransParams pp) {
    // HACK: Ensure that lifetimes are populated.
    pp.ppMethod.mLifetimes.resize(item.mParams.mLifetimes.size());

    if (item.mParams.isGeneric()) {
        MIR::EnumCache es;
        TransEnumerateFillFromMIR(es, *item.mValue.mir);
        es.apply(state, pp);
    } else if (item.mType->is_Infer()) {
        BUG(Span(), "Enumerating static with no assigned type (unused elevated literal)");
    } else if (item.valueGenerated) {
        TransEnumerateFillFromLiteral(state, item.valueRes, pp);
    }
    outStat.ptr = &item;
    outStat.pp = mv$(pp);
}
