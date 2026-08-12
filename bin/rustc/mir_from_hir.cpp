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
            ::MIR::LValue res_value;
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
            std::set<unsigned> saved_drop_flags;

            /// Path to the enum used for the state index field (used to generate enum variant construction)
            ::HIR::SimplePath state_idx_enm_path;

            /// Is this coroutine a future? (as opposed to a generator)
            bool is_future = false;
        } generatorState;

    public:
        ExprVisitorConv(MirBuilder& builder, const ::std::vector<::HIR::TypeRef>& var_types, const ::HIR::ExprNodeGeneratorWrapper* is_generator)
            : builder(builder)
            , variableTypes(var_types)
            , isGenerator(is_generator != nullptr)
        {
            if (isGenerator) {
                generatorState.is_future = is_generator->isFuture;
                generatorState.state_idx_enm_path = is_generator->stateIdxEnum;
                generatorState.bbOpen = builder.pause_cur_block();
                generatorState.states.push_back(GeneratorState::State(builder.new_bb_unlinked()));
                builder.set_cur_block(generatorState.states.back().entrypoint);
            }
        }

        SaveAndEditVal<const ScopeHandle*> disableBorrowExtension() override {
            return save_and_edit(borrowRaiseTarget, nullptr);
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
            return generatorState.saved_drop_flags;
        }

        std::set<unsigned> generatorFinalise(const Span& sp, ::HIR::Enum& state_enm) {
            std::set<unsigned> used_vars;
            std::vector<MIR::BasicBlockId> armTargets;
            armTargets.reserve(generatorState.states.size() + 1);
            ::std::vector<HIR::Enum::ValueVariant> enumVariants;
            enumVariants.reserve(generatorState.states.size() + 1);
            for (const auto& s : generatorState.states) {
                armTargets.push_back(builder.new_bb_unlinked());

                builder.set_cur_block(armTargets.back());
                builder.push_stmt_assign(sp, generatorStateLv(), ::MIR::RValue::make_EnumVariant({generatorState.state_idx_enm_path, static_cast<unsigned>(generatorState.states.size()), {}}));
                builder.endBlock(::MIR::Terminator::make_Goto(s.entrypoint));

                enumVariants.push_back(HIR::Enum::ValueVariant{RcString(), ::HIR::ExprPtr(), U128(armTargets.size() - 1)});
                for (const auto& e : s.saved) {
                    used_vars.insert(e.first);
                }
            }
            // Final arm is the end/panic state - it's a bug to reach this
            armTargets.push_back(builder.new_bb_unlinked());
            builder.set_cur_block(armTargets.back());
            builder.endBlock(::MIR::Terminator::make_Unreachable({}));

            enumVariants.push_back(HIR::Enum::ValueVariant{RcString::new_interned("END"), ::HIR::ExprPtr(), U128(armTargets.size() - 1)});
            state_enm.mData = ::HIR::Enum::Class::make_Value({mv$(enumVariants)});

            builder.set_cur_block(generatorState.bbOpen);

            // switch _n { ... }
            builder.endBlock(::MIR::Terminator::make_Switch({generatorStateLv(), mv$(armTargets)}));

            return used_vars;
        }

        void generatorMakeDrop(const Span& sp, MirBuilder& out_builder, size_t n_captures, const ::std::map<unsigned, std::vector<MIR::LValue::Wrapper>>& mappings, unsigned dropStateFieldIdx, const std::map<unsigned, unsigned>& drop_flag_mapping) const {
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
                    auto i = out_builder.new_drop_flag(false);
                    assert(i == flagMapping.second); // Should hold, as the map was created in-order
                    out_builder.push_stmt(
                        sp,
                        ::MIR::Statement::make_LoadDropFlag({
                            flagMapping.first,
                            slot.clone(),
                            flagMapping.second,
                        })
                    );
                }
            }

            auto entryBlock = out_builder.pause_cur_block();
            // if state is 0, then drop captures (this is the pre-run state)
            arms.push_back(out_builder.new_bb_unlinked());
            out_builder.set_cur_block(arms.back());
            size_t argCount = 2;
            for (size_t i = 0; i < n_captures; i++) {
                // TODO: State tracking on captures, what if a by-value capture is moved?
                if (mappings.count(argCount + i) == 0) {
                    out_builder.push_stmt_drop(sp, ::MIR::LValue::newField(self.clone(), 1 + i));
                }
            }
            out_builder.endBlock(::MIR::Terminator::make_Return({}));

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
                arms.push_back(out_builder.new_bb_unlinked());
                out_builder.set_cur_block(arms.back());
                for (const auto& v : generatorState.states[i].saved) {
                    if (v.first == 0) {
                        continue;
                    }
                    // Note: Conditional drop handled by drop flags above
                    // HACK: The caller re-maps drop flags
                    out_builder.dropActveLocal(sp, getLv(v.first), v.second);
                }
                out_builder.endBlock(::MIR::Terminator::make_Return({}));
            }
            // Generate the dispatch switch
            out_builder.set_cur_block(entryBlock);
            out_builder.push_stmt_assign(sp, ::MIR::LValue::newReturn(), ::MIR::RValue::make_Tuple({}));
            auto stmt_idx_lv = mv$(self);
            stmt_idx_lv = ::MIR::LValue::newField(mv$(stmt_idx_lv), 0);    // .state
            stmt_idx_lv = ::MIR::LValue::newDowncast(mv$(stmt_idx_lv), 1); // .value (From MaybeUninit)
            stmt_idx_lv = ::MIR::LValue::newField(mv$(stmt_idx_lv), 0);    // .value (From ManuallyDrop)
            stmt_idx_lv = ::MIR::LValue::newField(mv$(stmt_idx_lv), 0);    // .idx
            out_builder.endBlock(::MIR::Terminator::make_Switch({mv$(stmt_idx_lv), mv$(arms)}));
        }

        void visit_pattern_slots(const ::HIR::Pattern& pat, PatternDropOrder order, const std::function<void(unsigned)>& visit_slot) {
            for (const auto slot : ::HIR::pattern_binding_slots(pat, order)) {
                visit_slot(slot);
            }
        }

        void schedule_pattern_drops(const Span& sp, const ::HIR::Pattern& pat, PatternDropOrder order) override {
            (void)sp;
            visit_pattern_slots(pat, order, [&](unsigned slot) { builder.schedule_variable_drop(slot); });
        }

        void register_pattern_variables(const Span& sp, const ::HIR::Pattern& pat, PatternDropOrder order) override {
            (void)sp;
            visit_pattern_slots(pat, order, [&](unsigned slot) { builder.register_variable_state(slot); });
        }

        void schedule_registered_pattern_drops(const Span& sp, const ::HIR::Pattern& pat, PatternDropOrder order) override {
            (void)sp;
            visit_pattern_slots(pat, order, [&](unsigned slot) { builder.schedule_registered_variable_drop(slot); });
        }

        MIR::LValue getValueForBindingPath(const Span& sp, const ::HIR::TypeData* outer_ty, const ::MIR::LValue& outer_lval, const PatternBinding& b) {
            HIR::TypeRef ty;
            MIR::LValue lval;
            MIRLowerHIRGetTypeValueForPath(sp, builder, outer_ty, outer_lval, b.field, ty, lval);

            if (b.is_split_slice()) {
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

                unsigned sub_val_i = static_cast<unsigned>(b.split_slice.first + b.split_slice.second);
                auto& types = builder.resolve().crate.types;
                if (const auto* tep = ty->opt_Array()) {
                    auto inner_type = tep->inner;
                    auto len = tep->size.as_Known() - sub_val_i;
                    auto ret_ty = types.array(inner_type, len);

                    if (b.binding->mType == ::HIR::PatternBinding::Type::Move) {
                        // Create a new array value
                        std::vector<MIR::Param> arrayVals;
                        for (size_t i = b.split_slice.first; i < tep->size.as_Known() - b.split_slice.second; i++) {
                            arrayVals.push_back(::MIR::LValue::newField(lval.clone(), static_cast<unsigned>(i)));
                        }
                        lval = builder.lvalue_or_temp(sp, mv$(ret_ty), ::MIR::RValue::make_Array({std::move(arrayVals)}));
                    } else {
                        // Create a pointer to this array, by casting a raw pointer to its first element
                        ::HIR::BorrowType bt = H::getBorrowType(sp, *b.binding);
                        ::MIR::LValue ptr_val = builder.lvalue_or_temp(sp, types.pointer(bt, inner_type), ::MIR::RValue::make_Borrow({bt, true, ::MIR::LValue::newField(lval.clone(), static_cast<unsigned int>(b.split_slice.first))}));

                        // 3. Create a slice pointer
                        auto ptr_ty = types.pointer(bt, ret_ty);
                        lval = builder.lvalue_or_temp(sp, ptr_ty, ::MIR::RValue::make_Cast({mv$(ptr_val), ptr_ty}));
                        // 4. And dereference it
                        lval = ::MIR::LValue::newDeref(std::move(lval));
                    }
                } else if (const auto* tep = ty->opt_Slice()) {
                    auto inner_type = tep->inner;

                    // 1. Obtain remaining length
                    auto usize_ty = types.primitive(::HIR::CoreType::Usize);
                    auto src_len_lval = builder.lvalue_or_temp(sp, usize_ty, ::MIR::RValue::make_DstMeta({builder.getPtrToDst(sp, lval)}));
                    auto sub_val = ::MIR::Param(::MIR::Constant::make_Uint({U128(sub_val_i), ::HIR::CoreType::Usize}));
                    ::MIR::LValue len_val = builder.lvalue_or_temp(sp, usize_ty, ::MIR::RValue::make_BinOp({mv$(src_len_lval), ::MIR::eBinOp::SUB, mv$(sub_val)}));

                    // 2. Obtain pointer to the first element
                    // TODO: This currently emits a borrow to that element, but we need a raw pointer (to avoid being technically out-of-bounds)
                    // - Should add a MIR op for `BorrowRaw`
                    ::HIR::BorrowType bt = H::getBorrowType(sp, *b.binding);
                    ::MIR::LValue ptr_val = builder.lvalue_or_temp(sp, types.pointer(bt, inner_type), ::MIR::RValue::make_Borrow({bt, true, ::MIR::LValue::newField(lval.clone(), static_cast<unsigned int>(b.split_slice.first))}));

                    // 3. Create a slice pointer
                    lval = builder.lvalue_or_temp(sp, types.borrow(bt, ty), ::MIR::RValue::make_MakeDst({mv$(ptr_val), mv$(len_val)}));
                    // 4. And dereference it
                    lval = ::MIR::LValue::newDeref(std::move(lval));
                } else {
                    TODO(sp, "SplitSlice binding: " << b.split_slice << " - " << ty);
                }
            }

            return lval;
        }

        void destructureFromList(const Span& sp, const ::HIR::TypeData* outer_ty, ::MIR::LValue outer_lval, const ::std::vector<PatternBinding>& bindings, bool update_states /*=true*/) override {
            TRACE_FUNCTION_F(outer_lval << ": " << outer_ty << " [" << bindings << "]");
            // Reverse order to avoid potential use-after-move for `foo @ Bar(baz, ..)`
            for (size_t i = bindings.size(); i--;) {
                const auto& b = bindings[i];
                auto lval = getValueForBindingPath(sp, outer_ty, outer_lval, b);

                MIR::RValue rv;
                switch (b.binding->mType) {
                    case ::HIR::PatternBinding::Type::Move:
                        rv = mv$(lval);
                        break;
                    case ::HIR::PatternBinding::Type::Ref:
                        if (borrowRaiseTarget) {
                            DEBUG("- Raising destructure borrow of " << lval << " to scope " << *borrowRaiseTarget);
                            builder.raise_temporaries(sp, lval, *borrowRaiseTarget);
                        }

                        rv = ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, mv$(lval)});
                        break;
                    case ::HIR::PatternBinding::Type::MutRef:
                        if (borrowRaiseTarget) {
                            DEBUG("- Raising destructure borrow of " << lval << " to scope " << *borrowRaiseTarget);
                            builder.raise_temporaries(sp, lval, *borrowRaiseTarget);
                        }
                        rv = ::MIR::RValue::make_Borrow({::HIR::BorrowType::Unique, false, mv$(lval)});
                        break;
                }
                // NOTE: Don't drop the destination, as `match` does some tricky things with calling destructure multiple times (to handle or-patterns)
                builder.push_stmt_assign(sp, builder.getVariable(sp, b.binding->slot), mv$(rv), update_states);
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
        void visit_node_ptr(::HIR::ExprNodeP& node_p) override {
            DEBUG(node_p.get());
            ::HIR::ExprVisitor::visit_node_ptr(node_p);
        }

        void visit(::HIR::ExprNodeBlock& node) override {
            TRACE_FUNCTION_F("_Block");
            // NOTE: This doesn't create a BB, as BBs are not needed for scoping
            bool diverged = false;

            auto res_val = (node.valueNode ? builder.new_temporary(node.resType) : ::MIR::LValue());
            // Tail-expression temporaries outlive the block's locals. This is
            // a distinct scope from the one used for extended let initializers.
            auto tail_tmp_scope = builder.new_scope_temp(node.span());
            auto scope = builder.new_scope_var(node.span());
            auto _block_var_scope = save_and_edit(blockVarScope, &scope);
            auto tmp_scope = builder.new_scope_temp(node.span());
            auto _block_tmp_scope = save_and_edit(blockTmpScope, &tmp_scope);

            for (unsigned int i = 0; i < node.nodes.size(); i++) {
                auto _ = this->disableBorrowExtension();
                auto& subnode = node.nodes[i];
                const Span& sp = subnode->span();

                auto stmt_scope = builder.new_scope_temp(sp);
                const auto* let_node = cast<::HIR::ExprNodeLet>(subnode.get());
                auto _super_let_scope = save_and_edit(superLetScope, let_node && let_node->isSuper ? superLetScope : &stmt_scope);
                // NOTE: Only set the statement scope if processing a block
                auto _stmt_scope_push = save_and_edit(stmtScope, cast<::HIR::ExprNodeBlock>(subnode.get()) ? &stmt_scope : nullptr);
                this->visit_node_ptr(subnode);

                if (builder.block_active() || builder.hasResult()) {
                    auto result = builder.getResult(sp);
                    if (!builder.resolve().type_is_copy(sp, subnode->resType)) {
                        auto discarded = builder.new_temporary(subnode->resType);
                        builder.push_stmt_assign(sp, std::move(discarded), std::move(result));
                    }
                    builder.terminate_scope(sp, mv$(stmt_scope));
                    diverged |= subnode->resType->is_Diverge();
                } else {
                    builder.terminate_scope(sp, mv$(stmt_scope), false);

                    builder.set_cur_block(builder.new_bb_unlinked());
                    diverged = true;
                }
            }

            // For the last node, specially handle.
            // TODO: Any temporaries defined within this node must be elevated into the parent scope
            if (node.valueNode) {
                auto& subnode = node.valueNode;
                const Span& sp = subnode->span();

                auto stmt_scope = builder.new_scope_temp(sp);
                this->visit_node_ptr(subnode);
                if (builder.hasResult() || builder.block_active()) {
                    ASSERT_BUG(sp, builder.block_active(), "Result yielded, but no active block");
                    ASSERT_BUG(sp, builder.hasResult(), "Active block but no result yeilded");
                    // PROBLEM: This can drop the result before we want to use it.

                    builder.push_stmt_assign(sp, res_val.clone(), builder.getResult(sp));

                    // If this block is part of a statement, raise all temporaries from this final scope to the enclosing scope
                    if (stmtScope) {
                        builder.raise_all(sp, mv$(stmt_scope), *stmtScope);
                    } else {
                        builder.raise_all(sp, mv$(stmt_scope), tail_tmp_scope);
                    }
                    builder.set_result(node.span(), mv$(res_val));
                } else {
                    builder.terminate_scope(sp, mv$(stmt_scope), false);
                    // Block diverged in final node.
                }
                builder.terminate_scope(node.span(), mv$(tmp_scope), builder.block_active());
                builder.terminate_scope(node.span(), mv$(scope), builder.block_active());
                builder.terminate_scope(node.span(), mv$(tail_tmp_scope), builder.block_active());
            } else {
                if (diverged) {
                    builder.terminate_scope(node.span(), mv$(tmp_scope), false);
                    builder.terminate_scope(node.span(), mv$(scope), false);
                    builder.terminate_scope(node.span(), mv$(tail_tmp_scope), false);
                    builder.endBlock(::MIR::Terminator::make_Unreachable({}));
                    // Don't set a result if there's no block.
                } else {
                    builder.terminate_scope(node.span(), mv$(tmp_scope));
                    builder.terminate_scope(node.span(), mv$(scope));
                    builder.terminate_scope(node.span(), mv$(tail_tmp_scope));
                    builder.set_result(node.span(), ::MIR::RValue::make_Tuple({}));
                }
            }
        }

        void visit(::HIR::ExprNodeConstBlock& node) override {
            if (cast<HIR::ExprNodePathValue>(node.inner.get())) {
                this->visit_node_ptr(node.inner);
            } else {
                BUG(node.span(), "Const block shouldn't have reached MIR generation");
            }
        }

        void visit(::HIR::ExprNodeAsm& node) override {
            TRACE_FUNCTION_F("_Asm");

            ::std::vector<::std::pair<::std::string, ::MIR::LValue>> inputs;
            // Inputs just need to be in lvalues
            for (auto& v : node.inputs) {
                this->visit_node_ptr(v.value);
                auto lv = builder.getResultInLvalue(v.value->span(), v.value->resType);
                inputs.push_back(::std::make_pair(v.spec, mv$(lv)));
            }

            ::std::vector<::std::pair<::std::string, ::MIR::LValue>> outputs;
            // Outputs can also (sometimes) be rvalues (only for `*m`?)
            for (auto& v : node.outputs) {
                this->visit_node_ptr(v.value);
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

            builder.push_stmt_asm(node.span(), {node.templateText, mv$(outputs), mv$(inputs), node.clobbers, node.flags});
            builder.set_result(node.span(), ::MIR::RValue::make_Tuple({}));
        }

        void visit(::HIR::ExprNodeAsm2& node) override {
            TRACE_FUNCTION_F("_Asm2");

            // TODO: How to represent inout in the MIR?
            // - Potentially a register specifier that links to one of the inputs
            // - OR: Just keep the parameter list as before - but now simplified to just one `Reg`
            ::MIR::Statement::Data_Asm2 ent;
            ent.options = node.options;
            ent.lines = node.lines;

            auto moved_param = [&](const ::MIR::Param& p) {
                if (const auto* e = p.opt_LValue()) {
                    builder.moved_lvalue(node.span(), *e);
                }
            };

            for (auto& v : node.mParams) {
                TU_MATCH_HDRA( (v), { )
                TU_ARMA(Const, e) {
                        // This constant needs to have been evaluated fully (so a `MIR::Constant` can be created)
                        this->visit_node_ptr(e);
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
                        this->visit_node_ptr(e.val);
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
                            moved_param(*input);
                        }
                        ent.params.push_back(MIR::AsmParam::make_Reg({e.dir, std::move(e.spec), std::move(input), std::move(output)}));
                    }
                    TU_ARMA(Reg, e) {
                        std::unique_ptr<MIR::Param> input;
                        std::unique_ptr<MIR::LValue> output;
                        switch (e.dir) {
                            case AsmCommon::Direction::In:
                                ASSERT_BUG(node.span(), e.val_in, "`in` register with no input");
                                this->visit_node_ptr(e.val_in);
                                input = box$(builder.getResultInParam(e.val_in->span(), e.val_in->resType));
                                assert(!e.val_out);
                                break;
                            case AsmCommon::Direction::Out:
                            case AsmCommon::Direction::LateOut:
                                ASSERT_BUG(node.span(), !e.val_in, "`[late]out` register with input value");
                                if (e.val_out) {
                                    this->visit_node_ptr(e.val_out);
                                    output = box$(builder.getResultUnwrapLvalue(e.val_out->span()));
                                }
                                break;
                            case AsmCommon::Direction::InOut:
                            case AsmCommon::Direction::InLateOut:
                                ASSERT_BUG(node.span(), e.val_in, "`in[late]out` register with no input");
                                this->visit_node_ptr(e.val_in);
                                input = box$(builder.getResultInParam(e.val_in->span(), e.val_in->resType));
                                if (e.val_out) {
                                    this->visit_node_ptr(e.val_out);
                                    output = box$(builder.getResultUnwrapLvalue(e.val_out->span()));
                                }
                                break;
                        }
                        if (input) {
                            moved_param(*input);
                        }
                        ent.params.push_back(MIR::AsmParam::make_Reg({e.dir, std::move(e.spec), std::move(input), std::move(output)}));
                    }
                }
            }
            builder.push_stmt(node.span(), mv$(ent));
            if (!node.options.noreturn) {
                builder.set_result(node.span(), ::MIR::RValue::make_Tuple({}));
            } else {
                builder.endBlock(::MIR::Terminator::make_Unreachable({}));
            }
        }

        // Common code used by both `ExprNodeReturn` and the final return of a GeneratorWrapper
        void coroutineReturn(const Span& sp, const ::HIR::TypeData* value_ty) {
            static RcString rcstringComplete = RcString::new_interned("Complete");
            static RcString rcstringReady = RcString::new_interned("Ready"); // TODO: This is a lang item
            const auto& variant_name = generatorState.is_future ? rcstringReady : rcstringComplete;
            // TODO: Handle difference between generators and futures (different return/yield types)
            ::HIR::GenericPath enmPath;
            size_t variant_index = SIZE_MAX;
            builder.with_val_type(sp, ::MIR::LValue::newReturn(), [&](const ::HIR::TypeData* ty) {
                const auto& te = ty->as_Path();
                enmPath = te.path.mData.as_Generic().clone();
                variant_index = te.binding.as_Enum()->findVariant(variant_name);
            });
            ASSERT_BUG(sp, enmPath.mPath != HIR::SimplePath(), "Failed to get path from return type?");
            ASSERT_BUG(sp, variant_index != SIZE_MAX, "Unable to find variant " << variant_name << " in " << enmPath << " for coroutine return");

            ::std::vector<::MIR::Param> values;
            values.push_back(builder.getResultInParam(sp, value_ty));
            auto res = ::MIR::RValue::make_EnumVariant({std::move(enmPath), static_cast<unsigned>(variant_index), std::move(values)});
            builder.push_stmt_assign(sp, ::MIR::LValue::newReturn(), std::move(res));
        }

        void visit(::HIR::ExprNodeReturn& node) override {
            TRACE_FUNCTION_F("_Return");
            this->visit_node_ptr(node.mValue);

            if (!builder.block_active()) {
                return;
            }

            if (isGenerator) {
                coroutineReturn(node.span(), node.mValue->resType);
            } else {
                builder.push_stmt_assign(node.span(), ::MIR::LValue::newReturn(), builder.getResult(node.span()));
            }
            builder.terminate_scope_early(node.span(), builder.fcn_scope());
            builder.endBlock(::MIR::Terminator::make_Return({}));
        }

        void visit(::HIR::ExprNodeYield& node) override {
            TRACE_FUNCTION_F("_Yield");
            if (isGenerator) {
                ASSERT_BUG(node.span(), !generatorState.is_future, "");

                ::HIR::GenericPath enmPath;
                builder.with_val_type(node.span(), ::MIR::LValue::newReturn(), [&](const ::HIR::TypeData* ty) {
                    const auto& te = ty->as_Path();
                    enmPath = te.path.mData.as_Generic().clone();
                    ASSERT_BUG(node.span(), te.binding.as_Enum()->findVariant("Yielded") == 0, "");
                });

                this->visit_node_ptr(node.mValue);
                // Emit return, wrapped in GeneratorState::Yielded
                ::std::vector<::MIR::Param> values;
                values.push_back(builder.getResultInParam(node.span(), node.mValue->resType));
                auto res = ::MIR::RValue::make_EnumVariant(
                    {mv$(enmPath),
                     0, // Yielded is the first variant
                     mv$(values)}
                );
                builder.push_stmt_assign(node.span(), ::MIR::LValue::newReturn(), mv$(res));
                builder.push_stmt_assign(node.span(), generatorStateLv(), ::MIR::RValue::make_EnumVariant({generatorState.state_idx_enm_path.clone(), static_cast<unsigned>(generatorState.states.size()), {}}));
                // NOTE: No scope terminate
                builder.endBlock(::MIR::Terminator::make_Return({}));

                generatorState.states.back().saved = builder.getActiveLocals(node.span(), generatorState.saved_drop_flags);
                generatorState.states.push_back(builder.new_bb_unlinked());
                builder.set_cur_block(generatorState.states.back().entrypoint);

                builder.set_result(node.span(), ::MIR::RValue::make_Tuple({}));
            } else {
                BUG(node.span(), "Unexpected ExprNode_Yield (should have been re-written)");
            }
        }

        void visit(::HIR::ExprNodeAWait& node) override {
            const Span& sp = node.span();
            TRACE_FUNCTION_F("_AWait");
            ASSERT_BUG(node.span(), isGenerator && generatorState.is_future, "`.await` not in an async block/function");
            const auto& ty_inner = node.mValue->resType;

            this->visit_node_ptr(node.mValue);
            auto lv_res = builder.getResultInLvalue(sp, ty_inner);

            auto state_value = static_cast<unsigned>(generatorState.states.size());
            generatorState.states.back().saved = builder.getActiveLocals(node.span(), generatorState.saved_drop_flags);
            generatorState.states.push_back(builder.new_bb_unlinked());
            builder.endBlock(generatorState.states.back().entrypoint);
            builder.set_cur_block(generatorState.states.back().entrypoint);

            // Create `Pin<&mut >` as the reciever, using `Pin::new_unchecked`
            const auto& langPin = builder.resolve().crate.getLangItemPath(sp, "pin");
            auto& types = builder.resolve().crate.types;
            auto type_mut = types.borrow(::HIR::BorrowType::Unique, ty_inner);
            auto pin_path = ::HIR::GenericPath(langPin, ::HIR::PathParams(type_mut));
            auto type_pin = types.path(std::move(pin_path), &builder.resolve().crate.getStructByPath(sp, langPin));

            auto lv_mut = builder.lvalue_or_temp(sp, type_mut, ::MIR::RValue::make_Borrow({::HIR::BorrowType::Unique, false, std::move(lv_res)}));
            auto lv_pin = builder.new_temporary(type_pin);
            {
                auto bbRet = builder.new_bb_unlinked();
                auto bbPanic = builder.new_bb_unlinked();
                builder.endBlock(::MIR::Terminator::make_Call({bbRet, ::MIR::UnwindAction::make_Cleanup(bbPanic), lv_pin.clone(), ::HIR::Path(type_pin, "new_unchecked"), make_vec1(::MIR::Param(lv_mut.clone()))}));
                builder.moved_lvalue(node.span(), std::move(lv_mut));
                builder.set_cur_block(bbPanic);
                emitUnwind(sp);
                builder.set_cur_block(bbRet);
            }
            // Call `Future::poll`
            const auto& langPoll = builder.resolve().crate.getLangItemPath(sp, "Poll");
            auto type_poll = types.path(::HIR::GenericPath(langPoll, ::HIR::PathParams(node.resType)), &builder.resolve().crate.getEnumByPath(sp, langPoll));
            auto lv_poll = builder.new_temporary(type_poll);
            {
                auto bbRet = builder.new_bb_unlinked();
                auto bbPanic = builder.new_bb_unlinked();
                builder.endBlock(
                    ::MIR::Terminator::make_Call(
                        {bbRet,
                         ::MIR::UnwindAction::make_Cleanup(bbPanic),
                         lv_poll.clone(),
                         ::HIR::Path(ty_inner, builder.resolve().mLangFuture, "poll"),
                         make_vec2(
                             ::MIR::Param(lv_pin.clone()),
                             ::MIR::Param::make_Borrow({
                                 ::HIR::BorrowType::Unique,
                                 ::MIR::LValue::newDeref(::MIR::LValue::newArgument(1)) // Context is the second argument (first is `self`)
                             })
                         )}
                    )
                );
                builder.moved_lvalue(node.span(), std::move(lv_pin));
                builder.set_cur_block(bbPanic);
                emitUnwind(sp);
                builder.set_cur_block(bbRet);
            }
            // Check return
            const auto variantReady = 0;
            {
                auto bbPending = builder.new_bb_unlinked();
                auto bbReady = builder.new_bb_unlinked();
                ASSERT_BUG(node.span(), type_poll->as_Path().binding.as_Enum()->findVariant("Ready") == variantReady, "");
                ASSERT_BUG(node.span(), type_poll->as_Path().binding.as_Enum()->findVariant("Pending") == 1, "");
                builder.endBlock(::MIR::Terminator::make_Switch({lv_poll.clone(), make_vec2(bbReady, bbPending)}));
                builder.set_cur_block(bbPending);

                // `retval = ::core::task::Poll::Pending; RETURN`
                HIR::GenericPath pathLocalPoll;
                builder.with_val_type(sp, ::MIR::LValue::newReturn(), [&](const ::HIR::TypeData* ty) {
                    pathLocalPoll = ty->as_Path().path.mData.as_Generic().clone();
                });
                builder.push_stmt_assign(node.span(), ::MIR::LValue::newReturn(), ::MIR::RValue::make_EnumVariant({std::move(pathLocalPoll), 1, {}}));
                builder.push_stmt_assign(node.span(), generatorStateLv(), ::MIR::RValue::make_EnumVariant({generatorState.state_idx_enm_path.clone(), state_value, {}}));
                builder.endBlock(::MIR::Terminator::make_Return({}));

                builder.set_cur_block(bbReady);
            }
            // lv_poll.#0.0 to get the field of the first variant
            builder.set_result(node.span(), ::MIR::LValue::newField(::MIR::LValue::newDowncast(std::move(lv_poll), variantReady), 0));
        }

        void visit(::HIR::ExprNodeLet& node) override {
            TRACE_FUNCTION_F("_Let " << node.pattern);
            if (node.mValue) {
                auto _ = save_and_edit(borrowRaiseTarget, blockTmpScope);
                auto _super_let_scope = save_and_edit(superLetScope, node.isSuper ? superLetScope : blockVarScope);
                this->visit_node_ptr(node.mValue);

                if (!builder.block_active()) {
                    return;
                }
                auto res = builder.getResult(node.span());

                // Shortcut for `let foo = bar;` (avoids the extra temporary that would need to be optimised out)
                if (node.pattern.mData.is_Any() && !node.pattern.mBindings.empty() && std::all_of(node.pattern.mBindings.begin(), node.pattern.mBindings.end(), [](const HIR::PatternBinding& pb) {
                    return pb.mType == ::HIR::PatternBinding::Type::Move;
                })) {
                    this->schedule_pattern_drops(node.span(), node.pattern, PatternDropOrder::FirstCandidate);
                    for (const auto& pb : node.pattern.mBindings) {
                        builder.push_stmt_assign(node.span(), builder.getVariable(node.span(), pb.slot), mv$(res));
                    }
                } else {
                    auto pattern_value = builder.lvalue_or_temp(node.mValue->span(), node.mType, mv$(res));
                    auto dropValue = pattern_value.clone();
                    this->register_pattern_variables(node.span(), node.pattern, PatternDropOrder::FirstCandidate);
                    MIRLowerHIRLet(builder, *this, node.span(), node.pattern, mv$(pattern_value), nullptr);
                    if (blockTmpScope) {
                        builder.move_temporary_drop_to_variable_scope(node.span(), dropValue, *blockTmpScope);
                    }
                    this->schedule_registered_pattern_drops(node.span(), node.pattern, PatternDropOrder::FirstCandidate);
                }
            } else {
                this->schedule_pattern_drops(node.span(), node.pattern, PatternDropOrder::Declaration);
            }
            if (node.isSuper) {
                ASSERT_BUG(node.span(), superLetScope, "`super let` without an enclosing expression scope");
                for (const auto slot : ::HIR::pattern_binding_slots(node.pattern, PatternDropOrder::FirstCandidate)) {
                    builder.move_variable_to_scope(node.span(), slot, *superLetScope);
                }
            }
            builder.set_result(node.span(), ::MIR::RValue::make_Tuple({}));
        }

        void visit(::HIR::ExprNodeLoop& node) override {
            TRACE_FUNCTION_FR("_Loop", "_Loop");
            auto loop_block = builder.new_bb_linked();
            auto loop_body_scope = builder.new_scope_loop(node.span());
            auto loop_next = builder.new_bb_unlinked();

            auto loop_result_lvaue = builder.new_temporary(node.resType);

            auto loop_tmp_scope = builder.new_scope_temp(node.span());
            auto _ = save_and_edit(stmtScope, &loop_tmp_scope);

            loopStack.push_back(LoopDesc{mv$(loop_body_scope), node.label, node.requireLabel, loop_block, loop_next, loop_result_lvaue.clone()});
            this->visit_node_ptr(node.mCode);
            auto loop_scope = mv$(loopStack.back().scope);
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
                builder.terminate_scope(node.span(), mv$(loop_tmp_scope));
                builder.terminate_scope(node.span(), mv$(loop_scope));
                builder.endBlock(::MIR::Terminator::make_Goto(loop_block));
            } else {
                // Terminate scope without emitting cleanup (cleanup was handled by `break`)
                builder.terminate_scope(node.span(), mv$(loop_tmp_scope), false);
                builder.terminate_scope(node.span(), mv$(loop_scope), false);
            }

            if (!node.diverges) {
                DEBUG("- Doesn't diverge");
                builder.set_cur_block(loop_next);
                builder.set_result(node.span(), mv$(loop_result_lvaue));
            } else {
                DEBUG("- Diverges");
                assert(!builder.hasResult());

                builder.set_cur_block(loop_next);
                builder.endSplitArmEarly(node.span());
                assert(!builder.hasResult());
                builder.endBlock(::MIR::Terminator::make_Unreachable({}));
            }

            // TODO: Store the variable state on a break for restoration at the end of the loop.
        }

        /// Locate a loop given a name
        const LoopDesc& findLoop(const Span& sp, const RcString& target_label) const {
            if (target_label != "") {
                auto it = ::std::find_if(loopStack.rbegin(), loopStack.rend(), [&](const auto& x) {
                    return x.label == target_label;
                });
                if (it == loopStack.rend()) {
                    BUG(sp, "Named loop '" << target_label << " doesn't exist");
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
                this->visit_node_ptr(node.mValue);
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
            const LoopDesc& target_block = this->findLoop(node.span(), node.label);

            if (node.isContinue) {
                builder.terminate_scope_early(node.span(), target_block.scope, /*loop_exit=*/false);
                builder.endBlock(::MIR::Terminator::make_Goto(target_block.cur));
            } else {
                if (node.mValue) {
                    builder.push_stmt_assign(node.span(), target_block.res_value.clone(), builder.getResult(node.span()));
                } else {
                    // Set result to ()
                    builder.push_stmt_assign(node.span(), target_block.res_value.clone(), ::MIR::RValue::make_Tuple({{}}));
                }
                builder.terminate_scope_early(node.span(), target_block.scope, /*loop_exit=*/true);
                builder.endBlock(::MIR::Terminator::make_Goto(target_block.next));
            }
        }

        void visit(::HIR::ExprNodeMatch& node) override {
            TRACE_FUNCTION_FR("_Match", "_Match");
            std::vector<unsigned> let_else_initializer_temps;
            size_t let_else_first_temporary = 0;
            if (node.isLetElse) {
                ASSERT_BUG(node.span(), borrowRaiseTarget, "let-else match has no remainder temporary scope");
                let_else_first_temporary = builder.local_count();
                this->visit_node_ptr(node.mValue);
            } else {
                auto _ = save_and_edit(borrowRaiseTarget, nullptr);
                this->visit_node_ptr(node.mValue);
            }
            if (!builder.block_active()) {
                return;
            }
            auto match_val = builder.getResultInLvalue(node.mValue->span(), node.mValue->resType);
            if (node.isLetElse) {
                const auto endTemporary = builder.local_count();
                let_else_initializer_temps.reserve(endTemporary - let_else_first_temporary);
                for (auto temporary = let_else_first_temporary; temporary < endTemporary; ++temporary) {
                    let_else_initializer_temps.push_back(temporary);
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
                MIRLowerHIRMatch(builder, *this, node, mv$(match_val), let_else_initializer_temps);
            }

            if (builder.block_active()) {
                const auto& sp = node.span();

                auto res = builder.getResult(sp);
                //m_builder.raise_variables(sp, res, stmt_scope, /*to_above=*/true);
                builder.set_result(sp, mv$(res));

                //m_builder.terminate_scope( node.span(), mv$(stmt_scope) );
            } else {
                //m_builder.terminate_scope( node.span(), mv$(stmt_scope), false );
            }
        } // ExprNodeMatch

        void emitIf(/*const*/ ::HIR::ExprNodeP& cond, ::MIR::BasicBlockId true_branch, ::MIR::BasicBlockId falseBranch) {
            TRACE_FUNCTION_F("true=bb" << true_branch << ", false=bb" << falseBranch);
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
                    ::std::swap(true_branch, falseBranch);
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
                            auto inner_true_branch = builder.new_bb_unlinked();
                            emitIf(condBin->left, inner_true_branch, falseBranch);
                            // ELSE use right
                            builder.set_cur_block(inner_true_branch);
                        } else {
                            DEBUG("- Short-circuit BoolOr");

                            // IF left true: got to true
                            auto inner_false_branch = builder.new_bb_unlinked();
                            emitIf(condBin->left, true_branch, inner_false_branch);
                            // ELSE use right
                            builder.set_cur_block(inner_false_branch);
                        }

                        auto split_scope = builder.new_scope_split(condBin->span());
                        builder.endSplitArm(condBin->span(), split_scope, /*reachable=*/true);
                        auto finalTrueBranch = builder.new_bb_unlinked();
                        auto finalFalseBranch = builder.new_bb_unlinked();
                        emitIf(condBin->right, finalTrueBranch, finalFalseBranch);

                        builder.set_cur_block(finalFalseBranch);
                        builder.endSplitArm(condBin->span(), split_scope, /*reachable=*/true, true);
                        builder.endBlock(MIR::Terminator::make_Goto(falseBranch));

                        builder.set_cur_block(finalTrueBranch);
                        builder.endSplitArm(condBin->span(), split_scope, /*reachable=*/true);
                        builder.terminate_scope(condBin->span(), std::move(split_scope));
                        builder.endBlock(MIR::Terminator::make_Goto(true_branch));
                    }
                        return;
                    default:
                        break;
                }
            }

            if (auto* condLit = cast<::HIR::ExprNodeLiteral>(condP->get())) {
                DEBUG("- constant condition");
                if (condLit->mData.as_Boolean()) {
                    builder.endBlock(::MIR::Terminator::make_Goto(true_branch));
                } else {
                    builder.endBlock(::MIR::Terminator::make_Goto(falseBranch));
                }
                return;
            }

            // If short-circuiting didn't apply, emit condition
            ::MIR::LValue decisionVal;
            {
                auto scope = builder.new_scope_temp(cond->span());
                this->visit_node_ptr(*condP);
                ASSERT_BUG(cond->span(), cond->resType == ::HIR::CoreType::Bool, "If condition wasn't a bool");
                decisionVal = builder.getResultInIfCond(cond->span());
                builder.terminate_scope(cond->span(), mv$(scope));
            }

            builder.endBlock(::MIR::Terminator::make_If({mv$(decisionVal), true_branch, falseBranch}));
        }

        void generateCheckedBinop(const Span& sp, ::MIR::LValue res_slot, ::MIR::eBinOp op, ::MIR::Param val_l, const ::HIR::TypeData* ty_l, ::MIR::Param val_r, const ::HIR::TypeData* ty_r) {
            switch (op) {
                case ::MIR::eBinOp::EQ:
                case ::MIR::eBinOp::NE:
                case ::MIR::eBinOp::LT:
                case ::MIR::eBinOp::LE:
                case ::MIR::eBinOp::GT:
                case ::MIR::eBinOp::GE:
                    ASSERT_BUG(sp, ty_l == ty_r, "Types in comparison operators must be equal - " << ty_l << " != " << ty_r);
                    // Defensive assert that the type is a valid MIR comparison
                TU_MATCH_HDRA( (*ty_l), {)
                default:
                    BUG(sp, "Invalid type in comparison - " << ty_l);
                        TU_ARMA(Pointer, e) {
                            // Valid
                        }
                        // TODO: Should straight comparisons on &str be supported here?
                        TU_ARMA(Primitive, e) {
                            if (e == ::HIR::CoreType::Str) {
                                BUG(sp, "Invalid type in comparison - " << ty_l);
                            }
                        }
                }
                builder.push_stmt_assign(sp, mv$(res_slot), ::MIR::RValue::make_BinOp({ mv$(val_l), op, mv$(val_r) }));
                break;
            // Bitwise masking operations: Require equal integer types or bool
            case ::MIR::eBinOp::BIT_XOR:
            case ::MIR::eBinOp::BIT_OR :
            case ::MIR::eBinOp::BIT_AND:
                ASSERT_BUG(sp, ty_l == ty_r, "Types in bitwise operators must be equal - " << ty_l << " != " << ty_r);
                ASSERT_BUG(sp, ty_l->is_Primitive(), "Only primitives allowed in bitwise operators");
                switch(ty_l->as_Primitive())
                {
                        case ::HIR::CoreType::Str:
                        case ::HIR::CoreType::Char:
                        case ::HIR::CoreType::F32:
                        case ::HIR::CoreType::F64:
                            BUG(sp, "Invalid type for bitwise operator - " << ty_l);
                        default:
                            break;
                }
                builder.push_stmt_assign(sp, mv$(res_slot), ::MIR::RValue::make_BinOp({ mv$(val_l), op, mv$(val_r) }));
                break;
            case ::MIR::eBinOp::ADD:    case ::MIR::eBinOp::ADD_OV:
            case ::MIR::eBinOp::SUB:    case ::MIR::eBinOp::SUB_OV:
            case ::MIR::eBinOp::MUL:    case ::MIR::eBinOp::MUL_OV:
            case ::MIR::eBinOp::DIV:    case ::MIR::eBinOp::DIV_OV:
            case ::MIR::eBinOp::MOD:
                ASSERT_BUG(sp, ty_l == ty_r, "Types in arithmatic operators must be equal - " << ty_l << " != " << ty_r);
                ASSERT_BUG(sp, ty_l->is_Primitive(), "Only primitives allowed in arithmatic operators");
                switch(ty_l->as_Primitive())
                {
                        case ::HIR::CoreType::Str:
                        case ::HIR::CoreType::Char:
                        case ::HIR::CoreType::Bool:
                            BUG(sp, "Invalid type for arithmatic operator - " << ty_l);
                        default:
                            break;
                }
                // TODO: Overflow checks (none for eBinOp::MOD)
                builder.push_stmt_assign(sp, mv$(res_slot), ::MIR::RValue::make_BinOp({ mv$(val_l), op, mv$(val_r) }));
                break;
            case ::MIR::eBinOp::BIT_SHL:
            case ::MIR::eBinOp::BIT_SHR:
                ;
                ASSERT_BUG(sp, ty_l->is_Primitive(), "Only primitives allowed in arithmatic operators");
                ASSERT_BUG(sp, ty_r->is_Primitive(), "Only primitives allowed in arithmatic operators");
                switch(ty_l->as_Primitive())
                {
                        case ::HIR::CoreType::Str:
                        case ::HIR::CoreType::Char:
                        case ::HIR::CoreType::F32:
                        case ::HIR::CoreType::F64:
                            BUG(sp, "Invalid type for shift op-assignment - " << ty_l);
                        default:
                            break;
                }
                switch(ty_r->as_Primitive())
                {
                        case ::HIR::CoreType::Str:
                        case ::HIR::CoreType::Char:
                        case ::HIR::CoreType::F32:
                        case ::HIR::CoreType::F64:
                            BUG(sp, "Invalid type for shift op-assignment - " << ty_r);
                        default:
                            break;
                }
                // TODO: Overflow check
                builder.push_stmt_assign(sp, mv$(res_slot), ::MIR::RValue::make_BinOp({ mv$(val_l), op, mv$(val_r) }));
                break;
            }
        }

        void visit(::HIR::ExprNodeAssign& node) override {
            TRACE_FUNCTION_F("_Assign");
            const auto& sp = node.span();
            auto _ = disableBorrowExtension(); // A bit of a hack

            this->visit_node_ptr(node.mValue);
            ::MIR::RValue val = builder.getResult(sp);

            this->visit_node_ptr(node.slot);
            auto dst = builder.getResultUnwrapLvalue(sp);

            const auto& ty_slot = node.slot->resType;
            const auto& ty_val = node.mValue->resType;

            if (node.op != ::HIR::ExprNodeAssign::Op::None) {
                auto dstClone = dst.clone();
                ::MIR::Param val_p;
                if (auto* e = val.opt_Use()) {
                    val_p = mv$(*e);
                } else if (auto* e = val.opt_Constant()) {
                    val_p = mv$(*e);
                } else {
                    val_p = builder.lvalue_or_temp(node.span(), ty_val, mv$(val));
                }

                ASSERT_BUG(sp, ty_slot->is_Primitive(), "Assignment operator overloads are only valid on primitives - ty_slot=" << ty_slot);
                ASSERT_BUG(sp, ty_val->is_Primitive(), "Assignment operator overloads are only valid on primitives - ty_val=" << ty_val);

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
                        this->generateCheckedBinop(sp, mv$(dst), op, mv$(dstClone), ty_slot, mv$(val_p), ty_val);
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
                        this->generateCheckedBinop(sp, mv$(dst), op, mv$(dstClone), ty_slot, mv$(val_p), ty_val);
                        break;
                    case _(Shl):
                        op = ::MIR::eBinOp::BIT_SHL;
                        if (0) {
                            case _(Shr):
                                op = ::MIR::eBinOp::BIT_SHR;
                        }
                        this->generateCheckedBinop(sp, mv$(dst), op, mv$(dstClone), ty_slot, mv$(val_p), ty_val);
                        break;
                }
#undef _
            } else {
                ASSERT_BUG(sp, ty_slot == ty_val || ty_slot->equalsIgnoringRegions(ty_val), "Types must match for assignment - " << ty_slot << " != " << ty_val);
                builder.push_stmt_assign(node.span(), mv$(dst), mv$(val));
            }
            builder.set_result(node.span(), ::MIR::RValue::make_Tuple({}));
        }

        void visit(::HIR::ExprNodeBinOp& node) override {
            const auto& sp = node.span();
            TRACE_FUNCTION_F("_BinOp");

            const auto& ty_l = node.left->resType;
            const auto& ty_r = node.right->resType;
            auto res = builder.new_temporary(node.resType);

            // Short-circuiting boolean operations
            if (node.op == ::HIR::ExprNodeBinOp::Op::BoolAnd || node.op == ::HIR::ExprNodeBinOp::Op::BoolOr) {
                DEBUG("- ShortCircuit Left");
                this->visit_node_ptr(node.left);
                if (!builder.block_active()) {
                    return;
                }
                auto left = builder.getResultInLvalue(node.left->span(), ty_l);

                auto bbNext = builder.new_bb_unlinked();
                auto bbTrue = builder.new_bb_unlinked();
                auto bbFalse = builder.new_bb_unlinked();
                builder.endBlock(::MIR::Terminator::make_If({mv$(left), bbTrue, bbFalse}));

                // Generate a SplitScope to handle the conditional nature of the next code
                auto split_scope = builder.new_scope_split(node.span());

                if (node.op == ::HIR::ExprNodeBinOp::Op::BoolOr) {
                    DEBUG("- ShortCircuit ||");
                    // If left is true, assign result true and return
                    builder.set_cur_block(bbTrue);
                    builder.push_stmt_assign(node.span(), res.clone(), ::MIR::RValue(::MIR::Constant::make_Bool({true})));
                    builder.endSplitArm(node.left->span(), split_scope, /*reachable=*/true);
                    builder.endBlock(::MIR::Terminator::make_Goto(bbNext));

                    // If left is false, assign result to right
                    builder.set_cur_block(bbFalse);
                } else {
                    DEBUG("- ShortCircuit &&");
                    // If left is false, assign result false and return
                    builder.set_cur_block(bbFalse);
                    builder.push_stmt_assign(node.span(), res.clone(), ::MIR::RValue(::MIR::Constant::make_Bool({false})));
                    builder.endSplitArm(node.left->span(), split_scope, /*reachable=*/true);
                    builder.endBlock(::MIR::Terminator::make_Goto(bbNext));

                    // If left is true, assign result to right
                    builder.set_cur_block(bbTrue);
                }

                DEBUG("- ShortCircuit Right");
                auto tmp_scope = builder.new_scope_temp(node.right->span());
                this->visit_node_ptr(node.right);
                if (!builder.block_active()) {
                    builder.terminate_scope(node.right->span(), mv$(tmp_scope), false);
                    builder.endSplitArm(node.right->span(), split_scope, /*reachable=*/false);
                    builder.set_cur_block(bbNext);
                    builder.terminate_scope(node.span(), mv$(split_scope));
                    builder.set_result(node.span(), mv$(res));
                    return;
                }
                builder.push_stmt_assign(node.span(), res.clone(), builder.getResult(node.right->span()));
                builder.terminate_scope(node.right->span(), mv$(tmp_scope));

                builder.endSplitArm(node.right->span(), split_scope, /*reachable=*/true);
                builder.endBlock(::MIR::Terminator::make_Goto(bbNext));

                builder.set_cur_block(bbNext);
                builder.terminate_scope(node.span(), mv$(split_scope));
                builder.set_result(node.span(), mv$(res));
                return;
            } else {
            }

            this->visit_node_ptr(node.left);
            if (!builder.block_active()) {
                return;
            }
            auto left = builder.getResultInParam(node.left->span(), ty_l);
            this->visit_node_ptr(node.right);
            if (!builder.block_active()) {
                return;
            }
            auto right = builder.getResultInParam(node.right->span(), ty_r);

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
                    this->generateCheckedBinop(sp, res.clone(), op, mv$(left), ty_l, mv$(right), ty_r);
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
                    this->generateCheckedBinop(sp, res.clone(), op, mv$(left), ty_l, mv$(right), ty_r);
                    break;

                case ::HIR::ExprNodeBinOp::Op::Shr:
                    op = ::MIR::eBinOp::BIT_SHR;
                    if (0) {
                        case ::HIR::ExprNodeBinOp::Op::Shl:
                            op = ::MIR::eBinOp::BIT_SHL;
                    }
                    this->generateCheckedBinop(sp, res.clone(), op, mv$(left), ty_l, mv$(right), ty_r);
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
                    this->generateCheckedBinop(sp, res.clone(), op, mv$(left), ty_l, mv$(right), ty_r);
                    break;

                // Short-circuiting boolean operations
                case ::HIR::ExprNodeBinOp::Op::BoolAnd:
                case ::HIR::ExprNodeBinOp::Op::BoolOr:
                    BUG(node.span(), "");
                    break;
            }
            builder.set_result(node.span(), mv$(res));
        }

        void visit(::HIR::ExprNodeUniOp& node) override {
            TRACE_FUNCTION_F("_UniOp");

            const auto& ty_val = node.mValue->resType;
            this->visit_node_ptr(node.mValue);
            auto val = builder.getResultInLvalue(node.mValue->span(), ty_val);

            ::MIR::RValue res;
            switch (node.op) {
                case ::HIR::ExprNodeUniOp::Op::Invert:
                    if (ty_val->is_Primitive()) {
                        switch (ty_val->as_Primitive()) {
                            case ::HIR::CoreType::Str:
                            case ::HIR::CoreType::Char:
                            case ::HIR::CoreType::F32:
                            case ::HIR::CoreType::F64:
                                BUG(node.span(), "`!` operator on invalid type - " << ty_val);
                                break;
                            default:
                                break;
                        }
                    } else {
                        BUG(node.span(), "`!` operator on invalid type - " << ty_val);
                    }
                    res = ::MIR::RValue::make_UniOp({mv$(val), ::MIR::eUniOp::INV});
                    break;
                case ::HIR::ExprNodeUniOp::Op::Negate:
                    if (ty_val->is_Primitive()) {
                        switch (ty_val->as_Primitive()) {
                            case ::HIR::CoreType::Str:
                            case ::HIR::CoreType::Char:
                            case ::HIR::CoreType::Bool:
                                BUG(node.span(), "`-` operator on invalid type - " << ty_val);
                                break;
                            case ::HIR::CoreType::U8:
                            case ::HIR::CoreType::U16:
                            case ::HIR::CoreType::U32:
                            case ::HIR::CoreType::U64:
                            case ::HIR::CoreType::U128:
                            case ::HIR::CoreType::Usize:
                                BUG(node.span(), "`-` operator on unsigned integer - " << ty_val);
                                break;
                            default:
                                break;
                        }
                    } else {
                        BUG(node.span(), "`!` operator on invalid type - " << ty_val);
                    }
                    res = ::MIR::RValue::make_UniOp({mv$(val), ::MIR::eUniOp::NEG});
                    break;
            }
            builder.set_result(node.span(), mv$(res));
        }

        void visit(::HIR::ExprNodeBorrow& node) override {
            TRACE_FUNCTION_F("_Borrow");

            auto _ = save_and_edit(inBorrow, true);

            const auto& ty_val = node.mValue->resType;
            this->visit_node_ptr(node.mValue);
            auto val = builder.getResultInLvalue(node.mValue->span(), ty_val);

            if (borrowRaiseTarget) {
                DEBUG("- Raising borrow to scope " << *borrowRaiseTarget);
                builder.raise_temporaries(node.span(), val, *borrowRaiseTarget);
            }

            builder.set_result(node.span(), ::MIR::RValue::make_Borrow({node.mType, false, mv$(val)}));
        }

        void visit(::HIR::ExprNodeRawBorrow& node) override {
            TRACE_FUNCTION_F("_RawBorrow");

            auto _ = save_and_edit(inBorrow, true);

            const auto& ty_val = node.mValue->resType;
            this->visit_node_ptr(node.mValue);
            auto val = builder.getResultInLvalue(node.mValue->span(), ty_val);

            if (borrowRaiseTarget) {
                DEBUG("- Raising borrow to scope " << *borrowRaiseTarget);
                builder.raise_temporaries(node.span(), val, *borrowRaiseTarget);
            }

            builder.set_result(node.span(), ::MIR::RValue::make_Borrow({node.mType, true, mv$(val)}));
        }

        void visit(::HIR::ExprNodeCast& node) override {
            TRACE_FUNCTION_F("_Cast " << node.resType);
            this->visit_node_ptr(node.mValue);

            const auto& ty_out = node.resType;
            const auto& ty_in = node.mValue->resType;

            // TODO: The correct behavior is to do the cast (into a rvalue) no matter what.
            // See test run-pass/issue-36936
            if (ty_out == ty_in) {
                return;
            }

            auto val = builder.getResultInLvalue(node.mValue->span(), node.mValue->resType);

            TU_MATCH_HDRA( (*ty_out), {)
            default:
                BUG(node.span(), "Invalid cast to " << ty_out << " from " << ty_in);
                TU_ARMA(Function, de) {
                    // Just trust the previous stages.
                    if (ty_in->is_Function()) {
                        ASSERT_BUG(node.span(), de.argTypes == ty_in->as_Function().argTypes, ty_in);
                    } else if (ty_in->is_NamedFunction()) {
                        // TODO: Extra checks?
                    } else {
                        BUG(node.span(), "_Cast from bad type: " << ty_in);
                    }
                }
                TU_ARMA(Pointer, de) {
                    if (ty_in->is_Primitive()) {
                        const auto& ie = ty_in->as_Primitive();
                        switch (ie) {
                            case ::HIR::CoreType::Bool:
                            case ::HIR::CoreType::Char:
                            case ::HIR::CoreType::Str:
                            case ::HIR::CoreType::F32:
                            case ::HIR::CoreType::F64:
                                BUG(node.span(), "Cannot cast to pointer from " << ty_in);
                            default:
                                break;
                        }
                        // TODO: Only valid if T: Sized in *{const/mut/move} T
                    } else if (const auto* se = ty_in->opt_Borrow()) {
                        if (de.inner != se->inner && !de.inner->equalsIgnoringRegions(se->inner)) {
                            BUG(node.span(), "Cannot cast to " << ty_out << " from " << ty_in);
                        }
                        // Valid
                    } else if (ty_in->is_Function() || ty_in->is_NamedFunction()) {
                        if (!builder.resolve().type_is_sized(node.span(), de.inner)) {
                            BUG(node.span(), "Cannot cast to " << ty_out << " from " << ty_in);
                        }
                        // Valid
                    } else if (const auto* se = ty_in->opt_Pointer()) {
                        // Valid
                        if (se->inner == de.inner) {
                        }
                        // - If making a fat pointer from thin, convert to _Unsize
                        else if (builder.resolve().canUnsize(node.span(), de.inner, se->inner)) {
                            builder.set_result(node.span(), ::MIR::RValue::make_MakeDst({mv$(val), ::MIR::Constant::make_ItemAddr({})}));
                            auto tmp_ty = builder.resolve().crate.types.pointer(se->type, de.inner);
                            val = builder.getResultInLvalue(node.mValue->span(), tmp_ty);
                        }
                    } else {
                        BUG(node.span(), "Cannot cast to pointer from " << ty_in);
                    }
                }
                TU_ARMA(Primitive, de) {
                    switch (de) {
                        case ::HIR::CoreType::Str:
                            BUG(node.span(), "Cannot cast to str");
                            break;
                        case ::HIR::CoreType::Char:
                            if (ty_in->is_Primitive() && ty_in->as_Primitive() == ::HIR::CoreType::U8) {
                                // Valid
                            } else {
                                BUG(node.span(), "Cannot cast to char from " << ty_in);
                            }
                            break;
                        case ::HIR::CoreType::Bool:
                            BUG(node.span(), "Cannot cast to bool");
                            break;
                        case ::HIR::CoreType::F32:
                        case ::HIR::CoreType::F64:
                            if (ty_in->is_Primitive()) {
                                switch (de) {
                                    case ::HIR::CoreType::Str:
                                    case ::HIR::CoreType::Char:
                                    case ::HIR::CoreType::Bool:
                                        BUG(node.span(), "Cannot cast to " << ty_out << " from " << ty_in);
                                        break;
                                    default:
                                        // Valid
                                        break;
                                }
                            } else {
                                BUG(node.span(), "Cannot cast to " << ty_out << " from " << ty_in);
                            }
                            break;
                        default:
                            if (ty_in->opt_Primitive()) {
                                switch (de) {
                                    case ::HIR::CoreType::Str:
                                        BUG(node.span(), "Cannot cast to " << ty_out << " from " << ty_in);
                                    default:
                                        // Valid
                                        break;
                                }
                            } else if (const auto* se = ty_in->opt_Path()) {
                                if (se->binding.is_Enum()) {
                                    // TODO: Check if it's a repr(ty/C) enum - and if the type matches
                                } else {
                                    BUG(node.span(), "Cannot cast to " << ty_out << " from " << ty_in);
                                }
                            }
                            // NOTE: Valid for all integer types
                            else if (ty_in->is_Pointer()) {
                                // TODO: Only valid for T: Sized?
                            } else if (de == ::HIR::CoreType::Usize && ty_in->is_Function()) {
                                // TODO: Always valid?
                            } else if (de == ::HIR::CoreType::Usize && ty_in->is_NamedFunction()) {
                                // TODO: Always valid?
                            } else {
                                BUG(node.span(), "Cannot cast to " << ty_out << " from " << ty_in);
                            }
                            break;
                    }
                }
            }
            auto res = builder.new_temporary(node.resType);
            builder.push_stmt_assign(node.span(), res.clone(), ::MIR::RValue::make_Cast({ mv$(val), node.resType }));
            builder.set_result( node.span(), mv$(res) );
        }

        void visit(::HIR::ExprNodeUnsize& node) override {
            TRACE_FUNCTION_F("_Unsize");
            this->visit_node_ptr(node.mValue);

            const auto& ty_out = node.resType;
            const auto& ty_in = node.mValue->resType;

            if (ty_out == ty_in) {
                return;
            }

            auto ptr_lval = builder.getResultInLvalue(node.mValue->span(), node.mValue->resType);

            if (ty_out->is_Borrow() && ty_in->is_Borrow()) {
                const auto& oe = ty_out->as_Borrow();
                const auto& ie = ty_in->as_Borrow();
                const auto& ty_out = oe.inner;
                const auto& ty_in = ie.inner;
                TU_MATCH_HDRA( (*ty_out), {)
                default: {
                        const auto& langUnsize = builder.crate().getLangItemPath(node.span(), "unsize");
                        if (builder.resolve().findImpl(node.span(), langUnsize, ::HIR::PathParams(ty_out), ty_in, [](auto, bool) {
                            return true;
                        })) {
                            // - HACK: Emit a cast operation on the pointers. Leave it up to monomorph to 'fix' it
                            builder.set_result(node.span(), ::MIR::RValue::make_MakeDst({mv$(ptr_lval), ::MIR::Constant::make_ItemAddr({})}));
                        } else {
                            // Probably an error?
                            builder.set_result(node.span(), ::MIR::RValue::make_MakeDst({mv$(ptr_lval), ::MIR::Constant::make_ItemAddr({})}));
                            //TODO(node.span(), "MIR _Unsize to " << ty_out);
                        }
                    }
                    TU_ARMA(Slice, e) {
                        if (ty_in->is_Array()) {
                            const auto& in_array = ty_in->as_Array();
                            ::MIR::Constant size_val;
                        TU_MATCH_HDRA( (in_array.size), {)
                        TU_ARMA(Unevaluated, se) {
                            TU_MATCH_HDRA( (se), {)
                            default:
                                BUG(node.span(), "Unsize Array with unknown size " << ty_in);
                                        TU_ARMA(Generic, cge)
                                        size_val = cge;
                            }
                                }
                                TU_ARMA(Known, se) {
                                    size_val = ::MIR::Constant::make_Uint({U128(se), ::HIR::CoreType::Usize});
                                }
                        }
                        builder.set_result( node.span(), ::MIR::RValue::make_MakeDst({ mv$(ptr_lval), mv$(size_val) }) );
                        } else if (ty_in->is_Generic() || (ty_in->is_Path() && ty_in->as_Path().binding.is_Opaque())) {
                            // The source is thin here: its concrete array length becomes
                            // available only after monomorphisation. Preserve the unsize
                            // sentinel for MIR cleanup instead of reading nonexistent metadata.
                            builder.set_result(node.span(), ::MIR::RValue::make_MakeDst({mv$(ptr_lval), ::MIR::Constant::make_ItemAddr({})}));
                        } else {
                            ASSERT_BUG(node.span(), ty_in->is_Array(), "Unsize to slice from non-array - " << ty_in);
                        }
                    }
                    TU_ARMA(TraitObject, e) {
                        // NOTE: This pattern (an empty ItemAddr) is detected by cleanup, which populates the vtable properly
                        builder.set_result(node.span(), ::MIR::RValue::make_MakeDst({mv$(ptr_lval), ::MIR::Constant::make_ItemAddr({})}));
                    }
                }
            } else {
                // NOTES: (from IRC: eddyb)
                // < eddyb> they're required that T and U are the same struct definition (with different type parameters) and exactly one field differs in type between T and U (ignoring PhantomData)
                // < eddyb> Mutabah: I forgot to mention that the field that differs in type must also impl CoerceUnsized

                // TODO: Just emit a cast and leave magic handling to codegen
                // - This code _could_ do inspection of the types and insert a destructure+unsize+restructure, but that does't handle direct `T: CoerceUnsize<U>`
                builder.set_result(node.span(), ::MIR::RValue::make_MakeDst({mv$(ptr_lval), ::MIR::Constant::make_ItemAddr({})}));
            }
        }

        void visit_index_operator(::HIR::ExprNodeIndex& node, const ::HIR::TypeData* ty_val, MIR::LValue value, const ::HIR::TypeData* ty_idx, MIR::LValue index) {
            DEBUG("");
            const Span& sp = node.span();

            // NOTE: Do operator replacement here after handling scope-raising for _Borrow
            if (borrowRaiseTarget && inBorrow) {
                DEBUG("- Raising deref in borrow to scope " << *borrowRaiseTarget);
                builder.raise_temporaries(sp, value, *borrowRaiseTarget);
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
            ::HIR::PathParams pp_trait;
            pp_trait.types.push_back(ty_idx);
            ::HIR::GenericPath trait{builder.resolve().crate.getLangItemPath(node.span(), langitem), std::move(pp_trait)};

            ::HIR::PathParams pp_method;
            pp_method.mLifetimes.push_back(HIR::LifetimeRef());
            auto method_path = ::HIR::Path(ty_val, std::move(trait), RcString::new_interned(method), std::move(pp_method));

            // Store a borrow of the input value
            ::std::vector<::MIR::Param> args;
            args.push_back(builder.lvalue_or_temp(sp, builder.resolve().crate.types.borrow(bt, node.mValue->resType), ::MIR::RValue::make_Borrow({bt, false, std::move(value)})));
            args.push_back(std::move(index));
            builder.moved_lvalue(node.span(), args[0].as_LValue());
            builder.moved_lvalue(node.span(), args[1].as_LValue());
            auto res_val = builder.new_temporary(builder.resolve().crate.types.borrow(bt, node.resType));
            // Call the above trait method
            // Store result of that call in `val` (which will be derefed below)
            auto ok_block = builder.new_bb_unlinked();
            auto panic_block = builder.new_bb_unlinked();
            builder.endBlock(::MIR::Terminator::make_Call({ok_block, ::MIR::UnwindAction::make_Cleanup(panic_block), res_val.clone(), std::move(method_path), std::move(args)}));
            builder.set_cur_block(panic_block);
            emitUnwind(sp);

            builder.set_cur_block(ok_block);
            builder.set_result(node.span(), ::MIR::LValue::newDeref(std::move(res_val)));
        }

        void visit(::HIR::ExprNodeIndex& node) override {
            TRACE_FUNCTION_F("_Index");

            // NOTE: Calculate the index first (so if it borrows from the source, it's over by the time that's needed)
            const auto& ty_idx = node.index->resType;
            this->visit_node_ptr(node.index);
            auto index = builder.getResultInLvalue(node.index->span(), ty_idx);

            const auto& ty_val = node.mValue->resType;
            this->visit_node_ptr(node.mValue);
            auto value = builder.getResultInLvalue(node.mValue->span(), ty_val);

            if (ty_idx != ::HIR::CoreType::Usize) {
                DEBUG("non-usize index");
                visit_index_operator(node, ty_val, std::move(value), ty_idx, std::move(index));
                return;
            }

            ::MIR::RValue limit_val;
            TU_MATCH_HDRA( (*ty_val), {)
            default:
                DEBUG("non-builtin type");
                visit_index_operator(node, ty_val, std::move(value), ty_idx, std::move(index));
                return;
                TU_ARMA(Array, e) {
                TU_MATCH_HDRA( (e.size), {)
                TU_ARMA(Unevaluated, se) {
                            if (se.is_Generic()) {
                                limit_val = ::MIR::Constant::make_Generic(se.as_Generic());
                                break;
                            }
                            BUG(node.span(), "Indexing with unknown size - " << e.size);
                        }
                        TU_ARMA(Known, se) {
                            limit_val = ::MIR::Constant::make_Uint({U128(se), ::HIR::CoreType::Usize});
                        }
                }
                }
                TU_ARMA(Slice, e) {
                    limit_val = ::MIR::RValue::make_DstMeta({builder.getPtrToDst(node.mValue->span(), value)});
                }
            }

            {
                auto limit_lval = builder.lvalue_or_temp(node.span(), ty_idx, mv$(limit_val));

                auto cmpRes = builder.new_temporary(builder.resolve().crate.types.primitive(::HIR::CoreType::Bool));
                builder.push_stmt_assign(node.span(), cmpRes.clone(), ::MIR::RValue::make_BinOp({index.clone(), ::MIR::eBinOp::GE, limit_lval.clone()}));
                auto armPanic = builder.new_bb_unlinked();
                auto armContinue = builder.new_bb_unlinked();
                builder.endBlock(::MIR::Terminator::make_If({mv$(cmpRes), armPanic, armContinue}));

                builder.set_cur_block(armPanic);
                const auto& panic_bounds_check = builder.crate().getLangItemPath(node.span(), "panic_bounds_check");
                auto panic_result = builder.new_temporary(builder.resolve().crate.types.diverge());
                auto panic_return = builder.new_bb_unlinked();
                auto panic_unwind = builder.new_bb_unlinked();
                builder.endBlock(
                    ::MIR::Terminator::make_Call({
                        panic_return,
                        ::MIR::UnwindAction::make_Cleanup(panic_unwind),
                        std::move(panic_result),
                        ::HIR::Path(panic_bounds_check),
                        make_vec2<::MIR::Param>(index.clone(), limit_lval.clone()),
                    })
                );

                builder.set_cur_block(panic_return);
                builder.endBlock(::MIR::Terminator::make_Unreachable({}));

                builder.set_cur_block(panic_unwind);
                emitUnwind(node.span());

                builder.set_cur_block(armContinue);
            }

            if( !index.is_Local())
            {
                auto local_idx = builder.new_temporary(builder.resolve().crate.types.primitive(::HIR::CoreType::Usize));
                builder.push_stmt_assign(node.span(), local_idx.clone(), mv$(index));
                index = mv$(local_idx);
            }
            builder.set_result( node.span(), ::MIR::LValue::newIndex( mv$(value), index.root.as_Local() ) );
        }

        void visit(::HIR::ExprNodeDeref& node) override {
            const Span& sp = node.span();
            TRACE_FUNCTION_F("_Deref");

            const auto& ty_val = node.mValue->resType;
            this->visit_node_ptr(node.mValue);
            auto val = builder.getResultInLvalue(node.mValue->span(), ty_val);

            bool use_trait = node.traitUsed == ::HIR::ExprNodeDeref::TraitUsed::Trait;
            if (node.traitUsed == ::HIR::ExprNodeDeref::TraitUsed::Unknown) {
                use_trait = !ty_val->is_Pointer() && !ty_val->is_Borrow() && !builder.is_type_owned_box(ty_val);
            }

            if (use_trait) {
                // Do operator replacement here after handling scope-raising
                // for _Borrow.  The type checker recorded this choice, so a
                // primitive reference can still dispatch to a user impl.
                if (borrowRaiseTarget && inBorrow) {
                    DEBUG("- Raising deref in borrow to scope " << *borrowRaiseTarget);
                    builder.raise_temporaries(node.span(), val, *borrowRaiseTarget);
                }

                const char* langitem = nullptr;
                const char* method = nullptr;
                ::HIR::BorrowType bt;
                // - Uses the value's usage beacuse for T: Copy node.m_value->m_usage is Borrow, but node.m_usage is Move
                switch (node.mValue->usage) {
                    case ::HIR::ValueUsage::Unknown:
                        BUG(sp, "Unknown usage type of deref value - " << ty_val);
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

                auto method_path = ::HIR::Path(ty_val, ::HIR::GenericPath(builder.resolve().crate.getLangItemPath(node.span(), langitem), {}), method, HIR::PathParams(HIR::LifetimeRef()));

                ::std::vector<::MIR::Param> args;
                args.push_back(builder.lvalue_or_temp(sp, builder.resolve().crate.types.borrow(bt, node.mValue->resType), ::MIR::RValue::make_Borrow({bt, false, mv$(val)})));
                builder.moved_lvalue(node.span(), args[0].as_LValue());
                val = builder.new_temporary(builder.resolve().crate.types.borrow(bt, node.resType));
                auto ok_block = builder.new_bb_unlinked();
                auto panic_block = builder.new_bb_unlinked();
                builder.endBlock(::MIR::Terminator::make_Call({ok_block, ::MIR::UnwindAction::make_Cleanup(panic_block), val.clone(), mv$(method_path), mv$(args)}));
                builder.set_cur_block(panic_block);
                emitUnwind(sp);

                builder.set_cur_block(ok_block);
            }

            builder.set_result( node.span(), ::MIR::LValue::newDeref( mv$(val) ) );
        }

        void visit(::HIR::ExprNodeEmplace& node) override {
            assert(node.mType == ::HIR::ExprNodeEmplace::Type::Boxer);
            const auto& dataTy = node.mValue->resType;

            node.mValue->visit(*this);
            auto val = builder.getResult(node.span());

            return boxNew(node, dataTy, std::move(val));
        }

        void boxNew(::HIR::ExprNode& node, const ::HIR::TypeData* dataTy, ::MIR::RValue val) {
            const auto& lang_exchange_malloc = builder.crate().getLangItemPath(node.span(), "exchange_malloc");
            //const auto& lang_owned_box = m_builder.crate().get_lang_item_path(node.span(), "owned_box");

            ::HIR::PathParams trait_params_data;
            trait_params_data.types.push_back(dataTy);
            auto& types = builder.resolve().crate.types;

            // 1. Determine the size/alignment of the type
            ::MIR::Param size_param, alignParam;
            size_t item_size, item_align;
            if (TargetGetSizeAndAlignOf(node.span(), builder.resolve(), dataTy, item_size, item_align)) {
                size_param = ::MIR::Constant::make_Uint({U128(item_size), ::HIR::CoreType::Usize});
                alignParam = ::MIR::Constant::make_Uint({U128(item_align), ::HIR::CoreType::Usize});
            } else {
                // Insert calls to "size_of" and "align_of" intrinsics
                auto size_slot = builder.new_temporary(types.primitive(::HIR::CoreType::Usize));
                auto sizePanic = builder.new_bb_unlinked();
                auto sizeOk = builder.new_bb_unlinked();
                builder.endBlock(::MIR::Terminator::make_Call({sizeOk, ::MIR::UnwindAction::make_Cleanup(sizePanic), size_slot.clone(), ::MIR::CallTarget::make_Intrinsic({"size_of", trait_params_data.clone()}), {}}));
                builder.set_cur_block(sizePanic);
                emitUnwind(node.span());
                builder.set_cur_block(sizeOk);
                auto alignSlot = builder.new_temporary(types.primitive(::HIR::CoreType::Usize));
                auto alignPanic = builder.new_bb_unlinked();
                auto alignOk = builder.new_bb_unlinked();
                builder.endBlock(::MIR::Terminator::make_Call({alignOk, ::MIR::UnwindAction::make_Cleanup(alignPanic), alignSlot.clone(), ::MIR::CallTarget::make_Intrinsic({"align_of", trait_params_data.clone()}), {}}));
                builder.set_cur_block(alignPanic);
                emitUnwind(node.span());
                builder.set_cur_block(alignOk);

                size_param = ::std::move(size_slot);
                alignParam = ::std::move(alignSlot);
            }

            // 2. Call the allocator function and get a pointer
            // - NOTE: "exchange_malloc" returns a `*mut u8`, need to cast that to the target type
            auto place_raw_type = types.pointer(::HIR::BorrowType::Unique, types.primitive(::HIR::CoreType::U8));
            auto place_raw = builder.new_temporary(place_raw_type);

            auto placePanic = builder.new_bb_unlinked();
            auto placeOk = builder.new_bb_unlinked();
            builder.endBlock(::MIR::Terminator::make_Call({placeOk, ::MIR::UnwindAction::make_Cleanup(placePanic), place_raw.clone(), ::HIR::Path(lang_exchange_malloc), make_vec2<::MIR::Param>(::std::move(size_param), ::std::move(alignParam))}));
            builder.set_cur_block(placePanic);
            emitUnwind(node.span());
            builder.set_cur_block(placeOk);

            auto place_type = types.pointer(::HIR::BorrowType::Unique, dataTy);
            auto place = builder.new_temporary(place_type);
            builder.push_stmt_assign(node.span(), place.clone(), ::MIR::RValue::make_Cast({mv$(place_raw), place_type}));
            // 3. Do a non-dropping write into the target location (i.e. just a MIR assignment)
            builder.push_stmt_assign(node.span(), ::MIR::LValue::newDeref(place.clone()), mv$(val), /*drop_destination=*/false);
            // 4. Convert the pointer into an `owned_box`
            const auto& res_type = node.resType;
            auto res = builder.new_temporary(res_type);
            auto castPanic = builder.new_bb_unlinked();
            auto castOk = builder.new_bb_unlinked();
            ::HIR::PathParams transmute_params;
            transmute_params.types.push_back(res_type);
            transmute_params.types.push_back(place_type);
            builder.endBlock(::MIR::Terminator::make_Call({castOk, ::MIR::UnwindAction::make_Cleanup(castPanic), res.clone(), ::MIR::CallTarget::make_Intrinsic({"transmute", mv$(transmute_params)}), make_vec1(::MIR::Param(mv$(place)))}));
            builder.set_cur_block(castPanic);
            emitUnwind(node.span());
            builder.set_cur_block(castOk);

            builder.set_result(node.span(), mv$(res));
        }

        void visit(::HIR::ExprNodeTupleVariant& node) override {
            const Span& sp = node.span();
            TRACE_FUNCTION_F("_TupleVariant");
            ::std::vector<::MIR::Param> values;
            values.reserve(node.mArgs.size());
            for (auto& arg : node.mArgs) {
                this->visit_node_ptr(arg);
                if (!builder.block_active()) {
                    return;
                }
                values.push_back(builder.getResultInParam(arg->span(), arg->resType));
            }

            if (node.isStruct) {
                builder.set_result(node.span(), ::MIR::RValue::make_Struct({node.mPath.clone(), mv$(values)}));
            } else {
                // Get the variant index from the enum.
                auto enumPath = node.mPath.clone();
                const auto var_name = enumPath.mPath.pop_component();
                const auto& enm = builder.crate().getEnumByPath(sp, enumPath.mPath);

                size_t idx = enm.findVariant(var_name);
                ASSERT_BUG(sp, idx != SIZE_MAX, "Variant " << node.mPath.mPath << " isn't present");

                // TODO: Validation?
                ASSERT_BUG(sp, enm.mData.is_Data(), "TupleVariant on non-data enum - " << node.mPath.mPath);


                builder.set_result(node.span(), ::MIR::RValue::make_EnumVariant({mv$(enumPath), static_cast<unsigned>(idx), mv$(values)}));
            }
        }

        ::std::vector<::MIR::Param> getArgs(/*const*/ ::std::vector<::HIR::ExprNodeP>& args) {
            ::std::vector<::MIR::Param> values;
            values.reserve(args.size());
            for (auto& arg : args) {
                this->visit_node_ptr(arg);
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
                        auto tmp = builder.new_temporary(arg->resType);
                        builder.push_stmt_assign(arg->span(), tmp.clone(), mv$(res));
                        values.push_back(mv$(tmp));
                    }
                }
            }
            // Keep already evaluated arguments live while evaluating the remaining arguments.
            // A later argument can yield, so consuming an earlier temporary here would prevent
            // the coroutine lowering from saving a value that the eventual call still needs.
            for (size_t i = 0; i < values.size(); i++) {
                if (const auto* e = values[i].opt_LValue()) {
                    builder.moved_lvalue(args[i]->span(), *e);
                }
            }
            return values;
        }

        void visit(::HIR::ExprNodeCallPath& node) override {
            TRACE_FUNCTION_F("_CallPath " << node.mPath);
            // TODO: if this is a `<foo as Index[Mut]>::index[_mut]` call then allow the borrow raise to go through to the receiver
            ::std::vector<MIR::Param> values;
            bool is_operator = false;
            if (const auto* pe = node.mPath.mData.opt_UfcsKnown()) {
                if (pe->trait.mPath == builder.resolve().crate.getLangItemPathOpt("index")) {
                    is_operator = true;
                } else if (pe->trait.mPath == builder.resolve().crate.getLangItemPathOpt("index_mut")) {
                    is_operator = true;
                } else if (pe->trait.mPath == builder.resolve().crate.getLangItemPathOpt("deref")) {
                    is_operator = true;
                } else if (pe->trait.mPath == builder.resolve().crate.getLangItemPathOpt("deref_mut")) {
                    is_operator = true;
                }
            }
            if (is_operator) {
                values = getArgs(node.mArgs);
            } else {
                auto _ = save_and_edit(borrowRaiseTarget, nullptr);
                values = getArgs(node.mArgs);
            }
            if (!builder.block_active()) {
                return;
            }

            auto panic_block = builder.new_bb_unlinked();
            auto next_block = builder.new_bb_unlinked();
            auto res = builder.new_temporary(node.resType);

            bool unconditional_diverge = false;

            // Emit intrinsics as a special call type
            if (node.mPath.mData.is_Generic()) {
                const auto& gpath = node.mPath.mData.as_Generic();
                const auto& fcn = builder.crate().getFunctionByPath(node.span(), gpath.mPath);
                if (gpath.mPath.crate_name() == "#intrinsics") {
                    const auto& name = gpath.mPath.components().back();
                    if (name == "offset_of") {
                        builder.endBlock(::MIR::Terminator::make_Call({next_block, ::MIR::UnwindAction::make_Cleanup(panic_block), res.clone(), ::MIR::CallTarget::make_Intrinsic({name, gpath.mParams.clone()}), mv$(values)}));
                    } else {
                        ERROR(node.span(), E0000, "Unknown builtin - " << gpath.mPath);
                    }
                } else if (fcn.mAbi == "rust-intrinsic") {
                    auto name = gpath.mPath.components().back();
                    if (name == "ptr_metadata") {
                        auto& v = values.front();
                        builder.push_stmt_assign(node.span(), res.clone(), ::MIR::RValue::make_DstMeta({std::move(v.as_LValue())}));
                        builder.set_result(node.span(), std::move(res));
                        return;
                    }
                    // aggregate_raw_ptr: Lowers to mrustc's MakeDst (rustc's `Aggregate` with `AggregateKind::RawPtr`)
                    if (name == "aggregate_raw_ptr") {
                        auto& v_ptr = values.at(0);
                        auto& v_meta = values.at(1);
                        builder.push_stmt_assign(node.span(), res.clone(), ::MIR::RValue::make_MakeDst({std::move(v_ptr), std::move(v_meta)}));
                        builder.set_result(node.span(), std::move(res));
                        return;
                    }
                    if (name == "ub_checks") {
                        builder.set_result(node.span(), ::MIR::Constant::make_Bool({true}));
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
                        ::MIR::LValue   index_lv = builder.new_temporary(builder.resolve().crate.types.primitive(HIR::CoreType::Usize));
                        TU_MATCH_HDRA((values[1]), {)
                        TU_ARMA(LValue, lv) {
                                builder.push_stmt_assign(node.span(), index_lv.clone(), std::move(lv));
                            }
                            TU_ARMA(Constant, c) {
                                builder.push_stmt_assign(node.span(), index_lv.clone(), std::move(c));
                            }
                            TU_ARMA(Borrow, v)
                            TODO(node.span(), "Borrow index?");
                        }
                        const auto& ptr_ty = gpath.mParams.types.at(0);
                        ASSERT_BUG(node.span(), ptr_ty->is_Borrow() || ptr_ty->is_Pointer(), "" << ptr_ty);
                        bool is_raw = ptr_ty->is_Pointer();
                        auto borrowTy = is_raw ? ptr_ty->as_Pointer().type : ptr_ty->as_Borrow().type;
                        builder.push_stmt_assign(node.span(), res.clone(), ::MIR::RValue::make_Borrow({
                            borrowTy,
                            is_raw,
                            ::MIR::LValue::newIndex(std::move(slot), std::move(index_lv.as_Local()))
                            }));
                        builder.set_result(node.span(), std::move(res));
                        return ;
                    }

                    // Floating point operations that can be algebraically optimised
                    // Lazy: Just conver to base operations
                    if (name == "fadd_algebraic") {
                        builder.set_result(node.span(), ::MIR::RValue::make_BinOp({std::move(values[0]), ::MIR::eBinOp::ADD, std::move(values[1])}));
                        return;
                    }
                    if (name == "fsub_algebraic") {
                        builder.set_result(node.span(), ::MIR::RValue::make_BinOp({std::move(values[0]), ::MIR::eBinOp::SUB, std::move(values[1])}));
                        return;
                    }
                    if (name == "fmul_algebraic") {
                        builder.set_result(node.span(), ::MIR::RValue::make_BinOp({std::move(values[0]), ::MIR::eBinOp::MUL, std::move(values[1])}));
                        return;
                    }
                    if (name == "fdiv_algebraic") {
                        builder.set_result(node.span(), ::MIR::RValue::make_BinOp({std::move(values[0]), ::MIR::eBinOp::DIV, std::move(values[1])}));
                        return;
                    }
                    if (name == "frem_algebraic") {
                        builder.set_result(node.span(), ::MIR::RValue::make_BinOp({std::move(values[0]), ::MIR::eBinOp::MOD, std::move(values[1])}));
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
                    builder.endBlock(::MIR::Terminator::make_Call({next_block, ::MIR::UnwindAction::make_Cleanup(panic_block), res.clone(), ::MIR::CallTarget::make_Intrinsic({name, gpath.mParams.clone()}), mv$(values)}));
                } else if (fcn.mAbi == "platform-intrinsic") {
                    builder.endBlock(::MIR::Terminator::make_Call({next_block, ::MIR::UnwindAction::make_Cleanup(panic_block), res.clone(), ::MIR::CallTarget::make_Intrinsic({RcString(FMT("platform:" << gpath.mPath.components().back())), gpath.mParams.clone()}), mv$(values)}));
                }

                // rustc has drop_in_place as a lang item, mrustc uses an intrinsic
                if (gpath.mPath == builder.crate().getLangItemPathOpt("drop_in_place")) {
                    builder.endBlock(::MIR::Terminator::make_Call({next_block, ::MIR::UnwindAction::make_Cleanup(panic_block), res.clone(), ::MIR::CallTarget::make_Intrinsic({"drop_in_place", gpath.mParams.clone()}), mv$(values)}));
                }

                if (fcn.returnType->is_Diverge()) {
                    unconditional_diverge = true;
                }
            } else {
                // TODO: Know if the call unconditionally diverges.
                if (node.cache.argTypes.back()->is_Diverge()) {
                    unconditional_diverge = true;
                }
            }

            // If the call wasn't to an intrinsic, emit it as a path
            if (builder.block_active()) {
                builder.endBlock(::MIR::Terminator::make_Call({next_block, ::MIR::UnwindAction::make_Cleanup(panic_block), res.clone(), node.mPath.clone(), mv$(values)}));
            }

            builder.set_cur_block(panic_block);
            emitUnwind(node.span());

            builder.set_cur_block(next_block);

            // If the function doesn't return, early-terminate the return block.
            if (unconditional_diverge) {
                builder.endBlock(::MIR::Terminator::make_Unreachable({}));
                builder.set_cur_block(builder.new_bb_unlinked());
            } else {
                // NOTE: This has to be done here because the builder can't easily do it.
                builder.mark_value_assigned(node.span(), res);
            }
            builder.set_result(node.span(), mv$(res));
        }

        void visit(::HIR::ExprNodeCallValue& node) override {
            TRACE_FUNCTION_F("_CallValue " << node.mValue->resType);
            auto _ = save_and_edit(borrowRaiseTarget, nullptr);

            // _CallValue is ONLY valid on function pointers (all others must be desugared)
            ASSERT_BUG(node.span(), node.mValue->resType->is_Function(), "Leftover _CallValue on a non-fn()");
            this->visit_node_ptr(node.mValue);
            if (!builder.block_active()) {
                return;
            }

            // Get the function pointer in a temporary BEFORE getting arguments
            auto fcnVal = builder.new_temporary(node.mValue->resType);
            builder.push_stmt_assign(node.mValue->span(), fcnVal.clone(), builder.getResult(node.mValue->span()));

            auto values = getArgs(node.mArgs);
            if (!builder.block_active()) {
                return;
            }

            auto panic_block = builder.new_bb_unlinked();
            auto next_block = builder.new_bb_unlinked();
            auto res = builder.new_temporary(node.resType);
            builder.endBlock(::MIR::Terminator::make_Call({next_block, ::MIR::UnwindAction::make_Cleanup(panic_block), res.clone(), mv$(fcnVal), mv$(values)}));

            builder.set_cur_block(panic_block);
            emitUnwind(node.span());

            builder.set_cur_block(next_block);
            // TODO: Support diverging value calls
            builder.mark_value_assigned(node.span(), res);
            builder.set_result(node.span(), mv$(res));
        }

        void visit(::HIR::ExprNodeCallMethod& node) override {
            // TODO: Allow use on trait objects? May not be needed, depends.
            BUG(node.span(), "Leftover _CallMethod");
        }

        void visit(::HIR::ExprNodeField& node) override {
            TRACE_FUNCTION_F("_Field \"" << node.field << "\"");
            this->visit_node_ptr(node.mValue);
            auto val = builder.getResultInLvalue(node.mValue->span(), node.mValue->resType);

            const auto& val_ty = node.mValue->resType;

            unsigned int idx;
            if (::std::isdigit(node.field.c_str()[0])) {
                ::std::stringstream(node.field.c_str()) >> idx;
                builder.set_result(node.span(), ::MIR::LValue::newField(mv$(val), idx));
            } else if (const auto* bep = val_ty->as_Path().binding.opt_Struct()) {
                const auto& str = **bep;
                const auto& fields = str.mData.as_Named();
                idx = ::std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                    return x.name == node.field;
                }) - fields.begin();
                builder.set_result(node.span(), ::MIR::LValue::newField(mv$(val), idx));
            } else if (const auto* bep = val_ty->as_Path().binding.opt_Union()) {
                const auto& unm = **bep;
                const auto& fields = unm.mVariants;
                idx = ::std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                    return x.name == node.field;
                }) - fields.begin();

                builder.set_result(node.span(), ::MIR::LValue::newDowncast(mv$(val), idx));
            } else {
                BUG(node.span(), "Field access on non-union/struct - " << val_ty);
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
                            builder.set_result(node.span(), ::MIR::Constant::make_Uint({e.mValue, ity}));
                            break;
                        case ::HIR::CoreType::Char:
                            builder.set_result(node.span(), ::MIR::Constant::make_Uint({e.mValue, ity}));
                            break;
                        case ::HIR::CoreType::I8:
                        case ::HIR::CoreType::I16:
                        case ::HIR::CoreType::I32:
                        case ::HIR::CoreType::I64:
                        case ::HIR::CoreType::I128:
                        case ::HIR::CoreType::Isize:
                            builder.set_result(node.span(), ::MIR::Constant::make_Int({S128(e.mValue), ity}));
                            break;
                        default:
                            BUG(node.span(), "Integer literal with unexpected type - " << node.resType);
                    }
                }
                TU_ARMA(Float, e) {
                    ASSERT_BUG(node.span(), node.resType->is_Primitive(), "Non-primitive return type for Float literal - " << node.resType);
                    auto ity = node.resType->as_Primitive();
                    builder.set_result(node.span(), ::MIR::RValue::make_Constant(::MIR::Constant::make_Float({e.mValue, ity})));
                }
                TU_ARMA(Boolean, e) {
                    builder.set_result(node.span(), ::MIR::RValue::make_Constant(::MIR::Constant::make_Bool({e})));
                }
                TU_ARMA(String, e) {
                    builder.set_result(node.span(), ::MIR::RValue::make_Constant(::MIR::Constant(e)));
                }
                TU_ARMA(CString, e) {
                    auto s = e.v;
                    s.push_back('\0');

                    // Emit as `transmute<&Cstr,&str>`
                    auto res = builder.new_temporary(node.resType);

                    auto castPanic = builder.new_bb_unlinked();
                    auto castOk = builder.new_bb_unlinked();
                    ::HIR::PathParams transmute_params;
                    transmute_params.types.push_back(node.resType);
                    transmute_params.types.push_back(builder.resolve().crate.types.borrow(::HIR::BorrowType::Shared, builder.resolve().crate.types.primitive(::HIR::CoreType::Str)));
                    builder.endBlock(::MIR::Terminator::make_Call({castOk, ::MIR::UnwindAction::make_Cleanup(castPanic), res.clone(), ::MIR::CallTarget::make_Intrinsic({"transmute", mv$(transmute_params)}), make_vec1(::MIR::Param(::MIR::Constant(std::move(s))))}));
                    builder.set_cur_block(castPanic);
                    emitUnwind(node.span());
                    builder.set_cur_block(castOk);

                    builder.set_result(node.span(), mv$(res));
                }
                TU_ARMA(ByteString, e) {
                    auto v = mv$(*reinterpret_cast<::std::vector<uint8_t>*>(&e));
                    builder.set_result(node.span(), ::MIR::RValue::make_Constant(::MIR::Constant(mv$(v))));
                }
            }
        }

        void visit(::HIR::ExprNodeUnitVariant& node) override {
            const Span& sp = node.span();
            TRACE_FUNCTION_F("_UnitVariant");
            if (!node.isStruct) {
                // Get the variant index from the enum.
                auto enumPath = node.mPath.clone();
                auto var_name = enumPath.mPath.pop_component();

                const auto& enm = builder.crate().getEnumByPath(sp, enumPath.mPath);

                auto idx = enm.findVariant(var_name);
                ASSERT_BUG(sp, idx != SIZE_MAX, "Variant " << node.mPath.mPath << " isn't present");

                // VALIDATION
                if (const auto* e = enm.mData.opt_Data()) {
                    const auto& var = (*e)[idx];
                    ASSERT_BUG(sp, !var.is_struct, "Variant " << node.mPath.mPath << " isn't a unit variant");
                }

                builder.set_result(node.span(), ::MIR::RValue::make_EnumVariant({mv$(enumPath), static_cast<unsigned>(idx), {}}));
            } else {
                builder.set_result(node.span(), ::MIR::RValue::make_Struct({node.mPath.clone(), {}}));
            }
        }

        void visit(::HIR::ExprNodePathValue& node) override {
            const auto& sp = node.span();
            TRACE_FUNCTION_F("_PathValue - " << node.mPath);
            if (node.resType->is_NamedFunction() && node.target != ::HIR::ExprNodePathValue::STATIC && node.target != ::HIR::ExprNodePathValue::CONSTANT) {
                auto tmp = builder.new_temporary(node.resType);
                builder.push_stmt_assign(sp, tmp.clone(), ::MIR::Constant::make_Function({box$(node.mPath.clone())}));
                //m_builder.push_stmt_assign( sp, tmp.clone(), ::MIR::Constant::make_ItemAddr({ box$(node.m_path.clone()) }) );
                builder.set_result(sp, mv$(tmp));
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
                            auto ty = MonomorphStatePtr(builder.resolve().crate.types, nullptr, nullptr, &pe.mParams).monomorph_type(sp, e.mType);
                            auto tmp = builder.new_temporary(ty);
                            builder.push_stmt_assign(sp, tmp.clone(), ::MIR::Constant::make_Const({box$(node.mPath.clone())}));
                            builder.set_result(node.span(), mv$(tmp));
                        }
                        TU_ARMA(Static, e) {
                            builder.set_result(node.span(), ::MIR::LValue::newStatic(node.mPath.clone()));
                        }
                        TU_ARMA(StructConstant, e) {
                            // TODO: Why is this still a PathValue?
                            builder.set_result(node.span(), ::MIR::RValue::make_Struct({pe.clone(), {}}));
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
                    TU_MATCHA((it->second), (e), (Constant, builder.set_result(sp, ::MIR::Constant::make_Const({box$(node.mPath.clone())}));), (Static, TODO(sp, "Associated statics (non-rustc) - " << node.mPath);), (Function, BUG(node.span(), "Should have produced a NamedFunction type and have been handled above");))
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
                                builder.set_result(sp, ::MIR::Constant::make_ItemAddr({box$(node.mPath.clone())}));
                                return true;
                            }
                        }
                        // Associated consts
                        {
                            auto it = impl.constants.find(pe.item);
                            if (it != impl.constants.end()) {
                                builder.set_result(sp, ::MIR::Constant::make_Const({box$(node.mPath.clone())}));
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
                        builder.set_result(node.span(), a->second.clone());
                        break;
                    case ::HIR::PatternBinding::Type::Ref:
                        builder.set_result(node.span(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, a->second.clone()}));
                        break;
                    case ::HIR::PatternBinding::Type::MutRef:
                        builder.set_result(node.span(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Unique, false, a->second.clone()}));
                        break;
                }
                return;
            }
#endif
            builder.set_result(node.span(), builder.getVariable(node.span(), node.slot));
        }

        void visit(::HIR::ExprNodeConstParam& node) override {
            TRACE_FUNCTION_F("_ConstParam - " << node.mName << " #" << node.mBinding);
            builder.set_result(node.span(), ::MIR::Constant::make_Generic({node.mName, node.mBinding}));
        }

        void visit_sl_inner(::HIR::ExprNodeStructLiteral& node, const ::HIR::Struct& str, const ::HIR::GenericPath& path) {
            const Span& sp = node.span();

            ASSERT_BUG(sp, str.mData.is_Named(), "");
            const ::HIR::t_struct_fields& fields = str.mData.as_Named();

            ::std::vector<bool> values_set;
            ::std::vector<::MIR::Param> values;
            values.resize(fields.size());
            values_set.resize(fields.size());

            for (auto& ent : node.values) {
                auto& valnode = ent.second;
                auto idx = ::std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                    return x.name == ent.first;
                }) - fields.begin();
                assert(!values_set[idx]);
                values_set[idx] = true;
                DEBUG("_StructLiteral - fld '" << ent.first << "' (idx " << idx << ")");
                this->visit_node_ptr(valnode);
                if (!builder.block_active()) {
                    return;
                }

                auto res = builder.getResult(valnode->span());
                if (auto* e = res.opt_Constant()) {
                    values.at(idx) = mv$(*e);
                } else {
                    // NOTE: Have to allocate a new temporary because ordering matters
                    auto tmp = builder.new_temporary(valnode->resType);
                    builder.push_stmt_assign(valnode->span(), tmp.clone(), mv$(res));
                    values.at(idx) = mv$(tmp);
                }
            }

            auto baseVal = ::MIR::LValue::newReturn();
            if (node.baseValue) {
                DEBUG("_StructLiteral - base");
                this->visit_node_ptr(node.baseValue);
                if (!builder.block_active()) {
                    return;
                }
                baseVal = builder.getResultInLvalue(node.baseValue->span(), node.baseValue->resType);
            }
            for (unsigned int i = 0; i < values.size(); i++) {
                if (!values_set[i]) {
                    if (node.baseValue) {
                        values[i] = ::MIR::LValue::newField(baseVal.clone(), i);
                    } else if (fields[i].default_value) {
                        const auto& v = *fields[i].default_value;
                        auto ms = MonomorphStatePtr(builder.resolve().crate.types, nullptr, &path.mParams, nullptr);
                        values[i] = builder.lvalue_or_temp(sp, ms.monomorph_type(sp, fields[i].ty), MIR::Constant::make_Const({::std::make_unique<HIR::Path>(ms.monomorph_genericpath(sp, v))}));
                    } else {
                        ERROR(node.span(), E0000, "Field '" << fields[i].name << "' not specified");
                    }
                } else {
                    // Partial move support will handle dropping the rest?
                }
            }

            builder.set_result(node.span(), ::MIR::RValue::make_Struct({path.clone(), mv$(values)}));
        }

        void visit(::HIR::ExprNodeStructLiteral& node) override {
            TRACE_FUNCTION_F("_StructLiteral");

            const auto& ty_path = node.realPath;

            TU_MATCH_HDRA( (node.resType->as_Path().binding), {)
            TU_ARMA(Unbound, _e) {
                }
                TU_ARMA(Opaque, _e) {
                }
                TU_ARMA(Enum, e) {
                    auto enumPath = ty_path.clone();
                    auto var_name = enumPath.mPath.pop_component();

                    const auto& enm = *e;
                    size_t idx = enm.findVariant(var_name);
                    ASSERT_BUG(node.span(), idx != SIZE_MAX, "");
                    ASSERT_BUG(node.span(), enm.mData.is_Data(), "");
                    const auto& var_ty = enm.mData.as_Data()[idx].type;
                    const auto& str = *var_ty->as_Path().binding.as_Struct();

                    // Take advantage of the identical generics to cheaply clone/monomorph the path.
                    ::HIR::GenericPath struct_path = ty_path.clone();
                    struct_path.mPath = var_ty->as_Path().path.mData.as_Generic().mPath;

                    this->visit_sl_inner(node, str, struct_path);
                    if (!builder.block_active()) {
                        return;
                    }
                    auto vals = std::move(builder.getResult(node.span()).as_Struct().vals);

                    // And create Variant
                    builder.set_result(node.span(), ::MIR::RValue::make_EnumVariant({mv$(enumPath), static_cast<unsigned>(idx), mv$(vals)}));
                }
                TU_ARMA(Union, e) {
                    const auto& variant_name = node.values.front().first;
                    auto& value_node = node.values.front().second;
                    this->visit_node_ptr(value_node);
                    if (!builder.block_active()) {
                        return;
                    }
                    auto val = builder.getResultInLvalue(value_node->span(), value_node->resType);

                    const auto& unm = *e;
                    auto it = ::std::find_if(unm.mVariants.begin(), unm.mVariants.end(), [&](const HIR::StructField& v) -> auto {
                        return v.name == variant_name;
                    });
                    assert(it != unm.mVariants.end());
                    unsigned int idx = it - unm.mVariants.begin();

                    builder.set_result(node.span(), ::MIR::RValue::make_UnionVariant({node.realPath.clone(), idx, mv$(val)}));
                }
                TU_ARMA(ExternType, e) {
                    BUG(node.span(), "_StructLiteral ExternType isn't valid?");
                }
                TU_ARMA(Struct, e) {
                    if (e->mData.is_Unit()) {
                        builder.set_result(node.span(), ::MIR::RValue::make_Struct({ty_path.clone(), {}}));
                        return;
                    }

                    this->visit_sl_inner(node, *e, ty_path);
                }
            }
        }

        void visit(::HIR::ExprNodeTuple& node) override {
            TRACE_FUNCTION_F("_Tuple");
            auto values = getArgs(node.vals);
            if (!builder.block_active()) {
                return;
            }

            builder.set_result(node.span(), ::MIR::RValue::make_Tuple({mv$(values)}));
        }

        void visit(::HIR::ExprNodeArrayList& node) override {
            TRACE_FUNCTION_F("_ArrayList");
            auto values = getArgs(node.vals);
            if (!builder.block_active()) {
                return;
            }

            builder.set_result(node.span(), ::MIR::RValue::make_Array({mv$(values)}));
        }

        void visit(::HIR::ExprNodeArraySized& node) override {
            TRACE_FUNCTION_F("_ArraySized");
            this->visit_node_ptr(node.val);
            if (!builder.block_active()) {
                return;
            }
            auto value = builder.getResultInParam(node.span(), node.val->resType);

            builder.set_result(node.span(), ::MIR::RValue::make_SizedArray({mv$(value), std::move(node.mSize)}));
            // Ensure that the size is valid (avoids crashes when debug is enabled)
            node.mSize = HIR::ArraySize();
        }

        void visit(::HIR::ExprNodeClosure& node) override {
            TRACE_FUNCTION_F("_Closure - " << node.objPath);
            auto _ = save_and_edit(borrowRaiseTarget, nullptr);

            ::std::vector<::MIR::Param> vals;
            vals.reserve(node.captures.size());
            for (auto& arg : node.captures) {
                this->visit_node_ptr(arg);
                vals.push_back(builder.getResultInLvalue(arg->span(), arg->resType));
            }

            builder.set_result(node.span(), ::MIR::RValue::make_Struct({node.objPath.clone(), mv$(vals)}));
        }

        void visit_common_cr(const Span& sp, const HIR::GenericPath& obj_path, const HIR::TypeData* state_type, ::std::vector<::HIR::ExprNodeP>& captures) {
            auto _ = save_and_edit(borrowRaiseTarget, nullptr);

            ::std::vector<::MIR::Param> vals;
            vals.reserve(1 + captures.size());

            // Zero the state index
            {
                const auto& langMaybeUninit = builder.resolve().crate.getLangItemPath(sp, "maybe_uninit");
                const auto& unmMaybeUninit = builder.resolve().crate.getUnionByPath(sp, langMaybeUninit);
                auto slot_type = builder.resolve().crate.types.path(::HIR::GenericPath(langMaybeUninit, ::HIR::PathParams(state_type)), &unmMaybeUninit);

                auto res_slot = builder.new_temporary(slot_type);
                auto sizePanic = builder.new_bb_unlinked();
                auto sizeOk = builder.new_bb_unlinked();
                builder.endBlock(
                    ::MIR::Terminator::make_Call(
                        {sizeOk,
                         ::MIR::UnwindAction::make_Cleanup(sizePanic),
                         res_slot.clone(),
                         ::MIR::CallTarget::make_Intrinsic({"init", ::HIR::PathParams(mv$(slot_type))}), // I.e. `mem::zeroed`
                         {}}
                    )
                );
                builder.set_cur_block(sizePanic);
                emitUnwind(sp);
                builder.set_cur_block(sizeOk);
                vals.push_back(std::move(res_slot));
            }
            // Populate the rest
            for (auto& arg : captures) {
                this->visit_node_ptr(arg);
                vals.push_back(builder.getResultInLvalue(arg->span(), arg->resType));
            }

            builder.set_result(sp, ::MIR::RValue::make_Struct({obj_path.clone(), mv$(vals)}));
        }

        void visit(::HIR::ExprNodeGenerator& node) override {
            TRACE_FUNCTION_F("_Generator - " << node.objPath);
            ASSERT_BUG(node.span(), node.objPtr, "Generator not created");
            ASSERT_BUG(node.span(), !node.mCode, "Encountered outer generator wrapper");

            visit_common_cr(node.span(), node.objPath, node.stateDataType, node.captures);
        }

        void visit(::HIR::ExprNodeGeneratorWrapper& node) override {
            BUG(node.span(), "Unexpected");
        }

        void visit(::HIR::ExprNodeAsyncBlock& node) override {
            TRACE_FUNCTION_F("_AsyncBlock - " << node.objPath);
            ASSERT_BUG(node.span(), node.objPtr, "Future not created");
            ASSERT_BUG(node.span(), !node.mCode, "Encountered code inside post-expand async block");

            visit_common_cr(node.span(), node.objPath, node.stateDataType, node.captures);
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

        ::HIR::ExprNode& root_node = const_cast<::HIR::ExprNode&>(*ptr);
        MirBuilder builder{ptr->span(), resolve, ret_ty, args, fcn};
        ExprVisitorConv ev{builder, ptr.mBindings, cast<::HIR::ExprNodeGeneratorWrapper>(&root_node)};

        // 1. Apply destructuring to arguments
        unsigned int i = 0;
        for (const auto& arg : args) {
            const auto& pat = arg.first;
            builder.schedule_argument_drop(i);
            // If the binding is set (i.e. this isn't destructuring) then the table populated by `MirBuilder::MirBuilder(...)` will be used
            if (pat.mBindings.size() == 1 && pat.mBindings[0].mType == ::HIR::PatternBinding::Type::Move && pat.mData.is_Any()) {
                // Simple `var: Type` arguments are handled by `MirBuilder.m_var_arg_mappings`
            } else {
                DEBUG("Argument a" << i << " - " << pat);
                ev.schedule_pattern_drops(ptr->span(), arg.first, PatternDropOrder::FirstCandidate);
                MIRLowerHIRLet(builder, ev, ptr->span(), arg.first, ::MIR::LValue::newArgument(i), /*else_node=*/nullptr);
            }
            i++;
        }

        // 2. Destructure code
        if (auto* genNode = cast<::HIR::ExprNodeGeneratorWrapper>(&root_node)) {
            // Mark all capture locals as valid (for later rewrite into variable acesses)
            ::std::map<unsigned, std::vector<MIR::LValue::Wrapper>> mappings;
            for (size_t i = 0; i < genNode->captureUsages.size(); i++) {
                unsigned idx = args.size() + i;
                builder.schedule_variable_drop(idx);
                switch (genNode->captureUsages[i]) {
                    case ::HIR::ValueUsage::Borrow:
                    case ::HIR::ValueUsage::Mutate: {
                        // TODO: Use `m_variable_aliases` for by-borrow captures, to avoid them being dropped
                        auto lv = ::MIR::LValue::newArgument(0);
                        lv.wrappers.push_back(::MIR::LValue::Wrapper::newField(0)); // Pin.ptr
                        lv.wrappers.push_back(::MIR::LValue::Wrapper::newDeref());  // *
                        lv.wrappers.push_back(::MIR::LValue::Wrapper::newField(1 + i));
                        lv.wrappers.push_back(::MIR::LValue::Wrapper::newDeref());
                        builder.addVariableAlias(root_node.span(), idx, ::HIR::PatternBinding::Type::Move, std::move(lv));
                    } break;
                    case ::HIR::ValueUsage::Move:
                    case ::HIR::ValueUsage::Unknown:
                        builder.mark_value_assigned(root_node.span(), ::MIR::LValue::newLocal(idx));
                        mappings.insert(std::make_pair(idx, ::make_vec1(::MIR::LValue::Wrapper::newField(1 + i))));
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
            auto& state_ty = const_cast<HIR::Struct&>(*genNode->stateDataType->as_Path().binding.as_Struct());
            unsigned value_var_idx;
            {
                const auto& unmMaybeUninit = resolve.crate.getUnionByPath(sp, resolve.crate.getLangItemPath(genNode->span(), "maybe_uninit"));
                value_var_idx = std::find_if(unmMaybeUninit.mVariants.begin(), unmMaybeUninit.mVariants.end(), [&](const auto& e) {
                    return e.name == "value";
                }) - unmMaybeUninit.mVariants.begin();
            }
            ASSERT_BUG(sp, value_var_idx == 1, "Assumption on MaybeUninit.value's variant index failed");
            // - Any variables that are saved twice need to have a static address, others can share?
            // - Lazy option (doesn't require making sub-types): Toss everything together
            auto& fields = state_ty.mData.as_Tuple();
            for (auto idx : saved) {
                if (idx < 1 + genNode->captureUsages.size()) {
                } else {
                    auto fieldIdx = fields.size();
                    ASSERT_BUG(sp, idx < fcn.locals.size(), idx << " >= " << fcn.locals.size());
                    fields.push_back(::HIR::VisEnt<HIR::TypeRef>{HIR::Publicity::new_none(), fcn.locals.at(idx)});
                    // self.state(0).value(?#1).value(?0).IDX
                    mappings.insert(
                        std::make_pair(
                            idx,
                            std::vector<MIR::LValue::Wrapper>{
                                ::MIR::LValue::Wrapper::newField(0),
                                ::MIR::LValue::Wrapper::newDowncast(value_var_idx), // MaybeUninit.value
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
            fields.push_back(::HIR::VisEnt<HIR::TypeRef>{HIR::Publicity::new_none(), resolve.crate.types.array(resolve.crate.types.primitive(::HIR::CoreType::U8), (drop_flag_mapping.size() + 7) / 8)});

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
                unsigned stmt_idx = 0;

            public:
                Rewriter(const ::std::map<unsigned, std::vector<MIR::LValue::Wrapper>>& mappings, const ::std::map<unsigned, unsigned>& drop_flag_mapping, unsigned drop_flags_field)
                    : mMappings(mappings)
                    , dropFlagMapping(drop_flag_mapping)
                    , dropFlagsField(drop_flags_field)
                {
                }

                bool visit_lvalue(::MIR::LValue& lv, ::MIR::visit::ValUsage u) override {
                    if (lv.root.is_Local()) {
                        auto it = mMappings.find(lv.root.as_Local());
                        if (it != mMappings.end()) {
                            lv.root = ::MIR::LValue::Storage::newArgument(0);
                            auto dit = lv.wrappers.begin();
                            dit = lv.wrappers.insert(dit, ::MIR::LValue::Wrapper::newField(0)) + 1; // Pin.ptr
                            dit = lv.wrappers.insert(dit, ::MIR::LValue::Wrapper::newDeref()) + 1;  // *
                            dit = lv.wrappers.insert(dit, it->second.begin(), it->second.end()) + 1;
                            DEBUG("BB" << bbIdx << "/" << FMT_CB(os, if (stmt_idx == ~0u) { os << "TERM"; } else { os << stmt_idx; }) << " > " << lv);
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

                bool visit_stmt(::MIR::Statement& stmt) override {
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
                    return ::MIR::visit::VisitorMut::visit_stmt(stmt);
                }

                void push_statements(::MIR::BasicBlock& bb, size_t& ofs) {
                    for (auto& e : newStatements) {
                        bb.statements.insert(bb.statements.begin() + ofs, std::move(e));
                        ofs += 1;
                    }
                    newStatements.clear();
                }

                void rewrite_fcn(::MIR::Function& f) {
                    for (auto& bb : f.blocks) {
                        this->bbIdx = &bb - f.blocks.data();
                        for (size_t stmt_idx = 0; stmt_idx < bb.statements.size(); stmt_idx++) {
                            this->stmt_idx = stmt_idx;
                            this->visit_stmt(bb.statements[stmt_idx]);
                            this->push_statements(bb, stmt_idx);
                        }
                        this->stmt_idx = ~0u;
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
                        this->visit_terminator(bb.terminator);
                        size_t stmt_idx = bb.statements.size();
                        this->push_statements(bb, stmt_idx);
                    }
                }
            };

            Rewriter(mappings, drop_flag_mapping, dropFlagsFieldIdx).rewrite_fcn(fcn);

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
            root_node.visit(ev);
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

void HIRGenerateMIRExpr(const ::HIR::Crate& crate, const ::HIR::ItemPath& path, ::HIR::ExprPtr& expr_ptr, const ::HIR::Function::argsT& args, const ::HIR::TypeData* res_ty) {
    if (!expr_ptr.mir) {
        TRACE_FUNCTION;
        StaticTraitResolve resolve{crate};
        resolve.set_both_generics_raw(expr_ptr.state->implGenerics, expr_ptr.state->itemGenerics);
        expr_ptr.set_mir(LowerMIR(resolve, path, expr_ptr, res_ty, args));
        // Run cleanup to simplify consteval?
        // - This ends up running before things like vtable generation, so parts of cleanup won't work.
        //MIR_Cleanup(resolve, path, *expr_ptr.m_mir, args, res_ty);
        // This path prepares an on-demand body for the constant evaluator, not
        // the runtime MIR selected by the driver. Keep normal inlining disabled,
        // but retain the local simplification that CTFE historically required.
        MIROptimise(resolve, path, *expr_ptr.mir, args, res_ty, /*opt_level=*/2, /*do_inline=*/false);
    }
}

void HIRGenerateMIR(::HIR::Crate& crate) {
    ::MIR::OuterVisitor ov{crate, [&](const auto& res, const auto& p, ::HIR::ExprPtr& expr_ptr, const auto& args, const auto& ty) {
        if (!expr_ptr.getMirOpt()) {
            expr_ptr.set_mir(LowerMIR(res, p, expr_ptr, ty, args));
        }
    }};
    ov.visit_crate(crate);
}


void MIRLowerHIRMatch(MirBuilder& builder, MirConverter& conv, ::HIR::ExprNodeMatch& node, ::MIR::LValue match_val, const std::vector<unsigned>& let_else_initializer_temps);

namespace {
    void getTyAndVal(
        const Span& sp,
        MirBuilder& builder,
        const ::HIR::TypeData* top_ty,
        const ::MIR::LValue& top_val,
        const fieldPathT& field_path,
        unsigned int field_path_ofs,
        /*Out ->*/ ::HIR::TypeRef& out_ty,
        ::MIR::LValue& out_val
    );
}

void MIRLowerHIRGetTypeValueForPath(
    const Span& sp,
    MirBuilder& builder,
    const ::HIR::TypeData* top_ty,
    const ::MIR::LValue& top_val,
    const fieldPathT& field_path,
    /*Out ->*/ ::HIR::TypeRef& out_ty,
    ::MIR::LValue& out_val
) {
    getTyAndVal(sp, builder, top_ty, top_val, field_path, 0, out_ty, out_val);
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
             ::std::vector<PatternRule> sub_rules;
         }),
        // Slice (includes desired length)
        (Slice,
         struct {
             unsigned int len;
             ::std::vector<PatternRule> sub_rules;
         }),
        // SplitSlice
        // TODO: How can the negative offsets in the `trailing` be handled correctly? (both here and in the destructure)
        (SplitSlice,
         struct {
             unsigned int min_len;
             unsigned int trailing_len;
             ::std::vector<PatternRule> leading, trailing;
         }),
        // Boolean (different to Constant because of how restricted it is)
        (Bool, bool),
        // General value
        (Value, ::MIR::Constant),
        (ValueRange,
         struct {
             ::MIR::Constant first, last;
             bool is_inclusive;
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

    static ::Ordering rule_is_before(const PatternRule& l, const PatternRule& r);

    bool is_before(const PatternRuleset& other) const;
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

typedef ::std::vector<PatternRuleset> t_arm_rules;

void MIRLowerHIRMatchSimple(MirBuilder& builder, MirConverter& conv, ::HIR::ExprNodeMatch& node, ::MIR::LValue match_val, t_arm_rules armRules, ::std::vector<ArmCode> armCode, ::MIR::BasicBlockId firstCmpBlock);
int MIRLowerHIRMatchSimpleGeneratePattern(MirBuilder& builder, const Span& sp, const PatternRule* rules, unsigned int num_rules, const ::HIR::TypeData* top_ty, const ::MIR::LValue& top_val, unsigned int field_path_ofs, ::MIR::BasicBlockId failBb);
void MIRLowerHIRMatchGrouped(MirBuilder& builder, MirConverter& conv, const Span& sp, const HIR::TypeData* match_ty, ::MIR::LValue match_val, t_arm_rules armRules, ::std::vector<ArmCode> arms_code, ::MIR::BasicBlockId firstCmpBlock);
void MIRLowerHIRMatchDecisionTree(MirBuilder& builder, MirConverter& conv, ::HIR::ExprNodeMatch& node, ::MIR::LValue match_val, t_arm_rules armRules, ::std::vector<ArmCode> armCode, ::MIR::BasicBlockId firstCmpBlock);

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
    size_t subset_start, subset_end;

    fieldPathT fieldPath;

    PatternRulesetBuilder(const StaticTraitResolve& resolve)
        : mResolve(resolve)
        , rulesets(1)
        , subset_start(0)
        , subset_end(1)
    {
        if (resolve.crate.mLangItems.count("owned_box") > 0) {
            mLangBox = &resolve.crate.mLangItems.at("owned_box");
        }
    }

    void appendFromLit(const Span& sp, EncodedLiteralSlice lit, const ::HIR::TypeData* ty);
    void appendFrom(const Span& sp, const ::HIR::Pattern& pat, const ::HIR::TypeData* ty);

private:
    void push_rule(PatternRule r);
    void push_binding(PatternBinding b);
    void push_bindings(std::vector<PatternBinding> b);
    void set_impossible();

    void multiply_rulesets(size_t n, std::function<void(size_t idx)> cb);
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
            const auto& parent_rule = (*parent)[i][parentOfs];
            if (const auto* re = parent_rule.opt_Variant()) {
                return re->sub_rules;
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

void sort_rulesets(RulesetRef rulesets, size_t idx = 0);
void sort_rulesets_inner(RulesetRef rulesets, size_t idx);

// --------------------------------------------------------------------
// CODE
// --------------------------------------------------------------------
/// `let` (also used for destructuring arguments) - Introduces arguments into the current scope
///
/// If `else_node` is non-null, a `_` "arm" is added to invoke that block (which must diverge)
void MIRLowerHIRLet(MirBuilder& builder, MirConverter& conv, const Span& sp, const ::HIR::Pattern& pat, ::MIR::LValue val, const ::HIR::ExprNode* else_node) {
    TRACE_FUNCTION;

    HIR::TypeRef outer_ty;
    builder.with_val_type(sp, val, [&](const HIR::TypeData* ty) {
        outer_ty = ty;
    });

    auto success_node = builder.new_bb_unlinked();
    auto firstCmpBlock = builder.pause_cur_block();

    // - Convert HIR pattern into ruleset
    std::vector<PatternRuleset> armRules;
    std::vector<ArmCode> armCode;

    auto pat_scope = builder.new_scope_split(sp);

    auto pat_builder = PatternRulesetBuilder{builder.resolve()};
    pat_builder.appendFrom(sp, pat, outer_ty);
    for (auto& sr : pat_builder.rulesets) {
        auto pat_idx = static_cast<unsigned>(&sr - &pat_builder.rulesets.front());
        if (sr.isImpossible) {
            DEBUG("LET PAT #" << pat_idx << " " << pat << " ==> IMPOSSIBLE [" << sr.rules << "]");
        } else {
            DEBUG("LET PAT #" << pat_idx << " " << pat << " ==> [" << sr.rules << "]");
            armRules.push_back(PatternRuleset{pat_idx, 0, mv$(sr.rules), mv$(sr.mBindings)});

            auto pat_node = builder.new_bb_unlinked();
            builder.set_cur_block(pat_node);
            conv.destructureFromList(sp, outer_ty, val.clone(), armRules.back().mBindings);
            builder.endSplitArm(sp, pat_scope, /*reachable=*/true);
            builder.endBlock(MIR::Terminator::make_Goto(success_node));

            ArmCode::Pattern ap;
            ap.entry = pat_node;
            ArmCode ac;
            ac.rules.push_back(ap);
            armCode.push_back(ac);
        }
    }
    builder.terminate_scope(sp, mv$(pat_scope));
    if (else_node) {
        // Emit a check (similar to match)
        // NOTE: This is handled by "HIR Lower" currently, seems to work well
        TODO(sp, "Handle let-else");
    }

    MIRLowerHIRMatchGrouped(builder, conv, sp, outer_ty, mv$(val), mv$(armRules), mv$(armCode), firstCmpBlock);

    builder.set_cur_block(success_node);
}

// Handles lowering non-trivial matches to MIR
// - Non-trivial means that there's more than one pattern
// - Trivial matches are handled using `MIR_LowerHIR_Let`
void MIRLowerHIRMatch(MirBuilder& builder, MirConverter& conv, ::HIR::ExprNodeMatch& node, ::MIR::LValue match_val, const std::vector<unsigned>& let_else_initializer_temps) {
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

    const auto& match_ty = node.mValue->resType;
    auto result_val = builder.new_temporary(node.resType);
    auto next_block = builder.new_bb_unlinked();

    /// Top level scope for the match
    auto match_scope = builder.new_scope_loop(node.span());

    // 1. Stop the current block so we can generate code before generating the pattern matching code
    auto firstCmpBlock = builder.pause_cur_block();

    /// Entries for each arm, containing the code to run for each
    ::std::vector<ArmCode> armCode;
    /// Final list of rules (flattened patterns), for all patterns
    t_arm_rules armRules;

    // For each arm, generate the contents of the logical `if pattern_matches { if guard { break body; } }`
    for (unsigned int armIdx = 0; armIdx < node.arms.size(); armIdx++) {
        TRACE_FUNCTION_FR("ARM " << armIdx, "ARM " << armIdx);
        /*const*/ auto& arm = node.arms[armIdx];
        const Span& sp = arm.mCode->span();

        // ---
        // Convert all patterns on this arm into flattened "rules"
        // ---
        auto firstArmRuleIdx = armRules.size();
        for (unsigned int pat_idx = 0; pat_idx < arm.patterns.size(); pat_idx++) {
            const auto& pat = arm.patterns[pat_idx];

            auto pat_builder = PatternRulesetBuilder{builder.resolve()};
            pat_builder.appendFrom(node.span(), pat, match_ty);
            size_t firstRule = armRules.size();
            for (auto& sr : pat_builder.rulesets) {
                size_t i = &sr - &pat_builder.rulesets.front();
                if (sr.isImpossible) {
                    DEBUG("ARM PAT (" << armIdx << "," << pat_idx << " #" << i << ") " << pat << " ==> IMPOSSIBLE [" << sr.rules << "]");
                } else {
                    DEBUG("ARM PAT (" << armIdx << "," << pat_idx << " #" << i << ") " << pat << " ==> [" << sr.rules << "]");
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
        auto armBodyBlock = builder.new_bb_unlinked();

        /// Block for when the first rule matches (contains the guard and binding setup for this rule)
        auto entryBlockPat0 = builder.new_bb_unlinked();
        builder.set_cur_block(entryBlockPat0);

        // Split scope for the `if pattern_matches { }` outer arm,
        auto pat_scope = builder.new_scope_split(node.span());
        builder.endSplitArm(sp, pat_scope, /*reachable=*/true); // Inject the `else` case first, this should not push any statements

        // Generate code for this arm (guard, destructuring, and body)
        {
            // Scopes present for the body (generated during guard processing)
            // - Temporary/variable scopes, and split scopes
            struct MatchScope {
                ScopeHandle handle;
                bool is_split;
            };

            std::vector<MatchScope> scopes;

            const auto& bindings0 = armRules[firstArmRuleIdx].mBindings;
            // Create aliases for every binding that only allows shared/immutable access (for use in the guard)
            auto aliases = builder.save_aliases();
            std::vector<unsigned> bindingTemps;
            std::vector<unsigned> bindingTempsAlt(bindings0.size(), ~0u);
            for (const auto& b : bindings0) {
                HIR::TypeRef finalTy = conv.getBindingType(sp, b.binding->slot);
                const Span& sp = arm.mCode->span();
                auto val = conv.getValueForBindingPath(sp, match_ty, match_val, b);
                DEBUG("Set alias for: " << *b.binding << " := " << val);
                if (b.binding->mType != ::HIR::PatternBinding::Type::Move) {
                    const auto& borrow = finalTy->as_Borrow();
                    finalTy = builder.resolve().crate.types.borrow(::HIR::BorrowType::Shared, borrow.inner, borrow.lifetime);
                    // Not a move binding, still need to borrow but no deref
                    // - Or, make another temporary for the borrow (no scope needed)
                    auto tmp2 = builder.new_temporary(finalTy);
                    bindingTempsAlt[bindingTemps.size()] = tmp2.as_Local();
                    builder.push_stmt_assign(sp, tmp2.clone(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, std::move(val)}));
                    val = std::move(tmp2);
                }
                // Allocate a temporary to hold a borrow of that type
                auto tmp = builder.new_temporary(builder.resolve().crate.types.borrow(::HIR::BorrowType::Shared, finalTy));
                // - Store the temporary index so later copies can write to it
                bindingTemps.push_back(tmp.as_Local());
                // Assign the temporary with a borrow of the other slot
                builder.push_stmt_assign(sp, tmp.clone(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, std::move(val)}));
                // And set an alias to point to `*temp`
                builder.addVariableAlias(sp, b.binding->slot, ::HIR::PatternBinding::Type::Move, ::MIR::LValue::newDeref(std::move(tmp)));
            }

            // Require that either there's no guards, or that there's only one rule
            // - Otherwise, we can't (currently) prevent use-after-free
            // This is expected to fail at some point, but more testing needed elsewhere
            bool should_freeze = (!arm.guards.empty() && firstArmRuleIdx + 1 < armRules.size());
            scopes.push_back({builder.new_scope_freeze(sp), false});
            if (!should_freeze) {
                builder.unfreeze_scope(sp, scopes.front().handle);
            }

            // Block at the start of the saved guard data
            auto block0 = builder.pause_cur_block();
            builder.set_cur_block(block0);
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
                    auto destructure = builder.new_bb_unlinked();

                    // Make a temp scope and push
                    scopes.push_back({builder.new_scope_temp(c.val->span()), false});
                    conv.visit_node_ptr(c.val);
                    if (!builder.block_active()) {
                        guardDiverged = true;
                        break;
                    }
                    MIR::LValue match_cond_val = builder.getResultInLvalue(c.val->span(), c.val->resType);
                    DEBUG("GUARD " << c.pat << " = " << match_cond_val);

                    // If this is not a pattern-match, terminate the temporary scope here
                    if (c.is_if) {
                        auto t = builder.new_temporary(c.val->resType);
                        builder.push_stmt_assign(c.val->span(), t.clone(), std::move(match_cond_val));
                        match_cond_val = std::move(t);
                        builder.terminate_scope(sp, std::move(scopes.back().handle));
                        scopes.pop_back();
                    }

                    // Generate simplified rules from patterns
                    auto pat_builder = PatternRulesetBuilder{builder.resolve()};
                    pat_builder.appendFrom(node.span(), c.pat, c.val->resType);

                    /// Block for when a pattern fails to match
                    auto local_false = builder.new_bb_unlinked();
                    bool local_false_used = false;
                    // OR'd patterns
                    ::std::vector<std::pair<MIR::BasicBlockId, const PatternRulesetBuilder::Ruleset*>> ends;
                    for (auto& sr : pat_builder.rulesets) {
                        if (sr.isImpossible) {
                            // The rule is impossible, so don't visit
                        } else {
                            if (local_false_used) {
                                local_false = builder.new_bb_unlinked();
                            }

                            ASSERT_BUG(c.val->span(), builder.block_active(), "Block not active");
                            MIRLowerHIRMatchSimpleGeneratePattern(builder, c.val->span(), sr.rules.data(), sr.rules.size(), c.val->resType, match_cond_val, 0, local_false);
                            ends.push_back(std::make_pair(builder.pause_cur_block(), &sr));
                            builder.set_cur_block(local_false);
                            local_false_used = true;
                        }
                    }
                    if (!local_false_used) {
                        // None of the patterns were possible?
                        TODO(sp, "No possible arms in a `if-let` guard?");
                    }
                    if (condFalseBlockPat0 == ~0u) {
                        condFalseBlockPat0 = builder.new_bb_unlinked();
                    }
                    // Split scope for the body of this logical `if`
                    scopes.push_back({builder.new_scope_split(sp), true});
                    builder.endSplitArm(sp, scopes.back().handle, true);
                    // Currently in `local_false`
                    DEBUG("GUARD: Clean up and jump to `cond_false`");
                    // End the top scope early, which also handles ending all intervening scopes
                    builder.terminate_scope_early(sp, scopes.front().handle);
                    // Indicate an exit point to the split
                    builder.endSplitArm(arm.mCode->span(), pat_scope, /*reachable*/ true, /*early*/ true);
                    builder.endBlock(::MIR::Terminator::make_Goto(condFalseBlockPat0));

                    // Introduce a local variable scope for the new bindings
                    scopes.push_back({builder.new_scope_var(c.val->span()), false});
                    conv.schedule_pattern_drops(c.val->span(), c.pat, PatternDropOrder::FirstCandidate);

                    // Only introduce the new bindings (with `destructure_from_list`) after handling the early-exit case
                    // - This stops the `terminate_scope_early` from dropping too eagerly
                    for (const auto& e : ends) {
                        builder.set_cur_block(e.first);
                        conv.destructureFromList(arm.mCode->span(), c.val->resType, match_cond_val.clone(), e.second->mBindings, /*update_states=*/&e == ends.data());
                        builder.endBlock(::MIR::Terminator::make_Goto(destructure));
                    }

                    ASSERT_BUG(node.span(), !builder.block_active(), "Block still active?");
                    builder.set_cur_block(destructure);
                }
            }
            if (guardDiverged) {
                if (should_freeze) {
                    builder.unfreeze_scope(sp, scopes.front().handle);
                }
                builder.restore_aliases(std::move(aliases));
                auto guardCode = builder.codeSaveEnd(std::move(csH));

                while (!scopes.empty()) {
                    builder.terminate_scope(arm.mCode->span(), std::move(scopes.back().handle), false);
                    scopes.pop_back();
                }
                builder.endSplitArm(arm.mCode->span(), pat_scope, /*reachable=*/false);
                builder.terminate_scope(sp, std::move(pat_scope), false);
                builder.terminate_scope_early(sp, match_scope);

                ac.rules.push_back(ArmCode::Pattern{entryBlockPat0, ~0u});
                for (size_t i = firstArmRuleIdx + 1; i < armRules.size(); i++) {
                    struct DivergingGuardMapper: public MirBuilder::CloneMapper {
                        MIR::BasicBlockId block0;

                        DivergingGuardMapper(MIR::BasicBlockId block0)
                            : block0(block0)
                        {
                        }

                        MIR::BasicBlockId update_bb_ref(MIR::BasicBlockId bbIdx) override {
                            if (bbIdx < block0) {
                                return bbIdx;
                            }
                            BUG(Span(), "Diverging guard referenced unsaved block bb" << bbIdx << " after bb" << block0);
                        }
                    } mapper(block0);

                    auto entryBlock = builder.new_bb_unlinked();
                    builder.set_cur_block(entryBlock);
                    ASSERT_BUG(sp, bindingTemps.size() == armRules[i].mBindings.size(), "Mismatched guard bindings");
                    for (size_t j = 0; j < bindingTemps.size(); j++) {
                        const auto& b = armRules[i].mBindings[j];
                        auto val = conv.getValueForBindingPath(sp, match_ty, match_val, b);
                        if (b.binding->mType != ::HIR::PatternBinding::Type::Move) {
                            MIR::LValue tmp2;
                            if (bindingTempsAlt[j] == ~0u) {
                                auto finalTy = conv.getBindingType(sp, b.binding->slot);
                                const auto& borrow = finalTy->as_Borrow();
                                finalTy = builder.resolve().crate.types.borrow(::HIR::BorrowType::Shared, borrow.inner, borrow.lifetime);
                                tmp2 = builder.new_temporary(finalTy);
                                bindingTempsAlt[j] = tmp2.as_Local();
                            } else {
                                tmp2 = ::MIR::LValue::newLocal(bindingTempsAlt[j]);
                            }
                            builder.push_stmt_assign(sp, tmp2.clone(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, std::move(val)}));
                            val = std::move(tmp2);
                        }
                        builder.push_stmt_assign(sp, ::MIR::LValue::newLocal(bindingTemps[j]), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, std::move(val)}));
                    }
                    builder.insert_cloned(sp, guardCode, mapper);
                    ASSERT_BUG(sp, !builder.block_active(), "Diverging guard clone remained reachable");
                    ac.rules.push_back(ArmCode::Pattern{entryBlock, ~0u});
                }

                ac.hasCondition = false;
                fallBackOnSimple = true;
                armCode.push_back(std::move(ac));
                continue;
            }
            // Release the freezing of outer states
            if (should_freeze) {
                // NOTE: The first scope should be the freeze
                builder.unfreeze_scope(sp, scopes.front().handle);
            }
            // And undo aliases
            builder.restore_aliases(std::move(aliases));
            auto guardEndBlock = builder.new_bb_unlinked();
            builder.endBlock(::MIR::Terminator::make_Goto(guardEndBlock));
            auto guardCode = builder.codeSaveEnd(std::move(csH));
            builder.set_cur_block(guardEndBlock);
            // Emit actual bindings
            DEBUG("Arm " << armIdx << " rule " << 0 << ":  Destructure");
            scopes.push_back({builder.new_scope_var(arm.mCode->span()), false});
            conv.schedule_pattern_drops(node.span(), arm.patterns.back(), PatternDropOrder::LastCandidate);
            auto bindingSplit = builder.new_scope_split(arm.mCode->span());
            conv.destructureFromList(arm.mCode->span(), match_ty, match_val.clone(), bindings0);
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
                    MIR::BasicBlockId new_cond_false;
                    MIR::BasicBlockId new_cond_true;

                    Mapper(MirBuilder& builder, MIR::BasicBlockId block0, MIR::BasicBlockId condFalse, MIR::BasicBlockId condTrue)
                        : block0(block0)
                        , condFalse(condFalse)
                        , condTrue(condTrue)
                        , new_cond_false(builder.new_bb_unlinked())
                        , new_cond_true(builder.new_bb_unlinked())
                    {
                        DEBUG("new_cond_false=" << new_cond_false << ", new_cond_true=" << new_cond_true);
                    }

                    MIR::BasicBlockId update_bb_ref(MIR::BasicBlockId bbIdx) {
                        // Any block defined before the save just propagates through
                        // E.g. if the guard contains a `break`
                        if (bbIdx < block0) {
                            return bbIdx;
                        }
                        if (bbIdx == condFalse) {
                            return new_cond_false;
                        }
                        if (bbIdx == condTrue) {
                            return new_cond_true;
                        }
                        BUG(Span(),
                            "update_bb_ref: Unknown BB " << bbIdx << " "
                                                         << ": block0=" << block0 << ", cond_false=" << condFalse << ", cond_true=" << condTrue);
                    }
                } mapper(builder, block0, condFalseBlockPat0, guardEndBlock);

                auto entryBlock = builder.new_bb_unlinked();
                builder.set_cur_block(entryBlock);
                // Set the binding temporaries with the correct borrows
                assert(bindingTemps.size() == armRules[i].mBindings.size());
                for (size_t j = 0; j < bindingTemps.size(); j++) {
                    const auto& b = armRules[i].mBindings[j];
                    auto val = conv.getValueForBindingPath(sp, match_ty, match_val, b);
                    DEBUG("Set alias for: " << *b.binding << " := " << val);
                    if (b.binding->mType != ::HIR::PatternBinding::Type::Move) {
                        MIR::LValue tmp2;
                        if (bindingTempsAlt[j] == ~0u) {
                            // Not a move binding, still need to borrow but no deref
                            // - Or, make another temporary for the borrow (no scope needed)
                            auto finalTy = conv.getBindingType(sp, b.binding->slot);
                            const auto& borrow = finalTy->as_Borrow();
                            finalTy = builder.resolve().crate.types.borrow(::HIR::BorrowType::Shared, borrow.inner, borrow.lifetime);
                            tmp2 = builder.new_temporary(finalTy);
                            bindingTempsAlt[j] = tmp2.as_Local();
                        } else {
                            tmp2 = ::MIR::LValue::newLocal(bindingTempsAlt[j]);
                        }
                        builder.push_stmt_assign(sp, tmp2.clone(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, std::move(val)}));
                        val = std::move(tmp2);
                    }
                    builder.push_stmt_assign(sp, ::MIR::LValue::newLocal(bindingTemps[j]), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, std::move(val)}));
                }
                // Clone the guard contents with updated block references
                builder.insert_cloned(sp, guardCode, mapper);

                // Add the final bindings and jump to the body
                builder.set_cur_block(mapper.new_cond_true);
                DEBUG("Arm " << armIdx << " rule " << i - firstArmRuleIdx << ":  Destructure");
                conv.destructureFromList(arm.mCode->span(), match_ty, match_val.clone(), armRules[i].mBindings);
                builder.endSplitArm(arm.mCode->span(), bindingSplit, /*reachable=*/true);
                builder.endBlock(::MIR::Terminator::make_Goto(armBodyBlock));

                ArmCode::Pattern acp;
                acp.entry = entryBlock;
                acp.condFalse = mapper.new_cond_false;
                ac.rules.push_back(acp);
            }

            // All successful pattern alternatives enter the same body. Merge
            // their move states first so the body and every unwind edge use
            // drop flags valid for every predecessor.
            builder.terminate_scope(arm.mCode->span(), std::move(bindingSplit), /*emit_cleanup=*/false);

            // Emit body code
            DEBUG("-- Body Code");

            scopes.push_back({builder.new_scope_temp(arm.mCode->span()), false});
            builder.set_cur_block(armBodyBlock);

            if (node.isLetElse && armIdx + 1 == node.arms.size()) {
                for (const auto temporary : ::reverse(let_else_initializer_temps)) {
                    builder.dropLvalue(node.span(), ::MIR::LValue::newLocal(temporary));
                }
            }

            conv.visit_node_ptr(arm.mCode);

            if (builder.block_active()) {
                // - Set result
                auto res = builder.getResult(arm.mCode->span());
                builder.push_stmt_assign(arm.mCode->span(), result_val.clone(), mv$(res));
            } else {
                assert(!builder.hasResult());
            }
            // Pop/end scopes
            while (!scopes.empty()) {
                if (scopes.back().is_split) {
                    builder.endSplitArm(arm.mCode->span(), scopes.back().handle, /*reachable*/ builder.block_active());
                }
                builder.terminate_scope(arm.mCode->span(), std::move(scopes.back().handle), builder.block_active());
                scopes.pop_back();
            }
            builder.endSplitArm(arm.mCode->span(), pat_scope, /*reachable*/ builder.block_active());
            builder.terminate_scope(sp, std::move(pat_scope), builder.block_active());
            builder.terminate_scope_early(sp, match_scope);

            // Go to the next block (out of the match) (if the body didn't diverge)
            if (builder.block_active()) {
                builder.endBlock(::MIR::Terminator::make_Goto(next_block));
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
        sort_rulesets(armRules);
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
                WARNING(node.arms[it->armIdx].mCode->span(), W0000, "Duplicate match pattern, unreachable code" << "\n - Pattern : " << PatternDump(builder.resolve(), match_ty, it->rules) << "\n - Previous at " << node.arms[(it - 1)->armIdx].mCode->span());
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
        MIRLowerHIRMatchSimple(builder, conv, node /*.span(), match_ty*/, mv$(match_val), mv$(armRules), mv$(armCode), firstCmpBlock);
    } else {
        MIRLowerHIRMatchGrouped(builder, conv, node.span(), match_ty, mv$(match_val), mv$(armRules), mv$(armCode), firstCmpBlock);
    }

    builder.set_cur_block(next_block);
    builder.set_result(node.span(), mv$(result_val));
    builder.terminate_scope(node.span(), mv$(match_scope));
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
            os << e.idx << " [" << e.sub_rules << "]";
        }
        // Slice pattern
        TU_ARMA(Slice, e) {
            os << "len=" << e.len << " [" << e.sub_rules << "]";
        }
        // SplitSlice
        TU_ARMA(SplitSlice, e) {
            os << "len>=" << e.min_len << " [" << e.leading << ", ..., " << e.trailing << "]";
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
            os << e.first << " .." << (e.is_inclusive ? "=" : "") << " " << e.last;
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
            assert(te.sub_rules.size() == xe.sub_rules.size());
            for (unsigned int i = 0; i < te.sub_rules.size(); i++) {
                auto cmp = te.sub_rules[i].ord(xe.sub_rules[i]);
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
            assert(te.sub_rules.size() == xe.sub_rules.size());
            for (unsigned int i = 0; i < te.sub_rules.size(); i++) {
                auto cmp = te.sub_rules[i].ord(xe.sub_rules[i]);
                if (cmp != ::OrdEqual) {
                    return cmp;
                }
            }
            return ::OrdEqual;
        }
        TU_ARMA(SplitSlice, te, xe) {
            ORD(te.leading, xe.leading);
            ORD(te.min_len, xe.min_len);
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
            return ::ord(te.is_inclusive, xe.is_inclusive);
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
                return PatternRule::make_Variant({te.idx, H::cloneList(te.sub_rules)});
                TU_ARMA(Slice, te)
                return PatternRule::make_Slice({te.len, H::cloneList(te.sub_rules)});
                TU_ARMA(SplitSlice, te)
                return PatternRule::make_SplitSlice({te.min_len, te.trailing_len, H::cloneList(te.leading), H::cloneList(te.trailing)});

                TU_ARMA(Bool, te)
                return te;
                TU_ARMA(Value, te)
                return te.clone();
                TU_ARMA(ValueRange, te)
                return PatternRule::make_ValueRange({te.first.clone(), te.last.clone(), te.is_inclusive});
            }
            throw "";
        }
    };

    auto rv = H::cloneInner(*this);
    rv.field_path = this->field_path;
    return rv;
}

::Ordering PatternRuleset::rule_is_before(const PatternRule& l, const PatternRule& r) {
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
            assert(le.sub_rules.size() == re.sub_rules.size());
            for (unsigned int i = 0; i < le.sub_rules.size(); i++) {
                auto cmp = rule_is_before(le.sub_rules[i], re.sub_rules[i]);
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
            assert(le.sub_rules.size() == re.sub_rules.size());
            for (unsigned int i = 0; i < le.sub_rules.size(); i++) {
                auto cmp = rule_is_before(le.sub_rules[i], re.sub_rules[i]);
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

bool PatternRuleset::is_before(const PatternRuleset& other) const {
    assert(rules.size() == other.rules.size());
    for (unsigned int i = 0; i < rules.size(); i++) {
        const auto& l = rules[i];
        const auto& r = other.rules[i];
        auto cmp = rule_is_before(l, r);
        if (cmp != ::OrdEqual) {
            return cmp == ::OrdLess;
        }
    }
    return false;
}

void PatternRulesetBuilder::push_rule(PatternRule r) {
    assert(this->subset_start < this->subset_end);
    assert(this->subset_end <= rulesets.size());
    for (size_t i = subset_start; i < subset_end; i++) {
        rulesets[i].rules.push_back(i == subset_end - 1 ? std::move(r) : r.clone());
        rulesets[i].rules.back().field_path = fieldPath;
    }
}

void PatternRulesetBuilder::push_binding(PatternBinding b) {
    assert(this->subset_start < this->subset_end);
    assert(this->subset_end <= rulesets.size());
    for (size_t i = subset_start; i < subset_end; i++) {
        DEBUG(i << " " << b);
        rulesets[i].mBindings.push_back(b);
    }
}

void PatternRulesetBuilder::push_bindings(std::vector<PatternBinding> bindings) {
    assert(this->subset_start < this->subset_end);
    assert(this->subset_end <= rulesets.size());
    for (size_t i = subset_start; i < subset_end; i++) {
        auto& l = rulesets[i].mBindings;
        l.insert(l.end(), bindings.begin(), bindings.end());
        DEBUG(i << " [" << bindings << "] = [" << l << "]");
    }
}

void PatternRulesetBuilder::set_impossible() {
    assert(this->subset_start < this->subset_end);
    assert(this->subset_end <= rulesets.size());
    for (size_t i = subset_start; i < subset_end; i++) {
        rulesets[i].isImpossible = true;
    }
}

/// Multiply the current subset of the ruleset, then visit every new subset
void PatternRulesetBuilder::multiply_rulesets(size_t n, std::function<void(size_t idx)> cb) {
    assert(n > 0);
    if (n == 1) {
        cb(0);
        return;
    }
    TRACE_FUNCTION_F(n);
    assert(this->subset_start < this->subset_end);
    assert(this->subset_end <= rulesets.size());
    size_t subset_size = this->subset_end - this->subset_start;
    size_t ofs = (n - 1) * subset_size;
    assert(ofs > 0);
    size_t new_subset_end = this->subset_start + n * subset_size;
    size_t n_tail = rulesets.size() - this->subset_end;
    DEBUG("subset_size=" << subset_size << ", ofs = " << ofs << ", n_tail=" << n_tail);
    rulesets.resize(rulesets.size() + (n - 1) * subset_size);
    assert(new_subset_end == rulesets.size() - n_tail);
    // Copy the tail out of the way (reverse to avoid chasing itself)
    for (size_t i = rulesets.size(); i-- > new_subset_end;) {
        rulesets[i] = std::move(rulesets[i - ofs]);
    }
    // Copy `n-1` copies of the current subset after itself
    for (size_t j = 1; j < n; j++) {
        for (size_t i = 0; i < subset_size; i++) {
            const auto& src = rulesets[this->subset_start + i];
            rulesets[this->subset_start + j * subset_size + i] = src.clone();
        }
    }
    for (size_t j = this->subset_start + subset_size; j < new_subset_end; j += subset_size) {
        for (size_t i = 0; i < subset_size; i++) {
            const auto& exp = rulesets[this->subset_start + i];
            const auto& a = rulesets[j + i];
            ASSERT_BUG(Span(), a.rules == exp.rules, "BUG: {" << a.rules << "} != {" << exp.rules << "}");
            ASSERT_BUG(Span(), a.mBindings == exp.mBindings, "BUG: {" << a.mBindings << "} != {" << exp.mBindings << "}");
        }
    }
    for (size_t i = this->subset_start; i < new_subset_end; i += 1) {
        DEBUG("#" << i << " rules=[" << rulesets[i].rules << "], bindings=[" << rulesets[i].mBindings << "]");
    }

    // Iterate the new subsets
    size_t saved_start = this->subset_start;
    this->subset_end = this->subset_start;
    for (size_t i = 0; i < n; i++) {
        auto orig_start = this->subset_start;
        this->subset_end += subset_size;
        DEBUG("++ " << i << " " << this->subset_start << " - " << this->subset_end);
        for (size_t j = this->subset_start; j < this->subset_end; j++) {
            rulesets[j].orPath.push_back(static_cast<unsigned>(i));
        }
        cb(i);
        DEBUG("-- " << i);
        assert(this->subset_start == orig_start);                     // This should always be unchanged (even if the callback splits again). The end can change though.
        assert(this->subset_end >= this->subset_start + subset_size); // The end should always be at least equal to start + size (i.e. hasn't shrunk)
        this->subset_start = this->subset_end;
    }
    // Update the subset again to cover everything
    this->subset_start = saved_start;
    ::std::stable_sort(
        rulesets.begin() + this->subset_start,
        rulesets.begin() + this->subset_end,
        [](const Ruleset& a, const Ruleset& b) {
            return a.orPath < b.orPath;
        });
    // NOTE: Can't asser that the end is as-expected, as there might be inner subsets created that makes this assumption no longer valid
    //ASSERT_BUG(Span(), this->subset_end == new_subset_end, this->subset_end << " == " << new_subset_end);
    for (size_t i = this->subset_start; i < this->subset_end; i += 1) {
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
                    this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Float({lit.read_float(2), e})));
                    break;
                case ::HIR::CoreType::F32:
                    this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Float({lit.read_float(4), e})));
                    break;
                case ::HIR::CoreType::F64:
                    this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Float({lit.read_float(8), e})));
                    break;
                case ::HIR::CoreType::F128:
                    this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Float({lit.read_float(16), e})));
                    break;

                case ::HIR::CoreType::U8:
                    this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Uint({lit.read_uint(1), e})));
                    break;
                case ::HIR::CoreType::U16:
                    this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Uint({lit.read_uint(2), e})));
                    break;
                case ::HIR::CoreType::U32:
                    this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Uint({lit.read_uint(4), e})));
                    break;
                case ::HIR::CoreType::U64:
                    this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Uint({lit.read_uint(8), e})));
                    break;
                case ::HIR::CoreType::U128:
                    this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Uint({lit.read_uint(16), e})));
                    break;
                case ::HIR::CoreType::Usize:
                    this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Uint({lit.read_uint(TargetGetPointerBits() / 8), e})));
                    break;

                case ::HIR::CoreType::I8:
                    this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Int({lit.read_sint(1), e})));
                    break;
                case ::HIR::CoreType::I16:
                    this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Int({lit.read_sint(2), e})));
                    break;
                case ::HIR::CoreType::I32:
                    this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Int({lit.read_sint(4), e})));
                    break;
                case ::HIR::CoreType::I64:
                    this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Int({lit.read_sint(8), e})));
                    break;
                case ::HIR::CoreType::I128:
                    this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Int({lit.read_sint(16), e})));
                    break;
                case ::HIR::CoreType::Isize:
                    this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Int({lit.read_sint(TargetGetPointerBits() / 8), e})));
                    break;

                case ::HIR::CoreType::Bool:
                    this->push_rule(PatternRule::make_Bool(lit.read_uint(1) != 0));
                    break;
                // Char is just another name for 'u32'... but with a restricted range
                case ::HIR::CoreType::Char:
                    this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Uint({lit.read_uint(4), e})));
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
                    this->push_rule(PatternRule::make_Any({}));
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
                    auto var_info = enmRepr->getEnumVariant(sp, mResolve, lit);
                    unsigned var_idx = var_info.first;
                    bool sub_has_tag = var_info.second;

                    PatternRulesetBuilder sub_builder{this->mResolve};
                    if (enmRepr->fields.size() > 1 || enmRepr->variants.is_None()) {
                        sub_builder.fieldPath = fieldPath;
                        sub_builder.fieldPath.push_back(var_idx);

                        // If the tag is in the sub-type, then ignore.
                        const auto& var_ty = enmRepr->fields[var_idx].ty;
                        auto var_lit = lit.slice(enmRepr->fields[var_idx].offset);
                        // NOTE: The tag is only present if it's an auto-generated struct (i.e. not `()`)
                        if (sub_has_tag && var_ty != mResolve.crate.types.unit()) {
                            // This inner type should be a struct
                            DEBUG("Enum variant type w/ tag field: " << var_ty);
                            auto* inner_repr = TargetGetTypeRepr(sp, mResolve, var_ty);
                            assert(inner_repr->variants.is_None());
                            assert(inner_repr->fields.size() > 0);
                            sub_builder.fieldPath.push_back(0);
                            for (size_t i = 0; i < inner_repr->fields.size() - 1; i++) {
                                sub_builder.appendFromLit(sp, var_lit.slice(inner_repr->fields[i].offset), inner_repr->fields[i].ty);
                                sub_builder.fieldPath.back()++;
                            }
                            sub_builder.fieldPath.pop_back();
                        } else {
                            sub_builder.appendFromLit(sp, var_lit, var_ty);
                        }
                    }

                    ASSERT_BUG(sp, sub_builder.rulesets.size() == 1, "Multiple rulesets generated from a literal");
                    this->push_rule(PatternRule::make_Variant({var_idx, mv$(sub_builder.rulesets[0].rules)}));
                }
        }
        }
        TU_ARMA(Generic, e) {
            // Generics don't destructure, so the only valid pattern is `_`
            TODO(sp, "Match generic with literal?");
            this->push_rule(PatternRule::make_Any({}));
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
                auto ptr_size = TargetGetPointerBits() / 8;
                auto ptr = lit.read_uint(ptr_size).truncate_u64();
                auto len = lit.slice(ptr_size, ptr_size).read_uint(ptr_size).truncate_u64();
                auto* r = lit.getReloc();
                ASSERT_BUG(sp, r, "Null relocation for string in pattern generation");
                ASSERT_BUG(sp, ptr >= EncodedLiteral::PTR_BASE, "");
                ptr -= EncodedLiteral::PTR_BASE;

                ASSERT_BUG(sp, !r->p, "TODO: Handle &str match constant with non-string relocation - " << *r->p);
                ASSERT_BUG(sp, ptr <= r->bytes.size(), "");
                ASSERT_BUG(sp, len <= r->bytes.size(), "");
                ASSERT_BUG(sp, ptr + len <= r->bytes.size(), "");

                this->push_rule(PatternRule::make_Value(std::string(r->bytes.data() + ptr, r->bytes.data() + ptr + len)));
            } else if (e.inner->is_Slice() && e.inner->as_Slice().inner == ::HIR::CoreType::U8) {
                auto ptr_size = TargetGetPointerBits() / 8;
                auto ptr = lit.read_uint(ptr_size).truncate_u64();
                auto len = lit.slice(ptr_size, ptr_size).read_uint(ptr_size).truncate_u64();
                auto* r = lit.getReloc();
                ASSERT_BUG(sp, r, "Null relocation for byte-string in pattern generation");
                ASSERT_BUG(sp, ptr >= EncodedLiteral::PTR_BASE, "");
                ptr -= EncodedLiteral::PTR_BASE;

                if (r->p) {
                    ASSERT_BUG(sp, ptr == 0, "TODO: Non-zero offset with reference");
                    MonomorphState val_params(mResolve.crate.types);
                    auto v = mResolve.getValue(sp, *r->p, val_params);
                    ASSERT_BUG(sp, v.is_Static(), "&[u8] match with borrow of non-static (" << *r->p << ") - " << v.tag_str());
                    const HIR::Static& s = *v.as_Static();
                    ASSERT_BUG(sp, s.valueGenerated, "&[u8] match with borrow of non-resolved static (" << *r->p << ")");
                    const EncodedLiteral& val = s.valueRes;
                    ASSERT_BUG(sp, ptr <= val.bytes.size(), "");
                    ASSERT_BUG(sp, len <= val.bytes.size(), "");
                    ASSERT_BUG(sp, ptr + len <= val.bytes.size(), "");

                    this->push_rule(PatternRule::make_Value(std::vector<uint8_t>(val.bytes.data() + ptr, val.bytes.data() + ptr + len)));
                } else {
                    ASSERT_BUG(sp, ptr <= r->bytes.size(), "");
                    ASSERT_BUG(sp, len <= r->bytes.size(), "");
                    ASSERT_BUG(sp, ptr + len <= r->bytes.size(), "");

                    this->push_rule(PatternRule::make_Value(std::vector<uint8_t>(r->bytes.data() + ptr, r->bytes.data() + ptr + len)));
                }
            } else {
                TODO(sp, "Match literal Borrow: ty=" << ty << " lit=" << lit);
            }
            fieldPath.pop_back();
        }
        TU_ARMA(Pointer, e) {
            // Need to be able to tell downstream to cast to integer before comparison?
            this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Uint({lit.read_uint(TargetGetPointerBits() / 8), HIR::CoreType::Usize})));
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
            TU_MATCH_DEF(::HIR::Pattern::Value, (val), (e), (BUG(sp, "Invalid Value type in " << pat);), (Integer, return e.value;), (Named, assert(e.binding); return EncodedLiteralSlice(e.binding->valueRes).read_uint();))
            throw "";
        }

        static S128 getPatternValueSigned(const Span& sp, const ::HIR::Pattern& pat, const ::HIR::Pattern::Value& val) {
            TU_MATCH_DEF(::HIR::Pattern::Value, (val), (e), (BUG(sp, "Invalid signed Value type in " << pat);), (Integer, return S128(e.value);), (Named, assert(e.binding); return EncodedLiteralSlice(e.binding->valueRes).read_sint();))
            throw "";
        }

        static FloatValue getPatternValueFloat(const Span& sp, const ::HIR::Pattern& pat, const ::HIR::Pattern::Value& val) {
            TU_MATCH_DEF(::HIR::Pattern::Value, (val), (e), (BUG(sp, "Invalid Value type in " << pat);), (Float, return e.value;), (Named, assert(e.binding); return EncodedLiteralSlice(e.binding->valueRes).read_float();))
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

        this->push_binding(PatternBinding(std::move(path), pb));
    }

    const auto* ty_p = &top_ty;
    for (size_t i = 0; i < pat.implicitDerefCount; i++) {
        if (!(*ty_p)->is_Borrow()) {
            BUG(sp, "Deref step " << i << "/" << pat.implicitDerefCount << " hit a non-borrow " << *ty_p << " from " << top_ty);
        }
        ty_p = &(*ty_p)->as_Borrow().inner;
        fieldPath.push_back(FIELD_DEREF);
    }
    const auto& ty = *ty_p;

    // TODO: Outer handling for Value::Named patterns
    // - Convert them into either a pattern, or just a variant of this function that operates on ::HIR::Literal
    //  > It does need a way of handling unknown-value constants (e.g. <GenericT as Foo>::CONST)
    //  > Those should lead to a simple match? Or just a custom rule type that indicates that they're checked early
    if (const auto* pe = pat.mData.opt_Value()) {
        if (const auto* pve = pe->val.opt_Named()) {
            if (pve->binding) {
                // Request consteval
                if (pve->binding->valueState == HIR::Constant::ValueState::Unknown) {
                    MonomorphState unused_ms(mResolve.crate.types);
                    const HIR::GenericParams* impl_def = nullptr;
                    auto v = mResolve.getValue(sp, pve->path, unused_ms, false, &impl_def);
                    ConvertHIRConstantEvaluateConstant(mResolve.crate, impl_def, pve->path, const_cast<HIR::Constant&>(*pve->binding));
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
        this->multiply_rulesets(e.size(), [&](size_t i) {
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
                    this->push_rule(PatternRule::make_Any({}));
                }
                TU_ARM(pat.mData, Range, pe) {
                    if (!pe.start || !pe.end) {
                        assert(pe.start || pe.end);
                        if (pe.start) {
                            this->push_rule(
                                PatternRule::make_ValueRange({
                                    H::getPatternValue(sp, pat, *pe.start, e),
                                    H::getPatternValueMax(sp, pat, e),
                                    true // Inclusive always
                                })
                            );
                        } else {
                            this->push_rule(PatternRule::make_ValueRange({H::getPatternValueMin(sp, pat, e), H::getPatternValue(sp, pat, *pe.end, e), pe.is_inclusive}));
                        }
                    } else {
                        this->push_rule(PatternRule::make_ValueRange({H::getPatternValue(sp, pat, *pe.start, e), H::getPatternValue(sp, pat, *pe.end, e), pe.is_inclusive}));
                    }
                }
                TU_ARM(pat.mData, Value, pe) {
                    switch (e) {
                        case ::HIR::CoreType::Bool:
                            // TODO: Support values from `const` too
                            this->push_rule(PatternRule::make_Bool(pe.val.as_Integer().value != 0));
                            break;
                        default:
                            this->push_rule(H::getPatternValue(sp, pat, pe.val, e));
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
                    Tuple, assert(e.size() == pe.sub_patterns.size()); for (unsigned int i = 0; i < e.size(); i++) {
                        this->appendFrom(sp, pe.sub_patterns[i], e[i]);
                        fieldPath.back()++;
                    }
                ),
                (SplitTuple, assert(e.size() >= pe.leading.size() + pe.trailing.size()); unsigned trailing_start = e.size() - pe.trailing.size(); for (unsigned int i = 0; i < e.size(); i++) {
                    if (i < pe.leading.size()) {
                        this->appendFrom(sp, pe.leading[i], e[i]);
                    } else if (i < trailing_start) {
                        this->appendFrom(sp, emptyPattern, e[i]);
                    } else {
                        this->appendFrom(sp, pe.trailing[i - trailing_start], e[i]);
                    }
                    fieldPath.back()++;
                })
            )
            fieldPath.pop_back();
        }
        TU_ARMA(Path, e) {
            struct PH {
                static void push_pattern_tuple(PatternRulesetBuilder& builder, const Span& sp, const ::HIR::Pattern::Data::Data_PathTuple& pe, std::function<const HIR::TypeData*(const HIR::TypeData*)> maybe_monomorph) {
                    const auto& sd = ::HIR::pattern_get_tuple(sp, pe.path, pe.binding);
                    assert(sd.size() >= pe.leading.size() + pe.trailing.size());
                    size_t trailing_start = sd.size() - pe.trailing.size();
                    for (unsigned int i = 0; i < sd.size(); i++) {
                        const auto& fld = sd[i];

                        if (i < pe.leading.size()) {
                            builder.appendFrom(sp, pe.leading[i], maybe_monomorph(fld.ent));
                        } else if (i < trailing_start) {
                            builder.appendFrom(sp, emptyPattern, maybe_monomorph(fld.ent));
                        } else {
                            builder.appendFrom(sp, pe.trailing[i - trailing_start], maybe_monomorph(fld.ent));
                        }
                        builder.fieldPath.back()++;
                    }
                }
                static void push_pattern_struct(PatternRulesetBuilder& builder, const Span& sp, const ::HIR::Pattern::Data::Data_PathNamed& pe, std::function<const HIR::TypeData*(const HIR::TypeData*)> maybe_monomorph) {
                    const auto& sd = ::HIR::pattern_get_named(sp, pe.path, pe.binding);
                    // NOTE: Iterates in field order (not pattern order) to ensure that patterns are in order between arms
                    for (const auto& fld : sd) {
                        const auto& sty_mono = maybe_monomorph(fld.ty);

                        auto it = ::std::find_if(pe.sub_patterns.begin(), pe.sub_patterns.end(), [&](const auto& x) {
                            return x.first == fld.name;
                        });
                        if (it == pe.sub_patterns.end()) {
                            builder.appendFrom(sp, emptyPattern, sty_mono);
                        } else {
                            builder.appendFrom(sp, it->second, sty_mono);
                        }
                        builder.fieldPath.back()++;
                    }
                }
            };
            ::HIR::TypeRef tmp;
            auto maybe_monomorph = [&](const ::HIR::TypeData* ty) -> const ::HIR::TypeData* {
                if (monomorphise_type_needed(ty)) {
                    tmp = MonomorphStatePtr(mResolve.crate.types, nullptr, &e.path.mData.as_Generic().mParams, nullptr).monomorph_type(sp, ty);
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
                    TU_MATCH_DEF(::HIR::Pattern::Data, (pat.mData), (pe), (BUG(sp, "Matching opaque type with invalid pattern - " << pat);), (Any, this->push_rule(PatternRule::make_Any({}));))
                }
                TU_ARMA(Struct, pbe) {
                    const auto& str_data = pbe->mData;

                    if (mLangBox && e.path.mData.as_Generic().mPath == *mLangBox) {
                        const auto& inner_ty = e.path.mData.as_Generic().mParams.types.at(0);
                        TU_MATCH_DEF(
                            ::HIR::Pattern::Data,
                            (pat.mData),
                            (pe),
                            (BUG(sp, "Match not allowed, " << ty << " with " << pat);),
                            (Any,
                             // _ on a box, recurse into the box type.
                             fieldPath.push_back(FIELD_DEREF);
                             this->appendFrom(sp, emptyPattern, inner_ty);
                             fieldPath.pop_back();),
                            (Box, fieldPath.push_back(FIELD_DEREF); this->appendFrom(sp, *pe.sub, inner_ty); fieldPath.pop_back();)
                        )
                        break;
                    }
            TU_MATCH_HDRA( (str_data), {)
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
                                    ASSERT_BUG(sp, pe.sub_patterns.size() == 0, "Matching unit-like struct with sub-patterns - " << pat);
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
                                        this->appendFrom(sp, emptyPattern, maybe_monomorph(fld.ent));
                                        fieldPath.back()++;
                                    }
                                }
                                TU_ARMA(PathNamed, pe) {
                                    // Only allow with an empty tuple (assuming that the pattern is also empty)... or if the pattern is a wildcard
                                    if (sd.size() != 0 && !pe.is_wildcard()) {
                                        BUG(sp, "Match not allowed, " << ty << " with " << pat);
                                    }
                                    for (const auto& fld : sd) {
                                        ASSERT_BUG(sp, fieldPath.back() < FIELD_INDEX_MAX, "Too-large struct field index");
                                        this->appendFrom(sp, emptyPattern, maybe_monomorph(fld.ent));
                                        fieldPath.back()++;
                                    }
                                }
                                TU_ARMA(PathTuple, pe) {
                                    assert(pe.binding.is_Struct());
                                    PH::push_pattern_tuple(*this, sp, pe, maybe_monomorph);
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
                                        this->appendFrom(sp, emptyPattern, maybe_monomorph(fld.ty));
                                        fieldPath.back()++;
                                    }
                                    fieldPath.pop_back();
                                }
                                TU_ARMA(PathNamed, pe) {
                                    assert(pe.binding.is_Struct());
                                    fieldPath.push_back(0);
                                    PH::push_pattern_struct(*this, sp, pe, maybe_monomorph);
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
                            this->push_rule(PatternRule::make_Any({}));
                        }
                        TU_ARMA(PathNamed, pe) {
                            ASSERT_BUG(sp, pe.binding.is_Union() && pe.binding.as_Union() == pbe, "Union pattern binding mismatch");
                            ASSERT_BUG(sp, pe.sub_patterns.size() == 1, "Union pattern must select exactly one field");

                            const auto& fieldPattern = pe.sub_patterns.front();
                            auto fieldIt = ::std::find_if(pbe->mVariants.begin(), pbe->mVariants.end(), [&](const auto& field) {
                                return field.name == fieldPattern.first;
                            });
                            ASSERT_BUG(sp, fieldIt != pbe->mVariants.end(), "Unable to find union field " << fieldPattern.first);

                            const auto fieldIndex = static_cast<unsigned>(fieldIt - pbe->mVariants.begin());
                            ASSERT_BUG(sp, fieldIndex < FIELD_INDEX_MAX, "Too-large union field index");
                            fieldPath.push_back(fieldIndex);
                            this->appendFrom(sp, fieldPattern.second, maybe_monomorph(fieldIt->ty));
                            fieldPath.pop_back();
                        }
            }
                }
                TU_ARMA(ExternType, pbe) {
            TU_MATCH_HDRA( (pat.mData), {)
            default:
                BUG(sp, "Match not allowed, " << ty <<  " with " << pat);
                        TU_ARMA(Any, pe) {
                            this->push_rule(PatternRule::make_Any({}));
                        }
            }
                }
                TU_ARMA(Enum, pbe) {
            TU_MATCH_HDRA( (pat.mData), {)
            default:
                BUG(sp, "Match not allowed, " << ty <<  " with " << pat);
                        TU_ARMA(Any, pe) {
                            this->push_rule(PatternRule::make_Any({}));
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
                            this->push_rule(PatternRule::make_Variant({pe.binding.as_Enum().var_idx, {}}));
                        }
                        TU_ARMA(PathTuple, pe) {
                            assert(pe.binding.is_Enum());
                            const auto& be = pe.binding.as_Enum();

                            PatternRulesetBuilder sub_builder{this->mResolve};
                            sub_builder.fieldPath = fieldPath;
                            ASSERT_BUG(sp, be.var_idx < FIELD_INDEX_MAX, "Too-large variant index in " << ty);
                            sub_builder.fieldPath.push_back(be.var_idx);
                            sub_builder.fieldPath.push_back(0);

                            PH::push_pattern_tuple(sub_builder, sp, pe, maybe_monomorph);

                            this->multiply_rulesets(sub_builder.rulesets.size(), [&](size_t i) {
                                auto& sr = sub_builder.rulesets[i];
                                if (sr.isImpossible) {
                                    this->set_impossible();
                                }
                                this->push_rule(PatternRule::make_Variant({be.var_idx, mv$(sr.rules)}));
                                this->push_bindings(mv$(sr.mBindings));
                            });
                        }
                        TU_ARMA(PathNamed, pe) {
                            assert(pe.binding.is_Enum());
                            const auto& be = pe.binding.as_Enum();

                            PatternRulesetBuilder sub_builder{this->mResolve};
                            sub_builder.fieldPath = fieldPath;
                            ASSERT_BUG(sp, be.var_idx < FIELD_INDEX_MAX, "Too-large variant index");
                            sub_builder.fieldPath.push_back(be.var_idx);
                            sub_builder.fieldPath.push_back(0);

                            // Empty variants can be matched with `Var { [..] }` even if they're not struct-like
                            if (be.ptr->is_value()) {
                                assert(pe.sub_patterns.empty());
                            } else if (be.ptr->mData.as_Data().at(be.var_idx).type == mResolve.crate.types.unit()) {
                                assert(pe.sub_patterns.empty());
                            } else if (!be.ptr->mData.as_Data().at(be.var_idx).is_struct) {
                                assert(pe.sub_patterns.empty());
                                const auto& sd = ::HIR::pattern_get_tuple(sp, pe.path, pe.binding);
                                for (unsigned int i = 0; i < sd.size(); i++) {
                                    const auto& fld = sd[i];
                                    sub_builder.appendFrom(sp, emptyPattern, maybe_monomorph(fld.ent));
                                    sub_builder.fieldPath.back()++;
                                }
                            } else {
                                PH::push_pattern_struct(sub_builder, sp, pe, maybe_monomorph);
                            }

                            this->multiply_rulesets(sub_builder.rulesets.size(), [&](size_t i) {
                                auto& sr = sub_builder.rulesets[i];
                                if (sr.isImpossible) {
                                    this->set_impossible();
                                }
                                this->push_rule(PatternRule::make_Variant({be.var_idx, mv$(sr.rules)}));
                                this->push_bindings(mv$(sr.mBindings));
                            });
                        }
            }
                }
        }
        }
        TU_ARMA(Generic, e) {
            // Generics don't destructure, so the only valid pattern is `_`
            TU_MATCH_DEF(::HIR::Pattern::Data, (pat.mData), (pe), (BUG(sp, "Match not allowed, " << ty << " with " << pat);), (Any, this->push_rule(PatternRule::make_Any({}));))
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
                this->push_rule(PatternRule::make_Any({}));
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
                    ASSERT_BUG(sp, e.size.as_Known() == pe.sub_patterns.size(), "Pattern size mismatch");
                    for (const auto& v : pe.sub_patterns) {
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

                    if (pe.extraBind.is_valid()) {
                        ASSERT_BUG(sp, pe.extraBind.implicitDerefCount == 0, "");
                        PatternBinding pb(fieldPath, pe.extraBind);
                        pb.field.pop_back();
                        pb.split_slice = std::make_pair(pe.leading.size(), pe.trailing.size());
                        this->push_binding(mv$(pb));
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
                    this->push_rule(PatternRule::make_Any({}));
                }
                TU_ARMA(Slice, pe) {
                    // Sub-patterns
                    PatternRulesetBuilder sub_builder{this->mResolve};
                    sub_builder.fieldPath = fieldPath;
                    sub_builder.fieldPath.push_back(0);
                    ASSERT_BUG(sp, pe.sub_patterns.size() < FIELD_INDEX_MAX, "Too many slice rules to fit encodng");
                    for (const auto& subpat : pe.sub_patterns) {
                        sub_builder.appendFrom(sp, subpat, e.inner);
                        sub_builder.fieldPath.back()++;
                    }

                    // Encodes length check and sub-pattern rules
                    this->multiply_rulesets(sub_builder.rulesets.size(), [&](size_t i) {
                        auto& sr = sub_builder.rulesets[i];
                        if (sr.isImpossible) {
                            this->set_impossible();
                        }
                        this->push_rule(PatternRule::make_Slice({static_cast<unsigned int>(pe.sub_patterns.size()), mv$(sr.rules)}));
                        this->push_bindings(mv$(sr.mBindings));
                    });
                }
                TU_ARMA(SplitSlice, pe) {
                    PatternRulesetBuilder sub_builder{this->mResolve};
                    sub_builder.fieldPath = fieldPath;
                    ASSERT_BUG(sp, pe.leading.size() < FIELD_INDEX_MAX, "Too many leading slice rules to fit encodng");
                    sub_builder.fieldPath.push_back(0);
                    for (const auto& subpat : pe.leading) {
                        sub_builder.appendFrom(sp, subpat, e.inner);
                        sub_builder.fieldPath.back()++;
                    }
                    auto leading_rulesets = mv$(sub_builder.rulesets);
                    sub_builder.rulesets.clear();
                    sub_builder.rulesets.resize(1);
                    sub_builder.subset_start = 0;
                    sub_builder.subset_end = 1;

                    if (pe.trailing.size()) {
                        // Needs a way of encoding the negative offset in the field path
                        // - For now, just use a very high number (and assert that it's not more than 128)
                        ASSERT_BUG(sp, pe.trailing.size() < FIELD_INDEX_MAX, "Too many trailing slice rules to fit encodng");
                        sub_builder.fieldPath.back() = FIELD_INDEX_MAX + (FIELD_INDEX_MAX - pe.trailing.size());
                        for (const auto& subpat : pe.trailing) {
                            sub_builder.appendFrom(sp, subpat, e.inner);
                            sub_builder.fieldPath.back()++;
                        }
                    }
                    auto trailing_rulesets = mv$(sub_builder.rulesets);

                    if (pe.extraBind.is_valid()) {
                        ASSERT_BUG(sp, pe.extraBind.implicitDerefCount == 0, "");
                        PatternBinding pb(fieldPath, pe.extraBind);
                        pb.split_slice = std::make_pair(pe.leading.size(), pe.trailing.size());
                        this->push_binding(mv$(pb));
                    }

                    this->multiply_rulesets(leading_rulesets.size() * trailing_rulesets.size(), [&](size_t i) {
                        size_t i_l = i % leading_rulesets.size();
                        size_t i_t = i / leading_rulesets.size();
                        auto& sr_l = leading_rulesets[i_l];
                        auto& sr_t = trailing_rulesets[i_t];
                        if (sr_l.isImpossible || sr_t.isImpossible) {
                            this->set_impossible();
                        }

                        auto rules_l = sr_l.clone();
                        auto rules_t = sr_t.clone();
                        this->push_rule(PatternRule::make_SplitSlice({static_cast<unsigned int>(pe.leading.size() + pe.trailing.size()), static_cast<unsigned int>(pe.trailing.size()), mv$(rules_l.rules), mv$(rules_t.rules)}));
                        this->push_bindings(mv$(rules_l.mBindings));
                        this->push_bindings(mv$(rules_t.mBindings));
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
                        this->push_rule(PatternRule::make_Value(s));
                    } else if (pe.val.is_ByteString()) {
                        const auto& s = pe.val.as_ByteString().v;
                        // When matching a fixed-size array, expand to per-element rules so the
                        // field paths line up with `[a, b, ...]` patterns in sibling arms.
                        if (e.inner->is_Array()) {
                            const auto& ae = e.inner->as_Array();
                            ASSERT_BUG(sp, ae.size.is_Known() && ae.size.as_Known() == s.size(), "Byte string pattern size mismatch - " << pat << " vs " << e.inner);
                            fieldPath.push_back(0);
                            for (auto c : s) {
                                this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Uint({U128(static_cast<uint8_t>(c)), ::HIR::CoreType::U8})));
                                fieldPath.back()++;
                            }
                            fieldPath.pop_back();
                        } else {
                            ::std::vector<uint8_t> data;
                            data.reserve(s.size());
                            for (auto c : s) {
                                data.push_back(c);
                            }

                            this->push_rule(PatternRule::make_Value(mv$(data)));
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
                this->push_rule(PatternRule::make_Any({}));
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
    Ordering ord_rule_compatible(const PatternRule& a, const PatternRule& b) {
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
                return ::ord(ae.is_inclusive, be.is_inclusive);
            }
        }
        throw "";
    }

    bool rule_compatible(const PatternRule& a, const PatternRule& b) {
        return ord_rule_compatible(a, b) == OrdEqual;
    }

    bool rules_overlap(const PatternRule& a, const PatternRule& b) {
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
                return ae->as_Bytes().size() >= be->min_len;
            }
        }
        if (const auto* be = b.opt_Value(); be && be->is_Bytes()) {
            if (const auto* ae = a.opt_Slice()) {
                return be->as_Bytes().size() == ae->len;
            }
            if (const auto* ae = a.opt_SplitSlice()) {
                return be->as_Bytes().size() >= ae->min_len;
            }
        }

        // Checks if the value is within the righthand edge of the range
        auto is_within_right = [](const MIR::Constant& c, const PatternRule::Data_ValueRange& e) -> bool {
            return (e.is_inclusive ? c <= e.last : c < e.last);
        };

        // Value Range: Overlaps with contained values.
        if (const auto* ae = a.opt_ValueRange()) {
            if (const auto* be = b.opt_Value()) {
                return (ae->first <= *be && is_within_right(*be, *ae));
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
                ASSERT_BUG(Span(), ae->is_inclusive && be->is_inclusive, "TODO: Handle overlap with exclusive ranges: " << ae->first << ".." << (ae->is_inclusive ? "=" : "") << ae->last << " and " << be->first << ".." << (be->is_inclusive ? "=" : "") << be->last);
                assert(ae->is_inclusive && "TODO: Exclusive ranges");
                assert(be->is_inclusive && "TODO: Exclusive ranges");
                // Start of B within A
                if (ae->first <= be->first && is_within_right(be->first, *ae)) {
                    return true;
                }
                // End of B within A
                if (is_within_right(ae->first, *be) && be->last <= ae->last) { // TODO: Right-exclusive (if equal type then original check, otherwise complex)
                    return true;
                }
                // Start of A within B
                if (be->first <= ae->first && is_within_right(ae->first, *be)) {
                    return true;
                }
                // End of A within B
                if (is_within_right(be->first, *ae) && ae->last <= be->last) { // TODO: Right-exclusive
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
                if (be->is_inclusive) {
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
                return be->len >= ae->min_len;
            } else {
                TODO(Span(), "Check overlap of " << a << " and " << b);
            }
        }
        if (const auto* be = b.opt_SplitSlice()) {
            if (const auto* ae = a.opt_Slice()) {
                return ae->len >= be->min_len;
            } else {
                TODO(Span(), "Check overlap of " << a << " and " << b);
            }
        }

        // Otherwise, If rules are approximately equal, they overlap
        return (ord_rule_compatible(a, b) == OrdEqual);
    }
}

void sort_rulesets(RulesetRef rulesets, size_t idx) {
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
                if (rules_overlap(rulesets[i][idx], rulesets[i + 1][idx])) {
                    // Don't move
                } else if (ord_rule_compatible(rulesets[i][idx], rulesets[i + 1][idx]) == OrdGreater) {
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
            if (ord_rule_compatible(rulesets[i][idx], rulesets[start][idx]) != OrdEqual) {
                sort_rulesets_inner(rulesets.slice(start, i - start), idx);
                start = i;
            }
        }
        sort_rulesets_inner(rulesets.slice(start, rulesets.size() - start), idx);

        // Iterate onwards where rules are equal
        if (idx + 1 < rulesets[0].size()) {
            size_t start = 0;
            for (size_t i = 1; i < rulesets.size(); i++) {
                if (rulesets[i][idx] != rulesets[start][idx]) {
                    sort_rulesets(rulesets.slice(start, i - start), idx + 1);
                    start = i;
                }
            }
            sort_rulesets(rulesets.slice(start, rulesets.size() - start), idx + 1);
        }
    } else {
        if (idx + 1 < rulesets[0].size()) {
            sort_rulesets(rulesets, idx + 1);
        }
    }
}

void sort_rulesets_inner(RulesetRef rulesets, size_t idx) {
    TRACE_FUNCTION_F(idx << " - " << rulesets[0][idx].tag_str());
    if (const auto* re = rulesets[0][idx].opt_Variant()) {
        // Sort rules based on contents of enum
        if (re->sub_rules.size() > 0) {
            sort_rulesets(RulesetRef(rulesets, idx), 0);
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
        /*Out ->*/ ::HIR::TypeRef& out_ty,
        ::MIR::LValue& out_val
    ) {
        const StaticTraitResolve& resolve = builder.resolve();
        ::MIR::LValue lval = top_val.clone();
        ::HIR::TypeRef tmp_ty;
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
                        auto new_ty = resolve.is_type_owned_box(curTy);
                        ASSERT_BUG(sp, new_ty, "Deref on non-Box - " << curTy);
                        lval = ::MIR::LValue::newDeref(mv$(lval));
                        curTy = new_ty;
                        break;
                    }
                    auto monomorph_to_ptr = [&](const ::HIR::TypeData* ty) -> const ::HIR::TypeData* {
                        if (monomorphise_type_needed(ty)) {
                            auto rv = MonomorphStatePtr(resolve.crate.types, nullptr, &e.path.mData.as_Generic().mParams, nullptr).monomorph_type(sp, ty);
                            resolve.expandAssociatedTypes(sp, rv);
                            tmp_ty = mv$(rv);
                            return tmp_ty;
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
                                    curTy = monomorph_to_ptr(fld.ent);
                                    lval = ::MIR::LValue::newField(mv$(lval), idx);
                                }
                                TU_ARMA(Named, fields) {
                                    ASSERT_BUG(sp, idx < fields.size(), "Tuple struct index (" << idx << ") out of range (" << fields.size() << ") in " << curTy);
                                    const auto& fld = fields[idx];
                                    curTy = monomorph_to_ptr(fld.ty);
                                    lval = ::MIR::LValue::newField(mv$(lval), idx);
                                }
                    }
                        }
                        TU_ARMA(Union, pbe) {
                            ASSERT_BUG(sp, idx < pbe->mVariants.size(), "Union variant index (" << idx << ") out of range (" << pbe->mVariants.size() << ") in " << curTy);
                            const auto& fld = pbe->mVariants[idx];
                            curTy = monomorph_to_ptr(fld.ty);
                            lval = ::MIR::LValue::newDowncast(mv$(lval), idx);
                        }
                        TU_ARMA(Enum, pbe) {
                            ASSERT_BUG(sp, pbe->mData.is_Data(), "Value enum being destructured - " << curTy);
                            const auto& variants = pbe->mData.as_Data();
                            ASSERT_BUG(sp, idx < variants.size(), "Variant index (" << idx << ") out of range (" << variants.size() << ") for enum " << curTy);
                            const auto& var = variants[idx];

                            curTy = monomorph_to_ptr(var.type);
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
                        auto len_lval = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Usize), ::MIR::RValue::make_DstMeta({builder.getPtrToDst(sp, lval)}));
                        auto sub_val = ::MIR::Param(::MIR::Constant::make_Uint({U128(idx), ::HIR::CoreType::Usize}));
                        auto ofs_val = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Usize), ::MIR::RValue::make_BinOp({mv$(len_lval), ::MIR::eBinOp::SUB, mv$(sub_val)}));
                        // 2. Return _Index with that value
                        lval = ::MIR::LValue::newIndex(mv$(lval), ofs_val.as_Local());
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

        out_ty = curTy;
        out_val = mv$(lval);
    }
}

// --------------------------------------------------------------------
// Dumb and Simple
// --------------------------------------------------------------------

void MIRLowerHIRMatchSimple(MirBuilder& builder, MirConverter& conv, ::HIR::ExprNodeMatch& node, ::MIR::LValue match_val, t_arm_rules armRules, ::std::vector<ArmCode> arms_code, ::MIR::BasicBlockId firstCmpBlock) {
    TRACE_FUNCTION;

    // 1. Generate pattern matches
    builder.set_cur_block(firstCmpBlock);
    auto next_arm_bb = builder.new_bb_unlinked();
    size_t prev_arm_idx = !armRules.empty() ? armRules[0].armIdx : 0;
    for (const auto& pat_rule : armRules) {
        if (pat_rule.armIdx != prev_arm_idx) {
            DEBUG("New arm (" << prev_arm_idx << " -> " << pat_rule.armIdx << ")");
            prev_arm_idx = pat_rule.armIdx;
            builder.endBlock(::MIR::Terminator::make_Goto(next_arm_bb));
            builder.set_cur_block(next_arm_bb);
            next_arm_bb = builder.new_bb_unlinked();
        }
        const auto& arm = node.arms[pat_rule.armIdx];
        const auto& rc = arms_code[pat_rule.armIdx].rules[pat_rule.armRuleIdx];
        auto next_pattern_bb = builder.new_bb_unlinked();

        // 1. Check
        // - If the ruleset is empty, this is a _ arm over a value
        if (pat_rule.rules.size() > 0) {
            MIRLowerHIRMatchSimpleGeneratePattern(builder, arm.mCode->span(), pat_rule.rules.data(), pat_rule.rules.size(), node.mValue->resType, match_val, 0, next_pattern_bb);
        }
        builder.endBlock(::MIR::Terminator::make_Goto(rc.entry));

        // - Update the condition's failure target
        if (arms_code[pat_rule.armIdx].hasCondition && (pat_rule.armRuleIdx == 0 || rc.condFalse != arms_code[pat_rule.armIdx].rules[0].condFalse)) {
            builder.set_cur_block(rc.condFalse);
            // A guard belongs to this expanded pattern candidate, not to the
            // arm as a whole.  If it fails, another or-pattern candidate from
            // the same arm must still be tested before advancing to the next
            // arm.
            builder.endBlock(::MIR::Terminator::make_Goto(next_pattern_bb));
        }

        builder.set_cur_block(next_pattern_bb);
    }
    // - Kill the final pattern block (which is dead code)
    builder.endBlock(::MIR::Terminator::make_Unreachable({}));
    builder.set_cur_block(next_arm_bb);
    builder.endBlock(::MIR::Terminator::make_Unreachable({}));
}

int MIRLowerHIRMatchSimpleGeneratePattern(MirBuilder& builder, const Span& sp, const PatternRule* rules, unsigned int num_rules, const ::HIR::TypeData* top_ty, const ::MIR::LValue& top_val, unsigned int field_path_ofs, ::MIR::BasicBlockId failBb) {
    TRACE_FUNCTION_F("top_ty = " << top_ty << ", rules = [" << FMT_CB(os, for (size_t i = 0; i < num_rules; i++) os << rules[i] << ",";));
    for (unsigned int rule_idx = 0; rule_idx < num_rules; rule_idx++) {
        const auto& rule = rules[rule_idx];
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
                        bool test_val = rule.as_Bool();

                        auto succ_bb = builder.new_bb_unlinked();

                        if (test_val) {
                            builder.endBlock(::MIR::Terminator::make_If({val.clone(), succ_bb, failBb}));
                        } else {
                            builder.endBlock(::MIR::Terminator::make_If({val.clone(), failBb, succ_bb}));
                        }
                        builder.set_cur_block(succ_bb);
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
                                auto succ_bb = builder.new_bb_unlinked();

                                auto test_val = ::MIR::Param(::MIR::Constant::make_Uint({re.as_Uint().v, te}));
                                builder.push_stmt_assign(sp, builder.getIfCond(), ::MIR::RValue::make_BinOp({val.clone(), ::MIR::eBinOp::EQ, mv$(test_val)}));
                                builder.endBlock(::MIR::Terminator::make_If({builder.getIfCond(), succ_bb, failBb}));
                                builder.set_cur_block(succ_bb);
                            }
                            TU_ARMA(ValueRange, re) {
                                auto succ_bb = builder.new_bb_unlinked();

                                // IF `val` < `first` : fail_bb
                                if (re.first.as_Uint().v != 0) {
                                    auto test_bb_2 = builder.new_bb_unlinked();
                                    auto test_lt_val = ::MIR::Param(::MIR::Constant::make_Uint({re.first.as_Uint().v, te}));
                                    auto cmpLtLval = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), ::MIR::eBinOp::LT, mv$(test_lt_val)}));
                                    builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLtLval), failBb, test_bb_2}));

                                    builder.set_cur_block(test_bb_2);
                                }

                                // IF `val` > `last` : fail_bb
                                if (re.last.as_Uint().v == U128::max() && re.is_inclusive) {
                                    builder.endBlock(::MIR::Terminator::make_Goto({succ_bb}));
                                } else {
                                    auto test_gt_val = ::MIR::Param(::MIR::Constant::make_Uint({re.last.as_Uint().v, te}));
                                    auto op = re.is_inclusive ? ::MIR::eBinOp::GT : ::MIR::eBinOp::GE;
                                    auto cmpGtLval = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), op, mv$(test_gt_val)}));
                                    builder.endBlock(::MIR::Terminator::make_If({mv$(cmpGtLval), failBb, succ_bb}));
                                }

                                builder.set_cur_block(succ_bb);
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
                                auto succ_bb = builder.new_bb_unlinked();

                                auto test_val = ::MIR::Param(::MIR::Constant::make_Int({re.as_Int().v, te}));
                                auto cmpLval = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({val.clone(), ::MIR::eBinOp::EQ, mv$(test_val)}));
                                builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLval), succ_bb, failBb}));
                                builder.set_cur_block(succ_bb);
                            }
                            TU_ARMA(ValueRange, re) {
                                auto succ_bb = builder.new_bb_unlinked();

                                // IF `val` < `first` : fail_bb
                                if (re.first.as_Int().v != S128::min()) {
                                    auto test_bb_2 = builder.new_bb_unlinked();
                                    auto test_lt_val = ::MIR::Param(::MIR::Constant::make_Int({re.first.as_Int().v, te}));
                                    auto cmpLtLval = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), ::MIR::eBinOp::LT, mv$(test_lt_val)}));
                                    builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLtLval), failBb, test_bb_2}));
                                    builder.set_cur_block(test_bb_2);
                                }

                                // IF `val` > `last` : fail_bb
                                if (re.last.as_Int().v == S128::max() && re.is_inclusive) {
                                    builder.endBlock(::MIR::Terminator::make_Goto({succ_bb}));
                                } else {
                                    auto test_gt_val = ::MIR::Param(::MIR::Constant::make_Int({re.last.as_Int().v, te}));
                                    auto op = re.is_inclusive ? ::MIR::eBinOp::GT : ::MIR::eBinOp::GE;
                                    auto cmpGtLval = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), op, mv$(test_gt_val)}));
                                    builder.endBlock(::MIR::Terminator::make_If({mv$(cmpGtLval), failBb, succ_bb}));
                                }

                                builder.set_cur_block(succ_bb);
                            }
                }
                break;
            case ::HIR::CoreType::Char:
                TU_MATCH_DEF( PatternRule, (rule), (re),
                (
                    BUG(sp, "PatternRule for char is not Value or ValueRange");
                    ),
                (Value,
                    auto succ_bb = builder.new_bb_unlinked();

                    auto test_val = ::MIR::Param(::MIR::Constant::make_Uint({ re.as_Uint().v, te }));
                    auto cmpLval = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({ ::MIR::Param(val.clone()), ::MIR::eBinOp::EQ, mv$(test_val) }));
                    builder.endBlock( ::MIR::Terminator::make_If({ mv$(cmpLval), succ_bb, failBb }) );
                    builder.set_cur_block(succ_bb);
                    ),
                (ValueRange,
                    auto succ_bb = builder.new_bb_unlinked();

                    // IF `val` < `first` : fail_bb
                    if( re.first.as_Uint().v != 0 ) {
                            auto test_bb_2 = builder.new_bb_unlinked();

                            auto test_lt_val = ::MIR::Param(::MIR::Constant::make_Uint({re.first.as_Uint().v, te}));
                            auto cmpLtLval = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), ::MIR::eBinOp::LT, mv$(test_lt_val)}));
                            builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLtLval), failBb, test_bb_2}));

                            builder.set_cur_block(test_bb_2);
                    }

                    // IF `val` > `last` : fail_bb
                    if(re.last.as_Uint().v >= 0x10FFFF ) {
                            assert(re.is_inclusive);
                            builder.endBlock(::MIR::Terminator::make_Goto({succ_bb}));
                    }
                    else {
                            auto test_gt_val = ::MIR::Param(::MIR::Constant::make_Uint({re.last.as_Uint().v, te}));
                            auto op = re.is_inclusive ? ::MIR::eBinOp::GT : ::MIR::eBinOp::GE;
                            auto cmpGtLval = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), op, mv$(test_gt_val)}));
                            builder.endBlock(::MIR::Terminator::make_If({mv$(cmpGtLval), failBb, succ_bb}));
                    }

                    builder.set_cur_block(succ_bb);
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
                    auto succ_bb = builder.new_bb_unlinked();

                    auto test_val = ::MIR::Param(::MIR::Constant::make_Float({ re.as_Float().v, te }));
                    auto cmpLval = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({ val.clone(), ::MIR::eBinOp::EQ, mv$(test_val) }));
                    builder.endBlock( ::MIR::Terminator::make_If({ mv$(cmpLval), succ_bb, failBb }) );
                    builder.set_cur_block(succ_bb);
                    ),
                (ValueRange,
                    auto succ_bb = builder.new_bb_unlinked();

                    // IF `val` < `first` : fail_bb
                    if( re.first.as_Float().v == -std::numeric_limits<double>::infinity()) {
                    }
                    else {
                            auto test_bb_2 = builder.new_bb_unlinked();
                            auto test_lt_val = ::MIR::Param(::MIR::Constant::make_Float({re.first.as_Float().v, te}));
                            auto cmpLtLval = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), ::MIR::eBinOp::LT, mv$(test_lt_val)}));
                            builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLtLval), failBb, test_bb_2}));
                            builder.set_cur_block(test_bb_2);
                    }

                    // IF `val` > `last` : fail_bb
                    if( re.first.as_Float().v == std::numeric_limits<double>::infinity() && re.is_inclusive ) {
                            builder.endBlock(::MIR::Terminator::make_Goto({succ_bb}));
                    }
                    else {
                            auto test_gt_val = ::MIR::Param(::MIR::Constant::make_Float({re.last.as_Float().v, te}));
                            auto op = re.is_inclusive ? ::MIR::eBinOp::GT : ::MIR::eBinOp::GE;
                            auto cmpGtLval = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), op, mv$(test_gt_val)}));
                            builder.endBlock(::MIR::Terminator::make_If({mv$(cmpGtLval), failBb, succ_bb}));
                    }

                    builder.set_cur_block(succ_bb);
                    )
                )
                break;
            case ::HIR::CoreType::Str: {
                            ASSERT_BUG(sp, rule.is_Value() && rule.as_Value().is_StaticString(), "Unexpected use of non-value pattern on `str`");
                            const auto& v = rule.as_Value();
                            ASSERT_BUG(sp, val.is_Deref(), "");
                            val.wrappers.pop_back();
                            auto str_val = mv$(val);

                            auto succ_bb = builder.new_bb_unlinked();

                            auto test_val = ::MIR::Param(::MIR::Constant(v.as_StaticString()));
                            auto cmpLval = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({mv$(str_val), ::MIR::eBinOp::EQ, mv$(test_val)}));
                            builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLval), succ_bb, failBb}));
                            builder.set_cur_block(succ_bb);
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
                        const auto& str_data = pbe->mData;
                TU_MATCH_HDRA( (str_data), {)
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
                            auto rv = MonomorphStatePtr(builder.resolve().crate.types, nullptr, &te.path.mData.as_Generic().mParams, nullptr).monomorph_type(sp, ty);
                            builder.resolve().expandAssociatedTypes(sp, rv);
                            return rv;
                        };
                        ASSERT_BUG(sp, rule.is_Variant(), "Rule for enum isn't Any or Variant");
                        const auto& re = rule.as_Variant();
                        unsigned int var_idx = re.idx;

                        auto next_bb = builder.new_bb_unlinked();
                        auto var_count = pbe->num_variants();

                        // Generate a switch with only one option different.
                        ::std::vector<::MIR::BasicBlockId> arms(var_count, failBb);
                        arms[var_idx] = next_bb;
                        builder.endBlock(::MIR::Terminator::make_Switch({val.clone(), mv$(arms)}));

                        builder.set_cur_block(next_bb);

                        if (re.sub_rules.size() > 0) {
                            ASSERT_BUG(sp, pbe->mData.is_Data(), "Sub-rules present for non-data enum");
                            const auto& variants = pbe->mData.as_Data();
                            const auto& var_ty = variants.at(re.idx).type;
                            ::HIR::TypeRef tmp;
                            const auto& var_ty_m = (monomorphise_type_needed(var_ty) ? tmp = monomorph(var_ty) : var_ty);

                            // Recurse with the new ruleset
                            MIRLowerHIRMatchSimpleGeneratePattern(builder, sp, re.sub_rules.data(), re.sub_rules.size(), var_ty_m, ::MIR::LValue::newDowncast(val.clone(), var_idx), rule.field_path.size() + 1, failBb);
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
                    auto size_val = ::MIR::Constant::make_Uint({U128(rule.as_Value().as_Bytes().size()), ::HIR::CoreType::Usize});

                    auto succ_bb = builder.new_bb_unlinked();

                    ASSERT_BUG(sp, val.is_Deref(), "Slice pattern on non-Deref - " << val);
                    auto inner_val = val.cloneUnwrapped();

                    auto slice_rval = ::MIR::RValue::make_MakeDst({mv$(clonedVal), mv$(size_val)});
                    auto test_lval = builder.lvalue_or_temp(sp, builder.resolve().crate.types.borrow(::HIR::BorrowType::Shared, ty), mv$(slice_rval));
                    auto cmpLval = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({mv$(inner_val), ::MIR::eBinOp::EQ, mv$(test_lval)}));
                    builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLval), succ_bb, failBb}));
                    builder.set_cur_block(succ_bb);
                } else if (rule.is_Slice()) {
                    const auto& re = rule.as_Slice();

                    // Compare length
                    auto test_val = ::MIR::Param(::MIR::Constant::make_Uint({U128(re.len), ::HIR::CoreType::Usize}));
                    auto len_val = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Usize), ::MIR::RValue::make_DstMeta({builder.getPtrToDst(sp, val)}));
                    auto cmpLval = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({mv$(len_val), ::MIR::eBinOp::EQ, mv$(test_val)}));

                    auto len_succ_bb = builder.new_bb_unlinked();
                    builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLval), len_succ_bb, failBb}));
                    builder.set_cur_block(len_succ_bb);

                    // Recurse checking values
                    MIRLowerHIRMatchSimpleGeneratePattern(builder, sp, re.sub_rules.data(), re.sub_rules.size(), top_ty, top_val, field_path_ofs, failBb);
                } else if (rule.is_SplitSlice()) {
                    const auto& re = rule.as_SplitSlice();

                    // Compare length
                    auto test_val = ::MIR::Param(::MIR::Constant::make_Uint({U128(re.min_len), ::HIR::CoreType::Usize}));
                    auto len_val = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Usize), ::MIR::RValue::make_DstMeta({builder.getPtrToDst(sp, val)}));
                    auto cmpLval = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({mv$(len_val), ::MIR::eBinOp::LT, mv$(test_val)}));

                    auto len_succ_bb = builder.new_bb_unlinked();
                    builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLval), failBb, len_succ_bb})); // if len < test : FAIL
                    builder.set_cur_block(len_succ_bb);

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

class t_rules_subset {
    ::std::vector<const ::std::vector<PatternRule>*> rule_sets;
    bool is_arm_indexes;
    ::std::vector<size_t> armIdxes;

    static ::std::pair<size_t, size_t> decodeArmIdx(size_t v) {
        return ::std::make_pair(v & 0x3FFF, v >> 14);
    }

    static size_t encodeArmIdx(size_t armIdx, size_t pat_idx) {
        assert(armIdx <= 0x3FFF);
        assert(pat_idx <= 0x3FFF);
        return armIdx | (pat_idx << 14);
    }

public:
    t_rules_subset(size_t exp, bool is_arm_indexes)
        : is_arm_indexes(is_arm_indexes)
    {
        rule_sets.reserve(exp);
        armIdxes.reserve(exp);
    }

    size_t size() const {
        return rule_sets.size();
    }

    const ::std::vector<PatternRule>& operator[](size_t n) const {
        return *rule_sets[n];
    }

    bool is_arm() const {
        return is_arm_indexes;
    }

    struct ArmIdxes {
        size_t arm;
        size_t armRule;
    };

    ArmIdxes armIdx(size_t n) const {
        assert(is_arm_indexes);
        auto v = decodeArmIdx(armIdxes.at(n));
        return ArmIdxes{v.first, v.second};
    }

    ::MIR::BasicBlockId bbIdx(size_t n) const {
        assert(!is_arm_indexes);
        return armIdxes.at(n);
    }

    void sub_sort(size_t ofs, size_t start, size_t n) {
        ::std::vector<size_t> v;
        for (size_t i = 0; i < n; i++) {
            v.push_back(start + i);
        }
        // Sort rules based on just the value (ignore inner rules)
        ::std::stable_sort(v.begin(), v.end(), [&](auto a, auto b) {
            return ord_rule_compatible((*rule_sets[a])[ofs], (*rule_sets[b])[ofs]) == OrdLess;
        });

        // Reorder contents to above sorting
        {
            decltype(this->rule_sets) tmp;
            for (auto i : v) {
                tmp.push_back(rule_sets[i]);
            }
            ::std::copy(tmp.begin(), tmp.end(), rule_sets.begin() + start);
        }
        {
            decltype(this->armIdxes) tmp;
            for (auto i : v) {
                tmp.push_back(armIdxes[i]);
            }
            ::std::copy(tmp.begin(), tmp.end(), armIdxes.begin() + start);
        }
    }

    t_rules_subset sub_slice(size_t ofs, size_t n) {
        t_rules_subset rv{n, this->is_arm_indexes};
        rv.rule_sets.reserve(n);
        for (size_t i = 0; i < n; i++) {
            rv.rule_sets.push_back(this->rule_sets[ofs + i]);
            rv.armIdxes.push_back(this->armIdxes[ofs + i]);
        }
        return rv;
    }

    void push_arm(const ::std::vector<PatternRule>& x, size_t armIdx, size_t pat_idx) {
        assert(is_arm_indexes);
        rule_sets.push_back(&x);
        armIdxes.push_back(encodeArmIdx(armIdx, pat_idx));
    }

    void push_bb(const ::std::vector<PatternRule>& x, ::MIR::BasicBlockId bb) {
        assert(!is_arm_indexes);
        rule_sets.push_back(&x);
        armIdxes.push_back(bb);
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const t_rules_subset& x) {
        os << "t_rules_subset{";
        for (size_t i = 0; i < x.rule_sets.size(); i++) {
            if (i != 0) {
                os << ", ";
            }
            os << "[";
            if (x.is_arm_indexes) {
                auto v = decodeArmIdx(x.armIdxes[i]);
                os << v.first << "," << v.second;
            } else {
                os << "bb" << x.armIdxes[i];
            }
            os << "]";
            os << ": [" << *x.rule_sets[i] << "]";
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

    void genForSlice(t_rules_subset rules, size_t ofs, ::MIR::BasicBlockId defaultArm);
    void genDispatch(const ::std::vector<t_rules_subset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& armTargets, ::MIR::BasicBlockId defBlk);
    void genDispatchPrimitive(::HIR::TypeRef ty, ::MIR::LValue val, const ::std::vector<t_rules_subset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& armTargets, ::MIR::BasicBlockId defBlk);
    void genDispatchEnum(::HIR::TypeRef ty, ::MIR::LValue val, const ::std::vector<t_rules_subset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& armTargets, ::MIR::BasicBlockId defBlk);
    void genDispatchSlice(::HIR::TypeRef ty, ::MIR::LValue val, const ::std::vector<t_rules_subset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& armTargets, ::MIR::BasicBlockId defBlk);

    void genDispatchRange(const fieldPathT& field_path, const ::MIR::Constant& first, const ::MIR::Constant& last, bool is_inclusive, ::MIR::BasicBlockId defBlk);
    void genDispatchSplitslice(const fieldPathT& field_path, const PatternRule::Data_SplitSlice& e, ::MIR::BasicBlockId defBlk);

    ::MIR::LValue push_compare(::MIR::LValue left, ::MIR::eBinOp op, ::MIR::Param right) {
        return builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({mv$(left), op, mv$(right)}));
    }
};

namespace {
    void appendRuleColumns(::std::vector<PatternRule>& out_rules, PatternRule rule) {
        TU_MATCH_HDRA( (rule), {)
        TU_ARMA(Variant, e) {
                auto sub_rules = mv$(e.sub_rules);
                out_rules.push_back(mv$(rule));
                for (auto& sr : sub_rules) {
                    appendRuleColumns(out_rules, mv$(sr));
                }
            }
            TU_ARMA(Slice, e) {
                auto sub_rules = mv$(e.sub_rules);
                out_rules.push_back(mv$(rule));
                for (auto& sr : sub_rules) {
                    appendRuleColumns(out_rules, mv$(sr));
                }
            }
            TU_ARMA(SplitSlice, e) {
                auto leading = mv$(e.leading);
                auto trailing = mv$(e.trailing);
                auto idx = out_rules.size();
                out_rules.push_back(mv$(rule));
                for (auto& sr : leading) {
                    appendRuleColumns(out_rules, mv$(sr));
                }
                // Trailing rules are complex as they break the assumption that patterns across the same type share a prefix
                // - So, flatten them into the "flattened" rule
                for (auto& sr : trailing) {
                    appendRuleColumns(out_rules[idx].as_SplitSlice().trailing, mv$(sr));
                }
            }
            TU_ARMA(Bool, e) {
                out_rules.push_back(mv$(rule));
            }
            TU_ARMA(Value, e) {
                out_rules.push_back(mv$(rule));
            }
            TU_ARMA(ValueRange, e) {
                out_rules.push_back(mv$(rule));
            }
            TU_ARMA(Any, e) {
                out_rules.push_back(mv$(rule));
            }
        }
    }

    t_arm_rules linearize_rule_columns(t_arm_rules rules) {
        t_arm_rules rv;
        rv.reserve(rules.size());
        for (auto& ruleset : rules) {
            ::std::vector<PatternRule> pattern_rules;
            for (auto& r : ruleset.rules) {
                appendRuleColumns(pattern_rules, mv$(r));
            }
            rv.push_back(PatternRuleset{ruleset.armIdx, ruleset.armRuleIdx, mv$(pattern_rules)});
        }
        return rv;
    }
}

void MIRLowerHIRMatchGrouped(MirBuilder& builder, MirConverter& conv, const Span& sp, const HIR::TypeData* match_ty, ::MIR::LValue match_val, t_arm_rules armRules, ::std::vector<ArmCode> arms_code, ::MIR::BasicBlockId firstCmpBlock) {
    TRACE_FUNCTION_F("");

    // The grouped matcher consumes one constructor or field test per matrix
    // column. Keep each outer constructor before the payload columns and retain
    // the full field path on every test.
    armRules = linearize_rule_columns(mv$(armRules));

    // - Create a "slice" of the passed rules, suitable for passing to the recursive part of the algo
    t_rules_subset rules{armRules.size(), /*is_arm_indexes=*/true};
    for (const auto& r : armRules) {
        rules.push_arm(r.rules, r.armIdx, r.armRuleIdx);
    }

    auto inst = MatchGenGrouped{builder, sp, match_ty, match_val, arms_code, 0};

    // NOTE: This block should never be used
    auto defaultArm = builder.new_bb_unlinked();

    builder.set_cur_block(firstCmpBlock);
    inst.genForSlice(mv$(rules), 0, defaultArm);

    // Make the default infinite loop.
    // - Preferably, it'd abort.
    builder.set_cur_block(defaultArm);
    builder.endBlock(::MIR::Terminator::make_Unreachable({}));
}

void MatchGenGrouped::genForSlice(t_rules_subset armRules, size_t ofs, ::MIR::BasicBlockId defaultArm) {
    TRACE_FUNCTION_F("arm_rules=" << armRules << ", ofs=" << ofs << ", default_arm=" << defaultArm);
    ASSERT_BUG(sp, armRules.size() > 0, "");

    // Leading wildcard-only columns cannot discriminate between these rows.
    for (;;) {
        bool is_all_any = true;
        for (size_t i = 0; i < armRules.size() && is_all_any; i++) {
            if (armRules[i].size() <= ofs) {
                is_all_any = false;
            } else if (!armRules[i][ofs].is_Any()) {
                is_all_any = false;
            }
        }
        if (!is_all_any) {
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
            if (armRules.is_arm()) {
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
        bool stopped_at_overlap = false;
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
                if (!rule_compatible(armRules[prev][ofs], armRules[idx][ofs])
                    && rules_overlap(armRules[prev][ofs], armRules[idx][ofs])) {
                    stopped_at_overlap = true;
                    break;
                }
            }
            if (stopped_at_overlap) {
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
            auto next = (has_default ? builder.new_bb_unlinked() : defaultArm);

            // Sort rules before getting compatible runs
            // TODO: Is this a valid operation?
            armRules.sub_sort(ofs, start, firstAny - start);

            // Create list of compatible arm slices (runs with the same selector value)
            ::std::vector<t_rules_subset> slices;
            auto curTest = start;
            for (auto i = start; i < firstAny; i++) {
                // Just check if the decision value differs (don't check nested rules)
                if (!rule_compatible(armRules[i][ofs], armRules[curTest][ofs])) {
                    slices.push_back(armRules.sub_slice(curTest, i - curTest));
                    curTest = i;
                }
            }
            slices.push_back(armRules.sub_slice(curTest, firstAny - curTest));
            DEBUG("- " << slices.size() << " groupings");
            ::std::vector<::MIR::BasicBlockId> armBlocks;
            armBlocks.reserve(slices.size());

            auto curBlk = builder.pause_cur_block();
            // > Stable sort list
            ::std::sort(slices.begin(), slices.end(), [&](const auto& a, const auto& b) {
                return a[0][ofs] < b[0][ofs];
            });
            // TODO: Should this do a stable sort of inner patterns too?
            // - A sort of inner patterns such that `_` (and range?) patterns don't change position.

            // > Get type of match, generate dispatch list.
            for (size_t i = 0; i < slices.size(); i++) {
                auto curBlock = builder.new_bb_unlinked();
                builder.set_cur_block(curBlock);

                for (size_t j = 0; j < slices[i].size(); j++) {
                    if (j > 0) {
                        ASSERT_BUG(sp, slices[i][0][ofs] == slices[i][j][ofs], "Mismatched rules - " << slices[i][0][ofs] << " and " << slices[i][j][ofs]);
                    }
                    armBlocks.push_back(curBlock);
                }

                this->genForSlice(slices[i], ofs + 1, next);
            }

            builder.set_cur_block(curBlk);

            // Generate decision code
            this->genDispatch(slices, ofs, armBlocks, next);

            if (has_default) {
                builder.set_cur_block(next);
            }
        }

        if (stopped_at_overlap) {
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
            auto next = (hasNext ? builder.new_bb_unlinked() : defaultArm);

            const auto& rule = armRules[firstAny][ofs];
            if (const auto* e = rule.opt_ValueRange()) {
                // Generate branch based on range
                this->genDispatchRange(armRules[firstAny][ofs].field_path, e->first, e->last, e->is_inclusive, next);
            } else if (const auto* e = rule.opt_SplitSlice()) {
                // Generate branch based on slice length being at least required.
                this->genDispatchSplitslice(rule.field_path, *e, next);
            } else {
                ASSERT_BUG(sp, rule.is_Any(), "Didn't expect non-Any rule here, got " << rule.tag_str() << " " << rule);
            }

            // Step deeper into these arms
            auto slice = armRules.sub_slice(firstAny, idx - firstAny);
            this->genForSlice(mv$(slice), ofs + 1, next);

            if (hasNext) {
                builder.set_cur_block(next);
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
void MatchGenGrouped::genDispatch(const ::std::vector<t_rules_subset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& armTargets, ::MIR::BasicBlockId defBlk) {
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
                    const auto& str_data = pbe->mData;
            TU_MATCH_HDRA( (str_data), {)
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
            size_t tgt_ofs = 0;
            for (size_t i = 0; i < rules.size(); i++) {
                for (size_t j = 1; j < rules[i].size(); j++) {
                    ASSERT_BUG(sp, armTargets[tgt_ofs] == armTargets[tgt_ofs + j], "Mismatched target blocks for Value match");
                }

                const auto& r = rules[i][0][ofs];
                ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tag_str());
                const auto& re = r.as_Value();
                if (re.is_Const()) {
                    TODO(sp, "Handle Constant::Const in match");
                }

                targets.push_back(armTargets[tgt_ofs]);
                values.push_back(re.as_Bytes());

                tgt_ofs += rules[i].size();
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
            auto val_usize = builder.new_temporary(builder.resolve().crate.types.primitive(HIR::CoreType::Usize));
            builder.push_stmt_assign(sp, val_usize.clone(), ::MIR::RValue::make_Cast({mv$(val), builder.resolve().crate.types.primitive(::HIR::CoreType::Usize)}));
            this->genDispatchPrimitive(builder.resolve().crate.types.primitive(HIR::CoreType::Usize), mv$(val_usize), rules, ofs, armTargets, defBlk);
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
    void push_if_equal(const Span& sp, MirBuilder& builder, ::MIR::LValue val, ::MIR::Param test_val, ::MIR::BasicBlockId bbTrue, ::MIR::BasicBlockId bbFalse) {
        auto cmpLval = builder.getRvalInIfCond(sp, ::MIR::RValue::make_BinOp({mv$(val), ::MIR::eBinOp::EQ, mv$(test_val)}));
        builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLval), bbTrue, bbFalse}));
    }
}

void MatchGenGrouped::genDispatchPrimitive(::HIR::TypeRef ty, ::MIR::LValue val, const ::std::vector<t_rules_subset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& armTargets, ::MIR::BasicBlockId defBlk) {
    auto te = ty->as_Primitive();
    switch (te) {
        case ::HIR::CoreType::Bool: {
            ASSERT_BUG(sp, rules.size() <= 2, "More than 2 rules for boolean");
            for (size_t i = 0; i < rules.size(); i++) {
                ASSERT_BUG(sp, rules[i][0][ofs].is_Bool(), "PatternRule for bool isn't _Bool");
            }

            // False sorts before true.
            auto failBb = rules.size() == 2 ? armTargets[0] : (rules[0][0][ofs].as_Bool() ? defBlk : armTargets[0]);
            auto succ_bb = rules.size() == 2 ? armTargets[rules[0].size()] : (rules[0][0][ofs].as_Bool() ? armTargets[0] : defBlk);

            builder.endBlock(::MIR::Terminator::make_If({mv$(val), succ_bb, failBb}));
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
                ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tag_str());
                const auto& re = r.as_Value();
                push_if_equal(sp, builder, mv$(val), ::MIR::Param(re.clone()), armTargets[0], defBlk);
            } else {
                // NOTE: Rules are currently sorted
                // TODO: If there are Constant::Const values in the list, they need to come first! (with equality checks)

                ::std::vector<::std::pair<::MIR::Constant, ::MIR::BasicBlockId>> large_values;
                ::std::vector<uint64_t> values;
                ::std::vector<::MIR::BasicBlockId> targets;
                size_t tgt_ofs = 0;
                for (size_t i = 0; i < rules.size(); i++) {
                    for (size_t j = 1; j < rules[i].size(); j++) {
                        ASSERT_BUG(sp, armTargets[tgt_ofs] == armTargets[tgt_ofs + j], "Mismatched target blocks for Value match");
                    }

                    const auto& r = rules[i][0][ofs];
                    ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tag_str());
                    const auto& re = r.as_Value();
                    if (re.is_Const()) {
                        // Inject a `If` chained to a new block
                        auto next_block = builder.new_bb_unlinked();
                        push_if_equal(sp, builder, val.clone(), ::MIR::Param(re.clone()), armTargets[tgt_ofs], next_block);
                        builder.set_cur_block(next_block);
                    } else if (re.as_Uint().v > U128(UINT64_MAX)) {
                        large_values.push_back(std::make_pair(re.clone(), armTargets[tgt_ofs]));
                    } else {
                        values.push_back(re.as_Uint().v.truncate_u64());
                        targets.push_back(armTargets[tgt_ofs]);
                    }

                    tgt_ofs += rules[i].size();
                }
                // If there were any values that don't fit in u64, then emit those as a chain of `if` terminators
                if (!large_values.empty()) {
                    auto tail_block = builder.new_bb_unlinked();
                    builder.endBlock(::MIR::Terminator::make_SwitchValue({val.clone(), tail_block, mv$(targets), ::MIR::SwitchValues(mv$(values))}));
                    builder.set_cur_block(tail_block);
                    for (auto& v : large_values) {
                        auto next_block = builder.new_bb_unlinked();
                        push_if_equal(sp, builder, val.clone(), mv$(v.first), v.second, next_block);
                        builder.set_cur_block(next_block);
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
                ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tag_str());
                const auto& re = r.as_Value();
                push_if_equal(sp, builder, mv$(val), ::MIR::Param(re.clone()), armTargets[0], defBlk);
            } else {
                // NOTE: Rules are currently sorted
                // TODO: If there are Constant::Const values in the list, they need to come first! (with equality checks)

                ::std::vector<int64_t> values;
                ::std::vector<::MIR::BasicBlockId> targets;
                size_t tgt_ofs = 0;
                for (size_t i = 0; i < rules.size(); i++) {
                    for (size_t j = 1; j < rules[i].size(); j++) {
                        ASSERT_BUG(sp, armTargets[tgt_ofs] == armTargets[tgt_ofs + j], "Mismatched target blocks for Value match");
                    }

                    const auto& r = rules[i][0][ofs];
                    ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tag_str());
                    const auto& re = r.as_Value();
                    if (re.is_Const()) {
                        TODO(sp, "Handle Constant::Const in match");
                    }

                    if (re.as_Int().v > S128(INT64_MAX) || re.as_Int().v < S128(INT64_MIN)) {
                        TODO(sp, "Handle 128-bit values in SwitchValue");
                    }
                    values.push_back(re.as_Int().v.truncate_i64());
                    targets.push_back(armTargets[tgt_ofs]);

                    tgt_ofs += rules[i].size();
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
            size_t tgt_ofs = 0;
            for (size_t i = 0; i < rules.size(); i++) {
                for (size_t j = 1; j < rules[i].size(); j++) {
                    ASSERT_BUG(sp, armTargets[tgt_ofs] == armTargets[tgt_ofs + j], "Mismatched target blocks for Value match");
                }

                const auto& r = rules[i][0][ofs];
                ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tag_str());
                const auto& re = r.as_Value();
                if (re.is_Const()) {
                    TODO(sp, "Handle Constant::Const in match");
                }

                // IF v < tst : def_blk
                {
                    auto cmpEqBlk = builder.new_bb_unlinked();
                    auto cmpLvalLt = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({val.clone(), ::MIR::eBinOp::LT, ::MIR::Param(re.clone())}));
                    builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLvalLt), defBlk, cmpEqBlk}));
                    builder.set_cur_block(cmpEqBlk);
                }

                // IF v == tst : target
                {
                    auto next_cmp_blk = builder.new_bb_unlinked();
                    auto cmpLvalEq = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({val.clone(), ::MIR::eBinOp::EQ, ::MIR::Param(re.clone())}));
                    builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLvalEq), armTargets[tgt_ofs], next_cmp_blk}));
                    builder.set_cur_block(next_cmp_blk);
                }

                tgt_ofs += rules[i].size();
            }
            builder.endBlock(::MIR::Terminator::make_Goto(defBlk));
        } break;
        case ::HIR::CoreType::Str: {
            // Remove the deref on the &str
            ASSERT_BUG(sp, !val.wrappers.empty() && val.wrappers.back().is_Deref(), "&str match on non-Deref lvalue - " << val);
            val.wrappers.pop_back();

            ::std::vector<::MIR::BasicBlockId> targets;
            ::std::vector<::std::string> values;
            size_t tgt_ofs = 0;
            for (size_t i = 0; i < rules.size(); i++) {
                for (size_t j = 1; j < rules[i].size(); j++) {
                    ASSERT_BUG(sp, armTargets[tgt_ofs] == armTargets[tgt_ofs + j], "Mismatched target blocks for Value match");
                }

                const auto& r = rules[i][0][ofs];
                ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tag_str());
                const auto& re = r.as_Value();
                if (re.is_Const()) {
                    TODO(sp, "Handle Constant::Const in match");
                }

                targets.push_back(armTargets[tgt_ofs]);
                values.push_back(re.as_StaticString());

                tgt_ofs += rules[i].size();
            }
            builder.endBlock(::MIR::Terminator::make_SwitchValue({mv$(val), defBlk, mv$(targets), ::MIR::SwitchValues(mv$(values))}));
        } break;
    }
}

void MatchGenGrouped::genDispatchEnum(::HIR::TypeRef ty, ::MIR::LValue val, const ::std::vector<t_rules_subset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& armTargets, ::MIR::BasicBlockId defBlk) {
    TRACE_FUNCTION;
    auto& te = ty->as_Path();
    const auto& pbe = te.binding.as_Enum();

    auto decisonArm = builder.pause_cur_block();

    auto var_count = pbe->num_variants();
    ::std::vector<::MIR::BasicBlockId> arms(var_count, defBlk);
    size_t armIdx = 0;
    for (size_t i = 0; i < rules.size(); i++) {
        ASSERT_BUG(sp, rules[i][0][ofs].is_Variant(), "Rule for enum isn't Any or Variant - " << rules[i][0][ofs].tag_str());
        const auto& re = rules[i][0][ofs].as_Variant();
        unsigned int var_idx = re.idx;
        DEBUG("Variant " << var_idx);

        ASSERT_BUG(sp, re.sub_rules.size() == 0, "Sub-rules in MatchGenGrouped");

        arms[var_idx] = armTargets[armIdx];
        for (size_t j = 0; j < rules[i].size(); j++) {
            assert(arms[var_idx] == armTargets[armIdx]);
            armIdx++;
        }
    }

    builder.set_cur_block(decisonArm);
    builder.endBlock(::MIR::Terminator::make_Switch({mv$(val), mv$(arms)}));
}

void MatchGenGrouped::genDispatchSlice(::HIR::TypeRef ty, ::MIR::LValue val, const ::std::vector<t_rules_subset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& armTargets, ::MIR::BasicBlockId defBlk) {
    auto val_len = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Usize), ::MIR::RValue::make_DstMeta({builder.getPtrToDst(sp, val)}));

    // TODO: Re-sort the rules list to interleve Constant::Bytes and Slice

    // Just needs to check the lengths, then dispatch.
    size_t tgt_ofs = 0;
    for (size_t i = 0; i < rules.size(); i++) {
        const auto& r = rules[i][0][ofs];
        if (const auto* re = r.opt_Slice()) {
            ASSERT_BUG(sp, re->sub_rules.size() == 0, "Sub-rules in MatchGenGrouped");
            auto val_tst = ::MIR::Constant::make_Uint({U128(re->len), ::HIR::CoreType::Usize});

            for (size_t j = 0; j < rules[i].size(); j++) {
                assert(armTargets[tgt_ofs] == armTargets[tgt_ofs + j]);
            }

            // IF v < tst : target
            if (re->len > 0) {
                auto cmpEqBlk = builder.new_bb_unlinked();
                auto cmpLvalLt = this->push_compare(val_len.clone(), ::MIR::eBinOp::LT, val_tst.clone());
                builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLvalLt), defBlk, cmpEqBlk}));
                builder.set_cur_block(cmpEqBlk);
            }

            // IF v == tst : target
            {
                auto next_cmp_blk = builder.new_bb_unlinked();
                auto cmpLvalEq = this->push_compare(val_len.clone(), ::MIR::eBinOp::EQ, mv$(val_tst));
                builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLvalEq), armTargets[tgt_ofs], next_cmp_blk}));
                builder.set_cur_block(next_cmp_blk);
            }
        } else if (const auto* re = r.opt_Value()) {
            ASSERT_BUG(sp, re->is_Bytes(), "Slice with non-Bytes value - " << *re);
            const auto& b = re->as_Bytes();

            auto val_tst_len = ::MIR::Constant::make_Uint({U128(b.size()), ::HIR::CoreType::Usize});

            // IF v == tst : target
            {
                auto next_cmp_blk = builder.new_bb_unlinked();

                // TODO: What if `val` isn't a Deref?
                ASSERT_BUG(sp, !val.wrappers.empty() && val.wrappers.back().is_Deref(), "TODO: Handle non-Deref matches of byte strings - " << val);
                auto& types = builder.resolve().crate.types;
                auto cmpSliceVal = builder.lvalue_or_temp(sp, types.borrow(::HIR::BorrowType::Shared, types.slice(types.primitive(::HIR::CoreType::U8))), ::MIR::RValue::make_MakeDst({::MIR::Param(re->clone()), val_tst_len.clone()}));
                auto cmpLvalEq = this->push_compare(val.cloneUnwrapped(), ::MIR::eBinOp::EQ, mv$(cmpSliceVal));
                builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLvalEq), armTargets[tgt_ofs], next_cmp_blk}));

                builder.set_cur_block(next_cmp_blk);
            }
        } else {
            BUG(sp, "Matching without _Slice pattern - " << r.tag_str() << " - " << r);
        }

        tgt_ofs += rules[i].size();
    }
    builder.endBlock(::MIR::Terminator::make_Goto(defBlk));
}

void MatchGenGrouped::genDispatchRange(const fieldPathT& field_path, const ::MIR::Constant& first, const ::MIR::Constant& last, bool is_inclusive, ::MIR::BasicBlockId defBlk) {
    TRACE_FUNCTION_F("field_path=" << field_path << ", " << first << " .." << (is_inclusive ? "=" : "") << " " << last);
    ::MIR::LValue val;
    ::HIR::TypeRef ty;
    getTyAndVal(sp, builder, topTy, topVal, field_path, fieldPathOfs, ty, val);
    DEBUG("ty = " << ty << ", val = " << val);

    if (const auto* tep = ty->opt_Primitive()) {
        auto te = *tep;

        bool lower_possible = true;
        bool upper_possible = true;

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
                lower_possible = (first.as_Uint().v > 0);
                // TODO: Should this also check for the end being the max value of the type?
                // - Can just leave that to the optimiser
                upper_possible = is_inclusive ? (last.as_Uint().v < U128::max()) : true;
                break;
            case ::HIR::CoreType::I8:
            case ::HIR::CoreType::I16:
            case ::HIR::CoreType::I32:
            case ::HIR::CoreType::I64:
            case ::HIR::CoreType::I128:
            case ::HIR::CoreType::Isize:
                lower_possible = (first.as_Int().v > S128::min());
                upper_possible = is_inclusive ? (last.as_Int().v < S128::max()) : true;
                break;
            case ::HIR::CoreType::Char:
                lower_possible = (first.as_Uint().v > 0);
                upper_possible = is_inclusive ? (last.as_Uint().v <= 0x10FFFF) : (last.as_Uint().v < 0x10FFFF);
                break;
            case ::HIR::CoreType::F16:
            case ::HIR::CoreType::F32:
            case ::HIR::CoreType::F64:
            case ::HIR::CoreType::F128:
                // NOTE: No upper or lower limits
                lower_possible = (first.as_Float().v > -std::numeric_limits<double>::infinity());
                upper_possible = (last.as_Float().v < std::numeric_limits<double>::infinity());
                break;
        }

        if (lower_possible) {
            auto test_bb_2 = builder.new_bb_unlinked();
            // IF `val` < `first` : fail_bb
            auto cmpLtLval = builder.getRvalInIfCond(sp, ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), ::MIR::eBinOp::LT, ::MIR::Param(first.clone())}));
            builder.endBlock(::MIR::Terminator::make_If({mv$(cmpLtLval), defBlk, test_bb_2}));

            builder.set_cur_block(test_bb_2);
        }

        if (upper_possible) {
            auto succ_bb = builder.new_bb_unlinked();

            // IF `val` > `last` : fail_bb
            auto op = is_inclusive ? ::MIR::eBinOp::GT : ::MIR::eBinOp::GE;
            auto cmpGtLval = builder.getRvalInIfCond(sp, ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), op, ::MIR::Param(last.clone())}));
            builder.endBlock(::MIR::Terminator::make_If({mv$(cmpGtLval), defBlk, succ_bb}));

            builder.set_cur_block(succ_bb);
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
    auto val_len = builder.lvalue_or_temp(sp, builder.resolve().crate.types.primitive(::HIR::CoreType::Usize), ::MIR::RValue::make_DstMeta({builder.getPtrToDst(sp, val)}));

    // 1. Check that length is sufficient for the pattern to be used
    // `IF len < min_len : def_blk, next
    {
        auto next = builder.new_bb_unlinked();
        auto cmpVal = this->push_compare(val_len.clone(), ::MIR::eBinOp::LT, ::MIR::Constant::make_Uint({U128(e.min_len), ::HIR::CoreType::Usize}));
        builder.endBlock(::MIR::Terminator::make_If({mv$(cmpVal), defBlk, next}));
        builder.set_cur_block(next);
    }

    // 2. Recurse into leading patterns.
    // TODO: This is dead code (leading patterns should have been expanded, and there's an assert above for it)
    if (e.min_len > e.trailing_len) {
        auto next = builder.new_bb_unlinked();
        auto inner_set = t_rules_subset{1, /*is_arm_indexes=*/false};
        inner_set.push_bb(e.leading, next);
        auto inst = MatchGenGrouped{builder, sp, ty, val, {}, field_path.size()};
        inst.genForSlice(inner_set, 0, defBlk);

        builder.set_cur_block(next);
    }

    // 3. Recurse into trailing patterns
    if (e.trailing_len != 0) {
        auto next = builder.new_bb_unlinked();
        auto inner_set = t_rules_subset{1, /*is_arm_indexes=*/false};
        inner_set.push_bb(e.trailing, next);
        auto inst = MatchGenGrouped{builder, sp, ty, val, {}, field_path.size()};
        inst.genForSlice(inner_set, 0, defBlk);

        builder.set_cur_block(next);
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

    set_cur_block(new_bb_unlinked());
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

    ifCondLval = this->new_temporary(mResolve.crate.types.primitive(::HIR::CoreType::Bool));

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
            terminate_scope_early(sp, fcn_scope());
            // Validation fails if this is reachable.
            //end_block( ::MIR::Terminator::make_Incomplete({}) );
            endBlock(::MIR::Terminator::make_Unreachable({}));
        } else {
            if (hasResult()) {
                push_stmt_assign(sp, ::MIR::LValue::newReturn(), getResult(sp));
            }

            terminate_scope_early(sp, fcn_scope());

            endBlock(::MIR::Terminator::make_Return({}));
        }
    } else {
        terminate_scope(sp, ScopeHandle(*this, 1), /*emit_cleanup=*/false);
        terminate_scope(sp, mv$(fcnScope), /*emit_cleanup=*/false);
    }

    // Rewrite drop flags
    // - Expand recursive lookups
    for (;;) {
        bool added = false;
        for (auto& a : dropFlagAliases) {
            auto& mapped_flags = a.second;
            // Iterate every "destination" flag
            for (size_t i = 0; i < mapped_flags.size(); i++) {
                auto it2 = dropFlagAliases.find(mapped_flags[i]);
                if (it2 != dropFlagAliases.end()) {
                    for (unsigned other_flag : it2->second) {
                        // If this flag is not in the current list, add it and mark that something changed
                        if (std::find(mapped_flags.begin(), mapped_flags.end(), other_flag) == mapped_flags.end()) {
                            mapped_flags.push_back(other_flag);
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
                    for (unsigned other_idx : dfIt->second) {
                        v.idx = other_idx;
                        // Ensure that `it` always points to the original
                        it = b.statements.insert(it, ::MIR::Statement(v)) + 1;
                    }
                }
            }
        }
    }
}

const ::HIR::TypeData* MirBuilder::is_type_owned_box(const ::HIR::TypeData* ty) const {
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

void MirBuilder::schedule_variable_drop(unsigned int idx) {
    register_variable_state(idx);
    schedule_registered_variable_drop(idx);
}

void MirBuilder::register_variable_state(unsigned int idx) {
    DEBUG("REGISTER STATE (var) _" << idx << ": " << output.locals.at(idx));
    for (auto scope_idx : ::reverse(scopeStack)) {
        auto& scope_def = scopes.at(scope_idx);
        TU_MATCH_DEF(
            ScopeType,
            (scope_def.data),
            (e),
            (),
            (Owning,
             if (!e.is_temporary) {
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

void MirBuilder::schedule_registered_variable_drop(unsigned int idx) {
    DEBUG("SCHEDULE DROP (var) _" << idx << ": " << output.locals.at(idx));
    for (auto scope_idx : ::reverse(scopeStack)) {
        auto& scope_def = scopes.at(scope_idx);
        TU_MATCH_DEF(
            ScopeType,
            (scope_def.data),
            (e),
            (),
            (Owning,
             if (!e.is_temporary) {
                 auto state_it = ::std::find(e.slots.begin(), e.slots.end(), idx);
                 assert(state_it != e.slots.end());
                 auto dropIt = ::std::find_if(e.dropSlots.begin(), e.dropSlots.end(), [&](const ScopeDropSlot& slot) {
                     return !slot.is_argument && slot.index == idx;
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

void MirBuilder::schedule_argument_drop(unsigned int idx) {
    DEBUG("SCHEDULE DROP (arg) a" << idx << ": " << mArgs.at(idx).second);
    for (auto scope_idx : ::reverse(scopeStack)) {
        auto& scope_def = scopes.at(scope_idx);
        TU_MATCH_DEF(
            ScopeType,
            (scope_def.data),
            (e),
            (),
            (Owning,
             if (!e.is_temporary) {
                 auto it = ::std::find_if(e.dropSlots.begin(), e.dropSlots.end(), [&](const ScopeDropSlot& slot) {
                     return slot.is_argument && slot.index == idx;
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

void MirBuilder::move_temporary_drop_to_variable_scope(const Span& sp, const ::MIR::LValue& value, const ScopeHandle& source) {
    if (!value.root.is_Local() || !value.wrappers.empty()) {
        return;
    }
    const auto idx = value.root.as_Local();
    if (idx < firstTempIdx) {
        return;
    }

    ASSERT_BUG(sp, source.idx < scopes.size(), "Invalid temporary scope " << source);
    auto& source_scope = scopes.at(source.idx);
    ASSERT_BUG(sp, source_scope.data.is_Owning() && source_scope.data.as_Owning().is_temporary, "Drop source is not a temporary scope: " << source);
    auto& source_owning = source_scope.data.as_Owning();
    auto& source_drops = source_owning.dropSlots;
    auto source_it = ::std::find_if(source_drops.begin(), source_drops.end(), [&](const ScopeDropSlot& slot) {
        return !slot.is_argument && slot.index == idx;
    });
    if (source_it == source_drops.end()) {
        return;
    }
    auto source_state_it = ::std::find(source_owning.slots.begin(), source_owning.slots.end(), idx);
    ASSERT_BUG(sp, source_state_it != source_owning.slots.end(), "Missing state owner for " << value);

    bool source_seen = false;
    for (auto scope_idx : ::reverse(scopeStack)) {
        if (scope_idx == source.idx) {
            source_seen = true;
            continue;
        }
        if (!source_seen) {
            continue;
        }
        auto& scope = scopes.at(scope_idx);
        if (auto* owning = scope.data.opt_Owning()) {
            if (!owning->is_temporary) {
                auto target_state_it = ::std::find(owning->slots.begin(), owning->slots.end(), idx);
                ASSERT_BUG(sp, target_state_it == owning->slots.end(), "Duplicate state owner for " << value);
                source_owning.slots.erase(source_state_it);
                source_drops.erase(source_it);
                owning->slots.push_back(idx);
                owning->dropSlots.push_back(ScopeDropSlot{false, idx});
                DEBUG("MOVE DROP " << value << " from scope " << source.idx << " to scope " << scope_idx);
                return;
            }
        }
    }
    BUG(sp, "No variable scope outside temporary scope " << source);
}

void MirBuilder::move_variable_to_scope(const Span& sp, unsigned int idx, const ScopeHandle& target) {
    ASSERT_BUG(sp, target.idx < scopes.size(), "Invalid `super let` target scope " << target);
    auto& target_scope = scopes.at(target.idx);
    ASSERT_BUG(sp, target_scope.data.is_Owning(), "`super let` target is not an owning scope: " << target);

    ScopeType::Data_Owning* source = nullptr;
    for (auto scope_idx : ::reverse(scopeStack)) {
        auto* owning = scopes.at(scope_idx).data.opt_Owning();
        if (!owning) {
            continue;
        }
        auto state_it = ::std::find(owning->slots.begin(), owning->slots.end(), idx);
        if (state_it != owning->slots.end()) {
            if (scope_idx == target.idx) {
                return;
            }
            ASSERT_BUG(sp, !owning->is_temporary, "`super let` binding is already in a temporary scope");
            source = owning;
            owning->slots.erase(state_it);
            break;
        }
    }
    ASSERT_BUG(sp, source, "`super let` binding _" << idx << " has no lexical scope");

    auto dropIt = ::std::find_if(source->dropSlots.begin(), source->dropSlots.end(), [&](const ScopeDropSlot& slot) {
        return !slot.is_argument && slot.index == idx;
    });
    ASSERT_BUG(sp, dropIt != source->dropSlots.end(), "`super let` binding _" << idx << " has no scheduled drop");
    source->dropSlots.erase(dropIt);

    auto& target_owning = target_scope.data.as_Owning();
    ASSERT_BUG(sp, ::std::find(target_owning.slots.begin(), target_owning.slots.end(), idx) == target_owning.slots.end(), "Duplicate `super let` state owner for _" << idx);
    target_owning.slots.push_back(idx);
    target_owning.dropSlots.push_back(ScopeDropSlot{false, idx});
}

void MirBuilder::dropLvalue(const Span& sp, const ::MIR::LValue& value) {
    auto* state = getValStateMutP(sp, value);
    ASSERT_BUG(sp, state, "Dropping invalid value " << value);
    dropValueFromState(sp, *state, value.clone());
}

::MIR::LValue MirBuilder::new_temporary(const ::HIR::TypeData* ty) {
    unsigned int rv = output.locals.size();
    DEBUG("DEFINE (temp) _" << rv << ": " << ty);

    assert(output.locals.size() == slotStates.size());
    output.locals.push_back(ty);
    slotStates.push_back(VarState::make_Invalid(InvalidType::Uninit));
    assert(output.locals.size() == slotStates.size());

    ScopeDef* top_scope = nullptr;
    for (unsigned int i = scopeStack.size(); i--;) {
        auto idx = scopeStack[i];
        if (const auto* e = scopes.at(idx).data.opt_Owning()) {
            if (e->is_temporary) {
                top_scope = &scopes.at(idx);
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
    assert(top_scope);
    auto& tmp_scope = top_scope->data.as_Owning();
    assert(tmp_scope.is_temporary);
    tmp_scope.slots.push_back(rv);
    tmp_scope.dropSlots.push_back(ScopeDropSlot{false, rv});
    return ::MIR::LValue::newLocal(rv);
}

::MIR::LValue MirBuilder::lvalue_or_temp(const Span& sp, const ::HIR::TypeData* ty, ::MIR::RValue val) {
    TU_IFLET(::MIR::RValue, val, Use, e, return mv$(e);)
    else {
        auto temp = new_temporary(ty);
        push_stmt_assign(sp, temp.clone(), mv$(val));
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
        return new_temporary(ty);
    }
    auto rv = getResult(sp);
    TU_IFLET(::MIR::RValue, rv, Use, e, return mv$(e);)
    else {
        auto temp = new_temporary(ty);
        push_stmt_assign(sp, ::MIR::LValue(temp.clone()), mv$(rv));
        return temp;
    }
}

::MIR::Param MirBuilder::getResultInParam(const Span& sp, const ::HIR::TypeData* ty, bool allowMissingValue) {
    if (allowMissingValue && !block_active()) {
        return new_temporary(ty);
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
        auto temp = new_temporary(ty);
        push_stmt_assign(sp, ::MIR::LValue(temp.clone()), mv$(rv));
        return ::MIR::Param(mv$(temp));
    }
}

void MirBuilder::set_result(const Span& sp, ::MIR::RValue val) {
    if (resultValid) {
        BUG(sp, "Pushing a result over an existing result");
    }
    result = mv$(val);
    resultValid = true;
    DEBUG(result);
}

void MirBuilder::push_stmt_assign(const Span& sp, ::MIR::LValue dst, ::MIR::RValue val, bool update_dest_state /*=true*/) {
    DEBUG(dst << " = " << val);
    ASSERT_BUG(sp, blockActive, "Pushing statement with no active block");

    auto moved_param = [&](const ::MIR::Param& p) {
        if (const auto* e = p.opt_LValue()) {
            this->moved_lvalue(sp, *e);
        }
    };
    TU_MATCHA(
        (val),
        (e),
        (Use, this->moved_lvalue(sp, e);),
        (Constant, ),
        (SizedArray, moved_param(e.val);),
        (Borrow,
         if (e.type == ::HIR::BorrowType::Owned) {
             TODO(sp, "Move using &move");
             // Likely would require a marker that ensures that the memory isn't reused.
             this->moved_lvalue(sp, e.val);
         } else {
             // Doesn't move
         }),
        (Cast, this->moved_lvalue(sp, e.val);),
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
                 moved_param(e.val_l);
                 moved_param(e.val_r);
                 break;
         }),
        (UniOp, this->moved_lvalue(sp, e.val);),
        (
            DstMeta,
            // Doesn't move
        ),
        (
            DstPtr,
            // Doesn't move
        ),
        (MakeDst, moved_param(e.ptr_val); moved_param(e.meta_val);),
        (Tuple, for (const auto& val : e.vals) moved_param(val);),
        (Array, for (const auto& val : e.vals) moved_param(val);),
        (UnionVariant, moved_param(e.val);),
        (EnumVariant, for (const auto& val : e.vals) moved_param(val);),
        (Struct, for (const auto& val : e.vals) moved_param(val);)
    )

    // Drop target if populated
    if (update_dest_state) {
        mark_value_assigned(sp, dst);
    }
    this->push_stmt(sp, ::MIR::Statement::make_Assign({mv$(dst), mv$(val)}));
}

void MirBuilder::push_stmt_drop(const Span& sp, ::MIR::LValue val, unsigned int flag /*=~0u*/) {
    ASSERT_BUG(sp, blockActive, "Pushing statement with no active block");

    if (lvalue_is_copy(sp, val)) {
        // Don't emit a drop for Copy values
        return;
    }

    this->push_drop_terminator(sp, ::MIR::eDropKind::DEEP, mv$(val), flag);
}

void MirBuilder::push_stmt_drop_shallow(const Span& sp, ::MIR::LValue val, unsigned int flag /*=~0u*/) {
    ASSERT_BUG(sp, blockActive, "Pushing statement with no active block");

    // TODO: Ensure that the type is a Box?

    this->push_drop_terminator(sp, ::MIR::eDropKind::SHALLOW, mv$(val), flag);
}

void MirBuilder::push_drop_terminator(const Span& sp, ::MIR::eDropKind kind, ::MIR::LValue val, unsigned int flag) {
    ASSERT_BUG(sp, blockActive, "Dropping a value with no active block");

    const auto next_block = new_bb_unlinked();
    auto unwind = buildingCleanup
        ? ::MIR::UnwindAction::make_Terminate({})
        : make_unwind_action(sp, &val);
    endBlock(::MIR::Terminator::make_Drop({kind, mv$(val), flag, next_block, mv$(unwind)}));
    set_cur_block(next_block);
}

void MirBuilder::push_stmt_asm(const Span& sp, ::MIR::Statement::Data_Asm data) {
    ASSERT_BUG(sp, blockActive, "Pushing statement with no active block");

    // 1. Mark outputs as valid
    for (const auto& v : data.outputs) {
        mark_value_assigned(sp, v.second);
    }

    // 2. Push
    this->push_stmt(sp, ::MIR::Statement::make_Asm(mv$(data)));
}

void MirBuilder::push_stmt_set_dropflag_val(const Span& sp, unsigned int idx, bool value) {
    this->push_stmt(sp, ::MIR::Statement::make_SetDropFlag({idx, value, ~0u}));
}

void MirBuilder::push_stmt_set_dropflag_other(const Span& sp, unsigned int idx, unsigned int other) {
    this->push_stmt(sp, ::MIR::Statement::make_SetDropFlag({idx, false, other}));
}

void MirBuilder::push_stmt_set_dropflag_default(const Span& sp, unsigned int idx) {
    this->push_stmt(sp, ::MIR::Statement::make_SetDropFlag({idx, this->getDropFlagDefault(sp, idx), ~0u}));
}

void MirBuilder::push_stmt(const Span& sp, ::MIR::Statement stmt) {
    ASSERT_BUG(sp, blockActive, "Pushing statement with no active block");
    auto& blk = output.blocks.at(currentBlock);
    DEBUG("BB" << currentBlock << "/" << blk.statements.size() << " = " << stmt);
    blk.statements.push_back(mv$(stmt));
}

void MirBuilder::mark_value_assigned(const Span& sp, const ::MIR::LValue& dst) {
    if (dst.root.is_Return()) {
        ASSERT_BUG(sp, dst.wrappers.empty(), "Assignment to a component of the return value should be impossible.");
        return;
    }
    VarState* state_p = getValStateMutP(sp, dst, /*expect_valid=*/true);

    if (state_p) {
        TU_IFLET(VarState, (*state_p), Invalid, se, ASSERT_BUG(sp, se != InvalidType::Descoped, "Assining of descoped variable - " << dst);)
        dropValueFromState(sp, *state_p, dst.clone());
        auto new_state = VarState::make_Valid({});
        DEBUG("State " << dst << " " << *state_p << " => " << new_state);
        *state_p = std::move(new_state);
    } else {
        // Assigning into non-tracked locations still causes a drop
        auto state = VarState::make_Valid({});
        dropValueFromState(sp, state, dst.clone());
    }
}

void MirBuilder::raise_temporaries(const Span& sp, const ::MIR::LValue& val, const ScopeHandle& scope, bool to_above /*=false*/) {
    TRACE_FUNCTION_F(val);
    for (const auto& w : val.wrappers) {
        if (w.is_Index()) {
            // Raise index temporary
            raise_temporaries(sp, ::MIR::LValue::newLocal(w.as_Index()), scope, to_above);
        }
    }
    if (!val.root.is_Local()) {
        // No raising of these source values?
        return;
    }
    const auto idx = val.root.as_Local();
    bool is_temp = (idx >= firstTempIdx);
    /*
    if( !is_temp ) {
        return ;
    }
    */

    // Find controlling scope
    auto scope_it = scopeStack.rbegin();
    while (scope_it != scopeStack.rend()) {
        auto& scope_def = scopes.at(*scope_it);

        if (*scope_it == scope.idx && !to_above) {
            DEBUG(val << " defined in or above target (scope " << scope << ")");
        }

        TU_IFLET(
            ScopeType,
            scope_def.data,
            Owning,
            e,
            if (e.is_temporary == is_temp) {
                auto tmp_it = ::std::find(e.slots.begin(), e.slots.end(), idx);
                if (tmp_it != e.slots.end()) {
                    e.slots.erase(tmp_it);
                    auto dropIt = ::std::find_if(e.dropSlots.begin(), e.dropSlots.end(), [&](const ScopeDropSlot& slot) {
                        return !slot.is_argument && slot.index == idx;
                    });
                    ASSERT_BUG(sp, dropIt != e.dropSlots.end(), "Missing drop schedule for " << val);
                    e.dropSlots.erase(dropIt);
                    DEBUG("Raise slot " << idx << " from " << *scope_it);
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
        if (*scope_it == scope.idx) {
            DEBUG("Value " << val << " is defined above the target (scope " << scope << ")");
            return;
        }
        ++scope_it;
    }
    if (scope_it == scopeStack.rend()) {
        // Temporary wasn't defined in a visible scope?
        BUG(sp, val << " wasn't defined in a visible scope");
        return;
    }

    // If the definition scope was the target scope
    bool target_seen = false;
    if (*scope_it == scope.idx) {
        if (to_above) {
            // Want to shift to any above (but not including) it
            ++scope_it;
        } else {
            // Want to shift to it or above.
        }

        target_seen = true;
    } else {
        // Don't bother searching the original definition scope
        ++scope_it;
    }

    // Iterate stack until:
    // - The target scope is seen
    // - AND a scope was found for it
    for (; scope_it != scopeStack.rend(); ++scope_it) {
        auto& scope_def = scopes.at(*scope_it);
        DEBUG("> Cross " << *scope_it << " - " << scope_def.data.tag_str());

        if (*scope_it == scope.idx) {
            target_seen = true;
        }

        TU_MATCH_HDRA((scope_def.data), {)
        TU_ARMA(Owning, e) {
                if (target_seen && e.is_temporary == is_temp) {
                    e.slots.push_back(idx);
                    e.dropSlots.push_back(ScopeDropSlot{false, idx});
                    DEBUG("- to " << *scope_it);
                    return;
                }
            }
            TU_ARMA(Loop, sd_loop) {
                // If there is an exit state present, ensure that this variable is
                // present in that state (as invalid, as it can't have been valid
                // externally)
                if (sd_loop.exitStateValid) {
                    DEBUG("Adding " << val << " as unset to loop exit state");
                    auto v = sd_loop.exitState.states.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                    ASSERT_BUG(sp, v.second, "Raising " << val << " which already had a state entry");
                } else {
                    DEBUG("Crossing loop with no existing exit state");
                }
            }
            TU_ARMA(Split, sd_split) {
                // If the split has already registered an exit state, ensure that
                // this variable is present in it. (as invalid)
                if (sd_split.endStateValid) {
                    DEBUG("Adding " << val << " as unset to loop exit state");
                    auto v = sd_split.endState.states.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                    ASSERT_BUG(sp, v.second, "Raising " << val << " which already had a state entry");
                } else {
                    DEBUG("Crossing split with no existing end state");
                }

                // TODO: This should update the outer state to unset.
                auto& arm = sd_split.arms.back();
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

void MirBuilder::raise_temporaries(const Span& sp, const ::MIR::RValue& rval, const ScopeHandle& scope, bool to_above /*=false*/) {
    auto raise_vars = [&](const ::MIR::Param& p) {
        if (const auto* e = p.opt_LValue()) {
            this->raise_temporaries(sp, *e, scope, to_above);
        }
    };
    TU_MATCHA(
        (rval),
        (e),
        (Use, this->raise_temporaries(sp, e, scope, to_above);),
        (Constant, ),
        (SizedArray, raise_vars(e.val);),
        (Borrow,
         // TODO: Wait, is this valid?
         this->raise_temporaries(sp, e.val, scope, to_above);),
        (Cast, this->raise_temporaries(sp, e.val, scope, to_above);),
        (BinOp, raise_vars(e.val_l); raise_vars(e.val_r);),
        (UniOp, this->raise_temporaries(sp, e.val, scope, to_above);),
        (DstMeta, this->raise_temporaries(sp, e.val, scope, to_above);),
        (DstPtr, this->raise_temporaries(sp, e.val, scope, to_above);),
        (MakeDst, raise_vars(e.ptr_val); raise_vars(e.meta_val);),
        (Tuple, for (const auto& val : e.vals) raise_vars(val);),
        (Array, for (const auto& val : e.vals) raise_vars(val);),
        (UnionVariant, raise_vars(e.val);),
        (EnumVariant, for (const auto& val : e.vals) raise_vars(val);),
        (Struct, for (const auto& val : e.vals) raise_vars(val);)
    )
}

MirBuilder::SaveCodeProto MirBuilder::codeSaveStart() {
    TRACE_FUNCTION;
    // Push to the stack
    // Create a new block and link in
    static size_t s_next_index;
    SaveCodeProto rv;
    rv.index = s_next_index++;
    codeSaveStack.push_back(CodeSaveStackEnt{rv.index, {}});
    // If currently in a block, then go into a new one
    if (block_active()) {
        new_bb_linked();
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

void MirBuilder::insert_cloned(const Span& sp, const SavedCode& c, CloneMapper& mapper) {
    TRACE_FUNCTION;
    assert(block_active()); // Need an active block to start inserting
    if (!c.blocks.empty()) {
        struct Cloner: ::MIR::Cloner {
            CloneMapper& mapper;
            std::map<unsigned, unsigned> new_block_map;

            Cloner(const Span& sp, CloneMapper& mapper, HIR::TypeInterner& types)
                : ::MIR::Cloner(sp, types)
                , mapper(mapper)
            {
            }

            ::MIR::BasicBlockId map_bb_idx(::MIR::BasicBlockId idx) const override {
                auto it = new_block_map.find(idx);
                if (it != new_block_map.end()) {
                    return it->second;
                }
                return mapper.update_bb_ref(idx);
            }
        } cloner{sp, mapper, mResolve.crate.types};

        // Allocate new block IDs for all referenced blocks
        for (auto bbIdx : c.blocks) {
            cloner.new_block_map.insert(std::make_pair(bbIdx, new_bb_unlinked()));
        }
        // End the current block with a goto to the first block
        endBlock(::MIR::Terminator::make_Goto({cloner.new_block_map[c.blocks.front()]}));

        DEBUG("c.blocks = [" << c.blocks << "]");
        DEBUG("new_block_map = {" << cloner.new_block_map << "}");
        // Start inserting (and remapping)
        for (auto src_idx : c.blocks) {
            auto new_idx = cloner.new_block_map.at(src_idx);
            DEBUG("BB" << new_idx << " <= BB" << src_idx);
            const auto& src = output.blocks[src_idx];
            set_cur_block(new_idx);
            for (const auto& v : src.statements) {
                push_stmt(sp, cloner.cloneStmt(v));
            }
            endBlock(cloner.cloneTerm(src.terminator));
        }
        // Leave no active block
    }
}

void MirBuilder::set_cur_block(unsigned int new_block) {
    ASSERT_BUG(Span(), !blockActive, "Updating block when previous is active");
    ASSERT_BUG(Span(), new_block < output.blocks.size(), "Invalid block ID being started - " << new_block);
    ASSERT_BUG(Span(), output.blocks[new_block].terminator.is_Incomplete(), "Attempting to resume a completed block - BB" << new_block);
    // Record this new block in the save stack entries
    for (auto& v : codeSaveStack) {
        // Just in case a block is saved+resumed
        if (std::find(v.blocks.begin(), v.blocks.end(), new_block) == v.blocks.end()) {
            v.blocks.push_back(new_block);
        }
    }
    DEBUG("BB" << new_block << " START");
    currentBlock = new_block;
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

::MIR::BasicBlockId MirBuilder::pause_cur_block() {
    if (!blockActive) {
        BUG(Span(), "Pausing block when none active");
    }
    DEBUG("BB" << currentBlock << " PAUSE");
    blockActive = false;
    auto rv = currentBlock;
    currentBlock = 0;
    return rv;
}

::MIR::BasicBlockId MirBuilder::new_bb_linked() {
    auto rv = new_bb_unlinked();
    DEBUG("BB" << rv);
    endBlock(::MIR::Terminator::make_Goto(rv));
    set_cur_block(rv);
    return rv;
}

::MIR::BasicBlockId MirBuilder::new_bb_unlinked() {
    auto rv = output.blocks.size();
    DEBUG("BB" << rv);
    output.blocks.push_back({});
    output.blocks.back().is_cleanup = buildingCleanup;
    return rv;
}

unsigned int MirBuilder::new_drop_flag(bool defaultState) {
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

unsigned int MirBuilder::new_drop_flag_and_set(const Span& sp, bool set_state) {
    auto rv = new_drop_flag(!set_state);
    push_stmt_set_dropflag_val(sp, rv, set_state);
    return rv;
}

bool MirBuilder::getDropFlagDefault(const Span& sp, unsigned int idx) {
    return output.dropFlags.at(idx);
}

void MirBuilder::dropFlagAlias(unsigned int old_idx, unsigned int new_idx) {
    dropFlagAliases[old_idx].push_back(new_idx);
}

ScopeHandle MirBuilder::new_scope_var(const Span& sp) {
    unsigned int idx = scopes.size();
    scopes.push_back(ScopeDef{sp, ScopeType::make_Owning({false, {}, {}})});
    scopeStack.push_back(idx);
    DEBUG("START (var) scope " << idx);
    return ScopeHandle{*this, idx};
}

ScopeHandle MirBuilder::new_scope_temp(const Span& sp) {
    unsigned int idx = scopes.size();

    scopes.push_back(ScopeDef{sp, ScopeType::make_Owning({true, {}, {}})});
    scopeStack.push_back(idx);
    DEBUG("START (temp) scope " << idx);
    return ScopeHandle{*this, idx};
}

ScopeHandle MirBuilder::new_scope_split(const Span& sp) {
    unsigned int idx = scopes.size();
    scopes.push_back(ScopeDef{sp, ScopeType::make_Split({})});
    scopes.back().data.as_Split().arms.push_back({});
    scopeStack.push_back(idx);
    DEBUG("START (split) scope " << idx);
    return ScopeHandle{*this, idx};
}

ScopeHandle MirBuilder::new_scope_loop(const Span& sp) {
    unsigned int idx = scopes.size();
    scopes.push_back(ScopeDef{sp, ScopeType::make_Loop({})});
    scopes.back().data.as_Loop().entryBb = currentBlock;
    scopeStack.push_back(idx);
    DEBUG("START (loop) scope " << idx);
    return ScopeHandle{*this, idx};
}

ScopeHandle MirBuilder::new_scope_freeze(const Span& sp) {
    unsigned int idx = scopes.size();
    scopes.push_back(ScopeDef{sp, ScopeType::make_Freeze({})});
    scopeStack.push_back(idx);
    DEBUG("START (freeze) scope " << idx);
    return ScopeHandle{*this, idx};
}

void MirBuilder::terminate_scope(const Span& sp, ScopeHandle scope, bool emitCleanup /*=true*/) {
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

    auto& scope_def = scopes.at(scope.idx);
    //if( emit_cleanup ) {
    //    ASSERT_BUG( sp, scope_def.complete == false, "Terminating scope which is already terminated" );
    //}

    if (emitCleanup && scope_def.complete == false) {
        // 2. Emit drops for all non-moved variables (share with below)
        dropScopeValues(scope_def);

// Emit ScopeEnd for all controlled values
    }

    // 3. Pop scope (last because `drop_scope_values` uses the stack)
    scopeStack.pop_back();

    completeScope(scope_def);
}

void MirBuilder::raise_all(const Span& sp, ScopeHandle source, const ScopeHandle& target) {
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
    auto& src_scope_def = scopes.at(source.idx);

    ASSERT_BUG(sp, src_scope_def.data.is_Owning(), "Rasising scopes can only be done on temporaries (source)");
    ASSERT_BUG(sp, src_scope_def.data.as_Owning().is_temporary, "Rasising scopes can only be done on temporaries (source)");
    auto& src_list = src_scope_def.data.as_Owning().slots;
    for (auto idx : src_list) {
        DEBUG("> Raising " << ::MIR::LValue::newLocal(idx));
        assert(idx >= firstTempIdx);
    }

    // Seek up stack until the target scope is seen
    auto it = scopeStack.rbegin() + 1;
    for (; it != scopeStack.rend() && *it != target.idx; ++it) {
        auto& scope_def = scopes.at(*it);
        DEBUG("Through S" << *it << ": " << scope_def.data.tag_str());

        if (auto* sd_loop = scope_def.data.opt_Loop()) {
            if (sd_loop->exitStateValid) {
                DEBUG("Crossing loop with existing end state");
                // Insert these values as Invalid, both in the existing exit state, and in the changed list
                for (auto idx : src_list) {
                    auto v = sd_loop->exitState.states.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                    ASSERT_BUG(sp, v.second, "");
                }
            } else {
                DEBUG("Crossing loop with no end state");
            }

            for (auto idx : src_list) {
                auto v2 = sd_loop->changedSlots.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                ASSERT_BUG(sp, v2.second, "");
            }
        } else if (auto* sd_split = scope_def.data.opt_Split()) {
            if (sd_split->endStateValid) {
                DEBUG("Crossing split with existing end state");
                // Insert these indexes as Invalid
                for (auto idx : src_list) {
                    auto v = sd_split->endState.states.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                    ASSERT_BUG(sp, v.second, "");
                }
            } else {
                DEBUG("Crossing split with no end state");
            }

            // TODO: Insert current state in the current arm
            assert(!sd_split->arms.empty());
            auto& arm = sd_split->arms.back();
            for (auto idx : src_list) {
                arm.states.insert(::std::make_pair(idx, mv$(slotStates.at(idx))));
                slotStates.at(idx) = VarState(InvalidType::Uninit);
            }
        }
    }
    if (it == scopeStack.rend()) {
        BUG(sp, "Moving values to a scope not on the stack - scope " << target.idx);
    }
    auto& tgt_scope_def = scopes.at(target.idx);
    DEBUG("To S" << target.idx << ": " << tgt_scope_def.data.tag_str());
    ASSERT_BUG(sp, tgt_scope_def.data.is_Owning(), "Rasising scopes can only be done on temporaries (target)");
    ASSERT_BUG(sp, tgt_scope_def.data.as_Owning().is_temporary, "Rasising scopes can only be done on temporaries (target)");

    // Move all defined variables from one to the other
    auto& tgt_list = tgt_scope_def.data.as_Owning().slots;
    tgt_list.insert(tgt_list.end(), src_list.begin(), src_list.end());
    auto& src_drop_list = src_scope_def.data.as_Owning().dropSlots;
    auto& tgt_drop_list = tgt_scope_def.data.as_Owning().dropSlots;
    tgt_drop_list.insert(tgt_drop_list.end(), src_drop_list.begin(), src_drop_list.end());

    // Scope completed
    scopeStack.pop_back();
    src_scope_def.complete = true;
}

void MirBuilder::terminate_scope_early(const Span& sp, const ScopeHandle& scope, bool loop_exit /*=false*/) {
    TRACE_FUNCTION_F("EARLY scope " << scope.idx);

    // 1. Ensure that this block is in the stack
    auto it = ::std::find(scopeStack.begin(), scopeStack.end(), scope.idx);
    if (it == scopeStack.end()) {
        BUG(sp, "Early-terminating scope not on the stack");
    }
    unsigned int slot = it - scopeStack.begin();

    bool use_frozen_exit_state = false;
    for (unsigned int i = scopeStack.size(); i-- > slot;) {
        const auto* freeze = scopes.at(scopeStack[i]).data.opt_Freeze();
        use_frozen_exit_state |= freeze && !freeze->unfrozen;
    }
    ASSERT_BUG(sp, !use_frozen_exit_state || !frozenExitStateActive, "Nested frozen early-exit state");
    if (use_frozen_exit_state) {
        frozenExitStateActive = true;
        frozenExitSlotStates.clear();
        frozenExitArgStates.clear();
    }

    bool is_conditional = false;
    for (unsigned int i = scopeStack.size(); i-- > slot;) {
        auto idx = scopeStack[i];
        auto& scope_def = scopes.at(idx);

        if (idx == scope.idx) {
            // If this is exiting a loop, save the state so the variable state after the loop is known.
            if (loop_exit && scope_def.data.is_Loop()) {
                terminate_loop_early(sp, scope_def.data.as_Loop());
            }
        }

        // If a conditional block is hit, prevent full termination of the rest
        if (scope_def.data.is_Split() || scope_def.data.is_Loop()) {
            is_conditional = true;
        }

        if (!is_conditional) {
            DEBUG("Complete scope " << idx);
            dropScopeValues(scope_def);
            completeScope(scope_def);
        } else {
            // Mark patial within this scope?
            DEBUG("Drop part of scope " << idx);

            // Emit drops for dropped values within this scope
            dropScopeValues(scope_def);
            // Inform the scope that it's been early-exited
            TU_IFLET(ScopeType, scope_def.data, Split, e, e.arms.back().hasEarlyTerminated = true;)
        }
    }

    if (use_frozen_exit_state) {
        frozenExitSlotStates.clear();
        frozenExitArgStates.clear();
        frozenExitStateActive = false;
    }

}

namespace {
    static void merge_outer_validity(const Span& sp, MirBuilder& builder, unsigned int& old_flag, bool new_valid) {
        if (old_flag == ~0u) {
            if (!new_valid) {
                old_flag = builder.new_drop_flag_and_set(sp, false);
            }
        } else {
            builder.push_stmt_set_dropflag_val(sp, old_flag, new_valid);
        }
    }

    static void merge_outer_validity(const Span& sp, MirBuilder& builder, unsigned int& old_flag, unsigned int new_flag) {
        if (old_flag == new_flag) {
            return;
        }
        if (old_flag == ~0u) {
            if (builder.getDropFlagDefault(sp, new_flag)) {
                old_flag = new_flag;
            } else {
                old_flag = builder.new_drop_flag(true);
                builder.push_stmt_set_dropflag_other(sp, old_flag, new_flag);
            }
        } else {
            builder.push_stmt_set_dropflag_other(sp, old_flag, new_flag);
        }
    }

    static unsigned int merge_invalid_with_partial_outer(const Span& sp, MirBuilder& builder, unsigned int new_flag) {
        const auto outer_flag = builder.new_drop_flag(false);
        if (new_flag == ~0u) {
            builder.push_stmt_set_dropflag_val(sp, outer_flag, true);
        } else {
            builder.push_stmt_set_dropflag_other(sp, outer_flag, new_flag);
        }
        return outer_flag;
    }

    static void merge_state(const Span& sp, MirBuilder& builder, const ::MIR::LValue& lv, VarState& old_state, const VarState& new_state) {
        TRACE_FUNCTION_FR(lv << " : " << old_state << " <= " << new_state, lv << " : " << old_state);
        switch (old_state.tag()) {
            case VarState::TAGDEAD:
                throw "";
            case VarState::TAG_Invalid:
                switch (new_state.tag()) {
                    case VarState::TAGDEAD:
                        throw "";
                    case VarState::TAG_Invalid:
                        // Invalid->Invalid :: Choose the highest of the invalid types (TODO)
                        return;
                    case VarState::TAG_Valid:
                        // Allocate a drop flag
                        old_state = VarState::make_Optional(builder.new_drop_flag_and_set(sp, true));
                        return;
                    case VarState::TAG_Optional: {
                        // Was invalid, now optional.
                        auto flagIdx = new_state.as_Optional();
                        if (true || builder.getDropFlagDefault(sp, flagIdx) != false) {
                            auto new_flag = builder.new_drop_flag(false);
                            builder.push_stmt_set_dropflag_other(sp, new_flag, flagIdx);
                            old_state = VarState::make_Optional(new_flag);
                        } else {
                            old_state = VarState::make_Optional(flagIdx);
                        }
                        return;
                    }
                    case VarState::TAG_MovedOut: {
                        const auto& nse = new_state.as_MovedOut();

                        // Create a new state that is internally valid and uses the same drop flag
                        old_state = VarState::make_MovedOut({box$(old_state.clone()), nse.outer_flag});
                        auto& ose = old_state.as_MovedOut();
                        if (ose.outer_flag != ~0u) {
                            // If the flag's default isn't false, then create a new flag that does have such a default
                            // - Other arm (old_state) uses default, this arm (new_state) can be manipulated
                            if (builder.getDropFlagDefault(sp, ose.outer_flag) != false) {
                                auto new_flag = builder.new_drop_flag(false);
                                builder.push_stmt_set_dropflag_other(sp, new_flag, nse.outer_flag);
                                ose.outer_flag = new_flag;
                            }
                        } else {
                            // In the old arm, the container isn't valid. Create a drop flag with a default of false and set it to true
                            ose.outer_flag = builder.new_drop_flag(false);
                            builder.push_stmt_set_dropflag_val(sp, ose.outer_flag, true);
                        }

                        bool is_box = false;
                        builder.with_val_type(sp, lv, [&](const auto& ty) {
                            is_box = builder.is_type_owned_box(ty);
                        });
                        if (is_box) {
                            merge_state(sp, builder, ::MIR::LValue::newDeref(lv.clone()), *ose.inner_state, *nse.inner_state);
                        } else {
                            BUG(sp, "Handle MovedOut on non-Box");
                        }
                        return;
                    }
                    case VarState::TAG_Partial: {
                        const auto& nse = new_state.as_Partial();
                        bool is_enum = false;
                        builder.with_val_type(sp, lv, [&](const auto& ty) {
                            is_enum = ty->is_Path() && ty->as_Path().binding.is_Enum();
                        });
                        const auto outer_flag = is_enum ? merge_invalid_with_partial_outer(sp, builder, nse.outer_flag) : ~0u;

                        // Create a partial filled with Invalid
                        {
                            ::std::vector<VarState> inner;
                            inner.reserve(nse.inner_states.size());
                            for (size_t i = 0; i < nse.inner_states.size(); i++) {
                                inner.push_back(old_state.clone());
                            }
                            old_state = VarState::make_Partial({mv$(inner), outer_flag});
                        }
                        auto& ose = old_state.as_Partial();
                        if (is_enum) {
                            for (size_t i = 0; i < ose.inner_states.size(); i++) {
                                merge_state(sp, builder, ::MIR::LValue::newDowncast(lv.clone(), static_cast<unsigned int>(i)), ose.inner_states[i], nse.inner_states[i]);
                            }
                        } else {
                            for (unsigned int i = 0; i < ose.inner_states.size(); i++) {
                                merge_state(sp, builder, ::MIR::LValue::newField(lv.clone(), i), ose.inner_states[i], nse.inner_states[i]);
                            }
                        }
                    }
                        return;
                }
                break;
            // Valid <= ...
            case VarState::TAG_Valid:
                switch (new_state.tag()) {
                    case VarState::TAGDEAD:
                        throw "";
                    // Valid <= Invalid
                    case VarState::TAG_Invalid:
                        old_state = VarState::make_Optional(builder.new_drop_flag_and_set(sp, false));
                        return;
                    // Valid <= Valid
                    case VarState::TAG_Valid:
                        return;
                    // Valid <= Optional
                    case VarState::TAG_Optional: {
                        auto flagIdx = new_state.as_Optional();
                        // Was valid, now optional.
                        if (builder.getDropFlagDefault(sp, flagIdx) != true) {
                            // Allocate a new drop flag with a default state of `true` and set it to this flag?
                            auto new_flag = builder.new_drop_flag(true);
                            builder.push_stmt_set_dropflag_other(sp, new_flag, flagIdx);
                            old_state = VarState::make_Optional(new_flag);
                        } else {
                            old_state = VarState::make_Optional(new_state.as_Optional());
                        }
                        return;
                    }
                    // Valid <= MovedOut
                    case VarState::TAG_MovedOut: {
                        const auto& nse = new_state.as_MovedOut();

                        // Create a new state that is internally valid and uses the same drop flag
                        old_state = VarState::make_MovedOut({box$(VarState::make_Valid({})), nse.outer_flag});
                        auto& ose = old_state.as_MovedOut();
                        if (ose.outer_flag != ~0u) {
                            // If the flag's default isn't true, then create a new flag that does have such a default
                            // - Other arm (old_state) uses default, this arm (new_state) can be manipulated
                            if (builder.getDropFlagDefault(sp, ose.outer_flag) != true) {
                                auto new_flag = builder.new_drop_flag(true);
                                builder.push_stmt_set_dropflag_other(sp, new_flag, nse.outer_flag);
                                ose.outer_flag = new_flag;
                            }
                        } else {
                            // In both arms, the container is valid. No need for a drop flag
                        }

                        bool is_box = false;
                        builder.with_val_type(sp, lv, [&](const auto& ty) {
                            is_box = builder.is_type_owned_box(ty);
                        });

                        if (is_box) {
                            merge_state(sp, builder, ::MIR::LValue::newDeref(lv.clone()), *ose.inner_state, *nse.inner_state);
                        } else {
                            BUG(sp, "MovedOut on non-Box");
                        }
                        return;
                    }
                    // Valid <= Partial
                    case VarState::TAG_Partial: {
                        const auto& nse = new_state.as_Partial();
                        bool is_enum = false;
                        builder.with_val_type(sp, lv, [&](const auto& ty) {
                            is_enum = ty->is_Path() && ty->as_Path().binding.is_Enum();
                        });
                        unsigned int outer_flag = ~0u;
                        if (is_enum && nse.outer_flag != ~0u) {
                            merge_outer_validity(sp, builder, outer_flag, nse.outer_flag);
                        }

                        // Create a partial filled with Valid
                        {
                            ::std::vector<VarState> inner;
                            inner.reserve(nse.inner_states.size());
                            for (size_t i = 0; i < nse.inner_states.size(); i++) {
                                inner.push_back(VarState::make_Valid({}));
                            }
                            old_state = VarState::make_Partial({mv$(inner), outer_flag});
                        }
                        auto& ose = old_state.as_Partial();
                        if (is_enum) {
                            auto ilv = ::MIR::LValue::newDowncast(lv.clone(), 0);
                            for (size_t i = 0; i < ose.inner_states.size(); i++) {
                                merge_state(sp, builder, ilv, ose.inner_states[i], nse.inner_states[i]);
                                ilv.incDowncast();
                            }
                        } else {
                            auto ilv = ::MIR::LValue::newField(lv.clone(), 0);
                            for (unsigned int i = 0; i < ose.inner_states.size(); i++) {
                                merge_state(sp, builder, ilv, ose.inner_states[i], nse.inner_states[i]);
                                ilv.incField();
                            }
                        }
                    }
                        return;
                }
                break;
            // Optional <= ...
            case VarState::TAG_Optional:
                switch (new_state.tag()) {
                    case VarState::TAGDEAD:
                        throw "";
                    case VarState::TAG_Invalid:
                        builder.push_stmt_set_dropflag_val(sp, old_state.as_Optional(), false);
                        return;
                    case VarState::TAG_Valid:
                        builder.push_stmt_set_dropflag_val(sp, old_state.as_Optional(), true);
                        return;
                    case VarState::TAG_Optional:
                        if (old_state.as_Optional() != new_state.as_Optional()) {
                            builder.push_stmt_set_dropflag_other(sp, old_state.as_Optional(), new_state.as_Optional());
                        }
                        return;
                    case VarState::TAG_MovedOut: {
                        // Should become `MovedOut` with a flag
                        // - If this `MovedOut` has a flag, then propagate that into the `Optional`'s flag and reset
                        if (new_state.as_MovedOut().outer_flag != ~0u) {
                            if (old_state.as_Optional() != new_state.as_MovedOut().outer_flag) {
                                builder.push_stmt_set_dropflag_other(sp, old_state.as_Optional(), new_state.as_MovedOut().outer_flag);
                            }
                        }
                        // Create an old state that just wraps a copy of the `Optional`
                        old_state = VarState::make_MovedOut({std::make_unique<VarState>(old_state.clone()), old_state.as_Optional()});

                        bool is_box = false;
                        builder.with_val_type(sp, lv, [&](const auto& ty) {
                            is_box = builder.is_type_owned_box(ty);
                        });

                        if (is_box) {
                            merge_state(sp, builder, ::MIR::LValue::newDeref(lv.clone()), *old_state.as_MovedOut().inner_state, *new_state.as_MovedOut().inner_state);
                        } else {
                            BUG(sp, "MovedOut on non-Box");
                        }
                        return;
                    }
                    case VarState::TAG_Partial: {
                        const auto& nse = new_state.as_Partial();
                        bool is_enum = false;
                        builder.with_val_type(sp, lv, [&](const auto& ty) {
                            assert(!builder.is_type_owned_box(ty));
                            is_enum = ty->is_Path() && ty->as_Path().binding.is_Enum();
                        });
                        const auto old_optional_flag = old_state.as_Optional();

                        // Create a Partial filled with copies of the Optional
                        // TODO: This can lead to contradictions when one field is moved and another not.
                        // - Need to allocate a new drop flag and handle the case where old_state is the state before the
                        //   split (and hence the default state of this new drop flag has to be the original state)
                        //  > Could store reference to start BB and assign into it?
                        //  > Can't it not be from before the split, because that would be a move when not known-valid?
                        //  > Re-assign and partial drop.
                        {
                            ::std::vector<VarState> inner;
                            inner.reserve(nse.inner_states.size());
                            for (size_t i = 0; i < nse.inner_states.size(); i++) {
                                auto new_flag = builder.new_drop_flag(builder.getDropFlagDefault(sp, old_state.as_Optional()));
                                builder.dropFlagAlias(old_state.as_Optional(), new_flag);
                                inner.push_back(VarState::make_Optional(new_flag));
                            }
                            old_state = VarState::make_Partial({mv$(inner), is_enum ? old_optional_flag : ~0u});
                        }
                        auto& ose = old_state.as_Partial();
                        if (is_enum) {
                            if (nse.outer_flag == ~0u) {
                                merge_outer_validity(sp, builder, ose.outer_flag, true);
                            } else {
                                merge_outer_validity(sp, builder, ose.outer_flag, nse.outer_flag);
                            }
                        }
                        // Propagate to inners
                        if (is_enum) {
                            for (size_t i = 0; i < ose.inner_states.size(); i++) {
                                merge_state(sp, builder, ::MIR::LValue::newDowncast(lv.clone(), static_cast<unsigned int>(i)), ose.inner_states[i], nse.inner_states[i]);
                            }
                        } else {
                            for (unsigned int i = 0; i < ose.inner_states.size(); i++) {
                                merge_state(sp, builder, ::MIR::LValue::newField(lv.clone(), i), ose.inner_states[i], nse.inner_states[i]);
                            }
                        }
                        return;
                    }
                }
                break;
            case VarState::TAG_MovedOut: {
                auto& ose = old_state.as_MovedOut();
                bool is_box = false;
                builder.with_val_type(sp, lv, [&](const auto& ty) {
                    is_box = builder.is_type_owned_box(ty);
                });
                if (!is_box) {
                    BUG(sp, "MovedOut on non-Box");
                }
                switch (new_state.tag()) {
                    case VarState::TAGDEAD:
                        throw "";
                    case VarState::TAG_Invalid:
                    case VarState::TAG_Valid: {
                        bool is_valid = new_state.is_Valid();
                        if (ose.outer_flag == ~0u) {
                            // If not valid in new arm, then the outer state is conditional
                            if (!is_valid) {
                                ose.outer_flag = builder.new_drop_flag(true);
                                builder.push_stmt_set_dropflag_val(sp, ose.outer_flag, false);
                            }
                        } else {
                            builder.push_stmt_set_dropflag_val(sp, ose.outer_flag, is_valid);
                        }

                        merge_state(sp, builder, ::MIR::LValue::newDeref(lv.clone()), *ose.inner_state, new_state);
                        return;
                    }
                    case VarState::TAG_Optional: {
                        const auto& nse = new_state.as_Optional();
                        if (ose.outer_flag == ~0u) {
                            if (!builder.getDropFlagDefault(sp, nse)) {
                                // Default wasn't true, need to make a new flag that does have a default of true
                                auto new_flag = builder.new_drop_flag(true);
                                builder.push_stmt_set_dropflag_other(sp, new_flag, nse);
                                ose.outer_flag = new_flag;
                            } else {
                                ose.outer_flag = nse;
                            }
                        } else {
                            // In this arm, assign the outer state to this drop flag
                            builder.push_stmt_set_dropflag_other(sp, ose.outer_flag, nse);
                        }
                        merge_state(sp, builder, ::MIR::LValue::newDeref(lv.clone()), *ose.inner_state, new_state);
                        return;
                    }
                    case VarState::TAG_MovedOut: {
                        const auto& nse = new_state.as_MovedOut();

                        if (ose.outer_flag == ~0u) {
                            ose.outer_flag = nse.outer_flag;
                        } else {
                            builder.push_stmt_set_dropflag_other(sp, ose.outer_flag, nse.outer_flag);
                        }
                        merge_state(sp, builder, ::MIR::LValue::newDeref(lv.clone()), *ose.inner_state, *nse.inner_state);
                        return;
                    }
                    case VarState::TAG_Partial:
                        BUG(sp, "MovedOut->Partial not valid");
                }
                break;
            }
            case VarState::TAG_Partial: {
                auto& ose = old_state.as_Partial();
                bool is_enum = false;
                builder.with_val_type(sp, lv, [&](const auto& ty) {
                    assert(!builder.is_type_owned_box(ty));
                    is_enum = ty->is_Path() && ty->as_Path().binding.is_Enum();
                });
                // Need to tag for conditional shallow drop? Or just do that at the end of the split?
                // - End of the split means that the only optional state is outer drop.
                switch (new_state.tag()) {
                    case VarState::TAGDEAD:
                        throw "";
                    case VarState::TAG_Invalid:
                    case VarState::TAG_Valid:
                    case VarState::TAG_Optional:
                        if (is_enum) {
                            if (new_state.is_Invalid()) {
                                merge_outer_validity(sp, builder, ose.outer_flag, false);
                            } else if (new_state.is_Valid()) {
                                merge_outer_validity(sp, builder, ose.outer_flag, true);
                            } else {
                                merge_outer_validity(sp, builder, ose.outer_flag, new_state.as_Optional());
                            }
                            for (size_t i = 0; i < ose.inner_states.size(); i++) {
                                merge_state(sp, builder, ::MIR::LValue::newDowncast(lv.clone(), static_cast<unsigned int>(i)), ose.inner_states[i], new_state);
                            }
                        } else {
                            for (unsigned int i = 0; i < ose.inner_states.size(); i++) {
                                merge_state(sp, builder, ::MIR::LValue::newField(lv.clone(), i), ose.inner_states[i], new_state);
                            }
                        }
                        return;
                    case VarState::TAG_MovedOut:
                        BUG(sp, "Partial->MovedOut not valid");
                    case VarState::TAG_Partial: {
                        const auto& nse = new_state.as_Partial();
                        ASSERT_BUG(sp, ose.inner_states.size() == nse.inner_states.size(), "Partial->Partial with mismatched sizes - " << old_state << " <= " << new_state);
                        if (is_enum) {
                            if (nse.outer_flag == ~0u) {
                                merge_outer_validity(sp, builder, ose.outer_flag, true);
                            } else {
                                merge_outer_validity(sp, builder, ose.outer_flag, nse.outer_flag);
                            }
                            for (size_t i = 0; i < ose.inner_states.size(); i++) {
                                merge_state(sp, builder, ::MIR::LValue::newDowncast(lv.clone(), static_cast<unsigned int>(i)), ose.inner_states[i], nse.inner_states[i]);
                            }
                        } else {
                            for (unsigned int i = 0; i < ose.inner_states.size(); i++) {
                                merge_state(sp, builder, ::MIR::LValue::newField(lv.clone(), i), ose.inner_states[i], nse.inner_states[i]);
                            }
                        }
                    }
                        return;
                }
            } break;
        }
        BUG(sp, "Unhandled combination - " << old_state.tag_str() << " and " << new_state.tag_str());
    }
}

void MirBuilder::terminate_loop_early(const Span& sp, ScopeType::Data_Loop& sd_loop) {
    if (sd_loop.exitStateValid) {
        // Insert copies of parent state for newly changed values
        // and Merge all changed values
        auto merge_list = [sp, this](const auto& changed, auto& exitStates, ::std::function<::MIR::LValue(unsigned)> val_cb, auto type) {
            for (const auto& ent : changed) {
                auto idx = ent.first;
                auto it = exitStates.find(idx);
                if (it == exitStates.end()) {
                    it = exitStates.insert(::std::make_pair(idx, ent.second.clone())).first;
                }
                auto& old_state = it->second;
                merge_state(sp, *this, val_cb(idx), old_state, getSlotState(sp, idx, type));
            }
        };
        merge_list(sd_loop.changedSlots, sd_loop.exitState.states, ::MIR::LValue::newLocal, SlotType::Local);
        merge_list(sd_loop.changedArgs, sd_loop.exitState.arg_states, [](auto v) {
            return ::MIR::LValue::newArgument(v);
        }, SlotType::Argument);
    } else {
        auto init_list = [sp, this](const auto& changed, auto& exitStates, auto type) {
            for (const auto& ent : changed) {
                DEBUG("Slot(" << ent.first << ") = " << ent.second);
                auto idx = ent.first;
                exitStates.insert(::std::make_pair(idx, getSlotState(sp, idx, type).clone()));
            }
        };
        // Obtain states of changed variables/temporaries
        init_list(sd_loop.changedSlots, sd_loop.exitState.states, SlotType::Local);
        init_list(sd_loop.changedArgs, sd_loop.exitState.arg_states, SlotType::Argument);
        sd_loop.exitStateValid = true;
    }
}

void MirBuilder::merge_split_lists(const Span& sp, const ScopeHandle& handle, const ::std::map<unsigned int, VarState>& states, ::std::map<unsigned int, VarState>& endStates, MirBuilder::SlotType type) {
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
        auto& out_state = ent.second;

        // Merge the states
        auto it = states.find(idx);
        const auto& src_state = (it != states.end() ? it->second : this->getSlotState(sp, idx, type, &handle));

        auto lv = (type == SlotType::Local ? ::MIR::LValue::newLocal(idx) : ::MIR::LValue::newArgument(idx));
        merge_state(sp, *this, mv$(lv), out_state, src_state);
    }
}

void MirBuilder::endSplitArm(const Span& sp, const ScopeHandle& handle, bool reachable, bool early /*=false*/) {
    ASSERT_BUG(sp, handle.idx < scopes.size(), "Handle passed to end_split_arm is invalid");
    auto& sd = scopes.at(handle.idx);
    ASSERT_BUG(sp, sd.data.is_Split(), "Ending split arm on non-Split arm - " << sd.data.tag_str());
    auto& sd_split = sd.data.as_Split();
    ASSERT_BUG(sp, !sd_split.arms.empty(), "Split arm list is empty (impossible)");

    // If this is not at the top of the stack (if there are other splits in the way), then get state from them
    for (auto v : ::reverse(scopeStack)) {
        if (v == handle.idx) {
            break;
        }

        // If this stack entry is a Split, get the current values and add them to `sd_split`
        if (const auto* other_split = scopes.at(v).data.opt_Split()) {
            for (auto& s : other_split->arms.back().states) {
                DEBUG("In scope " << handle.idx << " _" << s.first << " = " << s.second << " (from scope " << v << ")");
                sd_split.arms.back().states[s.first] = s.second.clone();
            }
            for (auto& s : other_split->arms.back().arg_states) {
                DEBUG("In scope " << handle.idx << " a" << s.first << " = " << s.second << " (from scope " << v << ")");
                sd_split.arms.back().arg_states[s.first] = s.second.clone();
            }
        }
    }

    TRACE_FUNCTION_F("end split scope " << handle.idx << " arm " << (sd_split.arms.size() - 1) << (reachable ? " reachable" : "") << (early ? " early" : ""));
    if (reachable) {
        ASSERT_BUG(sp, blockActive, "Block must be active when ending a reachable split arm");
    }

    auto& this_arm_state = sd_split.arms.back();
    this_arm_state.alwaysEarlyTerminated = /*sd_split.arms.back().has_early_terminated &&*/ !reachable;

    if (sd_split.endStateValid) {
        if (reachable) {
            DEBUG("Reachable w/ end state, merging");

            merge_split_lists(sp, handle, this_arm_state.states, sd_split.endState.states, SlotType::Local);
            merge_split_lists(sp, handle, this_arm_state.arg_states, sd_split.endState.arg_states, SlotType::Argument);
        } else {
            DEBUG("Unreachable, not merging");
        }
    } else {
        if (reachable) {
            DEBUG("Reachable w/ no end state, setting");
            // Clone this arm's state
            for (auto& ent : this_arm_state.states) {
                DEBUG("State _" << ent.first << " = " << ent.second);
                sd_split.endState.states.insert(::std::make_pair(ent.first, ent.second.clone()));
            }
            for (auto& ent : this_arm_state.arg_states) {
                DEBUG("State a" << ent.first << " = " << ent.second);
                sd_split.endState.arg_states.insert(::std::make_pair(ent.first, ent.second.clone()));
            }
            sd_split.endStateValid = true;
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
        for (auto& ent : sd_split.condState.states) {
            DEBUG("Condition State _" << ent.first << " = " << ent.second);
            arm.states.insert(::std::make_pair(ent.first, ent.second.clone()));
        }
        for (auto& ent : sd_split.condState.arg_states) {
            DEBUG("Condition State a" << ent.first << " = " << ent.second);
            arm.arg_states.insert(::std::make_pair(ent.first, ent.second.clone()));
        }
        sd_split.arms.push_back(mv$(arm));
    }
}

void MirBuilder::endSplitArmEarly(const Span& sp) {
    TRACE_FUNCTION_F("");
    size_t i = scopeStack.size();
    // Terminate every sequence of owning scopes
    while (i-- && scopes.at(scopeStack[i]).data.is_Owning()) {
        auto& scope_def = scopes[scopeStack[i]];
        // Fully drop the scope
        DEBUG("Complete scope " << scopeStack[i]);
        dropScopeValues(scope_def);
        completeScope(scope_def);
    }

    if (i < scopeStack.size()) {
        if (scopes.at(scopeStack[i]).data.is_Split()) {
            DEBUG("Early terminate split scope " << scopeStack.back());
            auto& sd = scopes[scopeStack[i]];
            auto& sd_split = sd.data.as_Split();
            sd_split.arms.back().hasEarlyTerminated = true;

            // TODO: Create drop flags if required?
        }
        // TODO: What if this is a loop?
    }
}

void MirBuilder::endSplitCondition(const Span& sp, const ScopeHandle& handle) {
    ASSERT_BUG(sp, handle.idx < scopes.size(), "Handle passed to end_split_arm is invalid");
    auto& sd = scopes.at(handle.idx);
    ASSERT_BUG(sp, sd.data.is_Split(), "Ending split arm on non-Split arm - " << sd.data.tag_str());
    auto& sd_split = sd.data.as_Split();
    ASSERT_BUG(sp, !sd_split.arms.empty(), "Split arm list is empty (impossible)");

    const auto& this_arm_state = sd_split.arms.back();

    DEBUG("Split condition clause end (scope " << handle.idx << "): merging");

    merge_split_lists(sp, handle, this_arm_state.states, sd_split.condState.states, SlotType::Local);
    merge_split_lists(sp, handle, this_arm_state.arg_states, sd_split.condState.arg_states, SlotType::Argument);
}

void MirBuilder::unfreeze_scope(const Span& sp, const ScopeHandle& handle) {
    ASSERT_BUG(sp, handle.idx < scopes.size(), "Handle passed to `unfreeze_scope` is invalid");
    auto& sd = scopes.at(handle.idx);
    ASSERT_BUG(sp, sd.data.is_Freeze(), "Handle passed to `unfreeze_scope` was not a freeze,  - " << sd.data.tag_str());
    auto& sd_e = sd.data.as_Freeze();

    DEBUG("Unfreeze scope " << handle.idx);
    sd_e.unfrozen = true;
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

void MirBuilder::with_val_type(const Span& sp, const ::MIR::LValue& val, ::std::function<void(const ::HIR::TypeData*)> cb, const ::MIR::LValue::Wrapper* stop_wrapper /*=nullptr*/) const {
    ::HIR::TypeRef tmp;
    const ::HIR::TypeData* ty = nullptr;
    TU_MATCHA((val.root), (e), (Return, ty = retTy;), (Argument, ty = mArgs.at(e).second;), (Local, ty = output.locals.at(e);), (Static, TU_MATCHA((e.mData), (pe), (Generic, ASSERT_BUG(sp, pe.mParams.types.empty(), "Path params on static"); const auto& s = mResolve.crate.getStaticByPath(sp, pe.mPath); ty = s.mType;), (UfcsKnown, TODO(sp, "Static - UfcsKnown - " << e);), (UfcsUnknown, BUG(sp, "Encountered UfcsUnknown in Static - " << e);), (UfcsInherent, TODO(sp, "Static - UfcsInherent - " << e);))))
    assert(ty);
    for (const auto& w : val.wrappers) {
        if (&w == stop_wrapper) {
            stop_wrapper = nullptr; // Reset so the below bugcheck can work
            break;
        }
        const auto* currentTy = ty;
        ty = nullptr;
        //DEBUG(ty << " " << w);
        auto maybe_monomorph = [&](const ::HIR::GenericParams& params_def, const ::HIR::Path& p, const ::HIR::TypeData* t) -> const ::HIR::TypeData* {
            if (monomorphise_type_needed(t)) {
                tmp = MonomorphStatePtr(mResolve.crate.types, nullptr, &p.mData.as_Generic().mParams, nullptr).monomorph_type(sp, t);
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
                            TU_MATCHA((str.mData), (se), (Unit, BUG(sp, "Field on unit-like struct - " << currentTy);), (Tuple, ASSERT_BUG(sp, fieldIndex < se.size(), "Field index out of range in tuple-struct " << currentTy << " - " << fieldIndex << " > " << se.size()); const auto& fld = se[fieldIndex]; ty = maybe_monomorph(str.mParams, te.path, fld.ent);), (Named, ASSERT_BUG(sp, fieldIndex < se.size(), "Field index out of range in struct " << currentTy << " - " << fieldIndex << " > " << se.size()); const auto& fld = se[fieldIndex]; ty = maybe_monomorph(str.mParams, te.path, fld.ty);))
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
                        if (const auto* inner = this->is_type_owned_box(currentTy)) {
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
            TU_ARMA(Downcast, variant_index) {
            TU_MATCH_HDRA( (*currentTy), { )
            default:
                BUG(sp, "Downcast on unexpected type - " << currentTy);
                    TU_ARMA(Path, te) {
                        if (const auto* pbe = te.binding.opt_Enum()) {
                            const auto& enm = **pbe;
                            ASSERT_BUG(sp, enm.mData.is_Data(), "Downcast on non-data enum");
                            const auto& variants = enm.mData.as_Data();
                            ASSERT_BUG(sp, variant_index < variants.size(), "Variant index out of range");
                            const auto& variant = variants[variant_index];

                            ty = maybe_monomorph(enm.mParams, te.path, variant.type);
                        } else if (const auto* pbe = te.binding.opt_Union()) {
                            const auto& unm = **pbe;
                            ASSERT_BUG(sp, variant_index < unm.mVariants.size(), "Variant index out of range");
                            const auto& variant = unm.mVariants.at(variant_index);

                            ty = maybe_monomorph(unm.mParams, te.path, variant.ty);
                        } else {
                            BUG(sp, "Downcast on non-Enum/Union - " << currentTy << " for " << val);
                        }
                    }
            }
            }
        }
        assert(ty);
    }
    ASSERT_BUG(sp, !stop_wrapper, "A stop wrapper was passed, but not found");
    cb(ty);
}

bool MirBuilder::lvalue_is_copy(const Span& sp, const ::MIR::LValue& val) const {
    int rv = 0;
    with_val_type(sp, val, [&](const auto& ty) {
        DEBUG("[lvalue_is_copy] ty=" << ty);
        rv = (mResolve.type_is_copy(sp, ty) ? 2 : 1);
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
    for (auto scope_idx : ::reverse(scopeStack)) {
        // Is this supposed to only consider above a specified (likely split) scope?
        if (aboveScope) {
            // Once the scope is found, clear `above_scope` so subsequent iterations skip this check
            if (scope_idx == aboveScope->idx) {
                aboveScope = nullptr;
            }
            continue;
        }
        const auto& scope_def = scopes.at(scope_idx);
        TU_MATCH_HDRA( (scope_def.data), {)
        default:
            break;
            TU_ARMA(Owning, e) {
                if (type == SlotType::Local) {
                    auto it = ::std::find(e.slots.begin(), e.slots.end(), idx);
                    if (it != e.slots.end()) {
                        // State from an outer split belongs to the outer
                        // incarnation of this local.  Once its owning scope
                        // is reached, fall back to the local's base state.
                        goto out_of_loop;
                    }
                }
            }
            TU_ARMA(Split, e) {
                const auto& curArm = e.arms.back();
                const auto& list = (type == SlotType::Local ? curArm.states : curArm.arg_states);
                auto it = list.find(idx);
                if (it != list.end()) {
                    DEBUG("From scope " << scope_idx);
                    return it->second;
                }
            }
        }
    }

out_of_loop:
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
    for (auto scope_idx : ::reverse(scopeStack)) {
        auto& scope_def = scopes.at(scope_idx);
        TU_MATCH_HDRA( (scope_def.data), {)
        TU_ARMA(Owning, e) {
                if (type == SlotType::Local) // `Local` counts both variables and temporaries
                {
                    auto it = ::std::find(e.slots.begin(), e.slots.end(), idx);
                    if (it != e.slots.end()) {
                        goto out_of_loop; // `goto` to avoid issues with the loops in `TU_ARMA`
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
                            DEBUG("Split new (scope " << scope_idx << ")");
                            it = states.insert(::std::make_pair(idx, getSlotState(sp, idx, type).clone())).first;
                        } else {
                            DEBUG("Split existing (scope " << scope_idx << ")");
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
                    ERROR(sp, E0000, "Attempting to move/initialise a value where not allowed (across scope " << scope_idx << ")");
                }
            }
        }
    }
    // Label used because we need to break out of the loop and the `TU_ARMA`/`TU_MATCH_HDRA`
out_of_loop:
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
                    size_t n_flds = 0;
                    with_val_type(sp, lv, [&](const auto& ty) {
                        DEBUG("ty = " << ty);
                        if (const auto* e = ty->opt_Path()) {
                            ASSERT_BUG(sp, e->binding.is_Struct(), "");
                            const auto& str = *e->binding.as_Struct();
                            TU_MATCHA((str.mData), (se), (Unit, BUG(sp, "Field access of unit-like struct");), (Tuple, n_flds = se.size();), (Named, n_flds = se.size();))
                        } else if (const auto* e = ty->opt_Tuple()) {
                            n_flds = e->size();
                        } else if (const auto* e = ty->opt_Array()) {
                            ASSERT_BUG(sp, e->size.is_Known(), "Array size not known");
                            n_flds = e->size.as_Known();
                        } else {
                            TODO(sp, "Determine field count for " << ty);
                        }
                    }, &w);
                    ::std::vector<VarState> inner_vs;
                    inner_vs.reserve(n_flds);
                    for (size_t i = 0; i < n_flds; i++) {
                        inner_vs.push_back(tpl.clone());
                    }
                    ivs = VarState::make_Partial({mv$(inner_vs), ~0u});
                }
                vs = &ivs.as_Partial().inner_states.at(fieldIndex);
            }
            TU_ARMA(Deref, _e) {
                // A Box dereference is a move path: track its pointee separately so a
                // later shallow drop deallocates the Box without dropping moved data.
                bool is_box = false;
                if (this->mLangBox) {
                    with_val_type(sp, lv, [&](const auto& ty) {
                        DEBUG("ty = " << ty);
                        is_box = this->is_type_owned_box(ty);
                    }, &w);
                }

                if (is_box) {
                    if (!ivs.is_MovedOut()) {
                        ::std::vector<VarState> inner;
                        inner.push_back(VarState::make_Valid({}));
                        unsigned int dropFlag = (ivs.is_Optional() ? ivs.as_Optional() : ~0u);
                        ivs = VarState::make_MovedOut({box$(VarState::make_Valid({})), dropFlag});
                    }
                    vs = &*ivs.as_MovedOut().inner_state;
                } else {
                    return nullptr;
                }
            }
            TU_ARMA(Index, e) {
                return nullptr;
            }
            TU_ARMA(Downcast, variant_index) {
                if (!ivs.is_Partial()) {
                    ASSERT_BUG(sp, !ivs.is_MovedOut(), "Downcast of a MovedOut value");

                    size_t var_count = 0;
                    with_val_type(sp, lv, [&](const auto& ty) {
                        DEBUG("ty = " << ty);
                        ASSERT_BUG(sp, ty->is_Path(), "Downcast on non-Path type - " << ty);
                        const auto& pb = ty->as_Path().binding;
                        // TODO: What about unions?
                        // - Iirc, you can't move out of them so they will never have state mutated
                        if (pb.is_Enum()) {
                            const auto& enm = *pb.as_Enum();
                            var_count = enm.num_variants();
                        } else if (const auto* pbe = pb.opt_Union()) {
                            const auto& unm = **pbe;
                            var_count = unm.mVariants.size();
                        } else {
                            BUG(sp, "Downcast on non-Enum/Union - " << ty);
                        }
                    }, &w);

                    const auto outer_flag = ivs.is_Optional() ? ivs.as_Optional() : ~0u;
                    ::std::vector<VarState> inner;
                    for (size_t i = 0; i < var_count; i++) {
                        inner.push_back(VarState::make_Invalid(InvalidType::Uninit));
                    }
                    inner[variant_index] = mv$(ivs);
                    ivs = VarState::make_Partial({mv$(inner), outer_flag});
                }

                vs = &ivs.as_Partial().inner_states.at(variant_index);
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
        (Valid, vs = VarState::make_Invalid(InvalidType::Moved); push_stmt_drop(sp, mv$(lv));),
        (
            MovedOut, bool is_box = false; with_val_type(
                sp,
                lv,
                [&](const auto& ty) {
        is_box = this->is_type_owned_box(ty);
    }
            );
            if (is_box) {
                dropValueFromState(sp, *vse.inner_state, ::MIR::LValue::newDeref(lv.clone()));
                const auto outer_flag = vse.outer_flag;
                vs = VarState::make_Invalid(InvalidType::Moved);
                push_stmt_drop_shallow(sp, mv$(lv), outer_flag);
            } else { TODO(sp, ""); }
        ),
        (
            Partial, bool is_enum = false; bool is_union = false; with_val_type(
                sp,
                lv,
                [&](const auto& ty) {
        is_enum = ty->is_Path() && ty->as_Path().binding.is_Enum();
        is_union = ty->is_Path() && ty->as_Path().binding.is_Union();
    }
            );
            if (is_enum) {
                bool hasValidVariant = false;
                for (const auto& state : vse.inner_states) {
                    hasValidVariant |= !state.is_Invalid();
                }
                if (!hasValidVariant) {
                    return;
                }

                auto original_state = vs.clone();
                const auto outer_flag = vse.outer_flag;
                const auto next_bb = new_bb_unlinked();
                ::std::vector<::MIR::BasicBlockId> arms;
                ::std::vector<::MIR::BasicBlockId> cleanupBlocks;
                arms.reserve(vse.inner_states.size());
                cleanupBlocks.reserve(vse.inner_states.size());
                for (const auto& state : vse.inner_states) {
                    const auto cleanupBb = state.is_Invalid() ? next_bb : new_bb_unlinked();
                    arms.push_back(cleanupBb);
                    cleanupBlocks.push_back(cleanupBb);
                }
                endBlock(::MIR::Terminator::make_Switch({lv.clone(), mv$(arms), outer_flag, outer_flag == ~0u ? ~0u : next_bb}));

                const auto variant_count = original_state.as_Partial().inner_states.size();
                for (size_t i = 0; i < variant_count; i++) {
                    if (original_state.as_Partial().inner_states[i].is_Invalid()) {
                        continue;
                    }
                    set_cur_block(cleanupBlocks[i]);
                    vs = original_state.clone();
                    dropValueFromState(sp, vs.as_Partial().inner_states[i], ::MIR::LValue::newDowncast(lv.clone(), static_cast<unsigned int>(i)));
                    vs = VarState::make_Invalid(InvalidType::Moved);
                    endBlock(::MIR::Terminator::make_Goto(next_bb));
                }
                vs = VarState::make_Invalid(InvalidType::Moved);
                set_cur_block(next_bb);
            } else if (is_union) {
                // NOTE: Unions don't drop inner items.
                vs = VarState::make_Invalid(InvalidType::Moved);
            } else {
                for (size_t i = 0; i < vse.inner_states.size(); i++) {
                    dropValueFromState(sp, vse.inner_states[i], ::MIR::LValue::newField(lv.clone(), static_cast<unsigned int>(i)));
                }
                vs = VarState::make_Invalid(InvalidType::Moved);
            }
        ),
        (Optional, const auto flag = vse; vs = VarState::make_Invalid(InvalidType::Moved); push_stmt_drop(sp, mv$(lv), flag);)
    )
}

void MirBuilder::dropScopeValues(ScopeDef& sd) {
    TU_MATCHA(
        (sd.data),
        (e),
        (Owning,
         for (const auto& slot : ::reverse(e.dropSlots)) {
             const auto slot_type = slot.is_argument ? SlotType::Argument : SlotType::Local;
             auto lvalue = slot.is_argument
                 ? ::MIR::LValue::newArgument(slot.index)
                 : ::MIR::LValue::newLocal(slot.index);
             if (buildingCleanup) {
                 if (unwindConsumedValue && lvalue == *unwindConsumedValue) {
                     continue;
                 }
                 auto state = getSlotState(sd.span, slot.index, slot_type).clone();
                 DEBUG(lvalue << " - " << state);
                 dropValueFromState(sd.span, state, mv$(lvalue));
             } else {
                 auto& state = getSlotStateMut(sd.span, slot.index, slot_type);
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

void MirBuilder::moved_lvalue(const Span& sp, const ::MIR::LValue& lv) {
    if (!lvalue_is_copy(sp, lv)) {
        auto* vs_p = getValStateMutP(sp, lv);
        if (!vs_p) {
            ERROR(sp, E0000, "Attempting to move out of invalid slot - " << lv);
        }
        auto& vs = *vs_p;
        // TODO: If the current state is Optional, set the drop flag to 0
        auto new_state = VarState::make_Invalid(InvalidType::Moved);
        DEBUG("State " << lv << " " << vs << " => " << new_state);
        vs = std::move(new_state);
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

std::map<unsigned, MirBuilder::SavedActiveLocal> MirBuilder::getActiveLocals(const Span& sp, std::set<unsigned>& saved_drop_flags) const {
    TRACE_FUNCTION;
    std::map<unsigned, MirBuilder::SavedActiveLocal> rv;
    for (size_t i = 0; i < slotStates.size(); i++) {
        const auto& s = getSlotState(sp, i, SlotType::Local);
        TU_MATCH_HDRA( (s), {)
        default:
            DEBUG("_" << i << " : " << s);
            s.getUsedDropFlags(&saved_drop_flags);
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
    const auto was_building_cleanup = buildingCleanup;
    buildingCleanup = true;
    output.blocks.at(currentBlock).is_cleanup = true;
    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
        dropScopeValues(scopes.at(*it));
    }
    buildingCleanup = was_building_cleanup;
}

::MIR::UnwindAction MirBuilder::make_unwind_action(const Span& sp, const ::MIR::LValue* consumedValue) {
    if (buildingCleanup) {
        return ::MIR::UnwindAction::make_Terminate({});
    }

    const auto source_block = pause_cur_block();
    const auto cleanupBlock = new_bb_unlinked();
    set_cur_block(cleanupBlock);
    const auto* old_consumed_value = unwindConsumedValue;
    unwindConsumedValue = consumedValue;
    emitUnwindCleanup(sp);
    unwindConsumedValue = old_consumed_value;
    endBlock(::MIR::Terminator::make_UnwindResume({}));
    set_cur_block(source_block);
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
    TU_MATCHA((*this), (e), (Invalid, return VarState(e);), (Valid, return VarState(e);), (Optional, return VarState(e);), (MovedOut, return VarState::make_MovedOut({box$(e.inner_state->clone()), e.outer_flag});), (Partial, ::std::vector<VarState> n; n.reserve(e.inner_states.size()); for (const auto& a : e.inner_states) n.push_back(a.clone()); return VarState::make_Partial({mv$(n), e.outer_flag});))
    throw "";
}

bool VarState::operator==(const VarState& x) const {
    if (this->tag() != x.tag()) {
        return false;
    }
    TU_MATCHA((*this, x), (te, xe), (Invalid, return te == xe;), (Valid, return true;), (Optional, return te == xe;), (MovedOut, if (te.outer_flag != xe.outer_flag) return false; return *te.inner_state == *xe.inner_state;), (Partial, if (te.outer_flag != xe.outer_flag || te.inner_states.size() != xe.inner_states.size()) return false; for (unsigned int i = 0; i < te.inner_states.size(); i++) {
                  if (te.inner_states[i] != xe.inner_states[i]) {
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
        (MovedOut, os << "MovedOut("; if (e.outer_flag == ~0u) os << "-"; else os << "df" << e.outer_flag; os << " " << *e.inner_state << ")";),
        (Partial, os << "Partial("; if (e.outer_flag == ~0u) os << "-"; else os << "df" << e.outer_flag; os << ", [" << e.inner_states << "])";)
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
            if (ve.outer_flag != ~0u) {
                if (out) {
                    out->insert(ve.outer_flag);
                }
                rv = true;
            }
            for (const auto& vs : ve.inner_states) {
                rv |= vs.getUsedDropFlags(out);
            }
        }
        TU_ARMA(MovedOut, ve) {
            if (ve.outer_flag != ~0u) {
                if (out) {
                    out->insert(ve.outer_flag);
                }
                rv = true;
            }
            rv |= ve.inner_state->getUsedDropFlags(out);
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
    , split_slice(SIZE_MAX, SIZE_MAX) {
}
MirBuilder::ScopeDef::ScopeDef(const Span& span)
    : span(span) {
}
MirBuilder::ScopeDef::ScopeDef(const Span& span, ScopeType data)
    : span(span)
    , data(mv$(data)) {
}
/// Save the current state of aliases (see add_variable_alias)
MirBuilder::SavedAliases MirBuilder::save_aliases() const {
    SavedAliases rv;
    rv.set_aliases.reserve(variableAliases.size());
    for (const auto& v : variableAliases) {
        rv.set_aliases.push_back(v.second != MIR::LValue());
    }
    return rv;
}
void MirBuilder::restore_aliases(SavedAliases a) {
    assert(a.set_aliases.size() == variableAliases.size());
    for (size_t i = 0; i < a.set_aliases.size(); i++) {
        if (!a.set_aliases[i]) {
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
const MirBuilder::var_alias_t* MirBuilder::getVariableAlias(const Span& sp, unsigned idx) const {
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
    push_stmt_assign(sp, ifCondLval.clone(), mv$(val));
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
    if (x.is_split_slice()) {
        os << "[" << x.split_slice.first << "..-" << x.split_slice.second << "]";
    }
    return os;
}
