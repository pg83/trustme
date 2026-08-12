#include "mir_from_hir.h"
#include <type_traits> // for TU_MATCHA
#include <algorithm>
#include "mir_mir.h"
#include "mir_mir_ptr.h"
#include "hir_expr.h"
#include "hir_hir.h"
#include "hir_visitor.h"
#include "hir_typeck_common.h" // monomorphise_type
#include "mir_main_bindings.h"
#include "mir_operations.h"
#include "mir_visit_crate_mir.h"
#include "hir_expr_state.h"
#include "trans_target.h" // Target_GetSizeAndAlignOf - for `box`
#include <cctype>           // isdigit
#include "mir_helpers.h"
#include <numeric>
#include <limits> // std::numeric_limits
#include "hir_conv_main_bindings.h" // For consteval

namespace {
    class ExprVisitorConv: public MirConverter {
        MirBuilder& builder;

        const ::std::vector<::HIR::TypeRef>& variableTypes;

        /// Generators do some different codegen quirks
        bool isGenerator;

        struct LoopDesc {
            ScopeHandle scope;
            RcString label;
            bool require_label;
            unsigned int cur;
            unsigned int next;
            ::MIR::LValue resValue;
        };

        ::std::vector<LoopDesc> loopStack;

        const ScopeHandle* blockTmpScope = nullptr;
        const ScopeHandle* blockVarScope = nullptr;
        const ScopeHandle* borrowRaiseTarget = nullptr;
        const ScopeHandle* stmtScope = nullptr;
        const ScopeHandle* superLetScope = nullptr;
        bool inBorrow = false;

        struct GeneratorState {
            struct State {
                /// Entrypoint for the state
                MIR::BasicBlockId entrypoint;
                /// List of saved variables when this state yields
                std::map<unsigned, MirBuilder::SavedActiveLocal> saved;

                State(MIR::BasicBlockId entry)
                    : entrypoint(entry)
                {
                }
            };

            // Basic block to be terminated with the state switch
            MIR::BasicBlockId bbOpen;
            /// Yield points/states
            std::vector<State> states;

            // Set of drop flags that are stored in the output state
            // These are stored in a bit-set at the end of the state structure, and remapped after lower (with sets being writes,
            // and then re-read before use)
            std::set<unsigned> savedDropFlags;

            /// Path to the enum used for the state index field (used to generate enum variant construction)
            ::HIR::SimplePath stateIdxEnmPath;

            /// Is this coroutine a future? (as opposed to a generator)
            bool is_future = false;
        } generatorState;

    public:
        ExprVisitorConv(MirBuilder& builder, const ::std::vector<::HIR::TypeRef>& varTypes, const ::HIR::ExprNodeGeneratorWrapper* is_generator)
            : builder(builder)
            , variableTypes(varTypes)
            , isGenerator(is_generator != nullptr)
        {
            if (isGenerator) {
                generatorState.is_future = is_generator->isFuture;
                generatorState.stateIdxEnmPath = is_generator->stateIdxEnum;
                generatorState.bbOpen = builder.pauseCurBlock();
                generatorState.states.push_back(GeneratorState::State(builder.newBbUnlinked()));
                builder.setCurBlock(generatorState.states.back().entrypoint);
            }
        }

        SaveAndEditVal<const ScopeHandle*> disableBorrowExtension() override {
            return saveAndEdit(borrowRaiseTarget, nullptr);
        }

        // Get a LValue pointing at the state index
        ::MIR::LValue generatorStateLv() const {
            // (*self.ptr(?0)).state(0).value(?#1).idx(0)
            auto rv = ::MIR::LValue::newArgument(0);
            rv = ::MIR::LValue::newField(mv$(rv), 0);    // .ptr (From Pin)
            rv = ::MIR::LValue::newDeref(mv$(rv));       // .*
            rv = ::MIR::LValue::newField(mv$(rv), 0);    // .state
            rv = ::MIR::LValue::newDowncast(mv$(rv), 1); // .value (From MaybeUninit)
            rv = ::MIR::LValue::newField(mv$(rv), 0);    // .value (From ManuallyDrop)
            rv = ::MIR::LValue::newField(mv$(rv), 0);    // .idx
            return rv;
        }

        const std::set<unsigned>& generatorDropFlags() const {
            return generatorState.savedDropFlags;
        }

        std::set<unsigned> generatorFinalise(const Span& sp, ::HIR::Enum& stateEnm) {
            std::set<unsigned> usedVars;
            std::vector<MIR::BasicBlockId> armTargets;
            armTargets.reserve(generatorState.states.size() + 1);
            ::std::vector<HIR::Enum::ValueVariant> enumVariants;
            enumVariants.reserve(generatorState.states.size() + 1);
            for (const auto& s : generatorState.states) {
                armTargets.push_back(builder.newBbUnlinked());

                builder.setCurBlock(armTargets.back());
                builder.pushStmtAssign(sp, generatorStateLv(), ::MIR::RValue::make_EnumVariant({generatorState.stateIdxEnmPath, static_cast<unsigned>(generatorState.states.size()), {}}));
                builder.endBlock(::MIR::Terminator::make_Goto(s.entrypoint));

                enumVariants.push_back(HIR::Enum::ValueVariant{RcString(), ::HIR::ExprPtr(), U128(armTargets.size() - 1)});
                for (const auto& e : s.saved) {
                    usedVars.insert(e.first);
                }
            }
            // Final arm is the end/panic state - it's a bug to reach this
            armTargets.push_back(builder.newBbUnlinked());
            builder.setCurBlock(armTargets.back());
            builder.endBlock(::MIR::Terminator::make_Unreachable({}));

            enumVariants.push_back(HIR::Enum::ValueVariant{RcString::newInterned("END"), ::HIR::ExprPtr(), U128(armTargets.size() - 1)});
            stateEnm.mData = ::HIR::Enum::Class::make_Value({mv$(enumVariants)});

            builder.setCurBlock(generatorState.bbOpen);

            // switch _n { ... }
            builder.endBlock(::MIR::Terminator::make_Switch({generatorStateLv(), mv$(armTargets)}));

            return usedVars;
        }

        void generatorMakeDrop(const Span& sp, MirBuilder& outBuilder, size_t nCaptures, const ::std::map<unsigned, std::vector<MIR::LValue::Wrapper>>& mappings, unsigned dropStateFieldIdx, const std::map<unsigned, unsigned>& drop_flag_mapping) const {
            ::MIR::LValue self = ::MIR::LValue::newDeref(::MIR::LValue::newArgument(0));

            assert(generatorState.states.size() > 0);
            std::vector<::MIR::BasicBlockId> arms;
            arms.reserve(generatorState.states.size() + 1);

            // Set all drop flags from input
            if (!drop_flag_mapping.empty()) {
                auto slot = ::MIR::LValue::newArgument(0);
                slot.wrappers.push_back(::MIR::LValue::Wrapper::newDeref());                     // Deref `&mut Self`
                slot.wrappers.push_back(::MIR::LValue::Wrapper::newField(0));                    // Get state field
                slot.wrappers.push_back(::MIR::LValue::Wrapper::newDowncast(1));                 // .value (From MaybeUninit)
                slot.wrappers.push_back(::MIR::LValue::Wrapper::newField(0));                    // .value (From ManuallyDrop)
                slot.wrappers.push_back(::MIR::LValue::Wrapper::newField(dropStateFieldIdx)); // drop flag bitset
                for (const auto& flagMapping : drop_flag_mapping) {
                    auto i = outBuilder.newDropFlag(false);
                    assert(i == flagMapping.second); // Should hold, as the map was created in-order
                    outBuilder.pushStmt(
                        sp,
                        ::MIR::Statement::make_LoadDropFlag({
                            flagMapping.first,
                            slot.clone(),
                            flagMapping.second,
                        })
                    );
                }
            }

            auto entryBlock = outBuilder.pauseCurBlock();
            // if state is 0, then drop captures (this is the pre-run state)
            arms.push_back(outBuilder.newBbUnlinked());
            outBuilder.setCurBlock(arms.back());
            size_t argCount = 2;
            for (size_t i = 0; i < nCaptures; i++) {
                // TODO: State tracking on captures, what if a by-value capture is moved?
                if (mappings.count(argCount + i) == 0) {
                    outBuilder.pushStmtDrop(sp, ::MIR::LValue::newField(self.clone(), 1 + i));
                }
            }
            outBuilder.endBlock(::MIR::Terminator::make_Return({}));

            auto getLv = [&sp, &self, &mappings](unsigned idx) -> ::MIR::LValue {
                ::MIR::LValue rv = self.clone();
                ASSERT_BUG(sp, mappings.count(idx), "No LValue for index " << idx);
                rv.wrappers.insert(rv.wrappers.end(), mappings.at(idx).begin(), mappings.at(idx).end());
                DEBUG("get_lv: " << rv);
                return rv;
            };

            // Else, drop yield saves (Note: final state has no saves, so acts as the "completed" state)
            for (size_t i = 0; i < generatorState.states.size(); i++) {
                //
                arms.push_back(outBuilder.newBbUnlinked());
                outBuilder.setCurBlock(arms.back());
                for (const auto& v : generatorState.states[i].saved) {
                    if (v.first == 0) {
                        continue;
                    }
                    // Note: Conditional drop handled by drop flags above
                    // HACK: The caller re-maps drop flags
                    outBuilder.dropActveLocal(sp, getLv(v.first), v.second);
                }
                outBuilder.endBlock(::MIR::Terminator::make_Return({}));
            }
            // Generate the dispatch switch
            outBuilder.setCurBlock(entryBlock);
            outBuilder.pushStmtAssign(sp, ::MIR::LValue::newReturn(), ::MIR::RValue::make_Tuple({}));
            auto stmtIdxLv = mv$(self);
            stmtIdxLv = ::MIR::LValue::newField(mv$(stmtIdxLv), 0);    // .state
            stmtIdxLv = ::MIR::LValue::newDowncast(mv$(stmtIdxLv), 1); // .value (From MaybeUninit)
            stmtIdxLv = ::MIR::LValue::newField(mv$(stmtIdxLv), 0);    // .value (From ManuallyDrop)
            stmtIdxLv = ::MIR::LValue::newField(mv$(stmtIdxLv), 0);    // .idx
            outBuilder.endBlock(::MIR::Terminator::make_Switch({mv$(stmtIdxLv), mv$(arms)}));
        }

        void visitPatternSlots(const ::HIR::Pattern& pat, PatternDropOrder order, const std::function<void(unsigned)>& visitSlot) {
            for (const auto slot : ::HIR::patternBindingSlots(pat, order)) {
                visitSlot(slot);
            }
        }

        void schedulePatternDrops(const Span& sp, const ::HIR::Pattern& pat, PatternDropOrder order) override {
            (void)sp;
            visitPatternSlots(pat, order, [&](unsigned slot) { builder.scheduleVariableDrop(slot); });
        }

        void registerPatternVariables(const Span& sp, const ::HIR::Pattern& pat, PatternDropOrder order) override {
            (void)sp;
            visitPatternSlots(pat, order, [&](unsigned slot) { builder.registerVariableState(slot); });
        }

        void scheduleRegisteredPatternDrops(const Span& sp, const ::HIR::Pattern& pat, PatternDropOrder order) override {
            (void)sp;
            visitPatternSlots(pat, order, [&](unsigned slot) { builder.scheduleRegisteredVariableDrop(slot); });
        }

        MIR::LValue getValueForBindingPath(const Span& sp, const ::HIR::TypeData* outer_ty, const ::MIR::LValue& outerLval, const PatternBinding& b) {
            HIR::TypeRef ty;
            MIR::LValue lval;
            MIRLowerHIRGetTypeValueForPath(sp, builder, outer_ty, outerLval, b.field, ty, lval);

            if (b.isSplitSlice()) {
                struct H {
                    static ::HIR::BorrowType getBorrowType(const Span& sp, const ::HIR::PatternBinding& pb) {
                        switch (pb.mType) {
                            case ::HIR::PatternBinding::Type::Move:
                                BUG(sp, "By-value pattern binding of a slice");
                            case ::HIR::PatternBinding::Type::Ref:
                                return ::HIR::BorrowType::Shared;
                            case ::HIR::PatternBinding::Type::MutRef:
                                return ::HIR::BorrowType::Unique;
                        }
                        throw "";
                    }
                };

                unsigned subValI = static_cast<unsigned>(b.splitSlice.first + b.splitSlice.second);
                auto& types = builder.resolve().crate.types;
                if (const auto* tep = ty->opt_Array()) {
                    auto innerType = tep->inner;
                    auto len = tep->size.as_Known() - subValI;
                    auto ret_ty = types.array(innerType, len);

                    if (b.binding->mType == ::HIR::PatternBinding::Type::Move) {
                        // Create a new array value
                        std::vector<MIR::Param> arrayVals;
                        for (size_t i = b.splitSlice.first; i < tep->size.as_Known() - b.splitSlice.second; i++) {
                            arrayVals.push_back(::MIR::LValue::newField(lval.clone(), static_cast<unsigned>(i)));
                        }
                        lval = builder.lvalueOrTemp(sp, mv$(ret_ty), ::MIR::RValue::make_Array({std::move(arrayVals)}));
                    } else {
                        // Create a pointer to this array, by casting a raw pointer to its first element
                        ::HIR::BorrowType bt = H::getBorrowType(sp, *b.binding);
                        ::MIR::LValue ptrVal = builder.lvalueOrTemp(sp, types.pointer(bt, innerType), ::MIR::RValue::make_Borrow({bt, true, ::MIR::LValue::newField(lval.clone(), static_cast<unsigned int>(b.splitSlice.first))}));

                        // 3. Create a slice pointer
                        auto ptrTy = types.pointer(bt, ret_ty);
                        lval = builder.lvalueOrTemp(sp, ptrTy, ::MIR::RValue::make_Cast({mv$(ptrVal), ptrTy}));
                        // 4. And dereference it
                        lval = ::MIR::LValue::newDeref(std::move(lval));
                    }
                } else if (const auto* tep = ty->opt_Slice()) {
                    auto innerType = tep->inner;

                    // 1. Obtain remaining length
                    auto usizeTy = types.primitive(::HIR::CoreType::Usize);
                    auto srcLenLval = builder.lvalueOrTemp(sp, usizeTy, ::MIR::RValue::make_DstMeta({builder.getPtrToDst(sp, lval)}));
                    auto subVal = ::MIR::Param(::MIR::Constant::make_Uint({U128(subValI), ::HIR::CoreType::Usize}));
                    ::MIR::LValue lenVal = builder.lvalueOrTemp(sp, usizeTy, ::MIR::RValue::make_BinOp({mv$(srcLenLval), ::MIR::eBinOp::SUB, mv$(subVal)}));

                    // 2. Obtain pointer to the first element
                    // TODO: This currently emits a borrow to that element, but we need a raw pointer (to avoid being technically out-of-bounds)
                    // - Should add a MIR op for `BorrowRaw`
                    ::HIR::BorrowType bt = H::getBorrowType(sp, *b.binding);
                    ::MIR::LValue ptrVal = builder.lvalueOrTemp(sp, types.pointer(bt, innerType), ::MIR::RValue::make_Borrow({bt, true, ::MIR::LValue::newField(lval.clone(), static_cast<unsigned int>(b.splitSlice.first))}));

                    // 3. Create a slice pointer
                    lval = builder.lvalueOrTemp(sp, types.borrow(bt, ty), ::MIR::RValue::make_MakeDst({mv$(ptrVal), mv$(lenVal)}));
                    // 4. And dereference it
                    lval = ::MIR::LValue::newDeref(std::move(lval));
                } else {
                    TODO(sp, "SplitSlice binding: " << b.splitSlice << " - " << ty);
                }
            }

            return lval;
        }

        void destructureFromList(const Span& sp, const ::HIR::TypeData* outer_ty, ::MIR::LValue outerLval, const ::std::vector<PatternBinding>& bindings, bool updateStates /*=true*/) override {
            TRACE_FUNCTION_F(outerLval << ": " << outer_ty << " [" << bindings << "]");
            // Reverse order to avoid potential use-after-move for `foo @ Bar(baz, ..)`
            for (size_t i = bindings.size(); i--;) {
                const auto& b = bindings[i];
                auto lval = getValueForBindingPath(sp, outer_ty, outerLval, b);

                MIR::RValue rv;
                switch (b.binding->mType) {
                    case ::HIR::PatternBinding::Type::Move:
                        rv = mv$(lval);
                        break;
                    case ::HIR::PatternBinding::Type::Ref:
                        if (borrowRaiseTarget) {
                            DEBUG("- Raising destructure borrow of " << lval << " to scope " << *borrowRaiseTarget);
                            builder.raiseTemporaries(sp, lval, *borrowRaiseTarget);
                        }

                        rv = ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, mv$(lval)});
                        break;
                    case ::HIR::PatternBinding::Type::MutRef:
                        if (borrowRaiseTarget) {
                            DEBUG("- Raising destructure borrow of " << lval << " to scope " << *borrowRaiseTarget);
                            builder.raiseTemporaries(sp, lval, *borrowRaiseTarget);
                        }
                        rv = ::MIR::RValue::make_Borrow({::HIR::BorrowType::Unique, false, mv$(lval)});
                        break;
                }
                // NOTE: Don't drop the destination, as `match` does some tricky things with calling destructure multiple times (to handle or-patterns)
                builder.pushStmtAssign(sp, builder.getVariable(sp, b.binding->slot), mv$(rv), updateStates);
            }
        }

        const HIR::TypeData* getBindingType(const Span& sp, unsigned index) const {
            return variableTypes.at(index);
        }

        void emitUnwind(const Span& sp) {
            builder.emitUnwindCleanup(sp);
            builder.endBlock(::MIR::Terminator::make_UnwindResume({}));
        }

        // -- ExprVisitor
        void visitNodePtr(::HIR::ExprNodeP& nodeP) override {
            DEBUG(nodeP.get());
            ::HIR::ExprVisitor::visitNodePtr(nodeP);
        }

        void visit(::HIR::ExprNodeBlock& node) override {
            TRACE_FUNCTION_F("_Block");
            // NOTE: This doesn't create a BB, as BBs are not needed for scoping
            bool diverged = false;

            auto resVal = (node.valueNode ? builder.newTemporary(node.resType) : ::MIR::LValue());
            // Tail-expression temporaries outlive the block's locals. This is
            // a distinct scope from the one used for extended let initializers.
            auto tailTmpScope = builder.newScopeTemp(node.span());
            auto scope = builder.newScopeVar(node.span());
            auto _block_var_scope = saveAndEdit(blockVarScope, &scope);
            auto tmpScope = builder.newScopeTemp(node.span());
            auto _block_tmp_scope = saveAndEdit(blockTmpScope, &tmpScope);

            for (unsigned int i = 0; i < node.nodes.size(); i++) {
                auto _ = this->disableBorrowExtension();
                auto& subnode = node.nodes[i];
                const Span& sp = subnode->span();

                auto stmt_scope = builder.newScopeTemp(sp);
                const auto* letNode = cast<::HIR::ExprNodeLet>(subnode.get());
                auto _super_let_scope = saveAndEdit(superLetScope, letNode && letNode->isSuper ? superLetScope : &stmt_scope);
                // NOTE: Only set the statement scope if processing a block
                auto _stmt_scope_push = saveAndEdit(stmtScope, cast<::HIR::ExprNodeBlock>(subnode.get()) ? &stmt_scope : nullptr);
                this->visitNodePtr(subnode);

                if (builder.block_active() || builder.hasResult()) {
                    auto result = builder.getResult(sp);
                    if (!builder.resolve().typeIsCopy(sp, subnode->resType)) {
                        auto discarded = builder.newTemporary(subnode->resType);
                        builder.pushStmtAssign(sp, std::move(discarded), std::move(result));
                    }
                    builder.terminateScope(sp, mv$(stmt_scope));
                    diverged |= subnode->resType->is_Diverge();
                } else {
                    builder.terminateScope(sp, mv$(stmt_scope), false);

                    builder.setCurBlock(builder.newBbUnlinked());
                    diverged = true;
                }
            }

            // For the last node, specially handle.
            // TODO: Any temporaries defined within this node must be elevated into the parent scope
            if (node.valueNode) {
                auto& subnode = node.valueNode;
                const Span& sp = subnode->span();

                auto stmt_scope = builder.newScopeTemp(sp);
                this->visitNodePtr(subnode);
                if (builder.hasResult() || builder.block_active()) {
                    ASSERT_BUG(sp, builder.block_active(), "Result yielded, but no active block");
                    ASSERT_BUG(sp, builder.hasResult(), "Active block but no result yeilded");
                    // PROBLEM: This can drop the result before we want to use it.

                    builder.pushStmtAssign(sp, resVal.clone(), builder.getResult(sp));

                    // If this block is part of a statement, raise all temporaries from this final scope to the enclosing scope
                    if (stmtScope) {
                        builder.raiseAll(sp, mv$(stmt_scope), *stmtScope);
                    } else {
                        builder.raiseAll(sp, mv$(stmt_scope), tailTmpScope);
                    }
                    builder.setResult(node.span(), mv$(resVal));
                } else {
                    builder.terminateScope(sp, mv$(stmt_scope), false);
                    // Block diverged in final node.
                }
                builder.terminateScope(node.span(), mv$(tmpScope), builder.block_active());
                builder.terminateScope(node.span(), mv$(scope), builder.block_active());
                builder.terminateScope(node.span(), mv$(tailTmpScope), builder.block_active());
            } else {
                if (diverged) {
                    builder.terminateScope(node.span(), mv$(tmpScope), false);
                    builder.terminateScope(node.span(), mv$(scope), false);
                    builder.terminateScope(node.span(), mv$(tailTmpScope), false);
                    builder.endBlock(::MIR::Terminator::make_Unreachable({}));
                    // Don't set a result if there's no block.
                } else {
                    builder.terminateScope(node.span(), mv$(tmpScope));
                    builder.terminateScope(node.span(), mv$(scope));
                    builder.terminateScope(node.span(), mv$(tailTmpScope));
                    builder.setResult(node.span(), ::MIR::RValue::make_Tuple({}));
                }
            }
        }

        void visit(::HIR::ExprNodeConstBlock& node) override {
            if (cast<HIR::ExprNodePathValue>(node.inner.get())) {
                this->visitNodePtr(node.inner);
            } else {
                BUG(node.span(), "Const block shouldn't have reached MIR generation");
            }
        }

        void visit(::HIR::ExprNodeAsm& node) override {
            TRACE_FUNCTION_F("_Asm");

            ::std::vector<::std::pair<::std::string, ::MIR::LValue>> inputs;
            // Inputs just need to be in lvalues
            for (auto& v : node.inputs) {
                this->visitNodePtr(v.value);
                auto lv = builder.getResultInLvalue(v.value->span(), v.value->resType);
                inputs.push_back(::std::make_pair(v.spec, mv$(lv)));
            }

            ::std::vector<::std::pair<::std::string, ::MIR::LValue>> outputs;
            // Outputs can also (sometimes) be rvalues (only for `*m`?)
            for (auto& v : node.outputs) {
                this->visitNodePtr(v.value);
                if (v.spec[0] != '=' && v.spec[0] != '+') { // TODO: what does '+' mean?
                    ERROR(node.span(), E0000, "Assembly output specifiers must start with =");
                }
                ::MIR::LValue lv;
                if (v.spec[1] == '*') {
                    lv = builder.getResultInLvalue(v.value->span(), v.value->resType);
                } else {
                    lv = builder.getResultUnwrapLvalue(v.value->span());
                }
                outputs.push_back(::std::make_pair(v.spec, mv$(lv)));
            }

            builder.pushStmtAsm(node.span(), {node.templateText, mv$(outputs), mv$(inputs), node.clobbers, node.flags});
            builder.setResult(node.span(), ::MIR::RValue::make_Tuple({}));
        }

        void visit(::HIR::ExprNodeAsm2& node) override {
            TRACE_FUNCTION_F("_Asm2");

            // TODO: How to represent inout in the MIR?
            // - Potentially a register specifier that links to one of the inputs
            // - OR: Just keep the parameter list as before - but now simplified to just one `Reg`
            ::MIR::Statement::Data_Asm2 ent;
            ent.options = node.options;
            ent.lines = node.lines;

            auto movedParam = [&](const ::MIR::Param& p) {
                if (const auto* e = p.opt_LValue()) {
                    builder.movedLvalue(node.span(), *e);
                }
            };

            for (auto& v : node.mParams) {
                TU_MATCH_HDRA( (v), { )
                TU_ARMA(Const, e) {
                        // This constant needs to have been evaluated fully (so a `MIR::Constant` can be created)
                        this->visitNodePtr(e);
                        auto param = builder.getResultInParam(e->span(), e->resType);
                        if (param.is_Constant()) {
                            ent.params.push_back(MIR::AsmParam::make_Const(std::move(param.as_Constant())));
                        } else {
                            TODO(node.span(), "asm! const");
                        }
                    }
                    TU_ARMA(Sym, e) {
                        ent.params.push_back(MIR::AsmParam::make_Sym(e.clone()));
                    }
                    TU_ARMA(RegSingle, e) {
                        std::unique_ptr<MIR::Param> input;
                        std::unique_ptr<MIR::LValue> output;
                        this->visitNodePtr(e.val);
                        switch (e.dir) {
                            case AsmCommon::Direction::In:
                                ASSERT_BUG(node.span(), e.val, "`in` register with no value");
                                input = box$(builder.getResultInParam(e.val->span(), e.val->resType));
                                break;
                            case AsmCommon::Direction::Out:
                            case AsmCommon::Direction::LateOut:
                                if (e.val) {
                                    output = box$(builder.getResultUnwrapLvalue(e.val->span()));
                                }
                                break;
                            case AsmCommon::Direction::InOut:
                            case AsmCommon::Direction::InLateOut:
                                ASSERT_BUG(node.span(), e.val, "`inout` register with no value");
                                output = box$(builder.getResultUnwrapLvalue(e.val->span()));
                                input = std::make_unique<MIR::Param>(output->clone());
                                break;
                        }
                        if (input) {
                            movedParam(*input);
                        }
                        ent.params.push_back(MIR::AsmParam::make_Reg({e.dir, std::move(e.spec), std::move(input), std::move(output)}));
                    }
                    TU_ARMA(Reg, e) {
                        std::unique_ptr<MIR::Param> input;
                        std::unique_ptr<MIR::LValue> output;
                        switch (e.dir) {
                            case AsmCommon::Direction::In:
                                ASSERT_BUG(node.span(), e.valIn, "`in` register with no input");
                                this->visitNodePtr(e.valIn);
                                input = box$(builder.getResultInParam(e.valIn->span(), e.valIn->resType));
                                assert(!e.valOut);
                                break;
                            case AsmCommon::Direction::Out:
                            case AsmCommon::Direction::LateOut:
                                ASSERT_BUG(node.span(), !e.valIn, "`[late]out` register with input value");
                                if (e.valOut) {
                                    this->visitNodePtr(e.valOut);
                                    output = box$(builder.getResultUnwrapLvalue(e.valOut->span()));
                                }
                                break;
                            case AsmCommon::Direction::InOut:
                            case AsmCommon::Direction::InLateOut:
                                ASSERT_BUG(node.span(), e.valIn, "`in[late]out` register with no input");
                                this->visitNodePtr(e.valIn);
                                input = box$(builder.getResultInParam(e.valIn->span(), e.valIn->resType));
                                if (e.valOut) {
                                    this->visitNodePtr(e.valOut);
                                    output = box$(builder.getResultUnwrapLvalue(e.valOut->span()));
                                }
                                break;
                        }
                        if (input) {
                            movedParam(*input);
                        }
                        ent.params.push_back(MIR::AsmParam::make_Reg({e.dir, std::move(e.spec), std::move(input), std::move(output)}));
                    }
                }
            }
            builder.pushStmt(node.span(), mv$(ent));
            if (!node.options.noreturn) {
                builder.setResult(node.span(), ::MIR::RValue::make_Tuple({}));
            } else {
                builder.endBlock(::MIR::Terminator::make_Unreachable({}));
            }
        }

        // Common code used by both `ExprNodeReturn` and the final return of a GeneratorWrapper
        void coroutineReturn(const Span& sp, const ::HIR::TypeData* valueTy) {
            static RcString rcstringComplete = RcString::newInterned("Complete");
            static RcString rcstringReady = RcString::newInterned("Ready"); // TODO: This is a lang item
            const auto& variantName = generatorState.is_future ? rcstringReady : rcstringComplete;
            // TODO: Handle difference between generators and futures (different return/yield types)
            ::HIR::GenericPath enmPath;
            size_t variantIndex = SIZE_MAX;
            builder.withValType(sp, ::MIR::LValue::newReturn(), [&](const ::HIR::TypeData* ty) {
                const auto& te = ty->as_Path();
                enmPath = te.path.mData.as_Generic().clone();
                variantIndex = te.binding.as_Enum()->findVariant(variantName);
            });
            ASSERT_BUG(sp, enmPath.mPath != HIR::SimplePath(), "Failed to get path from return type?");
            ASSERT_BUG(sp, variantIndex != SIZE_MAX, "Unable to find variant " << variantName << " in " << enmPath << " for coroutine return");

            ::std::vector<::MIR::Param> values;
            values.push_back(builder.getResultInParam(sp, valueTy));
            auto res = ::MIR::RValue::make_EnumVariant({std::move(enmPath), static_cast<unsigned>(variantIndex), std::move(values)});
            builder.pushStmtAssign(sp, ::MIR::LValue::newReturn(), std::move(res));
        }

        void visit(::HIR::ExprNodeReturn& node) override {
            TRACE_FUNCTION_F("_Return");
            this->visitNodePtr(node.mValue);

            if (!builder.block_active()) {
                return;
            }

            if (isGenerator) {
                coroutineReturn(node.span(), node.mValue->resType);
            } else {
                builder.pushStmtAssign(node.span(), ::MIR::LValue::newReturn(), builder.getResult(node.span()));
            }
            builder.terminateScopeEarly(node.span(), builder.fcn_scope());
            builder.endBlock(::MIR::Terminator::make_Return({}));
        }

        void visit(::HIR::ExprNodeYield& node) override {
            TRACE_FUNCTION_F("_Yield");
            if (isGenerator) {
                ASSERT_BUG(node.span(), !generatorState.is_future, "");

                ::HIR::GenericPath enmPath;
                builder.withValType(node.span(), ::MIR::LValue::newReturn(), [&](const ::HIR::TypeData* ty) {
                    const auto& te = ty->as_Path();
                    enmPath = te.path.mData.as_Generic().clone();
                    ASSERT_BUG(node.span(), te.binding.as_Enum()->findVariant("Yielded") == 0, "");
                });

                this->visitNodePtr(node.mValue);
                // Emit return, wrapped in GeneratorState::Yielded
                ::std::vector<::MIR::Param> values;
                values.push_back(builder.getResultInParam(node.span(), node.mValue->resType));
                auto res = ::MIR::RValue::make_EnumVariant(
                    {mv$(enmPath),
                     0, // Yielded is the first variant
                     mv$(values)}
                );
                builder.pushStmtAssign(node.span(), ::MIR::LValue::newReturn(), mv$(res));
                builder.pushStmtAssign(node.span(), generatorStateLv(), ::MIR::RValue::make_EnumVariant({generatorState.stateIdxEnmPath.clone(), static_cast<unsigned>(generatorState.states.size()), {}}));
                // NOTE: No scope terminate
                builder.endBlock(::MIR::Terminator::make_Return({}));

                generatorState.states.back().saved = builder.getActiveLocals(node.span(), generatorState.savedDropFlags);
                generatorState.states.push_back(builder.newBbUnlinked());
                builder.setCurBlock(generatorState.states.back().entrypoint);

                builder.setResult(node.span(), ::MIR::RValue::make_Tuple({}));
            } else {
                BUG(node.span(), "Unexpected ExprNode_Yield (should have been re-written)");
            }
        }

        void visit(::HIR::ExprNodeAWait& node) override {
            const Span& sp = node.span();
            TRACE_FUNCTION_F("_AWait");
            ASSERT_BUG(node.span(), isGenerator && generatorState.is_future, "`.await` not in an async block/function");
            const auto& tyInner = node.mValue->resType;

            this->visitNodePtr(node.mValue);
            auto lvRes = builder.getResultInLvalue(sp, tyInner);

            auto stateValue = static_cast<unsigned>(generatorState.states.size());
            generatorState.states.back().saved = builder.getActiveLocals(node.span(), generatorState.savedDropFlags);
            generatorState.states.push_back(builder.newBbUnlinked());
            builder.endBlock(generatorState.states.back().entrypoint);
            builder.setCurBlock(generatorState.states.back().entrypoint);

            // Create `Pin<&mut >` as the reciever, using `Pin::new_unchecked`
            const auto& langPin = builder.resolve().crate.getLangItemPath(sp, "pin");
            auto& types = builder.resolve().crate.types;
            auto typeMut = types.borrow(::HIR::BorrowType::Unique, tyInner);
            auto pinPath = ::HIR::GenericPath(langPin, ::HIR::PathParams(typeMut));
            auto typePin = types.path(std::move(pinPath), &builder.resolve().crate.getStructByPath(sp, langPin));

            auto lvMut = builder.lvalueOrTemp(sp, typeMut, ::MIR::RValue::make_Borrow({::HIR::BorrowType::Unique, false, std::move(lvRes)}));
            auto lvPin = builder.newTemporary(typePin);
            {
                auto bbRet = builder.newBbUnlinked();
                auto bbPanic = builder.newBbUnlinked();
                builder.endBlock(::MIR::Terminator::make_Call({bbRet, ::MIR::UnwindAction::make_Cleanup(bbPanic), lvPin.clone(), ::HIR::Path(typePin, "new_unchecked"), makeVec1(::MIR::Param(lvMut.clone()))}));
                builder.movedLvalue(node.span(), std::move(lvMut));
                builder.setCurBlock(bbPanic);
                emitUnwind(sp);
                builder.setCurBlock(bbRet);
            }
            // Call `Future::poll`
            const auto& langPoll = builder.resolve().crate.getLangItemPath(sp, "Poll");
            auto typePoll = types.path(::HIR::GenericPath(langPoll, ::HIR::PathParams(node.resType)), &builder.resolve().crate.getEnumByPath(sp, langPoll));
            auto lvPoll = builder.newTemporary(typePoll);
            {
                auto bbRet = builder.newBbUnlinked();
                auto bbPanic = builder.newBbUnlinked();
                builder.endBlock(
                    ::MIR::Terminator::make_Call(
                        {bbRet,
                         ::MIR::UnwindAction::make_Cleanup(bbPanic),
                         lvPoll.clone(),
                         ::HIR::Path(tyInner, builder.resolve().mLangFuture, "poll"),
                         makeVec2(
                             ::MIR::Param(lvPin.clone()),
                             ::MIR::Param::make_Borrow({
                                 ::HIR::BorrowType::Unique,
                                 ::MIR::LValue::newDeref(::MIR::LValue::newArgument(1)) // Context is the second argument (first is `self`)
                             })
                         )}
                    )
                );
                builder.movedLvalue(node.span(), std::move(lvPin));
                builder.setCurBlock(bbPanic);
                emitUnwind(sp);
                builder.setCurBlock(bbRet);
            }
            // Check return
            const auto variantReady = 0;
            {
                auto bbPending = builder.newBbUnlinked();
                auto bbReady = builder.newBbUnlinked();
                ASSERT_BUG(node.span(), typePoll->as_Path().binding.as_Enum()->findVariant("Ready") == variantReady, "");
                ASSERT_BUG(node.span(), typePoll->as_Path().binding.as_Enum()->findVariant("Pending") == 1, "");
                builder.endBlock(::MIR::Terminator::make_Switch({lvPoll.clone(), makeVec2(bbReady, bbPending)}));
                builder.setCurBlock(bbPending);

                // `retval = ::core::task::Poll::Pending; RETURN`
                HIR::GenericPath pathLocalPoll;
                builder.withValType(sp, ::MIR::LValue::newReturn(), [&](const ::HIR::TypeData* ty) {
                    pathLocalPoll = ty->as_Path().path.mData.as_Generic().clone();
                });
                builder.pushStmtAssign(node.span(), ::MIR::LValue::newReturn(), ::MIR::RValue::make_EnumVariant({std::move(pathLocalPoll), 1, {}}));
                builder.pushStmtAssign(node.span(), generatorStateLv(), ::MIR::RValue::make_EnumVariant({generatorState.stateIdxEnmPath.clone(), stateValue, {}}));
                builder.endBlock(::MIR::Terminator::make_Return({}));

                builder.setCurBlock(bbReady);
            }
            // lv_poll.#0.0 to get the field of the first variant
            builder.setResult(node.span(), ::MIR::LValue::newField(::MIR::LValue::newDowncast(std::move(lvPoll), variantReady), 0));
        }

        void visit(::HIR::ExprNodeLet& node) override {
            TRACE_FUNCTION_F("_Let " << node.pattern);
            if (node.mValue) {
                auto _ = saveAndEdit(borrowRaiseTarget, blockTmpScope);
                auto _super_let_scope = saveAndEdit(superLetScope, node.isSuper ? superLetScope : blockVarScope);
                this->visitNodePtr(node.mValue);

                if (!builder.block_active()) {
                    return;
                }
                auto res = builder.getResult(node.span());

                // Shortcut for `let foo = bar;` (avoids the extra temporary that would need to be optimised out)
                if (node.pattern.mData.is_Any() && !node.pattern.mBindings.empty() && std::all_of(node.pattern.mBindings.begin(), node.pattern.mBindings.end(), [](const HIR::PatternBinding& pb) {
                    return pb.mType == ::HIR::PatternBinding::Type::Move;
                })) {
                    this->schedulePatternDrops(node.span(), node.pattern, PatternDropOrder::FirstCandidate);
                    for (const auto& pb : node.pattern.mBindings) {
                        builder.pushStmtAssign(node.span(), builder.getVariable(node.span(), pb.slot), mv$(res));
                    }
                } else {
                    auto patternValue = builder.lvalueOrTemp(node.mValue->span(), node.mType, mv$(res));
                    auto dropValue = patternValue.clone();
                    this->registerPatternVariables(node.span(), node.pattern, PatternDropOrder::FirstCandidate);
                    MIRLowerHIRLet(builder, *this, node.span(), node.pattern, mv$(patternValue), nullptr);
                    if (blockTmpScope) {
                        builder.moveTemporaryDropToVariableScope(node.span(), dropValue, *blockTmpScope);
                    }
                    this->scheduleRegisteredPatternDrops(node.span(), node.pattern, PatternDropOrder::FirstCandidate);
                }
            } else {
                this->schedulePatternDrops(node.span(), node.pattern, PatternDropOrder::Declaration);
            }
            if (node.isSuper) {
                ASSERT_BUG(node.span(), superLetScope, "`super let` without an enclosing expression scope");
                for (const auto slot : ::HIR::patternBindingSlots(node.pattern, PatternDropOrder::FirstCandidate)) {
                    builder.moveVariableToScope(node.span(), slot, *superLetScope);
                }
            }
            builder.setResult(node.span(), ::MIR::RValue::make_Tuple({}));
        }

        void visit(::HIR::ExprNodeLoop& node) override {
            TRACE_FUNCTION_FR("_Loop", "_Loop");
            auto loopBlock = builder.newBbLinked();
            auto loopBodyScope = builder.newScopeLoop(node.span());
            auto loopNext = builder.newBbUnlinked();

            auto loopResultLvaue = builder.newTemporary(node.resType);

            auto loopTmpScope = builder.newScopeTemp(node.span());
            auto _ = saveAndEdit(stmtScope, &loopTmpScope);

            loopStack.push_back(LoopDesc{mv$(loopBodyScope), node.label, node.requireLabel, loopBlock, loopNext, loopResultLvaue.clone()});
            this->visitNodePtr(node.mCode);
            auto loopScope = mv$(loopStack.back().scope);
            loopStack.pop_back();

            // If there's a stray result, drop it
            if (builder.hasResult()) {
                assert(builder.block_active());
                // TODO: Properly drop this? Or just discard it? It should be ()
                builder.getResult(node.span());
            }
            // Terminate block with a jump back to the start
            // - Also inserts the jump if this didn't uncondtionally diverge
            if (builder.block_active()) {
                DEBUG("- Reached end, loop back");
                // Insert drop of all scopes within the current scope
                builder.terminateScope(node.span(), mv$(loopTmpScope));
                builder.terminateScope(node.span(), mv$(loopScope));
                builder.endBlock(::MIR::Terminator::make_Goto(loopBlock));
            } else {
                // Terminate scope without emitting cleanup (cleanup was handled by `break`)
                builder.terminateScope(node.span(), mv$(loopTmpScope), false);
                builder.terminateScope(node.span(), mv$(loopScope), false);
            }

            if (!node.diverges) {
                DEBUG("- Doesn't diverge");
                builder.setCurBlock(loopNext);
                builder.setResult(node.span(), mv$(loopResultLvaue));
            } else {
                DEBUG("- Diverges");
                assert(!builder.hasResult());

                builder.setCurBlock(loopNext);
                builder.endSplitArmEarly(node.span());
                assert(!builder.hasResult());
                builder.endBlock(::MIR::Terminator::make_Unreachable({}));
            }

            // TODO: Store the variable state on a break for restoration at the end of the loop.
        }

        /// Locate a loop given a name
        const LoopDesc& findLoop(const Span& sp, const RcString& targetLabel) const {
            if (targetLabel != "") {
                auto it = ::std::find_if(loopStack.rbegin(), loopStack.rend(), [&](const auto& x) {
                    return x.label == targetLabel;
                });
                if (it == loopStack.rend()) {
                    BUG(sp, "Named loop '" << targetLabel << " doesn't exist");
                }
                return *it;
            } else {
                auto it = ::std::find_if(loopStack.rbegin(), loopStack.rend(), [](const auto& x) {
                    return !x.require_label;
                });
                if (it == loopStack.rend()) {
                    BUG(sp, "Break outside of a breakable block");
                }
                if (it->label != "" && it->label.c_str()[0] == '#') {
                    TODO(sp, "Break within try block, want to break parent loop instead");
                }
                return *it;
            }
        }

        void visit(::HIR::ExprNodeLoopControl& node) override {
            TRACE_FUNCTION_F("_LoopControl \"" << node.label << "\"");
            if (loopStack.size() == 0) {
                BUG(node.span(), "Loop control outside of a loop");
            }

            // Visit value before looking up the loop (loop stack may be manipulated during the inner visit)
            if (node.mValue) {
                ASSERT_BUG(node.span(), !node.isContinue, "Continue with a value isn't valid");
                DEBUG("break value;");
                this->visitNodePtr(node.mValue);
                //if( m_builder.resolve().type_is_impossible(node.span(), node.m_value->m_res_type) ) {
                if (node.mValue->resType->is_Diverge()) {
                    //ASSERT_BUG(node.span(), !m_builder.has_result(), "Result present when value type is uninhabited - " << node.m_value->m_res_type);
                    //ASSERT_BUG(node.span(), !m_builder.block_active(), "Result present when value type is uninhabited - " << node.m_value->m_res_type);
                }
            }
            if (!builder.block_active()) {
                // No block is currently active, not worth running the rest
                return;
            }

            // TODO: Use node.m_target_node
            const LoopDesc& targetBlock = this->findLoop(node.span(), node.label);

            if (node.isContinue) {
                builder.terminateScopeEarly(node.span(), targetBlock.scope, /*loop_exit=*/false);
                builder.endBlock(::MIR::Terminator::make_Goto(targetBlock.cur));
            } else {
                if (node.mValue) {
                    builder.pushStmtAssign(node.span(), targetBlock.resValue.clone(), builder.getResult(node.span()));
                } else {
                    // Set result to ()
                    builder.pushStmtAssign(node.span(), targetBlock.resValue.clone(), ::MIR::RValue::make_Tuple({{}}));
                }
                builder.terminateScopeEarly(node.span(), targetBlock.scope, /*loop_exit=*/true);
                builder.endBlock(::MIR::Terminator::make_Goto(targetBlock.next));
            }
        }

        void visit(::HIR::ExprNodeMatch& node) override {
            TRACE_FUNCTION_FR("_Match", "_Match");
            std::vector<unsigned> letElseInitializerTemps;
            size_t letElseFirstTemporary = 0;
            if (node.isLetElse) {
                ASSERT_BUG(node.span(), borrowRaiseTarget, "let-else match has no remainder temporary scope");
                letElseFirstTemporary = builder.localCount();
                this->visitNodePtr(node.mValue);
            } else {
                auto _ = saveAndEdit(borrowRaiseTarget, nullptr);
                this->visitNodePtr(node.mValue);
            }
            if (!builder.block_active()) {
                return;
            }
            auto matchVal = builder.getResultInLvalue(node.mValue->span(), node.mValue->resType);
            if (node.isLetElse) {
                const auto endTemporary = builder.localCount();
                letElseInitializerTemps.reserve(endTemporary - letElseFirstTemporary);
                for (auto temporary = letElseFirstTemporary; temporary < endTemporary; ++temporary) {
                    letElseInitializerTemps.push_back(temporary);
                }
            }

            if (node.arms.size() == 0) {
                // Nothing
                //const auto& ty = node.m_value->m_res_type;
                // TODO: Ensure that the type is a zero-variant enum or !
                builder.endSplitArmEarly(node.span());
                builder.endBlock(::MIR::Terminator::make_Unreachable({}));
                // Push an "diverge" result
                //m_builder.set_cur_block( m_builder.new_bb_unlinked() );
                //m_builder.set_result(node.span(), ::MIR::LValue::make_Invalid({}) );
            } else {
                MIRLowerHIRMatch(builder, *this, node, mv$(matchVal), letElseInitializerTemps);
            }

            if (builder.block_active()) {
                const auto& sp = node.span();

                auto res = builder.getResult(sp);
                //m_builder.raise_variables(sp, res, stmt_scope, /*to_above=*/true);
                builder.setResult(sp, mv$(res));

                //m_builder.terminate_scope( node.span(), mv$(stmt_scope) );
            } else {
                //m_builder.terminate_scope( node.span(), mv$(stmt_scope), false );
            }
        } // ExprNodeMatch

        void emitIf(/*const*/ ::HIR::ExprNodeP& cond, ::MIR::BasicBlockId trueBranch, ::MIR::BasicBlockId falseBranch) {
            TRACE_FUNCTION_F("true=bb" << trueBranch << ", false=bb" << falseBranch);
            auto* condP = &cond;

            // - Convert ! into a reverse of the branches
            {
                bool reverse = false;
                while (auto* condUni = cast<::HIR::ExprNodeUniOp>(condP->get())) {
                    ASSERT_BUG(condUni->span(), condUni->op == ::HIR::ExprNodeUniOp::Op::Invert, "Unexpected UniOp on boolean in `if` condition");
                    condP = &condUni->mValue;
                    reverse = !reverse;
                }

                if (reverse) {
                    ::std::swap(trueBranch, falseBranch);
                }
            }

            // Short-circuit && and ||
            if (auto* condBin = cast<::HIR::ExprNodeBinOp>(condP->get())) {
                switch (condBin->op) {
                    case ::HIR::ExprNodeBinOp::Op::BoolAnd:
                    case ::HIR::ExprNodeBinOp::Op::BoolOr: {
                        // TODO: Generate a SplitScope
                        if (condBin->op == ::HIR::ExprNodeBinOp::Op::BoolAnd) {
                            DEBUG("- Short-circuit BoolAnd");

                            // IF left false: go to false immediately
                            auto innerTrueBranch = builder.newBbUnlinked();
                            emitIf(condBin->left, innerTrueBranch, falseBranch);
                            // ELSE use right
                            builder.setCurBlock(innerTrueBranch);
                        } else {
                            DEBUG("- Short-circuit BoolOr");

                            // IF left true: got to true
                            auto innerFalseBranch = builder.newBbUnlinked();
                            emitIf(condBin->left, trueBranch, innerFalseBranch);
                            // ELSE use right
                            builder.setCurBlock(innerFalseBranch);
                        }

                        auto splitScope = builder.newScopeSplit(condBin->span());
                        builder.endSplitArm(condBin->span(), splitScope, /*reachable=*/true);
                        auto finalTrueBranch = builder.newBbUnlinked();
                        auto finalFalseBranch = builder.newBbUnlinked();
                        emitIf(condBin->right, finalTrueBranch, finalFalseBranch);

                        builder.setCurBlock(finalFalseBranch);
                        builder.endSplitArm(condBin->span(), splitScope, /*reachable=*/true, true);
                        builder.endBlock(MIR::Terminator::make_Goto(falseBranch));

                        builder.setCurBlock(finalTrueBranch);
                        builder.endSplitArm(condBin->span(), splitScope, /*reachable=*/true);
                        builder.terminateScope(condBin->span(), std::move(splitScope));
                        builder.endBlock(MIR::Terminator::make_Goto(trueBranch));
                    }
                        return;
                    default:
                        break;
                }
            }

            if (auto* condLit = cast<::HIR::ExprNodeLiteral>(condP->get())) {
                DEBUG("- constant condition");
                if (condLit->mData.as_Boolean()) {
                    builder.endBlock(::MIR::Terminator::make_Goto(trueBranch));
                } else {
                    builder.endBlock(::MIR::Terminator::make_Goto(falseBranch));
                }
                return;
            }

            // If short-circuiting didn't apply, emit condition
            ::MIR::LValue decisionVal;
            {
                auto scope = builder.newScopeTemp(cond->span());
                this->visitNodePtr(*condP);
                ASSERT_BUG(cond->span(), cond->resType == ::HIR::CoreType::Bool, "If condition wasn't a bool");
                decisionVal = builder.getResultInIfCond(cond->span());
                builder.terminateScope(cond->span(), mv$(scope));
            }

            builder.endBlock(::MIR::Terminator::make_If({mv$(decisionVal), trueBranch, falseBranch}));
        }

        void generateCheckedBinop(const Span& sp, ::MIR::LValue resSlot, ::MIR::eBinOp op, ::MIR::Param valL, const ::HIR::TypeData* tyL, ::MIR::Param valR, const ::HIR::TypeData* tyR) {
            switch (op) {
                case ::MIR::eBinOp::EQ:
                case ::MIR::eBinOp::NE:
                case ::MIR::eBinOp::LT:
                case ::MIR::eBinOp::LE:
                case ::MIR::eBinOp::GT:
                case ::MIR::eBinOp::GE:
                    ASSERT_BUG(sp, tyL == tyR, "Types in comparison operators must be equal - " << tyL << " != " << tyR);
                    // Defensive assert that the type is a valid MIR comparison
                TU_MATCH_HDRA( (*tyL), {)
                default:
                    BUG(sp, "Invalid type in comparison - " << tyL);
                        TU_ARMA(Pointer, e) {
                            // Valid
                        }
                        // TODO: Should straight comparisons on &str be supported here?
                        TU_ARMA(Primitive, e) {
                            if (e == ::HIR::CoreType::Str) {
                                BUG(sp, "Invalid type in comparison - " << tyL);
                            }
                        }
                }
                builder.pushStmtAssign(sp, mv$(resSlot), ::MIR::RValue::make_BinOp({ mv$(valL), op, mv$(valR) }));
                break;
            // Bitwise masking operations: Require equal integer types or bool
            case ::MIR::eBinOp::BIT_XOR:
            case ::MIR::eBinOp::BIT_OR :
            case ::MIR::eBinOp::BIT_AND:
                ASSERT_BUG(sp, tyL == tyR, "Types in bitwise operators must be equal - " << tyL << " != " << tyR);
                ASSERT_BUG(sp, tyL->is_Primitive(), "Only primitives allowed in bitwise operators");
                switch(tyL->as_Primitive())
                {
                        case ::HIR::CoreType::Str:
                        case ::HIR::CoreType::Char:
                        case ::HIR::CoreType::F32:
                        case ::HIR::CoreType::F64:
                            BUG(sp, "Invalid type for bitwise operator - " << tyL);
                        default:
                            break;
                }
                builder.pushStmtAssign(sp, mv$(resSlot), ::MIR::RValue::make_BinOp({ mv$(valL), op, mv$(valR) }));
                break;
            case ::MIR::eBinOp::ADD:    case ::MIR::eBinOp::ADD_OV:
            case ::MIR::eBinOp::SUB:    case ::MIR::eBinOp::SUB_OV:
            case ::MIR::eBinOp::MUL:    case ::MIR::eBinOp::MUL_OV:
            case ::MIR::eBinOp::DIV:    case ::MIR::eBinOp::DIV_OV:
            case ::MIR::eBinOp::MOD:
                ASSERT_BUG(sp, tyL == tyR, "Types in arithmatic operators must be equal - " << tyL << " != " << tyR);
                ASSERT_BUG(sp, tyL->is_Primitive(), "Only primitives allowed in arithmatic operators");
                switch(tyL->as_Primitive())
                {
                        case ::HIR::CoreType::Str:
                        case ::HIR::CoreType::Char:
                        case ::HIR::CoreType::Bool:
                            BUG(sp, "Invalid type for arithmatic operator - " << tyL);
                        default:
                            break;
                }
                // TODO: Overflow checks (none for eBinOp::MOD)
                builder.pushStmtAssign(sp, mv$(resSlot), ::MIR::RValue::make_BinOp({ mv$(valL), op, mv$(valR) }));
                break;
            case ::MIR::eBinOp::BIT_SHL:
            case ::MIR::eBinOp::BIT_SHR:
                ;
                ASSERT_BUG(sp, tyL->is_Primitive(), "Only primitives allowed in arithmatic operators");
                ASSERT_BUG(sp, tyR->is_Primitive(), "Only primitives allowed in arithmatic operators");
                switch(tyL->as_Primitive())
                {
                        case ::HIR::CoreType::Str:
                        case ::HIR::CoreType::Char:
                        case ::HIR::CoreType::F32:
                        case ::HIR::CoreType::F64:
                            BUG(sp, "Invalid type for shift op-assignment - " << tyL);
                        default:
                            break;
                }
                switch(tyR->as_Primitive())
                {
                        case ::HIR::CoreType::Str:
                        case ::HIR::CoreType::Char:
                        case ::HIR::CoreType::F32:
                        case ::HIR::CoreType::F64:
                            BUG(sp, "Invalid type for shift op-assignment - " << tyR);
                        default:
                            break;
                }
                // TODO: Overflow check
                builder.pushStmtAssign(sp, mv$(resSlot), ::MIR::RValue::make_BinOp({ mv$(valL), op, mv$(valR) }));
                break;
            }
        }

        void visit(::HIR::ExprNodeAssign& node) override {
            TRACE_FUNCTION_F("_Assign");
            const auto& sp = node.span();
            auto _ = disableBorrowExtension(); // A bit of a hack

            this->visitNodePtr(node.mValue);
            ::MIR::RValue val = builder.getResult(sp);

            this->visitNodePtr(node.slot);
            auto dst = builder.getResultUnwrapLvalue(sp);

            const auto& tySlot = node.slot->resType;
            const auto& tyVal = node.mValue->resType;

            if (node.op != ::HIR::ExprNodeAssign::Op::None) {
                auto dstClone = dst.clone();
                ::MIR::Param valP;
                if (auto* e = val.opt_Use()) {
                    valP = mv$(*e);
                } else if (auto* e = val.opt_Constant()) {
                    valP = mv$(*e);
                } else {
                    valP = builder.lvalueOrTemp(node.span(), tyVal, mv$(val));
                }

                ASSERT_BUG(sp, tySlot->is_Primitive(), "Assignment operator overloads are only valid on primitives - ty_slot=" << tySlot);
                ASSERT_BUG(sp, tyVal->is_Primitive(), "Assignment operator overloads are only valid on primitives - ty_val=" << tyVal);

#define _(v) ::HIR::ExprNodeAssign::Op::v
                ::MIR::eBinOp op;
                switch (node.op) {
                    case _(None):
                        throw "";
                    case _(Add):
                        op = ::MIR::eBinOp::ADD;
                        if (0) {
                            case _(Sub):
                                op = ::MIR::eBinOp::SUB;
                        }
                        if (0) {
                            case _(Mul):
                                op = ::MIR::eBinOp::MUL;
                        }
                        if (0) {
                            case _(Div):
                                op = ::MIR::eBinOp::DIV;
                        }
                        if (0) {
                            case _(Mod):
                                op = ::MIR::eBinOp::MOD;
                        }
                        this->generateCheckedBinop(sp, mv$(dst), op, mv$(dstClone), tySlot, mv$(valP), tyVal);
                        break;
                    case _(Xor):
                        op = ::MIR::eBinOp::BIT_XOR;
                        if (0) {
                            case _(Or):
                                op = ::MIR::eBinOp::BIT_OR;
                        }
                        if (0) {
                            case _(And):
                                op = ::MIR::eBinOp::BIT_AND;
                        }
                        this->generateCheckedBinop(sp, mv$(dst), op, mv$(dstClone), tySlot, mv$(valP), tyVal);
                        break;
                    case _(Shl):
                        op = ::MIR::eBinOp::BIT_SHL;
                        if (0) {
                            case _(Shr):
                                op = ::MIR::eBinOp::BIT_SHR;
                        }
                        this->generateCheckedBinop(sp, mv$(dst), op, mv$(dstClone), tySlot, mv$(valP), tyVal);
                        break;
                }
#undef _
            } else {
                ASSERT_BUG(sp, tySlot == tyVal || tySlot->equalsIgnoringRegions(tyVal), "Types must match for assignment - " << tySlot << " != " << tyVal);
                builder.pushStmtAssign(node.span(), mv$(dst), mv$(val));
            }
            builder.setResult(node.span(), ::MIR::RValue::make_Tuple({}));
        }

        void visit(::HIR::ExprNodeBinOp& node) override {
            const auto& sp = node.span();
            TRACE_FUNCTION_F("_BinOp");

            const auto& tyL = node.left->resType;
            const auto& tyR = node.right->resType;
            auto res = builder.newTemporary(node.resType);

            // Short-circuiting boolean operations
            if (node.op == ::HIR::ExprNodeBinOp::Op::BoolAnd || node.op == ::HIR::ExprNodeBinOp::Op::BoolOr) {
                DEBUG("- ShortCircuit Left");
                this->visitNodePtr(node.left);
                if (!builder.block_active()) {
                    return;
                }
                auto left = builder.getResultInLvalue(node.left->span(), tyL);

                auto bbNext = builder.newBbUnlinked();
                auto bbTrue = builder.newBbUnlinked();
                auto bbFalse = builder.newBbUnlinked();
                builder.endBlock(::MIR::Terminator::make_If({mv$(left), bbTrue, bbFalse}));

                // Generate a SplitScope to handle the conditional nature of the next code
                auto splitScope = builder.newScopeSplit(node.span());

                if (node.op == ::HIR::ExprNodeBinOp::Op::BoolOr) {
                    DEBUG("- ShortCircuit ||");
                    // If left is true, assign result true and return
                    builder.setCurBlock(bbTrue);
                    builder.pushStmtAssign(node.span(), res.clone(), ::MIR::RValue(::MIR::Constant::make_Bool({true})));
                    builder.endSplitArm(node.left->span(), splitScope, /*reachable=*/true);
                    builder.endBlock(::MIR::Terminator::make_Goto(bbNext));

                    // If left is false, assign result to right
                    builder.setCurBlock(bbFalse);
                } else {
                    DEBUG("- ShortCircuit &&");
                    // If left is false, assign result false and return
                    builder.setCurBlock(bbFalse);
                    builder.pushStmtAssign(node.span(), res.clone(), ::MIR::RValue(::MIR::Constant::make_Bool({false})));
                    builder.endSplitArm(node.left->span(), splitScope, /*reachable=*/true);
                    builder.endBlock(::MIR::Terminator::make_Goto(bbNext));

                    // If left is true, assign result to right
                    builder.setCurBlock(bbTrue);
                }

                DEBUG("- ShortCircuit Right");
                auto tmpScope = builder.newScopeTemp(node.right->span());
                this->visitNodePtr(node.right);
                if (!builder.block_active()) {
                    builder.terminateScope(node.right->span(), mv$(tmpScope), false);
                    builder.endSplitArm(node.right->span(), splitScope, /*reachable=*/false);
                    builder.setCurBlock(bbNext);
                    builder.terminateScope(node.span(), mv$(splitScope));
                    builder.setResult(node.span(), mv$(res));
                    return;
                }
                builder.pushStmtAssign(node.span(), res.clone(), builder.getResult(node.right->span()));
                builder.terminateScope(node.right->span(), mv$(tmpScope));

                builder.endSplitArm(node.right->span(), splitScope, /*reachable=*/true);
                builder.endBlock(::MIR::Terminator::make_Goto(bbNext));

                builder.setCurBlock(bbNext);
                builder.terminateScope(node.span(), mv$(splitScope));
                builder.setResult(node.span(), mv$(res));
                return;
            } else {
            }

            this->visitNodePtr(node.left);
            if (!builder.block_active()) {
                return;
            }
            auto left = builder.getResultInParam(node.left->span(), tyL);
            this->visitNodePtr(node.right);
            if (!builder.block_active()) {
                return;
            }
            auto right = builder.getResultInParam(node.right->span(), tyR);

            ::MIR::eBinOp op;
            switch (node.op) {
                case ::HIR::ExprNodeBinOp::Op::CmpEqu:
                    op = ::MIR::eBinOp::EQ;
                    if (0) {
                        case ::HIR::ExprNodeBinOp::Op::CmpNEqu:
                            op = ::MIR::eBinOp::NE;
                    }
                    if (0) {
                        case ::HIR::ExprNodeBinOp::Op::CmpLt:
                            op = ::MIR::eBinOp::LT;
                    }
                    if (0) {
                        case ::HIR::ExprNodeBinOp::Op::CmpLtE:
                            op = ::MIR::eBinOp::LE;
                    }
                    if (0) {
                        case ::HIR::ExprNodeBinOp::Op::CmpGt:
                            op = ::MIR::eBinOp::GT;
                    }
                    if (0) {
                        case ::HIR::ExprNodeBinOp::Op::CmpGtE:
                            op = ::MIR::eBinOp::GE;
                    }
                    this->generateCheckedBinop(sp, res.clone(), op, mv$(left), tyL, mv$(right), tyR);
                    break;

                case ::HIR::ExprNodeBinOp::Op::Xor:
                    op = ::MIR::eBinOp::BIT_XOR;
                    if (0) {
                        case ::HIR::ExprNodeBinOp::Op::Or:
                            op = ::MIR::eBinOp::BIT_OR;
                    }
                    if (0) {
                        case ::HIR::ExprNodeBinOp::Op::And:
                            op = ::MIR::eBinOp::BIT_AND;
                    }
                    this->generateCheckedBinop(sp, res.clone(), op, mv$(left), tyL, mv$(right), tyR);
                    break;

                case ::HIR::ExprNodeBinOp::Op::Shr:
                    op = ::MIR::eBinOp::BIT_SHR;
                    if (0) {
                        case ::HIR::ExprNodeBinOp::Op::Shl:
                            op = ::MIR::eBinOp::BIT_SHL;
                    }
                    this->generateCheckedBinop(sp, res.clone(), op, mv$(left), tyL, mv$(right), tyR);
                    break;

                case ::HIR::ExprNodeBinOp::Op::Add:
                    op = ::MIR::eBinOp::ADD;
                    if (0) {
                        case ::HIR::ExprNodeBinOp::Op::Sub:
                            op = ::MIR::eBinOp::SUB;
                    }
                    if (0) {
                        case ::HIR::ExprNodeBinOp::Op::Mul:
                            op = ::MIR::eBinOp::MUL;
                    }
                    if (0) {
                        case ::HIR::ExprNodeBinOp::Op::Div:
                            op = ::MIR::eBinOp::DIV;
                    }
                    if (0) {
                        case ::HIR::ExprNodeBinOp::Op::Mod:
                            op = ::MIR::eBinOp::MOD;
                    }
                    this->generateCheckedBinop(sp, res.clone(), op, mv$(left), tyL, mv$(right), tyR);
                    break;

                // Short-circuiting boolean operations
                case ::HIR::ExprNodeBinOp::Op::BoolAnd:
                case ::HIR::ExprNodeBinOp::Op::BoolOr:
                    BUG(node.span(), "");
                    break;
            }
            builder.setResult(node.span(), mv$(res));
        }

        void visit(::HIR::ExprNodeUniOp& node) override {
            TRACE_FUNCTION_F("_UniOp");

            const auto& tyVal = node.mValue->resType;
            this->visitNodePtr(node.mValue);
            auto val = builder.getResultInLvalue(node.mValue->span(), tyVal);

            ::MIR::RValue res;
            switch (node.op) {
                case ::HIR::ExprNodeUniOp::Op::Invert:
                    if (tyVal->is_Primitive()) {
                        switch (tyVal->as_Primitive()) {
                            case ::HIR::CoreType::Str:
                            case ::HIR::CoreType::Char:
                            case ::HIR::CoreType::F32:
                            case ::HIR::CoreType::F64:
                                BUG(node.span(), "`!` operator on invalid type - " << tyVal);
                                break;
                            default:
                                break;
                        }
                    } else {
                        BUG(node.span(), "`!` operator on invalid type - " << tyVal);
                    }
                    res = ::MIR::RValue::make_UniOp({mv$(val), ::MIR::eUniOp::INV});
                    break;
                case ::HIR::ExprNodeUniOp::Op::Negate:
                    if (tyVal->is_Primitive()) {
                        switch (tyVal->as_Primitive()) {
                            case ::HIR::CoreType::Str:
                            case ::HIR::CoreType::Char:
                            case ::HIR::CoreType::Bool:
                                BUG(node.span(), "`-` operator on invalid type - " << tyVal);
                                break;
                            case ::HIR::CoreType::U8:
                            case ::HIR::CoreType::U16:
                            case ::HIR::CoreType::U32:
                            case ::HIR::CoreType::U64:
                            case ::HIR::CoreType::U128:
                            case ::HIR::CoreType::Usize:
                                BUG(node.span(), "`-` operator on unsigned integer - " << tyVal);
                                break;
                            default:
                                break;
                        }
                    } else {
                        BUG(node.span(), "`!` operator on invalid type - " << tyVal);
                    }
                    res = ::MIR::RValue::make_UniOp({mv$(val), ::MIR::eUniOp::NEG});
                    break;
            }
            builder.setResult(node.span(), mv$(res));
        }

        void visit(::HIR::ExprNodeBorrow& node) override {
            TRACE_FUNCTION_F("_Borrow");

            auto _ = saveAndEdit(inBorrow, true);

            const auto& tyVal = node.mValue->resType;
            this->visitNodePtr(node.mValue);
            auto val = builder.getResultInLvalue(node.mValue->span(), tyVal);

            if (borrowRaiseTarget) {
                DEBUG("- Raising borrow to scope " << *borrowRaiseTarget);
                builder.raiseTemporaries(node.span(), val, *borrowRaiseTarget);
            }

            builder.setResult(node.span(), ::MIR::RValue::make_Borrow({node.mType, false, mv$(val)}));
        }

        void visit(::HIR::ExprNodeRawBorrow& node) override {
            TRACE_FUNCTION_F("_RawBorrow");

            auto _ = saveAndEdit(inBorrow, true);

            const auto& tyVal = node.mValue->resType;
            this->visitNodePtr(node.mValue);
            auto val = builder.getResultInLvalue(node.mValue->span(), tyVal);

            if (borrowRaiseTarget) {
                DEBUG("- Raising borrow to scope " << *borrowRaiseTarget);
                builder.raiseTemporaries(node.span(), val, *borrowRaiseTarget);
            }

            builder.setResult(node.span(), ::MIR::RValue::make_Borrow({node.mType, true, mv$(val)}));
        }

        void visit(::HIR::ExprNodeCast& node) override {
            TRACE_FUNCTION_F("_Cast " << node.resType);
            this->visitNodePtr(node.mValue);

            const auto& tyOut = node.resType;
            const auto& tyIn = node.mValue->resType;

            // TODO: The correct behavior is to do the cast (into a rvalue) no matter what.
            // See test run-pass/issue-36936
            if (tyOut == tyIn) {
                return;
            }

            auto val = builder.getResultInLvalue(node.mValue->span(), node.mValue->resType);

            TU_MATCH_HDRA( (*tyOut), {)
            default:
                BUG(node.span(), "Invalid cast to " << tyOut << " from " << tyIn);
                TU_ARMA(Function, de) {
                    // Just trust the previous stages.
                    if (tyIn->is_Function()) {
                        ASSERT_BUG(node.span(), de.argTypes == tyIn->as_Function().argTypes, tyIn);
                    } else if (tyIn->is_NamedFunction()) {
                        // TODO: Extra checks?
                    } else {
                        BUG(node.span(), "_Cast from bad type: " << tyIn);
                    }
                }
                TU_ARMA(Pointer, de) {
                    if (tyIn->is_Primitive()) {
                        const auto& ie = tyIn->as_Primitive();
                        switch (ie) {
                            case ::HIR::CoreType::Bool:
                            case ::HIR::CoreType::Char:
                            case ::HIR::CoreType::Str:
                            case ::HIR::CoreType::F32:
                            case ::HIR::CoreType::F64:
                                BUG(node.span(), "Cannot cast to pointer from " << tyIn);
                            default:
                                break;
                        }
                        // TODO: Only valid if T: Sized in *{const/mut/move} T
                    } else if (const auto* se = tyIn->opt_Borrow()) {
                        if (de.inner != se->inner && !de.inner->equalsIgnoringRegions(se->inner)) {
                            BUG(node.span(), "Cannot cast to " << tyOut << " from " << tyIn);
                        }
                        // Valid
                    } else if (tyIn->is_Function() || tyIn->is_NamedFunction()) {
                        if (!builder.resolve().typeIsSized(node.span(), de.inner)) {
                            BUG(node.span(), "Cannot cast to " << tyOut << " from " << tyIn);
                        }
                        // Valid
                    } else if (const auto* se = tyIn->opt_Pointer()) {
                        // Valid
                        if (se->inner == de.inner) {
                        }
                        // - If making a fat pointer from thin, convert to _Unsize
                        else if (builder.resolve().canUnsize(node.span(), de.inner, se->inner)) {
                            builder.setResult(node.span(), ::MIR::RValue::make_MakeDst({mv$(val), ::MIR::Constant::make_ItemAddr({})}));
                            auto tmpTy = builder.resolve().crate.types.pointer(se->type, de.inner);
                            val = builder.getResultInLvalue(node.mValue->span(), tmpTy);
                        }
                    } else {
                        BUG(node.span(), "Cannot cast to pointer from " << tyIn);
                    }
                }
                TU_ARMA(Primitive, de) {
                    switch (de) {
                        case ::HIR::CoreType::Str:
                            BUG(node.span(), "Cannot cast to str");
                            break;
                        case ::HIR::CoreType::Char:
                            if (tyIn->is_Primitive() && tyIn->as_Primitive() == ::HIR::CoreType::U8) {
                                // Valid
                            } else {
                                BUG(node.span(), "Cannot cast to char from " << tyIn);
                            }
                            break;
                        case ::HIR::CoreType::Bool:
                            BUG(node.span(), "Cannot cast to bool");
                            break;
                        case ::HIR::CoreType::F32:
                        case ::HIR::CoreType::F64:
                            if (tyIn->is_Primitive()) {
                                switch (de) {
                                    case ::HIR::CoreType::Str:
                                    case ::HIR::CoreType::Char:
                                    case ::HIR::CoreType::Bool:
                                        BUG(node.span(), "Cannot cast to " << tyOut << " from " << tyIn);
                                        break;
                                    default:
                                        // Valid
                                        break;
                                }
                            } else {
                                BUG(node.span(), "Cannot cast to " << tyOut << " from " << tyIn);
                            }
                            break;
                        default:
                            if (tyIn->opt_Primitive()) {
                                switch (de) {
                                    case ::HIR::CoreType::Str:
                                        BUG(node.span(), "Cannot cast to " << tyOut << " from " << tyIn);
                                    default:
                                        // Valid
                                        break;
                                }
                            } else if (const auto* se = tyIn->opt_Path()) {
                                if (se->binding.is_Enum()) {
                                    // TODO: Check if it's a repr(ty/C) enum - and if the type matches
                                } else {
                                    BUG(node.span(), "Cannot cast to " << tyOut << " from " << tyIn);
                                }
                            }
                            // NOTE: Valid for all integer types
                            else if (tyIn->is_Pointer()) {
                                // TODO: Only valid for T: Sized?
                            } else if (de == ::HIR::CoreType::Usize && tyIn->is_Function()) {
                                // TODO: Always valid?
                            } else if (de == ::HIR::CoreType::Usize && tyIn->is_NamedFunction()) {
                                // TODO: Always valid?
                            } else {
                                BUG(node.span(), "Cannot cast to " << tyOut << " from " << tyIn);
                            }
                            break;
                    }
                }
            }
            auto res = builder.newTemporary(node.resType);
            builder.pushStmtAssign(node.span(), res.clone(), ::MIR::RValue::make_Cast({ mv$(val), node.resType }));
            builder.setResult( node.span(), mv$(res) );
        }

        void visit(::HIR::ExprNodeUnsize& node) override {
            TRACE_FUNCTION_F("_Unsize");
            this->visitNodePtr(node.mValue);

            const auto& tyOut = node.resType;
            const auto& tyIn = node.mValue->resType;

            if (tyOut == tyIn) {
                return;
            }

            auto ptrLval = builder.getResultInLvalue(node.mValue->span(), node.mValue->resType);

            if (tyOut->is_Borrow() && tyIn->is_Borrow()) {
                const auto& oe = tyOut->as_Borrow();
                const auto& ie = tyIn->as_Borrow();
                const auto& tyOut = oe.inner;
                const auto& tyIn = ie.inner;
                TU_MATCH_HDRA( (*tyOut), {)
                default: {
                        const auto& langUnsize = builder.crate().getLangItemPath(node.span(), "unsize");
                        if (builder.resolve().findImpl(node.span(), langUnsize, ::HIR::PathParams(tyOut), tyIn, [](auto, bool) {
                            return true;
                        })) {
                            // - HACK: Emit a cast operation on the pointers. Leave it up to monomorph to 'fix' it
                            builder.setResult(node.span(), ::MIR::RValue::make_MakeDst({mv$(ptrLval), ::MIR::Constant::make_ItemAddr({})}));
                        } else {
                            // Probably an error?
                            builder.setResult(node.span(), ::MIR::RValue::make_MakeDst({mv$(ptrLval), ::MIR::Constant::make_ItemAddr({})}));
                            //TODO(node.span(), "MIR _Unsize to " << ty_out);
                        }
                    }
                    TU_ARMA(Slice, e) {
                        if (tyIn->is_Array()) {
                            const auto& inArray = tyIn->as_Array();
                            ::MIR::Constant sizeVal;
                        TU_MATCH_HDRA( (inArray.size), {)
                        TU_ARMA(Unevaluated, se) {
                            TU_MATCH_HDRA( (se), {)
                            default:
                                BUG(node.span(), "Unsize Array with unknown size " << tyIn);
                                        TU_ARMA(Generic, cge)
                                        sizeVal = cge;
                            }
                                }
                                TU_ARMA(Known, se) {
                                    sizeVal = ::MIR::Constant::make_Uint({U128(se), ::HIR::CoreType::Usize});
                                }
                        }
                        builder.setResult( node.span(), ::MIR::RValue::make_MakeDst({ mv$(ptrLval), mv$(sizeVal) }) );
                        } else if (tyIn->is_Generic() || (tyIn->is_Path() && tyIn->as_Path().binding.is_Opaque())) {
                            // The source is thin here: its concrete array length becomes
                            // available only after monomorphisation. Preserve the unsize
                            // sentinel for MIR cleanup instead of reading nonexistent metadata.
                            builder.setResult(node.span(), ::MIR::RValue::make_MakeDst({mv$(ptrLval), ::MIR::Constant::make_ItemAddr({})}));
                        } else {
                            ASSERT_BUG(node.span(), tyIn->is_Array(), "Unsize to slice from non-array - " << tyIn);
                        }
                    }
                    TU_ARMA(TraitObject, e) {
                        // NOTE: This pattern (an empty ItemAddr) is detected by cleanup, which populates the vtable properly
                        builder.setResult(node.span(), ::MIR::RValue::make_MakeDst({mv$(ptrLval), ::MIR::Constant::make_ItemAddr({})}));
                    }
                }
            } else {
                // NOTES: (from IRC: eddyb)
                // < eddyb> they're required that T and U are the same struct definition (with different type parameters) and exactly one field differs in type between T and U (ignoring PhantomData)
                // < eddyb> Mutabah: I forgot to mention that the field that differs in type must also impl CoerceUnsized

                // TODO: Just emit a cast and leave magic handling to codegen
                // - This code _could_ do inspection of the types and insert a destructure+unsize+restructure, but that does't handle direct `T: CoerceUnsize<U>`
                builder.setResult(node.span(), ::MIR::RValue::make_MakeDst({mv$(ptrLval), ::MIR::Constant::make_ItemAddr({})}));
            }
        }

        void visitIndexOperator(::HIR::ExprNodeIndex& node, const ::HIR::TypeData* tyVal, MIR::LValue value, const ::HIR::TypeData* tyIdx, MIR::LValue index) {
            DEBUG("");
            const Span& sp = node.span();

            // NOTE: Do operator replacement here after handling scope-raising for _Borrow
            if (borrowRaiseTarget && inBorrow) {
                DEBUG("- Raising deref in borrow to scope " << *borrowRaiseTarget);
                builder.raiseTemporaries(sp, value, *borrowRaiseTarget);
            }

            const char* langitem = nullptr;
            const char* method = nullptr;
            ::HIR::BorrowType bt;
            switch (node.mValue->usage) {
                case ::HIR::ValueUsage::Unknown:
                    BUG(sp, "Usage of index reciever is still `Unknown`");
                    break;
                case ::HIR::ValueUsage::Borrow:
                    bt = ::HIR::BorrowType::Shared;
                    langitem = method = "index";
                    break;
                case ::HIR::ValueUsage::Mutate:
                    bt = ::HIR::BorrowType::Unique;
                    langitem = method = "index_mut";
                    break;
                case ::HIR::ValueUsage::Move:
                    TODO(sp, "Support moving out of indexed values");
                    break;
            }
            // Needs replacement, continue
            assert(langitem);
            assert(method);

            // - Construct trait path - Index*<IdxTy>
            ::HIR::PathParams ppTrait;
            ppTrait.types.push_back(tyIdx);
            ::HIR::GenericPath trait{builder.resolve().crate.getLangItemPath(node.span(), langitem), std::move(ppTrait)};

            ::HIR::PathParams ppMethod;
            ppMethod.mLifetimes.push_back(HIR::LifetimeRef());
            auto method_path = ::HIR::Path(tyVal, std::move(trait), RcString::newInterned(method), std::move(ppMethod));

            // Store a borrow of the input value
            ::std::vector<::MIR::Param> args;
            args.push_back(builder.lvalueOrTemp(sp, builder.resolve().crate.types.borrow(bt, node.mValue->resType), ::MIR::RValue::make_Borrow({bt, false, std::move(value)})));
            args.push_back(std::move(index));
            builder.movedLvalue(node.span(), args[0].as_LValue());
            builder.movedLvalue(node.span(), args[1].as_LValue());
            auto resVal = builder.newTemporary(builder.resolve().crate.types.borrow(bt, node.resType));
            // Call the above trait method
            // Store result of that call in `val` (which will be derefed below)
            auto okBlock = builder.newBbUnlinked();
            auto panicBlock = builder.newBbUnlinked();
            builder.endBlock(::MIR::Terminator::make_Call({okBlock, ::MIR::UnwindAction::make_Cleanup(panicBlock), resVal.clone(), std::move(method_path), std::move(args)}));
            builder.setCurBlock(panicBlock);
            emitUnwind(sp);

            builder.setCurBlock(okBlock);
            builder.setResult(node.span(), ::MIR::LValue::newDeref(std::move(resVal)));
        }

        void visit(::HIR::ExprNodeIndex& node) override {
            TRACE_FUNCTION_F("_Index");

            // NOTE: Calculate the index first (so if it borrows from the source, it's over by the time that's needed)
            const auto& tyIdx = node.index->resType;
            this->visitNodePtr(node.index);
            auto index = builder.getResultInLvalue(node.index->span(), tyIdx);

            const auto& tyVal = node.mValue->resType;
            this->visitNodePtr(node.mValue);
            auto value = builder.getResultInLvalue(node.mValue->span(), tyVal);

            if (tyIdx != ::HIR::CoreType::Usize) {
                DEBUG("non-usize index");
                visitIndexOperator(node, tyVal, std::move(value), tyIdx, std::move(index));
                return;
            }

            ::MIR::RValue limitVal;
            TU_MATCH_HDRA( (*tyVal), {)
            default:
                DEBUG("non-builtin type");
                visitIndexOperator(node, tyVal, std::move(value), tyIdx, std::move(index));
                return;
                TU_ARMA(Array, e) {
                TU_MATCH_HDRA( (e.size), {)
                TU_ARMA(Unevaluated, se) {
                            if (se.is_Generic()) {
                                limitVal = ::MIR::Constant::make_Generic(se.as_Generic());
                                break;
                            }
                            BUG(node.span(), "Indexing with unknown size - " << e.size);
                        }
                        TU_ARMA(Known, se) {
                            limitVal = ::MIR::Constant::make_Uint({U128(se), ::HIR::CoreType::Usize});
                        }
                }
                }
                TU_ARMA(Slice, e) {
                    limitVal = ::MIR::RValue::make_DstMeta({builder.getPtrToDst(node.mValue->span(), value)});
                }
            }

            {
                auto limitLval = builder.lvalueOrTemp(node.span(), tyIdx, mv$(limitVal));

                auto cmpRes = builder.newTemporary(builder.resolve().crate.types.primitive(::HIR::CoreType::Bool));
                builder.pushStmtAssign(node.span(), cmpRes.clone(), ::MIR::RValue::make_BinOp({index.clone(), ::MIR::eBinOp::GE, limitLval.clone()}));
                auto armPanic = builder.newBbUnlinked();
                auto armContinue = builder.newBbUnlinked();
                builder.endBlock(::MIR::Terminator::make_If({mv$(cmpRes), armPanic, armContinue}));

                builder.setCurBlock(armPanic);
                const auto& panicBoundsCheck = builder.crate().getLangItemPath(node.span(), "panic_bounds_check");
                auto panicResult = builder.newTemporary(builder.resolve().crate.types.diverge());
                auto panicReturn = builder.newBbUnlinked();
                auto panicUnwind = builder.newBbUnlinked();
                builder.endBlock(
                    ::MIR::Terminator::make_Call({
                        panicReturn,
                        ::MIR::UnwindAction::make_Cleanup(panicUnwind),
                        std::move(panicResult),
                        ::HIR::Path(panicBoundsCheck),
                        makeVec2<::MIR::Param>(index.clone(), limitLval.clone()),
                    })
                );

                builder.setCurBlock(panicReturn);
                builder.endBlock(::MIR::Terminator::make_Unreachable({}));

                builder.setCurBlock(panicUnwind);
                emitUnwind(node.span());

                builder.setCurBlock(armContinue);
            }

            if( !index.is_Local())
            {
                auto localIdx = builder.newTemporary(builder.resolve().crate.types.primitive(::HIR::CoreType::Usize));
                builder.pushStmtAssign(node.span(), localIdx.clone(), mv$(index));
                index = mv$(localIdx);
            }
            builder.setResult( node.span(), ::MIR::LValue::newIndex( mv$(value), index.root.as_Local() ) );
        }

        void visit(::HIR::ExprNodeDeref& node) override {
            const Span& sp = node.span();
            TRACE_FUNCTION_F("_Deref");

            const auto& tyVal = node.mValue->resType;
            this->visitNodePtr(node.mValue);
            auto val = builder.getResultInLvalue(node.mValue->span(), tyVal);

            bool useTrait = node.traitUsed == ::HIR::ExprNodeDeref::TraitUsed::Trait;
            if (node.traitUsed == ::HIR::ExprNodeDeref::TraitUsed::Unknown) {
                useTrait = !tyVal->is_Pointer() && !tyVal->is_Borrow() && !builder.isTypeOwnedBox(tyVal);
            }

            if (useTrait) {
                // Do operator replacement here after handling scope-raising
                // for _Borrow.  The type checker recorded this choice, so a
                // primitive reference can still dispatch to a user impl.
                if (borrowRaiseTarget && inBorrow) {
                    DEBUG("- Raising deref in borrow to scope " << *borrowRaiseTarget);
                    builder.raiseTemporaries(node.span(), val, *borrowRaiseTarget);
                }

                const char* langitem = nullptr;
                const char* method = nullptr;
                ::HIR::BorrowType bt;
                // - Uses the value's usage beacuse for T: Copy node.m_value->m_usage is Borrow, but node.m_usage is Move
                switch (node.mValue->usage) {
                    case ::HIR::ValueUsage::Unknown:
                        BUG(sp, "Unknown usage type of deref value - " << tyVal);
                        break;
                    case ::HIR::ValueUsage::Borrow:
                        bt = ::HIR::BorrowType::Shared;
                        langitem = method = "deref";
                        break;
                    case ::HIR::ValueUsage::Mutate:
                        bt = ::HIR::BorrowType::Unique;
                        langitem = method = "deref_mut";
                        break;
                    case ::HIR::ValueUsage::Move:
                        TODO(sp, "ValueUsage::Move for desugared Deref of " << node.mValue->resType);
                        break;
                }
                assert(langitem);
                assert(method);

                auto method_path = ::HIR::Path(tyVal, ::HIR::GenericPath(builder.resolve().crate.getLangItemPath(node.span(), langitem), {}), method, HIR::PathParams(HIR::LifetimeRef()));

                ::std::vector<::MIR::Param> args;
                args.push_back(builder.lvalueOrTemp(sp, builder.resolve().crate.types.borrow(bt, node.mValue->resType), ::MIR::RValue::make_Borrow({bt, false, mv$(val)})));
                builder.movedLvalue(node.span(), args[0].as_LValue());
                val = builder.newTemporary(builder.resolve().crate.types.borrow(bt, node.resType));
                auto okBlock = builder.newBbUnlinked();
                auto panicBlock = builder.newBbUnlinked();
                builder.endBlock(::MIR::Terminator::make_Call({okBlock, ::MIR::UnwindAction::make_Cleanup(panicBlock), val.clone(), mv$(method_path), mv$(args)}));
                builder.setCurBlock(panicBlock);
                emitUnwind(sp);

                builder.setCurBlock(okBlock);
            }

            builder.setResult( node.span(), ::MIR::LValue::newDeref( mv$(val) ) );
        }

        void visit(::HIR::ExprNodeEmplace& node) override {
            assert(node.mType == ::HIR::ExprNodeEmplace::Type::Boxer);
            const auto& dataTy = node.mValue->resType;

            node.mValue->visit(*this);
            auto val = builder.getResult(node.span());

            return boxNew(node, dataTy, std::move(val));
        }

        void boxNew(::HIR::ExprNode& node, const ::HIR::TypeData* dataTy, ::MIR::RValue val) {
            const auto& langExchangeMalloc = builder.crate().getLangItemPath(node.span(), "exchange_malloc");
            //const auto& lang_owned_box = m_builder.crate().get_lang_item_path(node.span(), "owned_box");

            ::HIR::PathParams traitParamsData;
            traitParamsData.types.push_back(dataTy);
            auto& types = builder.resolve().crate.types;

            // 1. Determine the size/alignment of the type
            ::MIR::Param sizeParam, alignParam;
            size_t itemSize, itemAlign;
            if (TargetGetSizeAndAlignOf(node.span(), builder.resolve(), dataTy, itemSize, itemAlign)) {
                sizeParam = ::MIR::Constant::make_Uint({U128(itemSize), ::HIR::CoreType::Usize});
                alignParam = ::MIR::Constant::make_Uint({U128(itemAlign), ::HIR::CoreType::Usize});
            } else {
                // Insert calls to "size_of" and "align_of" intrinsics
                auto sizeSlot = builder.newTemporary(types.primitive(::HIR::CoreType::Usize));
                auto sizePanic = builder.newBbUnlinked();
                auto sizeOk = builder.newBbUnlinked();
                builder.endBlock(::MIR::Terminator::make_Call({sizeOk, ::MIR::UnwindAction::make_Cleanup(sizePanic), sizeSlot.clone(), ::MIR::CallTarget::make_Intrinsic({"size_of", traitParamsData.clone()}), {}}));
                builder.setCurBlock(sizePanic);
                emitUnwind(node.span());
                builder.setCurBlock(sizeOk);
                auto alignSlot = builder.newTemporary(types.primitive(::HIR::CoreType::Usize));
                auto alignPanic = builder.newBbUnlinked();
                auto alignOk = builder.newBbUnlinked();
                builder.endBlock(::MIR::Terminator::make_Call({alignOk, ::MIR::UnwindAction::make_Cleanup(alignPanic), alignSlot.clone(), ::MIR::CallTarget::make_Intrinsic({"align_of", traitParamsData.clone()}), {}}));
                builder.setCurBlock(alignPanic);
                emitUnwind(node.span());
                builder.setCurBlock(alignOk);

                sizeParam = ::std::move(sizeSlot);
                alignParam = ::std::move(alignSlot);
            }

            // 2. Call the allocator function and get a pointer
            // - NOTE: "exchange_malloc" returns a `*mut u8`, need to cast that to the target type
            auto placeRawType = types.pointer(::HIR::BorrowType::Unique, types.primitive(::HIR::CoreType::U8));
            auto placeRaw = builder.newTemporary(placeRawType);

            auto placePanic = builder.newBbUnlinked();
            auto placeOk = builder.newBbUnlinked();
            builder.endBlock(::MIR::Terminator::make_Call({placeOk, ::MIR::UnwindAction::make_Cleanup(placePanic), placeRaw.clone(), ::HIR::Path(langExchangeMalloc), makeVec2<::MIR::Param>(::std::move(sizeParam), ::std::move(alignParam))}));
            builder.setCurBlock(placePanic);
            emitUnwind(node.span());
            builder.setCurBlock(placeOk);

            auto placeType = types.pointer(::HIR::BorrowType::Unique, dataTy);
            auto place = builder.newTemporary(placeType);
            builder.pushStmtAssign(node.span(), place.clone(), ::MIR::RValue::make_Cast({mv$(placeRaw), placeType}));
            // 3. Do a non-dropping write into the target location (i.e. just a MIR assignment)
            builder.pushStmtAssign(node.span(), ::MIR::LValue::newDeref(place.clone()), mv$(val), /*drop_destination=*/false);
            // 4. Convert the pointer into an `owned_box`
            const auto& res_type = node.resType;
            auto res = builder.newTemporary(res_type);
            auto castPanic = builder.newBbUnlinked();
            auto castOk = builder.newBbUnlinked();
            ::HIR::PathParams transmuteParams;
            transmuteParams.types.push_back(res_type);
            transmuteParams.types.push_back(placeType);
            builder.endBlock(::MIR::Terminator::make_Call({castOk, ::MIR::UnwindAction::make_Cleanup(castPanic), res.clone(), ::MIR::CallTarget::make_Intrinsic({"transmute", mv$(transmuteParams)}), makeVec1(::MIR::Param(mv$(place)))}));
            builder.setCurBlock(castPanic);
            emitUnwind(node.span());
            builder.setCurBlock(castOk);

            builder.setResult(node.span(), mv$(res));
        }

        void visit(::HIR::ExprNodeTupleVariant& node) override {
            const Span& sp = node.span();
            TRACE_FUNCTION_F("_TupleVariant");
            ::std::vector<::MIR::Param> values;
            values.reserve(node.mArgs.size());
            for (auto& arg : node.mArgs) {
                this->visitNodePtr(arg);
                if (!builder.block_active()) {
                    return;
                }
                values.push_back(builder.getResultInParam(arg->span(), arg->resType));
            }

            if (node.isStruct) {
                builder.setResult(node.span(), ::MIR::RValue::make_Struct({node.mPath.clone(), mv$(values)}));
            } else {
                // Get the variant index from the enum.
                auto enumPath = node.mPath.clone();
                const auto varName = enumPath.mPath.popComponent();
                const auto& enm = builder.crate().getEnumByPath(sp, enumPath.mPath);

                size_t idx = enm.findVariant(varName);
                ASSERT_BUG(sp, idx != SIZE_MAX, "Variant " << node.mPath.mPath << " isn't present");

                // TODO: Validation?
                ASSERT_BUG(sp, enm.mData.is_Data(), "TupleVariant on non-data enum - " << node.mPath.mPath);


                builder.setResult(node.span(), ::MIR::RValue::make_EnumVariant({mv$(enumPath), static_cast<unsigned>(idx), mv$(values)}));
            }
        }

        ::std::vector<::MIR::Param> getArgs(/*const*/ ::std::vector<::HIR::ExprNodeP>& args) {
            ::std::vector<::MIR::Param> values;
            values.reserve(args.size());
            for (auto& arg : args) {
                this->visitNodePtr(arg);
                if (!builder.block_active()) {
                    return {};
                } else if (args.size() == 1) {
                    values.push_back(builder.getResultInParam(arg->span(), arg->resType, /*allow_missing_value=*/true));
                } else {
                    auto res = builder.getResult(arg->span());
                    if (auto* e = res.opt_Constant()) {
                        values.push_back(mv$(*e));
                    } else {
                        // NOTE: Have to allocate a new temporary because ordering matters
                        auto tmp = builder.newTemporary(arg->resType);
                        builder.pushStmtAssign(arg->span(), tmp.clone(), mv$(res));
                        values.push_back(mv$(tmp));
                    }
                }
            }
            // Keep already evaluated arguments live while evaluating the remaining arguments.
            // A later argument can yield, so consuming an earlier temporary here would prevent
            // the coroutine lowering from saving a value that the eventual call still needs.
            for (size_t i = 0; i < values.size(); i++) {
                if (const auto* e = values[i].opt_LValue()) {
                    builder.movedLvalue(args[i]->span(), *e);
                }
            }
            return values;
        }

        void visit(::HIR::ExprNodeCallPath& node) override {
            TRACE_FUNCTION_F("_CallPath " << node.mPath);
            // TODO: if this is a `<foo as Index[Mut]>::index[_mut]` call then allow the borrow raise to go through to the receiver
            ::std::vector<MIR::Param> values;
            bool isOperator = false;
            if (const auto* pe = node.mPath.mData.opt_UfcsKnown()) {
                if (pe->trait.mPath == builder.resolve().crate.getLangItemPathOpt("index")) {
                    isOperator = true;
                } else if (pe->trait.mPath == builder.resolve().crate.getLangItemPathOpt("index_mut")) {
                    isOperator = true;
                } else if (pe->trait.mPath == builder.resolve().crate.getLangItemPathOpt("deref")) {
                    isOperator = true;
                } else if (pe->trait.mPath == builder.resolve().crate.getLangItemPathOpt("deref_mut")) {
                    isOperator = true;
                }
            }
            if (isOperator) {
                values = getArgs(node.mArgs);
            } else {
                auto _ = saveAndEdit(borrowRaiseTarget, nullptr);
                values = getArgs(node.mArgs);
            }
            if (!builder.block_active()) {
                return;
            }

            auto panicBlock = builder.newBbUnlinked();
            auto nextBlock = builder.newBbUnlinked();
            auto res = builder.newTemporary(node.resType);

            bool unconditionalDiverge = false;

            // Emit intrinsics as a special call type
            if (node.mPath.mData.is_Generic()) {
                const auto& gpath = node.mPath.mData.as_Generic();
                const auto& fcn = builder.crate().getFunctionByPath(node.span(), gpath.mPath);
                if (gpath.mPath.crate_name() == "#intrinsics") {
                    const auto& name = gpath.mPath.components().back();
                    if (name == "offset_of") {
                        builder.endBlock(::MIR::Terminator::make_Call({nextBlock, ::MIR::UnwindAction::make_Cleanup(panicBlock), res.clone(), ::MIR::CallTarget::make_Intrinsic({name, gpath.mParams.clone()}), mv$(values)}));
                    } else {
                        ERROR(node.span(), E0000, "Unknown builtin - " << gpath.mPath);
                    }
                } else if (fcn.mAbi == "rust-intrinsic") {
                    auto name = gpath.mPath.components().back();
                    if (name == "ptr_metadata") {
                        auto& v = values.front();
                        builder.pushStmtAssign(node.span(), res.clone(), ::MIR::RValue::make_DstMeta({std::move(v.as_LValue())}));
                        builder.setResult(node.span(), std::move(res));
                        return;
                    }
                    // aggregate_raw_ptr: Lowers to mrustc's MakeDst (rustc's `Aggregate` with `AggregateKind::RawPtr`)
                    if (name == "aggregate_raw_ptr") {
                        auto& vPtr = values.at(0);
                        auto& vMeta = values.at(1);
                        builder.pushStmtAssign(node.span(), res.clone(), ::MIR::RValue::make_MakeDst({std::move(vPtr), std::move(vMeta)}));
                        builder.setResult(node.span(), std::move(res));
                        return;
                    }
                    if (name == "ub_checks") {
                        builder.setResult(node.span(), ::MIR::Constant::make_Bool({true}));
                        return;
                    }
                    // `slice_get_unchecked`: Acts like `&mut foo[idx]`, but handles all inner types
                    if (name == "slice_get_unchecked") {
                        ::MIR::LValue slot;
                        TU_MATCH_HDRA((values[0]), {)
                        TU_ARMA(LValue, lv) {
                                slot = ::MIR::LValue::newDeref(std::move(lv));
                            }
                            TU_ARMA(Constant, c) TODO(node.span(), "");
                            TU_ARMA(Borrow, v) {
                                slot = std::move(v.val);
                            }
                        }
                        ::MIR::LValue   indexLv = builder.newTemporary(builder.resolve().crate.types.primitive(HIR::CoreType::Usize));
                        TU_MATCH_HDRA((values[1]), {)
                        TU_ARMA(LValue, lv) {
                                builder.pushStmtAssign(node.span(), indexLv.clone(), std::move(lv));
                            }
                            TU_ARMA(Constant, c) {
                                builder.pushStmtAssign(node.span(), indexLv.clone(), std::move(c));
                            }
                            TU_ARMA(Borrow, v)
                            TODO(node.span(), "Borrow index?");
                        }
                        const auto& ptrTy = gpath.mParams.types.at(0);
                        ASSERT_BUG(node.span(), ptrTy->is_Borrow() || ptrTy->is_Pointer(), "" << ptrTy);
                        bool isRaw = ptrTy->is_Pointer();
                        auto borrowTy = isRaw ? ptrTy->as_Pointer().type : ptrTy->as_Borrow().type;
                        builder.pushStmtAssign(node.span(), res.clone(), ::MIR::RValue::make_Borrow({
                            borrowTy,
                            isRaw,
                            ::MIR::LValue::newIndex(std::move(slot), std::move(indexLv.as_Local()))
                            }));
                        builder.setResult(node.span(), std::move(res));
                        return ;
                    }

                    // Floating point operations that can be algebraically optimised
                    // Lazy: Just conver to base operations
                    if (name == "fadd_algebraic") {
                        builder.setResult(node.span(), ::MIR::RValue::make_BinOp({std::move(values[0]), ::MIR::eBinOp::ADD, std::move(values[1])}));
                        return;
                    }
                    if (name == "fsub_algebraic") {
                        builder.setResult(node.span(), ::MIR::RValue::make_BinOp({std::move(values[0]), ::MIR::eBinOp::SUB, std::move(values[1])}));
                        return;
                    }
                    if (name == "fmul_algebraic") {
                        builder.setResult(node.span(), ::MIR::RValue::make_BinOp({std::move(values[0]), ::MIR::eBinOp::MUL, std::move(values[1])}));
                        return;
                    }
                    if (name == "fdiv_algebraic") {
                        builder.setResult(node.span(), ::MIR::RValue::make_BinOp({std::move(values[0]), ::MIR::eBinOp::DIV, std::move(values[1])}));
                        return;
                    }
                    if (name == "frem_algebraic") {
                        builder.setResult(node.span(), ::MIR::RValue::make_BinOp({std::move(values[0]), ::MIR::eBinOp::MOD, std::move(values[1])}));
                        return;
                    }
                    if (name == "box_new") {
                        // Call "exchange_malloc" and move the argument into that returned pointer (same as 1.29 emplace)
                        const auto& dataTy = gpath.mParams.types.at(0);
                        ::MIR::RValue val;
                        TU_MATCH_HDRA((values[0]), {)
                        TU_ARMA(LValue, lv) {
                                val = std::move(lv);
                            }
                            TU_ARMA(Constant, c) {
                                val = std::move(c);
                            }
                            TU_ARMA(Borrow, v)
                            TODO(node.span(), "box_new with a borrow input?");
                        }
                        boxNew(node, dataTy, std::move(val));
                        return ;
                    }
                    builder.endBlock(::MIR::Terminator::make_Call({nextBlock, ::MIR::UnwindAction::make_Cleanup(panicBlock), res.clone(), ::MIR::CallTarget::make_Intrinsic({name, gpath.mParams.clone()}), mv$(values)}));
                } else if (fcn.mAbi == "platform-intrinsic") {
                    builder.endBlock(::MIR::Terminator::make_Call({nextBlock, ::MIR::UnwindAction::make_Cleanup(panicBlock), res.clone(), ::MIR::CallTarget::make_Intrinsic({RcString(FMT("platform:" << gpath.mPath.components().back())), gpath.mParams.clone()}), mv$(values)}));
                }

                // rustc has drop_in_place as a lang item, mrustc uses an intrinsic
                if (gpath.mPath == builder.crate().getLangItemPathOpt("drop_in_place")) {
                    builder.endBlock(::MIR::Terminator::make_Call({nextBlock, ::MIR::UnwindAction::make_Cleanup(panicBlock), res.clone(), ::MIR::CallTarget::make_Intrinsic({"drop_in_place", gpath.mParams.clone()}), mv$(values)}));
                }

                if (fcn.returnType->is_Diverge()) {
                    unconditionalDiverge = true;
                }
            } else {
                // TODO: Know if the call unconditionally diverges.
                if (node.cache.argTypes.back()->is_Diverge()) {
                    unconditionalDiverge = true;
                }
            }

            // If the call wasn't to an intrinsic, emit it as a path
            if (builder.block_active()) {
                builder.endBlock(::MIR::Terminator::make_Call({nextBlock, ::MIR::UnwindAction::make_Cleanup(panicBlock), res.clone(), node.mPath.clone(), mv$(values)}));
            }

            builder.setCurBlock(panicBlock);
            emitUnwind(node.span());

            builder.setCurBlock(nextBlock);

            // If the function doesn't return, early-terminate the return block.
            if (unconditionalDiverge) {
                builder.endBlock(::MIR::Terminator::make_Unreachable({}));
                builder.setCurBlock(builder.newBbUnlinked());
            } else {
                // NOTE: This has to be done here because the builder can't easily do it.
                builder.markValueAssigned(node.span(), res);
            }
            builder.setResult(node.span(), mv$(res));
        }

        void visit(::HIR::ExprNodeCallValue& node) override {
            TRACE_FUNCTION_F("_CallValue " << node.mValue->resType);
            auto _ = saveAndEdit(borrowRaiseTarget, nullptr);

            // _CallValue is ONLY valid on function pointers (all others must be desugared)
            ASSERT_BUG(node.span(), node.mValue->resType->is_Function(), "Leftover _CallValue on a non-fn()");
            this->visitNodePtr(node.mValue);
            if (!builder.block_active()) {
                return;
            }

            // Get the function pointer in a temporary BEFORE getting arguments
            auto fcnVal = builder.newTemporary(node.mValue->resType);
            builder.pushStmtAssign(node.mValue->span(), fcnVal.clone(), builder.getResult(node.mValue->span()));

            auto values = getArgs(node.mArgs);
            if (!builder.block_active()) {
                return;
            }

            auto panicBlock = builder.newBbUnlinked();
            auto nextBlock = builder.newBbUnlinked();
            auto res = builder.newTemporary(node.resType);
            builder.endBlock(::MIR::Terminator::make_Call({nextBlock, ::MIR::UnwindAction::make_Cleanup(panicBlock), res.clone(), mv$(fcnVal), mv$(values)}));

            builder.setCurBlock(panicBlock);
            emitUnwind(node.span());

            builder.setCurBlock(nextBlock);
            // TODO: Support diverging value calls
            builder.markValueAssigned(node.span(), res);
            builder.setResult(node.span(), mv$(res));
        }

        void visit(::HIR::ExprNodeCallMethod& node) override {
            // TODO: Allow use on trait objects? May not be needed, depends.
            BUG(node.span(), "Leftover _CallMethod");
        }

        void visit(::HIR::ExprNodeField& node) override {
            TRACE_FUNCTION_F("_Field \"" << node.field << "\"");
            this->visitNodePtr(node.mValue);
            auto val = builder.getResultInLvalue(node.mValue->span(), node.mValue->resType);

            const auto& valTy = node.mValue->resType;

            unsigned int idx;
            if (::std::isdigit(node.field.c_str()[0])) {
                ::std::stringstream(node.field.c_str()) >> idx;
                builder.setResult(node.span(), ::MIR::LValue::newField(mv$(val), idx));
            } else if (const auto* bep = valTy->as_Path().binding.opt_Struct()) {
                const auto& str = **bep;
                const auto& fields = str.mData.as_Named();
                idx = ::std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                    return x.name == node.field;
                }) - fields.begin();
                builder.setResult(node.span(), ::MIR::LValue::newField(mv$(val), idx));
            } else if (const auto* bep = valTy->as_Path().binding.opt_Union()) {
                const auto& unm = **bep;
                const auto& fields = unm.mVariants;
                idx = ::std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                    return x.name == node.field;
                }) - fields.begin();

                builder.setResult(node.span(), ::MIR::LValue::newDowncast(mv$(val), idx));
            } else {
                BUG(node.span(), "Field access on non-union/struct - " << valTy);
            }
        }

        void visit(::HIR::ExprNodeLiteral& node) override {
            TRACE_FUNCTION_F("_Literal");
            TU_MATCH_HDRA( (node.mData), {)
            TU_ARMA(Integer, e) {
                    ASSERT_BUG(node.span(), node.resType->is_Primitive(), "Non-primitive return type for Integer literal - " << node.resType);
                    auto ity = node.resType->as_Primitive();
                    switch (ity) {
                        case ::HIR::CoreType::U8:
                        case ::HIR::CoreType::U16:
                        case ::HIR::CoreType::U32:
                        case ::HIR::CoreType::U64:
                        case ::HIR::CoreType::U128:
                        case ::HIR::CoreType::Usize:
                            builder.setResult(node.span(), ::MIR::Constant::make_Uint({e.mValue, ity}));
                            break;
                        case ::HIR::CoreType::Char:
                            builder.setResult(node.span(), ::MIR::Constant::make_Uint({e.mValue, ity}));
                            break;
                        case ::HIR::CoreType::I8:
                        case ::HIR::CoreType::I16:
                        case ::HIR::CoreType::I32:
                        case ::HIR::CoreType::I64:
                        case ::HIR::CoreType::I128:
                        case ::HIR::CoreType::Isize:
                            builder.setResult(node.span(), ::MIR::Constant::make_Int({S128(e.mValue), ity}));
                            break;
                        default:
                            BUG(node.span(), "Integer literal with unexpected type - " << node.resType);
                    }
                }
                TU_ARMA(Float, e) {
                    ASSERT_BUG(node.span(), node.resType->is_Primitive(), "Non-primitive return type for Float literal - " << node.resType);
                    auto ity = node.resType->as_Primitive();
                    builder.setResult(node.span(), ::MIR::RValue::make_Constant(::MIR::Constant::make_Float({e.mValue, ity})));
                }
                TU_ARMA(Boolean, e) {
                    builder.setResult(node.span(), ::MIR::RValue::make_Constant(::MIR::Constant::make_Bool({e})));
                }
                TU_ARMA(String, e) {
                    builder.setResult(node.span(), ::MIR::RValue::make_Constant(::MIR::Constant(e)));
                }
                TU_ARMA(CString, e) {
                    auto s = e.v;
                    s.push_back('\0');

                    // Emit as `transmute<&Cstr,&str>`
                    auto res = builder.newTemporary(node.resType);

                    auto castPanic = builder.newBbUnlinked();
                    auto castOk = builder.newBbUnlinked();
                    ::HIR::PathParams transmuteParams;
                    transmuteParams.types.push_back(node.resType);
                    transmuteParams.types.push_back(builder.resolve().crate.types.borrow(::HIR::BorrowType::Shared, builder.resolve().crate.types.primitive(::HIR::CoreType::Str)));
                    builder.endBlock(::MIR::Terminator::make_Call({castOk, ::MIR::UnwindAction::make_Cleanup(castPanic), res.clone(), ::MIR::CallTarget::make_Intrinsic({"transmute", mv$(transmuteParams)}), makeVec1(::MIR::Param(::MIR::Constant(std::move(s))))}));
                    builder.setCurBlock(castPanic);
                    emitUnwind(node.span());
                    builder.setCurBlock(castOk);

                    builder.setResult(node.span(), mv$(res));
                }
                TU_ARMA(ByteString, e) {
                    auto v = mv$(*reinterpret_cast<::std::vector<uint8_t>*>(&e));
                    builder.setResult(node.span(), ::MIR::RValue::make_Constant(::MIR::Constant(mv$(v))));
                }
            }
        }

        void visit(::HIR::ExprNodeUnitVariant& node) override {
            const Span& sp = node.span();
            TRACE_FUNCTION_F("_UnitVariant");
            if (!node.isStruct) {
                // Get the variant index from the enum.
                auto enumPath = node.mPath.clone();
                auto varName = enumPath.mPath.popComponent();

                const auto& enm = builder.crate().getEnumByPath(sp, enumPath.mPath);

                auto idx = enm.findVariant(varName);
                ASSERT_BUG(sp, idx != SIZE_MAX, "Variant " << node.mPath.mPath << " isn't present");

                // VALIDATION
                if (const auto* e = enm.mData.opt_Data()) {
                    const auto& var = (*e)[idx];
                    ASSERT_BUG(sp, !var.is_struct, "Variant " << node.mPath.mPath << " isn't a unit variant");
                }

                builder.setResult(node.span(), ::MIR::RValue::make_EnumVariant({mv$(enumPath), static_cast<unsigned>(idx), {}}));
            } else {
                builder.setResult(node.span(), ::MIR::RValue::make_Struct({node.mPath.clone(), {}}));
            }
        }

        void visit(::HIR::ExprNodePathValue& node) override {
            const auto& sp = node.span();
            TRACE_FUNCTION_F("_PathValue - " << node.mPath);
            if (node.resType->is_NamedFunction() && node.target != ::HIR::ExprNodePathValue::STATIC && node.target != ::HIR::ExprNodePathValue::CONSTANT) {
                auto tmp = builder.newTemporary(node.resType);
                builder.pushStmtAssign(sp, tmp.clone(), ::MIR::Constant::make_Function({box$(node.mPath.clone())}));
                //m_builder.push_stmt_assign( sp, tmp.clone(), ::MIR::Constant::make_ItemAddr({ box$(node.m_path.clone()) }) );
                builder.setResult(sp, mv$(tmp));
                return;
            }
            TU_MATCH_HDRA( (node.mPath.mData), { )
            TU_ARMA(Generic, pe) {
                    // Enum variant constructor.
                    if (node.target == ::HIR::ExprNodePathValue::ENUM_VAR_CONSTR) {
                        BUG(node.span(), "Should have produced a NamedFunction type and have been handled above");
                    }
                    const auto& vi = builder.crate().getValitemByPath(node.span(), pe.mPath);
                TU_MATCH_HDRA( (vi), {)
                TU_ARMA(Import, e) {
                            BUG(sp, "All references via imports should be replaced");
                        }
                        TU_ARMA(Constant, e) {
                            auto ty = MonomorphStatePtr(builder.resolve().crate.types, nullptr, nullptr, &pe.mParams).monomorphType(sp, e.mType);
                            auto tmp = builder.newTemporary(ty);
                            builder.pushStmtAssign(sp, tmp.clone(), ::MIR::Constant::make_Const({box$(node.mPath.clone())}));
                            builder.setResult(node.span(), mv$(tmp));
                        }
                        TU_ARMA(Static, e) {
                            builder.setResult(node.span(), ::MIR::LValue::newStatic(node.mPath.clone()));
                        }
                        TU_ARMA(StructConstant, e) {
                            // TODO: Why is this still a PathValue?
                            builder.setResult(node.span(), ::MIR::RValue::make_Struct({pe.clone(), {}}));
                        }
                        TU_ARMA(Function, e) {
                            BUG(node.span(), "Should have produced a NamedFunction type and have been handled above");
                        }
                        TU_ARMA(StructConstructor, e) {
                            BUG(node.span(), "Should have produced a NamedFunction type and have been handled above");
                        }
                }
                }
                TU_ARMA(UfcsKnown, pe) {
                    // Check what item type this is (from the trait)
                    const auto& tr = builder.crate().getTraitByPath(sp, pe.trait.mPath);
                    auto it = tr.values.find(pe.item);
                    ASSERT_BUG(sp, it != tr.values.end(), "Cannot find trait item for " << node.mPath);
                    TU_MATCHA((it->second), (e), (Constant, builder.setResult(sp, ::MIR::Constant::make_Const({box$(node.mPath.clone())}));), (Static, TODO(sp, "Associated statics (non-rustc) - " << node.mPath);), (Function, BUG(node.span(), "Should have produced a NamedFunction type and have been handled above");))
                }
                TU_ARMA(UfcsUnknown, pe) {
                    BUG(sp, "PathValue - Encountered UfcsUnknown - " << node.mPath);
                }
                TU_ARMA(UfcsInherent, pe) {
                    // 1. Find item in an impl block
                    auto rv = builder.crate().findTypeImpls(pe.type, HIR::ResolvePlaceholdersNop(), [&](const auto& impl) {
                        DEBUG("- impl" << impl.mParams.fmtArgs() << " " << impl.mType);
                        // Associated functions
                        {
                            auto it = impl.methods.find(pe.item);
                            if (it != impl.methods.end()) {
                                //BUG(node.span(), "Should have produced a NamedFunction type and have been handled above: ");
                                builder.setResult(sp, ::MIR::Constant::make_ItemAddr({box$(node.mPath.clone())}));
                                return true;
                            }
                        }
                        // Associated consts
                        {
                            auto it = impl.constants.find(pe.item);
                            if (it != impl.constants.end()) {
                                builder.setResult(sp, ::MIR::Constant::make_Const({box$(node.mPath.clone())}));
                                return true;
                            }
                        }
                        // Associated static (undef)
                        return false;
                    });
                    if (!rv) {
                        ERROR(sp, E0000, "Failed to locate item for " << node.mPath);
                    }
                }
            }
        }

        void visit(::HIR::ExprNodeVariable& node) override {
            TRACE_FUNCTION_F("_Variable - " << node.mName << " #" << node.slot);
#if 1
            // If there's an alias active, emit that
            if (const auto* a = builder.getVariableAlias(node.span(), node.slot)) {
                switch (a->first) {
                    case ::HIR::PatternBinding::Type::Move:
                        builder.setResult(node.span(), a->second.clone());
                        break;
                    case ::HIR::PatternBinding::Type::Ref:
                        builder.setResult(node.span(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, a->second.clone()}));
                        break;
                    case ::HIR::PatternBinding::Type::MutRef:
                        builder.setResult(node.span(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Unique, false, a->second.clone()}));
                        break;
                }
                return;
            }
#endif
            builder.setResult(node.span(), builder.getVariable(node.span(), node.slot));
        }

        void visit(::HIR::ExprNodeConstParam& node) override {
            TRACE_FUNCTION_F("_ConstParam - " << node.mName << " #" << node.mBinding);
            builder.setResult(node.span(), ::MIR::Constant::make_Generic({node.mName, node.mBinding}));
        }

        void visitSlInner(::HIR::ExprNodeStructLiteral& node, const ::HIR::Struct& str, const ::HIR::GenericPath& path) {
            const Span& sp = node.span();

            ASSERT_BUG(sp, str.mData.is_Named(), "");
            const ::HIR::tStructFields& fields = str.mData.as_Named();

            ::std::vector<bool> valuesSet;
            ::std::vector<::MIR::Param> values;
            values.resize(fields.size());
            valuesSet.resize(fields.size());

            for (auto& ent : node.values) {
                auto& valnode = ent.second;
                auto idx = ::std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                    return x.name == ent.first;
                }) - fields.begin();
                assert(!valuesSet[idx]);
                valuesSet[idx] = true;
                DEBUG("_StructLiteral - fld '" << ent.first << "' (idx " << idx << ")");
                this->visitNodePtr(valnode);
                if (!builder.block_active()) {
                    return;
                }

                auto res = builder.getResult(valnode->span());
                if (auto* e = res.opt_Constant()) {
                    values.at(idx) = mv$(*e);
                } else {
                    // NOTE: Have to allocate a new temporary because ordering matters
                    auto tmp = builder.newTemporary(valnode->resType);
                    builder.pushStmtAssign(valnode->span(), tmp.clone(), mv$(res));
                    values.at(idx) = mv$(tmp);
                }
            }

            auto baseVal = ::MIR::LValue::newReturn();
            if (node.baseValue) {
                DEBUG("_StructLiteral - base");
                this->visitNodePtr(node.baseValue);
                if (!builder.block_active()) {
                    return;
                }
                baseVal = builder.getResultInLvalue(node.baseValue->span(), node.baseValue->resType);
            }
            for (unsigned int i = 0; i < values.size(); i++) {
                if (!valuesSet[i]) {
                    if (node.baseValue) {
                        values[i] = ::MIR::LValue::newField(baseVal.clone(), i);
                    } else if (fields[i].default_value) {
                        const auto& v = *fields[i].default_value;
                        auto ms = MonomorphStatePtr(builder.resolve().crate.types, nullptr, &path.mParams, nullptr);
                        values[i] = builder.lvalueOrTemp(sp, ms.monomorphType(sp, fields[i].ty), MIR::Constant::make_Const({::std::make_unique<HIR::Path>(ms.monomorphGenericpath(sp, v))}));
                    } else {
                        ERROR(node.span(), E0000, "Field '" << fields[i].name << "' not specified");
                    }
                } else {
                    // Partial move support will handle dropping the rest?
                }
            }

            builder.setResult(node.span(), ::MIR::RValue::make_Struct({path.clone(), mv$(values)}));
        }

        void visit(::HIR::ExprNodeStructLiteral& node) override {
            TRACE_FUNCTION_F("_StructLiteral");

            const auto& tyPath = node.realPath;

            TU_MATCH_HDRA( (node.resType->as_Path().binding), {)
            TU_ARMA(Unbound, _e) {
                }
                TU_ARMA(Opaque, _e) {
                }
                TU_ARMA(Enum, e) {
                    auto enumPath = tyPath.clone();
                    auto varName = enumPath.mPath.popComponent();

                    const auto& enm = *e;
                    size_t idx = enm.findVariant(varName);
                    ASSERT_BUG(node.span(), idx != SIZE_MAX, "");
                    ASSERT_BUG(node.span(), enm.mData.is_Data(), "");
                    const auto& varTy = enm.mData.as_Data()[idx].type;
                    const auto& str = *varTy->as_Path().binding.as_Struct();

                    // Take advantage of the identical generics to cheaply clone/monomorph the path.
                    ::HIR::GenericPath structPath = tyPath.clone();
                    structPath.mPath = varTy->as_Path().path.mData.as_Generic().mPath;

                    this->visitSlInner(node, str, structPath);
                    if (!builder.block_active()) {
                        return;
                    }
                    auto vals = std::move(builder.getResult(node.span()).as_Struct().vals);

                    // And create Variant
                    builder.setResult(node.span(), ::MIR::RValue::make_EnumVariant({mv$(enumPath), static_cast<unsigned>(idx), mv$(vals)}));
                }
                TU_ARMA(Union, e) {
                    const auto& variantName = node.values.front().first;
                    auto& value_node = node.values.front().second;
                    this->visitNodePtr(value_node);
                    if (!builder.block_active()) {
                        return;
                    }
                    auto val = builder.getResultInLvalue(value_node->span(), value_node->resType);

                    const auto& unm = *e;
                    auto it = ::std::find_if(unm.mVariants.begin(), unm.mVariants.end(), [&](const HIR::StructField& v) -> auto {
                        return v.name == variantName;
                    });
                    assert(it != unm.mVariants.end());
                    unsigned int idx = it - unm.mVariants.begin();

                    builder.setResult(node.span(), ::MIR::RValue::make_UnionVariant({node.realPath.clone(), idx, mv$(val)}));
                }
                TU_ARMA(ExternType, e) {
                    BUG(node.span(), "_StructLiteral ExternType isn't valid?");
                }
                TU_ARMA(Struct, e) {
                    if (e->mData.is_Unit()) {
                        builder.setResult(node.span(), ::MIR::RValue::make_Struct({tyPath.clone(), {}}));
                        return;
                    }

                    this->visitSlInner(node, *e, tyPath);
                }
            }
        }

        void visit(::HIR::ExprNodeTuple& node) override {
            TRACE_FUNCTION_F("_Tuple");
            auto values = getArgs(node.vals);
            if (!builder.block_active()) {
                return;
            }

            builder.setResult(node.span(), ::MIR::RValue::make_Tuple({mv$(values)}));
        }

        void visit(::HIR::ExprNodeArrayList& node) override {
            TRACE_FUNCTION_F("_ArrayList");
            auto values = getArgs(node.vals);
            if (!builder.block_active()) {
                return;
            }

            builder.setResult(node.span(), ::MIR::RValue::make_Array({mv$(values)}));
        }

        void visit(::HIR::ExprNodeArraySized& node) override {
            TRACE_FUNCTION_F("_ArraySized");
            this->visitNodePtr(node.val);
            if (!builder.block_active()) {
                return;
            }
            auto value = builder.getResultInParam(node.span(), node.val->resType);

            builder.setResult(node.span(), ::MIR::RValue::make_SizedArray({mv$(value), std::move(node.mSize)}));
            // Ensure that the size is valid (avoids crashes when debug is enabled)
            node.mSize = HIR::ArraySize();
        }

        void visit(::HIR::ExprNodeClosure& node) override {
            TRACE_FUNCTION_F("_Closure - " << node.objPath);
            auto _ = saveAndEdit(borrowRaiseTarget, nullptr);

            ::std::vector<::MIR::Param> vals;
            vals.reserve(node.captures.size());
            for (auto& arg : node.captures) {
                this->visitNodePtr(arg);
                vals.push_back(builder.getResultInLvalue(arg->span(), arg->resType));
            }

            builder.setResult(node.span(), ::MIR::RValue::make_Struct({node.objPath.clone(), mv$(vals)}));
        }

        void visitCommonCr(const Span& sp, const HIR::GenericPath& obj_path, const HIR::TypeData* state_type, ::std::vector<::HIR::ExprNodeP>& captures) {
            auto _ = saveAndEdit(borrowRaiseTarget, nullptr);

            ::std::vector<::MIR::Param> vals;
            vals.reserve(1 + captures.size());

            // Zero the state index
            {
                const auto& langMaybeUninit = builder.resolve().crate.getLangItemPath(sp, "maybe_uninit");
                const auto& unmMaybeUninit = builder.resolve().crate.getUnionByPath(sp, langMaybeUninit);
                auto slotType = builder.resolve().crate.types.path(::HIR::GenericPath(langMaybeUninit, ::HIR::PathParams(state_type)), &unmMaybeUninit);

                auto resSlot = builder.newTemporary(slotType);
                auto sizePanic = builder.newBbUnlinked();
                auto sizeOk = builder.newBbUnlinked();
                builder.endBlock(
                    ::MIR::Terminator::make_Call(
                        {sizeOk,
                         ::MIR::UnwindAction::make_Cleanup(sizePanic),
                         resSlot.clone(),
                         ::MIR::CallTarget::make_Intrinsic({"init", ::HIR::PathParams(mv$(slotType))}), // I.e. `mem::zeroed`
                         {}}
                    )
                );
                builder.setCurBlock(sizePanic);
                emitUnwind(sp);
                builder.setCurBlock(sizeOk);
                vals.push_back(std::move(resSlot));
            }
            // Populate the rest
            for (auto& arg : captures) {
                this->visitNodePtr(arg);
                vals.push_back(builder.getResultInLvalue(arg->span(), arg->resType));
            }

            builder.setResult(sp, ::MIR::RValue::make_Struct({obj_path.clone(), mv$(vals)}));
        }

        void visit(::HIR::ExprNodeGenerator& node) override {
            TRACE_FUNCTION_F("_Generator - " << node.objPath);
            ASSERT_BUG(node.span(), node.objPtr, "Generator not created");
            ASSERT_BUG(node.span(), !node.mCode, "Encountered outer generator wrapper");

            visitCommonCr(node.span(), node.objPath, node.stateDataType, node.captures);
        }

        void visit(::HIR::ExprNodeGeneratorWrapper& node) override {
            BUG(node.span(), "Unexpected");
        }

        void visit(::HIR::ExprNodeAsyncBlock& node) override {
            TRACE_FUNCTION_F("_AsyncBlock - " << node.objPath);
            ASSERT_BUG(node.span(), node.objPtr, "Future not created");
            ASSERT_BUG(node.span(), !node.mCode, "Encountered code inside post-expand async block");

            visitCommonCr(node.span(), node.objPath, node.stateDataType, node.captures);
        }
    };
}

::MIR::FunctionPointer LowerMIR(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, const ::HIR::ExprPtr& ptr, const ::HIR::TypeData* ret_ty, const ::HIR::Function::argsT& args) {
    TRACE_FUNCTION_F(path);

    ::MIR::Function fcn;
    fcn.locals.reserve(ptr.mBindings.size());
    for (const auto& t : ptr.mBindings) {
        fcn.locals.push_back(t);
    }

    // Scope ensures that builder cleanup happens before `fcn` is moved
    {
        const Span& sp = ptr->span();

        ::HIR::ExprNode& rootNode = const_cast<::HIR::ExprNode&>(*ptr);
        MirBuilder builder{ptr->span(), resolve, ret_ty, args, fcn};
        ExprVisitorConv ev{builder, ptr.mBindings, cast<::HIR::ExprNodeGeneratorWrapper>(&rootNode)};

        // 1. Apply destructuring to arguments
        unsigned int i = 0;
        for (const auto& arg : args) {
            const auto& pat = arg.first;
            builder.scheduleArgumentDrop(i);
            // If the binding is set (i.e. this isn't destructuring) then the table populated by `MirBuilder::MirBuilder(...)` will be used
            if (pat.mBindings.size() == 1 && pat.mBindings[0].mType == ::HIR::PatternBinding::Type::Move && pat.mData.is_Any()) {
                // Simple `var: Type` arguments are handled by `MirBuilder.m_var_arg_mappings`
            } else {
                DEBUG("Argument a" << i << " - " << pat);
                ev.schedulePatternDrops(ptr->span(), arg.first, PatternDropOrder::FirstCandidate);
                MIRLowerHIRLet(builder, ev, ptr->span(), arg.first, ::MIR::LValue::newArgument(i), /*else_node=*/nullptr);
            }
            i++;
        }

        // 2. Destructure code
        if (auto* genNode = cast<::HIR::ExprNodeGeneratorWrapper>(&rootNode)) {
            // Mark all capture locals as valid (for later rewrite into variable acesses)
            ::std::map<unsigned, std::vector<MIR::LValue::Wrapper>> mappings;
            for (size_t i = 0; i < genNode->captureUsages.size(); i++) {
                unsigned idx = args.size() + i;
                builder.scheduleVariableDrop(idx);
                switch (genNode->captureUsages[i]) {
                    case ::HIR::ValueUsage::Borrow:
                    case ::HIR::ValueUsage::Mutate: {
                        // TODO: Use `m_variable_aliases` for by-borrow captures, to avoid them being dropped
                        auto lv = ::MIR::LValue::newArgument(0);
                        lv.wrappers.push_back(::MIR::LValue::Wrapper::newField(0)); // Pin.ptr
                        lv.wrappers.push_back(::MIR::LValue::Wrapper::newDeref());  // *
                        lv.wrappers.push_back(::MIR::LValue::Wrapper::newField(1 + i));
                        lv.wrappers.push_back(::MIR::LValue::Wrapper::newDeref());
                        builder.addVariableAlias(rootNode.span(), idx, ::HIR::PatternBinding::Type::Move, std::move(lv));
                    } break;
                    case ::HIR::ValueUsage::Move:
                    case ::HIR::ValueUsage::Unknown:
                        builder.markValueAssigned(rootNode.span(), ::MIR::LValue::newLocal(idx));
                        mappings.insert(std::make_pair(idx, ::makeVec1(::MIR::LValue::Wrapper::newField(1 + i))));
                        break;
                }
            }

            // ------------

            genNode->mCode->visit(ev);
            if (builder.block_active() && builder.hasResult()) {
                ev.coroutineReturn(sp, genNode->mCode->resType);
            }
            builder.finalCleanup();

            // ------------

            // 1. Generate the state machine switch (and enumerate saved variables)
            std::set<unsigned> saved = ev.generatorFinalise(genNode->span(), const_cast<HIR::Enum&>(resolve.crate.getEnumByPath(sp, genNode->stateIdxEnum)));
            // 2. Populate state structure
            auto& stateTy = const_cast<HIR::Struct&>(*genNode->stateDataType->as_Path().binding.as_Struct());
            unsigned valueVarIdx;
            {
                const auto& unmMaybeUninit = resolve.crate.getUnionByPath(sp, resolve.crate.getLangItemPath(genNode->span(), "maybe_uninit"));
                valueVarIdx = std::find_if(unmMaybeUninit.mVariants.begin(), unmMaybeUninit.mVariants.end(), [&](const auto& e) {
                    return e.name == "value";
                }) - unmMaybeUninit.mVariants.begin();
            }
            ASSERT_BUG(sp, valueVarIdx == 1, "Assumption on MaybeUninit.value's variant index failed");
            // - Any variables that are saved twice need to have a static address, others can share?
            // - Lazy option (doesn't require making sub-types): Toss everything together
            auto& fields = stateTy.mData.as_Tuple();
            for (auto idx : saved) {
                if (idx < 1 + genNode->captureUsages.size()) {
                } else {
                    auto fieldIdx = fields.size();
                    ASSERT_BUG(sp, idx < fcn.locals.size(), idx << " >= " << fcn.locals.size());
                    fields.push_back(::HIR::VisEnt<HIR::TypeRef>{HIR::Publicity::newNone(), fcn.locals.at(idx)});
                    // self.state(0).value(?#1).value(?0).IDX
                    mappings.insert(
                        std::make_pair(
                            idx,
                            std::vector<MIR::LValue::Wrapper>{
                                ::MIR::LValue::Wrapper::newField(0),
                                ::MIR::LValue::Wrapper::newDowncast(valueVarIdx), // MaybeUninit.value
                                ::MIR::LValue::Wrapper::newField(0),                // ManuallyDrop.value
                                ::MIR::LValue::Wrapper::newField(fieldIdx)
                            }
                        )
                    );
                }
            }
            for (const auto& m : mappings) {
                DEBUG("Mapping _" << m.first << " = " << m.second);
            }
            ::std::map<unsigned, unsigned> drop_flag_mapping;
            for (auto idx : ev.generatorDropFlags()) {
                drop_flag_mapping[idx] = drop_flag_mapping.size();
                DEBUG("df$" << idx << " = BIT" << drop_flag_mapping[idx]);
            }
            // Add drop flags to the end
            auto dropFlagsFieldIdx = fields.size();
            fields.push_back(::HIR::VisEnt<HIR::TypeRef>{HIR::Publicity::newNone(), resolve.crate.types.array(resolve.crate.types.primitive(::HIR::CoreType::U8), (drop_flag_mapping.size() + 7) / 8)});

            // 3. Rewrite usage of saved values
            // - Note: Need to allocate new temporaries if indexing by an updated lvalue
            class Rewriter: public ::MIR::visit::VisitorMut {
                /// Remapped locals (indexes into coroutine struct, not just into the state)
                ///
                /// From `Pin<&mut self>`, these are appended to `self.pin.*`
                const ::std::map<unsigned, std::vector<MIR::LValue::Wrapper>>& mMappings;
                /// Mapping from drop flag indexes to bit sin the drop flag list
                const ::std::map<unsigned, unsigned>& dropFlagMapping;
                /// Index of the drop flags bitset (array of u8) in the state (field 0 of top structure)
                unsigned dropFlagsField;

                ::std::vector<::MIR::Statement> newStatements;
                unsigned bbIdx = 0;
                unsigned stmtIdx = 0;

            public:
                Rewriter(const ::std::map<unsigned, std::vector<MIR::LValue::Wrapper>>& mappings, const ::std::map<unsigned, unsigned>& drop_flag_mapping, unsigned drop_flags_field)
                    : mMappings(mappings)
                    , dropFlagMapping(drop_flag_mapping)
                    , dropFlagsField(drop_flags_field)
                {
                }

                bool visitLvalue(::MIR::LValue& lv, ::MIR::visit::ValUsage u) override {
                    if (lv.root.is_Local()) {
                        auto it = mMappings.find(lv.root.as_Local());
                        if (it != mMappings.end()) {
                            lv.root = ::MIR::LValue::Storage::newArgument(0);
                            auto dit = lv.wrappers.begin();
                            dit = lv.wrappers.insert(dit, ::MIR::LValue::Wrapper::newField(0)) + 1; // Pin.ptr
                            dit = lv.wrappers.insert(dit, ::MIR::LValue::Wrapper::newDeref()) + 1;  // *
                            dit = lv.wrappers.insert(dit, it->second.begin(), it->second.end()) + 1;
                            DEBUG("BB" << bbIdx << "/" << FMT_CB(os, if (stmtIdx == ~0u) { os << "TERM"; } else { os << stmtIdx; }) << " > " << lv);
                        }
                    }
                    for (auto& w : lv.wrappers) {
                        if (w.is_Index()) {
                            auto it = mMappings.find(w.as_Index());
                            if (it != mMappings.end()) {
                                // Allocate a new temporary, assign it before this statement, use that
                                TODO(Span(), "");
                            }
                        }
                    }

                    return true;
                }

                bool visitStmt(::MIR::Statement& stmt) override {
                    auto getDropFlagsSlot = [this]() -> MIR::LValue {
                        ::MIR::LValue slot = ::MIR::LValue::newArgument(0);
                        slot.wrappers.push_back(::MIR::LValue::Wrapper::newField(0));                  // Pin.ptr
                        slot.wrappers.push_back(::MIR::LValue::Wrapper::newDeref());                   // *
                        slot.wrappers.push_back(::MIR::LValue::Wrapper::newField(0));                  // .0
                        slot.wrappers.push_back(::MIR::LValue::Wrapper::newDowncast(1));               // .value (From MaybeUninit)
                        slot.wrappers.push_back(::MIR::LValue::Wrapper::newField(0));                  // .value (From ManuallyDrop)
                        slot.wrappers.push_back(::MIR::LValue::Wrapper::newField(dropFlagsField)); // .drop_flags
                        return slot;
                    };
                    if (auto* s = stmt.opt_SetDropFlag()) {
                        if (dropFlagMapping.count(s->other) != 0) {
                            auto slot = getDropFlagsSlot();
                            unsigned bitNum = dropFlagMapping.at(s->other);
                            // `LoadDropFlag(df$N, src_lv, bit_num)`, where `src_lv` is an array of `u8`
                            newStatements.push_back(
                                ::MIR::Statement::make_LoadDropFlag({
                                    s->other,
                                    std::move(slot),
                                    bitNum,
                                })
                            );
                        }
                        if (dropFlagMapping.count(s->idx) != 0) {
                            // Copy this statement to the output queue, and then rewrite to be:
                            newStatements.push_back(*s);
                            // `SaveDropFlag(dst_lv, bit_num, df$N)`
                            auto slot = getDropFlagsSlot();
                            unsigned bitNum = dropFlagMapping.at(s->idx);
                            stmt = ::MIR::Statement::make_SaveDropFlag({std::move(slot), bitNum, s->idx});
                            // TODO: Replace with no-op? (or let it be cleaned up later as dead code)
                        }
                    } else {
                        // Doesn't use drop flags, no changes/rewrites needed
                    }
                    return ::MIR::visit::VisitorMut::visitStmt(stmt);
                }

                void pushStatements(::MIR::BasicBlock& bb, size_t& ofs) {
                    for (auto& e : newStatements) {
                        bb.statements.insert(bb.statements.begin() + ofs, std::move(e));
                        ofs += 1;
                    }
                    newStatements.clear();
                }

                void rewriteFcn(::MIR::Function& f) {
                    for (auto& bb : f.blocks) {
                        this->bbIdx = &bb - f.blocks.data();
                        for (size_t stmtIdx = 0; stmtIdx < bb.statements.size(); stmtIdx++) {
                            this->stmtIdx = stmtIdx;
                            this->visitStmt(bb.statements[stmtIdx]);
                            this->pushStatements(bb, stmtIdx);
                        }
                        this->stmtIdx = ~0u;
                        if (auto* s = bb.terminator.opt_Drop()) {
                            if (dropFlagMapping.count(s->flagIdx) != 0) {
                                auto slot = ::MIR::LValue::newArgument(0);
                                slot.wrappers.push_back(::MIR::LValue::Wrapper::newField(0));
                                slot.wrappers.push_back(::MIR::LValue::Wrapper::newDeref());
                                slot.wrappers.push_back(::MIR::LValue::Wrapper::newField(0));
                                slot.wrappers.push_back(::MIR::LValue::Wrapper::newDowncast(1));
                                slot.wrappers.push_back(::MIR::LValue::Wrapper::newField(0));
                                slot.wrappers.push_back(::MIR::LValue::Wrapper::newField(dropFlagsField));
                                newStatements.push_back(::MIR::Statement::make_LoadDropFlag({
                                    s->flagIdx,
                                    std::move(slot),
                                    dropFlagMapping.at(s->flagIdx),
                                }));
                            }
                        }
                        this->visitTerminator(bb.terminator);
                        size_t stmtIdx = bb.statements.size();
                        this->pushStatements(bb, stmtIdx);
                    }
                }
            };

            Rewriter(mappings, drop_flag_mapping, dropFlagsFieldIdx).rewriteFcn(fcn);

            // 4. Generate drop glue for the generator type and save for later
            // - Make a builder
            // - Insert the switch for each arm
            // - Trigger drops
            auto dropImplBody = ::MIR::FunctionPointer(new ::MIR::Function());
            {
                TRACE_FUNCTION_F("Generating drop impl");
                MirBuilder dropBuilder(sp, resolve, resolve.crate.types.unit(), genNode->dropFcnPtr->mArgs, *dropImplBody);
                ev.generatorMakeDrop(sp, dropBuilder, genNode->captureUsages.size(), mappings, dropFlagsFieldIdx, drop_flag_mapping);
                dropBuilder.finalCleanup();
            }
            for (auto& bb : dropImplBody->blocks) {
                for (auto& stmt : bb.statements) {
                    if (auto* d = stmt.opt_LoadDropFlag()) {
                        d->idx = drop_flag_mapping.at(d->idx);
                    }
                }
                if (auto* d = bb.terminator.opt_Drop()) {
                    if (d->flagIdx != ~0u) {
                        d->flagIdx = drop_flag_mapping.at(d->flagIdx);
                    }
                }
            }
            MIRValidate(resolve, path, *dropImplBody, genNode->dropFcnPtr->mArgs, resolve.crate.types.unit());
            genNode->dropFcnPtr->mCode.mir = std::move(dropImplBody);
        } else {
            rootNode.visit(ev);
            builder.finalCleanup();
        }
    }

    // NOTE: Can't clean up yet, as consteval isn't done
    //MIR_Cleanup(resolve, path, fcn, args, ret_ty);
    //DEBUG("MIR Dump:" << ::std::endl << FMT_CB(ss, MIR_Dump_Fcn(ss, fcn, 1);));
    MIRValidate(resolve, path, fcn, args, ret_ty);

    if (getenv("MRUSTC_VALIDATE_FULL_EARLY")) {
        MIRValidateFull(resolve, path, fcn, args, ptr->resType);
    }

    return ::MIR::FunctionPointer(new ::MIR::Function(mv$(fcn)));
}

// --------------------------------------------------------------------

void HIRGenerateMIRExpr(const ::HIR::Crate& crate, const ::HIR::ItemPath& path, ::HIR::ExprPtr& expr_ptr, const ::HIR::Function::argsT& args, const ::HIR::TypeData* resTy) {
    if (!expr_ptr.mir) {
        TRACE_FUNCTION;
        StaticTraitResolve resolve{crate};
        resolve.setBothGenericsRaw(expr_ptr.state->implGenerics, expr_ptr.state->itemGenerics);
        expr_ptr.setMir(LowerMIR(resolve, path, expr_ptr, resTy, args));
        // Run cleanup to simplify consteval?
        // - This ends up running before things like vtable generation, so parts of cleanup won't work.
        //MIR_Cleanup(resolve, path, *expr_ptr.m_mir, args, res_ty);
        // This path prepares an on-demand body for the constant evaluator, not
        // the runtime MIR selected by the driver. Keep normal inlining disabled,
        // but retain the local simplification that CTFE historically required.
        MIROptimise(resolve, path, *expr_ptr.mir, args, resTy, /*opt_level=*/2, /*do_inline=*/false);
    }
}

void HIRGenerateMIR(::HIR::Crate& crate) {
    ::MIR::OuterVisitor ov{crate, [&](const auto& res, const auto& p, ::HIR::ExprPtr& expr_ptr, const auto& args, const auto& ty) {
        if (!expr_ptr.getMirOpt()) {
            expr_ptr.setMir(LowerMIR(res, p, expr_ptr, ty, args));
        }
    }};
    ov.visitCrate(crate);
}


void MIRLowerHIRMatch(MirBuilder& builder, MirConverter& conv, ::HIR::ExprNodeMatch& node, ::MIR::LValue matchVal, const std::vector<unsigned>& letElseInitializerTemps);

namespace {
    void getTyAndVal(
        const Span& sp,
        MirBuilder& builder,
        const ::HIR::TypeData* top_ty,
        const ::MIR::LValue& top_val,
        const fieldPathT& field_path,
        unsigned int field_path_ofs,
        /*Out ->*/ ::HIR::TypeRef& outTy,
        ::MIR::LValue& outVal
    );
}

void MIRLowerHIRGetTypeValueForPath(
    const Span& sp,
    MirBuilder& builder,
    const ::HIR::TypeData* top_ty,
    const ::MIR::LValue& top_val,
    const fieldPathT& field_path,
    /*Out ->*/ ::HIR::TypeRef& outTy,
    ::MIR::LValue& outVal
) {
    getTyAndVal(sp, builder, top_ty, top_val, field_path, 0, outTy, outVal);
}

TAGGED_UNION_EX(
    PatternRule,
    (),
    Any,
    (
        // Enum variant
        (Variant,
         struct {
             unsigned int idx;
             ::std::vector<PatternRule> subRules;
         }),
        // Slice (includes desired length)
        (Slice,
         struct {
             unsigned int len;
             ::std::vector<PatternRule> subRules;
         }),
        // SplitSlice
        // TODO: How can the negative offsets in the `trailing` be handled correctly? (both here and in the destructure)
        (SplitSlice,
         struct {
             unsigned int minLen;
             unsigned int trailingLen;
             ::std::vector<PatternRule> leading, trailing;
         }),
        // Boolean (different to Constant because of how restricted it is)
        (Bool, bool),
        // General value
        (Value, ::MIR::Constant),
        (ValueRange,
         struct {
             ::MIR::Constant first, last;
             bool isInclusive;
         }),
        // _ pattern
        (Any, struct {})
    ),
    (, field_path(mv$(x.field_path))),
    (field_path = mv$(x.field_path);),
    (fieldPathT field_path;

     bool operator<(const PatternRule & x) const { return this->ord(x) == OrdLess; } bool operator==(const PatternRule & x) const { return this->ord(x) == OrdEqual; } bool operator!=(const PatternRule & x) const { return this->ord(x) != OrdEqual; } Ordering ord(const PatternRule& x) const;
     PatternRule clone() const;)
);
::std::ostream& operator<<(::std::ostream& os, const PatternRule& x);

/// Constructed set of rules from a pattern
struct PatternRuleset {
    unsigned int armIdx;
    unsigned int armRuleIdx;

    ::std::vector<PatternRule> rules;
    ::std::vector<PatternBinding> mBindings;

    static ::Ordering ruleIsBefore(const PatternRule& l, const PatternRule& r);

    bool isBefore(const PatternRuleset& other) const;
};

struct PatternDump {
    const StaticTraitResolve& resolve;
    const HIR::TypeData* ty;
    const ::std::vector<PatternRule>& rules;

    PatternDump(const StaticTraitResolve& resolve, const HIR::TypeData* ty, const ::std::vector<PatternRule>& rules)
        : resolve(resolve)
        , ty(ty)
        , rules(rules)
    {
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const PatternDump& x) {
        os << "[" << x.rules << "]";
        return os;
    }
};

/// Generated code for an arm
struct ArmCode {
    bool hasCondition = false;

    struct Pattern {
        /// Entrypoint for guard and destructuring
        ::MIR::BasicBlockId entry = 0;
        /// Block jumped to by the guard code when the condition fails
        ::MIR::BasicBlockId condFalse = ~0u;
    };

    std::vector<Pattern> rules;
};

typedef ::std::vector<PatternRuleset> tArmRules;

void MIRLowerHIRMatchSimple(MirBuilder& builder, MirConverter& conv, ::HIR::ExprNodeMatch& node, ::MIR::LValue matchVal, tArmRules armRules, ::std::vector<ArmCode> armCode, ::MIR::BasicBlockId firstCmpBlock);
int MIRLowerHIRMatchSimpleGeneratePattern(MirBuilder& builder, const Span& sp, const PatternRule* rules, unsigned int numRules, const ::HIR::TypeData* top_ty, const ::MIR::LValue& top_val, unsigned int field_path_ofs, ::MIR::BasicBlockId failBb);
void MIRLowerHIRMatchGrouped(MirBuilder& builder, MirConverter& conv, const Span& sp, const HIR::TypeData* matchTy, ::MIR::LValue matchVal, tArmRules armRules, ::std::vector<ArmCode> arms_code, ::MIR::BasicBlockId firstCmpBlock);
void MIRLowerHIRMatchDecisionTree(MirBuilder& builder, MirConverter& conv, ::HIR::ExprNodeMatch& node, ::MIR::LValue matchVal, tArmRules armRules, ::std::vector<ArmCode> armCode, ::MIR::BasicBlockId firstCmpBlock);

/// Helper to construct rules from a passed pattern
struct PatternRulesetBuilder {
    const StaticTraitResolve& mResolve;
    const ::HIR::SimplePath* mLangBox = nullptr;

    // NOTE: Multiple rulesets to handle or-patterns (which multiply the pattern set)
    struct Ruleset {
        bool isImpossible;
        ::std::vector<PatternRule> rules;
        ::std::vector<PatternBinding> mBindings;
        // Source-order path through nested or-pattern alternatives.  The
        // matching semantics are depth-first and left-to-right, so this path
        // orders the cartesian product after each expansion.
        ::std::vector<unsigned> orPath;

        Ruleset()
            : isImpossible(false)
        {
        }

        Ruleset clone() const {
            Ruleset rv;
            rv.isImpossible = isImpossible;
            for (const auto& e : rules) {
                rv.rules.push_back(e.clone());
            }
            rv.mBindings = mBindings;
            rv.orPath = orPath;
            return rv;
        }
    };

    std::vector<Ruleset> rulesets;
    size_t subsetStart, subsetEnd;

    fieldPathT fieldPath;

    PatternRulesetBuilder(const StaticTraitResolve& resolve)
        : mResolve(resolve)
        , rulesets(1)
        , subsetStart(0)
        , subsetEnd(1)
    {
        if (resolve.crate.mLangItems.count("owned_box") > 0) {
            mLangBox = &resolve.crate.mLangItems.at("owned_box");
        }
    }

    void appendFromLit(const Span& sp, EncodedLiteralSlice lit, const ::HIR::TypeData* ty);
    void appendFrom(const Span& sp, const ::HIR::Pattern& pat, const ::HIR::TypeData* ty);

private:
    void pushRule(PatternRule r);
    void pushBinding(PatternBinding b);
    void pushBindings(std::vector<PatternBinding> b);
    void setImpossible();

    void multiplyRulesets(size_t n, std::function<void(size_t idx)> cb);
};

class RulesetRef {
    ::std::vector<PatternRuleset>* rulesVec = nullptr;
    RulesetRef* parent = nullptr;
    size_t parentOfs = 0; // If len == 0, this is the innner index, else it's the base
    size_t parentLen = 0;

public:
    RulesetRef(::std::vector<PatternRuleset>& rules)
        : rulesVec(&rules)
    {
    }

    RulesetRef(RulesetRef& parent, size_t start, size_t n)
        : parent(&parent)
        , parentOfs(start)
        , parentLen(n)
    {
    }

    RulesetRef(RulesetRef& parent, size_t idx)
        : parent(&parent)
        , parentOfs(idx)
    {
    }

    size_t size() const {
        if (rulesVec) {
            return rulesVec->size();
        } else if (parentLen) {
            return parentLen;
        } else {
            return parent->size();
        }
    }

    RulesetRef slice(size_t s, size_t n) {
        return RulesetRef(*this, s, n);
    }

    const ::std::vector<PatternRule>& operator[](size_t i) const {
        if (rulesVec) {
            return (*rulesVec)[i].rules;
        } else if (parentLen) {
            return (*parent)[parentOfs + i];
        } else {
            // Fun part - Indexes into inner patterns
            const auto& parentRule = (*parent)[i][parentOfs];
            if (const auto* re = parentRule.opt_Variant()) {
                return re->subRules;
            } else {
                throw "TODO";
            }
        }
    }

    void swap(size_t a, size_t b) {
        TRACE_FUNCTION_F(a << ", " << b);
        if (rulesVec) {
            ::std::swap((*rulesVec)[a], (*rulesVec)[b]);
        } else {
            assert(parent);
            if (parentLen) {
                parent->swap(parentOfs + a, parentOfs + b);
            } else {
                parent->swap(a, b);
            }
        }
    }
};

void sortRulesets(RulesetRef rulesets, size_t idx = 0);
void sortRulesetsInner(RulesetRef rulesets, size_t idx);

// --------------------------------------------------------------------
// CODE
// --------------------------------------------------------------------
/// `let` (also used for destructuring arguments) - Introduces arguments into the current scope
///
/// If `else_node` is non-null, a `_` "arm" is added to invoke that block (which must diverge)
void MIRLowerHIRLet(MirBuilder& builder, MirConverter& conv, const Span& sp, const ::HIR::Pattern& pat, ::MIR::LValue val, const ::HIR::ExprNode* else_node) {
    TRACE_FUNCTION;

    HIR::TypeRef outer_ty;
    builder.withValType(sp, val, [&](const HIR::TypeData* ty) {
        outer_ty = ty;
    });

    auto successNode = builder.newBbUnlinked();
    auto firstCmpBlock = builder.pauseCurBlock();

    // - Convert HIR pattern into ruleset
    std::vector<PatternRuleset> armRules;
    std::vector<ArmCode> armCode;

    auto patScope = builder.newScopeSplit(sp);

    auto patBuilder = PatternRulesetBuilder{builder.resolve()};
    patBuilder.appendFrom(sp, pat, outer_ty);
    for (auto& sr : patBuilder.rulesets) {
        auto patIdx = static_cast<unsigned>(&sr - &patBuilder.rulesets.front());
        if (sr.isImpossible) {
            DEBUG("LET PAT #" << patIdx << " " << pat << " ==> IMPOSSIBLE [" << sr.rules << "]");
        } else {
            DEBUG("LET PAT #" << patIdx << " " << pat << " ==> [" << sr.rules << "]");
            armRules.push_back(PatternRuleset{patIdx, 0, mv$(sr.rules), mv$(sr.mBindings)});

            auto patNode = builder.newBbUnlinked();
            builder.setCurBlock(patNode);
            conv.destructureFromList(sp, outer_ty, val.clone(), armRules.back().mBindings);
            builder.endSplitArm(sp, patScope, /*reachable=*/true);
            builder.endBlock(MIR::Terminator::make_Goto(successNode));

            ArmCode::Pattern ap;
            ap.entry = patNode;
            ArmCode ac;
            ac.rules.push_back(ap);
            armCode.push_back(ac);
        }
    }
    builder.terminateScope(sp, mv$(patScope));
    if (else_node) {
        // Emit a check (similar to match)
        // NOTE: This is handled by "HIR Lower" currently, seems to work well
        TODO(sp, "Handle let-else");
    }

    MIRLowerHIRMatchGrouped(builder, conv, sp, outer_ty, mv$(val), mv$(armRules), mv$(armCode), firstCmpBlock);

    builder.setCurBlock(successNode);
}

// Handles lowering non-trivial matches to MIR
// - Non-trivial means that there's more than one pattern
// - Trivial matches are handled using `MIR_LowerHIR_Let`
void MIRLowerHIRMatch(MirBuilder& builder, MirConverter& conv, ::HIR::ExprNodeMatch& node, ::MIR::LValue matchVal, const std::vector<unsigned>& letElseInitializerTemps) {
    TRACE_FUNCTION;
    // NOTE: Lowers to the following pattern:
    // ```
    // loop {   // `match_scope`
    //     let _value = foo;
    //     if let Ok(_) = _value {
    //         if bar() {
    //             break { 1 };
    //         }
    //     }
    //     if let Ok(v) = _value {
    //         if true {
    //             break v;
    //         }
    //     }
    //     if let Err(_) = _value {
    //         if true {
    //             break panic();
    //         }
    //     }
    //     diverge()
    // }
    // ```

    // Indicates that an arm has a guard (which prevents most of the match optimisations from working)
    bool fallBackOnSimple = false;

    const auto& matchTy = node.mValue->resType;
    auto resultVal = builder.newTemporary(node.resType);
    auto nextBlock = builder.newBbUnlinked();

    /// Top level scope for the match
    auto matchScope = builder.newScopeLoop(node.span());

    // 1. Stop the current block so we can generate code before generating the pattern matching code
    auto firstCmpBlock = builder.pauseCurBlock();

    /// Entries for each arm, containing the code to run for each
    ::std::vector<ArmCode> armCode;
    /// Final list of rules (flattened patterns), for all patterns
    tArmRules armRules;

    // For each arm, generate the contents of the logical `if pattern_matches { if guard { break body; } }`
    for (unsigned int armIdx = 0; armIdx < node.arms.size(); armIdx++) {
        TRACE_FUNCTION_FR("ARM " << armIdx, "ARM " << armIdx);
        /*const*/ auto& arm = node.arms[armIdx];
        const Span& sp = arm.mCode->span();

        // ---
        // Convert all patterns on this arm into flattened "rules"
        // ---
        auto firstArmRuleIdx = armRules.size();
        for (unsigned int patIdx = 0; patIdx < arm.patterns.size(); patIdx++) {
            const auto& pat = arm.patterns[patIdx];

            auto patBuilder = PatternRulesetBuilder{builder.resolve()};
            patBuilder.appendFrom(node.span(), pat, matchTy);
            size_t firstRule = armRules.size();
            for (auto& sr : patBuilder.rulesets) {
                size_t i = &sr - &patBuilder.rulesets.front();
                if (sr.isImpossible) {
                    DEBUG("ARM PAT (" << armIdx << "," << patIdx << " #" << i << ") " << pat << " ==> IMPOSSIBLE [" << sr.rules << "]");
                } else {
                    DEBUG("ARM PAT (" << armIdx << "," << patIdx << " #" << i << ") " << pat << " ==> [" << sr.rules << "]");
                    // Sort the binding lists, so we can check that the lists are compatible
                    ::std::sort(sr.mBindings.begin(), sr.mBindings.end(), [](const PatternBinding& a, const PatternBinding& b) {
                        return a.binding->slot < b.binding->slot;
                    });
                    // Ensure that all patterns binding to the same set of variables (only check the variables)
                    if (firstRule < armRules.size()) {
                        const auto& fr = armRules[firstRule];
                        ASSERT_BUG(sp, fr.mBindings.size() == sr.mBindings.size(), "Disagreement in bindings between pattern - {" << armRules[firstRule].mBindings << "} vs {" << sr.mBindings << "}");
                        for (size_t j = 0; j < fr.mBindings.size(); j++) {
                            ASSERT_BUG(sp, fr.mBindings[j].binding->slot == sr.mBindings[j].binding->slot, "Disagreement in bindings between pattern - {" << armRules[firstRule].mBindings << "} vs {" << sr.mBindings << "}");
                        }
                    }
                    armRules.push_back(PatternRuleset{armIdx, static_cast<unsigned>(armRules.size() - firstArmRuleIdx), mv$(sr.rules), mv$(sr.mBindings)});
                }
            }
        }

        ArmCode ac;

        /// Block allocated for the body code of this arm (jumped to after bindings are set)
        auto armBodyBlock = builder.newBbUnlinked();

        /// Block for when the first rule matches (contains the guard and binding setup for this rule)
        auto entryBlockPat0 = builder.newBbUnlinked();
        builder.setCurBlock(entryBlockPat0);

        // Split scope for the `if pattern_matches { }` outer arm,
        auto patScope = builder.newScopeSplit(node.span());
        builder.endSplitArm(sp, patScope, /*reachable=*/true); // Inject the `else` case first, this should not push any statements

        // Generate code for this arm (guard, destructuring, and body)
        {
            // Scopes present for the body (generated during guard processing)
            // - Temporary/variable scopes, and split scopes
            struct MatchScope {
                ScopeHandle handle;
                bool isSplit;
            };

            std::vector<MatchScope> scopes;

            const auto& bindings0 = armRules[firstArmRuleIdx].mBindings;
            // Create aliases for every binding that only allows shared/immutable access (for use in the guard)
            auto aliases = builder.saveAliases();
            std::vector<unsigned> bindingTemps;
            std::vector<unsigned> bindingTempsAlt(bindings0.size(), ~0u);
            for (const auto& b : bindings0) {
                HIR::TypeRef finalTy = conv.getBindingType(sp, b.binding->slot);
                const Span& sp = arm.mCode->span();
                auto val = conv.getValueForBindingPath(sp, matchTy, matchVal, b);
                DEBUG("Set alias for: " << *b.binding << " := " << val);
                if (b.binding->mType != ::HIR::PatternBinding::Type::Move) {
                    const auto& borrow = finalTy->as_Borrow();
                    finalTy = builder.resolve().crate.types.borrow(::HIR::BorrowType::Shared, borrow.inner, borrow.lifetime);
                    // Not a move binding, still need to borrow but no deref
                    // - Or, make another temporary for the borrow (no scope needed)
                    auto tmp2 = builder.newTemporary(finalTy);
                    bindingTempsAlt[bindingTemps.size()] = tmp2.as_Local();
                    builder.pushStmtAssign(sp, tmp2.clone(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, std::move(val)}));
                    val = std::move(tmp2);
                }
                // Allocate a temporary to hold a borrow of that type
                auto tmp = builder.newTemporary(builder.resolve().crate.types.borrow(::HIR::BorrowType::Shared, finalTy));
                // - Store the temporary index so later copies can write to it
                bindingTemps.push_back(tmp.as_Local());
                // Assign the temporary with a borrow of the other slot
                builder.pushStmtAssign(sp, tmp.clone(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, std::move(val)}));
                // And set an alias to point to `*temp`
                builder.addVariableAlias(sp, b.binding->slot, ::HIR::PatternBinding::Type::Move, ::MIR::LValue::newDeref(std::move(tmp)));
            }

            // Require that either there's no guards, or that there's only one rule
            // - Otherwise, we can't (currently) prevent use-after-free
            // This is expected to fail at some point, but more testing needed elsewhere
            bool shouldFreeze = (!arm.guards.empty() && firstArmRuleIdx + 1 < armRules.size());
            scopes.push_back({builder.newScopeFreeze(sp), false});
            if (!shouldFreeze) {
                builder.unfreezeScope(sp, scopes.front().handle);
            }

            // Block at the start of the saved guard data
            auto block0 = builder.pauseCurBlock();
            builder.setCurBlock(block0);
            // Start saving code (the copyable part of the guard, after the assignment of the binding temporaries)
            auto csH = builder.codeSaveStart();
            MIR::BasicBlockId condFalseBlockPat0 = ~0u;
            bool guardDiverged = false;
            // Emit the condtion using the first set of bindings
            if (!arm.guards.empty()) {
                auto _dbe = conv.disableBorrowExtension();
                // Emit the guard code
                TRACE_FUNCTION_FR("CONDITIONAL", "CONDITIONAL");

                // The guards are chanined, and all must match for the arm to be taken
                // I.e. These are ANDs
                for (auto& c : arm.guards) {
                    const Span& sp = c.val->span();
                    // Emit the logical `if !guard { } else { ... }`

                    /// Block for when this guard successfully matches
                    auto destructure = builder.newBbUnlinked();

                    // Make a temp scope and push
                    scopes.push_back({builder.newScopeTemp(c.val->span()), false});
                    conv.visitNodePtr(c.val);
                    if (!builder.block_active()) {
                        guardDiverged = true;
                        break;
                    }
                    MIR::LValue matchCondVal = builder.getResultInLvalue(c.val->span(), c.val->resType);
                    DEBUG("GUARD " << c.pat << " = " << matchCondVal);

                    // If this is not a pattern-match, terminate the temporary scope here
                    if (c.isIf) {
                        auto t = builder.newTemporary(c.val->resType);
                        builder.pushStmtAssign(c.val->span(), t.clone(), std::move(matchCondVal));
                        matchCondVal = std::move(t);
                        builder.terminateScope(sp, std::move(scopes.back().handle));
                        scopes.pop_back();
                    }

                    // Generate simplified rules from patterns
                    auto patBuilder = PatternRulesetBuilder{builder.resolve()};
                    patBuilder.appendFrom(node.span(), c.pat, c.val->resType);

                    /// Block for when a pattern fails to match
                    auto localFalse = builder.newBbUnlinked();
                    bool localFalseUsed = false;
                    // OR'd patterns
                    ::std::vector<std::pair<MIR::BasicBlockId, const PatternRulesetBuilder::Ruleset*>> ends;
                    for (auto& sr : patBuilder.rulesets) {
                        if (sr.isImpossible) {
                            // The rule is impossible, so don't visit
                        } else {
                            if (localFalseUsed) {
                                localFalse = builder.newBbUnlinked();
                            }

                            ASSERT_BUG(c.val->span(), builder.block_active(), "Block not active");
                            MIRLowerHIRMatchSimpleGeneratePattern(builder, c.val->span(), sr.rules.data(), sr.rules.size(), c.val->resType, matchCondVal, 0, localFalse);
                            ends.push_back(std::make_pair(builder.pauseCurBlock(), &sr));
                            builder.setCurBlock(localFalse);
                            localFalseUsed = true;
                        }
                    }
                    if (!localFalseUsed) {
                        // None of the patterns were possible?
                        TODO(sp, "No possible arms in a `if-let` guard?");
                    }
                    if (condFalseBlockPat0 == ~0u) {
                        condFalseBlockPat0 = builder.newBbUnlinked();
                    }
                    // Split scope for the body of this logical `if`
                    scopes.push_back({builder.newScopeSplit(sp), true});
                    builder.endSplitArm(sp, scopes.back().handle, true);
                    // Currently in `local_false`
                    DEBUG("GUARD: Clean up and jump to `cond_false`");
                    // End the top scope early, which also handles ending all intervening scopes
                    builder.terminateScopeEarly(sp, scopes.front().handle);
                    // Indicate an exit point to the split
                    builder.endSplitArm(arm.mCode->span(), patScope, /*reachable*/ true, /*early*/ true);
                    builder.endBlock(::MIR::Terminator::make_Goto(condFalseBlockPat0));

                    // Introduce a local variable scope for the new bindings
                    scopes.push_back({builder.newScopeVar(c.val->span()), false});
                    conv.schedulePatternDrops(c.val->span(), c.pat, PatternDropOrder::FirstCandidate);

                    // Only introduce the new bindings (with `destructure_from_list`) after handling the early-exit case
                    // - This stops the `terminate_scope_early` from dropping too eagerly
                    for (const auto& e : ends) {
                        builder.setCurBlock(e.first);
                        conv.destructureFromList(arm.mCode->span(), c.val->resType, matchCondVal.clone(), e.second->mBindings, /*update_states=*/&e == ends.data());
                        builder.endBlock(::MIR::Terminator::make_Goto(destructure));
                    }

                    ASSERT_BUG(node.span(), !builder.block_active(), "Block still active?");
                    builder.setCurBlock(destructure);
                }
            }
            if (guardDiverged) {
                if (shouldFreeze) {
                    builder.unfreezeScope(sp, scopes.front().handle);
                }
                builder.restoreAliases(std::move(aliases));
                auto guardCode = builder.codeSaveEnd(std::move(csH));

                while (!scopes.empty()) {
                    builder.terminateScope(arm.mCode->span(), std::move(scopes.back().handle), false);
                    scopes.pop_back();
                }
                builder.endSplitArm(arm.mCode->span(), patScope, /*reachable=*/false);
                builder.terminateScope(sp, std::move(patScope), false);
                builder.terminateScopeEarly(sp, matchScope);

                ac.rules.push_back(ArmCode::Pattern{entryBlockPat0, ~0u});
                for (size_t i = firstArmRuleIdx + 1; i < armRules.size(); i++) {
                    struct DivergingGuardMapper: public MirBuilder::CloneMapper {
                        MIR::BasicBlockId block0;

                        DivergingGuardMapper(MIR::BasicBlockId block0)
                            : block0(block0)
                        {
                        }

                        MIR::BasicBlockId updateBbRef(MIR::BasicBlockId bbIdx) override {
                            if (bbIdx < block0) {
                                return bbIdx;
                            }
                            BUG(Span(), "Diverging guard referenced unsaved block bb" << bbIdx << " after bb" << block0);
                        }
                    } mapper(block0);

                    auto entryBlock = builder.newBbUnlinked();
                    builder.setCurBlock(entryBlock);
                    ASSERT_BUG(sp, bindingTemps.size() == armRules[i].mBindings.size(), "Mismatched guard bindings");
                    for (size_t j = 0; j < bindingTemps.size(); j++) {
                        const auto& b = armRules[i].mBindings[j];
                        auto val = conv.getValueForBindingPath(sp, matchTy, matchVal, b);
                        if (b.binding->mType != ::HIR::PatternBinding::Type::Move) {
                            MIR::LValue tmp2;
                            if (bindingTempsAlt[j] == ~0u) {
                                auto finalTy = conv.getBindingType(sp, b.binding->slot);
                                const auto& borrow = finalTy->as_Borrow();
                                finalTy = builder.resolve().crate.types.borrow(::HIR::BorrowType::Shared, borrow.inner, borrow.lifetime);
                                tmp2 = builder.newTemporary(finalTy);
                                bindingTempsAlt[j] = tmp2.as_Local();
                            } else {
                                tmp2 = ::MIR::LValue::newLocal(bindingTempsAlt[j]);
                            }
                            builder.pushStmtAssign(sp, tmp2.clone(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, std::move(val)}));
                            val = std::move(tmp2);
                        }
                        builder.pushStmtAssign(sp, ::MIR::LValue::newLocal(bindingTemps[j]), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, std::move(val)}));
                    }
                    builder.insertCloned(sp, guardCode, mapper);
                    ASSERT_BUG(sp, !builder.block_active(), "Diverging guard clone remained reachable");
                    ac.rules.push_back(ArmCode::Pattern{entryBlock, ~0u});
                }

                ac.hasCondition = false;
                fallBackOnSimple = true;
                armCode.push_back(std::move(ac));
                continue;
            }
            // Release the freezing of outer states
            if (shouldFreeze) {
                // NOTE: The first scope should be the freeze
                builder.unfreezeScope(sp, scopes.front().handle);
            }
            // And undo aliases
            builder.restoreAliases(std::move(aliases));
            auto guardEndBlock = builder.newBbUnlinked();
            builder.endBlock(::MIR::Terminator::make_Goto(guardEndBlock));
            auto guardCode = builder.codeSaveEnd(std::move(csH));
            builder.setCurBlock(guardEndBlock);
            // Emit actual bindings
            DEBUG("Arm " << armIdx << " rule " << 0 << ":  Destructure");
            scopes.push_back({builder.newScopeVar(arm.mCode->span()), false});
            conv.schedulePatternDrops(node.span(), arm.patterns.back(), PatternDropOrder::LastCandidate);
            auto bindingSplit = builder.newScopeSplit(arm.mCode->span());
            conv.destructureFromList(arm.mCode->span(), matchTy, matchVal.clone(), bindings0);
            builder.endSplitArm(arm.mCode->span(), bindingSplit, /*reachable=*/true);
            builder.endBlock(::MIR::Terminator::make_Goto(armBodyBlock));

            // The first rule just uses the code generated above
            {
                ArmCode::Pattern acp;
                acp.entry = entryBlockPat0;
                acp.condFalse = condFalseBlockPat0;
                ac.rules.push_back(acp);
            }
            // Subsequent rules clone the guard with different values for the bindings, and (importantly) a different failure exit point
            for (size_t i = firstArmRuleIdx + 1; i < armRules.size(); i++) {
                TRACE_FUNCTION_FR("Bindings (AR" << i << ")", "Bindings (AR" << i << ")");

                // Clone guard code, with the two exit blocks updated, and references updated
                struct Mapper: public MirBuilder::CloneMapper {
                    MIR::BasicBlockId block0;
                    MIR::BasicBlockId condFalse;
                    MIR::BasicBlockId condTrue;
                    MIR::BasicBlockId newCondFalse;
                    MIR::BasicBlockId newCondTrue;

                    Mapper(MirBuilder& builder, MIR::BasicBlockId block0, MIR::BasicBlockId condFalse, MIR::BasicBlockId condTrue)
                        : block0(block0)
                        , condFalse(condFalse)
                        , condTrue(condTrue)
                        , newCondFalse(builder.newBbUnlinked())
                        , newCondTrue(builder.newBbUnlinked())
                    {
                        DEBUG("new_cond_false=" << newCondFalse << ", new_cond_true=" << newCondTrue);
                    }

                    MIR::BasicBlockId updateBbRef(MIR::BasicBlockId bbIdx) {
                        // Any block defined before the save just propagates through
                        // E.g. if the guard contains a `break`
                        if (bbIdx < block0) {
                            return bbIdx;
                        }
                        if (bbIdx == condFalse) {
                            return newCondFalse;
                        }
                        if (bbIdx == condTrue) {
                            return newCondTrue;
                        }
                        BUG(Span(),
                            "update_bb_ref: Unknown BB " << bbIdx << " "
                                                         << ": block0=" << block0 << ", cond_false=" << condFalse << ", cond_true=" << condTrue);
                    }
                } mapper(builder, block0, condFalseBlockPat0, guardEndBlock);

                auto entryBlock = builder.newBbUnlinked();
                builder.setCurBlock(entryBlock);
                // Set the binding temporaries with the correct borrows
                assert(bindingTemps.size() == armRules[i].mBindings.size());
                for (size_t j = 0; j < bindingTemps.size(); j++) {
                    const auto& b = armRules[i].mBindings[j];
                    auto val = conv.getValueForBindingPath(sp, matchTy, matchVal, b);
                    DEBUG("Set alias for: " << *b.binding << " := " << val);
                    if (b.binding->mType != ::HIR::PatternBinding::Type::Move) {
                        MIR::LValue tmp2;
                        if (bindingTempsAlt[j] == ~0u) {
                            // Not a move binding, still need to borrow but no deref
                            // - Or, make another temporary for the borrow (no scope needed)
                            auto finalTy = conv.getBindingType(sp, b.binding->slot);
                            const auto& borrow = finalTy->as_Borrow();
                            finalTy = builder.resolve().crate.types.borrow(::HIR::BorrowType::Shared, borrow.inner, borrow.lifetime);
                            tmp2 = builder.newTemporary(finalTy);
                            bindingTempsAlt[j] = tmp2.as_Local();
                        } else {
                            tmp2 = ::MIR::LValue::newLocal(bindingTempsAlt[j]);
                        }
                        builder.pushStmtAssign(sp, tmp2.clone(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, std::move(val)}));
                        val = std::move(tmp2);
                    }
                    builder.pushStmtAssign(sp, ::MIR::LValue::newLocal(bindingTemps[j]), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, std::move(val)}));
                }
                // Clone the guard contents with updated block references
                builder.insertCloned(sp, guardCode, mapper);

                // Add the final bindings and jump to the body
                builder.setCurBlock(mapper.newCondTrue);
                DEBUG("Arm " << armIdx << " rule " << i - firstArmRuleIdx << ":  Destructure");
                conv.destructureFromList(arm.mCode->span(), matchTy, matchVal.clone(), armRules[i].mBindings);
                builder.endSplitArm(arm.mCode->span(), bindingSplit, /*reachable=*/true);
                builder.endBlock(::MIR::Terminator::make_Goto(armBodyBlock));

                ArmCode::Pattern acp;
                acp.entry = entryBlock;
                acp.condFalse = mapper.newCondFalse;
                ac.rules.push_back(acp);
            }

            // All successful pattern alternatives enter the same body. Merge
            // their move states first so the body and every unwind edge use
            // drop flags valid for every predecessor.
            builder.terminateScope(arm.mCode->span(), std::move(bindingSplit), /*emit_cleanup=*/false);

            // Emit body code
            DEBUG("-- Body Code");

            scopes.push_back({builder.newScopeTemp(arm.mCode->span()), false});
            builder.setCurBlock(armBodyBlock);

            if (node.isLetElse && armIdx + 1 == node.arms.size()) {
                for (const auto temporary : ::reverse(letElseInitializerTemps)) {
                    builder.dropLvalue(node.span(), ::MIR::LValue::newLocal(temporary));
                }
            }

            conv.visitNodePtr(arm.mCode);

            if (builder.block_active()) {
                // - Set result
                auto res = builder.getResult(arm.mCode->span());
                builder.pushStmtAssign(arm.mCode->span(), resultVal.clone(), mv$(res));
            } else {
                assert(!builder.hasResult());
            }
            // Pop/end scopes
            while (!scopes.empty()) {
                if (scopes.back().isSplit) {
                    builder.endSplitArm(arm.mCode->span(), scopes.back().handle, /*reachable*/ builder.block_active());
                }
                builder.terminateScope(arm.mCode->span(), std::move(scopes.back().handle), builder.block_active());
                scopes.pop_back();
            }
            builder.endSplitArm(arm.mCode->span(), patScope, /*reachable*/ builder.block_active());
            builder.terminateScope(sp, std::move(patScope), builder.block_active());
            builder.terminateScopeEarly(sp, matchScope);

            // Go to the next block (out of the match) (if the body didn't diverge)
            if (builder.block_active()) {
                builder.endBlock(::MIR::Terminator::make_Goto(nextBlock));
            }
        }

        // If there is a guard, then flag
        if (!arm.guards.empty()) {
            ac.hasCondition = true;

            // TODO: What to do with conditionals in the fast model?
            // > Could split the match on each conditional - separating such that if a conditional fails it can fall into the other compatible branches.
            // For now: Disable the complex logic, and fall back to a sequence of checks.
            fallBackOnSimple = true;
        } else {
            ac.hasCondition = false;
        }

        armCode.push_back(std::move(ac));
    }

    // Sort columns of `arm_rules` to maximise effectiveness
    if (armRules[0].rules.size() > 1) {
        // TODO: Should columns be sorted within equal sub-arms too?
        ::std::vector<unsigned> columnWeights(armRules[0].rules.size());
        for (const auto& armRule : armRules) {
            ASSERT_BUG(node.span(), columnWeights.size() == armRule.rules.size(), "Arm " << (&armRule - &armRules.front()) << " size doesn't match first (" << armRule.rules.size() << " != " << columnWeights.size() << ")");
            for (unsigned int i = 0; i < armRule.rules.size(); i++) {
                if (!armRule.rules[i].is_Any()) {
                    columnWeights.at(i) += 1;
                }
            }
        }

        DEBUG("- Column weights = [" << columnWeights << "]");
        // - Sort columns such that the largest (most specific) comes first
        ::std::vector<unsigned> columnsSorted(columnWeights.size());
        ::std::iota(columnsSorted.begin(), columnsSorted.end(), 0);
        ::std::sort(columnsSorted.begin(), columnsSorted.end(), [&](auto a, auto b) {
            return columnWeights[a] > columnWeights[b];
        });
        DEBUG("- Sorted to = [" << columnsSorted << "]");
        for (auto& armRule : armRules) {
            assert(columnsSorted.size() == armRule.rules.size());
            ::std::vector<PatternRule> sorted;
            sorted.reserve(columnsSorted.size());
            for (auto idx : columnsSorted) {
                sorted.push_back(mv$(armRule.rules[idx]));
            }
            armRule.rules = mv$(sorted);
        }
    }

    for (const auto& armRule : armRules) {
        DEBUG("> (" << armRule.armIdx << ", " << armRule.armRuleIdx << ") - " << armRule.rules << (armCode[armRule.armIdx].hasCondition ? " (cond)" : ""));
    }

    // TODO: Remove columns that are all `_`?
    // - Ideally, only accessible structures would be fully destructured like this, making this check redundant

    // Sort rules using the following restrictions:
    // - A rule cannot be reordered across an item that has an overlapping match set
    //  > e.g. nothing can cross _
    //  > equal rules cannot be reordered
    //  > Values cannot cross ranges that contain the value
    //  > This will have to be a bubble sort to ensure that it's correctly stable.
    if (!fallBackOnSimple) {
        sortRulesets(armRules);
        DEBUG("Post-sort");
        for (const auto& armRule : armRules) {
            DEBUG("> (" << armRule.armIdx << ", " << armRule.armRuleIdx << ") - " << armRule.rules << (armCode[armRule.armIdx].hasCondition ? " (cond)" : ""));
        }
    }
    // De-duplicate arms (emitting a warning when it happens)
    // - This allows later code to assume that duplicate arms are a codegen bug.
    if (!armRules.empty()) {
        for (auto it = armRules.begin() + 1; it != armRules.end();) {
            // If duplicate rule, (and neither is conditional)
            if ((it - 1)->rules == it->rules && !armCode[it->armIdx].hasCondition && !armCode[(it - 1)->armIdx].hasCondition) {
                WARNING(node.arms[it->armIdx].mCode->span(), W0000, "Duplicate match pattern, unreachable code" << "\n - Pattern : " << PatternDump(builder.resolve(), matchTy, it->rules) << "\n - Previous at " << node.arms[(it - 1)->armIdx].mCode->span());
                // Remove
                it = armRules.erase(it);
            } else {
                ++it;
            }
        }
    }

    // TODO: Combine identical-pattern arms, allowing potential use of condtionals
    // -
    // If there's a conditional that isn't grouped with an unconditional pattern - then force fallback

    // TODO: SplitSlice is buggy, make it fall back to simple?

    // TODO: Don't generate inner code until decisions are generated (keeps MIR flow nice)
    // - Challenging, as the decision code needs somewhere to jump to.
    // - Allocating a BB and then rewriting references to it is a possibility.

    if (fallBackOnSimple) {
        MIRLowerHIRMatchSimple(builder, conv, node /*.span(), match_ty*/, mv$(matchVal), mv$(armRules), mv$(armCode), firstCmpBlock);
    } else {
        MIRLowerHIRMatchGrouped(builder, conv, node.span(), matchTy, mv$(matchVal), mv$(armRules), mv$(armCode), firstCmpBlock);
    }

    builder.setCurBlock(nextBlock);
    builder.setResult(node.span(), mv$(resultVal));
    builder.terminateScope(node.span(), mv$(matchScope));
}

// --------------------------------------------------------------------
// Common Code - Pattern Rules
// --------------------------------------------------------------------
::std::ostream& operator<<(::std::ostream& os, const PatternRule& x) {
    os << "{" << x.field_path << "}=";
    TU_MATCH_HDRA( (x), {)
    TU_ARMA(Any, e) {
            os << "_";
        }
        // Enum variant
        TU_ARMA(Variant, e) {
            os << e.idx << " [" << e.subRules << "]";
        }
        // Slice pattern
        TU_ARMA(Slice, e) {
            os << "len=" << e.len << " [" << e.subRules << "]";
        }
        // SplitSlice
        TU_ARMA(SplitSlice, e) {
            os << "len>=" << e.minLen << " [" << e.leading << ", ..., " << e.trailing << "]";
        }
        // Boolean (different to Constant because of how restricted it is)
        TU_ARMA(Bool, e) {
            os << (e ? "true" : "false");
        }
        // General value
        TU_ARMA(Value, e) {
            os << e;
        }
        TU_ARMA(ValueRange, e) {
            os << e.first << " .." << (e.isInclusive ? "=" : "") << " " << e.last;
        }
    }
    return os;
}

::Ordering PatternRule::ord(const PatternRule& x) const {
    ORD(static_cast<int>(tag()), static_cast<int>(x.tag()));
    ORD(this->field_path, x.field_path);

    TU_MATCH_HDRA( (*this, x), {)
    TU_ARMA(Any, te, xe) {
            return OrdEqual;
        }
        TU_ARMA(Variant, te, xe) {
            if (te.idx != xe.idx) {
                return ::ord(te.idx, xe.idx);
            }
            assert(te.subRules.size() == xe.subRules.size());
            for (unsigned int i = 0; i < te.subRules.size(); i++) {
                auto cmp = te.subRules[i].ord(xe.subRules[i]);
                if (cmp != ::OrdEqual) {
                    return cmp;
                }
            }
            return ::OrdEqual;
        }
        TU_ARMA(Slice, te, xe) {
            if (te.len != xe.len) {
                return ::ord(te.len, xe.len);
            }
            // Wait? Why would the rule count be the same?
            assert(te.subRules.size() == xe.subRules.size());
            for (unsigned int i = 0; i < te.subRules.size(); i++) {
                auto cmp = te.subRules[i].ord(xe.subRules[i]);
                if (cmp != ::OrdEqual) {
                    return cmp;
                }
            }
            return ::OrdEqual;
        }
        TU_ARMA(SplitSlice, te, xe) {
            ORD(te.leading, xe.leading);
            ORD(te.minLen, xe.minLen);
            return ::ord(te.trailing, xe.trailing);
        }
        TU_ARMA(Bool, te, xe) {
            return ::ord(te, xe);
        }
        TU_ARMA(Value, te, xe) {
            return ::ord(te, xe);
        }
        TU_ARMA(ValueRange, te, xe) {
            ORD(te.first, xe.first);
            ORD(te.last, xe.last);
            return ::ord(te.isInclusive, xe.isInclusive);
        }
    }
    throw "";
}

PatternRule PatternRule::clone() const {
    struct H {
        static std::vector<PatternRule> cloneList(const std::vector<PatternRule>& l) {
            std::vector<PatternRule> rv;
            for (const auto& e : l) {
                rv.push_back(e.clone());
            }
            return rv;
        }

        static PatternRule cloneInner(const PatternRule& t) {
            TU_MATCH_HDRA( (t), {)
            TU_ARMA(Any, te)
                return te;

                TU_ARMA(Variant, te)
                return PatternRule::make_Variant({te.idx, H::cloneList(te.subRules)});
                TU_ARMA(Slice, te)
                return PatternRule::make_Slice({te.len, H::cloneList(te.subRules)});
                TU_ARMA(SplitSlice, te)
                return PatternRule::make_SplitSlice({te.minLen, te.trailingLen, H::cloneList(te.leading), H::cloneList(te.trailing)});

                TU_ARMA(Bool, te)
                return te;
                TU_ARMA(Value, te)
                return te.clone();
                TU_ARMA(ValueRange, te)
                return PatternRule::make_ValueRange({te.first.clone(), te.last.clone(), te.isInclusive});
            }
            throw "";
        }
    };

    auto rv = H::cloneInner(*this);
    rv.field_path = this->field_path;
    return rv;
}

::Ordering PatternRuleset::ruleIsBefore(const PatternRule& l, const PatternRule& r) {
    if (l.tag() != r.tag()) {
        // Any comes last, don't care about rest
        if (l.tag() < r.tag()) {
            return ::OrdGreater;
        } else {
            return ::OrdLess;
        }
    }

    TU_MATCH_HDRA( (l,r), {)
    TU_ARMA(Any, le,re) {
            return ::OrdEqual;
        }
        TU_ARMA(Variant, le, re) {
            if (le.idx != re.idx) {
                return ::ord(le.idx, re.idx);
            }
            assert(le.subRules.size() == re.subRules.size());
            for (unsigned int i = 0; i < le.subRules.size(); i++) {
                auto cmp = ruleIsBefore(le.subRules[i], re.subRules[i]);
                if (cmp != ::OrdEqual) {
                    return cmp;
                }
            }
            return ::OrdEqual;
        }
        TU_ARMA(Slice, le, re) {
            if (le.len != re.len) {
                return ::ord(le.len, re.len);
            }
            // Wait? Why would the rule count be the same?
            assert(le.subRules.size() == re.subRules.size());
            for (unsigned int i = 0; i < le.subRules.size(); i++) {
                auto cmp = ruleIsBefore(le.subRules[i], re.subRules[i]);
                if (cmp != ::OrdEqual) {
                    return cmp;
                }
            }
            return ::OrdEqual;
        }
        TU_ARMA(SplitSlice, le, re) {
            TODO(Span(), "Order PatternRule::SplitSlice");
        }
        TU_ARMA(Bool, le, re) {
            return ::ord(le, re);
        }
        TU_ARMA(Value, le, re) {
            TODO(Span(), "Order PatternRule::Value");
        }
        TU_ARMA(ValueRange, le, re) {
            TODO(Span(), "Order PatternRule::ValueRange");
        }
    }
    throw "";
}

bool PatternRuleset::isBefore(const PatternRuleset& other) const {
    assert(rules.size() == other.rules.size());
    for (unsigned int i = 0; i < rules.size(); i++) {
        const auto& l = rules[i];
        const auto& r = other.rules[i];
        auto cmp = ruleIsBefore(l, r);
        if (cmp != ::OrdEqual) {
            return cmp == ::OrdLess;
        }
    }
    return false;
}

void PatternRulesetBuilder::pushRule(PatternRule r) {
    assert(this->subsetStart < this->subsetEnd);
    assert(this->subsetEnd <= rulesets.size());
    for (size_t i = subsetStart; i < subsetEnd; i++) {
        rulesets[i].rules.push_back(i == subsetEnd - 1 ? std::move(r) : r.clone());
        rulesets[i].rules.back().field_path = fieldPath;
    }
}

void PatternRulesetBuilder::pushBinding(PatternBinding b) {
    assert(this->subsetStart < this->subsetEnd);
    assert(this->subsetEnd <= rulesets.size());
    for (size_t i = subsetStart; i < subsetEnd; i++) {
        DEBUG(i << " " << b);
        rulesets[i].mBindings.push_back(b);
    }
}

void PatternRulesetBuilder::pushBindings(std::vector<PatternBinding> bindings) {
    assert(this->subsetStart < this->subsetEnd);
    assert(this->subsetEnd <= rulesets.size());
    for (size_t i = subsetStart; i < subsetEnd; i++) {
        auto& l = rulesets[i].mBindings;
        l.insert(l.end(), bindings.begin(), bindings.end());
        DEBUG(i << " [" << bindings << "] = [" << l << "]");
    }
}

void PatternRulesetBuilder::setImpossible() {
    assert(this->subsetStart < this->subsetEnd);
    assert(this->subsetEnd <= rulesets.size());
    for (size_t i = subsetStart; i < subsetEnd; i++) {
        rulesets[i].isImpossible = true;
    }
}

/// Multiply the current subset of the ruleset, then visit every new subset
void PatternRulesetBuilder::multiplyRulesets(size_t n, std::function<void(size_t idx)> cb) {
    assert(n > 0);
    if (n == 1) {
        cb(0);
        return;
    }
    TRACE_FUNCTION_F(n);
    assert(this->subsetStart < this->subsetEnd);
    assert(this->subsetEnd <= rulesets.size());
    size_t subsetSize = this->subsetEnd - this->subsetStart;
    size_t ofs = (n - 1) * subsetSize;
    assert(ofs > 0);
    size_t newSubsetEnd = this->subsetStart + n * subsetSize;
    size_t nTail = rulesets.size() - this->subsetEnd;
    DEBUG("subset_size=" << subsetSize << ", ofs = " << ofs << ", n_tail=" << nTail);
    rulesets.resize(rulesets.size() + (n - 1) * subsetSize);
    assert(newSubsetEnd == rulesets.size() - nTail);
    // Copy the tail out of the way (reverse to avoid chasing itself)
    for (size_t i = rulesets.size(); i-- > newSubsetEnd;) {
        rulesets[i] = std::move(rulesets[i - ofs]);
    }
    // Copy `n-1` copies of the current subset after itself
    for (size_t j = 1; j < n; j++) {
        for (size_t i = 0; i < subsetSize; i++) {
            const auto& src = rulesets[this->subsetStart + i];
            rulesets[this->subsetStart + j * subsetSize + i] = src.clone();
        }
    }
    for (size_t j = this->subsetStart + subsetSize; j < newSubsetEnd; j += subsetSize) {
        for (size_t i = 0; i < subsetSize; i++) {
            const auto& exp = rulesets[this->subsetStart + i];
            const auto& a = rulesets[j + i];
            ASSERT_BUG(Span(), a.rules == exp.rules, "BUG: {" << a.rules << "} != {" << exp.rules << "}");
            ASSERT_BUG(Span(), a.mBindings == exp.mBindings, "BUG: {" << a.mBindings << "} != {" << exp.mBindings << "}");
        }
    }
    for (size_t i = this->subsetStart; i < newSubsetEnd; i += 1) {
        DEBUG("#" << i << " rules=[" << rulesets[i].rules << "], bindings=[" << rulesets[i].mBindings << "]");
    }

    // Iterate the new subsets
    size_t savedStart = this->subsetStart;
    this->subsetEnd = this->subsetStart;
    for (size_t i = 0; i < n; i++) {
        auto origStart = this->subsetStart;
        this->subsetEnd += subsetSize;
        DEBUG("++ " << i << " " << this->subsetStart << " - " << this->subsetEnd);
        for (size_t j = this->subsetStart; j < this->subsetEnd; j++) {
            rulesets[j].orPath.push_back(static_cast<unsigned>(i));
        }
        cb(i);
        DEBUG("-- " << i);
        assert(this->subsetStart == origStart);                     // This should always be unchanged (even if the callback splits again). The end can change though.
        assert(this->subsetEnd >= this->subsetStart + subsetSize); // The end should always be at least equal to start + size (i.e. hasn't shrunk)
        this->subsetStart = this->subsetEnd;
    }
    // Update the subset again to cover everything
    this->subsetStart = savedStart;
    ::std::stable_sort(
        rulesets.begin() + this->subsetStart,
        rulesets.begin() + this->subsetEnd,
        [](const Ruleset& a, const Ruleset& b) {
            return a.orPath < b.orPath;
        });
    // NOTE: Can't asser that the end is as-expected, as there might be inner subsets created that makes this assumption no longer valid
    //ASSERT_BUG(Span(), this->subset_end == new_subset_end, this->subset_end << " == " << new_subset_end);
    for (size_t i = this->subsetStart; i < this->subsetEnd; i += 1) {
        DEBUG("#" << i << " rules=[" << rulesets[i].rules << "], bindings=[" << rulesets[i].mBindings << "]");
    }
}

void PatternRulesetBuilder::appendFromLit(const Span& sp, EncodedLiteralSlice lit, const ::HIR::TypeData* ty) {
    TRACE_FUNCTION_F("lit=" << lit << ", ty=" << ty << ",   m_field_path=[" << fieldPath << "]");

    TU_MATCH_HDRA( (*ty), {)
    TU_ARMA(Infer, e)   BUG(sp, "Ivar for in match type");
        TU_ARMA(Diverge, e) BUG(sp, "Diverge in match type");
        TU_ARMA(Primitive, e) {
            switch (e) {
                case ::HIR::CoreType::F16:
                    this->pushRule(PatternRule::make_Value(::MIR::Constant::make_Float({lit.readFloat(2), e})));
                    break;
                case ::HIR::CoreType::F32:
                    this->pushRule(PatternRule::make_Value(::MIR::Constant::make_Float({lit.readFloat(4), e})));
                    break;
                case ::HIR::CoreType::F64:
                    this->pushRule(PatternRule::make_Value(::MIR::Constant::make_Float({lit.readFloat(8), e})));
                    break;
                case ::HIR::CoreType::F128:
                    this->pushRule(PatternRule::make_Value(::MIR::Constant::make_Float({lit.readFloat(16), e})));
                    break;

                case ::HIR::CoreType::U8:
                    this->pushRule(PatternRule::make_Value(::MIR::Constant::make_Uint({lit.readUint(1), e})));
                    break;
                case ::HIR::CoreType::U16:
                    this->pushRule(PatternRule::make_Value(::MIR::Constant::make_Uint({lit.readUint(2), e})));
                    break;
                case ::HIR::CoreType::U32:
                    this->pushRule(PatternRule::make_Value(::MIR::Constant::make_Uint({lit.readUint(4), e})));
                    break;
                case ::HIR::CoreType::U64:
                    this->pushRule(PatternRule::make_Value(::MIR::Constant::make_Uint({lit.readUint(8), e})));
                    break;
                case ::HIR::CoreType::U128:
                    this->pushRule(PatternRule::make_Value(::MIR::Constant::make_Uint({lit.readUint(16), e})));
                    break;
                case ::HIR::CoreType::Usize:
                    this->pushRule(PatternRule::make_Value(::MIR::Constant::make_Uint({lit.readUint(TargetGetPointerBits() / 8), e})));
                    break;

                case ::HIR::CoreType::I8:
                    this->pushRule(PatternRule::make_Value(::MIR::Constant::make_Int({lit.readSint(1), e})));
                    break;
                case ::HIR::CoreType::I16:
                    this->pushRule(PatternRule::make_Value(::MIR::Constant::make_Int({lit.readSint(2), e})));
                    break;
                case ::HIR::CoreType::I32:
                    this->pushRule(PatternRule::make_Value(::MIR::Constant::make_Int({lit.readSint(4), e})));
                    break;
                case ::HIR::CoreType::I64:
                    this->pushRule(PatternRule::make_Value(::MIR::Constant::make_Int({lit.readSint(8), e})));
                    break;
                case ::HIR::CoreType::I128:
                    this->pushRule(PatternRule::make_Value(::MIR::Constant::make_Int({lit.readSint(16), e})));
                    break;
                case ::HIR::CoreType::Isize:
                    this->pushRule(PatternRule::make_Value(::MIR::Constant::make_Int({lit.readSint(TargetGetPointerBits() / 8), e})));
                    break;

                case ::HIR::CoreType::Bool:
                    this->pushRule(PatternRule::make_Bool(lit.readUint(1) != 0));
                    break;
                // Char is just another name for 'u32'... but with a restricted range
                case ::HIR::CoreType::Char:
                    this->pushRule(PatternRule::make_Value(::MIR::Constant::make_Uint({lit.readUint(4), e})));
                    break;
                case ::HIR::CoreType::Str:
                    BUG(sp, "Hit match over `str` - must be `&str`");
                    break;
            }
        }
        TU_ARMA(Tuple, e) {
            auto* repr = TargetGetTypeRepr(sp, mResolve, ty);
            ASSERT_BUG(sp, repr, "Matching with generic constant type not valid - " << ty);
            ASSERT_BUG(sp, e.size() == repr->fields.size(), "Matching tuple with mismatched literal size - " << e.size() << " != " << repr->fields.size());

            fieldPath.push_back(0);
            for (unsigned int i = 0; i < e.size(); i++) {
                this->appendFromLit(sp, lit.slice(repr->fields[i].offset), repr->fields[i].ty);
                fieldPath.back()++;
            }
            fieldPath.pop_back();
        }
        TU_ARMA(Path, e) {
            // This is either a struct destructure or an enum
        TU_MATCH_HDRA( (e.binding), {)
        TU_ARMA(Unbound, pbe) {
                    BUG(sp, "Encounterd unbound path - " << e.path);
                }
                TU_ARMA(Opaque, pbe) {
                    TODO(sp, "Can an opaque path type be matched with a literal?");
                    //ASSERT_BUG(sp, lit.as_List().size() == 0 , "Matching unit struct with non-empty list - " << lit);
                    this->pushRule(PatternRule::make_Any({}));
                }
                TU_ARMA(Struct, pbe) {
                    auto* repr = TargetGetTypeRepr(sp, mResolve, ty);
                    ASSERT_BUG(sp, repr, "Matching with generic constant type not valid - " << ty);

                    fieldPath.push_back(0);
                    for (size_t i = 0; i < repr->fields.size(); i++) {
                        this->appendFromLit(sp, lit.slice(repr->fields[i].offset), repr->fields[i].ty);
                        fieldPath.back()++;
                    }
                    fieldPath.pop_back();
                }
                TU_ARMA(ExternType, pbe) {
                    TODO(sp, "Match extern type");
                }
                TU_ARMA(Union, pbe) {
                    TODO(sp, "Match union");
                }
                TU_ARMA(Enum, pbe) {
                    auto* enmRepr = TargetGetTypeRepr(sp, mResolve, ty);
                    ASSERT_BUG(sp, enmRepr, "Matching with generic constant type not valid - " << ty);

                    // TODO: Share code with `MIR_Cleanup_LiteralToRValue`
                    auto varInfo = enmRepr->getEnumVariant(sp, mResolve, lit);
                    unsigned varIdx = varInfo.first;
                    bool subHasTag = varInfo.second;

                    PatternRulesetBuilder subBuilder{this->mResolve};
                    if (enmRepr->fields.size() > 1 || enmRepr->variants.is_None()) {
                        subBuilder.fieldPath = fieldPath;
                        subBuilder.fieldPath.push_back(varIdx);

                        // If the tag is in the sub-type, then ignore.
                        const auto& varTy = enmRepr->fields[varIdx].ty;
                        auto varLit = lit.slice(enmRepr->fields[varIdx].offset);
                        // NOTE: The tag is only present if it's an auto-generated struct (i.e. not `()`)
                        if (subHasTag && varTy != mResolve.crate.types.unit()) {
                            // This inner type should be a struct
                            DEBUG("Enum variant type w/ tag field: " << varTy);
                            auto* innerRepr = TargetGetTypeRepr(sp, mResolve, varTy);
                            assert(innerRepr->variants.is_None());
                            assert(innerRepr->fields.size() > 0);
                            subBuilder.fieldPath.push_back(0);
                            for (size_t i = 0; i < innerRepr->fields.size() - 1; i++) {
                                subBuilder.appendFromLit(sp, varLit.slice(innerRepr->fields[i].offset), innerRepr->fields[i].ty);
                                subBuilder.fieldPath.back()++;
                            }
                            subBuilder.fieldPath.pop_back();
                        } else {
                            subBuilder.appendFromLit(sp, varLit, varTy);
                        }
                    }

                    ASSERT_BUG(sp, subBuilder.rulesets.size() == 1, "Multiple rulesets generated from a literal");
                    this->pushRule(PatternRule::make_Variant({varIdx, mv$(subBuilder.rulesets[0].rules)}));
                }
        }
        }
        TU_ARMA(Generic, e) {
            // Generics don't destructure, so the only valid pattern is `_`
            TODO(sp, "Match generic with literal?");
            this->pushRule(PatternRule::make_Any({}));
        }
        TU_ARMA(TraitObject, e) {
            TODO(sp, "Match trait object with literal?");
        }
        TU_ARMA(ErasedType, e) {
            TODO(sp, "Match erased type with literal?");
        }
        TU_ARMA(Array, e) {
            size_t size = 0;
            ASSERT_BUG(sp, TargetGetSizeOf(sp, mResolve, e.inner, size), "Matching with generic constant type not valid - " << ty);

            fieldPath.push_back(0);
            size_t ofs = 0;
            for (unsigned int i = 0; i < e.size.as_Known(); i++) {
                this->appendFromLit(sp, lit.slice(ofs, size), e.inner);
                ofs += size;
                fieldPath.back()++;
            }
            fieldPath.pop_back();
        }
        TU_ARMA(Slice, e) {
            TODO(sp, "Match literal Slice");
        }
        TU_ARMA(Borrow, e) {
            fieldPath.push_back(FIELD_DEREF);
            if (e.inner == ::HIR::CoreType::Str) {
                auto ptrSize = TargetGetPointerBits() / 8;
                auto ptr = lit.readUint(ptrSize).truncateU64();
                auto len = lit.slice(ptrSize, ptrSize).readUint(ptrSize).truncateU64();
                auto* r = lit.getReloc();
                ASSERT_BUG(sp, r, "Null relocation for string in pattern generation");
                ASSERT_BUG(sp, ptr >= EncodedLiteral::PTR_BASE, "");
                ptr -= EncodedLiteral::PTR_BASE;

                ASSERT_BUG(sp, !r->p, "TODO: Handle &str match constant with non-string relocation - " << *r->p);
                ASSERT_BUG(sp, ptr <= r->bytes.size(), "");
                ASSERT_BUG(sp, len <= r->bytes.size(), "");
                ASSERT_BUG(sp, ptr + len <= r->bytes.size(), "");

                this->pushRule(PatternRule::make_Value(std::string(r->bytes.data() + ptr, r->bytes.data() + ptr + len)));
            } else if (e.inner->is_Slice() && e.inner->as_Slice().inner == ::HIR::CoreType::U8) {
                auto ptrSize = TargetGetPointerBits() / 8;
                auto ptr = lit.readUint(ptrSize).truncateU64();
                auto len = lit.slice(ptrSize, ptrSize).readUint(ptrSize).truncateU64();
                auto* r = lit.getReloc();
                ASSERT_BUG(sp, r, "Null relocation for byte-string in pattern generation");
                ASSERT_BUG(sp, ptr >= EncodedLiteral::PTR_BASE, "");
                ptr -= EncodedLiteral::PTR_BASE;

                if (r->p) {
                    ASSERT_BUG(sp, ptr == 0, "TODO: Non-zero offset with reference");
                    MonomorphState valParams(mResolve.crate.types);
                    auto v = mResolve.getValue(sp, *r->p, valParams);
                    ASSERT_BUG(sp, v.is_Static(), "&[u8] match with borrow of non-static (" << *r->p << ") - " << v.tagStr());
                    const HIR::Static& s = *v.as_Static();
                    ASSERT_BUG(sp, s.valueGenerated, "&[u8] match with borrow of non-resolved static (" << *r->p << ")");
                    const EncodedLiteral& val = s.valueRes;
                    ASSERT_BUG(sp, ptr <= val.bytes.size(), "");
                    ASSERT_BUG(sp, len <= val.bytes.size(), "");
                    ASSERT_BUG(sp, ptr + len <= val.bytes.size(), "");

                    this->pushRule(PatternRule::make_Value(std::vector<uint8_t>(val.bytes.data() + ptr, val.bytes.data() + ptr + len)));
                } else {
                    ASSERT_BUG(sp, ptr <= r->bytes.size(), "");
                    ASSERT_BUG(sp, len <= r->bytes.size(), "");
                    ASSERT_BUG(sp, ptr + len <= r->bytes.size(), "");

                    this->pushRule(PatternRule::make_Value(std::vector<uint8_t>(r->bytes.data() + ptr, r->bytes.data() + ptr + len)));
                }
            } else {
                TODO(sp, "Match literal Borrow: ty=" << ty << " lit=" << lit);
            }
            fieldPath.pop_back();
        }
        TU_ARMA(Pointer, e) {
            // Need to be able to tell downstream to cast to integer before comparison?
            this->pushRule(PatternRule::make_Value(::MIR::Constant::make_Uint({lit.readUint(TargetGetPointerBits() / 8), HIR::CoreType::Usize})));
            //TODO(sp, "Match literal with pointer? " << lit);
        }
        TU_ARMA(NamedFunction, e) {
            ERROR(sp, E0000, "Attempting to match over a functon pointer");
        }
        TU_ARMA(Function, e) {
            ERROR(sp, E0000, "Attempting to match over a functon pointer");
        }
        TU_ARMA(NodeType, e) {
            ERROR(sp, E0000, "Attempting to match over a magic type");
        }
    }
}

void PatternRulesetBuilder::appendFrom(const Span& sp, const ::HIR::Pattern& pat, const ::HIR::TypeData* top_ty) {
    static ::HIR::Pattern emptyPattern;
    TRACE_FUNCTION_F("pat=" << pat << ", ty=" << top_ty << ",   m_field_path=[" << fieldPath << "]");

    struct H {
        static U128 getPatternValueInt(const Span& sp, const ::HIR::Pattern& pat, const ::HIR::Pattern::Value& val) {
            TU_MATCH_DEF(::HIR::Pattern::Value, (val), (e), (BUG(sp, "Invalid Value type in " << pat);), (Integer, return e.value;), (Named, assert(e.binding); return EncodedLiteralSlice(e.binding->valueRes).readUint();))
            throw "";
        }

        static S128 getPatternValueSigned(const Span& sp, const ::HIR::Pattern& pat, const ::HIR::Pattern::Value& val) {
            TU_MATCH_DEF(::HIR::Pattern::Value, (val), (e), (BUG(sp, "Invalid signed Value type in " << pat);), (Integer, return S128(e.value);), (Named, assert(e.binding); return EncodedLiteralSlice(e.binding->valueRes).readSint();))
            throw "";
        }

        static FloatValue getPatternValueFloat(const Span& sp, const ::HIR::Pattern& pat, const ::HIR::Pattern::Value& val) {
            TU_MATCH_DEF(::HIR::Pattern::Value, (val), (e), (BUG(sp, "Invalid Value type in " << pat);), (Float, return e.value;), (Named, assert(e.binding); return EncodedLiteralSlice(e.binding->valueRes).readFloat();))
            throw "";
        }

        static MIR::Constant getPatternValue(const Span& sp, const ::HIR::Pattern& pat, const ::HIR::Pattern::Value& val, const ::HIR::CoreType& e) {
            switch (e) {
                case ::HIR::CoreType::F16:
                case ::HIR::CoreType::F32:
                case ::HIR::CoreType::F64:
                case ::HIR::CoreType::F128:
                    // Yes, this is valid.
                    return ::MIR::Constant::make_Float({H::getPatternValueFloat(sp, pat, val), e});
                case ::HIR::CoreType::U8:
                case ::HIR::CoreType::U16:
                case ::HIR::CoreType::U32:
                case ::HIR::CoreType::U64:
                case ::HIR::CoreType::U128:
                case ::HIR::CoreType::Usize:
                    return ::MIR::Constant::make_Uint({H::getPatternValueInt(sp, pat, val), e});
                case ::HIR::CoreType::I8:
                case ::HIR::CoreType::I16:
                case ::HIR::CoreType::I32:
                case ::HIR::CoreType::I64:
                case ::HIR::CoreType::I128:
                case ::HIR::CoreType::Isize:
                    return ::MIR::Constant::make_Int({H::getPatternValueSigned(sp, pat, val), e});
                case ::HIR::CoreType::Bool:
                    BUG(sp, "Can't range match on Bool");
                    break;
                case ::HIR::CoreType::Char:
                    // Char is just another name for 'u32'... but with a restricted range
                    return ::MIR::Constant::make_Uint({H::getPatternValueInt(sp, pat, val), e});
                case ::HIR::CoreType::Str:
                    BUG(sp, "Hit match over `str` - must be `&str`");
                    break;
            }
            throw "";
        }

        static MIR::Constant getPatternValueMin(const Span& sp, const ::HIR::Pattern& pat, const ::HIR::CoreType& e) {
            switch (e) {
                case ::HIR::CoreType::F16:
                case ::HIR::CoreType::F32:
                case ::HIR::CoreType::F64:
                case ::HIR::CoreType::F128:
                    // Yes, this is valid.
                    return ::MIR::Constant::make_Float({-std::numeric_limits<double>::infinity(), e});
                case ::HIR::CoreType::U8:
                case ::HIR::CoreType::U16:
                case ::HIR::CoreType::U32:
                case ::HIR::CoreType::U64:
                case ::HIR::CoreType::U128:
                case ::HIR::CoreType::Usize:
                    return ::MIR::Constant::make_Uint({U128(0), e});
                case ::HIR::CoreType::I8:
                case ::HIR::CoreType::I16:
                case ::HIR::CoreType::I32:
                case ::HIR::CoreType::I64:
                case ::HIR::CoreType::I128:
                case ::HIR::CoreType::Isize:
                    return ::MIR::Constant::make_Int({S128::min(), e});
                case ::HIR::CoreType::Bool:
                    BUG(sp, "Can't range match on Bool");
                    break;
                case ::HIR::CoreType::Char:
                    // Char is just another name for 'u32'... but with a restricted range
                    return ::MIR::Constant::make_Uint({U128(0), e});
                case ::HIR::CoreType::Str:
                    BUG(sp, "Hit match over `str` - must be `&str`");
                    break;
            }
            throw "";
        }

        static MIR::Constant getPatternValueMax(const Span& sp, const ::HIR::Pattern& pat, const ::HIR::CoreType& e) {
            switch (e) {
                case ::HIR::CoreType::F16:
                case ::HIR::CoreType::F32:
                case ::HIR::CoreType::F64:
                case ::HIR::CoreType::F128:
                    // Yes, this is valid.
                    return ::MIR::Constant::make_Float({std::numeric_limits<double>::infinity(), e});
                case ::HIR::CoreType::U8:
                case ::HIR::CoreType::U16:
                case ::HIR::CoreType::U32:
                case ::HIR::CoreType::U64:
                case ::HIR::CoreType::U128:
                case ::HIR::CoreType::Usize:
                    return ::MIR::Constant::make_Uint({U128::max(), e});
                case ::HIR::CoreType::I8:
                case ::HIR::CoreType::I16:
                case ::HIR::CoreType::I32:
                case ::HIR::CoreType::I64:
                case ::HIR::CoreType::I128:
                case ::HIR::CoreType::Isize:
                    return ::MIR::Constant::make_Int({S128::max(), e});
                case ::HIR::CoreType::Bool:
                    BUG(sp, "Can't range match on Bool");
                    break;
                case ::HIR::CoreType::Char:
                    // Char is just another name for 'u32'... but with a restricted range
                    return ::MIR::Constant::make_Uint({U128::max(), e});
                case ::HIR::CoreType::Str:
                    BUG(sp, "Hit match over `str` - must be `&str`");
                    break;
            }
            throw "";
        }
    };

    for (const auto& pb : pat.mBindings) {
        auto path = fieldPath;
        for (size_t i = 0; i < pb.implicitDerefCount; i++) {
            path.push_back(FIELD_DEREF);
        }

        this->pushBinding(PatternBinding(std::move(path), pb));
    }

    const auto* tyP = &top_ty;
    for (size_t i = 0; i < pat.implicitDerefCount; i++) {
        if (!(*tyP)->is_Borrow()) {
            BUG(sp, "Deref step " << i << "/" << pat.implicitDerefCount << " hit a non-borrow " << *tyP << " from " << top_ty);
        }
        tyP = &(*tyP)->as_Borrow().inner;
        fieldPath.push_back(FIELD_DEREF);
    }
    const auto& ty = *tyP;

    // TODO: Outer handling for Value::Named patterns
    // - Convert them into either a pattern, or just a variant of this function that operates on ::HIR::Literal
    //  > It does need a way of handling unknown-value constants (e.g. <GenericT as Foo>::CONST)
    //  > Those should lead to a simple match? Or just a custom rule type that indicates that they're checked early
    if (const auto* pe = pat.mData.opt_Value()) {
        if (const auto* pve = pe->val.opt_Named()) {
            if (pve->binding) {
                // Request consteval
                if (pve->binding->valueState == HIR::Constant::ValueState::Unknown) {
                    MonomorphState unusedMs(mResolve.crate.types);
                    const HIR::GenericParams* implDef = nullptr;
                    auto v = mResolve.getValue(sp, pve->path, unusedMs, false, &implDef);
                    ConvertHIRConstantEvaluateConstant(mResolve.crate, implDef, pve->path, const_cast<HIR::Constant&>(*pve->binding));
                }
                ASSERT_BUG(sp, pve->binding->valueState == HIR::Constant::ValueState::Known, "Match with an unresolved constant - " << pve->path);
                this->appendFromLit(sp, pve->binding->valueRes, ty);
                for (size_t i = 0; i < pat.implicitDerefCount; i++) {
                    fieldPath.pop_back();
                }
                return;
            } else {
                TODO(sp, "Match with an unbound constant - " << pve->path);
            }
        }
    }

    if (pat.mData.is_Or()) {
        // Multiply the current pattern (sub)set out, visit with sub-sets
        const auto& e = pat.mData.as_Or();
        assert(pat.implicitDerefCount == 0); // Shouldn't have any, so this code doesn't need to pop them.
        assert(e.size() > 0);
        this->multiplyRulesets(e.size(), [&](size_t i) {
            this->appendFrom(sp, e[i], top_ty);
        });
        return;
    }

    TU_MATCH_HDRA( (*ty), {)
    TU_ARMA(Infer, e) {
            BUG(sp, "Ivar for in match type");
        }
        TU_ARMA(Diverge, e) {
            // Since ! can never exist, mark this arm as impossible.
            // TODO: Marking as impossible (and not emitting) leads to exhuaustiveness failure.
            //this->m_is_impossible = true;
        }
        TU_ARMA(Primitive, e) {
        TU_MATCH_HDR( (pat.mData), {)
        default:
            BUG(sp, "Matching primitive with invalid pattern - " << pat);
                TU_ARM(pat.mData, Any, pe) {
                    this->pushRule(PatternRule::make_Any({}));
                }
                TU_ARM(pat.mData, Range, pe) {
                    if (!pe.start || !pe.end) {
                        assert(pe.start || pe.end);
                        if (pe.start) {
                            this->pushRule(
                                PatternRule::make_ValueRange({
                                    H::getPatternValue(sp, pat, *pe.start, e),
                                    H::getPatternValueMax(sp, pat, e),
                                    true // Inclusive always
                                })
                            );
                        } else {
                            this->pushRule(PatternRule::make_ValueRange({H::getPatternValueMin(sp, pat, e), H::getPatternValue(sp, pat, *pe.end, e), pe.isInclusive}));
                        }
                    } else {
                        this->pushRule(PatternRule::make_ValueRange({H::getPatternValue(sp, pat, *pe.start, e), H::getPatternValue(sp, pat, *pe.end, e), pe.isInclusive}));
                    }
                }
                TU_ARM(pat.mData, Value, pe) {
                    switch (e) {
                        case ::HIR::CoreType::Bool:
                            // TODO: Support values from `const` too
                            this->pushRule(PatternRule::make_Bool(pe.val.as_Integer().value != 0));
                            break;
                        default:
                            this->pushRule(H::getPatternValue(sp, pat, pe.val, e));
                            break;
                    }
                }
        }
        }
        TU_ARMA(Tuple, e) {
            fieldPath.push_back(0);
            TU_MATCH_DEF(
                ::HIR::Pattern::Data,
                (pat.mData),
                (pe),
                (BUG(sp, "Matching tuple with invalid pattern - " << pat);),
                (Any,
                 // TODO: Avoid storing the empty patterns, to save on space/cost
                 for (const auto& sty : e) {
                     this->appendFrom(sp, emptyPattern, sty);
                     fieldPath.back()++;
                 }),
                (
                    Tuple, assert(e.size() == pe.subPatterns.size()); for (unsigned int i = 0; i < e.size(); i++) {
                        this->appendFrom(sp, pe.subPatterns[i], e[i]);
                        fieldPath.back()++;
                    }
                ),
                (SplitTuple, assert(e.size() >= pe.leading.size() + pe.trailing.size()); unsigned trailingStart = e.size() - pe.trailing.size(); for (unsigned int i = 0; i < e.size(); i++) {
                    if (i < pe.leading.size()) {
                        this->appendFrom(sp, pe.leading[i], e[i]);
                    } else if (i < trailingStart) {
                        this->appendFrom(sp, emptyPattern, e[i]);
                    } else {
                        this->appendFrom(sp, pe.trailing[i - trailingStart], e[i]);
                    }
                    fieldPath.back()++;
                })
            )
            fieldPath.pop_back();
        }
        TU_ARMA(Path, e) {
            struct PH {
                static void pushPatternTuple(PatternRulesetBuilder& builder, const Span& sp, const ::HIR::Pattern::Data::Data_PathTuple& pe, std::function<const HIR::TypeData*(const HIR::TypeData*)> maybeMonomorph) {
                    const auto& sd = ::HIR::patternGetTuple(sp, pe.path, pe.binding);
                    assert(sd.size() >= pe.leading.size() + pe.trailing.size());
                    size_t trailingStart = sd.size() - pe.trailing.size();
                    for (unsigned int i = 0; i < sd.size(); i++) {
                        const auto& fld = sd[i];

                        if (i < pe.leading.size()) {
                            builder.appendFrom(sp, pe.leading[i], maybeMonomorph(fld.ent));
                        } else if (i < trailingStart) {
                            builder.appendFrom(sp, emptyPattern, maybeMonomorph(fld.ent));
                        } else {
                            builder.appendFrom(sp, pe.trailing[i - trailingStart], maybeMonomorph(fld.ent));
                        }
                        builder.fieldPath.back()++;
                    }
                }
                static void pushPatternStruct(PatternRulesetBuilder& builder, const Span& sp, const ::HIR::Pattern::Data::Data_PathNamed& pe, std::function<const HIR::TypeData*(const HIR::TypeData*)> maybeMonomorph) {
                    const auto& sd = ::HIR::patternGetNamed(sp, pe.path, pe.binding);
                    // NOTE: Iterates in field order (not pattern order) to ensure that patterns are in order between arms
                    for (const auto& fld : sd) {
                        const auto& styMono = maybeMonomorph(fld.ty);

                        auto it = ::std::find_if(pe.subPatterns.begin(), pe.subPatterns.end(), [&](const auto& x) {
                            return x.first == fld.name;
                        });
                        if (it == pe.subPatterns.end()) {
                            builder.appendFrom(sp, emptyPattern, styMono);
                        } else {
                            builder.appendFrom(sp, it->second, styMono);
                        }
                        builder.fieldPath.back()++;
                    }
                }
            };
            ::HIR::TypeRef tmp;
            auto maybeMonomorph = [&](const ::HIR::TypeData* ty) -> const ::HIR::TypeData* {
                if (monomorphiseTypeNeeded(ty)) {
                    tmp = MonomorphStatePtr(mResolve.crate.types, nullptr, &e.path.mData.as_Generic().mParams, nullptr).monomorphType(sp, ty);
                    this->mResolve.expandAssociatedTypes(sp, tmp);
                    return tmp;
                } else {
                    return ty;
                }
            };
            // This is either a struct destructure or an enum
        TU_MATCH_HDRA( (e.binding), {)
        TU_ARMA(Unbound, pbe) {
                    BUG(sp, "Encounterd unbound path - " << e.path);
                }
                TU_ARMA(Opaque, be) {
                    TU_MATCH_DEF(::HIR::Pattern::Data, (pat.mData), (pe), (BUG(sp, "Matching opaque type with invalid pattern - " << pat);), (Any, this->pushRule(PatternRule::make_Any({}));))
                }
                TU_ARMA(Struct, pbe) {
                    const auto& strData = pbe->mData;

                    if (mLangBox && e.path.mData.as_Generic().mPath == *mLangBox) {
                        const auto& innerTy = e.path.mData.as_Generic().mParams.types.at(0);
                        TU_MATCH_DEF(
                            ::HIR::Pattern::Data,
                            (pat.mData),
                            (pe),
                            (BUG(sp, "Match not allowed, " << ty << " with " << pat);),
                            (Any,
                             // _ on a box, recurse into the box type.
                             fieldPath.push_back(FIELD_DEREF);
                             this->appendFrom(sp, emptyPattern, innerTy);
                             fieldPath.pop_back();),
                            (Box, fieldPath.push_back(FIELD_DEREF); this->appendFrom(sp, *pe.sub, innerTy); fieldPath.pop_back();)
                        )
                        break;
                    }
            TU_MATCH_HDRA( (strData), {)
            TU_ARMA(Unit, sd) {
                TU_MATCH_HDRA( (pat.mData), {)
                default:
                    BUG(sp, "Match not allowed, " << ty <<  " with " << pat);
                                TU_ARMA(Any, pe) {
                                    // _ on a unit-like type, unconditional
                                }
                                TU_ARMA(PathValue, pe) {
                                    // Unit-like struct value, nothing to match (it's unconditional)
                                }
                                TU_ARMA(Value, pe) {
                                    // Unit-like struct value, nothing to match (it's unconditional)
                                }
                                TU_ARMA(PathNamed, pe) {
                                    ASSERT_BUG(sp, pe.subPatterns.size() == 0, "Matching unit-like struct with sub-patterns - " << pat);
                                }
                }
                        }
                        TU_ARMA(Tuple, sd) {
                            fieldPath.push_back(0);
                TU_MATCH_HDRA( (pat.mData), {)
                default:
                    BUG(sp, "Match not allowed, " << ty <<  " with " << pat);
                                TU_ARMA(Any, pe) {
                                    // - Recurse into type using an empty pattern
                                    for (const auto& fld : sd) {
                                        ASSERT_BUG(sp, fieldPath.back() < FIELD_INDEX_MAX, "Too-large struct field index");
                                        this->appendFrom(sp, emptyPattern, maybeMonomorph(fld.ent));
                                        fieldPath.back()++;
                                    }
                                }
                                TU_ARMA(PathNamed, pe) {
                                    // Only allow with an empty tuple (assuming that the pattern is also empty)... or if the pattern is a wildcard
                                    if (sd.size() != 0 && !pe.isWildcard()) {
                                        BUG(sp, "Match not allowed, " << ty << " with " << pat);
                                    }
                                    for (const auto& fld : sd) {
                                        ASSERT_BUG(sp, fieldPath.back() < FIELD_INDEX_MAX, "Too-large struct field index");
                                        this->appendFrom(sp, emptyPattern, maybeMonomorph(fld.ent));
                                        fieldPath.back()++;
                                    }
                                }
                                TU_ARMA(PathTuple, pe) {
                                    assert(pe.binding.is_Struct());
                                    PH::pushPatternTuple(*this, sp, pe, maybeMonomorph);
                                }
                }
                fieldPath.pop_back();
                        }
                        TU_ARMA(Named, sd) {
                TU_MATCH_HDRA( (pat.mData), {)
                default:
                    BUG(sp, "Match not allowed, " << ty <<  " with " << pat);
                                TU_ARMA(Any, pe) {
                                    fieldPath.push_back(0);
                                    for (const auto& fld : sd) {
                                        ASSERT_BUG(sp, fieldPath.back() < FIELD_INDEX_MAX, "Too-large struct field index");
                                        this->appendFrom(sp, emptyPattern, maybeMonomorph(fld.ty));
                                        fieldPath.back()++;
                                    }
                                    fieldPath.pop_back();
                                }
                                TU_ARMA(PathNamed, pe) {
                                    assert(pe.binding.is_Struct());
                                    fieldPath.push_back(0);
                                    PH::pushPatternStruct(*this, sp, pe, maybeMonomorph);
                                    fieldPath.pop_back();
                                }
                }
                        }
            }
                }
                TU_ARMA(Union, pbe) {
            TU_MATCH_HDRA( (pat.mData), {)
            default:
                TODO(sp, "Match over union - " << ty << " with " << pat);
                        TU_ARMA(Any, pe) {
                            this->pushRule(PatternRule::make_Any({}));
                        }
                        TU_ARMA(PathNamed, pe) {
                            ASSERT_BUG(sp, pe.binding.is_Union() && pe.binding.as_Union() == pbe, "Union pattern binding mismatch");
                            ASSERT_BUG(sp, pe.subPatterns.size() == 1, "Union pattern must select exactly one field");

                            const auto& fieldPattern = pe.subPatterns.front();
                            auto fieldIt = ::std::find_if(pbe->mVariants.begin(), pbe->mVariants.end(), [&](const auto& field) {
                                return field.name == fieldPattern.first;
                            });
                            ASSERT_BUG(sp, fieldIt != pbe->mVariants.end(), "Unable to find union field " << fieldPattern.first);

                            const auto fieldIndex = static_cast<unsigned>(fieldIt - pbe->mVariants.begin());
                            ASSERT_BUG(sp, fieldIndex < FIELD_INDEX_MAX, "Too-large union field index");
                            fieldPath.push_back(fieldIndex);
                            this->appendFrom(sp, fieldPattern.second, maybeMonomorph(fieldIt->ty));
                            fieldPath.pop_back();
                        }
            }
                }
                TU_ARMA(ExternType, pbe) {
            TU_MATCH_HDRA( (pat.mData), {)
            default:
                BUG(sp, "Match not allowed, " << ty <<  " with " << pat);
                        TU_ARMA(Any, pe) {
                            this->pushRule(PatternRule::make_Any({}));
                        }
            }
                }
                TU_ARMA(Enum, pbe) {
            TU_MATCH_HDRA( (pat.mData), {)
            default:
                BUG(sp, "Match not allowed, " << ty <<  " with " << pat);
                        TU_ARMA(Any, pe) {
                            this->pushRule(PatternRule::make_Any({}));
                        }
                        TU_ARMA(Value, pe) {
                            if (!pe.val.is_Named()) {
                                BUG(sp, "Match not allowed, " << ty << " with " << pat);
                            }
                            // TODO: If the value of this constant isn't known at this point (i.e. it won't be until monomorphisation)
                            //       emit a special type of rule.
                            TODO(sp, "Match enum with const - " << pat);
                        }
                        TU_ARMA(PathValue, pe) {
                            assert(pe.binding.is_Enum());
                            this->pushRule(PatternRule::make_Variant({pe.binding.as_Enum().varIdx, {}}));
                        }
                        TU_ARMA(PathTuple, pe) {
                            assert(pe.binding.is_Enum());
                            const auto& be = pe.binding.as_Enum();

                            PatternRulesetBuilder subBuilder{this->mResolve};
                            subBuilder.fieldPath = fieldPath;
                            ASSERT_BUG(sp, be.varIdx < FIELD_INDEX_MAX, "Too-large variant index in " << ty);
                            subBuilder.fieldPath.push_back(be.varIdx);
                            subBuilder.fieldPath.push_back(0);

                            PH::pushPatternTuple(subBuilder, sp, pe, maybeMonomorph);

                            this->multiplyRulesets(subBuilder.rulesets.size(), [&](size_t i) {
                                auto& sr = subBuilder.rulesets[i];
                                if (sr.isImpossible) {
                                    this->setImpossible();
                                }
                                this->pushRule(PatternRule::make_Variant({be.varIdx, mv$(sr.rules)}));
                                this->pushBindings(mv$(sr.mBindings));
                            });
                        }
                        TU_ARMA(PathNamed, pe) {
                            assert(pe.binding.is_Enum());
                            const auto& be = pe.binding.as_Enum();

                            PatternRulesetBuilder subBuilder{this->mResolve};
                            subBuilder.fieldPath = fieldPath;
                            ASSERT_BUG(sp, be.varIdx < FIELD_INDEX_MAX, "Too-large variant index");
                            subBuilder.fieldPath.push_back(be.varIdx);
                            subBuilder.fieldPath.push_back(0);

                            // Empty variants can be matched with `Var { [..] }` even if they're not struct-like
                            if (be.ptr->isValue()) {
                                assert(pe.subPatterns.empty());
                            } else if (be.ptr->mData.as_Data().at(be.varIdx).type == mResolve.crate.types.unit()) {
                                assert(pe.subPatterns.empty());
                            } else if (!be.ptr->mData.as_Data().at(be.varIdx).is_struct) {
                                assert(pe.subPatterns.empty());
                                const auto& sd = ::HIR::patternGetTuple(sp, pe.path, pe.binding);
                                for (unsigned int i = 0; i < sd.size(); i++) {
                                    const auto& fld = sd[i];
                                    subBuilder.appendFrom(sp, emptyPattern, maybeMonomorph(fld.ent));
                                    subBuilder.fieldPath.back()++;
                                }
                            } else {
                                PH::pushPatternStruct(subBuilder, sp, pe, maybeMonomorph);
                            }

                            this->multiplyRulesets(subBuilder.rulesets.size(), [&](size_t i) {
                                auto& sr = subBuilder.rulesets[i];
                                if (sr.isImpossible) {
                                    this->setImpossible();
                                }
                                this->pushRule(PatternRule::make_Variant({be.varIdx, mv$(sr.rules)}));
                                this->pushBindings(mv$(sr.mBindings));
                            });
                        }
            }
                }
        }
        }
        TU_ARMA(Generic, e) {
            // Generics don't destructure, so the only valid pattern is `_`
            TU_MATCH_DEF(::HIR::Pattern::Data, (pat.mData), (pe), (BUG(sp, "Match not allowed, " << ty << " with " << pat);), (Any, this->pushRule(PatternRule::make_Any({}));))
        }
        TU_ARMA(TraitObject, e) {
            if (pat.mData.is_Any()) {
            } else {
                ERROR(sp, E0000, "Attempting to match over a trait object");
            }
        }
        TU_ARMA(ErasedType, e) {
            if (pat.mData.is_Any()) {
            } else {
                ERROR(sp, E0000, "Attempting to match over an erased type");
            }
        }
        TU_ARMA(Array, e) {
            // If the size is unknown, just push a `_` pattern.
            // OR: don't push anything?
            if (!e.size.is_Known()) {
                DEBUG("Matching over unknown-sized array - " << e.size);
                ASSERT_BUG(sp, pat.mData.is_Any(), "Matching generic-sized array with non `_` pattern - " << pat);
                this->pushRule(PatternRule::make_Any({}));
                break;
            }
            // Sequential match just like tuples.
            fieldPath.push_back(0);
        TU_MATCH_HDRA( (pat.mData), {)
        default:
            BUG(sp, "Matching array with invalid pattern - " << pat);
                TU_ARMA(Any, pe) {
                    for (unsigned int i = 0; i < e.size.as_Known(); i++) {
                        this->appendFrom(sp, emptyPattern, e.inner);
                        fieldPath.back()++;
                    }
                }
                TU_ARMA(Slice, pe) {
                    ASSERT_BUG(sp, e.size.as_Known() == pe.subPatterns.size(), "Pattern size mismatch");
                    for (const auto& v : pe.subPatterns) {
                        this->appendFrom(sp, v, e.inner);
                        fieldPath.back()++;
                    }
                }
                TU_ARMA(SplitSlice, pe) {
                    ASSERT_BUG(sp, pe.leading.size() < FIELD_INDEX_MAX, "Too many leading slice rules to fit encodng");
                    const auto arraySize = e.size.as_Known();
                    ASSERT_BUG(sp, pe.leading.size() <= arraySize, "Too many leading slice rules for array type");
                    ASSERT_BUG(sp, pe.trailing.size() <= arraySize - pe.leading.size(), "Too many slice rules for array type");
                    for (const auto& subpat : pe.leading) {
                        this->appendFrom(sp, subpat, e.inner);
                        fieldPath.back()++;
                    }
                    while (fieldPath.back() < arraySize - pe.trailing.size()) {
                        this->appendFrom(sp, emptyPattern, e.inner);
                        fieldPath.back()++;
                    }
                    for (const auto& subpat : pe.trailing) {
                        this->appendFrom(sp, subpat, e.inner);
                        fieldPath.back()++;
                    }

                    if (pe.extraBind.isValid()) {
                        ASSERT_BUG(sp, pe.extraBind.implicitDerefCount == 0, "");
                        PatternBinding pb(fieldPath, pe.extraBind);
                        pb.field.pop_back();
                        pb.splitSlice = std::make_pair(pe.leading.size(), pe.trailing.size());
                        this->pushBinding(mv$(pb));
                    }
                }
        }
        fieldPath.pop_back();
        }
        TU_ARMA(Slice, e) {
        TU_MATCH_HDRA( (pat.mData), {)
        default:
            BUG(sp, "Matching over [T] with invalid pattern - " << pat);
                TU_ARMA(Any, pe) {
                    this->pushRule(PatternRule::make_Any({}));
                }
                TU_ARMA(Slice, pe) {
                    // Sub-patterns
                    PatternRulesetBuilder subBuilder{this->mResolve};
                    subBuilder.fieldPath = fieldPath;
                    subBuilder.fieldPath.push_back(0);
                    ASSERT_BUG(sp, pe.subPatterns.size() < FIELD_INDEX_MAX, "Too many slice rules to fit encodng");
                    for (const auto& subpat : pe.subPatterns) {
                        subBuilder.appendFrom(sp, subpat, e.inner);
                        subBuilder.fieldPath.back()++;
                    }

                    // Encodes length check and sub-pattern rules
                    this->multiplyRulesets(subBuilder.rulesets.size(), [&](size_t i) {
                        auto& sr = subBuilder.rulesets[i];
                        if (sr.isImpossible) {
                            this->setImpossible();
                        }
                        this->pushRule(PatternRule::make_Slice({static_cast<unsigned int>(pe.subPatterns.size()), mv$(sr.rules)}));
                        this->pushBindings(mv$(sr.mBindings));
                    });
                }
                TU_ARMA(SplitSlice, pe) {
                    PatternRulesetBuilder subBuilder{this->mResolve};
                    subBuilder.fieldPath = fieldPath;
                    ASSERT_BUG(sp, pe.leading.size() < FIELD_INDEX_MAX, "Too many leading slice rules to fit encodng");
                    subBuilder.fieldPath.push_back(0);
                    for (const auto& subpat : pe.leading) {
                        subBuilder.appendFrom(sp, subpat, e.inner);
                        subBuilder.fieldPath.back()++;
                    }
                    auto leadingRulesets = mv$(subBuilder.rulesets);
                    subBuilder.rulesets.clear();
                    subBuilder.rulesets.resize(1);
                    subBuilder.subsetStart = 0;
                    subBuilder.subsetEnd = 1;

                    if (pe.trailing.size()) {
                        // Needs a way of encoding the negative offset in the field path
                        // - For now, just use a very high number (and assert that it's not more than 128)
                        ASSERT_BUG(sp, pe.trailing.size() < FIELD_INDEX_MAX, "Too many trailing slice rules to fit encodng");
                        subBuilder.fieldPath.back() = FIELD_INDEX_MAX + (FIELD_INDEX_MAX - pe.trailing.size());
                        for (const auto& subpat : pe.trailing) {
                            subBuilder.appendFrom(sp, subpat, e.inner);
                            subBuilder.fieldPath.back()++;
                        }
                    }
                    auto trailingRulesets = mv$(subBuilder.rulesets);

                    if (pe.extraBind.isValid()) {
                        ASSERT_BUG(sp, pe.extraBind.implicitDerefCount == 0, "");
                        PatternBinding pb(fieldPath, pe.extraBind);
                        pb.splitSlice = std::make_pair(pe.leading.size(), pe.trailing.size());
                        this->pushBinding(mv$(pb));
                    }

                    this->multiplyRulesets(leadingRulesets.size() * trailingRulesets.size(), [&](size_t i) {
                        size_t iL = i % leadingRulesets.size();
                        size_t iT = i / leadingRulesets.size();
                        auto& srL = leadingRulesets[iL];
                        auto& srT = trailingRulesets[iT];
                        if (srL.isImpossible || srT.isImpossible) {
                            this->setImpossible();
                        }

                        auto rulesL = srL.clone();
                        auto rulesT = srT.clone();
                        this->pushRule(PatternRule::make_SplitSlice({static_cast<unsigned int>(pe.leading.size() + pe.trailing.size()), static_cast<unsigned int>(pe.trailing.size()), mv$(rulesL.rules), mv$(rulesT.rules)}));
                        this->pushBindings(mv$(rulesL.mBindings));
                        this->pushBindings(mv$(rulesT.mBindings));
                    });
                }
        }
        }
        TU_ARMA(Borrow, e) {
            fieldPath.push_back(FIELD_DEREF);
        TU_MATCH_HDR( (pat.mData), {)
        default:
            BUG(sp, "Matching borrow invalid pattern - " << ty << " with " << pat);
                TU_ARM(pat.mData, Any, pe) {
                    this->appendFrom(sp, emptyPattern, e.inner);
                }
                TU_ARM(pat.mData, Ref, pe) {
                    this->appendFrom(sp, *pe.sub, e.inner);
                }
                TU_ARM(pat.mData, Value, pe) {
                    // TODO: Check type?
                    if (pe.val.is_String()) {
                        const auto& s = pe.val.as_String();
                        this->pushRule(PatternRule::make_Value(s));
                    } else if (pe.val.is_ByteString()) {
                        const auto& s = pe.val.as_ByteString().v;
                        // When matching a fixed-size array, expand to per-element rules so the
                        // field paths line up with `[a, b, ...]` patterns in sibling arms.
                        if (e.inner->is_Array()) {
                            const auto& ae = e.inner->as_Array();
                            ASSERT_BUG(sp, ae.size.is_Known() && ae.size.as_Known() == s.size(), "Byte string pattern size mismatch - " << pat << " vs " << e.inner);
                            fieldPath.push_back(0);
                            for (auto c : s) {
                                this->pushRule(PatternRule::make_Value(::MIR::Constant::make_Uint({U128(static_cast<uint8_t>(c)), ::HIR::CoreType::U8})));
                                fieldPath.back()++;
                            }
                            fieldPath.pop_back();
                        } else {
                            ::std::vector<uint8_t> data;
                            data.reserve(s.size());
                            for (auto c : s) {
                                data.push_back(c);
                            }

                            this->pushRule(PatternRule::make_Value(mv$(data)));
                        }
                    }
                    // TODO: Handle named values
                    else {
                        BUG(sp, "Matching borrow invalid pattern - " << pat);
                    }
                }
        }
        fieldPath.pop_back();
        }
        TU_ARMA(Pointer, e) {
            if (pat.mData.is_Any()) {
                this->pushRule(PatternRule::make_Any({}));
            } else {
                ERROR(sp, E0000, "Attempting to match over a pointer");
            }
        }
        TU_ARMA(NamedFunction, e) {
            if (pat.mData.is_Any()) {
            } else {
                ERROR(sp, E0000, "Attempting to match over a functon pointer");
            }
        }
        TU_ARMA(Function, e) {
            if (pat.mData.is_Any()) {
            } else {
                ERROR(sp, E0000, "Attempting to match over a functon pointer");
            }
        }
        TU_ARMA(NodeType, e) {
            if (pat.mData.is_Any()) {
            } else {
                ERROR(sp, E0000, "Attempting to match over a closure/generator/async");
            }
        }
    }
    for(size_t i = 0; i < pat.implicitDerefCount; i ++)
    {
        fieldPath.pop_back();
    }
}

namespace {
    // Order rules ignoring inner rules
    Ordering ordRuleCompatible(const PatternRule& a, const PatternRule& b) {
        if (a.tag() != b.tag()) {
            return ::ord((unsigned)a.tag(), (unsigned)b.tag());
        }

        TU_MATCH_HDRA( (a, b), { )
        TU_ARMA(Any, ae, be) {
                return OrdEqual;
            }
            TU_ARMA(Variant, ae, be) {
                return ::ord(ae.idx, be.idx);
            }
            TU_ARMA(Slice, ae, be) {
                return ::ord(ae.len, be.len);
            }
            TU_ARMA(SplitSlice, ae, be) {
                ORD(ae.leading, be.leading);
                // TODO: lengths?
                ORD(ae.trailing, be.trailing);
                return OrdEqual;
            }
            TU_ARMA(Bool, ae, be) {
                return ::ord(ae, be);
            }
            TU_ARMA(Value, ae, be) {
                return ::ord(ae, be);
            }
            TU_ARMA(ValueRange, ae, be) {
                ORD(ae.first, be.first);
                ORD(ae.last, be.last);
                return ::ord(ae.isInclusive, be.isInclusive);
            }
        }
        throw "";
    }

    bool ruleCompatible(const PatternRule& a, const PatternRule& b) {
        return ordRuleCompatible(a, b) == OrdEqual;
    }

    bool rulesOverlap(const PatternRule& a, const PatternRule& b) {
        if (a.is_Any() || b.is_Any()) {
            return true;
        }

        // Defensive: If a constant is encountered, assume it overlaps with anything
        if (const auto* ae = a.opt_Value()) {
            if (ae->is_Const()) {
                return true;
            }
        }
        if (const auto* be = b.opt_Value()) {
            if (be->is_Const()) {
                return true;
            }
        }

        // A byte-string literal denotes a slice with one exact length.  For
        // reordering purposes, sequence patterns can only be proven disjoint
        // from it when their accepted length domains do not contain that
        // length; their element rules may otherwise accept the literal.
        if (const auto* ae = a.opt_Value(); ae && ae->is_Bytes()) {
            if (const auto* be = b.opt_Slice()) {
                return ae->as_Bytes().size() == be->len;
            }
            if (const auto* be = b.opt_SplitSlice()) {
                return ae->as_Bytes().size() >= be->minLen;
            }
        }
        if (const auto* be = b.opt_Value(); be && be->is_Bytes()) {
            if (const auto* ae = a.opt_Slice()) {
                return be->as_Bytes().size() == ae->len;
            }
            if (const auto* ae = a.opt_SplitSlice()) {
                return be->as_Bytes().size() >= ae->minLen;
            }
        }

        // Checks if the value is within the righthand edge of the range
        auto isWithinRight = [](const MIR::Constant& c, const PatternRule::Data_ValueRange& e) -> bool {
            return (e.isInclusive ? c <= e.last : c < e.last);
        };

        // Value Range: Overlaps with contained values.
        if (const auto* ae = a.opt_ValueRange()) {
            if (const auto* be = b.opt_Value()) {
                return (ae->first <= *be && isWithinRight(*be, *ae));
            } else if (const auto* be = b.opt_ValueRange()) {
                // First ends before the second starts (or vice-versa): Disjoint
                if (ae->last < be->first) {
                    return false;
                }
                if (be->last < ae->first) {
                    return false;
                }
                // If the starts are the same (always inclusive) then overlap
                if (ae->first == be->first) {
                    return true;
                }

                //auto check_ends = []( const PatternRule::Data_ValueRange& lo, const PatternRule::Data_ValueRange& hi)->bool {
                //    return lo.is_inclusive == hi.is_inclusive ? lo.last <= hi.last
                //        : (lo.is_inclusive
                //            ? lo.last < hi.last // Lower side is inclusive, higher side exlusive - must be less than higher side
                //            : throw "TODO" // Lower side is excl, higher side incl - lower+1 < higher = lower < higher-1 = lower
                //            );
                //    };
                ASSERT_BUG(Span(), ae->isInclusive && be->isInclusive, "TODO: Handle overlap with exclusive ranges: " << ae->first << ".." << (ae->isInclusive ? "=" : "") << ae->last << " and " << be->first << ".." << (be->isInclusive ? "=" : "") << be->last);
                assert(ae->isInclusive && "TODO: Exclusive ranges");
                assert(be->isInclusive && "TODO: Exclusive ranges");
                // Start of B within A
                if (ae->first <= be->first && isWithinRight(be->first, *ae)) {
                    return true;
                }
                // End of B within A
                if (isWithinRight(ae->first, *be) && be->last <= ae->last) { // TODO: Right-exclusive (if equal type then original check, otherwise complex)
                    return true;
                }
                // Start of A within B
                if (be->first <= ae->first && isWithinRight(ae->first, *be)) {
                    return true;
                }
                // End of A within B
                if (isWithinRight(be->first, *ae) && ae->last <= be->last) { // TODO: Right-exclusive
                    return true;
                }

                // Disjoint
                return false;
            } else {
                TODO(Span(), "Check overlap of " << a << " and " << b);
            }
        }
        if (const auto* be = b.opt_ValueRange()) {
            if (const auto* ae = a.opt_Value()) {
                if (be->isInclusive) {
                    return (be->first <= *ae && *ae <= be->last);
                } else {
                    return (be->first <= *ae && *ae < be->last);
                }
            }
            // Note: A can't be ValueRange
            else {
                TODO(Span(), "Check overlap of " << a << " and " << b);
            }
        }

        // SplitSlice patterns overlap with other SplitSlice patterns and larger slices
        if (const auto* ae = a.opt_SplitSlice()) {
            if (b.is_SplitSlice()) {
                return true;
            } else if (const auto* be = b.opt_Slice()) {
                return be->len >= ae->minLen;
            } else {
                TODO(Span(), "Check overlap of " << a << " and " << b);
            }
        }
        if (const auto* be = b.opt_SplitSlice()) {
            if (const auto* ae = a.opt_Slice()) {
                return ae->len >= be->minLen;
            } else {
                TODO(Span(), "Check overlap of " << a << " and " << b);
            }
        }

        // Otherwise, If rules are approximately equal, they overlap
        return (ordRuleCompatible(a, b) == OrdEqual);
    }
}

void sortRulesets(RulesetRef rulesets, size_t idx) {
    if (rulesets.size() < 2) {
        return;
    }

    // NOTE: Assumption kinda breaks with byte string literals
    //for(size_t i = 0; i < rulesets.size(); i ++)
    //    assert(rulesets[i].size() == rulesets[0].size());

    // Multiple rules, but no checks within then (can happen with `match () { _ if foo => ..., _ => ... }`)
    if (rulesets[0].size() == 0) {
        return;
    }

    bool foundNonAny = false;
    for (size_t i = 0; i < rulesets.size(); i++) {
        assert(idx < rulesets[i].size());
        if (!rulesets[i][idx].is_Any()) {
            foundNonAny = true;
        }
    }
    if (foundNonAny) {
        TRACE_FUNCTION_F(idx);
        for (size_t i = 0; i < rulesets.size(); i++) {
            DEBUG("- " << i << ": " << rulesets[i]);
        }

        bool actionTaken;
        do {
            actionTaken = false;
            for (size_t i = 0; i < rulesets.size() - 1; i++) {
                if (rulesOverlap(rulesets[i][idx], rulesets[i + 1][idx])) {
                    // Don't move
                } else if (ordRuleCompatible(rulesets[i][idx], rulesets[i + 1][idx]) == OrdGreater) {
                    rulesets.swap(i, i + 1);
                    actionTaken = true;
                } else {
                }
            }
        } while (actionTaken);
        for (size_t i = 0; i < rulesets.size(); i++) {
            DEBUG("- " << i << ": " << rulesets[i]);
        }

        // TODO: Print sorted ruleset

        // Where compatible, sort insides
        size_t start = 0;
        for (size_t i = 1; i < rulesets.size(); i++) {
            if (ordRuleCompatible(rulesets[i][idx], rulesets[start][idx]) != OrdEqual) {
                sortRulesetsInner(rulesets.slice(start, i - start), idx);
                start = i;
            }
        }
        sortRulesetsInner(rulesets.slice(start, rulesets.size() - start), idx);

        // Iterate onwards where rules are equal
        if (idx + 1 < rulesets[0].size()) {
            size_t start = 0;
            for (size_t i = 1; i < rulesets.size(); i++) {
                if (rulesets[i][idx] != rulesets[start][idx]) {
                    sortRulesets(rulesets.slice(start, i - start), idx + 1);
                    start = i;
                }
            }
            sortRulesets(rulesets.slice(start, rulesets.size() - start), idx + 1);
        }
    } else {
        if (idx + 1 < rulesets[0].size()) {
            sortRulesets(rulesets, idx + 1);
        }
    }
}

void sortRulesetsInner(RulesetRef rulesets, size_t idx) {
    TRACE_FUNCTION_F(idx << " - " << rulesets[0][idx].tagStr());
    if (const auto* re = rulesets[0][idx].opt_Variant()) {
        // Sort rules based on contents of enum
        if (re->subRules.size() > 0) {
            sortRulesets(RulesetRef(rulesets, idx), 0);
        }
    }
}

namespace {
    void getTyAndVal(
        const Span& sp,
        MirBuilder& builder,
        const ::HIR::TypeData* top_ty,
        const ::MIR::LValue& top_val,
        const fieldPathT& field_path,
        unsigned int field_path_ofs,
        /*Out ->*/ ::HIR::TypeRef& outTy,
        ::MIR::LValue& outVal
    ) {
        const StaticTraitResolve& resolve = builder.resolve();
        ::MIR::LValue lval = top_val.clone();
        ::HIR::TypeRef tmpTy;
        const ::HIR::TypeData* curTy = top_ty;

        // TODO: Cache the correspondance of path->type (lval can be inferred)
        ASSERT_BUG(sp, field_path_ofs <= field_path.size(), "Field path offset " << field_path_ofs << " is larger than the path [" << field_path << "]");
        for (unsigned int i = field_path_ofs; i < field_path.size(); i++) {
            unsigned idx = field_path.data[i];
            DEBUG("> " << curTy << " #" << idx);

            TU_MATCH_HDRA( (*curTy), {)
            TU_ARMA(Infer, e)   BUG(sp, "Ivar for in match type");
                TU_ARMA(Diverge, e) BUG(sp, "Diverge in match type");
                TU_ARMA(Primitive, e) BUG(sp, "Destructuring a primitive");
                TU_ARMA(Tuple, e) {
                    ASSERT_BUG(sp, idx < e.size(), "Tuple index out of range");
                    lval = ::MIR::LValue::newField(mv$(lval), idx);
                    curTy = e[idx];
                }
                TU_ARMA(Path, e) {
                    if (idx == FIELD_DEREF) {
                        auto newTy = resolve.isTypeOwnedBox(curTy);
                        ASSERT_BUG(sp, newTy, "Deref on non-Box - " << curTy);
                        lval = ::MIR::LValue::newDeref(mv$(lval));
                        curTy = newTy;
                        break;
                    }
                    auto monomorphToPtr = [&](const ::HIR::TypeData* ty) -> const ::HIR::TypeData* {
                        if (monomorphiseTypeNeeded(ty)) {
                            auto rv = MonomorphStatePtr(resolve.crate.types, nullptr, &e.path.mData.as_Generic().mParams, nullptr).monomorphType(sp, ty);
                            resolve.expandAssociatedTypes(sp, rv);
                            tmpTy = mv$(rv);
                            return tmpTy;
                        } else {
                            return ty;
                        }
                    };
                TU_MATCH_HDRA( (e.binding), {)
                TU_ARMA(Unbound, pbe) {
                            BUG(sp, "Encounterd unbound path - " << e.path);
                        }
                        TU_ARMA(Opaque, pbe) {
                            BUG(sp, "Destructuring an opaque type - " << curTy);
                        }
                        TU_ARMA(ExternType, pbe) {
                            BUG(sp, "Destructuring an extern type - " << curTy);
                        }
                        TU_ARMA(Struct, pbe) {
                    TU_MATCH_HDRA( (pbe->mData), { )
                    TU_ARMA(Unit, fields) {
                                    BUG(sp, "Destructuring an unit-like tuple - " << curTy);
                                }
                                TU_ARMA(Tuple, fields) {
                                    ASSERT_BUG(sp, idx < fields.size(), "Tuple struct index (" << idx << ") out of range (" << fields.size() << ") in " << curTy);
                                    const auto& fld = fields[idx];
                                    curTy = monomorphToPtr(fld.ent);
                                    lval = ::MIR::LValue::newField(mv$(lval), idx);
                                }
                                TU_ARMA(Named, fields) {
                                    ASSERT_BUG(sp, idx < fields.size(), "Tuple struct index (" << idx << ") out of range (" << fields.size() << ") in " << curTy);
                                    const auto& fld = fields[idx];
                                    curTy = monomorphToPtr(fld.ty);
                                    lval = ::MIR::LValue::newField(mv$(lval), idx);
                                }
                    }
                        }
                        TU_ARMA(Union, pbe) {
                            ASSERT_BUG(sp, idx < pbe->mVariants.size(), "Union variant index (" << idx << ") out of range (" << pbe->mVariants.size() << ") in " << curTy);
                            const auto& fld = pbe->mVariants[idx];
                            curTy = monomorphToPtr(fld.ty);
                            lval = ::MIR::LValue::newDowncast(mv$(lval), idx);
                        }
                        TU_ARMA(Enum, pbe) {
                            ASSERT_BUG(sp, pbe->mData.is_Data(), "Value enum being destructured - " << curTy);
                            const auto& variants = pbe->mData.as_Data();
                            ASSERT_BUG(sp, idx < variants.size(), "Variant index (" << idx << ") out of range (" << variants.size() << ") for enum " << curTy);
                            const auto& var = variants[idx];

                            curTy = monomorphToPtr(var.type);
                            lval = ::MIR::LValue::newDowncast(mv$(lval), idx);
                        }
                }
                }
                TU_ARMA(Generic, e) {
                    BUG(sp, "Destructuring a generic - " << curTy);
                }
                TU_ARMA(TraitObject, e) {
                    BUG(sp, "Destructuring a trait object - " << curTy);
                }
                TU_ARMA(ErasedType, e) {
                    BUG(sp, "Destructuring an erased type - " << curTy);
                }
                TU_ARMA(Array, e) {
                    curTy = e.inner;
                    if (idx < FIELD_INDEX_MAX) {
                        ASSERT_BUG(sp, idx < e.size.as_Known(), "Index out of range");
                        lval = ::MIR::LValue::newField(mv$(lval), idx);
                    } else {
                        idx -= FIELD_INDEX_MAX;
                        idx = FIELD_INDEX_MAX - idx;
                        ASSERT_BUG(sp, idx < e.size.as_Known(), "Index out of range");
                        TODO(sp, "Index " << idx << " from end of array " << lval);
                    }
                }
                TU_ARMA(Slice, e) {
                    curTy = e.inner;
                    if (idx < FIELD_INDEX_MAX) {
                        lval = ::MIR::LValue::newField(mv$(lval), idx);
                    } else {
                        idx -= FIELD_INDEX_MAX;
                        idx = FIELD_INDEX_MAX - idx;
                        // 1. Create an LValue containing the size of this slice subtract `idx`
                        auto lenLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Usize), ::MIR::RValue::make_DstMeta({builder.getPtrToDst(sp, lval)}));
                        auto subVal = ::MIR::Param(::MIR::Constant::make_Uint({U128(idx), ::HIR::CoreType::Usize}));
                        auto ofsVal = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Usize), ::MIR::RValue::make_BinOp({mv$(lenLval), ::MIR::eBinOp::SUB, mv$(subVal)}));
                        // 2. Return _Index with that value
                        lval = ::MIR::LValue::newIndex(mv$(lval), ofsVal.as_Local());
                    }
                }
                TU_ARMA(Borrow, e) {
                    ASSERT_BUG(sp, idx == FIELD_DEREF, "Destructure of borrow doesn't correspond to a deref in the path");
                    //DEBUG(i << " " << *cur_ty << " - " << cur_ty << " " << &tmp_ty);
                    curTy = e.inner;
                    //DEBUG(i << " " << *cur_ty);
                    lval = ::MIR::LValue::newDeref(mv$(lval));
                }
                TU_ARMA(Pointer, e) {
                    ERROR(sp, E0000, "Attempting to match over a pointer");
                }
                TU_ARMA(NamedFunction, e) {
                    ERROR(sp, E0000, "Attempting to match over a functon pointer");
                }
                TU_ARMA(Function, e) {
                    ERROR(sp, E0000, "Attempting to match over a functon pointer");
                }
                TU_ARMA(NodeType, e) {
                    ERROR(sp, E0000, "Attempting to match over a magic type");
                }
            }
        }

        outTy = curTy;
        outVal = mv$(lval);
    }
}

// --------------------------------------------------------------------
// Dumb and Simple
// --------------------------------------------------------------------

void MIRLowerHIRMatchSimple(MirBuilder& builder, MirConverter& conv, ::HIR::ExprNodeMatch& node, ::MIR::LValue matchVal, tArmRules armRules, ::std::vector<ArmCode> arms_code, ::MIR::BasicBlockId firstCmpBlock) {
    TRACE_FUNCTION;

    // 1. Generate pattern matches
    builder.setCurBlock(firstCmpBlock);
    auto nextArmBb = builder.newBbUnlinked();
    size_t prevArmIdx = !armRules.empty() ? armRules[0].armIdx : 0;
    for (const auto& patRule : armRules) {
        if (patRule.armIdx != prevArmIdx) {
            DEBUG("New arm (" << prevArmIdx << " -> " << patRule.armIdx << ")");
            prevArmIdx = patRule.armIdx;
            builder.endBlock(::MIR::Terminator::make_Goto(nextArmBb));
            builder.setCurBlock(nextArmBb);
            nextArmBb = builder.newBbUnlinked();
        }
        const auto& arm = node.arms[patRule.armIdx];
        const auto& rc = arms_code[patRule.armIdx].rules[patRule.armRuleIdx];
        auto nextPatternBb = builder.newBbUnlinked();

        // 1. Check
        // - If the ruleset is empty, this is a _ arm over a value
        if (patRule.rules.size() > 0) {
            MIRLowerHIRMatchSimpleGeneratePattern(builder, arm.mCode->span(), patRule.rules.data(), patRule.rules.size(), node.mValue->resType, matchVal, 0, nextPatternBb);
        }
        builder.endBlock(::MIR::Terminator::make_Goto(rc.entry));

        // - Update the condition's failure target
        if (arms_code[patRule.armIdx].hasCondition && (patRule.armRuleIdx == 0 || rc.condFalse != arms_code[patRule.armIdx].rules[0].condFalse)) {
            builder.setCurBlock(rc.condFalse);
            // A guard belongs to this expanded pattern candidate, not to the
            // arm as a whole.  If it fails, another or-pattern candidate from
            // the same arm must still be tested before advancing to the next
            // arm.
            builder.endBlock(::MIR::Terminator::make_Goto(nextPatternBb));
        }

        builder.setCurBlock(nextPatternBb);
    }
    // - Kill the final pattern block (which is dead code)
    builder.endBlock(::MIR::Terminator::make_Unreachable({}));
    builder.setCurBlock(nextArmBb);
    builder.endBlock(::MIR::Terminator::make_Unreachable({}));
}

int MIRLowerHIRMatchSimpleGeneratePattern(MirBuilder& builder, const Span& sp, const PatternRule* rules, unsigned int numRules, const ::HIR::TypeData* top_ty, const ::MIR::LValue& top_val, unsigned int field_path_ofs, ::MIR::BasicBlockId failBb) {
    TRACE_FUNCTION_F("top_ty = " << top_ty << ", rules = [" << FMT_CB(os, for (size_t i = 0; i < numRules; i++) os << rules[i] << ",";));
    for (unsigned int ruleIdx = 0; ruleIdx < numRules; ruleIdx++) {
        const auto& rule = rules[ruleIdx];
        DEBUG("rule = " << rule);

        // Don't emit anything for '_' matches
        if (rule.is_Any()) {
            continue;
        }

        ::MIR::LValue val;
        ::HIR::TypeRef ity;

        getTyAndVal(sp, builder, top_ty, top_val, rule.field_path, field_path_ofs, ity, val);
        DEBUG("ty = " << ity << ", val = " << val);

        const auto& ty = ity;
        TU_MATCH_HDRA( (*ty), {)
        TU_ARMA(Infer, _te) {
                BUG(sp, "Hit _ in type - " << ty);
            }
            TU_ARMA(Diverge, _te) {
                BUG(sp, "Matching over !");
            }
            TU_ARMA(Primitive, te) {
                switch (te) {
                    case ::HIR::CoreType::Bool: {
                        ASSERT_BUG(sp, rule.is_Bool(), "PatternRule for bool isn't _Bool");
                        bool testVal = rule.as_Bool();

                        auto succBb = builder.newBbUnlinked();

                        if (testVal) {
                            builder.endBlock(::MIR::Terminator::make_If({val.clone(), succBb, failBb}));
                        } else {
                            builder.endBlock(::MIR::Terminator::make_If({val.clone(), failBb, succBb}));
                        }
                        builder.setCurBlock(succBb);
                    } break;
                    case ::HIR::CoreType::U8:
                    case ::HIR::CoreType::U16:
                    case ::HIR::CoreType::U32:
                    case ::HIR::CoreType::U64:
                    case ::HIR::CoreType::U128:
                    case ::HIR::CoreType::Usize:
                TU_MATCH_HDRA((rule), {)
                default:
                    BUG(sp, "PatternRule for integer is not Value or ValueRange");
                            TU_ARMA(Value, re) {
                                auto succBb = builder.newBbUnlinked();

                                auto testVal = ::MIR::Param(::MIR::Constant::make_Uint({re.as_Uint().v, te}));
                                builder.pushStmtAssign(sp, builder.getIfCond(), ::MIR::RValue::make_BinOp({val.clone(), ::MIR::eBinOp::EQ, mv$(testVal)}));
                                builder.endBlock(::MIR::Terminator::make_If({builder.getIfCond(), succBb, failBb}));
                                builder.setCurBlock(succBb);
                            }
                            TU_ARMA(ValueRange, re) {
                                auto succBb = builder.newBbUnlinked();

                                // IF `val` < `first` : fail_bb
                                if (re.first.as_Uint().v != 0) {
                                    auto testBb2 = builder.newBbUnlinked();
                                    auto testLtVal = ::MIR::Param(::MIR::Constant::make_Uint({re.first.as_Uint().v, te}));
                                    auto cmpLtLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), ::MIR::eBinOp::LT, mv$(testLtVal)}));
                                    builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLtLval), failBb, testBb2}));

                                    builder.setCurBlock(testBb2);
                                }

                                // IF `val` > `last` : fail_bb
                                if (re.last.as_Uint().v == U128::max() && re.isInclusive) {
                                    builder.endBlock(::MIR::Terminator::make_Goto({succBb}));
                                } else {
                                    auto testGtVal = ::MIR::Param(::MIR::Constant::make_Uint({re.last.as_Uint().v, te}));
                                    auto op = re.isInclusive ? ::MIR::eBinOp::GT : ::MIR::eBinOp::GE;
                                    auto cmpGtLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), op, mv$(testGtVal)}));
                                    builder.endBlock(::MIR::Terminator::make_If({mv$(cmpGtLval), failBb, succBb}));
                                }

                                builder.setCurBlock(succBb);
                            }
                }
                break;
            case ::HIR::CoreType::I8:
            case ::HIR::CoreType::I16:
            case ::HIR::CoreType::I32:
            case ::HIR::CoreType::I64:
            case ::HIR::CoreType::I128:
            case ::HIR::CoreType::Isize:
                TU_MATCH_HDRA((rule), {)
                default:
                    BUG(sp, "PatternRule for integer is not Value or ValueRange");
                            TU_ARMA(Value, re) {
                                auto succBb = builder.newBbUnlinked();

                                auto testVal = ::MIR::Param(::MIR::Constant::make_Int({re.as_Int().v, te}));
                                auto cmpLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({val.clone(), ::MIR::eBinOp::EQ, mv$(testVal)}));
                                builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLval), succBb, failBb}));
                                builder.setCurBlock(succBb);
                            }
                            TU_ARMA(ValueRange, re) {
                                auto succBb = builder.newBbUnlinked();

                                // IF `val` < `first` : fail_bb
                                if (re.first.as_Int().v != S128::min()) {
                                    auto testBb2 = builder.newBbUnlinked();
                                    auto testLtVal = ::MIR::Param(::MIR::Constant::make_Int({re.first.as_Int().v, te}));
                                    auto cmpLtLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), ::MIR::eBinOp::LT, mv$(testLtVal)}));
                                    builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLtLval), failBb, testBb2}));
                                    builder.setCurBlock(testBb2);
                                }

                                // IF `val` > `last` : fail_bb
                                if (re.last.as_Int().v == S128::max() && re.isInclusive) {
                                    builder.endBlock(::MIR::Terminator::make_Goto({succBb}));
                                } else {
                                    auto testGtVal = ::MIR::Param(::MIR::Constant::make_Int({re.last.as_Int().v, te}));
                                    auto op = re.isInclusive ? ::MIR::eBinOp::GT : ::MIR::eBinOp::GE;
                                    auto cmpGtLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), op, mv$(testGtVal)}));
                                    builder.endBlock(::MIR::Terminator::make_If({mv$(cmpGtLval), failBb, succBb}));
                                }

                                builder.setCurBlock(succBb);
                            }
                }
                break;
            case ::HIR::CoreType::Char:
                TU_MATCH_DEF( PatternRule, (rule), (re),
                (
                    BUG(sp, "PatternRule for char is not Value or ValueRange");
                    ),
                (Value,
                    auto succBb = builder.newBbUnlinked();

                    auto testVal = ::MIR::Param(::MIR::Constant::make_Uint({ re.as_Uint().v, te }));
                    auto cmpLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({ ::MIR::Param(val.clone()), ::MIR::eBinOp::EQ, mv$(testVal) }));
                    builder.endBlock( ::MIR::Terminator::make_If({ mv$(cmpLval), succBb, failBb }) );
                    builder.setCurBlock(succBb);
                    ),
                (ValueRange,
                    auto succBb = builder.newBbUnlinked();

                    // IF `val` < `first` : fail_bb
                    if( re.first.as_Uint().v != 0 ) {
                            auto testBb2 = builder.newBbUnlinked();

                            auto testLtVal = ::MIR::Param(::MIR::Constant::make_Uint({re.first.as_Uint().v, te}));
                            auto cmpLtLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), ::MIR::eBinOp::LT, mv$(testLtVal)}));
                            builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLtLval), failBb, testBb2}));

                            builder.setCurBlock(testBb2);
                    }

                    // IF `val` > `last` : fail_bb
                    if(re.last.as_Uint().v >= 0x10FFFF ) {
                            assert(re.isInclusive);
                            builder.endBlock(::MIR::Terminator::make_Goto({succBb}));
                    }
                    else {
                            auto testGtVal = ::MIR::Param(::MIR::Constant::make_Uint({re.last.as_Uint().v, te}));
                            auto op = re.isInclusive ? ::MIR::eBinOp::GT : ::MIR::eBinOp::GE;
                            auto cmpGtLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), op, mv$(testGtVal)}));
                            builder.endBlock(::MIR::Terminator::make_If({mv$(cmpGtLval), failBb, succBb}));
                    }

                    builder.setCurBlock(succBb);
                    )
                )
                break;
            case ::HIR::CoreType::F16:
            case ::HIR::CoreType::F32:
            case ::HIR::CoreType::F64:
            case ::HIR::CoreType::F128:
                TU_MATCH_DEF( PatternRule, (rule), (re),
                (
                    BUG(sp, "PatternRule for float is not Value or ValueRange");
                    ),
                (Value,
                    auto succBb = builder.newBbUnlinked();

                    auto testVal = ::MIR::Param(::MIR::Constant::make_Float({ re.as_Float().v, te }));
                    auto cmpLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({ val.clone(), ::MIR::eBinOp::EQ, mv$(testVal) }));
                    builder.endBlock( ::MIR::Terminator::make_If({ mv$(cmpLval), succBb, failBb }) );
                    builder.setCurBlock(succBb);
                    ),
                (ValueRange,
                    auto succBb = builder.newBbUnlinked();

                    // IF `val` < `first` : fail_bb
                    if( re.first.as_Float().v == -std::numeric_limits<double>::infinity()) {
                    }
                    else {
                            auto testBb2 = builder.newBbUnlinked();
                            auto testLtVal = ::MIR::Param(::MIR::Constant::make_Float({re.first.as_Float().v, te}));
                            auto cmpLtLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), ::MIR::eBinOp::LT, mv$(testLtVal)}));
                            builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLtLval), failBb, testBb2}));
                            builder.setCurBlock(testBb2);
                    }

                    // IF `val` > `last` : fail_bb
                    if( re.first.as_Float().v == std::numeric_limits<double>::infinity() && re.isInclusive ) {
                            builder.endBlock(::MIR::Terminator::make_Goto({succBb}));
                    }
                    else {
                            auto testGtVal = ::MIR::Param(::MIR::Constant::make_Float({re.last.as_Float().v, te}));
                            auto op = re.isInclusive ? ::MIR::eBinOp::GT : ::MIR::eBinOp::GE;
                            auto cmpGtLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), op, mv$(testGtVal)}));
                            builder.endBlock(::MIR::Terminator::make_If({mv$(cmpGtLval), failBb, succBb}));
                    }

                    builder.setCurBlock(succBb);
                    )
                )
                break;
            case ::HIR::CoreType::Str: {
                            ASSERT_BUG(sp, rule.is_Value() && rule.as_Value().is_StaticString(), "Unexpected use of non-value pattern on `str`");
                            const auto& v = rule.as_Value();
                            ASSERT_BUG(sp, val.is_Deref(), "");
                            val.wrappers.pop_back();
                            auto strVal = mv$(val);

                            auto succBb = builder.newBbUnlinked();

                            auto testVal = ::MIR::Param(::MIR::Constant(v.as_StaticString()));
                            auto cmpLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({mv$(strVal), ::MIR::eBinOp::EQ, mv$(testVal)}));
                            builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLval), succBb, failBb}));
                            builder.setCurBlock(succBb);
                } break;
                }
            }
            TU_ARMA(Path, te) {
            TU_MATCH_HDRA( (te.binding), {)
            TU_ARMA(Unbound, pbe) {
                        BUG(sp, "Encounterd unbound path - " << te.path);
                    }
                    TU_ARMA(Opaque, pbe) {
                        BUG(sp, "Attempting to match over opaque type - " << ty);
                    }
                    TU_ARMA(Struct, pbe) {
                        const auto& strData = pbe->mData;
                TU_MATCH_HDRA( (strData), {)
                TU_ARMA(Unit, sd) {
                                BUG(sp, "Attempting to match over unit type - " << ty);
                            }
                            TU_ARMA(Tuple, sd) {
                                TODO(sp, "Matching on tuple-like struct?");
                            }
                            TU_ARMA(Named, sd) {
                                TODO(sp, "Matching on struct?");
                            }
                }
                    }
                    TU_ARMA(Union, pbe) {
                        TODO(sp, "Match over Union");
                    }
                    TU_ARMA(ExternType, pbe) {
                        TODO(sp, "Match over ExternType");
                    }
                    TU_ARMA(Enum, pbe) {
                        auto monomorph = [&](const auto& ty) {
                            auto rv = MonomorphStatePtr(builder.resolve().crate.types, nullptr, &te.path.mData.as_Generic().mParams, nullptr).monomorphType(sp, ty);
                            builder.resolve().expandAssociatedTypes(sp, rv);
                            return rv;
                        };
                        ASSERT_BUG(sp, rule.is_Variant(), "Rule for enum isn't Any or Variant");
                        const auto& re = rule.as_Variant();
                        unsigned int varIdx = re.idx;

                        auto nextBb = builder.newBbUnlinked();
                        auto var_count = pbe->numVariants();

                        // Generate a switch with only one option different.
                        ::std::vector<::MIR::BasicBlockId> arms(var_count, failBb);
                        arms[varIdx] = nextBb;
                        builder.endBlock(::MIR::Terminator::make_Switch({val.clone(), mv$(arms)}));

                        builder.setCurBlock(nextBb);

                        if (re.subRules.size() > 0) {
                            ASSERT_BUG(sp, pbe->mData.is_Data(), "Sub-rules present for non-data enum");
                            const auto& variants = pbe->mData.as_Data();
                            const auto& varTy = variants.at(re.idx).type;
                            ::HIR::TypeRef tmp;
                            const auto& varTyM = (monomorphiseTypeNeeded(varTy) ? tmp = monomorph(varTy) : varTy);

                            // Recurse with the new ruleset
                            MIRLowerHIRMatchSimpleGeneratePattern(builder, sp, re.subRules.data(), re.subRules.size(), varTyM, ::MIR::LValue::newDowncast(val.clone(), varIdx), rule.field_path.size() + 1, failBb);
                        }
                    } // TypePathBinding::Enum
            }
            } // Type::Data::Path
            TU_ARMA(Generic, _te) {
                BUG(sp, "Attempting to match a generic");
            }
            TU_ARMA(TraitObject, te) {
                BUG(sp, "Attempting to match a trait object");
            }
            TU_ARMA(ErasedType, te) {
                BUG(sp, "Attempting to match an erased type");
            }
            TU_ARMA(Array, te) {
                TODO(sp, "Match directly on array?");
            }
            TU_ARMA(Slice, te) {
                ASSERT_BUG(sp, rule.is_Slice() || rule.is_SplitSlice() || (rule.is_Value() && rule.as_Value().is_Bytes()), "Can only match slice with Bytes or Slice rules - " << rule);
                if (rule.is_Value()) {
                    ASSERT_BUG(sp, te.inner == ::HIR::CoreType::U8, "Bytes pattern on non-&[u8]");
                    auto clonedVal = ::MIR::Constant(rule.as_Value().as_Bytes());
                    auto sizeVal = ::MIR::Constant::make_Uint({U128(rule.as_Value().as_Bytes().size()), ::HIR::CoreType::Usize});

                    auto succBb = builder.newBbUnlinked();

                    ASSERT_BUG(sp, val.is_Deref(), "Slice pattern on non-Deref - " << val);
                    auto innerVal = val.cloneUnwrapped();

                    auto sliceRval = ::MIR::RValue::make_MakeDst({mv$(clonedVal), mv$(sizeVal)});
                    auto testLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.borrow(::HIR::BorrowType::Shared, ty), mv$(sliceRval));
                    auto cmpLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({mv$(innerVal), ::MIR::eBinOp::EQ, mv$(testLval)}));
                    builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLval), succBb, failBb}));
                    builder.setCurBlock(succBb);
                } else if (rule.is_Slice()) {
                    const auto& re = rule.as_Slice();

                    // Compare length
                    auto testVal = ::MIR::Param(::MIR::Constant::make_Uint({U128(re.len), ::HIR::CoreType::Usize}));
                    auto lenVal = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Usize), ::MIR::RValue::make_DstMeta({builder.getPtrToDst(sp, val)}));
                    auto cmpLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({mv$(lenVal), ::MIR::eBinOp::EQ, mv$(testVal)}));

                    auto lenSuccBb = builder.newBbUnlinked();
                    builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLval), lenSuccBb, failBb}));
                    builder.setCurBlock(lenSuccBb);

                    // Recurse checking values
                    MIRLowerHIRMatchSimpleGeneratePattern(builder, sp, re.subRules.data(), re.subRules.size(), top_ty, top_val, field_path_ofs, failBb);
                } else if (rule.is_SplitSlice()) {
                    const auto& re = rule.as_SplitSlice();

                    // Compare length
                    auto testVal = ::MIR::Param(::MIR::Constant::make_Uint({U128(re.minLen), ::HIR::CoreType::Usize}));
                    auto lenVal = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Usize), ::MIR::RValue::make_DstMeta({builder.getPtrToDst(sp, val)}));
                    auto cmpLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({mv$(lenVal), ::MIR::eBinOp::LT, mv$(testVal)}));

                    auto lenSuccBb = builder.newBbUnlinked();
                    builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLval), failBb, lenSuccBb})); // if len < test : FAIL
                    builder.setCurBlock(lenSuccBb);

                    MIRLowerHIRMatchSimpleGeneratePattern(builder, sp, re.leading.data(), re.leading.size(), top_ty, top_val, field_path_ofs, failBb);

                    MIRLowerHIRMatchSimpleGeneratePattern(builder, sp, re.trailing.data(), re.trailing.size(), top_ty, top_val, field_path_ofs, failBb);
                } else {
                    BUG(sp, "Invalid rule type for slice - " << rule);
                }
            } // Type::Data::Array
            TU_ARMA(Tuple, te) {
                TODO(sp, "Match directly on tuple?");
            }
            TU_ARMA(Borrow, te) {
                TODO(sp, "Match directly on borrow?");
            } // Type::Data::Borrow
            TU_ARMA(Pointer, te) {
                BUG(sp, "Attempting to match a pointer - " << rule << " against " << ty);
            }
            TU_ARMA(NamedFunction, te) {
                BUG(sp, "Attempting to match a function pointer - " << rule << " against " << ty);
            }
            TU_ARMA(Function, te) {
                BUG(sp, "Attempting to match a function pointer - " << rule << " against " << ty);
            }
            TU_ARMA(NodeType, te) {
                BUG(sp, "Attempting to match a magic type - " << rule << " against " << ty);
            }
        }
    }
    return 0;
}

// --
// Match v2 Algo - Grouped rules
// --

class tRulesSubset {
    ::std::vector<const ::std::vector<PatternRule>*> ruleSets;
    bool isArmIndexes;
    ::std::vector<size_t> armIdxes;

    static ::std::pair<size_t, size_t> decodeArmIdx(size_t v) {
        return ::std::make_pair(v & 0x3FFF, v >> 14);
    }

    static size_t encodeArmIdx(size_t armIdx, size_t patIdx) {
        assert(armIdx <= 0x3FFF);
        assert(patIdx <= 0x3FFF);
        return armIdx | (patIdx << 14);
    }

public:
    tRulesSubset(size_t exp, bool isArmIndexes)
        : isArmIndexes(isArmIndexes)
    {
        ruleSets.reserve(exp);
        armIdxes.reserve(exp);
    }

    size_t size() const {
        return ruleSets.size();
    }

    const ::std::vector<PatternRule>& operator[](size_t n) const {
        return *ruleSets[n];
    }

    bool isArm() const {
        return isArmIndexes;
    }

    struct ArmIdxes {
        size_t arm;
        size_t armRule;
    };

    ArmIdxes armIdx(size_t n) const {
        assert(isArmIndexes);
        auto v = decodeArmIdx(armIdxes.at(n));
        return ArmIdxes{v.first, v.second};
    }

    ::MIR::BasicBlockId bbIdx(size_t n) const {
        assert(!isArmIndexes);
        return armIdxes.at(n);
    }

    void subSort(size_t ofs, size_t start, size_t n) {
        ::std::vector<size_t> v;
        for (size_t i = 0; i < n; i++) {
            v.push_back(start + i);
        }
        // Sort rules based on just the value (ignore inner rules)
        ::std::stable_sort(v.begin(), v.end(), [&](auto a, auto b) {
            return ordRuleCompatible((*ruleSets[a])[ofs], (*ruleSets[b])[ofs]) == OrdLess;
        });

        // Reorder contents to above sorting
        {
            decltype(this->ruleSets) tmp;
            for (auto i : v) {
                tmp.push_back(ruleSets[i]);
            }
            ::std::copy(tmp.begin(), tmp.end(), ruleSets.begin() + start);
        }
        {
            decltype(this->armIdxes) tmp;
            for (auto i : v) {
                tmp.push_back(armIdxes[i]);
            }
            ::std::copy(tmp.begin(), tmp.end(), armIdxes.begin() + start);
        }
    }

    tRulesSubset subSlice(size_t ofs, size_t n) {
        tRulesSubset rv{n, this->isArmIndexes};
        rv.ruleSets.reserve(n);
        for (size_t i = 0; i < n; i++) {
            rv.ruleSets.push_back(this->ruleSets[ofs + i]);
            rv.armIdxes.push_back(this->armIdxes[ofs + i]);
        }
        return rv;
    }

    void pushArm(const ::std::vector<PatternRule>& x, size_t armIdx, size_t patIdx) {
        assert(isArmIndexes);
        ruleSets.push_back(&x);
        armIdxes.push_back(encodeArmIdx(armIdx, patIdx));
    }

    void pushBb(const ::std::vector<PatternRule>& x, ::MIR::BasicBlockId bb) {
        assert(!isArmIndexes);
        ruleSets.push_back(&x);
        armIdxes.push_back(bb);
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const tRulesSubset& x) {
        os << "t_rules_subset{";
        for (size_t i = 0; i < x.ruleSets.size(); i++) {
            if (i != 0) {
                os << ", ";
            }
            os << "[";
            if (x.isArmIndexes) {
                auto v = decodeArmIdx(x.armIdxes[i]);
                os << v.first << "," << v.second;
            } else {
                os << "bb" << x.armIdxes[i];
            }
            os << "]";
            os << ": [" << *x.ruleSets[i] << "]";
        }
        os << "}";
        return os;
    }
};

class MatchGenGrouped {
    const Span& sp;
    MirBuilder& builder;
    const ::HIR::TypeData* topTy;
    const ::MIR::LValue& topVal;
    const ::std::vector<ArmCode>& armsCode;

    size_t fieldPathOfs;

public:
    MatchGenGrouped(MirBuilder& builder, const Span& sp, const ::HIR::TypeData* top_ty, const ::MIR::LValue& top_val, const ::std::vector<ArmCode>& arms_code, size_t field_path_ofs)
        : sp(sp)
        , builder(builder)
        , topTy(top_ty)
        , topVal(top_val)
        , armsCode(arms_code)
        , fieldPathOfs(field_path_ofs)
    {
    }

    void genForSlice(tRulesSubset rules, size_t ofs, ::MIR::BasicBlockId defaultArm);
    void genDispatch(const ::std::vector<tRulesSubset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& armTargets, ::MIR::BasicBlockId defBlk);
    void genDispatchPrimitive(::HIR::TypeRef ty, ::MIR::LValue val, const ::std::vector<tRulesSubset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& armTargets, ::MIR::BasicBlockId defBlk);
    void genDispatchEnum(::HIR::TypeRef ty, ::MIR::LValue val, const ::std::vector<tRulesSubset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& armTargets, ::MIR::BasicBlockId defBlk);
    void genDispatchSlice(::HIR::TypeRef ty, ::MIR::LValue val, const ::std::vector<tRulesSubset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& armTargets, ::MIR::BasicBlockId defBlk);

    void genDispatchRange(const fieldPathT& field_path, const ::MIR::Constant& first, const ::MIR::Constant& last, bool isInclusive, ::MIR::BasicBlockId defBlk);
    void genDispatchSplitslice(const fieldPathT& field_path, const PatternRule::Data_SplitSlice& e, ::MIR::BasicBlockId defBlk);

    ::MIR::LValue pushCompare(::MIR::LValue left, ::MIR::eBinOp op, ::MIR::Param right) {
        return builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({mv$(left), op, mv$(right)}));
    }
};

namespace {
    void appendRuleColumns(::std::vector<PatternRule>& outRules, PatternRule rule) {
        TU_MATCH_HDRA( (rule), {)
        TU_ARMA(Variant, e) {
                auto subRules = mv$(e.subRules);
                outRules.push_back(mv$(rule));
                for (auto& sr : subRules) {
                    appendRuleColumns(outRules, mv$(sr));
                }
            }
            TU_ARMA(Slice, e) {
                auto subRules = mv$(e.subRules);
                outRules.push_back(mv$(rule));
                for (auto& sr : subRules) {
                    appendRuleColumns(outRules, mv$(sr));
                }
            }
            TU_ARMA(SplitSlice, e) {
                auto leading = mv$(e.leading);
                auto trailing = mv$(e.trailing);
                auto idx = outRules.size();
                outRules.push_back(mv$(rule));
                for (auto& sr : leading) {
                    appendRuleColumns(outRules, mv$(sr));
                }
                // Trailing rules are complex as they break the assumption that patterns across the same type share a prefix
                // - So, flatten them into the "flattened" rule
                for (auto& sr : trailing) {
                    appendRuleColumns(outRules[idx].as_SplitSlice().trailing, mv$(sr));
                }
            }
            TU_ARMA(Bool, e) {
                outRules.push_back(mv$(rule));
            }
            TU_ARMA(Value, e) {
                outRules.push_back(mv$(rule));
            }
            TU_ARMA(ValueRange, e) {
                outRules.push_back(mv$(rule));
            }
            TU_ARMA(Any, e) {
                outRules.push_back(mv$(rule));
            }
        }
    }

    tArmRules linearizeRuleColumns(tArmRules rules) {
        tArmRules rv;
        rv.reserve(rules.size());
        for (auto& ruleset : rules) {
            ::std::vector<PatternRule> patternRules;
            for (auto& r : ruleset.rules) {
                appendRuleColumns(patternRules, mv$(r));
            }
            rv.push_back(PatternRuleset{ruleset.armIdx, ruleset.armRuleIdx, mv$(patternRules)});
        }
        return rv;
    }
}

void MIRLowerHIRMatchGrouped(MirBuilder& builder, MirConverter& conv, const Span& sp, const HIR::TypeData* matchTy, ::MIR::LValue matchVal, tArmRules armRules, ::std::vector<ArmCode> arms_code, ::MIR::BasicBlockId firstCmpBlock) {
    TRACE_FUNCTION_F("");

    // The grouped matcher consumes one constructor or field test per matrix
    // column. Keep each outer constructor before the payload columns and retain
    // the full field path on every test.
    armRules = linearizeRuleColumns(mv$(armRules));

    // - Create a "slice" of the passed rules, suitable for passing to the recursive part of the algo
    tRulesSubset rules{armRules.size(), /*is_arm_indexes=*/true};
    for (const auto& r : armRules) {
        rules.pushArm(r.rules, r.armIdx, r.armRuleIdx);
    }

    auto inst = MatchGenGrouped{builder, sp, matchTy, matchVal, arms_code, 0};

    // NOTE: This block should never be used
    auto defaultArm = builder.newBbUnlinked();

    builder.setCurBlock(firstCmpBlock);
    inst.genForSlice(mv$(rules), 0, defaultArm);

    // Make the default infinite loop.
    // - Preferably, it'd abort.
    builder.setCurBlock(defaultArm);
    builder.endBlock(::MIR::Terminator::make_Unreachable({}));
}

void MatchGenGrouped::genForSlice(tRulesSubset armRules, size_t ofs, ::MIR::BasicBlockId defaultArm) {
    TRACE_FUNCTION_F("arm_rules=" << armRules << ", ofs=" << ofs << ", default_arm=" << defaultArm);
    ASSERT_BUG(sp, armRules.size() > 0, "");

    // Leading wildcard-only columns cannot discriminate between these rows.
    for (;;) {
        bool isAllAny = true;
        for (size_t i = 0; i < armRules.size() && isAllAny; i++) {
            if (armRules[i].size() <= ofs) {
                isAllAny = false;
            } else if (!armRules[i][ofs].is_Any()) {
                isAllAny = false;
            }
        }
        if (!isAllAny) {
            break;
        }
        ofs++;
        DEBUG("Skip to ofs=" << ofs);
    }

    // Split current set of rules into groups based on _ patterns
    for (size_t idx = 0; idx < armRules.size();) {
        // Completed arms
        while (idx < armRules.size() && armRules[idx].size() <= ofs) {
            //auto next = idx+1 == arm_rules.size() ? default_arm : m_builder.new_bb_unlinked();
            ASSERT_BUG(sp, armRules[idx].size() == ofs, "Offset too large for rule - ofs=" << ofs << ", rules=" << armRules[idx]);
            DEBUG(idx << ": Complete");
            // Emit jump to either arm code, or arm condition
            if (armRules.isArm()) {
                auto ai = armRules.armIdx(idx);
                ASSERT_BUG(sp, armsCode.size() > 0, "Bottom-level ruleset with no arm code information");
                const auto& ac = armsCode[ai.arm];
                ASSERT_BUG(sp, ai.armRule < ac.rules.size(), "Arm rule index (" << ai.armRule << ") out of bounds (" << ac.rules.size() << ")");

                builder.endBlock(::MIR::Terminator::make_Goto(ac.rules.at(ai.armRule).entry));

                if (ac.hasCondition) {
                    TODO(sp, "Handle conditionals in Grouped");
                    // TODO: If the condition fails, this should re-try the match on other rules that could have worked.
                    // - For now, conditionals are disabled.

                    // TODO: What if there's multiple patterns on this condition?
                    // - For now, only the first pattern gets edited.
                    // - Maybe clone the blocks used for the condition?

                } else {
                    ASSERT_BUG(sp, idx + 1 == armRules.size(), "Ended arm with other arms present");
                }
            } else {
                auto bb = armRules.bbIdx(idx);
                builder.endBlock(::MIR::Terminator::make_Goto(bb));
                while (idx + 1 < armRules.size() && bb == armRules.bbIdx(idx) && armRules[idx].size() == ofs) {
                    idx++;
                }
                ASSERT_BUG(sp, idx + 1 == armRules.size(), "Ended arm (inner) with other arms present");
            }
            idx++;
        }

        // - Value arms
        auto start = idx;
        bool stoppedAtOverlap = false;
        for (; idx < armRules.size(); idx++) {
            if (armRules[idx].size() <= ofs) {
                break;
            }
            if (armRules[idx][ofs].is_Any()) {
                break;
            }
            if (armRules[idx][ofs].is_SplitSlice()) {
                break;
            }
            // TODO: It would be nice if ValueRange could be combined with Value (if there's no overlap)
            if (armRules[idx][ofs].is_ValueRange()) {
                break;
            }

            // The dispatch below sorts selector groups.  Keep an ordering
            // boundary before a selector that overlaps an incompatible
            // earlier selector, otherwise e.g. a byte literal can move past
            // an equal-length slice pattern and change the selected arm.
            for (size_t prev = start; prev < idx; prev++) {
                if (!ruleCompatible(armRules[prev][ofs], armRules[idx][ofs])
                    && rulesOverlap(armRules[prev][ofs], armRules[idx][ofs])) {
                    stoppedAtOverlap = true;
                    break;
                }
            }
            if (stoppedAtOverlap) {
                break;
            }
        }
        auto firstAny = idx;

        // Generate dispatch based on the above list
        // - If there's value ranges they need special handling
        // - Can sort arms within this group (ordering doesn't matter, as long as ranges are handled)
        // - Sort must be stable.

        if (start < firstAny) {
            DEBUG(start << "+" << (firstAny - start) << ": Values");
            bool has_default = (firstAny < armRules.size());
            auto next = (has_default ? builder.newBbUnlinked() : defaultArm);

            // Sort rules before getting compatible runs
            // TODO: Is this a valid operation?
            armRules.subSort(ofs, start, firstAny - start);

            // Create list of compatible arm slices (runs with the same selector value)
            ::std::vector<tRulesSubset> slices;
            auto curTest = start;
            for (auto i = start; i < firstAny; i++) {
                // Just check if the decision value differs (don't check nested rules)
                if (!ruleCompatible(armRules[i][ofs], armRules[curTest][ofs])) {
                    slices.push_back(armRules.subSlice(curTest, i - curTest));
                    curTest = i;
                }
            }
            slices.push_back(armRules.subSlice(curTest, firstAny - curTest));
            DEBUG("- " << slices.size() << " groupings");
            ::std::vector<::MIR::BasicBlockId> armBlocks;
            armBlocks.reserve(slices.size());

            auto curBlk = builder.pauseCurBlock();
            // > Stable sort list
            ::std::sort(slices.begin(), slices.end(), [&](const auto& a, const auto& b) {
                return a[0][ofs] < b[0][ofs];
            });
            // TODO: Should this do a stable sort of inner patterns too?
            // - A sort of inner patterns such that `_` (and range?) patterns don't change position.

            // > Get type of match, generate dispatch list.
            for (size_t i = 0; i < slices.size(); i++) {
                auto curBlock = builder.newBbUnlinked();
                builder.setCurBlock(curBlock);

                for (size_t j = 0; j < slices[i].size(); j++) {
                    if (j > 0) {
                        ASSERT_BUG(sp, slices[i][0][ofs] == slices[i][j][ofs], "Mismatched rules - " << slices[i][0][ofs] << " and " << slices[i][j][ofs]);
                    }
                    armBlocks.push_back(curBlock);
                }

                this->genForSlice(slices[i], ofs + 1, next);
            }

            builder.setCurBlock(curBlk);

            // Generate decision code
            this->genDispatch(slices, ofs, armBlocks, next);

            if (has_default) {
                builder.setCurBlock(next);
            }
        }

        if (stoppedAtOverlap) {
            continue;
        }

        // Collate matching blocks at `first_any`
        assert(firstAny == idx);
        if (firstAny < armRules.size() && armRules[idx].size() > ofs) {
            // Collate all equal rules
            while (idx < armRules.size() && armRules[idx][ofs] == armRules[firstAny][ofs]) {
                idx++;
            }
            DEBUG(firstAny << "-" << idx << ": Multi-match");

            bool hasNext = idx < armRules.size();
            auto next = (hasNext ? builder.newBbUnlinked() : defaultArm);

            const auto& rule = armRules[firstAny][ofs];
            if (const auto* e = rule.opt_ValueRange()) {
                // Generate branch based on range
                this->genDispatchRange(armRules[firstAny][ofs].field_path, e->first, e->last, e->isInclusive, next);
            } else if (const auto* e = rule.opt_SplitSlice()) {
                // Generate branch based on slice length being at least required.
                this->genDispatchSplitslice(rule.field_path, *e, next);
            } else {
                ASSERT_BUG(sp, rule.is_Any(), "Didn't expect non-Any rule here, got " << rule.tagStr() << " " << rule);
            }

            // Step deeper into these arms
            auto slice = armRules.subSlice(firstAny, idx - firstAny);
            this->genForSlice(mv$(slice), ofs + 1, next);

            if (hasNext) {
                builder.setCurBlock(next);
            }
        }
    }

    ASSERT_BUG(sp, !builder.block_active(), "Block left active after match group");
}

/// <summary>
/// Generate dispatch code for the provided pattern list
/// </summary>
/// <param name="rules">A list of equivalent pattern rules (at the given offset)</param>
/// <param name="ofs">Offset into sub-patterns</param>
/// <param name="arm_targets">Target blocks for each arm in `rules`</param>
/// <param name="def_blk">Default block for if no arm matched</param>
void MatchGenGrouped::genDispatch(const ::std::vector<tRulesSubset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& armTargets, ::MIR::BasicBlockId defBlk) {
    const auto& field_path = rules[0][0][ofs].field_path;
    TRACE_FUNCTION_F("rules=[" << rules << "], ofs=" << ofs << ", field_path=" << field_path);

    // Assert that all patterns combined here are over the same field
    {
        size_t n = 0;
        for (size_t i = 0; i < rules.size(); i++) {
            for (size_t j = 0; j < rules[i].size(); j++) {
                ASSERT_BUG(sp, rules[i][j][ofs].field_path == field_path, "Field path mismatch, " << rules[i][j][ofs].field_path << " != " << field_path);
                n++;
            }
        }
        ASSERT_BUG(sp, armTargets.size() == n, "Arm target count mismatch - " << n << " != " << armTargets.size());
    }

    ::MIR::LValue val;
    ::HIR::TypeRef ty;
    getTyAndVal(sp, builder, topTy, topVal, field_path, fieldPathOfs, ty, val);
    DEBUG("ty = " << ty << ", val = " << val);

    TU_MATCH_HDRA( (*ty), {)
    TU_ARMA(Infer, te) {
            BUG(sp, "Hit _ in type - " << ty);
        }
        TU_ARMA(Diverge, te) {
            BUG(sp, "Matching over !");
        }
        TU_ARMA(Primitive, te) {
            this->genDispatchPrimitive(mv$(ty), mv$(val), rules, ofs, armTargets, defBlk);
        }
        TU_ARMA(Path, te) {
            // Matching over a path can only happen with an enum.
            // TODO: What about `box` destructures?
            // - They're handled via hidden derefs
        TU_MATCH_HDR( (te.binding), { )
        TU_ARM(te.binding, Unbound, pbe) {
                    BUG(sp, "Encounterd unbound path - " << te.path);
                }
                TU_ARM(te.binding, Opaque, pbe) {
                    BUG(sp, "Attempting to match over opaque type - " << ty);
                }
                TU_ARM(te.binding, Struct, pbe) {
                    const auto& strData = pbe->mData;
            TU_MATCH_HDRA( (strData), {)
            TU_ARMA(Unit, sd) {
                            BUG(sp, "Attempting to match over unit type - " << ty);
                        }
                        TU_ARMA(Tuple, sd) {
                            TODO(sp, "Matching on tuple-like struct?");
                        }
                        TU_ARMA(Named, sd) {
                            TODO(sp, "Matching on struct? - " << ty);
                        }
            }
                }
                TU_ARM(te.binding, Union, pbe) {
                    TODO(sp, "Match over Union");
                }
                TU_ARM(te.binding, ExternType, pbe) {
                    TODO(sp, "Match over ExternType - " << ty);
                }
                TU_ARM(te.binding, Enum, pbe) {
                    this->genDispatchEnum(mv$(ty), mv$(val), rules, ofs, armTargets, defBlk);
                }
        }
        }
        TU_ARMA(Generic, te) {
            BUG(sp, "Attempting to match a generic");
        }
        TU_ARMA(TraitObject, te) {
            BUG(sp, "Attempting to match a trait object");
        }
        TU_ARMA(ErasedType, te) {
            BUG(sp, "Attempting to match an erased type");
        }
        TU_ARMA(Array, te) {
            // Byte strings?
            // Remove the deref on the &str
            ASSERT_BUG(sp, !val.wrappers.empty() && val.wrappers.back().is_Deref(), "&[T; N] match on non-Deref lvalue - " << val);
            val.wrappers.pop_back();

            ::std::vector<::MIR::BasicBlockId> targets;
            ::std::vector<::std::vector<uint8_t>> values;
            size_t tgtOfs = 0;
            for (size_t i = 0; i < rules.size(); i++) {
                for (size_t j = 1; j < rules[i].size(); j++) {
                    ASSERT_BUG(sp, armTargets[tgtOfs] == armTargets[tgtOfs + j], "Mismatched target blocks for Value match");
                }

                const auto& r = rules[i][0][ofs];
                ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tagStr());
                const auto& re = r.as_Value();
                if (re.is_Const()) {
                    TODO(sp, "Handle Constant::Const in match");
                }

                targets.push_back(armTargets[tgtOfs]);
                values.push_back(re.as_Bytes());

                tgtOfs += rules[i].size();
            }
            builder.endBlock(::MIR::Terminator::make_SwitchValue({mv$(val), defBlk, mv$(targets), ::MIR::SwitchValues(mv$(values))}));
        }
        TU_ARMA(Slice, te) {
            this->genDispatchSlice(mv$(ty), mv$(val), rules, ofs, armTargets, defBlk);
        }
        TU_ARMA(Tuple, te) {
            BUG(sp, "Match directly on tuple");
        }
        TU_ARMA(Borrow, te) {
            BUG(sp, "Match directly on borrow");
        }
        TU_ARMA(Pointer, te) {
            auto valUsize = builder.newTemporary(builder.resolve().crate.types.primitive(HIR::CoreType::Usize));
            builder.pushStmtAssign(sp, valUsize.clone(), ::MIR::RValue::make_Cast({mv$(val), builder.resolve().crate.types.primitive(::HIR::CoreType::Usize)}));
            this->genDispatchPrimitive(builder.resolve().crate.types.primitive(HIR::CoreType::Usize), mv$(valUsize), rules, ofs, armTargets, defBlk);
        }
        TU_ARMA(NamedFunction, te) {
            BUG(sp, "Attempting to match a function pointer - " << ty);
        }
        TU_ARMA(Function, te) {
            // TODO: Could this actually be valid?
            BUG(sp, "Attempting to match a function pointer - " << ty);
        }
        TU_ARMA(NodeType, te) {
            BUG(sp, "Attempting to match a magic type - " << ty);
        }
    }
}

namespace {
    void pushIfEqual(const Span& sp, MirBuilder& builder, ::MIR::LValue val, ::MIR::Param testVal, ::MIR::BasicBlockId bbTrue, ::MIR::BasicBlockId bbFalse) {
        auto cmpLval = builder.getRvalInIfCond(sp, ::MIR::RValue::make_BinOp({mv$(val), ::MIR::eBinOp::EQ, mv$(testVal)}));
        builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLval), bbTrue, bbFalse}));
    }
}

void MatchGenGrouped::genDispatchPrimitive(::HIR::TypeRef ty, ::MIR::LValue val, const ::std::vector<tRulesSubset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& armTargets, ::MIR::BasicBlockId defBlk) {
    auto te = ty->as_Primitive();
    switch (te) {
        case ::HIR::CoreType::Bool: {
            ASSERT_BUG(sp, rules.size() <= 2, "More than 2 rules for boolean");
            for (size_t i = 0; i < rules.size(); i++) {
                ASSERT_BUG(sp, rules[i][0][ofs].is_Bool(), "PatternRule for bool isn't _Bool");
            }

            // False sorts before true.
            auto failBb = rules.size() == 2 ? armTargets[0] : (rules[0][0][ofs].as_Bool() ? defBlk : armTargets[0]);
            auto succBb = rules.size() == 2 ? armTargets[rules[0].size()] : (rules[0][0][ofs].as_Bool() ? armTargets[0] : defBlk);

            builder.endBlock(::MIR::Terminator::make_If({mv$(val), succBb, failBb}));
        } break;
        case ::HIR::CoreType::U8:
        case ::HIR::CoreType::U16:
        case ::HIR::CoreType::U32:
        case ::HIR::CoreType::U64:
        case ::HIR::CoreType::U128:
        case ::HIR::CoreType::Usize:

        case ::HIR::CoreType::Char:
            if (rules.size() == 1) {
                // Special case, single option, equality only
                const auto& r = rules[0][0][ofs];
                ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tagStr());
                const auto& re = r.as_Value();
                pushIfEqual(sp, builder, mv$(val), ::MIR::Param(re.clone()), armTargets[0], defBlk);
            } else {
                // NOTE: Rules are currently sorted
                // TODO: If there are Constant::Const values in the list, they need to come first! (with equality checks)

                ::std::vector<::std::pair<::MIR::Constant, ::MIR::BasicBlockId>> largeValues;
                ::std::vector<uint64_t> values;
                ::std::vector<::MIR::BasicBlockId> targets;
                size_t tgtOfs = 0;
                for (size_t i = 0; i < rules.size(); i++) {
                    for (size_t j = 1; j < rules[i].size(); j++) {
                        ASSERT_BUG(sp, armTargets[tgtOfs] == armTargets[tgtOfs + j], "Mismatched target blocks for Value match");
                    }

                    const auto& r = rules[i][0][ofs];
                    ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tagStr());
                    const auto& re = r.as_Value();
                    if (re.is_Const()) {
                        // Inject a `If` chained to a new block
                        auto nextBlock = builder.newBbUnlinked();
                        pushIfEqual(sp, builder, val.clone(), ::MIR::Param(re.clone()), armTargets[tgtOfs], nextBlock);
                        builder.setCurBlock(nextBlock);
                    } else if (re.as_Uint().v > U128(UINT64_MAX)) {
                        largeValues.push_back(std::make_pair(re.clone(), armTargets[tgtOfs]));
                    } else {
                        values.push_back(re.as_Uint().v.truncateU64());
                        targets.push_back(armTargets[tgtOfs]);
                    }

                    tgtOfs += rules[i].size();
                }
                // If there were any values that don't fit in u64, then emit those as a chain of `if` terminators
                if (!largeValues.empty()) {
                    auto tailBlock = builder.newBbUnlinked();
                    builder.endBlock(::MIR::Terminator::make_SwitchValue({val.clone(), tailBlock, mv$(targets), ::MIR::SwitchValues(mv$(values))}));
                    builder.setCurBlock(tailBlock);
                    for (auto& v : largeValues) {
                        auto nextBlock = builder.newBbUnlinked();
                        pushIfEqual(sp, builder, val.clone(), mv$(v.first), v.second, nextBlock);
                        builder.setCurBlock(nextBlock);
                    }
                    builder.endBlock(::MIR::Terminator::make_Goto(defBlk));
                } else {
                    builder.endBlock(::MIR::Terminator::make_SwitchValue({mv$(val), defBlk, mv$(targets), ::MIR::SwitchValues(mv$(values))}));
                }
            }
            break;

        case ::HIR::CoreType::I8:
        case ::HIR::CoreType::I16:
        case ::HIR::CoreType::I32:
        case ::HIR::CoreType::I64:
        case ::HIR::CoreType::I128:
        case ::HIR::CoreType::Isize:
            if (rules.size() == 1) {
                // Special case, single option, equality only
                const auto& r = rules[0][0][ofs];
                ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tagStr());
                const auto& re = r.as_Value();
                pushIfEqual(sp, builder, mv$(val), ::MIR::Param(re.clone()), armTargets[0], defBlk);
            } else {
                // NOTE: Rules are currently sorted
                // TODO: If there are Constant::Const values in the list, they need to come first! (with equality checks)

                ::std::vector<int64_t> values;
                ::std::vector<::MIR::BasicBlockId> targets;
                size_t tgtOfs = 0;
                for (size_t i = 0; i < rules.size(); i++) {
                    for (size_t j = 1; j < rules[i].size(); j++) {
                        ASSERT_BUG(sp, armTargets[tgtOfs] == armTargets[tgtOfs + j], "Mismatched target blocks for Value match");
                    }

                    const auto& r = rules[i][0][ofs];
                    ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tagStr());
                    const auto& re = r.as_Value();
                    if (re.is_Const()) {
                        TODO(sp, "Handle Constant::Const in match");
                    }

                    if (re.as_Int().v > S128(INT64_MAX) || re.as_Int().v < S128(INT64_MIN)) {
                        TODO(sp, "Handle 128-bit values in SwitchValue");
                    }
                    values.push_back(re.as_Int().v.truncateI64());
                    targets.push_back(armTargets[tgtOfs]);

                    tgtOfs += rules[i].size();
                }
                builder.endBlock(::MIR::Terminator::make_SwitchValue({mv$(val), defBlk, mv$(targets), ::MIR::SwitchValues(mv$(values))}));
            }
            break;

        case ::HIR::CoreType::F16:
        case ::HIR::CoreType::F32:
        case ::HIR::CoreType::F64:
        case ::HIR::CoreType::F128: {
            // NOTE: Rules are currently sorted
            // TODO: If there are Constant::Const values in the list, they need to come first!
            size_t tgtOfs = 0;
            for (size_t i = 0; i < rules.size(); i++) {
                for (size_t j = 1; j < rules[i].size(); j++) {
                    ASSERT_BUG(sp, armTargets[tgtOfs] == armTargets[tgtOfs + j], "Mismatched target blocks for Value match");
                }

                const auto& r = rules[i][0][ofs];
                ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tagStr());
                const auto& re = r.as_Value();
                if (re.is_Const()) {
                    TODO(sp, "Handle Constant::Const in match");
                }

                // IF v < tst : def_blk
                {
                    auto cmpEqBlk = builder.newBbUnlinked();
                    auto cmpLvalLt = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({val.clone(), ::MIR::eBinOp::LT, ::MIR::Param(re.clone())}));
                    builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLvalLt), defBlk, cmpEqBlk}));
                    builder.setCurBlock(cmpEqBlk);
                }

                // IF v == tst : target
                {
                    auto nextCmpBlk = builder.newBbUnlinked();
                    auto cmpLvalEq = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({val.clone(), ::MIR::eBinOp::EQ, ::MIR::Param(re.clone())}));
                    builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLvalEq), armTargets[tgtOfs], nextCmpBlk}));
                    builder.setCurBlock(nextCmpBlk);
                }

                tgtOfs += rules[i].size();
            }
            builder.endBlock(::MIR::Terminator::make_Goto(defBlk));
        } break;
        case ::HIR::CoreType::Str: {
            // Remove the deref on the &str
            ASSERT_BUG(sp, !val.wrappers.empty() && val.wrappers.back().is_Deref(), "&str match on non-Deref lvalue - " << val);
            val.wrappers.pop_back();

            ::std::vector<::MIR::BasicBlockId> targets;
            ::std::vector<::std::string> values;
            size_t tgtOfs = 0;
            for (size_t i = 0; i < rules.size(); i++) {
                for (size_t j = 1; j < rules[i].size(); j++) {
                    ASSERT_BUG(sp, armTargets[tgtOfs] == armTargets[tgtOfs + j], "Mismatched target blocks for Value match");
                }

                const auto& r = rules[i][0][ofs];
                ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tagStr());
                const auto& re = r.as_Value();
                if (re.is_Const()) {
                    TODO(sp, "Handle Constant::Const in match");
                }

                targets.push_back(armTargets[tgtOfs]);
                values.push_back(re.as_StaticString());

                tgtOfs += rules[i].size();
            }
            builder.endBlock(::MIR::Terminator::make_SwitchValue({mv$(val), defBlk, mv$(targets), ::MIR::SwitchValues(mv$(values))}));
        } break;
    }
}

void MatchGenGrouped::genDispatchEnum(::HIR::TypeRef ty, ::MIR::LValue val, const ::std::vector<tRulesSubset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& armTargets, ::MIR::BasicBlockId defBlk) {
    TRACE_FUNCTION;
    auto& te = ty->as_Path();
    const auto& pbe = te.binding.as_Enum();

    auto decisonArm = builder.pauseCurBlock();

    auto var_count = pbe->numVariants();
    ::std::vector<::MIR::BasicBlockId> arms(var_count, defBlk);
    size_t armIdx = 0;
    for (size_t i = 0; i < rules.size(); i++) {
        ASSERT_BUG(sp, rules[i][0][ofs].is_Variant(), "Rule for enum isn't Any or Variant - " << rules[i][0][ofs].tagStr());
        const auto& re = rules[i][0][ofs].as_Variant();
        unsigned int varIdx = re.idx;
        DEBUG("Variant " << varIdx);

        ASSERT_BUG(sp, re.subRules.size() == 0, "Sub-rules in MatchGenGrouped");

        arms[varIdx] = armTargets[armIdx];
        for (size_t j = 0; j < rules[i].size(); j++) {
            assert(arms[varIdx] == armTargets[armIdx]);
            armIdx++;
        }
    }

    builder.setCurBlock(decisonArm);
    builder.endBlock(::MIR::Terminator::make_Switch({mv$(val), mv$(arms)}));
}

void MatchGenGrouped::genDispatchSlice(::HIR::TypeRef ty, ::MIR::LValue val, const ::std::vector<tRulesSubset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& armTargets, ::MIR::BasicBlockId defBlk) {
    auto valLen = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Usize), ::MIR::RValue::make_DstMeta({builder.getPtrToDst(sp, val)}));

    // TODO: Re-sort the rules list to interleve Constant::Bytes and Slice

    // Just needs to check the lengths, then dispatch.
    size_t tgtOfs = 0;
    for (size_t i = 0; i < rules.size(); i++) {
        const auto& r = rules[i][0][ofs];
        if (const auto* re = r.opt_Slice()) {
            ASSERT_BUG(sp, re->subRules.size() == 0, "Sub-rules in MatchGenGrouped");
            auto valTst = ::MIR::Constant::make_Uint({U128(re->len), ::HIR::CoreType::Usize});

            for (size_t j = 0; j < rules[i].size(); j++) {
                assert(armTargets[tgtOfs] == armTargets[tgtOfs + j]);
            }

            // IF v < tst : target
            if (re->len > 0) {
                auto cmpEqBlk = builder.newBbUnlinked();
                auto cmpLvalLt = this->pushCompare(valLen.clone(), ::MIR::eBinOp::LT, valTst.clone());
                builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLvalLt), defBlk, cmpEqBlk}));
                builder.setCurBlock(cmpEqBlk);
            }

            // IF v == tst : target
            {
                auto nextCmpBlk = builder.newBbUnlinked();
                auto cmpLvalEq = this->pushCompare(valLen.clone(), ::MIR::eBinOp::EQ, mv$(valTst));
                builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLvalEq), armTargets[tgtOfs], nextCmpBlk}));
                builder.setCurBlock(nextCmpBlk);
            }
        } else if (const auto* re = r.opt_Value()) {
            ASSERT_BUG(sp, re->is_Bytes(), "Slice with non-Bytes value - " << *re);
            const auto& b = re->as_Bytes();

            auto valTstLen = ::MIR::Constant::make_Uint({U128(b.size()), ::HIR::CoreType::Usize});

            // IF v == tst : target
            {
                auto nextCmpBlk = builder.newBbUnlinked();

                // TODO: What if `val` isn't a Deref?
                ASSERT_BUG(sp, !val.wrappers.empty() && val.wrappers.back().is_Deref(), "TODO: Handle non-Deref matches of byte strings - " << val);
                auto& types = builder.resolve().crate.types;
                auto cmpSliceVal = builder.lvalueOrTemp(sp, types.borrow(::HIR::BorrowType::Shared, types.slice(types.primitive(::HIR::CoreType::U8))), ::MIR::RValue::make_MakeDst({::MIR::Param(re->clone()), valTstLen.clone()}));
                auto cmpLvalEq = this->pushCompare(val.cloneUnwrapped(), ::MIR::eBinOp::EQ, mv$(cmpSliceVal));
                builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLvalEq), armTargets[tgtOfs], nextCmpBlk}));

                builder.setCurBlock(nextCmpBlk);
            }
        } else {
            BUG(sp, "Matching without _Slice pattern - " << r.tagStr() << " - " << r);
        }

        tgtOfs += rules[i].size();
    }
    builder.endBlock(::MIR::Terminator::make_Goto(defBlk));
}

void MatchGenGrouped::genDispatchRange(const fieldPathT& field_path, const ::MIR::Constant& first, const ::MIR::Constant& last, bool isInclusive, ::MIR::BasicBlockId defBlk) {
    TRACE_FUNCTION_F("field_path=" << field_path << ", " << first << " .." << (isInclusive ? "=" : "") << " " << last);
    ::MIR::LValue val;
    ::HIR::TypeRef ty;
    getTyAndVal(sp, builder, topTy, topVal, field_path, fieldPathOfs, ty, val);
    DEBUG("ty = " << ty << ", val = " << val);

    if (const auto* tep = ty->opt_Primitive()) {
        auto te = *tep;

        bool lowerPossible = true;
        bool upperPossible = true;

        switch (te) {
            case ::HIR::CoreType::Bool:
                BUG(sp, "Range match over Bool");
                break;
            case ::HIR::CoreType::Str:
                BUG(sp, "Range match over Str - is this valid?");
                break;
            case ::HIR::CoreType::U8:
            case ::HIR::CoreType::U16:
            case ::HIR::CoreType::U32:
            case ::HIR::CoreType::U64:
            case ::HIR::CoreType::U128:
            case ::HIR::CoreType::Usize:
                lowerPossible = (first.as_Uint().v > 0);
                // TODO: Should this also check for the end being the max value of the type?
                // - Can just leave that to the optimiser
                upperPossible = isInclusive ? (last.as_Uint().v < U128::max()) : true;
                break;
            case ::HIR::CoreType::I8:
            case ::HIR::CoreType::I16:
            case ::HIR::CoreType::I32:
            case ::HIR::CoreType::I64:
            case ::HIR::CoreType::I128:
            case ::HIR::CoreType::Isize:
                lowerPossible = (first.as_Int().v > S128::min());
                upperPossible = isInclusive ? (last.as_Int().v < S128::max()) : true;
                break;
            case ::HIR::CoreType::Char:
                lowerPossible = (first.as_Uint().v > 0);
                upperPossible = isInclusive ? (last.as_Uint().v <= 0x10FFFF) : (last.as_Uint().v < 0x10FFFF);
                break;
            case ::HIR::CoreType::F16:
            case ::HIR::CoreType::F32:
            case ::HIR::CoreType::F64:
            case ::HIR::CoreType::F128:
                // NOTE: No upper or lower limits
                lowerPossible = (first.as_Float().v > -std::numeric_limits<double>::infinity());
                upperPossible = (last.as_Float().v < std::numeric_limits<double>::infinity());
                break;
        }

        if (lowerPossible) {
            auto testBb2 = builder.newBbUnlinked();
            // IF `val` < `first` : fail_bb
            auto cmpLtLval = builder.getRvalInIfCond(sp, ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), ::MIR::eBinOp::LT, ::MIR::Param(first.clone())}));
            builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLtLval), defBlk, testBb2}));

            builder.setCurBlock(testBb2);
        }

        if (upperPossible) {
            auto succBb = builder.newBbUnlinked();

            // IF `val` > `last` : fail_bb
            auto op = isInclusive ? ::MIR::eBinOp::GT : ::MIR::eBinOp::GE;
            auto cmpGtLval = builder.getRvalInIfCond(sp, ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), op, ::MIR::Param(last.clone())}));
            builder.endBlock(::MIR::Terminator::make_If({mv$(cmpGtLval), defBlk, succBb}));

            builder.setCurBlock(succBb);
        }
    } else {
        TODO(sp, "ValueRange on " << ty);
    }
}

void MatchGenGrouped::genDispatchSplitslice(const fieldPathT& field_path, const PatternRule::Data_SplitSlice& e, ::MIR::BasicBlockId defBlk) {
    TRACE_FUNCTION_F("field_path=" << field_path << ", [" << e.leading << ", .., " << e.trailing << "]");
    ::MIR::LValue val;
    ::HIR::TypeRef ty;
    getTyAndVal(sp, builder, topTy, topVal, field_path, fieldPathOfs, ty, val);
    DEBUG("ty = " << ty << ", val = " << val);

    ASSERT_BUG(sp, e.leading.size() == 0, "Sub-rules in MatchGenGrouped");
    ASSERT_BUG(sp, ty->is_Slice(), "SplitSlice pattern on non-slice - " << ty);

    // Obtain slice length
    auto valLen = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Usize), ::MIR::RValue::make_DstMeta({builder.getPtrToDst(sp, val)}));

    // 1. Check that length is sufficient for the pattern to be used
    // `IF len < min_len : def_blk, next
    {
        auto next = builder.newBbUnlinked();
        auto cmpVal = this->pushCompare(valLen.clone(), ::MIR::eBinOp::LT, ::MIR::Constant::make_Uint({U128(e.minLen), ::HIR::CoreType::Usize}));
        builder.endBlock(::MIR::Terminator::make_If({mv$(cmpVal), defBlk, next}));
        builder.setCurBlock(next);
    }

    // 2. Recurse into leading patterns.
    // TODO: This is dead code (leading patterns should have been expanded, and there's an assert above for it)
    if (e.minLen > e.trailingLen) {
        auto next = builder.newBbUnlinked();
        auto innerSet = tRulesSubset{1, /*is_arm_indexes=*/false};
        innerSet.pushBb(e.leading, next);
        auto inst = MatchGenGrouped{builder, sp, ty, val, {}, field_path.size()};
        inst.genForSlice(innerSet, 0, defBlk);

        builder.setCurBlock(next);
    }

    // 3. Recurse into trailing patterns
    if (e.trailingLen != 0) {
        auto next = builder.newBbUnlinked();
        auto innerSet = tRulesSubset{1, /*is_arm_indexes=*/false};
        innerSet.pushBb(e.trailing, next);
        auto inst = MatchGenGrouped{builder, sp, ty, val, {}, field_path.size()};
        inst.genForSlice(innerSet, 0, defBlk);

        builder.setCurBlock(next);
    }
}


// --------------------------------------------------------------------
// MirBuilder
// --------------------------------------------------------------------
MirBuilder::MirBuilder(const Span& sp, const StaticTraitResolve& resolve, const ::HIR::TypeData* ret_ty, const ::HIR::Function::argsT& args, ::MIR::Function& output)
    : rootSpan(sp)
    , mResolve(resolve)
    , retTy(ret_ty)
    , mArgs(args)
    , output(output)
    , mLangBox(nullptr)
    , blockActive(false)
    , resultValid(false)
    , fcnScope(*this, 0)
{
    if (resolve.crate.mLangItems.count("owned_box") > 0) {
        mLangBox = &resolve.crate.mLangItems.at("owned_box");
    }

    setCurBlock(newBbUnlinked());
    scopes.push_back(ScopeDef{sp, ScopeType::make_Owning({false, {}, {}})});
    scopeStack.push_back(0);

    scopes.push_back(ScopeDef{sp, ScopeType::make_Owning({true, {}, {}})});
    scopeStack.push_back(1);

    argStates.reserve(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        argStates.push_back(VarState::make_Valid({}));
    }
    slotStates.resize(output.locals.size());
    firstTempIdx = output.locals.size();
    DEBUG("First temporary will be " << firstTempIdx);

    ifCondLval = this->newTemporary(mResolve.crate.types.primitive(::HIR::CoreType::Bool));

    // Determine which variables can be replaced by arguents
    for (size_t i = 0; i < args.size(); i++) {
        const auto& pat = args[i].first;
        if (pat.mBindings.size() == 1 && pat.mBindings[0].mType == ::HIR::PatternBinding::Type::Move) {
            DEBUG("Argument shortcut: " << pat.mBindings[0] << " -> a" << i);
            varArgMappings[pat.mBindings[0].slot] = i;
        }
    }

    variableAliases.resize(output.locals.size());
}

void MirBuilder::finalCleanup() {
    TRACE_FUNCTION_F("");
    const auto& sp = rootSpan;
    if (block_active()) {
        if (retTy->is_Diverge()) {
            terminateScopeEarly(sp, fcn_scope());
            // Validation fails if this is reachable.
            //end_block( ::MIR::Terminator::make_Incomplete({}) );
            endBlock(::MIR::Terminator::make_Unreachable({}));
        } else {
            if (hasResult()) {
                pushStmtAssign(sp, ::MIR::LValue::newReturn(), getResult(sp));
            }

            terminateScopeEarly(sp, fcn_scope());

            endBlock(::MIR::Terminator::make_Return({}));
        }
    } else {
        terminateScope(sp, ScopeHandle(*this, 1), /*emit_cleanup=*/false);
        terminateScope(sp, mv$(fcnScope), /*emit_cleanup=*/false);
    }

    // Rewrite drop flags
    // - Expand recursive lookups
    for (;;) {
        bool added = false;
        for (auto& a : dropFlagAliases) {
            auto& mappedFlags = a.second;
            // Iterate every "destination" flag
            for (size_t i = 0; i < mappedFlags.size(); i++) {
                auto it2 = dropFlagAliases.find(mappedFlags[i]);
                if (it2 != dropFlagAliases.end()) {
                    for (unsigned otherFlag : it2->second) {
                        // If this flag is not in the current list, add it and mark that something changed
                        if (std::find(mappedFlags.begin(), mappedFlags.end(), otherFlag) == mappedFlags.end()) {
                            mappedFlags.push_back(otherFlag);
                            added = true;
                        }
                    }
                }
            }
        }
        if (!added) {
            break;
        }
    }

    for (auto& b : output.blocks) {
        for (auto it = b.statements.begin(); it != b.statements.end(); ++it) {
            // NOTE: Only need to worry about SetDropFlag, as the other ways of setting a flag are not generated yet.
            if (auto* p = it->opt_SetDropFlag()) {
                // Take a copy, which will be mutated to create the copies
                auto v = *p;
                auto dfIt = dropFlagAliases.find(v.idx);
                if (dfIt != dropFlagAliases.end()) {
                    // For each entry in `df_it->second`, add a copy of this SetDropFlag _before_ `it` (so it doesn't get re-visited)
                    for (unsigned otherIdx : dfIt->second) {
                        v.idx = otherIdx;
                        // Ensure that `it` always points to the original
                        it = b.statements.insert(it, ::MIR::Statement(v)) + 1;
                    }
                }
            }
        }
    }
}

const ::HIR::TypeData* MirBuilder::isTypeOwnedBox(const ::HIR::TypeData* ty) const {
    if (mLangBox) {
        if (!ty->is_Path()) {
            return nullptr;
        }
        const auto& te = ty->as_Path();

        if (!te.path.mData.is_Generic()) {
            return nullptr;
        }
        const auto& pe = te.path.mData.as_Generic();

        if (pe.mPath != *mLangBox) {
            return nullptr;
        }
        // TODO: Properly assert the size?
        return pe.mParams.types.at(0);
    } else {
        return nullptr;
    }
}

void MirBuilder::scheduleVariableDrop(unsigned int idx) {
    registerVariableState(idx);
    scheduleRegisteredVariableDrop(idx);
}

void MirBuilder::registerVariableState(unsigned int idx) {
    DEBUG("REGISTER STATE (var) _" << idx << ": " << output.locals.at(idx));
    for (auto scopeIdx : ::reverse(scopeStack)) {
        auto& scopeDef = scopes.at(scopeIdx);
        TU_MATCH_DEF(
            ScopeType,
            (scopeDef.data),
            (e),
            (),
            (Owning,
             if (!e.isTemporary) {
                 auto it = ::std::find(e.slots.begin(), e.slots.end(), idx);
                 assert(it == e.slots.end());
                 e.slots.push_back(idx);
                 return;
             }),
            (Split, BUG(Span(), "Variable " << idx << " introduced within a Split");)
        )
    }
    BUG(Span(), "Variable " << idx << " introduced with no Variable scope");
}

void MirBuilder::scheduleRegisteredVariableDrop(unsigned int idx) {
    DEBUG("SCHEDULE DROP (var) _" << idx << ": " << output.locals.at(idx));
    for (auto scopeIdx : ::reverse(scopeStack)) {
        auto& scopeDef = scopes.at(scopeIdx);
        TU_MATCH_DEF(
            ScopeType,
            (scopeDef.data),
            (e),
            (),
            (Owning,
             if (!e.isTemporary) {
                 auto stateIt = ::std::find(e.slots.begin(), e.slots.end(), idx);
                 assert(stateIt != e.slots.end());
                 auto dropIt = ::std::find_if(e.dropSlots.begin(), e.dropSlots.end(), [&](const ScopeDropSlot& slot) {
                     return !slot.isArgument && slot.index == idx;
                 });
                 assert(dropIt == e.dropSlots.end());
                 e.dropSlots.push_back(ScopeDropSlot{false, idx});
                 return;
             }),
            (Split, BUG(Span(), "Variable " << idx << " scheduled within a Split");)
        )
    }
    BUG(Span(), "Variable " << idx << " scheduled with no Variable scope");
}

void MirBuilder::scheduleArgumentDrop(unsigned int idx) {
    DEBUG("SCHEDULE DROP (arg) a" << idx << ": " << mArgs.at(idx).second);
    for (auto scopeIdx : ::reverse(scopeStack)) {
        auto& scopeDef = scopes.at(scopeIdx);
        TU_MATCH_DEF(
            ScopeType,
            (scopeDef.data),
            (e),
            (),
            (Owning,
             if (!e.isTemporary) {
                 auto it = ::std::find_if(e.dropSlots.begin(), e.dropSlots.end(), [&](const ScopeDropSlot& slot) {
                     return slot.isArgument && slot.index == idx;
                 });
                 assert(it == e.dropSlots.end());
                 e.dropSlots.push_back(ScopeDropSlot{true, idx});
                 return;
             }),
            (Split, BUG(Span(), "Argument " << idx << " introduced within a Split");)
        )
    }
    BUG(Span(), "Argument " << idx << " introduced with no Variable scope");
}

void MirBuilder::moveTemporaryDropToVariableScope(const Span& sp, const ::MIR::LValue& value, const ScopeHandle& source) {
    if (!value.root.is_Local() || !value.wrappers.empty()) {
        return;
    }
    const auto idx = value.root.as_Local();
    if (idx < firstTempIdx) {
        return;
    }

    ASSERT_BUG(sp, source.idx < scopes.size(), "Invalid temporary scope " << source);
    auto& sourceScope = scopes.at(source.idx);
    ASSERT_BUG(sp, sourceScope.data.is_Owning() && sourceScope.data.as_Owning().isTemporary, "Drop source is not a temporary scope: " << source);
    auto& sourceOwning = sourceScope.data.as_Owning();
    auto& sourceDrops = sourceOwning.dropSlots;
    auto sourceIt = ::std::find_if(sourceDrops.begin(), sourceDrops.end(), [&](const ScopeDropSlot& slot) {
        return !slot.isArgument && slot.index == idx;
    });
    if (sourceIt == sourceDrops.end()) {
        return;
    }
    auto sourceStateIt = ::std::find(sourceOwning.slots.begin(), sourceOwning.slots.end(), idx);
    ASSERT_BUG(sp, sourceStateIt != sourceOwning.slots.end(), "Missing state owner for " << value);

    bool sourceSeen = false;
    for (auto scopeIdx : ::reverse(scopeStack)) {
        if (scopeIdx == source.idx) {
            sourceSeen = true;
            continue;
        }
        if (!sourceSeen) {
            continue;
        }
        auto& scope = scopes.at(scopeIdx);
        if (auto* owning = scope.data.opt_Owning()) {
            if (!owning->isTemporary) {
                auto targetStateIt = ::std::find(owning->slots.begin(), owning->slots.end(), idx);
                ASSERT_BUG(sp, targetStateIt == owning->slots.end(), "Duplicate state owner for " << value);
                sourceOwning.slots.erase(sourceStateIt);
                sourceDrops.erase(sourceIt);
                owning->slots.push_back(idx);
                owning->dropSlots.push_back(ScopeDropSlot{false, idx});
                DEBUG("MOVE DROP " << value << " from scope " << source.idx << " to scope " << scopeIdx);
                return;
            }
        }
    }
    BUG(sp, "No variable scope outside temporary scope " << source);
}

void MirBuilder::moveVariableToScope(const Span& sp, unsigned int idx, const ScopeHandle& target) {
    ASSERT_BUG(sp, target.idx < scopes.size(), "Invalid `super let` target scope " << target);
    auto& targetScope = scopes.at(target.idx);
    ASSERT_BUG(sp, targetScope.data.is_Owning(), "`super let` target is not an owning scope: " << target);

    ScopeType::Data_Owning* source = nullptr;
    for (auto scopeIdx : ::reverse(scopeStack)) {
        auto* owning = scopes.at(scopeIdx).data.opt_Owning();
        if (!owning) {
            continue;
        }
        auto stateIt = ::std::find(owning->slots.begin(), owning->slots.end(), idx);
        if (stateIt != owning->slots.end()) {
            if (scopeIdx == target.idx) {
                return;
            }
            ASSERT_BUG(sp, !owning->isTemporary, "`super let` binding is already in a temporary scope");
            source = owning;
            owning->slots.erase(stateIt);
            break;
        }
    }
    ASSERT_BUG(sp, source, "`super let` binding _" << idx << " has no lexical scope");

    auto dropIt = ::std::find_if(source->dropSlots.begin(), source->dropSlots.end(), [&](const ScopeDropSlot& slot) {
        return !slot.isArgument && slot.index == idx;
    });
    ASSERT_BUG(sp, dropIt != source->dropSlots.end(), "`super let` binding _" << idx << " has no scheduled drop");
    source->dropSlots.erase(dropIt);

    auto& targetOwning = targetScope.data.as_Owning();
    ASSERT_BUG(sp, ::std::find(targetOwning.slots.begin(), targetOwning.slots.end(), idx) == targetOwning.slots.end(), "Duplicate `super let` state owner for _" << idx);
    targetOwning.slots.push_back(idx);
    targetOwning.dropSlots.push_back(ScopeDropSlot{false, idx});
}

void MirBuilder::dropLvalue(const Span& sp, const ::MIR::LValue& value) {
    auto* state = getValStateMutP(sp, value);
    ASSERT_BUG(sp, state, "Dropping invalid value " << value);
    dropValueFromState(sp, *state, value.clone());
}

::MIR::LValue MirBuilder::newTemporary(const ::HIR::TypeData* ty) {
    unsigned int rv = output.locals.size();
    DEBUG("DEFINE (temp) _" << rv << ": " << ty);

    assert(output.locals.size() == slotStates.size());
    output.locals.push_back(ty);
    slotStates.push_back(VarState::make_Invalid(InvalidType::Uninit));
    assert(output.locals.size() == slotStates.size());

    ScopeDef* topScope = nullptr;
    for (unsigned int i = scopeStack.size(); i--;) {
        auto idx = scopeStack[i];
        if (const auto* e = scopes.at(idx).data.opt_Owning()) {
            if (e->isTemporary) {
                topScope = &scopes.at(idx);
                break;
            }
        } else if (scopes.at(idx).data.is_Loop()) {
            // Newly created temporary within a loop, if there is a saved
            // state this temp needs a drop flag.
            // TODO: ^
        } else if (scopes.at(idx).data.is_Split()) {
            // Newly created temporary within a split, if there is a saved
            // state this temp needs a drop flag.
            // TODO: ^
        } else {
            // Nothign.
        }
    }
    assert(topScope);
    auto& tmpScope = topScope->data.as_Owning();
    assert(tmpScope.isTemporary);
    tmpScope.slots.push_back(rv);
    tmpScope.dropSlots.push_back(ScopeDropSlot{false, rv});
    return ::MIR::LValue::newLocal(rv);
}

::MIR::LValue MirBuilder::lvalueOrTemp(const Span& sp, const ::HIR::TypeData* ty, ::MIR::RValue val) {
    TU_IFLET(::MIR::RValue, val, Use, e, return mv$(e);)
    else {
        auto temp = newTemporary(ty);
        pushStmtAssign(sp, temp.clone(), mv$(val));
        return temp;
    }
}

::MIR::RValue MirBuilder::getResult(const Span& sp) {
    if (!resultValid) {
        BUG(sp, "No value avaliable");
    }
    auto rv = mv$(result);
    resultValid = false;
    DEBUG(rv);
    return rv;
}

::MIR::LValue MirBuilder::getResultUnwrapLvalue(const Span& sp) {
    auto rv = getResult(sp);
    TU_IFLET(::MIR::RValue, rv, Use, e, return mv$(e);)
    else {
        BUG(sp, "LValue expected, got RValue");
    }
}

::MIR::LValue MirBuilder::getResultInLvalue(const Span& sp, const ::HIR::TypeData* ty, bool allowMissingValue /*=false*/) {
    if (allowMissingValue && !block_active()) {
        return newTemporary(ty);
    }
    auto rv = getResult(sp);
    TU_IFLET(::MIR::RValue, rv, Use, e, return mv$(e);)
    else {
        auto temp = newTemporary(ty);
        pushStmtAssign(sp, ::MIR::LValue(temp.clone()), mv$(rv));
        return temp;
    }
}

::MIR::Param MirBuilder::getResultInParam(const Span& sp, const ::HIR::TypeData* ty, bool allowMissingValue) {
    if (allowMissingValue && !block_active()) {
        return newTemporary(ty);
    }

    auto rv = getResult(sp);
    if (auto* e = rv.opt_Constant()) {
        return mv$(*e);
    }
    //else if( auto* e = rv.opt_Use() )
    //{
    //    return mv$(*e);
    //}
    else {
        auto temp = newTemporary(ty);
        pushStmtAssign(sp, ::MIR::LValue(temp.clone()), mv$(rv));
        return ::MIR::Param(mv$(temp));
    }
}

void MirBuilder::setResult(const Span& sp, ::MIR::RValue val) {
    if (resultValid) {
        BUG(sp, "Pushing a result over an existing result");
    }
    result = mv$(val);
    resultValid = true;
    DEBUG(result);
}

void MirBuilder::pushStmtAssign(const Span& sp, ::MIR::LValue dst, ::MIR::RValue val, bool updateDestState /*=true*/) {
    DEBUG(dst << " = " << val);
    ASSERT_BUG(sp, blockActive, "Pushing statement with no active block");

    auto movedParam = [&](const ::MIR::Param& p) {
        if (const auto* e = p.opt_LValue()) {
            this->movedLvalue(sp, *e);
        }
    };
    TU_MATCHA(
        (val),
        (e),
        (Use, this->movedLvalue(sp, e);),
        (Constant, ),
        (SizedArray, movedParam(e.val);),
        (Borrow,
         if (e.type == ::HIR::BorrowType::Owned) {
             TODO(sp, "Move using &move");
             // Likely would require a marker that ensures that the memory isn't reused.
             this->movedLvalue(sp, e.val);
         } else {
             // Doesn't move
         }),
        (Cast, this->movedLvalue(sp, e.val);),
        (BinOp,
         switch (e.op) {
             case ::MIR::eBinOp::EQ:
             case ::MIR::eBinOp::NE:
             case ::MIR::eBinOp::GT:
             case ::MIR::eBinOp::GE:
             case ::MIR::eBinOp::LT:
             case ::MIR::eBinOp::LE:
                 // Takes an implicit borrow... and only works on copy, so why is this block here?
                 break;
             default:
                 movedParam(e.valL);
                 movedParam(e.valR);
                 break;
         }),
        (UniOp, this->movedLvalue(sp, e.val);),
        (
            DstMeta,
            // Doesn't move
        ),
        (
            DstPtr,
            // Doesn't move
        ),
        (MakeDst, movedParam(e.ptrVal); movedParam(e.metaVal);),
        (Tuple, for (const auto& val : e.vals) movedParam(val);),
        (Array, for (const auto& val : e.vals) movedParam(val);),
        (UnionVariant, movedParam(e.val);),
        (EnumVariant, for (const auto& val : e.vals) movedParam(val);),
        (Struct, for (const auto& val : e.vals) movedParam(val);)
    )

    // Drop target if populated
    if (updateDestState) {
        markValueAssigned(sp, dst);
    }
    this->pushStmt(sp, ::MIR::Statement::make_Assign({mv$(dst), mv$(val)}));
}

void MirBuilder::pushStmtDrop(const Span& sp, ::MIR::LValue val, unsigned int flag /*=~0u*/) {
    ASSERT_BUG(sp, blockActive, "Pushing statement with no active block");

    if (lvalueIsCopy(sp, val)) {
        // Don't emit a drop for Copy values
        return;
    }

    this->pushDropTerminator(sp, ::MIR::eDropKind::DEEP, mv$(val), flag);
}

void MirBuilder::pushStmtDropShallow(const Span& sp, ::MIR::LValue val, unsigned int flag /*=~0u*/) {
    ASSERT_BUG(sp, blockActive, "Pushing statement with no active block");

    // TODO: Ensure that the type is a Box?

    this->pushDropTerminator(sp, ::MIR::eDropKind::SHALLOW, mv$(val), flag);
}

void MirBuilder::pushDropTerminator(const Span& sp, ::MIR::eDropKind kind, ::MIR::LValue val, unsigned int flag) {
    ASSERT_BUG(sp, blockActive, "Dropping a value with no active block");

    const auto nextBlock = newBbUnlinked();
    auto unwind = buildingCleanup
        ? ::MIR::UnwindAction::make_Terminate({})
        : makeUnwindAction(sp, &val);
    endBlock(::MIR::Terminator::make_Drop({kind, mv$(val), flag, nextBlock, mv$(unwind)}));
    setCurBlock(nextBlock);
}

void MirBuilder::pushStmtAsm(const Span& sp, ::MIR::Statement::Data_Asm data) {
    ASSERT_BUG(sp, blockActive, "Pushing statement with no active block");

    // 1. Mark outputs as valid
    for (const auto& v : data.outputs) {
        markValueAssigned(sp, v.second);
    }

    // 2. Push
    this->pushStmt(sp, ::MIR::Statement::make_Asm(mv$(data)));
}

void MirBuilder::pushStmtSetDropflagVal(const Span& sp, unsigned int idx, bool value) {
    this->pushStmt(sp, ::MIR::Statement::make_SetDropFlag({idx, value, ~0u}));
}

void MirBuilder::pushStmtSetDropflagOther(const Span& sp, unsigned int idx, unsigned int other) {
    this->pushStmt(sp, ::MIR::Statement::make_SetDropFlag({idx, false, other}));
}

void MirBuilder::pushStmtSetDropflagDefault(const Span& sp, unsigned int idx) {
    this->pushStmt(sp, ::MIR::Statement::make_SetDropFlag({idx, this->getDropFlagDefault(sp, idx), ~0u}));
}

void MirBuilder::pushStmt(const Span& sp, ::MIR::Statement stmt) {
    ASSERT_BUG(sp, blockActive, "Pushing statement with no active block");
    auto& blk = output.blocks.at(currentBlock);
    DEBUG("BB" << currentBlock << "/" << blk.statements.size() << " = " << stmt);
    blk.statements.push_back(mv$(stmt));
}

void MirBuilder::markValueAssigned(const Span& sp, const ::MIR::LValue& dst) {
    if (dst.root.is_Return()) {
        ASSERT_BUG(sp, dst.wrappers.empty(), "Assignment to a component of the return value should be impossible.");
        return;
    }
    VarState* stateP = getValStateMutP(sp, dst, /*expect_valid=*/true);

    if (stateP) {
        TU_IFLET(VarState, (*stateP), Invalid, se, ASSERT_BUG(sp, se != InvalidType::Descoped, "Assining of descoped variable - " << dst);)
        dropValueFromState(sp, *stateP, dst.clone());
        auto newState = VarState::make_Valid({});
        DEBUG("State " << dst << " " << *stateP << " => " << newState);
        *stateP = std::move(newState);
    } else {
        // Assigning into non-tracked locations still causes a drop
        auto state = VarState::make_Valid({});
        dropValueFromState(sp, state, dst.clone());
    }
}

void MirBuilder::raiseTemporaries(const Span& sp, const ::MIR::LValue& val, const ScopeHandle& scope, bool toAbove /*=false*/) {
    TRACE_FUNCTION_F(val);
    for (const auto& w : val.wrappers) {
        if (w.is_Index()) {
            // Raise index temporary
            raiseTemporaries(sp, ::MIR::LValue::newLocal(w.as_Index()), scope, toAbove);
        }
    }
    if (!val.root.is_Local()) {
        // No raising of these source values?
        return;
    }
    const auto idx = val.root.as_Local();
    bool isTemp = (idx >= firstTempIdx);
    /*
    if( !is_temp ) {
        return ;
    }
    */

    // Find controlling scope
    auto scopeIt = scopeStack.rbegin();
    while (scopeIt != scopeStack.rend()) {
        auto& scopeDef = scopes.at(*scopeIt);

        if (*scopeIt == scope.idx && !toAbove) {
            DEBUG(val << " defined in or above target (scope " << scope << ")");
        }

        TU_IFLET(
            ScopeType,
            scopeDef.data,
            Owning,
            e,
            if (e.isTemporary == isTemp) {
                auto tmpIt = ::std::find(e.slots.begin(), e.slots.end(), idx);
                if (tmpIt != e.slots.end()) {
                    e.slots.erase(tmpIt);
                    auto dropIt = ::std::find_if(e.dropSlots.begin(), e.dropSlots.end(), [&](const ScopeDropSlot& slot) {
                        return !slot.isArgument && slot.index == idx;
                    });
                    ASSERT_BUG(sp, dropIt != e.dropSlots.end(), "Missing drop schedule for " << val);
                    e.dropSlots.erase(dropIt);
                    DEBUG("Raise slot " << idx << " from " << *scopeIt);
                    break;
                }
            } else {
                // TODO: Should this care about variables?
            }
        )
        else {
            // TODO: Does this need to handle this value being set in the
            // split scopes?
        }
        // If the variable was defined above the desired scope (i.e. this didn't find it), return
        if (*scopeIt == scope.idx) {
            DEBUG("Value " << val << " is defined above the target (scope " << scope << ")");
            return;
        }
        ++scopeIt;
    }
    if (scopeIt == scopeStack.rend()) {
        // Temporary wasn't defined in a visible scope?
        BUG(sp, val << " wasn't defined in a visible scope");
        return;
    }

    // If the definition scope was the target scope
    bool targetSeen = false;
    if (*scopeIt == scope.idx) {
        if (toAbove) {
            // Want to shift to any above (but not including) it
            ++scopeIt;
        } else {
            // Want to shift to it or above.
        }

        targetSeen = true;
    } else {
        // Don't bother searching the original definition scope
        ++scopeIt;
    }

    // Iterate stack until:
    // - The target scope is seen
    // - AND a scope was found for it
    for (; scopeIt != scopeStack.rend(); ++scopeIt) {
        auto& scopeDef = scopes.at(*scopeIt);
        DEBUG("> Cross " << *scopeIt << " - " << scopeDef.data.tagStr());

        if (*scopeIt == scope.idx) {
            targetSeen = true;
        }

        TU_MATCH_HDRA((scopeDef.data), {)
        TU_ARMA(Owning, e) {
                if (targetSeen && e.isTemporary == isTemp) {
                    e.slots.push_back(idx);
                    e.dropSlots.push_back(ScopeDropSlot{false, idx});
                    DEBUG("- to " << *scopeIt);
                    return;
                }
            }
            TU_ARMA(Loop, sdLoop) {
                // If there is an exit state present, ensure that this variable is
                // present in that state (as invalid, as it can't have been valid
                // externally)
                if (sdLoop.exitStateValid) {
                    DEBUG("Adding " << val << " as unset to loop exit state");
                    auto v = sdLoop.exitState.states.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                    ASSERT_BUG(sp, v.second, "Raising " << val << " which already had a state entry");
                } else {
                    DEBUG("Crossing loop with no existing exit state");
                }
            }
            TU_ARMA(Split, sdSplit) {
                // If the split has already registered an exit state, ensure that
                // this variable is present in it. (as invalid)
                if (sdSplit.endStateValid) {
                    DEBUG("Adding " << val << " as unset to loop exit state");
                    auto v = sdSplit.endState.states.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                    ASSERT_BUG(sp, v.second, "Raising " << val << " which already had a state entry");
                } else {
                    DEBUG("Crossing split with no existing end state");
                }

                // TODO: This should update the outer state to unset.
                auto& arm = sdSplit.arms.back();
                arm.states.insert(::std::make_pair(idx, getSlotState(sp, idx, SlotType::Local).clone()));
                slotStates.at(idx) = VarState(InvalidType::Uninit);
            }
            TU_ARMA(Freeze, sde) {
                // Can we raise across a freeze state?
                if (!sde.unfrozen) {
                    TODO(sp, "Raising temporary across a freeze?");
                }
            }
        }
    }
    BUG(sp, "Couldn't find a scope to raise " << val << " into");
}

void MirBuilder::raiseTemporaries(const Span& sp, const ::MIR::RValue& rval, const ScopeHandle& scope, bool toAbove /*=false*/) {
    auto raiseVars = [&](const ::MIR::Param& p) {
        if (const auto* e = p.opt_LValue()) {
            this->raiseTemporaries(sp, *e, scope, toAbove);
        }
    };
    TU_MATCHA(
        (rval),
        (e),
        (Use, this->raiseTemporaries(sp, e, scope, toAbove);),
        (Constant, ),
        (SizedArray, raiseVars(e.val);),
        (Borrow,
         // TODO: Wait, is this valid?
         this->raiseTemporaries(sp, e.val, scope, toAbove);),
        (Cast, this->raiseTemporaries(sp, e.val, scope, toAbove);),
        (BinOp, raiseVars(e.valL); raiseVars(e.valR);),
        (UniOp, this->raiseTemporaries(sp, e.val, scope, toAbove);),
        (DstMeta, this->raiseTemporaries(sp, e.val, scope, toAbove);),
        (DstPtr, this->raiseTemporaries(sp, e.val, scope, toAbove);),
        (MakeDst, raiseVars(e.ptrVal); raiseVars(e.metaVal);),
        (Tuple, for (const auto& val : e.vals) raiseVars(val);),
        (Array, for (const auto& val : e.vals) raiseVars(val);),
        (UnionVariant, raiseVars(e.val);),
        (EnumVariant, for (const auto& val : e.vals) raiseVars(val);),
        (Struct, for (const auto& val : e.vals) raiseVars(val);)
    )
}

MirBuilder::SaveCodeProto MirBuilder::codeSaveStart() {
    TRACE_FUNCTION;
    // Push to the stack
    // Create a new block and link in
    static size_t sNextIndex;
    SaveCodeProto rv;
    rv.index = sNextIndex++;
    codeSaveStack.push_back(CodeSaveStackEnt{rv.index, {}});
    // If currently in a block, then go into a new one
    if (block_active()) {
        newBbLinked();
    }
    return rv;
}

MirBuilder::SavedCode MirBuilder::codeSaveEnd(SaveCodeProto h) {
    // Check stack
    assert(!block_active()); // Can't be a block active
    assert(!codeSaveStack.empty());
    assert(h.index == codeSaveStack.back().index);
    SavedCode rv;
    rv.blocks = std::move(codeSaveStack.back().blocks);
    codeSaveStack.pop_back();
    DEBUG("rv.blocks = { " << rv.blocks << " }");
    return rv;
}

void MirBuilder::insertCloned(const Span& sp, const SavedCode& c, CloneMapper& mapper) {
    TRACE_FUNCTION;
    assert(block_active()); // Need an active block to start inserting
    if (!c.blocks.empty()) {
        struct Cloner: ::MIR::Cloner {
            CloneMapper& mapper;
            std::map<unsigned, unsigned> newBlockMap;

            Cloner(const Span& sp, CloneMapper& mapper, HIR::TypeInterner& types)
                : ::MIR::Cloner(sp, types)
                , mapper(mapper)
            {
            }

            ::MIR::BasicBlockId mapBbIdx(::MIR::BasicBlockId idx) const override {
                auto it = newBlockMap.find(idx);
                if (it != newBlockMap.end()) {
                    return it->second;
                }
                return mapper.updateBbRef(idx);
            }
        } cloner{sp, mapper, mResolve.crate.types};

        // Allocate new block IDs for all referenced blocks
        for (auto bbIdx : c.blocks) {
            cloner.newBlockMap.insert(std::make_pair(bbIdx, newBbUnlinked()));
        }
        // End the current block with a goto to the first block
        endBlock(::MIR::Terminator::make_Goto({cloner.newBlockMap[c.blocks.front()]}));

        DEBUG("c.blocks = [" << c.blocks << "]");
        DEBUG("new_block_map = {" << cloner.newBlockMap << "}");
        // Start inserting (and remapping)
        for (auto srcIdx : c.blocks) {
            auto newIdx = cloner.newBlockMap.at(srcIdx);
            DEBUG("BB" << newIdx << " <= BB" << srcIdx);
            const auto& src = output.blocks[srcIdx];
            setCurBlock(newIdx);
            for (const auto& v : src.statements) {
                pushStmt(sp, cloner.cloneStmt(v));
            }
            endBlock(cloner.cloneTerm(src.terminator));
        }
        // Leave no active block
    }
}

void MirBuilder::setCurBlock(unsigned int newBlock) {
    ASSERT_BUG(Span(), !blockActive, "Updating block when previous is active");
    ASSERT_BUG(Span(), newBlock < output.blocks.size(), "Invalid block ID being started - " << newBlock);
    ASSERT_BUG(Span(), output.blocks[newBlock].terminator.is_Incomplete(), "Attempting to resume a completed block - BB" << newBlock);
    // Record this new block in the save stack entries
    for (auto& v : codeSaveStack) {
        // Just in case a block is saved+resumed
        if (std::find(v.blocks.begin(), v.blocks.end(), newBlock) == v.blocks.end()) {
            v.blocks.push_back(newBlock);
        }
    }
    DEBUG("BB" << newBlock << " START");
    currentBlock = newBlock;
    blockActive = true;
}

void MirBuilder::endBlock(::MIR::Terminator term) {
    if (!blockActive) {
        BUG(Span(), "Terminating block when none active");
    }
    DEBUG("BB" << currentBlock << " END -> " << term);
    output.blocks.at(currentBlock).terminator = mv$(term);
    blockActive = false;
    currentBlock = 0;
}

::MIR::BasicBlockId MirBuilder::pauseCurBlock() {
    if (!blockActive) {
        BUG(Span(), "Pausing block when none active");
    }
    DEBUG("BB" << currentBlock << " PAUSE");
    blockActive = false;
    auto rv = currentBlock;
    currentBlock = 0;
    return rv;
}

::MIR::BasicBlockId MirBuilder::newBbLinked() {
    auto rv = newBbUnlinked();
    DEBUG("BB" << rv);
    endBlock(::MIR::Terminator::make_Goto(rv));
    setCurBlock(rv);
    return rv;
}

::MIR::BasicBlockId MirBuilder::newBbUnlinked() {
    auto rv = output.blocks.size();
    DEBUG("BB" << rv);
    output.blocks.push_back({});
    output.blocks.back().isCleanup = buildingCleanup;
    return rv;
}

unsigned int MirBuilder::newDropFlag(bool defaultState) {
    auto rv = output.dropFlags.size();
    output.dropFlags.push_back(defaultState);
    for (size_t i = scopeStack.size(); i--;) {
        if (auto* e = scopes.at(scopeStack[i]).data.opt_Loop()) {
            e->dropFlags.push_back(rv);
            break;
        }
    }
    DEBUG("df$" << rv << " := " << defaultState);
    return rv;
}

unsigned int MirBuilder::newDropFlagAndSet(const Span& sp, bool setState) {
    auto rv = newDropFlag(!setState);
    pushStmtSetDropflagVal(sp, rv, setState);
    return rv;
}

bool MirBuilder::getDropFlagDefault(const Span& sp, unsigned int idx) {
    return output.dropFlags.at(idx);
}

void MirBuilder::dropFlagAlias(unsigned int oldIdx, unsigned int newIdx) {
    dropFlagAliases[oldIdx].push_back(newIdx);
}

ScopeHandle MirBuilder::newScopeVar(const Span& sp) {
    unsigned int idx = scopes.size();
    scopes.push_back(ScopeDef{sp, ScopeType::make_Owning({false, {}, {}})});
    scopeStack.push_back(idx);
    DEBUG("START (var) scope " << idx);
    return ScopeHandle{*this, idx};
}

ScopeHandle MirBuilder::newScopeTemp(const Span& sp) {
    unsigned int idx = scopes.size();

    scopes.push_back(ScopeDef{sp, ScopeType::make_Owning({true, {}, {}})});
    scopeStack.push_back(idx);
    DEBUG("START (temp) scope " << idx);
    return ScopeHandle{*this, idx};
}

ScopeHandle MirBuilder::newScopeSplit(const Span& sp) {
    unsigned int idx = scopes.size();
    scopes.push_back(ScopeDef{sp, ScopeType::make_Split({})});
    scopes.back().data.as_Split().arms.push_back({});
    scopeStack.push_back(idx);
    DEBUG("START (split) scope " << idx);
    return ScopeHandle{*this, idx};
}

ScopeHandle MirBuilder::newScopeLoop(const Span& sp) {
    unsigned int idx = scopes.size();
    scopes.push_back(ScopeDef{sp, ScopeType::make_Loop({})});
    scopes.back().data.as_Loop().entryBb = currentBlock;
    scopeStack.push_back(idx);
    DEBUG("START (loop) scope " << idx);
    return ScopeHandle{*this, idx};
}

ScopeHandle MirBuilder::newScopeFreeze(const Span& sp) {
    unsigned int idx = scopes.size();
    scopes.push_back(ScopeDef{sp, ScopeType::make_Freeze({})});
    scopeStack.push_back(idx);
    DEBUG("START (freeze) scope " << idx);
    return ScopeHandle{*this, idx};
}

void MirBuilder::terminateScope(const Span& sp, ScopeHandle scope, bool emitCleanup /*=true*/) {
    TRACE_FUNCTION_F("DONE scope " << scope.idx << " - " << (emitCleanup ? "CLEANUP" : "NO CLEANUP"));
    // 1. Check that this is the current scope (at the top of the stack)
    if (scopeStack.empty() || scopeStack.back() != scope.idx) {
        DEBUG("- m_scope_stack = [" << scopeStack << "]");
        auto it = ::std::find(scopeStack.begin(), scopeStack.end(), scope.idx);
        if (it == scopeStack.end()) {
            BUG(sp, "Terminating scope not on the stack - scope " << scope.idx);
        }
        BUG(sp, "Terminating scope " << scope.idx << " when not at top of stack, " << (scopeStack.end() - it - 1) << " scopes in the way");
    }

    auto& scopeDef = scopes.at(scope.idx);
    //if( emit_cleanup ) {
    //    ASSERT_BUG( sp, scope_def.complete == false, "Terminating scope which is already terminated" );
    //}

    if (emitCleanup && scopeDef.complete == false) {
        // 2. Emit drops for all non-moved variables (share with below)
        dropScopeValues(scopeDef);

// Emit ScopeEnd for all controlled values
    }

    // 3. Pop scope (last because `drop_scope_values` uses the stack)
    scopeStack.pop_back();

    completeScope(scopeDef);
}

void MirBuilder::raiseAll(const Span& sp, ScopeHandle source, const ScopeHandle& target) {
    TRACE_FUNCTION_F("scope " << source.idx << " => " << target.idx);

    // 1. Check that this is the current scope (at the top of the stack)
    if (scopeStack.empty() || scopeStack.back() != source.idx) {
        DEBUG("- m_scope_stack = [" << scopeStack << "]");
        auto it = ::std::find(scopeStack.begin(), scopeStack.end(), source.idx);
        if (it == scopeStack.end()) {
            BUG(sp, "Terminating scope not on the stack - scope " << source.idx);
        }
        BUG(sp, "Terminating scope " << source.idx << " when not at top of stack, " << (scopeStack.end() - it - 1) << " scopes in the way");
    }
    auto& srcScopeDef = scopes.at(source.idx);

    ASSERT_BUG(sp, srcScopeDef.data.is_Owning(), "Rasising scopes can only be done on temporaries (source)");
    ASSERT_BUG(sp, srcScopeDef.data.as_Owning().isTemporary, "Rasising scopes can only be done on temporaries (source)");
    auto& srcList = srcScopeDef.data.as_Owning().slots;
    for (auto idx : srcList) {
        DEBUG("> Raising " << ::MIR::LValue::newLocal(idx));
        assert(idx >= firstTempIdx);
    }

    // Seek up stack until the target scope is seen
    auto it = scopeStack.rbegin() + 1;
    for (; it != scopeStack.rend() && *it != target.idx; ++it) {
        auto& scopeDef = scopes.at(*it);
        DEBUG("Through S" << *it << ": " << scopeDef.data.tagStr());

        if (auto* sdLoop = scopeDef.data.opt_Loop()) {
            if (sdLoop->exitStateValid) {
                DEBUG("Crossing loop with existing end state");
                // Insert these values as Invalid, both in the existing exit state, and in the changed list
                for (auto idx : srcList) {
                    auto v = sdLoop->exitState.states.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                    ASSERT_BUG(sp, v.second, "");
                }
            } else {
                DEBUG("Crossing loop with no end state");
            }

            for (auto idx : srcList) {
                auto v2 = sdLoop->changedSlots.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                ASSERT_BUG(sp, v2.second, "");
            }
        } else if (auto* sdSplit = scopeDef.data.opt_Split()) {
            if (sdSplit->endStateValid) {
                DEBUG("Crossing split with existing end state");
                // Insert these indexes as Invalid
                for (auto idx : srcList) {
                    auto v = sdSplit->endState.states.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                    ASSERT_BUG(sp, v.second, "");
                }
            } else {
                DEBUG("Crossing split with no end state");
            }

            // TODO: Insert current state in the current arm
            assert(!sdSplit->arms.empty());
            auto& arm = sdSplit->arms.back();
            for (auto idx : srcList) {
                arm.states.insert(::std::make_pair(idx, mv$(slotStates.at(idx))));
                slotStates.at(idx) = VarState(InvalidType::Uninit);
            }
        }
    }
    if (it == scopeStack.rend()) {
        BUG(sp, "Moving values to a scope not on the stack - scope " << target.idx);
    }
    auto& tgtScopeDef = scopes.at(target.idx);
    DEBUG("To S" << target.idx << ": " << tgtScopeDef.data.tagStr());
    ASSERT_BUG(sp, tgtScopeDef.data.is_Owning(), "Rasising scopes can only be done on temporaries (target)");
    ASSERT_BUG(sp, tgtScopeDef.data.as_Owning().isTemporary, "Rasising scopes can only be done on temporaries (target)");

    // Move all defined variables from one to the other
    auto& tgtList = tgtScopeDef.data.as_Owning().slots;
    tgtList.insert(tgtList.end(), srcList.begin(), srcList.end());
    auto& srcDropList = srcScopeDef.data.as_Owning().dropSlots;
    auto& tgtDropList = tgtScopeDef.data.as_Owning().dropSlots;
    tgtDropList.insert(tgtDropList.end(), srcDropList.begin(), srcDropList.end());

    // Scope completed
    scopeStack.pop_back();
    srcScopeDef.complete = true;
}

void MirBuilder::terminateScopeEarly(const Span& sp, const ScopeHandle& scope, bool loopExit /*=false*/) {
    TRACE_FUNCTION_F("EARLY scope " << scope.idx);

    // 1. Ensure that this block is in the stack
    auto it = ::std::find(scopeStack.begin(), scopeStack.end(), scope.idx);
    if (it == scopeStack.end()) {
        BUG(sp, "Early-terminating scope not on the stack");
    }
    unsigned int slot = it - scopeStack.begin();

    bool useFrozenExitState = false;
    for (unsigned int i = scopeStack.size(); i-- > slot;) {
        const auto* freeze = scopes.at(scopeStack[i]).data.opt_Freeze();
        useFrozenExitState |= freeze && !freeze->unfrozen;
    }
    ASSERT_BUG(sp, !useFrozenExitState || !frozenExitStateActive, "Nested frozen early-exit state");
    if (useFrozenExitState) {
        frozenExitStateActive = true;
        frozenExitSlotStates.clear();
        frozenExitArgStates.clear();
    }

    bool isConditional = false;
    for (unsigned int i = scopeStack.size(); i-- > slot;) {
        auto idx = scopeStack[i];
        auto& scopeDef = scopes.at(idx);

        if (idx == scope.idx) {
            // If this is exiting a loop, save the state so the variable state after the loop is known.
            if (loopExit && scopeDef.data.is_Loop()) {
                terminateLoopEarly(sp, scopeDef.data.as_Loop());
            }
        }

        // If a conditional block is hit, prevent full termination of the rest
        if (scopeDef.data.is_Split() || scopeDef.data.is_Loop()) {
            isConditional = true;
        }

        if (!isConditional) {
            DEBUG("Complete scope " << idx);
            dropScopeValues(scopeDef);
            completeScope(scopeDef);
        } else {
            // Mark patial within this scope?
            DEBUG("Drop part of scope " << idx);

            // Emit drops for dropped values within this scope
            dropScopeValues(scopeDef);
            // Inform the scope that it's been early-exited
            TU_IFLET(ScopeType, scopeDef.data, Split, e, e.arms.back().hasEarlyTerminated = true;)
        }
    }

    if (useFrozenExitState) {
        frozenExitSlotStates.clear();
        frozenExitArgStates.clear();
        frozenExitStateActive = false;
    }

}

namespace {
    static void mergeOuterValidity(const Span& sp, MirBuilder& builder, unsigned int& oldFlag, bool newValid) {
        if (oldFlag == ~0u) {
            if (!newValid) {
                oldFlag = builder.newDropFlagAndSet(sp, false);
            }
        } else {
            builder.pushStmtSetDropflagVal(sp, oldFlag, newValid);
        }
    }

    static void mergeOuterValidity(const Span& sp, MirBuilder& builder, unsigned int& oldFlag, unsigned int newFlag) {
        if (oldFlag == newFlag) {
            return;
        }
        if (oldFlag == ~0u) {
            if (builder.getDropFlagDefault(sp, newFlag)) {
                oldFlag = newFlag;
            } else {
                oldFlag = builder.newDropFlag(true);
                builder.pushStmtSetDropflagOther(sp, oldFlag, newFlag);
            }
        } else {
            builder.pushStmtSetDropflagOther(sp, oldFlag, newFlag);
        }
    }

    static unsigned int mergeInvalidWithPartialOuter(const Span& sp, MirBuilder& builder, unsigned int newFlag) {
        const auto outerFlag = builder.newDropFlag(false);
        if (newFlag == ~0u) {
            builder.pushStmtSetDropflagVal(sp, outerFlag, true);
        } else {
            builder.pushStmtSetDropflagOther(sp, outerFlag, newFlag);
        }
        return outerFlag;
    }

    static void mergeState(const Span& sp, MirBuilder& builder, const ::MIR::LValue& lv, VarState& oldState, const VarState& newState) {
        TRACE_FUNCTION_FR(lv << " : " << oldState << " <= " << newState, lv << " : " << oldState);
        switch (oldState.tag()) {
            case VarState::TAGDEAD:
                throw "";
            case VarState::TAG_Invalid:
                switch (newState.tag()) {
                    case VarState::TAGDEAD:
                        throw "";
                    case VarState::TAG_Invalid:
                        // Invalid->Invalid :: Choose the highest of the invalid types (TODO)
                        return;
                    case VarState::TAG_Valid:
                        // Allocate a drop flag
                        oldState = VarState::make_Optional(builder.newDropFlagAndSet(sp, true));
                        return;
                    case VarState::TAG_Optional: {
                        // Was invalid, now optional.
                        auto flagIdx = newState.as_Optional();
                        if (true || builder.getDropFlagDefault(sp, flagIdx) != false) {
                            auto newFlag = builder.newDropFlag(false);
                            builder.pushStmtSetDropflagOther(sp, newFlag, flagIdx);
                            oldState = VarState::make_Optional(newFlag);
                        } else {
                            oldState = VarState::make_Optional(flagIdx);
                        }
                        return;
                    }
                    case VarState::TAG_MovedOut: {
                        const auto& nse = newState.as_MovedOut();

                        // Create a new state that is internally valid and uses the same drop flag
                        oldState = VarState::make_MovedOut({box$(oldState.clone()), nse.outerFlag});
                        auto& ose = oldState.as_MovedOut();
                        if (ose.outerFlag != ~0u) {
                            // If the flag's default isn't false, then create a new flag that does have such a default
                            // - Other arm (old_state) uses default, this arm (new_state) can be manipulated
                            if (builder.getDropFlagDefault(sp, ose.outerFlag) != false) {
                                auto newFlag = builder.newDropFlag(false);
                                builder.pushStmtSetDropflagOther(sp, newFlag, nse.outerFlag);
                                ose.outerFlag = newFlag;
                            }
                        } else {
                            // In the old arm, the container isn't valid. Create a drop flag with a default of false and set it to true
                            ose.outerFlag = builder.newDropFlag(false);
                            builder.pushStmtSetDropflagVal(sp, ose.outerFlag, true);
                        }

                        bool isBox = false;
                        builder.withValType(sp, lv, [&](const auto& ty) {
                            isBox = builder.isTypeOwnedBox(ty);
                        });
                        if (isBox) {
                            mergeState(sp, builder, ::MIR::LValue::newDeref(lv.clone()), *ose.innerState, *nse.innerState);
                        } else {
                            BUG(sp, "Handle MovedOut on non-Box");
                        }
                        return;
                    }
                    case VarState::TAG_Partial: {
                        const auto& nse = newState.as_Partial();
                        bool is_enum = false;
                        builder.withValType(sp, lv, [&](const auto& ty) {
                            is_enum = ty->is_Path() && ty->as_Path().binding.is_Enum();
                        });
                        const auto outerFlag = is_enum ? mergeInvalidWithPartialOuter(sp, builder, nse.outerFlag) : ~0u;

                        // Create a partial filled with Invalid
                        {
                            ::std::vector<VarState> inner;
                            inner.reserve(nse.innerStates.size());
                            for (size_t i = 0; i < nse.innerStates.size(); i++) {
                                inner.push_back(oldState.clone());
                            }
                            oldState = VarState::make_Partial({mv$(inner), outerFlag});
                        }
                        auto& ose = oldState.as_Partial();
                        if (is_enum) {
                            for (size_t i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, ::MIR::LValue::newDowncast(lv.clone(), static_cast<unsigned int>(i)), ose.innerStates[i], nse.innerStates[i]);
                            }
                        } else {
                            for (unsigned int i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, ::MIR::LValue::newField(lv.clone(), i), ose.innerStates[i], nse.innerStates[i]);
                            }
                        }
                    }
                        return;
                }
                break;
            // Valid <= ...
            case VarState::TAG_Valid:
                switch (newState.tag()) {
                    case VarState::TAGDEAD:
                        throw "";
                    // Valid <= Invalid
                    case VarState::TAG_Invalid:
                        oldState = VarState::make_Optional(builder.newDropFlagAndSet(sp, false));
                        return;
                    // Valid <= Valid
                    case VarState::TAG_Valid:
                        return;
                    // Valid <= Optional
                    case VarState::TAG_Optional: {
                        auto flagIdx = newState.as_Optional();
                        // Was valid, now optional.
                        if (builder.getDropFlagDefault(sp, flagIdx) != true) {
                            // Allocate a new drop flag with a default state of `true` and set it to this flag?
                            auto newFlag = builder.newDropFlag(true);
                            builder.pushStmtSetDropflagOther(sp, newFlag, flagIdx);
                            oldState = VarState::make_Optional(newFlag);
                        } else {
                            oldState = VarState::make_Optional(newState.as_Optional());
                        }
                        return;
                    }
                    // Valid <= MovedOut
                    case VarState::TAG_MovedOut: {
                        const auto& nse = newState.as_MovedOut();

                        // Create a new state that is internally valid and uses the same drop flag
                        oldState = VarState::make_MovedOut({box$(VarState::make_Valid({})), nse.outerFlag});
                        auto& ose = oldState.as_MovedOut();
                        if (ose.outerFlag != ~0u) {
                            // If the flag's default isn't true, then create a new flag that does have such a default
                            // - Other arm (old_state) uses default, this arm (new_state) can be manipulated
                            if (builder.getDropFlagDefault(sp, ose.outerFlag) != true) {
                                auto newFlag = builder.newDropFlag(true);
                                builder.pushStmtSetDropflagOther(sp, newFlag, nse.outerFlag);
                                ose.outerFlag = newFlag;
                            }
                        } else {
                            // In both arms, the container is valid. No need for a drop flag
                        }

                        bool isBox = false;
                        builder.withValType(sp, lv, [&](const auto& ty) {
                            isBox = builder.isTypeOwnedBox(ty);
                        });

                        if (isBox) {
                            mergeState(sp, builder, ::MIR::LValue::newDeref(lv.clone()), *ose.innerState, *nse.innerState);
                        } else {
                            BUG(sp, "MovedOut on non-Box");
                        }
                        return;
                    }
                    // Valid <= Partial
                    case VarState::TAG_Partial: {
                        const auto& nse = newState.as_Partial();
                        bool is_enum = false;
                        builder.withValType(sp, lv, [&](const auto& ty) {
                            is_enum = ty->is_Path() && ty->as_Path().binding.is_Enum();
                        });
                        unsigned int outerFlag = ~0u;
                        if (is_enum && nse.outerFlag != ~0u) {
                            mergeOuterValidity(sp, builder, outerFlag, nse.outerFlag);
                        }

                        // Create a partial filled with Valid
                        {
                            ::std::vector<VarState> inner;
                            inner.reserve(nse.innerStates.size());
                            for (size_t i = 0; i < nse.innerStates.size(); i++) {
                                inner.push_back(VarState::make_Valid({}));
                            }
                            oldState = VarState::make_Partial({mv$(inner), outerFlag});
                        }
                        auto& ose = oldState.as_Partial();
                        if (is_enum) {
                            auto ilv = ::MIR::LValue::newDowncast(lv.clone(), 0);
                            for (size_t i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, ilv, ose.innerStates[i], nse.innerStates[i]);
                                ilv.incDowncast();
                            }
                        } else {
                            auto ilv = ::MIR::LValue::newField(lv.clone(), 0);
                            for (unsigned int i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, ilv, ose.innerStates[i], nse.innerStates[i]);
                                ilv.incField();
                            }
                        }
                    }
                        return;
                }
                break;
            // Optional <= ...
            case VarState::TAG_Optional:
                switch (newState.tag()) {
                    case VarState::TAGDEAD:
                        throw "";
                    case VarState::TAG_Invalid:
                        builder.pushStmtSetDropflagVal(sp, oldState.as_Optional(), false);
                        return;
                    case VarState::TAG_Valid:
                        builder.pushStmtSetDropflagVal(sp, oldState.as_Optional(), true);
                        return;
                    case VarState::TAG_Optional:
                        if (oldState.as_Optional() != newState.as_Optional()) {
                            builder.pushStmtSetDropflagOther(sp, oldState.as_Optional(), newState.as_Optional());
                        }
                        return;
                    case VarState::TAG_MovedOut: {
                        // Should become `MovedOut` with a flag
                        // - If this `MovedOut` has a flag, then propagate that into the `Optional`'s flag and reset
                        if (newState.as_MovedOut().outerFlag != ~0u) {
                            if (oldState.as_Optional() != newState.as_MovedOut().outerFlag) {
                                builder.pushStmtSetDropflagOther(sp, oldState.as_Optional(), newState.as_MovedOut().outerFlag);
                            }
                        }
                        // Create an old state that just wraps a copy of the `Optional`
                        oldState = VarState::make_MovedOut({std::make_unique<VarState>(oldState.clone()), oldState.as_Optional()});

                        bool isBox = false;
                        builder.withValType(sp, lv, [&](const auto& ty) {
                            isBox = builder.isTypeOwnedBox(ty);
                        });

                        if (isBox) {
                            mergeState(sp, builder, ::MIR::LValue::newDeref(lv.clone()), *oldState.as_MovedOut().innerState, *newState.as_MovedOut().innerState);
                        } else {
                            BUG(sp, "MovedOut on non-Box");
                        }
                        return;
                    }
                    case VarState::TAG_Partial: {
                        const auto& nse = newState.as_Partial();
                        bool is_enum = false;
                        builder.withValType(sp, lv, [&](const auto& ty) {
                            assert(!builder.isTypeOwnedBox(ty));
                            is_enum = ty->is_Path() && ty->as_Path().binding.is_Enum();
                        });
                        const auto oldOptionalFlag = oldState.as_Optional();

                        // Create a Partial filled with copies of the Optional
                        // TODO: This can lead to contradictions when one field is moved and another not.
                        // - Need to allocate a new drop flag and handle the case where old_state is the state before the
                        //   split (and hence the default state of this new drop flag has to be the original state)
                        //  > Could store reference to start BB and assign into it?
                        //  > Can't it not be from before the split, because that would be a move when not known-valid?
                        //  > Re-assign and partial drop.
                        {
                            ::std::vector<VarState> inner;
                            inner.reserve(nse.innerStates.size());
                            for (size_t i = 0; i < nse.innerStates.size(); i++) {
                                auto newFlag = builder.newDropFlag(builder.getDropFlagDefault(sp, oldState.as_Optional()));
                                builder.dropFlagAlias(oldState.as_Optional(), newFlag);
                                inner.push_back(VarState::make_Optional(newFlag));
                            }
                            oldState = VarState::make_Partial({mv$(inner), is_enum ? oldOptionalFlag : ~0u});
                        }
                        auto& ose = oldState.as_Partial();
                        if (is_enum) {
                            if (nse.outerFlag == ~0u) {
                                mergeOuterValidity(sp, builder, ose.outerFlag, true);
                            } else {
                                mergeOuterValidity(sp, builder, ose.outerFlag, nse.outerFlag);
                            }
                        }
                        // Propagate to inners
                        if (is_enum) {
                            for (size_t i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, ::MIR::LValue::newDowncast(lv.clone(), static_cast<unsigned int>(i)), ose.innerStates[i], nse.innerStates[i]);
                            }
                        } else {
                            for (unsigned int i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, ::MIR::LValue::newField(lv.clone(), i), ose.innerStates[i], nse.innerStates[i]);
                            }
                        }
                        return;
                    }
                }
                break;
            case VarState::TAG_MovedOut: {
                auto& ose = oldState.as_MovedOut();
                bool isBox = false;
                builder.withValType(sp, lv, [&](const auto& ty) {
                    isBox = builder.isTypeOwnedBox(ty);
                });
                if (!isBox) {
                    BUG(sp, "MovedOut on non-Box");
                }
                switch (newState.tag()) {
                    case VarState::TAGDEAD:
                        throw "";
                    case VarState::TAG_Invalid:
                    case VarState::TAG_Valid: {
                        bool isValid = newState.is_Valid();
                        if (ose.outerFlag == ~0u) {
                            // If not valid in new arm, then the outer state is conditional
                            if (!isValid) {
                                ose.outerFlag = builder.newDropFlag(true);
                                builder.pushStmtSetDropflagVal(sp, ose.outerFlag, false);
                            }
                        } else {
                            builder.pushStmtSetDropflagVal(sp, ose.outerFlag, isValid);
                        }

                        mergeState(sp, builder, ::MIR::LValue::newDeref(lv.clone()), *ose.innerState, newState);
                        return;
                    }
                    case VarState::TAG_Optional: {
                        const auto& nse = newState.as_Optional();
                        if (ose.outerFlag == ~0u) {
                            if (!builder.getDropFlagDefault(sp, nse)) {
                                // Default wasn't true, need to make a new flag that does have a default of true
                                auto newFlag = builder.newDropFlag(true);
                                builder.pushStmtSetDropflagOther(sp, newFlag, nse);
                                ose.outerFlag = newFlag;
                            } else {
                                ose.outerFlag = nse;
                            }
                        } else {
                            // In this arm, assign the outer state to this drop flag
                            builder.pushStmtSetDropflagOther(sp, ose.outerFlag, nse);
                        }
                        mergeState(sp, builder, ::MIR::LValue::newDeref(lv.clone()), *ose.innerState, newState);
                        return;
                    }
                    case VarState::TAG_MovedOut: {
                        const auto& nse = newState.as_MovedOut();

                        if (ose.outerFlag == ~0u) {
                            ose.outerFlag = nse.outerFlag;
                        } else {
                            builder.pushStmtSetDropflagOther(sp, ose.outerFlag, nse.outerFlag);
                        }
                        mergeState(sp, builder, ::MIR::LValue::newDeref(lv.clone()), *ose.innerState, *nse.innerState);
                        return;
                    }
                    case VarState::TAG_Partial:
                        BUG(sp, "MovedOut->Partial not valid");
                }
                break;
            }
            case VarState::TAG_Partial: {
                auto& ose = oldState.as_Partial();
                bool is_enum = false;
                builder.withValType(sp, lv, [&](const auto& ty) {
                    assert(!builder.isTypeOwnedBox(ty));
                    is_enum = ty->is_Path() && ty->as_Path().binding.is_Enum();
                });
                // Need to tag for conditional shallow drop? Or just do that at the end of the split?
                // - End of the split means that the only optional state is outer drop.
                switch (newState.tag()) {
                    case VarState::TAGDEAD:
                        throw "";
                    case VarState::TAG_Invalid:
                    case VarState::TAG_Valid:
                    case VarState::TAG_Optional:
                        if (is_enum) {
                            if (newState.is_Invalid()) {
                                mergeOuterValidity(sp, builder, ose.outerFlag, false);
                            } else if (newState.is_Valid()) {
                                mergeOuterValidity(sp, builder, ose.outerFlag, true);
                            } else {
                                mergeOuterValidity(sp, builder, ose.outerFlag, newState.as_Optional());
                            }
                            for (size_t i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, ::MIR::LValue::newDowncast(lv.clone(), static_cast<unsigned int>(i)), ose.innerStates[i], newState);
                            }
                        } else {
                            for (unsigned int i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, ::MIR::LValue::newField(lv.clone(), i), ose.innerStates[i], newState);
                            }
                        }
                        return;
                    case VarState::TAG_MovedOut:
                        BUG(sp, "Partial->MovedOut not valid");
                    case VarState::TAG_Partial: {
                        const auto& nse = newState.as_Partial();
                        ASSERT_BUG(sp, ose.innerStates.size() == nse.innerStates.size(), "Partial->Partial with mismatched sizes - " << oldState << " <= " << newState);
                        if (is_enum) {
                            if (nse.outerFlag == ~0u) {
                                mergeOuterValidity(sp, builder, ose.outerFlag, true);
                            } else {
                                mergeOuterValidity(sp, builder, ose.outerFlag, nse.outerFlag);
                            }
                            for (size_t i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, ::MIR::LValue::newDowncast(lv.clone(), static_cast<unsigned int>(i)), ose.innerStates[i], nse.innerStates[i]);
                            }
                        } else {
                            for (unsigned int i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, ::MIR::LValue::newField(lv.clone(), i), ose.innerStates[i], nse.innerStates[i]);
                            }
                        }
                    }
                        return;
                }
            } break;
        }
        BUG(sp, "Unhandled combination - " << oldState.tagStr() << " and " << newState.tagStr());
    }
}

void MirBuilder::terminateLoopEarly(const Span& sp, ScopeType::Data_Loop& sdLoop) {
    if (sdLoop.exitStateValid) {
        // Insert copies of parent state for newly changed values
        // and Merge all changed values
        auto mergeList = [sp, this](const auto& changed, auto& exitStates, ::std::function<::MIR::LValue(unsigned)> valCb, auto type) {
            for (const auto& ent : changed) {
                auto idx = ent.first;
                auto it = exitStates.find(idx);
                if (it == exitStates.end()) {
                    it = exitStates.insert(::std::make_pair(idx, ent.second.clone())).first;
                }
                auto& oldState = it->second;
                mergeState(sp, *this, valCb(idx), oldState, getSlotState(sp, idx, type));
            }
        };
        mergeList(sdLoop.changedSlots, sdLoop.exitState.states, ::MIR::LValue::newLocal, SlotType::Local);
        mergeList(sdLoop.changedArgs, sdLoop.exitState.arg_states, [](auto v) {
            return ::MIR::LValue::newArgument(v);
        }, SlotType::Argument);
    } else {
        auto initList = [sp, this](const auto& changed, auto& exitStates, auto type) {
            for (const auto& ent : changed) {
                DEBUG("Slot(" << ent.first << ") = " << ent.second);
                auto idx = ent.first;
                exitStates.insert(::std::make_pair(idx, getSlotState(sp, idx, type).clone()));
            }
        };
        // Obtain states of changed variables/temporaries
        initList(sdLoop.changedSlots, sdLoop.exitState.states, SlotType::Local);
        initList(sdLoop.changedArgs, sdLoop.exitState.arg_states, SlotType::Argument);
        sdLoop.exitStateValid = true;
    }
}

void MirBuilder::mergeSplitLists(const Span& sp, const ScopeHandle& handle, const ::std::map<unsigned int, VarState>& states, ::std::map<unsigned int, VarState>& endStates, MirBuilder::SlotType type) {
    // Insert copies of the parent state
    for (const auto& ent : states) {
        if (endStates.count(ent.first) == 0) {
            auto s = this->getSlotState(sp, ent.first, type, &handle).clone();
            DEBUG("Add from parent: " << (type == SlotType::Local ? ::MIR::LValue::newLocal(ent.first) : ::MIR::LValue::newArgument(ent.first)) << " = " << s);
            endStates.insert(::std::make_pair(ent.first, std::move(s)));
        }
    }
    // Merge state
    for (auto& ent : endStates) {
        auto idx = ent.first;
        auto& outState = ent.second;

        // Merge the states
        auto it = states.find(idx);
        const auto& srcState = (it != states.end() ? it->second : this->getSlotState(sp, idx, type, &handle));

        auto lv = (type == SlotType::Local ? ::MIR::LValue::newLocal(idx) : ::MIR::LValue::newArgument(idx));
        mergeState(sp, *this, mv$(lv), outState, srcState);
    }
}

void MirBuilder::endSplitArm(const Span& sp, const ScopeHandle& handle, bool reachable, bool early /*=false*/) {
    ASSERT_BUG(sp, handle.idx < scopes.size(), "Handle passed to end_split_arm is invalid");
    auto& sd = scopes.at(handle.idx);
    ASSERT_BUG(sp, sd.data.is_Split(), "Ending split arm on non-Split arm - " << sd.data.tagStr());
    auto& sdSplit = sd.data.as_Split();
    ASSERT_BUG(sp, !sdSplit.arms.empty(), "Split arm list is empty (impossible)");

    // If this is not at the top of the stack (if there are other splits in the way), then get state from them
    for (auto v : ::reverse(scopeStack)) {
        if (v == handle.idx) {
            break;
        }

        // If this stack entry is a Split, get the current values and add them to `sd_split`
        if (const auto* otherSplit = scopes.at(v).data.opt_Split()) {
            for (auto& s : otherSplit->arms.back().states) {
                DEBUG("In scope " << handle.idx << " _" << s.first << " = " << s.second << " (from scope " << v << ")");
                sdSplit.arms.back().states[s.first] = s.second.clone();
            }
            for (auto& s : otherSplit->arms.back().arg_states) {
                DEBUG("In scope " << handle.idx << " a" << s.first << " = " << s.second << " (from scope " << v << ")");
                sdSplit.arms.back().arg_states[s.first] = s.second.clone();
            }
        }
    }

    TRACE_FUNCTION_F("end split scope " << handle.idx << " arm " << (sdSplit.arms.size() - 1) << (reachable ? " reachable" : "") << (early ? " early" : ""));
    if (reachable) {
        ASSERT_BUG(sp, blockActive, "Block must be active when ending a reachable split arm");
    }

    auto& thisArmState = sdSplit.arms.back();
    thisArmState.alwaysEarlyTerminated = /*sd_split.arms.back().has_early_terminated &&*/ !reachable;

    if (sdSplit.endStateValid) {
        if (reachable) {
            DEBUG("Reachable w/ end state, merging");

            mergeSplitLists(sp, handle, thisArmState.states, sdSplit.endState.states, SlotType::Local);
            mergeSplitLists(sp, handle, thisArmState.arg_states, sdSplit.endState.arg_states, SlotType::Argument);
        } else {
            DEBUG("Unreachable, not merging");
        }
    } else {
        if (reachable) {
            DEBUG("Reachable w/ no end state, setting");
            // Clone this arm's state
            for (auto& ent : thisArmState.states) {
                DEBUG("State _" << ent.first << " = " << ent.second);
                sdSplit.endState.states.insert(::std::make_pair(ent.first, ent.second.clone()));
            }
            for (auto& ent : thisArmState.arg_states) {
                DEBUG("State a" << ent.first << " = " << ent.second);
                sdSplit.endState.arg_states.insert(::std::make_pair(ent.first, ent.second.clone()));
            }
            sdSplit.endStateValid = true;
        } else {
            DEBUG("Unreachable, not setting");
        }
    }

    if (reachable) {
        assert(blockActive);
    }
    if (!early) {
        SplitArm arm;
        DEBUG("New Arm");
        for (auto& ent : sdSplit.condState.states) {
            DEBUG("Condition State _" << ent.first << " = " << ent.second);
            arm.states.insert(::std::make_pair(ent.first, ent.second.clone()));
        }
        for (auto& ent : sdSplit.condState.arg_states) {
            DEBUG("Condition State a" << ent.first << " = " << ent.second);
            arm.arg_states.insert(::std::make_pair(ent.first, ent.second.clone()));
        }
        sdSplit.arms.push_back(mv$(arm));
    }
}

void MirBuilder::endSplitArmEarly(const Span& sp) {
    TRACE_FUNCTION_F("");
    size_t i = scopeStack.size();
    // Terminate every sequence of owning scopes
    while (i-- && scopes.at(scopeStack[i]).data.is_Owning()) {
        auto& scopeDef = scopes[scopeStack[i]];
        // Fully drop the scope
        DEBUG("Complete scope " << scopeStack[i]);
        dropScopeValues(scopeDef);
        completeScope(scopeDef);
    }

    if (i < scopeStack.size()) {
        if (scopes.at(scopeStack[i]).data.is_Split()) {
            DEBUG("Early terminate split scope " << scopeStack.back());
            auto& sd = scopes[scopeStack[i]];
            auto& sdSplit = sd.data.as_Split();
            sdSplit.arms.back().hasEarlyTerminated = true;

            // TODO: Create drop flags if required?
        }
        // TODO: What if this is a loop?
    }
}

void MirBuilder::endSplitCondition(const Span& sp, const ScopeHandle& handle) {
    ASSERT_BUG(sp, handle.idx < scopes.size(), "Handle passed to end_split_arm is invalid");
    auto& sd = scopes.at(handle.idx);
    ASSERT_BUG(sp, sd.data.is_Split(), "Ending split arm on non-Split arm - " << sd.data.tagStr());
    auto& sdSplit = sd.data.as_Split();
    ASSERT_BUG(sp, !sdSplit.arms.empty(), "Split arm list is empty (impossible)");

    const auto& thisArmState = sdSplit.arms.back();

    DEBUG("Split condition clause end (scope " << handle.idx << "): merging");

    mergeSplitLists(sp, handle, thisArmState.states, sdSplit.condState.states, SlotType::Local);
    mergeSplitLists(sp, handle, thisArmState.arg_states, sdSplit.condState.arg_states, SlotType::Argument);
}

void MirBuilder::unfreezeScope(const Span& sp, const ScopeHandle& handle) {
    ASSERT_BUG(sp, handle.idx < scopes.size(), "Handle passed to `unfreeze_scope` is invalid");
    auto& sd = scopes.at(handle.idx);
    ASSERT_BUG(sp, sd.data.is_Freeze(), "Handle passed to `unfreeze_scope` was not a freeze,  - " << sd.data.tagStr());
    auto& sdE = sd.data.as_Freeze();

    DEBUG("Unfreeze scope " << handle.idx);
    sdE.unfrozen = true;
}

void MirBuilder::completeScope(ScopeDef& sd) {
    struct H {
        static void applyEndState(const Span& sp, MirBuilder& builder, SplitEnd& endState) {
            for (auto& ent : endState.states) {
                auto& vs = builder.getSlotStateMut(sp, ent.first, SlotType::Local);
                if (vs != ent.second) {
                    DEBUG(::MIR::LValue::newLocal(ent.first) << " " << vs << " => " << ent.second);
                    vs = ::std::move(ent.second);
                }
            }
            for (auto& ent : endState.arg_states) {
                auto& vs = builder.getSlotStateMut(sp, ent.first, SlotType::Argument);
                if (vs != ent.second) {
                    DEBUG(::MIR::LValue::newArgument(ent.first) << " " << vs << " => " << ent.second);
                    vs = ::std::move(ent.second);
                }
            }
        }
    };

    sd.complete = true;

    TU_MATCH_HDRA( (sd.data), { )
    TU_ARMA(Owning, e) {
        }
        TU_ARMA(Freeze, e) {
        }
        TU_ARMA(Loop, e) {
            TRACE_FUNCTION_F("Loop");
            if (e.exitStateValid) {
                H::applyEndState(sd.span, *this, e.exitState);
            }

            // Insert sets of drop flags to the first block (at the start of that block)
            auto& stmts = output.blocks.at(e.entryBb).statements;
            for (auto idx : e.dropFlags) {
                DEBUG("Reset df$" << idx);
                stmts.insert(stmts.begin(), ::MIR::Statement::make_SetDropFlag({idx, output.dropFlags.at(idx), ~0u}));
            }
        }
        TU_ARMA(Split, e) {
            TRACE_FUNCTION_F("Split - " << (e.arms.size() - 1) << " arms");

            // TODO: if not set, then end the current state as unreachable?
            //ASSERT_BUG(sd.span, e.end_state_valid, "Completing split scope with no end state set?");
            if (e.endStateValid) {
                H::applyEndState(sd.span, *this, e.endState);
            }
        }
    }
}

void MirBuilder::withValType(const Span& sp, const ::MIR::LValue& val, ::std::function<void(const ::HIR::TypeData*)> cb, const ::MIR::LValue::Wrapper* stopWrapper /*=nullptr*/) const {
    ::HIR::TypeRef tmp;
    const ::HIR::TypeData* ty = nullptr;
    TU_MATCHA((val.root), (e), (Return, ty = retTy;), (Argument, ty = mArgs.at(e).second;), (Local, ty = output.locals.at(e);), (Static, TU_MATCHA((e.mData), (pe), (Generic, ASSERT_BUG(sp, pe.mParams.types.empty(), "Path params on static"); const auto& s = mResolve.crate.getStaticByPath(sp, pe.mPath); ty = s.mType;), (UfcsKnown, TODO(sp, "Static - UfcsKnown - " << e);), (UfcsUnknown, BUG(sp, "Encountered UfcsUnknown in Static - " << e);), (UfcsInherent, TODO(sp, "Static - UfcsInherent - " << e);))))
    assert(ty);
    for (const auto& w : val.wrappers) {
        if (&w == stopWrapper) {
            stopWrapper = nullptr; // Reset so the below bugcheck can work
            break;
        }
        const auto* currentTy = ty;
        ty = nullptr;
        //DEBUG(ty << " " << w);
        auto maybeMonomorph = [&](const ::HIR::GenericParams& paramsDef, const ::HIR::Path& p, const ::HIR::TypeData* t) -> const ::HIR::TypeData* {
            if (monomorphiseTypeNeeded(t)) {
                tmp = MonomorphStatePtr(mResolve.crate.types, nullptr, &p.mData.as_Generic().mParams, nullptr).monomorphType(sp, t);
                mResolve.expandAssociatedTypes(sp, tmp);
                return tmp;
            } else {
                return t;
            }
        };
        TU_MATCH_HDRA( (w), {)
        TU_ARMA(Field, fieldIndex) {
            TU_MATCH_HDRA( (*currentTy), {)
            default:
                BUG(sp, "Field access on unexpected type - " << currentTy);
                    TU_ARMA(Array, te) {
                        ty = te.inner;
                    }
                    TU_ARMA(Slice, te) {
                        ty = te.inner;
                    }
                    TU_ARMA(Path, te) {
                        if (const auto* tep = te.binding.opt_Struct()) {
                            const auto& str = **tep;
                            TU_MATCHA((str.mData), (se), (Unit, BUG(sp, "Field on unit-like struct - " << currentTy);), (Tuple, ASSERT_BUG(sp, fieldIndex < se.size(), "Field index out of range in tuple-struct " << currentTy << " - " << fieldIndex << " > " << se.size()); const auto& fld = se[fieldIndex]; ty = maybeMonomorph(str.mParams, te.path, fld.ent);), (Named, ASSERT_BUG(sp, fieldIndex < se.size(), "Field index out of range in struct " << currentTy << " - " << fieldIndex << " > " << se.size()); const auto& fld = se[fieldIndex]; ty = maybeMonomorph(str.mParams, te.path, fld.ty);))
                        } else if (/*const auto* tep =*/te.binding.opt_Union()) {
                            BUG(sp, "Field access on a union isn't valid, use Downcast instead - " << currentTy);
                        } else {
                            BUG(sp, "Field acess on unexpected type - " << currentTy);
                        }
                    }
                    TU_ARMA(Tuple, te) {
                        ASSERT_BUG(sp, fieldIndex < te.size(), "Field index out of range in tuple " << fieldIndex << " >= " << te.size());
                        ty = te[fieldIndex];
                    }
            }
            }
            TU_ARMA(Deref, _e) {
            TU_MATCH_HDRA( (*currentTy), { )
            default:
                BUG(sp, "Deref on unexpected type - " << currentTy);
                    TU_ARMA(Path, te) {
                        if (const auto* inner = this->isTypeOwnedBox(currentTy)) {
                            ty = inner;
                        } else {
                            BUG(sp, "Deref on unexpected type - " << currentTy);
                        }
                    }
                    TU_ARMA(Pointer, te) {
                        ty = te.inner;
                    }
                    TU_ARMA(Borrow, te) {
                        ty = te.inner;
                    }
            }
            }
            TU_ARMA(Index, _index_val) {
                TU_MATCH_DEF(::HIR::TypeData, (*currentTy), (te), (BUG(sp, "Index on unexpected type - " << currentTy);), (Slice, ty = te.inner;), (Array, ty = te.inner;))
            }
            TU_ARMA(Downcast, variantIndex) {
            TU_MATCH_HDRA( (*currentTy), { )
            default:
                BUG(sp, "Downcast on unexpected type - " << currentTy);
                    TU_ARMA(Path, te) {
                        if (const auto* pbe = te.binding.opt_Enum()) {
                            const auto& enm = **pbe;
                            ASSERT_BUG(sp, enm.mData.is_Data(), "Downcast on non-data enum");
                            const auto& variants = enm.mData.as_Data();
                            ASSERT_BUG(sp, variantIndex < variants.size(), "Variant index out of range");
                            const auto& variant = variants[variantIndex];

                            ty = maybeMonomorph(enm.mParams, te.path, variant.type);
                        } else if (const auto* pbe = te.binding.opt_Union()) {
                            const auto& unm = **pbe;
                            ASSERT_BUG(sp, variantIndex < unm.mVariants.size(), "Variant index out of range");
                            const auto& variant = unm.mVariants.at(variantIndex);

                            ty = maybeMonomorph(unm.mParams, te.path, variant.ty);
                        } else {
                            BUG(sp, "Downcast on non-Enum/Union - " << currentTy << " for " << val);
                        }
                    }
            }
            }
        }
        assert(ty);
    }
    ASSERT_BUG(sp, !stopWrapper, "A stop wrapper was passed, but not found");
    cb(ty);
}

bool MirBuilder::lvalueIsCopy(const Span& sp, const ::MIR::LValue& val) const {
    int rv = 0;
    withValType(sp, val, [&](const auto& ty) {
        DEBUG("[lvalue_is_copy] ty=" << ty);
        rv = (mResolve.typeIsCopy(sp, ty) ? 2 : 1);
    });
    ASSERT_BUG(sp, rv != 0, "Type for " << val << " can't be determined");
    return rv == 2;
}

const VarState& MirBuilder::getSlotState(const Span& sp, unsigned int idx, SlotType type, const ScopeHandle* aboveScope /*=nullptr*/) const {
    if (frozenExitStateActive && !aboveScope) {
        const auto& states = type == SlotType::Local ? frozenExitSlotStates : frozenExitArgStates;
        auto it = states.find(idx);
        if (it != states.end()) {
            return it->second;
        }
    }

    // 1. Find an applicable Split scope
    for (auto scopeIdx : ::reverse(scopeStack)) {
        // Is this supposed to only consider above a specified (likely split) scope?
        if (aboveScope) {
            // Once the scope is found, clear `above_scope` so subsequent iterations skip this check
            if (scopeIdx == aboveScope->idx) {
                aboveScope = nullptr;
            }
            continue;
        }
        const auto& scopeDef = scopes.at(scopeIdx);
        TU_MATCH_HDRA( (scopeDef.data), {)
        default:
            break;
            TU_ARMA(Owning, e) {
                if (type == SlotType::Local) {
                    auto it = ::std::find(e.slots.begin(), e.slots.end(), idx);
                    if (it != e.slots.end()) {
                        // State from an outer split belongs to the outer
                        // incarnation of this local.  Once its owning scope
                        // is reached, fall back to the local's base state.
                        goto outOfLoop;
                    }
                }
            }
            TU_ARMA(Split, e) {
                const auto& curArm = e.arms.back();
                const auto& list = (type == SlotType::Local ? curArm.states : curArm.arg_states);
                auto it = list.find(idx);
                if (it != list.end()) {
                    DEBUG("From scope " << scopeIdx);
                    return it->second;
                }
            }
        }
    }

outOfLoop:
    if (aboveScope) {
        BUG(sp, "Scope " << *aboveScope << " not found on stack");
    }
    switch (type) {
        case SlotType::Local:
            if (idx == ~0u) {
                return returnState;
            } else {
                ASSERT_BUG(sp, idx < slotStates.size(), "Slot " << idx << " out of range for state table");
                return slotStates.at(idx);
            }
            break;
        case SlotType::Argument:
            ASSERT_BUG(sp, idx < argStates.size(), "Argument " << idx << " out of range for state table");
            return argStates.at(idx);
    }
    throw "";
}

VarState& MirBuilder::getSlotStateMut(const Span& sp, unsigned int idx, SlotType type) {
    if (frozenExitStateActive) {
        auto& states = type == SlotType::Local ? frozenExitSlotStates : frozenExitArgStates;
        auto it = states.find(idx);
        if (it == states.end()) {
            it = states.insert(::std::make_pair(idx, getSlotState(sp, idx, type).clone())).first;
        }
        return it->second;
    }

    VarState* ret = nullptr;
    for (auto scopeIdx : ::reverse(scopeStack)) {
        auto& scopeDef = scopes.at(scopeIdx);
        TU_MATCH_HDRA( (scopeDef.data), {)
        TU_ARMA(Owning, e) {
                if (type == SlotType::Local) // `Local` counts both variables and temporaries
                {
                    auto it = ::std::find(e.slots.begin(), e.slots.end(), idx);
                    if (it != e.slots.end()) {
                        goto outOfLoop; // `goto` to avoid issues with the loops in `TU_ARMA`
                    }
                }
            }
            TU_ARMA(Split, e) {
                auto& curArm = e.arms.back();
                if (!ret) {
                    if (idx == ~0u) {
                    } else {
                        auto& states = (type == SlotType::Local ? curArm.states : curArm.arg_states);
                        auto it = states.find(idx);
                        if (it == states.end()) {
                            DEBUG("Split new (scope " << scopeIdx << ")");
                            it = states.insert(::std::make_pair(idx, getSlotState(sp, idx, type).clone())).first;
                        } else {
                            DEBUG("Split existing (scope " << scopeIdx << ")");
                        }
                        ret = &it->second;
                    }
                }
            }
            TU_ARMA(Loop, e) {
                if (idx == ~0u) {
                } else {
                    auto& states = (type == SlotType::Local ? e.changedSlots : e.changedArgs);
                    if (states.count(idx) == 0) {
                        auto state = e.exitStateValid ? getSlotState(sp, idx, type).clone() : VarState::make_Valid({});
                        states.insert(::std::make_pair(idx, mv$(state)));
                    }
                }
            }
            TU_ARMA(Freeze, e) {
                if (!e.unfrozen) {
                    // Prevent any mutation
                    ERROR(sp, E0000, "Attempting to move/initialise a value where not allowed (across scope " << scopeIdx << ")");
                }
            }
        }
    }
    // Label used because we need to break out of the loop and the `TU_ARMA`/`TU_MATCH_HDRA`
outOfLoop:
    if (!ret) {
        // Not set by a split/loop scope
        switch (type) {
            case SlotType::Local:
                ret = (idx == ~0u) ? &returnState : &slotStates.at(idx);
                break;
            case SlotType::Argument:
                ret = &argStates.at(idx);
                break;
        }
    }
    assert(ret);
    return *ret;
}

VarState* MirBuilder::getValStateMutP(const Span& sp, const ::MIR::LValue& lv, bool expectValid /*=false*/) {
    TRACE_FUNCTION_F(lv);
    VarState* vs = nullptr;
    TU_MATCHA(
        (lv.root),
        (e),
        (Return, BUG(sp, "Move of return value"); vs = &getSlotStateMut(sp, ~0u, SlotType::Local);),
        (Argument, vs = &getSlotStateMut(sp, e, SlotType::Argument);),
        (Local, vs = &getSlotStateMut(sp, e, SlotType::Local);),
        (
            Static, return nullptr;
            //BUG(sp, "Attempting to mutate state of a static");
        )
    )
    assert(vs);

    if (expectValid && vs->is_Valid()) {
        return nullptr;
    }

    for (const auto& w : lv.wrappers) {
        auto& ivs = *vs;
        vs = nullptr;
        TU_MATCH_HDRA( (w), { )
        TU_ARMA(Field, fieldIndex) {
                VarState tpl;
                TU_MATCHA(
                    (ivs),
                    (ivse),
                    (Invalid,
                     //BUG(sp, "Mutating inner state of an invalidated composite - " << lv);
                     tpl = VarState::make_Valid({});),
                    (MovedOut, BUG(sp, "Field on value with MovedOut state - " << lv);),
                    (Partial, ),
                    (Optional, tpl = ivs.clone();),
                    (Valid, tpl = VarState::make_Valid({});)
                )
                if (!ivs.is_Partial()) {
                    size_t nFlds = 0;
                    withValType(sp, lv, [&](const auto& ty) {
                        DEBUG("ty = " << ty);
                        if (const auto* e = ty->opt_Path()) {
                            ASSERT_BUG(sp, e->binding.is_Struct(), "");
                            const auto& str = *e->binding.as_Struct();
                            TU_MATCHA((str.mData), (se), (Unit, BUG(sp, "Field access of unit-like struct");), (Tuple, nFlds = se.size();), (Named, nFlds = se.size();))
                        } else if (const auto* e = ty->opt_Tuple()) {
                            nFlds = e->size();
                        } else if (const auto* e = ty->opt_Array()) {
                            ASSERT_BUG(sp, e->size.is_Known(), "Array size not known");
                            nFlds = e->size.as_Known();
                        } else {
                            TODO(sp, "Determine field count for " << ty);
                        }
                    }, &w);
                    ::std::vector<VarState> innerVs;
                    innerVs.reserve(nFlds);
                    for (size_t i = 0; i < nFlds; i++) {
                        innerVs.push_back(tpl.clone());
                    }
                    ivs = VarState::make_Partial({mv$(innerVs), ~0u});
                }
                vs = &ivs.as_Partial().innerStates.at(fieldIndex);
            }
            TU_ARMA(Deref, _e) {
                // A Box dereference is a move path: track its pointee separately so a
                // later shallow drop deallocates the Box without dropping moved data.
                bool isBox = false;
                if (this->mLangBox) {
                    withValType(sp, lv, [&](const auto& ty) {
                        DEBUG("ty = " << ty);
                        isBox = this->isTypeOwnedBox(ty);
                    }, &w);
                }

                if (isBox) {
                    if (!ivs.is_MovedOut()) {
                        ::std::vector<VarState> inner;
                        inner.push_back(VarState::make_Valid({}));
                        unsigned int dropFlag = (ivs.is_Optional() ? ivs.as_Optional() : ~0u);
                        ivs = VarState::make_MovedOut({box$(VarState::make_Valid({})), dropFlag});
                    }
                    vs = &*ivs.as_MovedOut().innerState;
                } else {
                    return nullptr;
                }
            }
            TU_ARMA(Index, e) {
                return nullptr;
            }
            TU_ARMA(Downcast, variantIndex) {
                if (!ivs.is_Partial()) {
                    ASSERT_BUG(sp, !ivs.is_MovedOut(), "Downcast of a MovedOut value");

                    size_t var_count = 0;
                    withValType(sp, lv, [&](const auto& ty) {
                        DEBUG("ty = " << ty);
                        ASSERT_BUG(sp, ty->is_Path(), "Downcast on non-Path type - " << ty);
                        const auto& pb = ty->as_Path().binding;
                        // TODO: What about unions?
                        // - Iirc, you can't move out of them so they will never have state mutated
                        if (pb.is_Enum()) {
                            const auto& enm = *pb.as_Enum();
                            var_count = enm.numVariants();
                        } else if (const auto* pbe = pb.opt_Union()) {
                            const auto& unm = **pbe;
                            var_count = unm.mVariants.size();
                        } else {
                            BUG(sp, "Downcast on non-Enum/Union - " << ty);
                        }
                    }, &w);

                    const auto outerFlag = ivs.is_Optional() ? ivs.as_Optional() : ~0u;
                    ::std::vector<VarState> inner;
                    for (size_t i = 0; i < var_count; i++) {
                        inner.push_back(VarState::make_Invalid(InvalidType::Uninit));
                    }
                    inner[variantIndex] = mv$(ivs);
                    ivs = VarState::make_Partial({mv$(inner), outerFlag});
                }

                vs = &ivs.as_Partial().innerStates.at(variantIndex);
            }
        }
        assert(vs);
    }
    return vs;
}

void MirBuilder::dropValueFromState(const Span& sp, VarState& vs, ::MIR::LValue lv) {
    TRACE_FUNCTION_F(lv << " " << vs);
    TU_MATCHA(
        (vs),
        (vse),
        (Invalid, ),
        (Valid, vs = VarState::make_Invalid(InvalidType::Moved); pushStmtDrop(sp, mv$(lv));),
        (
            MovedOut, bool isBox = false; withValType(
                sp,
                lv,
                [&](const auto& ty) {
        isBox = this->isTypeOwnedBox(ty);
    }
            );
            if (isBox) {
                dropValueFromState(sp, *vse.innerState, ::MIR::LValue::newDeref(lv.clone()));
                const auto outerFlag = vse.outerFlag;
                vs = VarState::make_Invalid(InvalidType::Moved);
                pushStmtDropShallow(sp, mv$(lv), outerFlag);
            } else { TODO(sp, ""); }
        ),
        (
            Partial, bool is_enum = false; bool is_union = false; withValType(
                sp,
                lv,
                [&](const auto& ty) {
        is_enum = ty->is_Path() && ty->as_Path().binding.is_Enum();
        is_union = ty->is_Path() && ty->as_Path().binding.is_Union();
    }
            );
            if (is_enum) {
                bool hasValidVariant = false;
                for (const auto& state : vse.innerStates) {
                    hasValidVariant |= !state.is_Invalid();
                }
                if (!hasValidVariant) {
                    return;
                }

                auto originalState = vs.clone();
                const auto outerFlag = vse.outerFlag;
                const auto nextBb = newBbUnlinked();
                ::std::vector<::MIR::BasicBlockId> arms;
                ::std::vector<::MIR::BasicBlockId> cleanupBlocks;
                arms.reserve(vse.innerStates.size());
                cleanupBlocks.reserve(vse.innerStates.size());
                for (const auto& state : vse.innerStates) {
                    const auto cleanupBb = state.is_Invalid() ? nextBb : newBbUnlinked();
                    arms.push_back(cleanupBb);
                    cleanupBlocks.push_back(cleanupBb);
                }
                endBlock(::MIR::Terminator::make_Switch({lv.clone(), mv$(arms), outerFlag, outerFlag == ~0u ? ~0u : nextBb}));

                const auto variantCount = originalState.as_Partial().innerStates.size();
                for (size_t i = 0; i < variantCount; i++) {
                    if (originalState.as_Partial().innerStates[i].is_Invalid()) {
                        continue;
                    }
                    setCurBlock(cleanupBlocks[i]);
                    vs = originalState.clone();
                    dropValueFromState(sp, vs.as_Partial().innerStates[i], ::MIR::LValue::newDowncast(lv.clone(), static_cast<unsigned int>(i)));
                    vs = VarState::make_Invalid(InvalidType::Moved);
                    endBlock(::MIR::Terminator::make_Goto(nextBb));
                }
                vs = VarState::make_Invalid(InvalidType::Moved);
                setCurBlock(nextBb);
            } else if (is_union) {
                // NOTE: Unions don't drop inner items.
                vs = VarState::make_Invalid(InvalidType::Moved);
            } else {
                for (size_t i = 0; i < vse.innerStates.size(); i++) {
                    dropValueFromState(sp, vse.innerStates[i], ::MIR::LValue::newField(lv.clone(), static_cast<unsigned int>(i)));
                }
                vs = VarState::make_Invalid(InvalidType::Moved);
            }
        ),
        (Optional, const auto flag = vse; vs = VarState::make_Invalid(InvalidType::Moved); pushStmtDrop(sp, mv$(lv), flag);)
    )
}

void MirBuilder::dropScopeValues(ScopeDef& sd) {
    TU_MATCHA(
        (sd.data),
        (e),
        (Owning,
         for (const auto& slot : ::reverse(e.dropSlots)) {
             const auto slotType = slot.isArgument ? SlotType::Argument : SlotType::Local;
             auto lvalue = slot.isArgument
                 ? ::MIR::LValue::newArgument(slot.index)
                 : ::MIR::LValue::newLocal(slot.index);
             if (buildingCleanup) {
                 if (unwindConsumedValue && lvalue == *unwindConsumedValue) {
                     continue;
                 }
                 auto state = getSlotState(sd.span, slot.index, slotType).clone();
                 DEBUG(lvalue << " - " << state);
                 dropValueFromState(sd.span, state, mv$(lvalue));
             } else {
                 auto& state = getSlotStateMut(sd.span, slot.index, slotType);
                 DEBUG(lvalue << " - " << state);
                 dropValueFromState(sd.span, state, mv$(lvalue));
             }
         }),
        (
            Split,
            // No values, controls parent
        ),
        (
            Loop,
            // No values
        ),
        (
            Freeze,
            // No values
        )
    )
}

void MirBuilder::movedLvalue(const Span& sp, const ::MIR::LValue& lv) {
    if (!lvalueIsCopy(sp, lv)) {
        auto* vsP = getValStateMutP(sp, lv);
        if (!vsP) {
            ERROR(sp, E0000, "Attempting to move out of invalid slot - " << lv);
        }
        auto& vs = *vsP;
        // TODO: If the current state is Optional, set the drop flag to 0
        auto newState = VarState::make_Invalid(InvalidType::Moved);
        DEBUG("State " << lv << " " << vs << " => " << newState);
        vs = std::move(newState);
    }
}

::MIR::LValue MirBuilder::getPtrToDst(const Span& sp, const ::MIR::LValue& lv) const {
    // Undo field accesses
    size_t count = 0;
    while (count < lv.wrappers.size() && lv.wrappers[lv.wrappers.size() - 1 - count].is_Field()) {
        count++;
    }

    // TODO: Enum variants?

    ASSERT_BUG(sp, count < lv.wrappers.size() && lv.wrappers[lv.wrappers.size() - 1 - count].is_Deref(), "Access of an unsized field without a dereference - " << lv);

    return lv.cloneUnwrapped(count + 1);
}

std::map<unsigned, MirBuilder::SavedActiveLocal> MirBuilder::getActiveLocals(const Span& sp, std::set<unsigned>& savedDropFlags) const {
    TRACE_FUNCTION;
    std::map<unsigned, MirBuilder::SavedActiveLocal> rv;
    for (size_t i = 0; i < slotStates.size(); i++) {
        const auto& s = getSlotState(sp, i, SlotType::Local);
        TU_MATCH_HDRA( (s), {)
        default:
            DEBUG("_" << i << " : " << s);
            s.getUsedDropFlags(&savedDropFlags);
            rv.insert(std::make_pair(static_cast<unsigned>(i), SavedActiveLocal(s.clone())));
            break;
            TU_ARMA(Invalid, e) {
            }
            TU_ARMA(MovedOut, e) {
            }
        }
    }
    return rv;
}

void MirBuilder::dropActveLocal(const Span& sp, ::MIR::LValue lv, const SavedActiveLocal& loc) {
    auto state = loc.state.clone();
    this->dropValueFromState(sp, state, mv$(lv));
}

void MirBuilder::emitUnwindCleanup(const Span& sp) {
    const auto wasBuildingCleanup = buildingCleanup;
    buildingCleanup = true;
    output.blocks.at(currentBlock).isCleanup = true;
    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
        dropScopeValues(scopes.at(*it));
    }
    buildingCleanup = wasBuildingCleanup;
}

::MIR::UnwindAction MirBuilder::makeUnwindAction(const Span& sp, const ::MIR::LValue* consumedValue) {
    if (buildingCleanup) {
        return ::MIR::UnwindAction::make_Terminate({});
    }

    const auto sourceBlock = pauseCurBlock();
    const auto cleanupBlock = newBbUnlinked();
    setCurBlock(cleanupBlock);
    const auto* oldConsumedValue = unwindConsumedValue;
    unwindConsumedValue = consumedValue;
    emitUnwindCleanup(sp);
    unwindConsumedValue = oldConsumedValue;
    endBlock(::MIR::Terminator::make_UnwindResume({}));
    setCurBlock(sourceBlock);
    return ::MIR::UnwindAction::make_Cleanup(cleanupBlock);
}

// --------------------------------------------------------------------

ScopeHandle::~ScopeHandle() {
    if (idx != ~0u) {
        try {
            ASSERT_BUG(Span(), builder.scopes.size() > idx, "Scope invalid");
            ASSERT_BUG(Span(), builder.scopes.at(idx).complete, "Scope " << idx << " not completed");
        } catch (...) {
            abort();
        }
    }
}

VarState VarState::clone() const {
    TU_MATCHA((*this), (e), (Invalid, return VarState(e);), (Valid, return VarState(e);), (Optional, return VarState(e);), (MovedOut, return VarState::make_MovedOut({box$(e.innerState->clone()), e.outerFlag});), (Partial, ::std::vector<VarState> n; n.reserve(e.innerStates.size()); for (const auto& a : e.innerStates) n.push_back(a.clone()); return VarState::make_Partial({mv$(n), e.outerFlag});))
    throw "";
}

bool VarState::operator==(const VarState& x) const {
    if (this->tag() != x.tag()) {
        return false;
    }
    TU_MATCHA((*this, x), (te, xe), (Invalid, return te == xe;), (Valid, return true;), (Optional, return te == xe;), (MovedOut, if (te.outerFlag != xe.outerFlag) return false; return *te.innerState == *xe.innerState;), (Partial, if (te.outerFlag != xe.outerFlag || te.innerStates.size() != xe.innerStates.size()) return false; for (unsigned int i = 0; i < te.innerStates.size(); i++) {
                  if (te.innerStates[i] != xe.innerStates[i]) {
                      return false;
                  }
              } return true;))
    throw "";
}

::std::ostream& operator<<(::std::ostream& os, const VarState& x) {
    TU_MATCHA(
        (x),
        (e),
        (Invalid,
         switch (e) {
             case InvalidType::Uninit:
                 os << "Uninit";
                 break;
             case InvalidType::Moved:
                 os << "Moved";
                 break;
             case InvalidType::Descoped:
                 os << "Descoped";
                 break;
         }),
        (Valid, os << "Valid";),
        (Optional, os << "Optional(df" << e << ")";),
        (MovedOut, os << "MovedOut("; if (e.outerFlag == ~0u) os << "-"; else os << "df" << e.outerFlag; os << " " << *e.innerState << ")";),
        (Partial, os << "Partial("; if (e.outerFlag == ~0u) os << "-"; else os << "df" << e.outerFlag; os << ", [" << e.innerStates << "])";)
    )
    return os;
}

bool VarState::getUsedDropFlags(std::set<unsigned>* out) const {
    bool rv = false;
    TU_MATCH_HDRA((*this), {)
    TU_ARMA(Optional, ve) {
            if (out) {
                out->insert(ve);
            }
            rv = true;
        }
        TU_ARMA(Invalid, ve) {
        }
        TU_ARMA(Valid, ve) {
        }
        TU_ARMA(Partial, ve) {
            if (ve.outerFlag != ~0u) {
                if (out) {
                    out->insert(ve.outerFlag);
                }
                rv = true;
            }
            for (const auto& vs : ve.innerStates) {
                rv |= vs.getUsedDropFlags(out);
            }
        }
        TU_ARMA(MovedOut, ve) {
            if (ve.outerFlag != ~0u) {
                if (out) {
                    out->insert(ve.outerFlag);
                }
                rv = true;
            }
            rv |= ve.innerState->getUsedDropFlags(out);
        }
    }
    return rv;
}

ScopeHandle::ScopeHandle(const MirBuilder& builder, unsigned int idx)
    : builder(builder)
    , idx(idx) {
}
ScopeHandle::ScopeHandle(ScopeHandle&& x)
    : builder(x.builder)
    , idx(x.idx) {
    x.idx = ~0;
}
PatternBinding::PatternBinding(fieldPathT field, const ::HIR::PatternBinding& binding)
    : field(std::move(field))
    , binding(&binding)
    , splitSlice(SIZE_MAX, SIZE_MAX) {
}
MirBuilder::ScopeDef::ScopeDef(const Span& span)
    : span(span) {
}
MirBuilder::ScopeDef::ScopeDef(const Span& span, ScopeType data)
    : span(span)
    , data(mv$(data)) {
}
/// Save the current state of aliases (see add_variable_alias)
MirBuilder::SavedAliases MirBuilder::saveAliases() const {
    SavedAliases rv;
    rv.setAliases.reserve(variableAliases.size());
    for (const auto& v : variableAliases) {
        rv.setAliases.push_back(v.second != MIR::LValue());
    }
    return rv;
}
void MirBuilder::restoreAliases(SavedAliases a) {
    assert(a.setAliases.size() == variableAliases.size());
    for (size_t i = 0; i < a.setAliases.size(); i++) {
        if (!a.setAliases[i]) {
            variableAliases.at(i).second = MIR::LValue();
        }
    }
}
// Variable aliases (used for match guards)
void MirBuilder::addVariableAlias(const Span& sp, unsigned idx, HIR::PatternBinding::Type ty, MIR::LValue lv) {
    DEBUG("#" << idx << " = " << int(ty) << " " << lv);
    ASSERT_BUG(sp, idx < variableAliases.size(), "Variable alias #" << idx << " out of bounds");
    ASSERT_BUG(sp, variableAliases[idx].second == MIR::LValue(), "Variable alias #" << idx << " already exists: " << variableAliases[idx].second << " setting " << lv);
    variableAliases[idx] = std::make_pair(ty, mv$(lv));
}
const MirBuilder::varAliasT* MirBuilder::getVariableAlias(const Span& sp, unsigned idx) const {
    ASSERT_BUG(sp, idx < variableAliases.size(), "Variable alias #" << idx << " out of bounds");
    if (variableAliases[idx].second == MIR::LValue()) {
        return nullptr;
    } else {
        return &variableAliases[idx];
    }
}
// - Values
::MIR::LValue MirBuilder::getVariable(const Span& sp, unsigned idx) const {
    auto it = varArgMappings.find(idx);
    if (it != varArgMappings.end()) {
        return ::MIR::LValue::newArgument(it->second);
    }
    return ::MIR::LValue::newLocal(idx);
}
::MIR::LValue MirBuilder::getRvalInIfCond(const Span& sp, ::MIR::RValue val) {
    pushStmtAssign(sp, ifCondLval.clone(), mv$(val));
    return ifCondLval.clone();
}
MirBuilder::SavedActiveLocal::SavedActiveLocal(VarState vs)
    : state(mv$(vs)) {
}

::std::ostream& operator<<(::std::ostream& os, const ScopeHandle& x) {
    os << x.idx;
    return os;
}
::std::ostream& operator<<(::std::ostream& os, const fieldPathT& x) {
    for (auto idx : x.data) {
        os << ".";
        if (idx == FIELD_DEREF) {
            os << "*";
        } else if (idx > FIELD_INDEX_MAX) {
            idx -= FIELD_INDEX_MAX;
            idx = FIELD_INDEX_MAX - idx;
            os << "-" << static_cast<unsigned int>(idx);
        } else {
            os << static_cast<unsigned int>(idx);
        }
    }
    return os;
}
::std::ostream& operator<<(::std::ostream& os, const PatternBinding& x) {
    os << *x.binding << x.field;
    if (x.isSplitSlice()) {
        os << "[" << x.splitSlice.first << "..-" << x.splitSlice.second << "]";
    }
    return os;
}
